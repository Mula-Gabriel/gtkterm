/* Tests de script_port.c (cœur) : port d'un script Lua sur le transport (paire pty, TCP local), décodage hexadécimal, noms
 * de parité et de flux. La boucle GLib par défaut tourne dans les attentes (rappels du transport). */

#include <glib.h>
#include <errno.h>
#include <poll.h>
#include <pty.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "script_port.h"

/* ---- Aides ------------------------------------------------------------ */

static gboolean
attendre_etat(GtScriptPort *port, GtScriptPortState etat, guint ms)
{
	gint64 fin = g_get_monotonic_time() + (gint64)ms * 1000;

	while(gt_script_port_get_state(port) != etat)
	{
		if(g_get_monotonic_time() >= fin)
			return FALSE;
		g_main_context_iteration(NULL, FALSE);
		g_usleep(1000);
	}
	return TRUE;
}

static gboolean
attendre_longueur(GtRxQueue *q, gsize n, guint ms)
{
	gint64 fin = g_get_monotonic_time() + (gint64)ms * 1000;

	while(gt_rx_queue_length(q) < n)
	{
		if(g_get_monotonic_time() >= fin)
			return FALSE;
		g_main_context_iteration(NULL, FALSE);
		g_usleep(1000);
	}
	return TRUE;
}

/* Descripteurs du processus ouverts sur le périphérique rdev (Linux : /proc/self/fd ; stat suit le descripteur même quand
 * le nœud a disparu, comme /dev/pts/N une fois le maître fermé) */
static guint
compter_fd(dev_t rdev)
{
	GDir *dir = g_dir_open("/proc/self/fd", 0, NULL);
	const gchar *nom;
	guint n = 0;

	g_assert_nonnull(dir);
	while((nom = g_dir_read_name(dir)) != NULL)
	{
		gchar *lien = g_build_filename("/proc/self/fd", nom, NULL);
		struct stat st;

		if(stat(lien, &st) == 0 && S_ISCHR(st.st_mode) && st.st_rdev == rdev)
			n++;
		g_free(lien);
	}
	g_dir_close(dir);
	return n;
}

/* Lit jusqu'à n octets (ou moins au bout de ms) en faisant tourner la boucle GLib */
static gsize
lire(int fd, guint8 *buf, gsize n, guint ms)
{
	gsize lu = 0;
	gint64 fin = g_get_monotonic_time() + (gint64)ms * 1000;

	while(lu < n && g_get_monotonic_time() < fin)
	{
		struct pollfd pfd = { fd, POLLIN, 0 };

		if(poll(&pfd, 1, 10) > 0)
		{
			ssize_t r = read(fd, buf + lu, n - lu);

			if(r > 0)
				lu += (gsize)r;
		}
		g_main_context_iteration(NULL, FALSE);
	}
	return lu;
}

static void
verifier_octets(GBytes *b, const void *attendu, gsize taille)
{
	gsize n = 0;
	const guint8 *p = g_bytes_get_data(b, &n);

	g_assert_cmpuint(n, ==, taille);
	if(taille > 0)
		g_assert_cmpmem(p, n, attendu, taille);
}

/* Les chaînes restent celles de l'appelant : gt_script_port_open copie les paramètres (copie profonde par le transport) */
static void
params_serie(TransportParams *p, const gchar *chemin, guint debit, gint stop)
{
	memset(p, 0, sizeof(*p));
	p->type = TRANSPORT_SERIAL;
	p->port = (gchar *)chemin;
	p->baudrate = debit;
	p->bits = 8;
	p->stopbits = stop;
	p->parity = GT_PARITY_NONE;
	p->flow = GT_FLOW_NONE;
}

static void
params_tcp(TransportParams *p, TransportType type, const gchar *hote, const gchar *port)
{
	memset(p, 0, sizeof(*p));
	p->type = type;
	p->host = (gchar *)hote;
	p->tcp_port = (gchar *)port;
}

/* Socket d'écoute sur 127.0.0.1, port choisi par le système */
static int
ecoute_locale(guint16 *port)
{
	struct sockaddr_in adresse;
	socklen_t taille = sizeof(adresse);
	int fd = socket(AF_INET, SOCK_STREAM, 0);

	g_assert_cmpint(fd, >=, 0);
	memset(&adresse, 0, sizeof(adresse));
	adresse.sin_family = AF_INET;
	adresse.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	g_assert_cmpint(bind(fd, (struct sockaddr *)&adresse, sizeof(adresse)), ==, 0);
	g_assert_cmpint(listen(fd, 1), ==, 0);
	g_assert_cmpint(getsockname(fd, (struct sockaddr *)&adresse, &taille), ==, 0);
	*port = ntohs(adresse.sin_port);
	return fd;
}

static guint16
port_libre(void)
{
	guint16 port;

	close(ecoute_locale(&port));
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

/* ---- Série sur pty ------------------------------------------------------ */

static void
test_serie_pty(void)
{
	int maitre, esclave;
	gchar *chemin;
	TransportParams p;
	GtScriptPort *port;
	GError *err = NULL;
	struct termios tio;
	GBytes *b;
	guint8 buf[8];
	gint e = -1;

	if(openpty(&maitre, &esclave, NULL, NULL, NULL) != 0)
	{
		g_test_skip("openpty indisponible");
		return;
	}
	chemin = g_strdup(ttyname(esclave));
	params_serie(&p, chemin, 115200, 2);
	port = gt_script_port_open(&p, &err);
	g_assert_no_error(err);
	g_assert_nonnull(port);
	g_assert_cmpint(gt_script_port_get_state(port), ==, GT_SCRIPT_PORT_OPEN);
	g_assert_null(gt_script_port_dup_reason(port));

	/* Paramètres appliqués (une pty n'expose que le débit et CSTOPB) */
	g_assert_cmpint(tcgetattr(esclave, &tio), ==, 0);
	g_assert_cmpuint(get_port_baudrate(esclave), ==, 115200);
	g_assert_cmpuint(tio.c_cflag & CSTOPB, ==, CSTOPB);

	/* Réception dans la file */
	g_assert_cmpint(write(maitre, "abc", 3), ==, 3);
	g_assert_true(attendre_longueur(gt_script_port_get_queue(port), 3, 5000));
	b = gt_rx_queue_take_all(gt_script_port_get_queue(port));
	verifier_octets(b, "abc", 3);
	g_bytes_unref(b);

	/* Envoi */
	g_assert_cmpint(gt_script_port_try_send(port, (const guint8 *)"xyz", 3, &e), ==, 3);
	g_assert_cmpint(e, ==, 0);
	g_assert_cmpuint(lire(maitre, buf, 3, 5000), ==, 3);
	g_assert_cmpmem(buf, 3, "xyz", 3);

	gt_script_port_close(port);
	/* Fermeture : réglages d'origine restaurés (CSTOPB absent par défaut sur une pty) */
	g_assert_cmpint(tcgetattr(esclave, &tio), ==, 0);
	g_assert_cmpuint(tio.c_cflag & CSTOPB, ==, 0);
	close(esclave);
	close(maitre);
	g_free(chemin);
}

static void
test_peripherique_absent(void)
{
	TransportParams p;
	GError *err = NULL;

	params_serie(&p, "/nonexistent/gtkterm-test-tty", 9600, 1);
	g_assert_null(gt_script_port_open(&p, &err));
	g_assert_error(err, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_OPEN);
	g_clear_error(&err);
}

static void
test_maitre_ferme(void)
{
	int maitre, esclave;
	gchar *chemin, *raison;
	TransportParams p;
	GtScriptPort *port;
	GError *err = NULL;
	struct stat st;
	gint e = 0;

	if(openpty(&maitre, &esclave, NULL, NULL, NULL) != 0)
	{
		g_test_skip("openpty indisponible");
		return;
	}
	chemin = g_strdup(ttyname(esclave));
	params_serie(&p, chemin, 9600, 1);
	port = gt_script_port_open(&p, &err);
	g_assert_no_error(err);
	g_assert_cmpint(fstat(esclave, &st), ==, 0);
	g_assert_cmpuint(compter_fd(st.st_rdev), ==, 2);	/* celui du test et celui du port */
	close(maitre);
	g_assert_true(attendre_etat(port, GT_SCRIPT_PORT_LOST, 5000));
	/* Le port relâche son descripteur dès la perte, sans attendre la fin du script : un adaptateur USB débranché puis
	 * rebranché retrouve son nom (le noyau garde le numéro mineur tant qu'un descripteur reste ouvert) */
	g_assert_cmpuint(compter_fd(st.st_rdev), ==, 1);
	g_assert_true(gt_rx_queue_is_closed(gt_script_port_get_queue(port)));
	raison = gt_script_port_dup_reason(port);
	g_assert_nonnull(raison);
	g_assert_cmpuint(strlen(raison), >, 0);
	g_assert_false(g_str_has_suffix(raison, "\n"));
	g_free(raison);
	/* Après la perte, l'envoi échoue avec EIO */
	g_assert_cmpint(gt_script_port_try_send(port, (const guint8 *)"x", 1, &e), ==, -1);
	g_assert_cmpint(e, ==, EIO);
	gt_script_port_close(port);
	close(esclave);
	g_free(chemin);
}

static void
test_verrou(void)
{
	int maitre, esclave;
	gchar *chemin;
	TransportParams p;
	GtScriptPort *a, *b;
	GError *err = NULL;

	if(openpty(&maitre, &esclave, NULL, NULL, NULL) != 0)
	{
		g_test_skip("openpty indisponible");
		return;
	}
	chemin = g_strdup(ttyname(esclave));
	params_serie(&p, chemin, 9600, 1);
	/* lock_port n'est pas demandé par l'appelant : gt_script_port_open le force */
	a = gt_script_port_open(&p, &err);
	g_assert_no_error(err);
	b = gt_script_port_open(&p, &err);
	g_assert_null(b);
	g_assert_error(err, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_LOCKED);
	g_clear_error(&err);
	gt_script_port_close(a);
	b = gt_script_port_open(&p, &err);
	g_assert_no_error(err);
	g_assert_nonnull(b);
	gt_script_port_close(b);
	close(esclave);
	close(maitre);
	g_free(chemin);
}

/* ---- TCP ------------------------------------------------------------------ */

static void
test_client_tcp(void)
{
	guint16 num;
	int ecoute = ecoute_locale(&num), pair;
	gchar *port_txt = g_strdup_printf("%u", num);
	TransportParams p;
	GtScriptPort *port;
	GError *err = NULL;
	GBytes *b;
	guint8 buf[8];
	gint e = -1;

	params_tcp(&p, TRANSPORT_TCP_CLIENT, "127.0.0.1", port_txt);
	port = gt_script_port_open(&p, &err);
	g_assert_no_error(err);
	/* Connexion asynchrone : aucun rappel avant une itération de la boucle */
	g_assert_cmpint(gt_script_port_get_state(port), ==, GT_SCRIPT_PORT_CONNECTING);
	g_assert_true(attendre_etat(port, GT_SCRIPT_PORT_OPEN, 5000));
	pair = accept(ecoute, NULL, NULL);
	g_assert_cmpint(pair, >=, 0);

	g_assert_cmpint(write(pair, "pong", 4), ==, 4);
	g_assert_true(attendre_longueur(gt_script_port_get_queue(port), 4, 5000));
	b = gt_rx_queue_take_all(gt_script_port_get_queue(port));
	verifier_octets(b, "pong", 4);
	g_bytes_unref(b);

	g_assert_cmpint(gt_script_port_try_send(port, (const guint8 *)"ping", 4, &e), ==, 4);
	g_assert_cmpuint(lire(pair, buf, 4, 5000), ==, 4);
	g_assert_cmpmem(buf, 4, "ping", 4);

	gt_script_port_close(port);
	close(pair);
	close(ecoute);
	g_free(port_txt);
}

static void
test_connexion_refusee(void)
{
	gchar *port_txt = g_strdup_printf("%u", port_libre());
	TransportParams p;
	GtScriptPort *port;
	GError *err = NULL;

	params_tcp(&p, TRANSPORT_TCP_CLIENT, "127.0.0.1", port_txt);
	port = gt_script_port_open(&p, &err);
	if(port == NULL)
	{
		/* Refus immédiat du connect non bloquant : erreur d'ouverture */
		g_assert_error(err, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_CONNECT);
		g_clear_error(&err);
	}
	else
	{
		gchar *raison;

		g_assert_true(attendre_etat(port, GT_SCRIPT_PORT_LOST, 5000));
		raison = gt_script_port_dup_reason(port);
		g_assert_nonnull(strstr(raison, "Connection refused"));
		g_assert_false(g_str_has_suffix(raison, "\n"));
		g_assert_true(gt_rx_queue_is_closed(gt_script_port_get_queue(port)));
		g_free(raison);
		gt_script_port_close(port);
	}
	g_free(port_txt);
}

static void
test_serveur(void)
{
	guint16 num = port_libre();
	gchar *port_txt = g_strdup_printf("%u", num);
	TransportParams p;
	GtScriptPort *port;
	GError *err = NULL;
	GBytes *b;
	guint8 buf[8];
	gint e = -1;
	int client;

	params_tcp(&p, TRANSPORT_TCP_SERVER, "127.0.0.1", port_txt);
	port = gt_script_port_open(&p, &err);
	g_assert_no_error(err);
	g_assert_cmpint(gt_script_port_get_state(port), ==, GT_SCRIPT_PORT_LISTENING);
	/* Serveur sans client : rien d'écrit, pas d'erreur */
	g_assert_cmpint(gt_script_port_try_send(port, (const guint8 *)"x", 1, &e), ==, 0);
	g_assert_cmpint(e, ==, 0);

	client = connecter(num);
	g_assert_true(attendre_etat(port, GT_SCRIPT_PORT_CONNECTED, 5000));
	g_assert_cmpint(gt_script_port_try_send(port, (const guint8 *)"hi", 2, &e), ==, 2);
	g_assert_cmpuint(lire(client, buf, 2, 5000), ==, 2);
	g_assert_cmpmem(buf, 2, "hi", 2);
	g_assert_cmpint(write(client, "yo", 2), ==, 2);
	g_assert_true(attendre_longueur(gt_script_port_get_queue(port), 2, 5000));
	b = gt_rx_queue_take_all(gt_script_port_get_queue(port));
	verifier_octets(b, "yo", 2);
	g_bytes_unref(b);

	/* Client parti : on écoute de nouveau, la file reste ouverte pour le client suivant */
	close(client);
	g_assert_true(attendre_etat(port, GT_SCRIPT_PORT_LISTENING, 5000));
	g_assert_false(gt_rx_queue_is_closed(gt_script_port_get_queue(port)));
	g_assert_null(gt_script_port_dup_reason(port));
	gt_script_port_close(port);
	g_free(port_txt);
}

static void
test_eagain_pty_non_lu(void)
{
	int maitre, esclave;
	gchar *chemin;
	TransportParams p;
	GtScriptPort *port;
	GError *err = NULL;
	guint8 bloc[4096];
	gssize n = 0;
	gint e = 0;

	if(openpty(&maitre, &esclave, NULL, NULL, NULL) != 0)
	{
		g_test_skip("openpty indisponible");
		return;
	}
	chemin = g_strdup(ttyname(esclave));
	params_serie(&p, chemin, 9600, 1);
	port = gt_script_port_open(&p, &err);
	g_assert_no_error(err);
	memset(bloc, 'x', sizeof(bloc));
	/* Personne ne lit le maître : le tampon de la pty se remplit, l'écriture non bloquante finit en EAGAIN */
	for(guint i = 0; i < 4096; i++)
	{
		n = gt_script_port_try_send(port, bloc, sizeof(bloc), &e);
		if(n < 0)
			break;
	}
	g_assert_cmpint(n, ==, -1);
	g_assert_true(e == EAGAIN || e == EWOULDBLOCK);
	gt_script_port_close(port);
	close(esclave);
	close(maitre);
	g_free(chemin);
}

/* ---- Décodage hexadécimal, noms --------------------------------------------- */

static void
test_hex_valide(void)
{
	static const struct
	{
		const gchar *texte;
		const gchar *octets;
		gsize taille;
	} cas[] = {
		{ "48656c6C6f", "Hello", 5 },
		{ " 48 65\t6c\r\n6c 6f ", "Hello", 5 },
		{ "00ff7F", "\x00\xff\x7f", 3 },
		{ "", "", 0 },
		{ "  \n", "", 0 },
	};

	for(guint i = 0; i < G_N_ELEMENTS(cas); i++)
	{
		GError *err = NULL;
		GBytes *b = gt_script_hex_decode(cas[i].texte, &err);

		g_assert_no_error(err);
		g_assert_nonnull(b);
		verifier_octets(b, cas[i].octets, cas[i].taille);
		g_bytes_unref(b);
	}
}

static void
test_hex_invalide(void)
{
	static const struct
	{
		const gchar *texte;
		const gchar *message;
	} cas[] = {
		{ "4G", "invalid character at position 1" },
		{ "41 4", "odd number of hex digits" },
		{ "4 1", "invalid character at position 1" },	/* blanc à l'intérieur d'un octet */
		{ "41x2", "invalid character at position 2" },
		{ "0x41", "invalid character at position 1" },
	};

	for(guint i = 0; i < G_N_ELEMENTS(cas); i++)
	{
		GError *err = NULL;

		g_assert_null(gt_script_hex_decode(cas[i].texte, &err));
		g_assert_error(err, G_CONVERT_ERROR, G_CONVERT_ERROR_ILLEGAL_SEQUENCE);
		g_assert_cmpstr(err->message, ==, cas[i].message);
		g_clear_error(&err);
	}
}

static void
test_noms(void)
{
	GtParity parite = GT_PARITY_EVEN;
	GtFlow flux = GT_FLOW_RTSCTS;

	g_assert_true(gt_script_parity_from_name("none", &parite));
	g_assert_cmpint(parite, ==, GT_PARITY_NONE);
	g_assert_true(gt_script_parity_from_name("odd", &parite));
	g_assert_cmpint(parite, ==, GT_PARITY_ODD);
	g_assert_true(gt_script_parity_from_name("even", &parite));
	g_assert_cmpint(parite, ==, GT_PARITY_EVEN);
	g_assert_false(gt_script_parity_from_name("mark", &parite));
	g_assert_false(gt_script_parity_from_name(NULL, &parite));

	g_assert_true(gt_script_flow_from_name("none", &flux));
	g_assert_cmpint(flux, ==, GT_FLOW_NONE);
	g_assert_true(gt_script_flow_from_name("xon", &flux));
	g_assert_cmpint(flux, ==, GT_FLOW_XONXOFF);
	g_assert_true(gt_script_flow_from_name("rts", &flux));
	g_assert_cmpint(flux, ==, GT_FLOW_RTSCTS);
	g_assert_false(gt_script_flow_from_name("rs485", &flux));
	g_assert_false(gt_script_flow_from_name(NULL, &flux));
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/script_port/serie-pty", test_serie_pty);
	g_test_add_func("/script_port/peripherique-absent", test_peripherique_absent);
	g_test_add_func("/script_port/maitre-ferme", test_maitre_ferme);
	g_test_add_func("/script_port/verrou", test_verrou);
	g_test_add_func("/script_port/client-tcp", test_client_tcp);
	g_test_add_func("/script_port/connexion-refusee", test_connexion_refusee);
	g_test_add_func("/script_port/serveur", test_serveur);
	g_test_add_func("/script_port/eagain-pty-non-lu", test_eagain_pty_non_lu);
	g_test_add_func("/script_port/hex-valide", test_hex_valide);
	g_test_add_func("/script_port/hex-invalide", test_hex_invalide);
	g_test_add_func("/script_port/noms", test_noms);
	return g_test_run();
}
