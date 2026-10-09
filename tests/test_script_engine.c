/* Tests d'intégration du vrai moteur Lua (script_engine.c) sans interface : bouchons pour la liaison (envois, signal
 * « received ») et l'état des macros (jeu fourni par le test). Le thread principal fait tourner une GMainLoop pendant le script. */

#include <glib.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <fcntl.h>
#include <libintl.h>
#include <locale.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <pty.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdarg.h>

#include "script_engine.h"
#include "link.h"
#include "macros_list.h"
#include "stubs.h"
#include "stub_macros.h"
#include "test_util.h"

static GThread *thread_principal;

/* ---- Interceptions (-Wl,--wrap=ioctl, -Wl,--wrap=getaddrinfo) ---------------- */

int __real_ioctl(int fd, unsigned long requete, ...);
int __real_getaddrinfo(const char *noeud, const char *service, const struct addrinfo *indices, struct addrinfo **res);

/* Une pty n'a pas de file d'émission (TIOCOUTQ y rend toujours 0) mais son tcflush(TCOFLUSH) jette ce que le maître n'a pas
 * encore reçu : TIOCOUTQ est simulé, comme le ferait un pilote série, par le nombre d'octets envoyés pas encore lus. */
static gint sortie_simulee = 0;		/* atomique : TIOCOUTQ simulé */
static gint sortie_en_attente = 0;	/* atomique : valeur rendue par TIOCOUTQ simulé */
static gint appels_resolution = 0;	/* atomique : appels à getaddrinfo */
static gint resolution_sur_principal = 0;	/* atomique : un nom (non numérique) a été résolu sur le thread principal */

int
__wrap_ioctl(int fd, unsigned long requete, ...)
{
	va_list ap;
	void *arg;

	va_start(ap, requete);
	arg = va_arg(ap, void *);
	va_end(ap);
	if(requete == TIOCOUTQ && g_atomic_int_get(&sortie_simulee))
	{
		*(int *)arg = MAX(g_atomic_int_get(&sortie_en_attente), 0);
		return 0;
	}
	return __real_ioctl(fd, requete, arg);
}

static gboolean
adresse_numerique(const char *noeud)
{
	guint8 tampon[sizeof(struct in6_addr)];

	return inet_pton(AF_INET, noeud, tampon) == 1 || inet_pton(AF_INET6, noeud, tampon) == 1;
}

/* Noms en « .invalid » : jamais résolus (réponse immédiate, sans réseau) */
int
__wrap_getaddrinfo(const char *noeud, const char *service, const struct addrinfo *indices, struct addrinfo **res)
{
	int ret;

	g_atomic_int_inc(&appels_resolution);
	if(noeud != NULL && !adresse_numerique(noeud) && g_thread_self() == thread_principal)
		g_atomic_int_set(&resolution_sur_principal, 1);
	/* localhost : ::1 puis 127.0.0.1, toujours (repli d'adresse déterministe) */
	if(test_localhost_deterministe(__real_getaddrinfo, noeud, service, indices, res, &ret))
		return ret;
	if(noeud != NULL && g_str_has_suffix(noeud, ".invalid"))
		return EAI_NONAME;
	return __real_getaddrinfo(noeud, service, indices, res);
}
static GMainLoop *boucle = NULL;
static GString *journal;
static gboolean fini;
static guint appels_journal;
static GtScriptEnd derniere_fin;
static gint64 fin_us;

static void
sur_journal(const char *texte, gpointer data)
{
	appels_journal++;
	g_string_append(journal, texte);
}

static void
sur_fin(GtScriptEnd fin_script, gpointer data)
{
	derniere_fin = fin_script;
	fin_us = g_get_monotonic_time();
	fini = TRUE;
	if(boucle != NULL)
		g_main_loop_quit(boucle);
}

static gboolean
sur_delai(gpointer data)
{
	g_main_loop_quit(boucle);
	return G_SOURCE_REMOVE;
}

/* Lance le script et fait tourner la boucle jusqu'à sa fin (TRUE) ou jusqu'au délai (FALSE). */
static gboolean
executer(const gchar *script, guint delai_ms)
{
	guint delai;

	g_string_truncate(journal, 0);
	appels_journal = 0;
	fini = FALSE;
	boucle = g_main_loop_new(NULL, FALSE);
	delai = g_timeout_add(delai_ms, sur_delai, NULL);
	script_engine_run(script);
	g_main_loop_run(boucle);
	if(fini)
		g_source_remove(delai);
	g_main_loop_unref(boucle);
	boucle = NULL;
	return fini;
}

static guint
compter_descripteurs(void)
{
	GDir *d = g_dir_open("/proc/self/fd", 0, NULL);
	guint n = 0;

	g_assert_nonnull(d);
	while(g_dir_read_name(d) != NULL)
		n++;
	g_dir_close(d);
	return n;
}

/* ---- C1 : erreur avec des ports ouverts ------------------------------- */

static void
test_erreur_avec_ports_ouverts(void)
{
	guint avant;

	/* Le contexte par défaut crée son eventfd au premier usage : on le fait avant de compter, pour ne mesurer que les ports. */
	g_main_context_default();
	avant = compter_descripteurs();
	stub_reset();
	g_assert_true(executer("assert(gtkterm.open(1, 'tcp_server', 0))\n"
	                       "assert(gtkterm.open(2, 'tcp_server', 0))\n"
	                       "error('boum')\n", 5000));
	g_assert_nonnull(strstr(journal->str, "boum"));
	g_assert_cmpuint(compter_descripteurs(), ==, avant);
}

/* ---- C10, C11, M27 : thread principal et ordre ------------------------ */

static void
installer_macro_a(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint v;

	gt_macro_set_append(set, "A", "", "A", "");
	v = gt_macro_set_append(set, "V", "", "V%d", "");
	gt_macro_set_set_arg(set, v, 0, "42");
	stub_set_macro_set(set);
}

static void
test_envois_sur_le_thread_principal_et_dans_l_ordre(void)
{
	GByteArray *octets;

	stub_reset();
	installer_macro_a();
	g_assert_true(executer("assert(gtkterm.send_macro('A'))\n"
	                       "assert(gtkterm.send(0, 'B'))\n", 5000));
	octets = stub_sent();
	g_assert_cmpuint(octets->len, ==, 2);
	g_assert_cmpmem(octets->data, octets->len, "AB", 2);
	g_assert_true(stub_send_thread() == thread_principal);
	g_assert_cmpint(stub_last_origin(), ==, GT_SEND_LUA);
}

static void
test_get_macros_sur_le_thread_principal(void)
{
	stub_reset();
	installer_macro_a();
	g_assert_true(executer("local m = gtkterm.get_macros()\n"
	                       "gtkterm.log(#m .. ' ' .. m[1].label)\n", 5000));
	g_assert_nonnull(strstr(journal->str, "2 A"));	/* deux macros : A et V */
	g_assert_true(stub_macros_thread() == thread_principal);
}

static void
test_macro_absente(void)
{
	stub_reset();
	installer_macro_a();
	g_assert_true(executer("local ok, err = gtkterm.send_macro('absente')\n"
	                       "gtkterm.log(tostring(ok) .. ' ' .. err)\n", 5000));
	g_assert_nonnull(strstr(journal->str, "false macro not found"));
}

static void
test_get_lists(void)
{
	GtMacroSet *set = gt_macro_set_new();
	GtMacroLists *l = gt_macro_lists_new();

	gt_macro_lists_add(l, "Mode", "Marche", "M");
	gt_macro_set_take_lists(set, l);
	stub_set_macro_set(set);
	g_assert_true(executer("local l = gtkterm.get_lists()\n"
	                       "gtkterm.log(l[1].name .. ' ' .. l[1].entries[1].value)\n", 5000));
	g_assert_nonnull(strstr(journal->str, "Mode M"));
	g_assert_true(stub_macros_thread() == thread_principal);
}

/* Sans argument Lua, les arguments enregistrés ; avec, ceux fournis ; par indice (0-based) comme par libellé. */
static void
test_send_macro_arguments(void)
{
	GByteArray *octets;

	stub_reset();
	installer_macro_a();
	g_assert_true(executer("assert(gtkterm.send_macro('V'))\n"
	                       "assert(gtkterm.send_macro('V', 7))\n"
	                       "assert(gtkterm.send_macro(1))\n", 5000));
	octets = stub_sent();
	g_assert_cmpmem(octets->data, octets->len, "V42V7V42", 8);
	g_assert_true(stub_send_thread() == thread_principal);
}

/* ---- M26 : wait_for binaire ------------------------------------------- */

static gboolean
injecter_binaire(gpointer data)
{
	stub_inject_rx("\x01\x00\x02OK", 5);
	return G_SOURCE_REMOVE;
}

static void
test_wait_for_binaire(void)
{
	stub_reset();
	g_timeout_add(200, injecter_binaire, NULL);
	g_assert_true(executer("local r = gtkterm.wait_for(0, '\\0\\2', 2000)\n"
	                       "gtkterm.log('longueur ' .. (r and #r or -1))\n", 5000));
	g_assert_nonnull(strstr(journal->str, "longueur 3"));
}

/* ---- C2 : pair TCP fermé sur un port Lua ------------------------------- */

static int ecoute_c2 = -1;

static gboolean
fermer_le_pair(gpointer data)
{
	int pair = accept(ecoute_c2, NULL, NULL);

	if(pair >= 0)
		close(pair);
	return G_SOURCE_REMOVE;
}

static void
test_pair_tcp_ferme(void)
{
	struct sockaddr_in adresse;
	socklen_t taille = sizeof(adresse);
	gchar *script;

	ecoute_c2 = socket(AF_INET, SOCK_STREAM, 0);
	memset(&adresse, 0, sizeof(adresse));
	adresse.sin_family = AF_INET;
	adresse.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	g_assert_cmpint(bind(ecoute_c2, (struct sockaddr *)&adresse, sizeof(adresse)), ==, 0);
	g_assert_cmpint(listen(ecoute_c2, 1), ==, 0);
	g_assert_cmpint(getsockname(ecoute_c2, (struct sockaddr *)&adresse, &taille), ==, 0);

	script = g_strdup_printf("assert(gtkterm.open(1, 'tcp_client', '127.0.0.1', %u))\n"
	                         "gtkterm.sleep(400)\n"
	                         "local r\n"
	                         "for i = 1, 3 do r = gtkterm.send(1, 'abc'); gtkterm.sleep(50) end\n"
	                         "gtkterm.log('envoi ' .. tostring(r))\n", ntohs(adresse.sin_port));
	g_timeout_add(150, fermer_le_pair, NULL);
	g_assert_true(executer(script, 5000));
	g_assert_nonnull(strstr(journal->str, "envoi false"));
	g_free(script);
	close(ecoute_c2);
}

/* ---- M30 : exécutions successives ------------------------------------- */

static void
test_executions_successives(void)
{
	g_assert_true(executer("gtkterm.log('un')\n", 5000));
	g_assert_true(executer("gtkterm.log('deux')\n", 5000));
	g_assert_false(script_engine_is_running());
}

/* ---- M9 : nombres Lua indépendants de la langue ----------------------- */

static void
test_nombres_lua_sous_locale_fr(void)
{
	gchar *ancienne = g_strdup(setlocale(LC_ALL, NULL));

	if(setlocale(LC_ALL, "fr_FR.UTF-8") == NULL)
	{
		g_free(ancienne);
		g_test_skip("locale fr_FR.UTF-8 indisponible");
		return;
	}
	g_assert_true(executer("gtkterm.log(tostring(3.5))\n", 5000));
	setlocale(LC_ALL, ancienne);
	g_free(ancienne);
	g_assert_nonnull(strstr(journal->str, "3.5"));
}

/* ---- M29 : l'exemple 10 contre un équipement simulé sur paire pty ---- */

static void
repondre(int maitre, const gchar *ligne)
{
	gchar *reponse = NULL;
	gint numero;
	gchar hex[256];

	if(g_strcmp0(ligne, "VERSION") == 0)
		reponse = g_strdup("VER: 1.2.3\r\n");
	else if(g_strcmp0(ligne, "BOOTLOADER") == 0)
		reponse = g_strdup("READY\r\n");
	else if(sscanf(ligne, "PAGE %d %255s", &numero, hex) == 2)
	{
		guint somme = 0;

		for(gsize i = 0; hex[i] != '\0' && hex[i + 1] != '\0'; i += 2)
		{
			gchar octet[3] = { hex[i], hex[i + 1], '\0' };
			somme += (guint)strtoul(octet, NULL, 16);
		}
		reponse = g_strdup_printf("ACK %02X\r\n", somme % 256);
	}
	else if(g_strcmp0(ligne, "COMMIT") == 0 || g_strcmp0(ligne, "ABORT") == 0)
		reponse = g_strdup("OK\r\n");
	if(reponse != NULL && write(maitre, reponse, strlen(reponse)) < 0)
		g_printerr("ecriture du repondeur impossible\n");
	g_free(reponse);
}

static gpointer
repondeur(gpointer data)
{
	int maitre = GPOINTER_TO_INT(data);
	GString *ligne = g_string_new(NULL);
	gchar c;

	while(read(maitre, &c, 1) == 1)
	{
		if(c == '\n')
		{
			repondre(maitre, ligne->str);
			g_string_truncate(ligne, 0);
		}
		else if(c != '\r')
			g_string_append_c(ligne, c);
	}
	g_string_free(ligne, TRUE);
	return NULL;
}

static void
test_exemple_10_mise_a_jour(void)
{
	gchar *chemin = g_test_build_filename(G_TEST_DIST, "..", "scripts", "10_firmware_update.lua", NULL);
	gchar *script = NULL, *adapte;
	gchar **morceaux;
	int maitre, esclave;
	GThread *th;

	if(openpty(&maitre, &esclave, NULL, NULL, NULL) != 0)
	{
		g_test_skip("openpty indisponible");
		g_free(chemin);
		return;
	}
	g_assert_true(g_file_get_contents(chemin, &script, NULL, NULL));
	morceaux = g_strsplit(script, "/dev/ttyUSB0", -1);
	adapte = g_strjoinv(ttyname(esclave), morceaux);
	th = g_thread_new("repondeur", repondeur, GINT_TO_POINTER(maitre));

	stub_reset();
	g_assert_true(executer(adapte, 15000));
	g_assert_nonnull(strstr(journal->str, "Version courante : 1.2.3"));
	g_assert_null(strstr(journal->str, "MISMATCH"));
	g_assert_nonnull(strstr(journal->str, "Mise à jour réussie"));

	/* Fermer l'esclave d'abord : le read() du répondeur sur le maître renvoie alors EIO et le thread se termine. */
	close(esclave);
	g_thread_join(th);
	close(maitre);
	g_strfreev(morceaux);
	g_free(adapte);
	g_free(script);
	g_free(chemin);
}

/* ---- Sous-projet 5 : aides --------------------------------------------- */

/* Exécute le script ; pilote est appelé toutes les 10 ms pendant l'exécution et renvoie toujours G_SOURCE_CONTINUE. */
static gboolean
executer_avec(const gchar *script, guint delai_ms, GSourceFunc pilote, gpointer data)
{
	guint id = g_timeout_add(10, pilote, data);
	gboolean ok = executer(script, delai_ms);

	g_source_remove(id);
	return ok;
}

static gboolean
journal_contient(const gchar *texte)
{
	return strstr(journal->str, texte) != NULL;
}

/* Nombre réel écrit par le script après « prefixe » dans le journal (format %.3f ou %.6f) */
static gdouble
nombre_apres(const gchar *prefixe)
{
	const gchar *p = strstr(journal->str, prefixe);

	g_assert_nonnull(p);
	return g_ascii_strtod(p + strlen(prefixe), NULL);
}

/* Socket d'écoute sur 127.0.0.1, port choisi par le système, file d'attente « backlog » */
static int
ecoute_locale(guint16 *port, int backlog)
{
	struct sockaddr_in adresse;
	socklen_t taille = sizeof(adresse);
	int fd = socket(AF_INET, SOCK_STREAM, 0);

	g_assert_cmpint(fd, >=, 0);
	memset(&adresse, 0, sizeof(adresse));
	adresse.sin_family = AF_INET;
	adresse.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	g_assert_cmpint(bind(fd, (struct sockaddr *)&adresse, sizeof(adresse)), ==, 0);
	g_assert_cmpint(listen(fd, backlog), ==, 0);
	g_assert_cmpint(getsockname(fd, (struct sockaddr *)&adresse, &taille), ==, 0);
	*port = ntohs(adresse.sin_port);
	return fd;
}

static guint16
port_libre(void)
{
	guint16 port;

	close(ecoute_locale(&port, 1));
	return port;
}

static int
connecter(guint16 port)
{
	struct sockaddr_in adresse;
	int fd = socket(AF_INET, SOCK_STREAM, 0);

	memset(&adresse, 0, sizeof(adresse));
	adresse.sin_family = AF_INET;
	adresse.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	adresse.sin_port = htons(port);
	g_assert_cmpint(connect(fd, (struct sockaddr *)&adresse, sizeof(adresse)), ==, 0);
	return fd;
}

/* Écouteur dont la file d'attente est pleine : une connexion suivante reste « en cours » (SYN ignoré).
 * Renvoie FALSE (cas sauté) si le noyau accepte quand même une deuxième connexion. */
static gboolean
ecouteur_sature(int *ecoute, int *occupant, guint16 *port)
{
	struct sockaddr_in adresse;
	struct pollfd pfd;
	int essai;
	gboolean sature;

	*ecoute = ecoute_locale(port, 0);
	*occupant = connecter(*port);
	essai = socket(AF_INET, SOCK_STREAM, 0);
	fcntl(essai, F_SETFL, O_NONBLOCK);
	memset(&adresse, 0, sizeof(adresse));
	adresse.sin_family = AF_INET;
	adresse.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	adresse.sin_port = htons(*port);
	if(connect(essai, (struct sockaddr *)&adresse, sizeof(adresse)) == 0)
		sature = FALSE;
	else
	{
		pfd.fd = essai;
		pfd.events = POLLOUT;
		pfd.revents = 0;
		sature = (errno == EINPROGRESS && poll(&pfd, 1, 300) == 0);
	}
	close(essai);
	return sature;
}

/* ---- Série sur pty : paramètres et validation ---------------------------- */

typedef struct
{
	int esclave;
	gboolean lu;
	guint vitesse;
	tcflag_t cflag;
} ParamsPty;

static gboolean
piloter_parametres(gpointer data)
{
	ParamsPty *p = data;
	struct termios tio;

	if(!p->lu && journal_contient("ouvert\n"))
	{
		g_assert_cmpint(tcgetattr(p->esclave, &tio), ==, 0);
		p->vitesse = get_port_baudrate(p->esclave);
		p->cflag = tio.c_cflag;
		p->lu = TRUE;
		stub_inject_rx("fin", 3);
	}
	return G_SOURCE_CONTINUE;
}

static void
test_serie_parametres(void)
{
	int maitre, esclave;
	ParamsPty p = { 0 };
	gchar *chemin, *script;
	struct termios tio;

	if(openpty(&maitre, &esclave, NULL, NULL, NULL) != 0)
	{
		g_test_skip("openpty indisponible");
		return;
	}
	chemin = g_strdup(ttyname(esclave));
	p.esclave = esclave;
	script = g_strdup_printf("assert(gtkterm.open(1, 'serial', '%s', 115200, 8, 2, 'even', 'none'))\n"
	                         "gtkterm.log('ouvert')\n"
	                         "assert(gtkterm.wait_for(0, 'fin', 10000))\n"
	                         "gtkterm.close(1)\n"
	                         "local ok, err = gtkterm.open(2, 'serial', '%s', 115200, 8, 1, 'mark')\n"
	                         "gtkterm.log('parite ' .. tostring(ok) .. ' ' .. err)\n"
	                         "ok, err = gtkterm.open(2, 'serial', '%s', 115200, 9)\n"
	                         "gtkterm.log('bits ' .. tostring(ok) .. ' ' .. err)\n"
	                         "ok, err = gtkterm.open(2, 'serial', '%s', 0)\n"
	                         "gtkterm.log('debit ' .. tostring(ok) .. ' ' .. err)\n"
	                         "ok, err = gtkterm.open(2, 'serial', '%s', 9600, 8, 3)\n"
	                         "gtkterm.log('stop ' .. tostring(ok) .. ' ' .. err)\n"
	                         "ok, err = gtkterm.open(2, 'serial', '%s', 9600, 8, 1, 0, 5)\n"
	                         "gtkterm.log('flux ' .. tostring(ok) .. ' ' .. err)\n"
	                         "gtkterm.log('etat ' .. gtkterm.status(1) .. ' ' .. gtkterm.status(2))\n",
	                         chemin, chemin, chemin, chemin, chemin, chemin);
	stub_reset();
	g_assert_true(executer_avec(script, 15000, piloter_parametres, &p));
	g_assert_true(p.lu);
	g_assert_cmpuint(p.vitesse, ==, 115200);
	g_assert_cmpuint(p.cflag & CSTOPB, ==, CSTOPB);
	g_assert_true(journal_contient("parite false invalid parity\n"));
	g_assert_true(journal_contient("bits false invalid bits\n"));
	g_assert_true(journal_contient("debit false invalid baud\n"));
	g_assert_true(journal_contient("stop false invalid stop\n"));
	g_assert_true(journal_contient("flux false invalid flow\n"));
	g_assert_true(journal_contient("etat closed closed\n"));
	/* Port refermé par gtkterm.close : réglages d'origine restaurés */
	g_assert_cmpint(tcgetattr(esclave, &tio), ==, 0);
	g_assert_cmpuint(tio.c_cflag & CSTOPB, ==, 0);
	g_free(script);
	g_free(chemin);
	close(esclave);
	close(maitre);
}

/* ---- send : tout ou échec (1 Mio) ------------------------------------------ */

typedef struct
{
	int maitre;
	GByteArray *recu;
	gsize attendu;
	gint complet;		/* atomique : tout est reçu (lu par le pilote sur le thread principal) */
	gboolean signale;
} Lecteur;

/* Lit le maître jusqu'à « attendu » octets ou 30 s (poll : jamais bloqué si l'envoi échoue) */
static gpointer
lire_maitre(gpointer data)
{
	Lecteur *l = data;
	guint8 tampon[65536];
	gint64 fin = g_get_monotonic_time() + 30 * G_USEC_PER_SEC;

	while(l->recu->len < l->attendu && g_get_monotonic_time() < fin)
	{
		struct pollfd pfd = { l->maitre, POLLIN, 0 };

		if(poll(&pfd, 1, 50) > 0)
		{
			ssize_t n = read(l->maitre, tampon, sizeof(tampon));

			if(n > 0)
				g_byte_array_append(l->recu, tampon, (guint)n);
			else if(n == 0 || (errno != EAGAIN && errno != EINTR))
				break;
		}
	}
	g_atomic_int_set(&l->complet, l->recu->len >= l->attendu);
	return NULL;
}

/* Le script attend « lu » avant de refermer le port : la fermeture (tcflush TCOFLUSH du transport) jetterait ce que le lecteur
 * n'a pas encore lu dans le tampon du pty. */
static gboolean
piloter_lecteur(gpointer data)
{
	Lecteur *l = data;

	if(!l->signale && g_atomic_int_get(&l->complet))
	{
		l->signale = TRUE;
		stub_inject_rx("lu", 2);
	}
	return G_SOURCE_CONTINUE;
}

static void
test_send_complet_ou_delai(void)
{
	int ma, ea, mb, eb;
	Lecteur l;
	GThread *th;
	gchar *a, *b, *script;
	gboolean conforme = TRUE;

	if(openpty(&ma, &ea, NULL, NULL, NULL) != 0)
	{
		g_test_skip("openpty indisponible");
		return;
	}
	if(openpty(&mb, &eb, NULL, NULL, NULL) != 0)
	{
		close(ea);
		close(ma);
		g_test_skip("openpty indisponible");
		return;
	}
	a = g_strdup(ttyname(ea));
	b = g_strdup(ttyname(eb));
	l.maitre = mb;
	l.recu = g_byte_array_new();
	l.attendu = 1048576;
	l.complet = 0;
	l.signale = FALSE;
	th = g_thread_new("lecteur", lire_maitre, &l);
	/* Port 1 : pty A que personne ne lit (délai) ; port 2 : pty B lue par le thread « lecteur » */
	script = g_strdup_printf("local bloc = {}\n"
	                         "for i = 0, 250 do bloc[#bloc + 1] = string.char(i) end\n"
	                         "local donnees = string.rep(table.concat(bloc), 4178):sub(1, 1048576)\n"
	                         "assert(gtkterm.open(1, 'serial', '%s'))\n"
	                         "local t0 = gtkterm.time()\n"
	                         "local ok, err = gtkterm.send(1, donnees, 300)\n"
	                         "gtkterm.log(string.format('sans lecteur %%s %%s %%.3f', tostring(ok), tostring(err), gtkterm.time() - t0))\n"
	                         "gtkterm.close(1)\n"
	                         "assert(gtkterm.open(2, 'serial', '%s'))\n"
	                         "ok, err = gtkterm.send(2, donnees, 20000)\n"
	                         "gtkterm.log('avec lecteur ' .. tostring(ok) .. ' ' .. tostring(err))\n"
	                         "gtkterm.wait_for(0, 'lu', 30000)\n", a, b);
	stub_reset();
	g_assert_true(executer_avec(script, 40000, piloter_lecteur, &l));
	g_thread_join(th);
	g_assert_true(journal_contient("sans lecteur false timeout "));
	g_assert_cmpfloat(nombre_apres("sans lecteur false timeout "), >=, 0.29);
	g_assert_true(journal_contient("avec lecteur true nil\n"));
	g_assert_cmpuint(l.recu->len, ==, 1048576);
	for(guint i = 0; i < l.recu->len && conforme; i++)
		conforme = (l.recu->data[i] == i % 251);
	g_assert_true(conforme);
	g_byte_array_unref(l.recu);
	g_free(script);
	g_free(a);
	g_free(b);
	close(ea);
	close(ma);
	close(eb);
	close(mb);
}

/* ---- send_hex : texte invalide, rien d'envoyé ------------------------------ */

static void
test_send_hex(void)
{
	GByteArray *octets;

	stub_reset();
	g_assert_true(executer("local ok, err = gtkterm.send_hex(0, '41 4G')\n"
	                       "gtkterm.log('invalide ' .. tostring(ok) .. ' ' .. err)\n"
	                       "ok, err = gtkterm.send_hex(0, '414')\n"
	                       "gtkterm.log('impair ' .. tostring(ok) .. ' ' .. err)\n"
	                       "ok, err = gtkterm.send_hex(0, '41\\0' .. '42')\n"
	                       "gtkterm.log('nul ' .. tostring(ok) .. ' ' .. err)\n"
	                       "ok, err = gtkterm.send_hex(0, '4G\\0')\n"
	                       "gtkterm.log('invnul ' .. tostring(ok) .. ' ' .. err)\n"
	                       "ok, err = gtkterm.send_hex(0, '4\\0')\n"
	                       "gtkterm.log('imparnul ' .. tostring(ok) .. ' ' .. err)\n", 5000));
	g_assert_true(journal_contient("invalide false invalid hex: invalid character at position 4\n"));
	g_assert_true(journal_contient("impair false invalid hex: odd number of hex digits\n"));
	/* Un \0 dans la chaîne Lua est refusé, pas tronqué (« 41 » seul serait envoyé sinon) */
	g_assert_true(journal_contient("nul false invalid hex: invalid character at position 2\n"));
	/* Le caractère invalide du préfixe garde sa position, le \0 ne vient qu'après un préfixe valide */
	g_assert_true(journal_contient("invnul false invalid hex: invalid character at position 1\n"));
	g_assert_true(journal_contient("imparnul false invalid hex: invalid character at position 1\n"));
	g_assert_cmpuint(stub_sent()->len, ==, 0);
	g_assert_true(executer("assert(gtkterm.send_hex(0, '41 42\\r\\n\\t4a'))\n", 5000));
	octets = stub_sent();
	g_assert_cmpmem(octets->data, octets->len, "ABJ", 3);
}

/* ---- Port perdu : wait_for rend « closed » tout de suite --------------------- */

typedef struct
{
	int maitre;
	gint etape;
	gint64 fermeture_us;
} Perte;

static gboolean
piloter_perte(gpointer data)
{
	Perte *p = data;

	if(p->etape == 0 && journal_contient("ouvert\n"))
	{
		g_assert_cmpint(write(p->maitre, "abcXYZ", 6), ==, 6);
		p->etape = 1;
	}
	else if(p->etape == 1 && journal_contient("pret\n"))
	{
		p->fermeture_us = g_get_monotonic_time();
		close(p->maitre);
		p->maitre = -1;
		p->etape = 2;
	}
	return G_SOURCE_CONTINUE;
}

static void
test_port_perdu(void)
{
	int maitre, esclave;
	Perte p = { -1, 0, 0 };
	gchar *chemin, *script;
	gdouble retour;

	if(openpty(&maitre, &esclave, NULL, NULL, NULL) != 0)
	{
		g_test_skip("openpty indisponible");
		return;
	}
	chemin = g_strdup(ttyname(esclave));
	p.maitre = maitre;
	script = g_strdup_printf("assert(gtkterm.open(1, 'serial', '%s'))\n"
	                         "gtkterm.log('ouvert')\n"
	                         "assert(gtkterm.wait_for(1, 'abc', 10000))\n"
	                         "gtkterm.log('pret')\n"
	                         "local r, err = gtkterm.wait_for(1, 'jamais', 10000)\n"
	                         "gtkterm.log(string.format('fin %%s %%s %%.6f', tostring(r), tostring(err), gtkterm.time()))\n"
	                         "gtkterm.log('reste ' .. gtkterm.read(1))\n"
	                         "gtkterm.log('etat ' .. gtkterm.status(1))\n"
	                         "local ok, e2 = gtkterm.send(1, 'x')\n"
	                         "gtkterm.log('envoi ' .. tostring(ok) .. ' ' .. tostring(e2))\n", chemin);
	stub_reset();
	g_assert_true(executer_avec(script, 20000, piloter_perte, &p));
	g_assert_cmpint(p.etape, ==, 2);
	/* Même horloge (monotone) des deux côtés : moins de 500 ms entre la fermeture du maître et le retour de wait_for */
	retour = nombre_apres("fin nil closed ");
	g_assert_cmpfloat(retour * G_USEC_PER_SEC - (gdouble)p.fermeture_us, <, 500000.0);
	g_assert_true(journal_contient("reste XYZ\n"));
	g_assert_true(journal_contient("etat lost\n"));
	g_assert_true(journal_contient("envoi false link lost\n"));
	g_free(script);
	g_free(chemin);
	close(esclave);
}

/* ---- Serveur TCP : listening → connected → listening ------------------------- */

typedef struct
{
	guint16 port;
	int client;
	gint etape;
	GString *recu;
} Serveur;

static gboolean
piloter_serveur(gpointer data)
{
	Serveur *s = data;
	guint8 tampon[64];
	ssize_t n;

	if(s->etape == 0 && journal_contient("ecoute listening\n"))
	{
		s->client = connecter(s->port);
		fcntl(s->client, F_SETFL, O_NONBLOCK);
		s->etape = 1;
	}
	else if(s->etape == 1 && journal_contient("client connected\n"))
	{
		/* Lecture non bloquante : l'envoi du script a besoin de ce thread (run_on_main_sync) */
		n = read(s->client, tampon, sizeof(tampon));
		if(n > 0)
			g_string_append_len(s->recu, (const gchar *)tampon, n);
		if(strstr(s->recu->str, "bonjour") != NULL)
		{
			g_assert_cmpint(write(s->client, "merci", 5), ==, 5);
			close(s->client);
			s->client = -1;
			s->etape = 2;
		}
	}
	return G_SOURCE_CONTINUE;
}

static void
test_serveur_tcp_status(void)
{
	Serveur s = { port_libre(), -1, 0, g_string_new(NULL) };
	gchar *script = g_strdup_printf("assert(gtkterm.open(1, 'tcp_server', %u, '127.0.0.1'))\n"
	                                "gtkterm.log('ecoute ' .. gtkterm.status(1))\n"
	                                "local t0 = gtkterm.time()\n"
	                                "while gtkterm.status(1) ~= 'connected' and gtkterm.time() - t0 < 10 do gtkterm.sleep(10) end\n"
	                                "gtkterm.log('client ' .. gtkterm.status(1))\n"
	                                "assert(gtkterm.send(1, 'bonjour'))\n"
	                                "gtkterm.log('recu ' .. tostring(gtkterm.wait_for(1, 'merci', 10000)))\n"
	                                "t0 = gtkterm.time()\n"
	                                "while gtkterm.status(1) ~= 'listening' and gtkterm.time() - t0 < 10 do gtkterm.sleep(10) end\n"
	                                "gtkterm.log('parti ' .. gtkterm.status(1))\n", s.port);

	stub_reset();
	g_assert_true(executer_avec(script, 30000, piloter_serveur, &s));
	g_assert_true(journal_contient("ecoute listening\n"));
	g_assert_true(journal_contient("client connected\n"));
	g_assert_true(journal_contient("recu merci\n"));
	g_assert_true(journal_contient("parti listening\n"));
	if(s.client >= 0)
		close(s.client);
	g_string_free(s.recu, TRUE);
	g_free(script);
}

/* ---- Client TCP : délai de connexion ------------------------------------------ */

static void
test_client_tcp_delai(void)
{
	int ecoute, occupant;
	guint16 port;
	gchar *script;

	if(!ecouteur_sature(&ecoute, &occupant, &port))
	{
		close(occupant);
		close(ecoute);
		g_test_skip("file d'attente TCP non saturable ici");
		return;
	}
	script = g_strdup_printf("local t0 = gtkterm.time()\n"
	                         "local ok, err = gtkterm.open(1, 'tcp_client', '127.0.0.1', %u, 300)\n"
	                         "gtkterm.log(string.format('delai %%s %%s %%.3f %%s', tostring(ok), tostring(err), gtkterm.time() - t0, gtkterm.status(1)))\n",
	                         port);
	stub_reset();
	g_assert_true(executer(script, 15000));
	g_assert_true(journal_contient("delai false timeout "));
	g_assert_cmpfloat(nombre_apres("delai false timeout "), >=, 0.29);
	g_assert_true(journal_contient(" closed\n"));
	g_free(script);
	close(occupant);
	close(ecoute);
}

/* ---- Port 0 : liaison en échec, états ------------------------------------------ */

static void
test_liaison_etats_et_echec(void)
{
	static const gchar *script = "local ok, err = gtkterm.send(0, 'x')\n"
	                             "gtkterm.log('envoi ' .. tostring(ok) .. ' ' .. tostring(err) .. ' ' .. gtkterm.status(0))\n";

	stub_reset();
	stub_link_set_write_fails(TRUE);
	g_assert_true(executer(script, 5000));
	g_assert_true(journal_contient("envoi false not connected open\n"));
	stub_link_set_state(GT_LINK_RECONNECTING);
	g_assert_true(executer(script, 5000));
	g_assert_true(journal_contient("envoi false link lost lost\n"));
	stub_link_set_state(GT_LINK_LISTENING);
	g_assert_true(executer("gtkterm.log('etat ' .. gtkterm.status(0))\n", 5000));
	g_assert_true(journal_contient("etat listening\n"));
	stub_link_set_state(GT_LINK_CONNECTING);
	g_assert_true(executer("gtkterm.log('etat ' .. gtkterm.status(0))\n", 5000));
	g_assert_true(journal_contient("etat connecting\n"));
	stub_link_set_state(GT_LINK_CLOSED);
	g_assert_true(executer("gtkterm.log('etat ' .. gtkterm.status(0))\n", 5000));
	g_assert_true(journal_contient("etat closed\n"));
	g_assert_cmpuint(stub_sent()->len, ==, 0);
	stub_reset();
}

/* ---- Arguments hors domaine ---------------------------------------------------- */

static void
test_arguments_extremes(void)
{
	stub_reset();
	installer_macro_a();
	g_assert_true(executer("local ok, err = gtkterm.open(0, 'tcp_server', 0)\n"
	                       "gtkterm.log('a ' .. tostring(ok) .. ' ' .. err)\n"
	                       "ok, err = gtkterm.open(4294967297, 'tcp_server', 0)\n"
	                       "gtkterm.log('b ' .. tostring(ok) .. ' ' .. err)\n"
	                       "ok, err = gtkterm.send(16, 'x')\n"
	                       "gtkterm.log('c ' .. tostring(ok) .. ' ' .. err)\n"
	                       "ok, err = gtkterm.send(3, 'x')\n"
	                       "gtkterm.log('d ' .. tostring(ok) .. ' ' .. err)\n"
	                       "local r, e = gtkterm.wait_for(4294967296, 'x', 10)\n"
	                       "gtkterm.log('e ' .. tostring(r) .. ' ' .. e)\n"
	                       "r, e = gtkterm.wait_for(3, 'x', 10)\n"
	                       "gtkterm.log('f ' .. tostring(r) .. ' ' .. e)\n"
	                       "gtkterm.log('g ' .. gtkterm.status(16) .. ' ' .. gtkterm.status(3) .. ' ' .. gtkterm.status(-1))\n"
	                       "local t0 = gtkterm.time()\n"
	                       "gtkterm.sleep(-5)\n"
	                       "r, e = gtkterm.wait_for(0, 'x', -1)\n"
	                       "gtkterm.log(string.format('h %s %s %.3f', tostring(r), e, gtkterm.time() - t0))\n"
	                       "gtkterm.close(99)\n"
	                       "gtkterm.close(-1)\n"
	                       "gtkterm.close(3)\n"
	                       "ok, err = gtkterm.open(1, 'tcp_server', 70000)\n"
	                       "gtkterm.log('i ' .. tostring(ok) .. ' ' .. err)\n"
	                       "ok, err = gtkterm.open(1, 'udp', 1)\n"
	                       "gtkterm.log('j ' .. tostring(ok) .. ' ' .. err)\n"
	                       "gtkterm.log('k ' .. gtkterm.read(3) .. '|' .. gtkterm.read(99) .. '|')\n"
	                       "assert(gtkterm.open(1, 'tcp_server', 0))\n"
	                       "ok, err = gtkterm.open(1, 'tcp_server', 0)\n"
	                       "gtkterm.log('l ' .. tostring(ok) .. ' ' .. err)\n"
	                       "ok, err = gtkterm.send_macro(4294967296)\n"
	                       "gtkterm.log('m ' .. tostring(ok) .. ' ' .. err)\n"
	                       "ok, err = gtkterm.send_macro(-1)\n"
	                       "gtkterm.log('n ' .. tostring(ok) .. ' ' .. err)\n"
	                       "ok, err = gtkterm.send_macro(2147483648)\n"
	                       "gtkterm.log('o ' .. tostring(ok) .. ' ' .. err)\n"
	                       "ok, err = pcall(gtkterm.send_macro, 1.5)\n"
	                       "gtkterm.log('p ' .. tostring(ok))\n", 10000));
	g_assert_true(journal_contient("a false invalid handle\n"));
	g_assert_true(journal_contient("b false invalid handle\n"));
	g_assert_true(journal_contient("c false invalid handle\n"));
	g_assert_true(journal_contient("d false not open\n"));
	g_assert_true(journal_contient("e nil invalid handle\n"));
	g_assert_true(journal_contient("f nil closed\n"));
	g_assert_true(journal_contient("g closed closed closed\n"));
	g_assert_true(journal_contient("h nil timeout "));
	g_assert_cmpfloat(nombre_apres("h nil timeout "), <, 1.0);
	g_assert_true(journal_contient("i false invalid port\n"));
	g_assert_true(journal_contient("j false unknown type (serial/tcp_client/tcp_server)\n"));
	g_assert_true(journal_contient("k ||\n"));
	g_assert_true(journal_contient("l false already open\n"));
	/* M4 : l'indice n'est jamais tronqué en int (4294967296 serait la macro 0) ; 1.5 est refusé */
	g_assert_true(journal_contient("m false macro not found\n"));
	g_assert_true(journal_contient("n false macro not found\n"));
	g_assert_true(journal_contient("o false macro not found\n"));
	g_assert_true(journal_contient("p false\n"));
	g_assert_cmpuint(stub_sent()->len, ==, 0);
}

/* ---- Débordement de la file du port 0 : signalé une fois par exécution ---------- */

typedef struct
{
	gint etape;
	gchar *gros;
	guint taille;
} Debordement;

static gboolean
piloter_debordement(gpointer data)
{
	Debordement *d = data;

	if(d->etape == 0 && journal_contient("pret1\n"))
	{
		stub_inject_rx(d->gros, d->taille);
		stub_inject_rx("FIN1", 4);
		d->etape = 1;
	}
	else if(d->etape == 1 && journal_contient("pret2\n"))
	{
		stub_inject_rx(d->gros, d->taille);
		stub_inject_rx("FIN2", 4);
		d->etape = 2;
	}
	return G_SOURCE_CONTINUE;
}

static void
test_debordement_signale_une_fois(void)
{
	Debordement d = { 0, NULL, 1536 * 1024 };
	const gchar *message = "Port 0: receive buffer full, oldest data dropped\n";
	const gchar *p;
	guint n = 0;

	d.gros = g_malloc(d.taille);
	memset(d.gros, 'a', d.taille);
	stub_reset();
	g_assert_true(executer_avec("gtkterm.log('pret1')\n"
	                            "gtkterm.log('taille1 ' .. #gtkterm.wait_for(0, 'FIN1', 10000))\n"
	                            "gtkterm.log('pret2')\n"
	                            "gtkterm.log('taille2 ' .. #gtkterm.wait_for(0, 'FIN2', 10000))\n", 20000, piloter_debordement, &d));
	/* Les plus anciens octets sont jetés : il reste exactement 1 Mio, motif compris */
	g_assert_true(journal_contient("taille1 1048576\n"));
	g_assert_true(journal_contient("taille2 1048576\n"));
	for(p = strstr(journal->str, message); p != NULL; p = strstr(p + 1, message))
		n++;
	g_assert_cmpuint(n, ==, 1);
	g_free(d.gros);
}

/* ---- Sous-projet 5 : arrêt, os.exit, print, journal, fermeture ---------------- */

static guint arret_id = 0;
static gint64 arret_us = 0;
static guint envoyes_a_l_arret = 0;	/* octets envoyés par la liaison bouchon au moment de la demande d'arrêt */

static gboolean
arreter(gpointer data)
{
	arret_id = 0;
	arret_us = g_get_monotonic_time();
	envoyes_a_l_arret = stub_sent()->len;
	script_engine_stop();
	return G_SOURCE_REMOVE;
}

/* Lance le script, demande l'arrêt après apres_ms, vérifie une fin STOPPED moins d'une seconde après la demande. */
static void
verifier_arret(const gchar *script, guint apres_ms)
{
	arret_us = 0;
	arret_id = g_timeout_add(apres_ms, arreter, NULL);
	g_assert_true(executer(script, 15000));
	if(arret_id != 0)
	{
		g_source_remove(arret_id);
		arret_id = 0;
	}
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_STOPPED);
	g_assert_cmpint(arret_us, >, 0);
	g_assert_cmpint(fin_us - arret_us, <, G_USEC_PER_SEC);
	g_assert_null(strstr(journal->str, "Lua error"));
}

static void
test_stop_boucle_infinie(void)
{
	stub_reset();
	verifier_arret("while true do end\n", 200);
}

static void
test_stop_boucles_pcall(void)
{
	stub_reset();
	/* pcall qui avale tout, imbriqué dans une boucle infinie */
	verifier_arret("while true do pcall(function() while true do end end) end\n", 200);
	/* pcall court répété */
	verifier_arret("while true do pcall(function() local x = 0 for i = 1, 100 do x = x + i end end) end\n", 200);
	/* xpcall avec gestionnaire de message (interrompu à son tour : « error in error handling ») */
	verifier_arret("while true do xpcall(function() while true do end end, function(e) return e end) end\n", 200);
	/* coroutines relancées en boucle */
	verifier_arret("while true do coroutine.resume(coroutine.create(function() while true do end end)) end\n", 200);
}

static void
test_stop_pendant_sleep(void)
{
	stub_reset();
	verifier_arret("gtkterm.sleep(60000)\ngtkterm.log('apres')\n", 200);
	g_assert_null(strstr(journal->str, "apres"));
}

static void
test_stop_pendant_wait_for(void)
{
	stub_reset();
	verifier_arret("gtkterm.wait_for(0, 'jamais', 60000)\ngtkterm.log('apres')\n", 200);
	g_assert_null(strstr(journal->str, "apres"));
}

static void
test_stop_pendant_connexion(void)
{
	int ecoute, occupant;
	guint16 port;
	guint avant;
	gchar *script;

	if(!ecouteur_sature(&ecoute, &occupant, &port))
	{
		close(occupant);
		close(ecoute);
		g_test_skip("file d'attente TCP non saturable ici");
		return;
	}
	g_main_context_default();
	avant = compter_descripteurs();
	script = g_strdup_printf("gtkterm.open(1, 'tcp_client', '127.0.0.1', %u, 60000)\ngtkterm.log('apres')\n", port);
	stub_reset();
	verifier_arret(script, 300);
	g_assert_null(strstr(journal->str, "apres"));
	/* Port refermé par le nettoyage de fin de script */
	g_assert_cmpuint(compter_descripteurs(), ==, avant);
	g_free(script);
	close(occupant);
	close(ecoute);
}

static void
test_stop_pendant_envoi(void)
{
	int maitre, esclave;
	guint avant;
	gchar *chemin, *script;

	if(openpty(&maitre, &esclave, NULL, NULL, NULL) != 0)
	{
		g_test_skip("openpty indisponible");
		return;
	}
	chemin = g_strdup(ttyname(esclave));
	g_main_context_default();
	avant = compter_descripteurs();
	/* Personne ne lit le maître : l'envoi attend en reprises de 2 ms jusqu'à l'arrêt */
	script = g_strdup_printf("assert(gtkterm.open(1, 'serial', '%s'))\n"
	                         "gtkterm.send(1, string.rep('x', 1048576), 60000)\n"
	                         "gtkterm.log('apres')\n", chemin);
	stub_reset();
	verifier_arret(script, 300);
	g_assert_null(strstr(journal->str, "apres"));
	g_assert_cmpuint(compter_descripteurs(), ==, avant);
	g_free(script);
	g_free(chemin);
	close(esclave);
	close(maitre);
}

static void
test_os_exit(void)
{
	static const struct
	{
		const gchar *script;
		GtScriptEnd fin;
		const gchar *message;
	} cas[] = {
		{ "os.exit()\ngtkterm.log('apres')\n", GT_SCRIPT_END_FINISHED, NULL },
		{ "os.exit(true)\ngtkterm.log('apres')\n", GT_SCRIPT_END_FINISHED, NULL },
		{ "os.exit(0)\ngtkterm.log('apres')\n", GT_SCRIPT_END_FINISHED, NULL },
		{ "os.exit(3)\ngtkterm.log('apres')\n", GT_SCRIPT_END_ERROR, "Script exited with status 3\n" },
		{ "os.exit(false)\ngtkterm.log('apres')\n", GT_SCRIPT_END_ERROR, "Script exited with status 1\n" },
		{ "pcall(os.exit, 4)\ngtkterm.log('apres')\n", GT_SCRIPT_END_ERROR, "Script exited with status 4\n" },
		/* Code hors de int : jamais tronqué (2^32 donnerait 0, fin normale) */
		{ "os.exit(4294967296)\ngtkterm.log('apres')\n", GT_SCRIPT_END_ERROR, "Script exited with status 4294967296\n" },
	};

	stub_reset();
	for(guint i = 0; i < G_N_ELEMENTS(cas); i++)
	{
		/* Le processus de test survit : os.exit termine le script, jamais GTKTerm */
		g_assert_true(executer(cas[i].script, 5000));
		g_assert_cmpint(derniere_fin, ==, cas[i].fin);
		g_assert_null(strstr(journal->str, "apres"));
		g_assert_null(strstr(journal->str, "Lua error"));
		if(cas[i].message != NULL)
			g_assert_nonnull(strstr(journal->str, cas[i].message));
		else
			g_assert_null(strstr(journal->str, "Script exited"));
	}
}

static void
test_print(void)
{
	stub_reset();
	g_assert_true(executer("print('a', 1, nil, true)\nprint()\nprint('fin')\n", 5000));
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_FINISHED);
	g_assert_nonnull(strstr(journal->str, "a\t1\tnil\ttrue\n\nfin\n"));
}

static void
test_raisons_de_fin(void)
{
	stub_reset();
	g_assert_true(executer("gtkterm.log('ok')\n", 5000));
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_FINISHED);
	g_assert_null(strstr(journal->str, "Lua error"));

	g_assert_true(executer("error('boum')\n", 5000));
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_ERROR);
	g_assert_nonnull(strstr(journal->str, "Lua error: script:1: boum\n"));

	g_assert_true(executer("x = = 1\n", 5000));
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_ERROR);
	g_assert_nonnull(strstr(journal->str, "Lua error: "));

	g_assert_true(executer("error({})\n", 5000));
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_ERROR);
	g_assert_nonnull(strstr(journal->str, "Lua error: (error object is a table value)\n"));

	g_assert_true(executer("error(42)\n", 5000));
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_ERROR);
	g_assert_nonnull(strstr(journal->str, "Lua error: 42\n"));

	/* Octet nul dans le message : montré « \0 », la suite n'est pas perdue */
	g_assert_true(executer("error('a\\0b')\n", 5000));
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_ERROR);
	g_assert_nonnull(strstr(journal->str, "Lua error: script:1: a\\0b\n"));
}

static void
test_journal_en_rafale(void)
{
	guint lignes = 0;

	stub_reset();
	g_assert_true(executer("for i = 1, 100000 do gtkterm.log(i) end\n", 60000));
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_FINISHED);
	/* Regroupé : bien moins d'un appel par ligne ; rien de perdu sous le plafond (~0,6 Mo) */
	g_assert_cmpuint(appels_journal, <, 1000);
	for(const gchar *p = journal->str; *p != '\0'; p++)
		if(*p == '\n')
			lignes++;
	g_assert_cmpuint(lignes, ==, 100000);
	g_assert_true(g_str_has_prefix(journal->str, "1\n2\n3\n"));
	g_assert_true(g_str_has_suffix(journal->str, "99999\n100000\n"));
	g_assert_null(strstr(journal->str, "dropped"));
}

/* Fait tourner le contexte par défaut jusqu'à ce que le journal contienne marqueur (ou ms) */
static gboolean
iterer_jusqu_a(const gchar *marqueur, guint ms)
{
	gint64 fin = g_get_monotonic_time() + (gint64)ms * 1000;

	while(!journal_contient(marqueur))
	{
		if(g_get_monotonic_time() >= fin)
			return FALSE;
		if(!g_main_context_iteration(NULL, FALSE))
			g_usleep(1000);
	}
	return TRUE;
}

static void
test_journal_plafond(void)
{
	guint lignes_x = 0;
	gint64 fin;

	stub_reset();
	g_string_truncate(journal, 0);
	fini = FALSE;
	script_engine_run("gtkterm.log('pret')\n"
	                  "assert(gtkterm.wait_for(0, 'go', 10000))\n"
	                  "local s = string.rep('x', 1023)\n"
	                  "for i = 1, 3072 do gtkterm.log(s) end\n"
	                  "assert(gtkterm.wait_for(0, 'suite', 10000))\n"
	                  "gtkterm.log('termine')\n");
	g_assert_true(iterer_jusqu_a("pret\n", 10000));
	stub_inject_rx("go", 2);
	/* Thread principal immobile 1 s : 3 Mio journalisés sans vidage possible, seuls les 1024 premiers Kio tiennent */
	g_usleep(G_USEC_PER_SEC);
	g_assert_true(iterer_jusqu_a("dropped]\n", 10000));
	stub_inject_rx("suite", 5);
	fin = g_get_monotonic_time() + 10 * G_USEC_PER_SEC;
	while(!fini && g_get_monotonic_time() < fin)
		if(!g_main_context_iteration(NULL, FALSE))
			g_usleep(1000);
	g_assert_true(fini);
	g_assert_nonnull(strstr(journal->str, "[2097152 bytes of log output dropped]\n"));
	g_assert_nonnull(strstr(journal->str, "termine\n"));
	for(const gchar *p = strstr(journal->str, "xxx"); p != NULL; p = strstr(p + 1, "\nxxx"))
		lignes_x++;
	g_assert_cmpuint(lignes_x, ==, 1024);
}

static void
test_journal_binaire(void)
{
	stub_reset();
	g_assert_true(executer("gtkterm.log('a\\0b')\ngtkterm.log('\\255fin')\ngtkterm.log('suite')\n", 5000));
	/* Octet nul montré « \0 » (il couperait tout le lot) ; UTF-8 réparé (U+FFFD) au lieu de faire refuser le lot */
	g_assert_nonnull(strstr(journal->str, "a\\0b\n"));
	g_assert_nonnull(strstr(journal->str, "\357\277\275fin\nsuite\n"));
	g_assert_true(g_utf8_validate(journal->str, -1, NULL));
}

/* Plafond comparé à la taille après échappement : 600 000 octets nuls deviennent 1,2 Mo, au-delà du plafond de 1 Mio */
static void
test_journal_nuls_plafond(void)
{
	stub_reset();
	g_assert_true(executer("gtkterm.log(string.rep('\\0', 600000))\ngtkterm.log('fin')\n", 5000));
	g_assert_nonnull(strstr(journal->str, "[600001 bytes of log output dropped]\n"));
	g_assert_nonnull(strstr(journal->str, "fin\n"));
	g_assert_null(strstr(journal->str, "\\0"));
}

/* debug.sethook retiré : un script ne peut pas enlever le crochet d'arrêt */
static void
test_stop_malgre_debug_sethook(void)
{
	stub_reset();
	verifier_arret("if debug and debug.sethook then debug.sethook() end\nwhile true do end\n", 200);
	g_assert_true(executer("gtkterm.log(tostring(debug.sethook))\n", 5000));
	g_assert_nonnull(strstr(journal->str, "nil\n"));
}

/* Boucle d'envois de macro : après la demande d'arrêt (thread principal), plus aucun envoi, même déjà demandé par le thread Lua */
static void
test_stop_boucle_send_macro(void)
{
	stub_reset();
	installer_macro_a();
	verifier_arret("while true do gtkterm.send_macro('A') end\n", 200);
	g_assert_cmpuint(envoyes_a_l_arret, >, 0);
	g_assert_cmpuint(stub_sent()->len, ==, envoyes_a_l_arret);
}

static void
test_relance_apres_arret(void)
{
	stub_reset();
	verifier_arret("while true do end\n", 200);
	g_assert_true(executer("gtkterm.log('relance')\n", 5000));
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_FINISHED);
	g_assert_nonnull(strstr(journal->str, "relance\n"));
	g_assert_true(executer("os.exit(2)\n", 5000));
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_ERROR);
	g_assert_true(executer("gtkterm.sleep(10)\ngtkterm.log('encore')\n", 5000));
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_FINISHED);
	g_assert_nonnull(strstr(journal->str, "encore\n"));
	g_assert_null(strstr(journal->str, "Script exited"));
}

static void
test_shutdown_sans_script(void)
{
	g_assert_false(script_engine_is_running());
	script_engine_shutdown();
	g_assert_false(script_engine_is_running());
}

/* Dernier cas : script_engine_shutdown retire les fonctions de journal et de fin (remises ensuite pour la forme). */
static void
test_shutdown_pendant_boucle(void)
{
	int maitre, esclave;
	struct termios tio;
	guint avant;
	gchar *chemin, *script;
	gint64 t0;

	if(openpty(&maitre, &esclave, NULL, NULL, NULL) != 0)
	{
		g_test_skip("openpty indisponible");
		return;
	}
	chemin = g_strdup(ttyname(esclave));
	/* Marque d'origine : CSTOPB posé ; le script ouvre avec 1 bit de stop ; la fermeture doit le restaurer */
	g_assert_cmpint(tcgetattr(esclave, &tio), ==, 0);
	tio.c_cflag |= CSTOPB;
	g_assert_cmpint(tcsetattr(esclave, TCSANOW, &tio), ==, 0);
	g_main_context_default();
	avant = compter_descripteurs();
	script = g_strdup_printf("assert(gtkterm.open(1, 'serial', '%s'))\ngtkterm.log('ouvert')\nwhile true do end\n", chemin);
	stub_reset();
	g_string_truncate(journal, 0);
	fini = FALSE;
	script_engine_run(script);
	g_assert_true(iterer_jusqu_a("ouvert\n", 10000));
	t0 = g_get_monotonic_time();
	script_engine_shutdown();
	g_assert_cmpint(g_get_monotonic_time() - t0, <, 3 * G_USEC_PER_SEC);
	g_assert_false(script_engine_is_running());
	g_assert_false(fini);	/* fonction de fin retirée : jamais appelée */
	g_assert_cmpuint(compter_descripteurs(), ==, avant);
	g_assert_cmpint(tcgetattr(esclave, &tio), ==, 0);
	g_assert_cmpuint(tio.c_cflag & CSTOPB, ==, CSTOPB);
	script_engine_set_log_func(sur_journal, NULL);
	script_engine_set_done_func(sur_fin, NULL);
	g_free(script);
	g_free(chemin);
	close(esclave);
	close(maitre);
}

/* ---- I2 : sortie série vidée avant la fermeture -------------------------------- */

typedef struct
{
	int maitre;
	gsize attendu;
	GByteArray *recu;
} LecteurLent;

/* Lit au plus 512 octets toutes les 2 ms (lecteur plus lent que l'envoi) et tient à jour la file d'émission simulée */
static gpointer
lire_lentement(gpointer data)
{
	LecteurLent *l = data;
	guint8 tampon[512];
	gint64 fin = g_get_monotonic_time() + 15 * G_USEC_PER_SEC;

	while(l->recu->len < l->attendu && g_get_monotonic_time() < fin)
	{
		struct pollfd pfd = { l->maitre, POLLIN, 0 };

		if(poll(&pfd, 1, 2) > 0)
		{
			ssize_t n = read(l->maitre, tampon, sizeof(tampon));

			if(n > 0)
			{
				g_byte_array_append(l->recu, tampon, (guint)n);
				g_atomic_int_set(&sortie_en_attente, (gint)(l->attendu - l->recu->len));
			}
			else if(n == 0 || (errno != EAGAIN && errno != EINTR))
				break;
		}
		g_usleep(2000);
	}
	return NULL;
}

static void
test_vidange_avant_fermeture(void)
{
	int maitre, esclave;
	LecteurLent l;
	GThread *th;
	gchar *script;
	gboolean conforme = TRUE;
	struct termios tio;

	if(openpty(&maitre, &esclave, NULL, NULL, NULL) != 0)
	{
		g_test_skip("openpty indisponible");
		return;
	}
	/* Maître brut : aucun traitement de ligne, aucun écho */
	g_assert_cmpint(tcgetattr(maitre, &tio), ==, 0);
	cfmakeraw(&tio);
	g_assert_cmpint(tcsetattr(maitre, TCSANOW, &tio), ==, 0);
	l.maitre = maitre;
	l.attendu = 65536;
	l.recu = g_byte_array_new();
	g_atomic_int_set(&sortie_en_attente, (gint)l.attendu);
	g_atomic_int_set(&sortie_simulee, 1);
	th = g_thread_new("lecteur-lent", lire_lentement, &l);
	/* Envoi puis fin du script : la fermeture de fin de script ne doit rien jeter */
	script = g_strdup_printf("local bloc = {}\n"
	                         "for i = 0, 250 do bloc[#bloc + 1] = string.char(i) end\n"
	                         "local donnees = string.rep(table.concat(bloc), 262):sub(1, 65536)\n"
	                         "assert(gtkterm.open(1, 'serial', '%s', 115200))\n"
	                         "assert(gtkterm.send(1, donnees, 20000))\n", ttyname(esclave));
	stub_reset();
	g_assert_true(executer(script, 20000));
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_FINISHED);
	g_thread_join(th);
	g_atomic_int_set(&sortie_simulee, 0);
	g_assert_cmpuint(l.recu->len, ==, 65536);
	for(guint i = 0; i < l.recu->len && conforme; i++)
		conforme = (l.recu->data[i] == i % 251);
	g_assert_true(conforme);
	g_byte_array_unref(l.recu);
	g_free(script);
	close(esclave);
	close(maitre);
}

/* File d'émission qui ne se vide jamais : vidange bornée (2 s au plus), aucune vidange si l'arrêt est demandé (os.exit) */
static void
test_vidange_bornee(void)
{
	int maitre, esclave;
	gchar *script;
	gdouble fin_script;

	if(openpty(&maitre, &esclave, NULL, NULL, NULL) != 0)
	{
		g_test_skip("openpty indisponible");
		return;
	}
	g_atomic_int_set(&sortie_en_attente, 4096);
	g_atomic_int_set(&sortie_simulee, 1);
	/* 4096 octets à 9600 bauds : 4096 * 10 / 9600 + 0,5 s = 4,8 s, plafonné à 2 s */
	script = g_strdup_printf("assert(gtkterm.open(1, 'serial', '%s', 9600))\n"
	                         "assert(gtkterm.send(1, 'x'))\n"
	                         "gtkterm.log(string.format('envoye %%.6f', gtkterm.time()))\n", ttyname(esclave));
	stub_reset();
	g_assert_true(executer(script, 10000));
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_FINISHED);
	fin_script = nombre_apres("envoye ");
	g_assert_cmpfloat((gdouble)fin_us / G_USEC_PER_SEC - fin_script, >=, 1.9);
	g_assert_cmpfloat((gdouble)fin_us / G_USEC_PER_SEC - fin_script, <, 3.0);
	g_free(script);

	script = g_strdup_printf("assert(gtkterm.open(1, 'serial', '%s', 9600))\n"
	                         "assert(gtkterm.send(1, 'x'))\n"
	                         "gtkterm.log(string.format('envoye %%.6f', gtkterm.time()))\n"
	                         "os.exit(0)\n", ttyname(esclave));
	g_assert_true(executer(script, 10000));
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_FINISHED);
	fin_script = nombre_apres("envoye ");
	g_assert_cmpfloat((gdouble)fin_us / G_USEC_PER_SEC - fin_script, <, 0.5);
	g_atomic_int_set(&sortie_simulee, 0);
	g_free(script);
	close(esclave);
	close(maitre);
}

/* ---- I3, I4 : résolution sur le thread Lua, adresses essayées l'une après l'autre ---- */

static void
test_client_tcp_localhost(void)
{
	guint16 port;
	int ecoute = ecoute_locale(&port, 4);
	gchar *script;

	/* localhost rend souvent ::1 d'abord : refus asynchrone, puis 127.0.0.1 (seule adresse écoutée) */
	script = g_strdup_printf("local ok, err = gtkterm.open(1, 'tcp_client', 'localhost', %u, 3000)\n"
	                         "gtkterm.log('localhost ' .. tostring(ok) .. ' ' .. tostring(err) .. ' ' .. gtkterm.status(1))\n"
	                         "ok, err = gtkterm.open(2, 'tcp_client', 'nulle-part.invalid', %u)\n"
	                         "gtkterm.log('inconnu ' .. tostring(ok) .. ' ' .. tostring(err) .. ' ' .. gtkterm.status(2))\n",
	                         port, port);
	stub_reset();
	g_atomic_int_set(&resolution_sur_principal, 0);
	g_assert_true(executer(script, 10000));
	g_assert_true(journal_contient("localhost true nil open\n"));
	g_assert_true(journal_contient("inconnu false Cannot resolve nulle-part.invalid: "));
	g_assert_true(journal_contient(" closed\n"));
	g_assert_cmpint(g_atomic_int_get(&resolution_sur_principal), ==, 0);
	g_free(script);
	close(ecoute);
}

/* ---- I5 : arrêt dans une boucle d'appels lents de la bibliothèque standard ---------- */

static void
test_stop_boucle_appels_lents(void)
{
	stub_reset();
	verifier_arret("while true do os.execute('sleep 0.2') end\n", 300);
	verifier_arret("while true do io.popen('sleep 0.2'):close() end\n", 300);
}

/* ---- M2 : finaliseurs après le nettoyage de fin de script ------------------------- */

static void
test_finaliseurs_apres_nettoyage(void)
{
	guint16 port = port_libre();
	guint avant;
	gchar *script;

	g_main_context_default();
	avant = compter_descripteurs();
	/* Objet inaccessible dès sa création : son __gc s'exécute au plus tard à la fermeture de l'état Lua */
	script = g_strdup_printf("setmetatable({}, {__gc = function() gtkterm.open(1, 'tcp_server', %u) end})\n", port);
	stub_reset();
	g_assert_true(executer(script, 5000));
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_FINISHED);
	g_assert_cmpuint(compter_descripteurs(), ==, avant);
	g_free(script);
	script = g_strdup_printf("local ok, err = gtkterm.open(1, 'tcp_server', %u)\n"
	                         "gtkterm.log('relance ' .. tostring(ok) .. ' ' .. tostring(err))\n", port);
	g_assert_true(executer(script, 5000));
	g_assert_true(journal_contient("relance true nil\n"));
	g_free(script);
	/* Finaliseur qui boucle sur l'API : refusé, l'état Lua se ferme */
	g_assert_true(executer("setmetatable({}, {__gc = function() while true do gtkterm.sleep(10) end end})\n", 5000));
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_FINISHED);
}

/* ---- Vague 2 : messages traduits, erreurs d'arguments, refus de connexion, vidange ---- */

/* M1 : les messages journalisés par le thread Lua passent par gettext (seul LC_NUMERIC est en « C » dans ce thread). Le catalogue
 * français compilé par la construction est celui de GTKTERM_PO_DIR/fr/LC_MESSAGES/gtkterm.mo. */
static void
test_messages_traduits(void)
{
	const gchar *po = g_getenv("GTKTERM_PO_DIR");
	gchar *ancienne = g_strdup(setlocale(LC_ALL, NULL));
	gchar *ancien_langage = g_strdup(g_getenv("LANGUAGE"));
	gchar *catalogue = po != NULL ? g_build_filename(po, "fr", "LC_MESSAGES", "gtkterm.mo", NULL) : NULL;

	if(catalogue == NULL || !g_file_test(catalogue, G_FILE_TEST_EXISTS) || setlocale(LC_ALL, "fr_FR.UTF-8") == NULL)
	{
		g_test_skip("catalogue fr ou locale fr_FR.UTF-8 indisponible");
		goto fin;
	}
	g_unsetenv("LANGUAGE");
	bindtextdomain("gtkterm", po);
	bind_textdomain_codeset("gtkterm", "UTF-8");
	textdomain("gtkterm");

	stub_reset();
	g_assert_true(executer("error('boum')\n", 5000));
	g_assert_true(journal_contient("Erreur Lua : script:1: boum\n"));
	g_assert_true(executer("os.exit(3)\n", 5000));
	g_assert_true(journal_contient("Script termin\xc3\xa9 avec le code 3\n"));
	/* Les nombres restent avec un point dans ce thread, même sous une locale française */
	g_assert_true(executer("gtkterm.log(tostring(2.5))\n", 5000));
	g_assert_true(journal_contient("2.5\n"));

	textdomain("messages");
	if(ancien_langage != NULL)
		g_setenv("LANGUAGE", ancien_langage, TRUE);
	setlocale(LC_ALL, ancienne);
fin:
	g_free(catalogue);
	g_free(ancienne);
	g_free(ancien_langage);
}

/* M6 : save_csv convertit tout avant fopen ; un __tostring qui lève ne laisse aucun FILE* ouvert, et rien n'est écrit */
static void
test_save_csv_tostring_leve(void)
{
	gchar *racine = g_dir_make_tmp("gtkterm-csv-XXXXXX", NULL);
	gchar *chemin = g_build_filename(racine, "sortie.csv", NULL);
	gchar *script, *contenu = NULL;
	guint avant;

	g_main_context_default();
	stub_reset();
	avant = compter_descripteurs();
	script = g_strdup_printf("local piege = setmetatable({}, {__tostring = function() error('boum') end})\n"
	                         "local ok, err = pcall(gtkterm.save_csv, '%s', 'a', piege)\n"
	                         "gtkterm.log('csv ' .. tostring(ok))\n"
	                         "assert(gtkterm.save_csv('%s', 'a', 1, true))\n", chemin, chemin);
	g_assert_true(executer(script, 5000));
	g_assert_true(journal_contient("csv false\n"));
	g_assert_cmpuint(compter_descripteurs(), ==, avant);
	g_assert_true(g_file_get_contents(chemin, &contenu, NULL, NULL));
	g_assert_cmpstr(contenu, ==, "a,1,true\n");
	g_free(contenu);
	g_unlink(chemin);
	g_free(script);

	/* 100 colonnes : les conversions restent sur la pile, qui doit être agrandie (LUA_MINSTACK = 20 places seulement) */
	script = g_strdup_printf("local t = {}\nfor i = 1, 100 do t[i] = i end\nassert(gtkterm.save_csv('%s', table.unpack(t)))\n", chemin);
	g_assert_true(executer(script, 5000));
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_FINISHED);
	g_assert_true(g_file_get_contents(chemin, &contenu, NULL, NULL));
	g_assert_true(g_str_has_prefix(contenu, "1,2,3,"));
	g_assert_true(g_str_has_suffix(contenu, ",99,100\n"));
	g_free(contenu);
	g_unlink(chemin);
	g_rmdir(racine);
	g_free(script);
	g_free(chemin);
	g_free(racine);
}

/* print avec un __tostring qui lève : l'erreur se propage (pcall l'attrape, sinon le script finit en erreur), rien n'est écrit */
static void
test_print_tostring_leve(void)
{
	stub_reset();
	g_assert_true(executer("local piege = setmetatable({}, {__tostring = function() error('boum') end})\n"
	                       "local ok, err = pcall(print, 'avant', piege)\n"
	                       "print('apres ' .. tostring(ok) .. ' ' .. tostring(err):match('boum'))\n", 5000));
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_FINISHED);
	g_assert_true(journal_contient("apres false boum\n"));
	g_assert_null(strstr(journal->str, "avant"));
	g_assert_true(executer("print(setmetatable({}, {__tostring = function() error('boum') end}))\n", 5000));
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_ERROR);
	g_assert_true(journal_contient("Lua error: script:1: boum\n"));
}

/* Fonctions de la bibliothèque standard enveloppées : le nom et la position de l'appelant restent dans les messages d'argument */
static void
test_std_guard_messages(void)
{
	stub_reset();
	g_assert_true(executer("local ok, err = pcall(function() local f = io.open(nil) end)\n"
	                       "gtkterm.log('A ' .. err)\n"
	                       "ok, err = pcall(io.open, nil)\n"
	                       "gtkterm.log('B ' .. err)\n"
	                       "ok, err = pcall(function() local r = os.execute({}) end)\n"
	                       "gtkterm.log('C ' .. err)\n"
	                       "local f = assert(io.open('/dev/null', 'r'))\n"
	                       "ok, err = pcall(function() return f:read('x') end)\n"
	                       "gtkterm.log('D ' .. err)\n"
	                       "f:close()\n", 5000));
	g_assert_true(journal_contient("A script:1: bad argument #1 to 'open' (string expected, got nil)\n"));
	g_assert_true(journal_contient("B bad argument #1 to 'io.open' (string expected, got nil)\n"));
	g_assert_true(journal_contient("C script:5: bad argument #1 to 'execute' (string expected, got table)\n"));
	g_assert_null(strstr(journal->str, "'?'"));
	/* Méthode : le numéro d'argument compte l'objet fichier (#2) ; le nom et la position sont ceux du script */
	g_assert_true(journal_contient("D script:8: bad argument #2 to 'read' (invalid format)\n"));
}

/* ---- Client TCP : délai nul, nom donné dans la raison d'échec ---------------------- */

static void
test_client_tcp_delai_nul_et_nom(void)
{
	guint16 port = port_libre();
	gchar *script;
	gchar *attendu;
	gint avant;

	stub_reset();
	avant = g_atomic_int_get(&appels_resolution);
	/* Délai nul ou négatif : « timeout » avant toute résolution */
	g_assert_true(executer("local ok, err = gtkterm.open(1, 'tcp_client', 'nulle-part.invalid', 4000, 0)\n"
	                       "gtkterm.log('zero ' .. tostring(ok) .. ' ' .. tostring(err) .. ' ' .. gtkterm.status(1))\n"
	                       "ok, err = gtkterm.open(1, 'tcp_client', '127.0.0.1', 4000, -5)\n"
	                       "gtkterm.log('negatif ' .. tostring(ok) .. ' ' .. tostring(err) .. ' ' .. gtkterm.status(1))\n", 5000));
	g_assert_true(journal_contient("zero false timeout closed\n"));
	g_assert_true(journal_contient("negatif false timeout closed\n"));
	g_assert_cmpint(g_atomic_int_get(&appels_resolution), ==, avant);

	/* Rien n'écoute : le refus nomme l'hôte du script devant l'adresse numérique essayée en dernier (127.0.0.1) */
	script = g_strdup_printf("local ok, err = gtkterm.open(1, 'tcp_client', 'localhost', %u, 3000)\n"
	                         "gtkterm.log('refus ' .. tostring(ok) .. ' ' .. tostring(err))\n"
	                         "ok, err = gtkterm.open(2, 'tcp_client', '127.0.0.1', %u, 3000)\n"
	                         "gtkterm.log('numerique ' .. tostring(ok) .. ' ' .. tostring(err))\n", port, port);
	g_assert_true(executer(script, 10000));
	attendu = g_strdup_printf("refus false Cannot connect to localhost (127.0.0.1:%u): ", port);
	g_assert_true(journal_contient(attendu));
	g_free(attendu);
	attendu = g_strdup_printf("numerique false Cannot connect to 127.0.0.1:%u: ", port);
	g_assert_true(journal_contient(attendu));
	g_free(attendu);
	g_free(script);
}

/* ---- M5 : client TCP parti pendant l'envoi d'un serveur : « not connected », jamais l'erreur système ---- */

typedef struct
{
	guint16 port;
	int client;
	gint etape;
} Depart;

static gboolean
piloter_depart(gpointer data)
{
	Depart *d = data;

	if(d->etape == 0 && journal_contient("ecoute listening\n"))
	{
		d->client = connecter(d->port);
		d->etape = 1;
	}
	else if(d->etape == 1 && journal_contient("connecte\n"))
	{
		/* RST plutôt que FIN : le prochain envoi du serveur reçoit ECONNRESET ou EPIPE */
		struct linger l = { 1, 0 };

		setsockopt(d->client, SOL_SOCKET, SO_LINGER, &l, sizeof(l));
		close(d->client);
		d->client = -1;
		d->etape = 2;
	}
	return G_SOURCE_CONTINUE;
}

static void
test_client_parti_pendant_envoi(void)
{
	Depart d = { port_libre(), -1, 0 };
	gchar *script = g_strdup_printf("assert(gtkterm.open(1, 'tcp_server', %u, '127.0.0.1'))\n"
	                                "gtkterm.log('ecoute ' .. gtkterm.status(1))\n"
	                                "local t0 = gtkterm.time()\n"
	                                "while gtkterm.status(1) ~= 'connected' and gtkterm.time() - t0 < 10 do gtkterm.sleep(5) end\n"
	                                "gtkterm.log('connecte')\n"
	                                "local gros = string.rep('x', 4096)\n"
	                                "local ok, err = true, nil\n"
	                                "for i = 1, 5000 do\n"
	                                "  ok, err = gtkterm.send(1, gros, 300)\n"
	                                "  if not ok then break end\n"
	                                "  gtkterm.sleep(1)\n"
	                                "end\n"
	                                "gtkterm.log('fin ' .. tostring(ok) .. ' ' .. tostring(err))\n", d.port);

	stub_reset();
	g_assert_true(executer_avec(script, 30000, piloter_depart, &d));
	g_assert_true(journal_contient("fin false not connected\n"));
	if(d.client >= 0)
		close(d.client);
	g_free(script);
}

/* ---- Vidange : échéance unique à bas débit, pause de FIFO d'une trame courte ---- */

/* 300 bauds, file simulée qui ne se vide jamais : une seule échéance de 2 s pour toute la vidange (pas de pause de FIFO en plus) */
static void
test_vidange_bas_debit(void)
{
	int maitre, esclave;
	gchar *script;
	gdouble duree;

	if(openpty(&maitre, &esclave, NULL, NULL, NULL) != 0)
	{
		g_test_skip("openpty indisponible");
		return;
	}
	g_atomic_int_set(&sortie_en_attente, 4096);
	g_atomic_int_set(&sortie_simulee, 1);
	script = g_strdup_printf("assert(gtkterm.open(1, 'serial', '%s', 300))\n"
	                         "assert(gtkterm.send(1, 'x'))\n"
	                         "gtkterm.log(string.format('envoye %%.6f', gtkterm.time()))\n", ttyname(esclave));
	stub_reset();
	g_assert_true(executer(script, 10000));
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_FINISHED);
	duree = (gdouble)fin_us / G_USEC_PER_SEC - nombre_apres("envoye ");
	g_assert_cmpfloat(duree, >=, 1.9);
	g_assert_cmpfloat(duree, <, 2.1);
	g_free(script);

	/* 300 bauds, rien en attente : la pause de FIFO (64 x 10 / 300 + 10 ms = 2,14 s) est bornée par la même échéance de 2 s */
	g_atomic_int_set(&sortie_en_attente, 0);
	script = g_strdup_printf("assert(gtkterm.open(1, 'serial', '%s', 300))\n"
	                         "assert(gtkterm.send(1, 'x'))\n"
	                         "gtkterm.log(string.format('envoye %%.6f', gtkterm.time()))\n", ttyname(esclave));
	stub_reset();
	g_assert_true(executer(script, 10000));
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_FINISHED);
	duree = (gdouble)fin_us / G_USEC_PER_SEC - nombre_apres("envoye ");
	g_assert_cmpfloat(duree, >=, 1.9);
	g_assert_cmpfloat(duree, <, 2.1);
	g_atomic_int_set(&sortie_simulee, 0);
	g_free(script);
	close(esclave);
	close(maitre);
}

/* Trame courte : rien en attente au départ, mais la FIFO de l'UART peut encore contenir 64 octets : pause de 64 x 10 / débit
 * (533 ms à 1200 bauds), sautée si l'arrêt est demandé (os.exit). */
static void
test_vidange_trame_courte(void)
{
	int maitre, esclave;
	gchar *script;
	gdouble duree;

	if(openpty(&maitre, &esclave, NULL, NULL, NULL) != 0)
	{
		g_test_skip("openpty indisponible");
		return;
	}
	g_atomic_int_set(&sortie_en_attente, 0);
	g_atomic_int_set(&sortie_simulee, 1);
	script = g_strdup_printf("assert(gtkterm.open(1, 'serial', '%s', 1200))\n"
	                         "assert(gtkterm.send(1, 'RESET\\r\\n'))\n"
	                         "gtkterm.log(string.format('envoye %%.6f', gtkterm.time()))\n", ttyname(esclave));
	stub_reset();
	g_assert_true(executer(script, 10000));
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_FINISHED);
	duree = (gdouble)fin_us / G_USEC_PER_SEC - nombre_apres("envoye ");
	g_assert_cmpfloat(duree, >=, 64.0 * 10 / 1200);
	g_assert_cmpfloat(duree, <, 1.5);
	g_free(script);

	script = g_strdup_printf("assert(gtkterm.open(1, 'serial', '%s', 1200))\n"
	                         "assert(gtkterm.send(1, 'RESET\\r\\n'))\n"
	                         "gtkterm.log(string.format('envoye %%.6f', gtkterm.time()))\n"
	                         "os.exit(0)\n", ttyname(esclave));
	g_assert_true(executer(script, 10000));
	duree = (gdouble)fin_us / G_USEC_PER_SEC - nombre_apres("envoye ");
	g_assert_cmpfloat(duree, <, 0.3);
	g_atomic_int_set(&sortie_simulee, 0);
	g_free(script);
	close(esclave);
	close(maitre);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	thread_principal = g_thread_self();
	journal = g_string_new(NULL);
	script_engine_setup();
	script_engine_set_log_func(sur_journal, NULL);
	script_engine_set_done_func(sur_fin, NULL);

	g_test_add_func("/script_engine/erreur-avec-ports-ouverts", test_erreur_avec_ports_ouverts);
	g_test_add_func("/script_engine/envois-sur-le-thread-principal-et-dans-l-ordre", test_envois_sur_le_thread_principal_et_dans_l_ordre);
	g_test_add_func("/script_engine/get-macros-sur-le-thread-principal", test_get_macros_sur_le_thread_principal);
	g_test_add_func("/script_engine/macro-absente", test_macro_absente);
	g_test_add_func("/script_engine/get-lists", test_get_lists);
	g_test_add_func("/script_engine/send-macro-arguments", test_send_macro_arguments);
	g_test_add_func("/script_engine/wait-for-binaire", test_wait_for_binaire);
	g_test_add_func("/script_engine/pair-tcp-ferme", test_pair_tcp_ferme);
	g_test_add_func("/script_engine/executions-successives", test_executions_successives);
	g_test_add_func("/script_engine/nombres-lua-sous-locale-fr", test_nombres_lua_sous_locale_fr);
	g_test_add_func("/script_engine/exemple-10-mise-a-jour", test_exemple_10_mise_a_jour);
	g_test_add_func("/script_engine/serie-parametres", test_serie_parametres);
	g_test_add_func("/script_engine/send-complet-ou-delai", test_send_complet_ou_delai);
	g_test_add_func("/script_engine/send-hex", test_send_hex);
	g_test_add_func("/script_engine/port-perdu", test_port_perdu);
	g_test_add_func("/script_engine/serveur-tcp-status", test_serveur_tcp_status);
	g_test_add_func("/script_engine/client-tcp-delai", test_client_tcp_delai);
	g_test_add_func("/script_engine/liaison-etats-et-echec", test_liaison_etats_et_echec);
	g_test_add_func("/script_engine/arguments-extremes", test_arguments_extremes);
	g_test_add_func("/script_engine/debordement-signale-une-fois", test_debordement_signale_une_fois);
	g_test_add_func("/script_engine/stop-boucle-infinie", test_stop_boucle_infinie);
	g_test_add_func("/script_engine/stop-boucles-pcall", test_stop_boucles_pcall);
	g_test_add_func("/script_engine/stop-pendant-sleep", test_stop_pendant_sleep);
	g_test_add_func("/script_engine/stop-pendant-wait-for", test_stop_pendant_wait_for);
	g_test_add_func("/script_engine/stop-pendant-connexion", test_stop_pendant_connexion);
	g_test_add_func("/script_engine/stop-pendant-envoi", test_stop_pendant_envoi);
	g_test_add_func("/script_engine/os-exit", test_os_exit);
	g_test_add_func("/script_engine/print", test_print);
	g_test_add_func("/script_engine/raisons-de-fin", test_raisons_de_fin);
	g_test_add_func("/script_engine/journal-en-rafale", test_journal_en_rafale);
	g_test_add_func("/script_engine/journal-plafond", test_journal_plafond);
	g_test_add_func("/script_engine/journal-binaire", test_journal_binaire);
	g_test_add_func("/script_engine/journal-nuls-plafond", test_journal_nuls_plafond);
	g_test_add_func("/script_engine/stop-malgre-debug-sethook", test_stop_malgre_debug_sethook);
	g_test_add_func("/script_engine/stop-boucle-send-macro", test_stop_boucle_send_macro);
	g_test_add_func("/script_engine/relance-apres-arret", test_relance_apres_arret);
	g_test_add_func("/script_engine/vidange-avant-fermeture", test_vidange_avant_fermeture);
	g_test_add_func("/script_engine/vidange-bornee", test_vidange_bornee);
	g_test_add_func("/script_engine/client-tcp-localhost", test_client_tcp_localhost);
	g_test_add_func("/script_engine/stop-boucle-appels-lents", test_stop_boucle_appels_lents);
	g_test_add_func("/script_engine/finaliseurs-apres-nettoyage", test_finaliseurs_apres_nettoyage);
	g_test_add_func("/script_engine/messages-traduits", test_messages_traduits);
	g_test_add_func("/script_engine/save-csv-tostring-leve", test_save_csv_tostring_leve);
	g_test_add_func("/script_engine/print-tostring-leve", test_print_tostring_leve);
	g_test_add_func("/script_engine/std-guard-messages", test_std_guard_messages);
	g_test_add_func("/script_engine/client-tcp-delai-nul-et-nom", test_client_tcp_delai_nul_et_nom);
	g_test_add_func("/script_engine/client-parti-pendant-envoi", test_client_parti_pendant_envoi);
	g_test_add_func("/script_engine/vidange-bas-debit", test_vidange_bas_debit);
	g_test_add_func("/script_engine/vidange-trame-courte", test_vidange_trame_courte);
	g_test_add_func("/script_engine/shutdown-sans-script", test_shutdown_sans_script);
	g_test_add_func("/script_engine/shutdown-pendant-boucle", test_shutdown_pendant_boucle);
	return g_test_run();
}
