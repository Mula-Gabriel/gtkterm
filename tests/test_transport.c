/* Tests d'intégration du transport (transport.c, cœur sans interface) sur paire pty et TCP local.
 * ioctl est intercepté (-Wl,--wrap=ioctl) pour simuler les lignes modem RTS/DTR qu'une pty n'a pas. */

#include <glib.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <stdarg.h>
#include <string.h>
#include <netdb.h>
#include <termios.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "transport.h"
#include "config_file.h"
#include "test_util.h"

/* ---- Lignes modem simulées ------------------------------------------- */

int __real_ioctl(int fd, unsigned long requete, ...);
int __real_getaddrinfo(const char *noeud, const char *service, const struct addrinfo *indices, struct addrinfo **res);

/* localhost : ::1 puis 127.0.0.1, toujours (repli d'adresse déterministe) */
int
__wrap_getaddrinfo(const char *noeud, const char *service, const struct addrinfo *indices, struct addrinfo **res)
{
	int ret;

	if(test_localhost_deterministe(__real_getaddrinfo, noeud, service, indices, res, &ret))
		return ret;
	return __real_getaddrinfo(noeud, service, indices, res);
}

static int modem_fd = -1;
static int modem_etat = 0;
static int changements_rts = 0;
static int appels_bis = 0;	/* TIOCMBIS sur modem_fd */
static int appels_bic = 0;	/* TIOCMBIC sur modem_fd */
static gint64 instant_bic = 0;	/* heure monotone du dernier TIOCMBIC */

int
__wrap_ioctl(int fd, unsigned long requete, ...)
{
	va_list ap;
	void *arg;

	va_start(ap, requete);
	arg = va_arg(ap, void *);
	va_end(ap);

	if(fd == modem_fd && (requete == TIOCMGET || requete == TIOCMSET || requete == TIOCMBIS || requete == TIOCMBIC))
	{
		int ancien = modem_etat;

		if(requete == TIOCMGET)
			*(int *)arg = modem_etat;
		else if(requete == TIOCMSET)
			modem_etat = *(int *)arg;
		else if(requete == TIOCMBIS)
		{
			modem_etat |= *(int *)arg;
			appels_bis++;
		}
		else
		{
			modem_etat &= ~*(int *)arg;
			appels_bic++;
			instant_bic = g_get_monotonic_time();
		}
		if((ancien ^ modem_etat) & TIOCM_RTS)
			changements_rts++;
		return 0;
	}
	return __real_ioctl(fd, requete, arg);
}

/* ---- Récepteur des rappels ------------------------------------------- */

typedef struct
{
	GByteArray *octets;
	GArray *evenements;	/* TransportEvent */
	gchar *detail;
	GByteArray *ecrits;	/* on_sent : octets réellement écrits */
	GByteArray *etiquettes;	/* on_sent : étiquette de chaque octet écrit (GPOINTER_TO_INT) */
	guint appels_ecrits;
	gint64 dernier_ecrit;	/* heure monotone du dernier on_sent */
} Recepteur;

static void
recepteur_init(Recepteur *r)
{
	r->octets = g_byte_array_new();
	r->evenements = g_array_new(FALSE, FALSE, sizeof(TransportEvent));
	r->detail = NULL;
	r->ecrits = g_byte_array_new();
	r->etiquettes = g_byte_array_new();
	r->appels_ecrits = 0;
	r->dernier_ecrit = 0;
}

static void
recepteur_liberer(Recepteur *r)
{
	g_byte_array_unref(r->octets);
	g_array_unref(r->evenements);
	g_free(r->detail);
	g_byte_array_unref(r->ecrits);
	g_byte_array_unref(r->etiquettes);
}

static void
sur_donnees(const guint8 *data, gsize len, gpointer user_data)
{
	g_byte_array_append(((Recepteur *)user_data)->octets, data, (guint)len);
}

static void
sur_evenement(TransportEvent ev, const gchar *detail, gpointer user_data)
{
	Recepteur *r = user_data;

	g_array_append_val(r->evenements, ev);
	g_free(r->detail);
	r->detail = g_strdup(detail);
}

static void
sur_ecrits(const guint8 *data, gsize len, gpointer tag, gpointer user_data)
{
	Recepteur *r = user_data;
	guint8 e = (guint8)GPOINTER_TO_INT(tag);

	g_assert_cmpuint(len, >, 0);
	g_byte_array_append(r->ecrits, data, (guint)len);
	for(gsize i = 0; i < len; i++)
		g_byte_array_append(r->etiquettes, &e, 1);
	r->appels_ecrits++;
	r->dernier_ecrit = g_get_monotonic_time();
}

static const TransportCallbacks rappels = { sur_donnees, sur_evenement, NULL };
static const TransportCallbacks rappels_emission = { sur_donnees, sur_evenement, sur_ecrits };

static guint
compter(Recepteur *r, TransportEvent ev)
{
	guint n = 0;

	for(guint i = 0; i < r->evenements->len; i++)
		if(g_array_index(r->evenements, TransportEvent, i) == ev)
			n++;
	return n;
}

static gboolean
attendre_evenement(Recepteur *r, TransportEvent ev, guint ms)
{
	gint64 fin = g_get_monotonic_time() + (gint64)ms * 1000;

	while(compter(r, ev) == 0 && g_get_monotonic_time() < fin)
	{
		g_main_context_iteration(NULL, FALSE);
		g_usleep(1000);
	}
	return compter(r, ev) > 0;
}

static gboolean
attendre_octets(Recepteur *r, guint n, guint ms)
{
	gint64 fin = g_get_monotonic_time() + (gint64)ms * 1000;

	while(r->octets->len < n && g_get_monotonic_time() < fin)
	{
		g_main_context_iteration(NULL, FALSE);
		g_usleep(1000);
	}
	return r->octets->len >= n;
}

/* ---- Paramètres et ports ---------------------------------------------- */

static void
params_serie(TransportParams *p, const gchar *port)
{
	memset(p, 0, sizeof(*p));
	p->type = TRANSPORT_SERIAL;
	p->port = g_strdup(port);
	p->baudrate = 115200;
	p->bits = 8;
	p->stopbits = 1;
	p->parity = GT_PARITY_NONE;
	p->flow = GT_FLOW_NONE;
	p->lock_port = FALSE;
}

static void
params_tcp(TransportParams *p, TransportType type, guint16 port)
{
	memset(p, 0, sizeof(*p));
	p->type = type;
	p->host = g_strdup("127.0.0.1");
	p->tcp_port = g_strdup_printf("%u", port);
}

typedef struct
{
	int maitre;
	int esclave;
} paire_pty_t;

static gboolean
ouvrir_pty(paire_pty_t *p)
{
	return openpty(&p->maitre, &p->esclave, NULL, NULL, NULL) == 0;
}

static void
fermer_pty(paire_pty_t *p)
{
	if(p->esclave >= 0)
		close(p->esclave);
	if(p->maitre >= 0)
		close(p->maitre);
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

static gsize
lire(int fd, guint8 *buf, gsize taille, guint ms)
{
	gsize lu = 0;
	gint64 fin = g_get_monotonic_time() + (gint64)ms * 1000;

	while(lu < taille && g_get_monotonic_time() < fin)
	{
		struct pollfd pfd = { fd, POLLIN, 0 };

		if(poll(&pfd, 1, 10) > 0)
		{
			ssize_t n = read(fd, buf + lu, taille - lu);

			if(n > 0)
				lu += (gsize)n;
		}
		g_main_context_iteration(NULL, FALSE);
	}
	return lu;
}

/* ---- C4 : aucun réglage hérité, restauration à la fermeture ----------- */

static void
test_termios_sans_heritage(void)
{
	paire_pty_t p;
	struct termios tio;
	TransportParams tp;
	GtTransport *t;
	GError *err = NULL;

	if(!ouvrir_pty(&p))
	{
		g_test_skip("openpty indisponible");
		return;
	}
	g_assert_cmpint(tcgetattr(p.esclave, &tio), ==, 0);
	/* Une pty impose CS8 et sans parité : seuls CRTSCTS, CSTOPB et le débit y sont observables. */
	tio.c_cflag = (tio.c_cflag & ~CSIZE) | CS8 | CSTOPB | CRTSCTS;
	cfsetospeed(&tio, B9600);
	cfsetispeed(&tio, B9600);
	g_assert_cmpint(tcsetattr(p.esclave, TCSANOW, &tio), ==, 0);

	params_serie(&tp, ttyname(p.esclave));
	t = gt_transport_open(&tp, &rappels, NULL, &err);
	g_assert_no_error(err);
	g_assert_nonnull(t);
	g_assert_cmpint(tcgetattr(gt_transport_get_fd(t), &tio), ==, 0);
	g_assert_cmpuint(tio.c_cflag & CSIZE, ==, CS8);
	g_assert_cmpuint(tio.c_cflag & (CSTOPB | CRTSCTS), ==, 0);
	/* Relu sans cfgetospeed : avant la glibc 2.42, un débit posé par BOTHER s'y relit 4096 (BOTHER) sur une pty. */
	g_assert_cmpuint(get_port_baudrate(gt_transport_get_fd(t)), ==, 115200);
	gt_transport_close(t);

	/* Fermeture d'une instance série : réglages d'origine restaurés, débit compris (relevé avant de le changer, M3) */
	g_assert_cmpint(tcgetattr(p.esclave, &tio), ==, 0);
	g_assert_cmpuint(tio.c_cflag & CSTOPB, ==, CSTOPB);
	g_assert_cmpuint(cfgetospeed(&tio), ==, B9600);
	transport_params_clear(&tp);
	fermer_pty(&p);
}

/* ---- Numéro de périphérique relevé à l'ouverture ----------------------- */

static void
test_numero_de_peripherique(void)
{
	paire_pty_t p;
	TransportParams tp;
	GtTransport *t;
	struct stat st;

	if(!ouvrir_pty(&p))
	{
		g_test_skip("openpty indisponible");
		return;
	}
	params_serie(&tp, ttyname(p.esclave));
	g_assert_cmpint(stat(tp.port, &st), ==, 0);
	t = gt_transport_open(&tp, &rappels, NULL, NULL);
	g_assert_nonnull(t);
	g_assert_true(gt_transport_get_device_number(t) == st.st_rdev);
	g_assert_cmpuint(gt_transport_get_actual_baudrate(t), ==, 115200);
	gt_transport_close(t);
	transport_params_clear(&tp);
	fermer_pty(&p);
}

/* ---- C3 : RTS stable en RS485 ------------------------------------------ */

static void
test_rs485_rts_stable(void)
{
	paire_pty_t p;
	TransportParams tp;
	GtTransport *t;

	if(!ouvrir_pty(&p))
	{
		g_test_skip("openpty indisponible");
		return;
	}
	params_serie(&tp, ttyname(p.esclave));
	tp.flow = GT_FLOW_RS485;
	t = gt_transport_open(&tp, &rappels, NULL, NULL);
	g_assert_nonnull(t);
	modem_fd = gt_transport_get_fd(t);
	modem_etat = TIOCM_DTR;
	changements_rts = 0;

	for(gint i = 0; i < 10; i++)
		g_assert_cmpint(gt_transport_get_modem_lines(t), ==, TIOCM_DTR);
	g_assert_cmpint(changements_rts, ==, 0);

	/* RTS levé à l'envoi, baissé par la minuterie de fin de cycle (jamais d'attente bloquante) */
	g_assert_cmpint(gt_transport_send(t, (const guint8 *)"x", 1), ==, 1);
	g_assert_cmpint(changements_rts, ==, 1);
	for(gint64 fin = g_get_monotonic_time() + 2000000; changements_rts < 2 && g_get_monotonic_time() < fin; g_usleep(1000))
		g_main_context_iteration(NULL, FALSE);
	g_assert_cmpint(changements_rts, ==, 2);
	g_assert_cmpint(modem_etat & TIOCM_RTS, ==, 0);

	gt_transport_toggle_line(t, GT_MODEM_DTR);
	g_assert_cmpint(modem_etat & TIOCM_DTR, ==, 0);

	modem_fd = -1;
	gt_transport_close(t);
	transport_params_clear(&tp);
	fermer_pty(&p);
}

/* ---- Octets binaires transmis intacts ---------------------------------- */

static void
test_octets_binaires(void)
{
	paire_pty_t p;
	TransportParams tp;
	GtTransport *t;
	Recepteur r;

	if(!ouvrir_pty(&p))
	{
		g_test_skip("openpty indisponible");
		return;
	}
	recepteur_init(&r);
	params_serie(&tp, ttyname(p.esclave));
	t = gt_transport_open(&tp, &rappels, &r, NULL);
	g_assert_nonnull(t);
	g_assert_cmpint(write(p.maitre, "\xff\x00\x41", 3), ==, 3);
	g_assert_true(attendre_octets(&r, 3, 2000));
	g_assert_cmpmem(r.octets->data, r.octets->len, "\xff\x00\x41", 3);
	gt_transport_close(t);
	transport_params_clear(&tp);
	recepteur_liberer(&r);
	fermer_pty(&p);
}

/* ---- M3 : perte signalée à la fermeture du maître pty ------------------ */

static void
test_perte_maitre_pty(void)
{
	paire_pty_t p;
	TransportParams tp;
	GtTransport *t;
	Recepteur r;

	if(!ouvrir_pty(&p))
	{
		g_test_skip("openpty indisponible");
		return;
	}
	recepteur_init(&r);
	params_serie(&tp, ttyname(p.esclave));
	t = gt_transport_open(&tp, &rappels, &r, NULL);
	g_assert_nonnull(t);
	close(p.maitre);
	p.maitre = -1;
	g_assert_true(attendre_evenement(&r, TRANSPORT_EVENT_LOST, 2000));
	g_assert_cmpint(gt_transport_send(t, (const guint8 *)"x", 1), <, 0);
	g_assert_cmpint(gt_transport_get_modem_lines(t), ==, -1);
	gt_transport_close(t);
	transport_params_clear(&tp);
	recepteur_liberer(&r);
	fermer_pty(&p);
}

/* ---- Erreurs d'ouverture remontées par GError -------------------------- */

static void
test_erreurs_ouverture(void)
{
	TransportParams tp;
	GError *err = NULL;

	params_serie(&tp, "/nonexistent/gtkterm-test");
	g_assert_null(gt_transport_open(&tp, &rappels, NULL, &err));
	g_assert_error(err, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_OPEN);
	g_assert_nonnull(strstr(err->message, "/nonexistent/gtkterm-test"));
	g_clear_error(&err);
	transport_params_clear(&tp);

	params_serie(&tp, "/dev/null");
	g_assert_null(gt_transport_open(&tp, &rappels, NULL, &err));
	g_assert_error(err, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_NOT_TTY);
	g_clear_error(&err);
	transport_params_clear(&tp);
}

/* ---- C2 : pair TCP fermé ------------------------------------------------ */

static void
test_tcp_pair_ferme(void)
{
	TransportParams tp;
	GtTransport *t;
	Recepteur r;
	guint16 port;
	int ecoute, pair;
	gssize resultat = 0;
	int erreur = 0;

	recepteur_init(&r);
	ecoute = ecoute_locale(&port);
	params_tcp(&tp, TRANSPORT_TCP_CLIENT, port);
	t = gt_transport_open(&tp, &rappels, &r, NULL);
	g_assert_nonnull(t);
	pair = accept(ecoute, NULL, NULL);
	g_assert_cmpint(pair, >=, 0);
	g_assert_true(attendre_evenement(&r, TRANSPORT_EVENT_CONNECTED, 2000));

	close(pair);
	g_usleep(50000);
	/* Le premier envoi peut encore réussir (RST pas encore reçu) ; un suivant échoue au lieu de tuer le processus. */
	for(gint i = 0; i < 3; i++)
	{
		resultat = gt_transport_send(t, (const guint8 *)"abc", 3);
		erreur = errno;
		/* Pas d'itération de la boucle : LOST ne doit pas court-circuiter l'appel système send() */
		g_usleep(20000);
	}
	g_assert_cmpint(resultat, <, 0);
	g_assert_true(erreur == EPIPE || erreur == ECONNRESET);

	gt_transport_close(t);
	transport_params_clear(&tp);
	recepteur_liberer(&r);
	close(ecoute);
}

/* ---- M5 : fin de flux TCP client signalée ------------------------------ */

static void
test_tcp_fin_de_flux(void)
{
	TransportParams tp;
	GtTransport *t;
	Recepteur r;
	guint16 port;
	int ecoute, pair;

	recepteur_init(&r);
	ecoute = ecoute_locale(&port);
	params_tcp(&tp, TRANSPORT_TCP_CLIENT, port);
	t = gt_transport_open(&tp, &rappels, &r, NULL);
	g_assert_nonnull(t);
	pair = accept(ecoute, NULL, NULL);
	g_assert_true(attendre_evenement(&r, TRANSPORT_EVENT_CONNECTED, 2000));
	g_assert_cmpint(write(pair, "hi", 2), ==, 2);
	g_assert_true(attendre_octets(&r, 2, 2000));
	close(pair);
	g_assert_true(attendre_evenement(&r, TRANSPORT_EVENT_LOST, 2000));
	gt_transport_close(t);
	transport_params_clear(&tp);
	recepteur_liberer(&r);
	close(ecoute);
}

/* ---- Connexion refusée : erreur synchrone ou LOST asynchrone ----------- */

static void
test_tcp_connexion_refusee(void)
{
	TransportParams tp;
	GtTransport *t;
	Recepteur r;
	GError *err = NULL;

	recepteur_init(&r);
	params_tcp(&tp, TRANSPORT_TCP_CLIENT, port_libre());
	t = gt_transport_open(&tp, &rappels, &r, &err);
	if(t == NULL)
	{
		g_assert_error(err, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_CONNECT);
		g_clear_error(&err);
	}
	else
	{
		g_assert_true(attendre_evenement(&r, TRANSPORT_EVENT_LOST, 2000));
		g_assert_cmpuint(compter(&r, TRANSPORT_EVENT_CONNECTED), ==, 0);
		g_assert_nonnull(strstr(r.detail, "Cannot connect"));
		gt_transport_close(t);
	}
	transport_params_clear(&tp);
	recepteur_liberer(&r);
}

/* ---- I4 : adresses résolues essayées l'une après l'autre ----------------- */

static void
test_tcp_adresse_suivante(void)
{
	TransportParams tp;
	GtTransport *t;
	Recepteur r;
	guint16 port;
	int ecoute = ecoute_locale(&port);
	GError *err = NULL;

	/* localhost rend souvent ::1 d'abord (refus asynchrone : rien n'écoute en IPv6), puis 127.0.0.1 qui écoute */
	recepteur_init(&r);
	params_tcp(&tp, TRANSPORT_TCP_CLIENT, port);
	g_free(tp.host);
	tp.host = g_strdup("localhost");
	t = gt_transport_open(&tp, &rappels, &r, &err);
	g_assert_no_error(err);
	g_assert_nonnull(t);
	g_assert_true(attendre_evenement(&r, TRANSPORT_EVENT_CONNECTED, 2000));
	g_assert_cmpuint(compter(&r, TRANSPORT_EVENT_LOST), ==, 0);
	gt_transport_close(t);
	transport_params_clear(&tp);
	recepteur_liberer(&r);
	close(ecoute);
}

/* ---- Serveur TCP : client accepté, données, départ du client ----------- */

static void
test_tcp_serveur_evenements(void)
{
	TransportParams tp;
	GtTransport *t;
	Recepteur r;
	guint16 port = port_libre();
	guint8 buf[2];
	int client;

	recepteur_init(&r);
	params_tcp(&tp, TRANSPORT_TCP_SERVER, port);
	t = gt_transport_open(&tp, &rappels, &r, NULL);
	g_assert_nonnull(t);
	g_assert_cmpint(gt_transport_send(t, (const guint8 *)"x", 1), ==, 0);	/* aucun client */

	client = connecter(port);
	g_assert_true(attendre_evenement(&r, TRANSPORT_EVENT_CLIENT_CONNECTED, 2000));
	g_assert_cmpint(write(client, "hi", 2), ==, 2);
	g_assert_true(attendre_octets(&r, 2, 2000));
	g_assert_cmpint(gt_transport_send(t, (const guint8 *)"yo", 2), ==, 2);
	g_assert_cmpuint(lire(client, buf, 2, 2000), ==, 2);
	g_assert_cmpmem(buf, 2, "yo", 2);

	close(client);
	g_assert_true(attendre_evenement(&r, TRANSPORT_EVENT_CLIENT_GONE, 2000));
	g_assert_cmpint(gt_transport_send(t, (const guint8 *)"x", 1), ==, 0);
	g_assert_cmpuint(compter(&r, TRANSPORT_EVENT_LOST), ==, 0);

	gt_transport_close(t);
	transport_params_clear(&tp);
	recepteur_liberer(&r);
}

/* ---- M1 : fermeture selon le type ouvert (socket d'écoute libérée une fois) */

static void
test_tcp_serveur_fermeture_libere_le_port(void)
{
	TransportParams tp;
	GtTransport *t;

	params_tcp(&tp, TRANSPORT_TCP_SERVER, port_libre());
	t = gt_transport_open(&tp, &rappels, NULL, NULL);
	g_assert_nonnull(t);
	gt_transport_close(t);
	t = gt_transport_open(&tp, &rappels, NULL, NULL);
	g_assert_nonnull(t);
	gt_transport_close(t);
	transport_params_clear(&tp);
}

/* ---- Deux instances simultanées ---------------------------------------- */

static void
test_deux_instances(void)
{
	paire_pty_t p1, p2;
	TransportParams tp1, tp2;
	GtTransport *t1, *t2;
	Recepteur r1, r2;

	if(!ouvrir_pty(&p1) || !ouvrir_pty(&p2))
	{
		g_test_skip("openpty indisponible");
		return;
	}
	recepteur_init(&r1);
	recepteur_init(&r2);
	params_serie(&tp1, ttyname(p1.esclave));
	params_serie(&tp2, ttyname(p2.esclave));
	t1 = gt_transport_open(&tp1, &rappels, &r1, NULL);
	t2 = gt_transport_open(&tp2, &rappels, &r2, NULL);
	g_assert_nonnull(t1);
	g_assert_nonnull(t2);
	g_assert_cmpint(write(p1.maitre, "un", 2), ==, 2);
	g_assert_cmpint(write(p2.maitre, "deux", 4), ==, 4);
	g_assert_true(attendre_octets(&r1, 2, 2000));
	g_assert_true(attendre_octets(&r2, 4, 2000));
	g_assert_cmpmem(r1.octets->data, r1.octets->len, "un", 2);
	g_assert_cmpmem(r2.octets->data, r2.octets->len, "deux", 4);
	gt_transport_close(t1);
	gt_transport_close(t2);
	transport_params_clear(&tp1);
	transport_params_clear(&tp2);
	recepteur_liberer(&r1);
	recepteur_liberer(&r2);
	fermer_pty(&p1);
	fermer_pty(&p2);
}

/* ---- File d'émission ---------------------------------------------------- */

/* Motif reconnaissable : minuscules pour le premier bloc, majuscules pour le second (l'ordre des blocs se vérifie) */
static guint8 *
motif(gsize taille, gchar base)
{
	guint8 *d = g_malloc(taille);

	for(gsize i = 0; i < taille; i++)
		d[i] = (guint8)(base + (i % 26));
	return d;
}

/* Lit fd (non bloquant) par morceaux de morceau octets en itérant la boucle, jusqu'à attendu octets ou ms écoulées */
static void
lire_par_morceaux(int fd, GByteArray *dest, gsize attendu, gsize morceau, guint ms)
{
	gint64 fin = g_get_monotonic_time() + (gint64)ms * 1000;
	guint8 *buf = g_malloc(morceau);

	while(dest->len < attendu && g_get_monotonic_time() < fin)
	{
		ssize_t n = read(fd, buf, morceau);

		if(n > 0)
			g_byte_array_append(dest, buf, (guint)n);
		g_main_context_iteration(NULL, FALSE);
		if(n <= 0)
			g_usleep(500);
	}
	g_free(buf);
}

static void
non_bloquant(int fd)
{
	g_assert_cmpint(fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK), ==, 0);
}

/* 2 × 64 Kio sur une pty que personne ne lit : tout est accepté, puis arrive dans l'ordre à mesure que le maître lit par petits bouts ;
 * on_sent rend exactement les octets écrits, avec leur étiquette ; try_send attend que la file soit vide. */
static void
test_file_pty_ordre(void)
{
	const gsize bloc = 65536;
	paire_pty_t p;
	TransportParams tp;
	GtTransport *t;
	Recepteur r;
	GByteArray *lu = g_byte_array_new();
	guint8 *a = motif(bloc, 'a'), *b = motif(bloc, 'A');

	if(!ouvrir_pty(&p))
	{
		g_test_skip("openpty indisponible");
		return;
	}
	non_bloquant(p.maitre);
	recepteur_init(&r);
	params_serie(&tp, ttyname(p.esclave));
	t = gt_transport_open(&tp, &rappels_emission, &r, NULL);
	g_assert_nonnull(t);

	g_assert_cmpint(gt_transport_send_queued(t, a, bloc, GINT_TO_POINTER(1)), ==, (gssize)bloc);
	g_assert_cmpint(gt_transport_send_queued(t, b, bloc, GINT_TO_POINTER(2)), ==, (gssize)bloc);
	g_assert_cmpuint(gt_transport_get_queued(t), >, 0);
	g_assert_cmpuint(r.ecrits->len + gt_transport_get_queued(t), ==, 2 * bloc);
	errno = 0;
	g_assert_cmpint(gt_transport_try_send(t, (const guint8 *)"z", 1, GINT_TO_POINTER(3)), ==, -1);
	g_assert_cmpint(errno, ==, EAGAIN);

	lire_par_morceaux(p.maitre, lu, 2 * bloc, 1000, 10000);
	g_assert_cmpuint(lu->len, ==, 2 * bloc);
	g_assert_cmpmem(lu->data, bloc, a, bloc);
	g_assert_cmpmem(lu->data + bloc, bloc, b, bloc);
	g_assert_cmpuint(gt_transport_get_queued(t), ==, 0);
	g_assert_cmpmem(r.ecrits->data, r.ecrits->len, lu->data, lu->len);
	g_assert_cmpuint(r.appels_ecrits, >, 2);	/* plusieurs morceaux : la file a bien servi */
	for(gsize i = 0; i < 2 * bloc; i++)
		g_assert_cmpuint(r.etiquettes->data[i], ==, i < bloc ? 1 : 2);

	/* File vide : écriture directe et synchrone, on_sent compris */
	g_assert_cmpint(gt_transport_try_send(t, (const guint8 *)"z", 1, GINT_TO_POINTER(3)), ==, 1);
	g_assert_cmpint(gt_transport_send_queued(t, (const guint8 *)"yx", 2, GINT_TO_POINTER(4)), ==, 2);
	g_assert_cmpuint(r.ecrits->len, ==, 2 * bloc + 3);
	g_assert_cmpuint(r.etiquettes->data[2 * bloc], ==, 3);
	g_assert_cmpuint(r.etiquettes->data[2 * bloc + 2], ==, 4);

	gt_transport_close(t);
	transport_params_clear(&tp);
	recepteur_liberer(&r);
	g_byte_array_unref(lu);
	g_free(a);
	g_free(b);
	fermer_pty(&p);
}

/* Plafond : au-delà de la place restante, -1 / ENOBUFS, rien d'écrit ni de mis en file */
static void
test_file_plafond(void)
{
	paire_pty_t p;
	TransportParams tp;
	GtTransport *t;
	Recepteur r;
	guint8 *d = motif(GT_TRANSPORT_QUEUE_MAX + 1, 'a');
	GByteArray *lu = g_byte_array_new();
	gsize en_file;

	if(!ouvrir_pty(&p))
	{
		g_test_skip("openpty indisponible");
		return;
	}
	non_bloquant(p.maitre);
	recepteur_init(&r);
	params_serie(&tp, ttyname(p.esclave));
	t = gt_transport_open(&tp, &rappels_emission, &r, NULL);
	g_assert_nonnull(t);

	errno = 0;
	g_assert_cmpint(gt_transport_send_queued(t, d, GT_TRANSPORT_QUEUE_MAX + 1, NULL), ==, -1);
	g_assert_cmpint(errno, ==, ENOBUFS);
	g_assert_cmpuint(gt_transport_get_queued(t), ==, 0);
	g_assert_cmpuint(r.ecrits->len, ==, 0);
	lire_par_morceaux(p.maitre, lu, 1, 4096, 100);
	g_assert_cmpuint(lu->len, ==, 0);

	/* Une première moitié acceptée (pty non lue : presque tout en file), la seconde ne tient plus */
	g_assert_cmpint(gt_transport_send_queued(t, d, GT_TRANSPORT_QUEUE_MAX / 2 + 1, NULL), ==, GT_TRANSPORT_QUEUE_MAX / 2 + 1);
	en_file = gt_transport_get_queued(t);
	g_assert_cmpuint(en_file, >, 0);
	errno = 0;
	g_assert_cmpint(gt_transport_send_queued(t, d, GT_TRANSPORT_QUEUE_MAX / 2 + r.ecrits->len, NULL), ==, -1);
	g_assert_cmpint(errno, ==, ENOBUFS);
	g_assert_cmpuint(gt_transport_get_queued(t), ==, en_file);
	g_assert_cmpint(gt_transport_send_queued(t, d, GT_TRANSPORT_QUEUE_MAX - en_file, NULL), ==, (gssize)(GT_TRANSPORT_QUEUE_MAX - en_file));
	g_assert_cmpuint(gt_transport_get_queued(t), ==, GT_TRANSPORT_QUEUE_MAX);

	gt_transport_close(t);
	transport_params_clear(&tp);
	recepteur_liberer(&r);
	g_byte_array_unref(lu);
	g_free(d);
	fermer_pty(&p);
}

/* Fermeture : la file est jetée, plus aucun on_sent */
static void
test_file_fermeture(void)
{
	paire_pty_t p;
	TransportParams tp;
	GtTransport *t;
	Recepteur r;
	guint8 *d = motif(262144, 'a');
	GByteArray *lu = g_byte_array_new();
	guint appels;

	if(!ouvrir_pty(&p))
	{
		g_test_skip("openpty indisponible");
		return;
	}
	non_bloquant(p.maitre);
	recepteur_init(&r);
	params_serie(&tp, ttyname(p.esclave));
	t = gt_transport_open(&tp, &rappels_emission, &r, NULL);
	g_assert_nonnull(t);
	g_assert_cmpint(gt_transport_send_queued(t, d, 262144, NULL), ==, 262144);
	g_assert_cmpuint(gt_transport_get_queued(t), >, 0);
	appels = r.appels_ecrits;
	gt_transport_close(t);
	lire_par_morceaux(p.maitre, lu, 262144, 4096, 300);
	g_assert_cmpuint(r.appels_ecrits, ==, appels);
	g_assert_cmpuint(lu->len, <, 262144);
	transport_params_clear(&tp);
	recepteur_liberer(&r);
	g_byte_array_unref(lu);
	g_free(d);
	fermer_pty(&p);
}

/* Perte (maître fermé) : file jetée, plus d'on_sent, puis EIO */
static void
test_file_perte(void)
{
	paire_pty_t p;
	TransportParams tp;
	GtTransport *t;
	Recepteur r;
	guint8 *d = motif(262144, 'a');
	guint appels;

	if(!ouvrir_pty(&p))
	{
		g_test_skip("openpty indisponible");
		return;
	}
	recepteur_init(&r);
	params_serie(&tp, ttyname(p.esclave));
	t = gt_transport_open(&tp, &rappels_emission, &r, NULL);
	g_assert_nonnull(t);
	g_assert_cmpint(gt_transport_send_queued(t, d, 262144, NULL), ==, 262144);
	g_assert_cmpuint(gt_transport_get_queued(t), >, 0);
	appels = r.appels_ecrits;
	close(p.maitre);
	p.maitre = -1;
	g_assert_true(attendre_evenement(&r, TRANSPORT_EVENT_LOST, 2000));
	g_assert_cmpuint(gt_transport_get_queued(t), ==, 0);
	g_assert_cmpuint(r.appels_ecrits, ==, appels);
	errno = 0;
	g_assert_cmpint(gt_transport_send_queued(t, d, 1, NULL), ==, -1);
	g_assert_cmpint(errno, ==, EIO);
	gt_transport_close(t);
	transport_params_clear(&tp);
	recepteur_liberer(&r);
	g_free(d);
	fermer_pty(&p);
}

/* Serveur : le client parti, sa file est jetée ; le client suivant ne reçoit rien d'ancien */
static void
test_file_serveur_client_parti(void)
{
	TransportParams tp;
	GtTransport *t;
	Recepteur r;
	guint16 port = port_libre();
	guint8 *d = motif(262144, 'a');
	GByteArray *lu = g_byte_array_new();
	int client, petit = 4096;

	recepteur_init(&r);
	params_tcp(&tp, TRANSPORT_TCP_SERVER, port);
	t = gt_transport_open(&tp, &rappels_emission, &r, NULL);
	g_assert_nonnull(t);
	g_assert_cmpint(gt_transport_send_queued(t, d, 10, NULL), ==, 0);	/* aucun client : rien en file */
	g_assert_cmpuint(gt_transport_get_queued(t), ==, 0);

	client = connecter(port);
	g_assert_true(attendre_evenement(&r, TRANSPORT_EVENT_CLIENT_CONNECTED, 2000));
	g_assert_cmpint(setsockopt(gt_transport_get_fd(t), SOL_SOCKET, SO_SNDBUF, &petit, sizeof(petit)), ==, 0);
	for(gint i = 0; i < 4 && gt_transport_get_queued(t) == 0; i++)
		g_assert_cmpint(gt_transport_send_queued(t, d, 262144, NULL), ==, 262144);
	g_assert_cmpuint(gt_transport_get_queued(t), >, 0);

	close(client);
	g_assert_true(attendre_evenement(&r, TRANSPORT_EVENT_CLIENT_GONE, 2000));
	g_assert_cmpuint(gt_transport_get_queued(t), ==, 0);

	client = connecter(port);
	for(gint64 fin = g_get_monotonic_time() + 2000000; compter(&r, TRANSPORT_EVENT_CLIENT_CONNECTED) < 2 && g_get_monotonic_time() < fin; g_usleep(1000))
		g_main_context_iteration(NULL, FALSE);
	g_assert_cmpuint(compter(&r, TRANSPORT_EVENT_CLIENT_CONNECTED), ==, 2);
	non_bloquant(client);
	lire_par_morceaux(client, lu, 1, 4096, 300);
	g_assert_cmpuint(lu->len, ==, 0);
	g_assert_cmpint(gt_transport_send_queued(t, (const guint8 *)"ok", 2, NULL), ==, 2);
	lire_par_morceaux(client, lu, 2, 4096, 2000);
	g_assert_cmpmem(lu->data, lu->len, "ok", 2);

	close(client);
	gt_transport_close(t);
	transport_params_clear(&tp);
	recepteur_liberer(&r);
	g_byte_array_unref(lu);
	g_free(d);
}

static gboolean
rts_baisse(void)
{
	return (modem_etat & TIOCM_RTS) == 0;
}

/* RS485 : l'envoi ne bloque pas ; un seul TIOCMBIS et un seul TIOCMBIC pour toute la file (deux envois pendant le cycle compris) ;
 * délais « avant » et « après » respectés par minuterie ; try_send attend la fin du cycle. */
static void
test_file_rs485(void)
{
	const gsize bloc = 65536;
	paire_pty_t p;
	TransportParams tp;
	GtTransport *t;
	Recepteur r;
	guint8 *a = motif(bloc, 'a'), *b = motif(bloc, 'A');
	GByteArray *lu = g_byte_array_new();
	gint64 debut, duree, fin;
	gint64 avant_premier = 0;

	if(!ouvrir_pty(&p))
	{
		g_test_skip("openpty indisponible");
		return;
	}
	non_bloquant(p.maitre);
	recepteur_init(&r);
	params_serie(&tp, ttyname(p.esclave));
	tp.flow = GT_FLOW_RS485;
	tp.rs485_before_ms = 100;
	tp.rs485_after_ms = 150;
	t = gt_transport_open(&tp, &rappels_emission, &r, NULL);
	g_assert_nonnull(t);
	modem_fd = gt_transport_get_fd(t);
	modem_etat = TIOCM_DTR;
	changements_rts = appels_bis = appels_bic = 0;

	debut = g_get_monotonic_time();
	g_assert_cmpint(gt_transport_send_queued(t, a, bloc, GINT_TO_POINTER(1)), ==, (gssize)bloc);
	duree = g_get_monotonic_time() - debut;
	g_assert_cmpint(duree, <, 50000);
	g_assert_cmpint(appels_bis, ==, 1);
	g_assert_cmpint(modem_etat & TIOCM_RTS, ==, TIOCM_RTS);
	g_assert_cmpuint(r.ecrits->len, ==, 0);	/* délai « avant » : rien d'écrit encore */
	g_assert_cmpuint(gt_transport_get_queued(t), ==, bloc);
	errno = 0;
	g_assert_cmpint(gt_transport_try_send(t, (const guint8 *)"z", 1, NULL), ==, -1);
	g_assert_cmpint(errno, ==, EAGAIN);

	lire_par_morceaux(p.maitre, lu, 1000, 1000, 2000);
	avant_premier = r.dernier_ecrit;
	g_assert_cmpint(avant_premier - debut, >=, 95000);	/* délai « avant » (minuterie à la milliseconde près) */
	g_assert_cmpint(gt_transport_send_queued(t, b, bloc, GINT_TO_POINTER(2)), ==, (gssize)bloc);
	lire_par_morceaux(p.maitre, lu, 2 * bloc, 1000, 10000);
	g_assert_cmpuint(lu->len, ==, 2 * bloc);
	g_assert_cmpmem(lu->data, bloc, a, bloc);
	g_assert_cmpmem(lu->data + bloc, bloc, b, bloc);
	fin = g_get_monotonic_time();

	while(!rts_baisse() && g_get_monotonic_time() - fin < 2000000)
	{
		g_main_context_iteration(NULL, FALSE);
		g_usleep(1000);
	}
	g_assert_true(rts_baisse());
	g_assert_cmpint(instant_bic - r.dernier_ecrit, >=, 145000);	/* délai « après », compté depuis la dernière écriture */
	g_assert_cmpint(appels_bis, ==, 1);
	g_assert_cmpint(appels_bic, ==, 1);
	g_assert_cmpint(changements_rts, ==, 2);
	g_assert_cmpuint(r.ecrits->len, ==, 2 * bloc);

	/* Cycle terminé : try_send accepte de nouveau (un nouveau cycle) */
	g_assert_cmpint(gt_transport_try_send(t, (const guint8 *)"z", 1, NULL), ==, 1);
	g_assert_cmpint(appels_bis, ==, 2);

	modem_fd = -1;
	gt_transport_close(t);
	transport_params_clear(&tp);
	recepteur_liberer(&r);
	g_byte_array_unref(lu);
	g_free(a);
	g_free(b);
	fermer_pty(&p);
}

static void
test_null_tolere(void)
{
	gt_transport_close(NULL);
	g_assert_cmpint(gt_transport_send(NULL, (const guint8 *)"x", 1), ==, 0);
	g_assert_cmpint(gt_transport_get_modem_lines(NULL), ==, -1);
	g_assert_cmpint(gt_transport_get_fd(NULL), ==, -1);
	gt_transport_toggle_line(NULL, GT_MODEM_RTS);
	gt_transport_send_break(NULL);
}

static void
test_wait_char_ancien_codage(void)
{
	g_assert_cmpint(config_wait_char_from_file(0), ==, -1);
	g_assert_cmpint(config_wait_char_from_file(-1), ==, -1);
	g_assert_cmpint(config_wait_char_from_file(65), ==, 65);
	g_assert_cmpint(config_wait_char_from_file(255), ==, 255);
	g_assert_cmpint(config_wait_char_from_file(-23), ==, 233);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/transport/termios-sans-heritage", test_termios_sans_heritage);
	g_test_add_func("/transport/numero-de-peripherique", test_numero_de_peripherique);
	g_test_add_func("/transport/rs485-rts-stable", test_rs485_rts_stable);
	g_test_add_func("/transport/octets-binaires", test_octets_binaires);
	g_test_add_func("/transport/perte-maitre-pty", test_perte_maitre_pty);
	g_test_add_func("/transport/erreurs-ouverture", test_erreurs_ouverture);
	g_test_add_func("/transport/tcp-pair-ferme", test_tcp_pair_ferme);
	g_test_add_func("/transport/tcp-fin-de-flux", test_tcp_fin_de_flux);
	g_test_add_func("/transport/tcp-connexion-refusee", test_tcp_connexion_refusee);
	g_test_add_func("/transport/tcp-adresse-suivante", test_tcp_adresse_suivante);
	g_test_add_func("/transport/tcp-serveur-evenements", test_tcp_serveur_evenements);
	g_test_add_func("/transport/tcp-serveur-fermeture-libere-le-port", test_tcp_serveur_fermeture_libere_le_port);
	g_test_add_func("/transport/deux-instances", test_deux_instances);
	g_test_add_func("/transport/file-pty-ordre", test_file_pty_ordre);
	g_test_add_func("/transport/file-plafond", test_file_plafond);
	g_test_add_func("/transport/file-fermeture", test_file_fermeture);
	g_test_add_func("/transport/file-perte", test_file_perte);
	g_test_add_func("/transport/file-serveur-client-parti", test_file_serveur_client_parti);
	g_test_add_func("/transport/file-rs485", test_file_rs485);
	g_test_add_func("/transport/null-tolere", test_null_tolere);
	g_test_add_func("/transport/wait-char-ancien-codage", test_wait_char_ancien_codage);
	return g_test_run();
}
