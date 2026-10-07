/* Tests d'intégration du transport (transport.c, cœur sans interface) sur paire pty et TCP local.
 * ioctl est intercepté (-Wl,--wrap=ioctl) pour simuler les lignes modem RTS/DTR qu'une pty n'a pas. */

#include <glib.h>
#include <glib/gstdio.h>
#include <errno.h>
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
			modem_etat |= *(int *)arg;
		else
			modem_etat &= ~*(int *)arg;
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
} Recepteur;

static void
recepteur_init(Recepteur *r)
{
	r->octets = g_byte_array_new();
	r->evenements = g_array_new(FALSE, FALSE, sizeof(TransportEvent));
	r->detail = NULL;
}

static void
recepteur_liberer(Recepteur *r)
{
	g_byte_array_unref(r->octets);
	g_array_unref(r->evenements);
	g_free(r->detail);
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

static const TransportCallbacks rappels = { sur_donnees, sur_evenement };

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
	g_assert_cmpuint(cfgetospeed(&tio), ==, B115200);
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

	g_assert_cmpint(gt_transport_send(t, (const guint8 *)"x", 1), ==, 1);
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
	g_test_add_func("/transport/null-tolere", test_null_tolere);
	g_test_add_func("/transport/wait-char-ancien-codage", test_wait_char_ancien_codage);
	return g_test_run();
}
