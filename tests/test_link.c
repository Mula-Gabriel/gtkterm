/* Tests d'intégration de la liaison (link.c) : machine d'état sur paire pty (lien symbolique recréé) et TCP local.
 * Les événements udev sont simulés par gt_link_handle_device_event ; aucune fenêtre, aucune boîte de dialogue. */

#include <glib.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "link.h"

/* ---- Observateur des signaux ----------------------------------------- */

typedef struct
{
	gint open_failed;
	gchar *dernier_message;
	GByteArray *recu;
	GByteArray *envoye;
	gint derniere_origine;
	gint effacements;
} Observateur;

static void
sur_open_failed(GtkTermLink *link, const gchar *message, gpointer data)
{
	Observateur *o = data;

	o->open_failed++;
	g_free(o->dernier_message);
	o->dernier_message = g_strdup(message);
}

static void
sur_received(GtkTermLink *link, GBytes *octets, gint64 heure, gpointer data)
{
	gsize n;
	const guint8 *p = g_bytes_get_data(octets, &n);

	g_assert_cmpint(heure, >, 0);
	g_byte_array_append(((Observateur *)data)->recu, p, (guint)n);
}

static void
sur_sent(GtkTermLink *link, GBytes *octets, gint origine, gpointer data)
{
	Observateur *o = data;
	gsize n;
	const guint8 *p = g_bytes_get_data(octets, &n);

	g_byte_array_append(o->envoye, p, (guint)n);
	o->derniere_origine = origine;
}

static void
sur_cleared(GtkTermLink *link, gpointer data)
{
	((Observateur *)data)->effacements++;
}

static GtkTermLink *
nouvelle_liaison(Observateur *o)
{
	GtkTermLink *link = gt_link_new();

	memset(o, 0, sizeof(*o));
	o->recu = g_byte_array_new();
	o->envoye = g_byte_array_new();
	g_signal_connect(link, "open-failed", G_CALLBACK(sur_open_failed), o);
	g_signal_connect(link, "received", G_CALLBACK(sur_received), o);
	g_signal_connect(link, "sent", G_CALLBACK(sur_sent), o);
	g_signal_connect(link, "history-cleared", G_CALLBACK(sur_cleared), o);
	g_object_set(link, "reconnect-interval-ms", 50, NULL);
	return link;
}

static void
liberer(GtkTermLink *link, Observateur *o)
{
	gt_link_close(link);
	g_object_unref(link);
	g_byte_array_unref(o->recu);
	g_byte_array_unref(o->envoye);
	g_free(o->dernier_message);
}

/* ---- Boucle ------------------------------------------------------------ */

static void
iterer(guint ms)
{
	gint64 fin = g_get_monotonic_time() + (gint64)ms * 1000;

	while(g_get_monotonic_time() < fin)
	{
		g_main_context_iteration(NULL, FALSE);
		g_usleep(1000);
	}
}

static gboolean
attendre_etat(GtkTermLink *link, GtLinkState etat, guint ms)
{
	gint64 fin = g_get_monotonic_time() + (gint64)ms * 1000;

	while(gt_link_get_state(link) != etat && g_get_monotonic_time() < fin)
	{
		g_main_context_iteration(NULL, FALSE);
		g_usleep(1000);
	}
	return gt_link_get_state(link) == etat;
}

static gboolean
attendre_octets(GByteArray *a, guint n, guint ms)
{
	gint64 fin = g_get_monotonic_time() + (gint64)ms * 1000;

	while(a->len < n && g_get_monotonic_time() < fin)
	{
		g_main_context_iteration(NULL, FALSE);
		g_usleep(1000);
	}
	return a->len >= n;
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

/* ---- Port pty derrière un lien symbolique stable ------------------------ */

typedef struct
{
	int maitre;
	int esclave;
	gchar *dir;
	gchar *lien;
} Port;

#define PORT_INIT { -1, -1, NULL, NULL }

static void
port_brancher(Port *p)
{
	g_assert_cmpint(openpty(&p->maitre, &p->esclave, NULL, NULL, NULL), ==, 0);
	if(p->dir == NULL)
		p->dir = g_dir_make_tmp("gtkterm-link-XXXXXX", NULL);
	if(p->lien == NULL)
		p->lien = g_build_filename(p->dir, "port", NULL);
	g_unlink(p->lien);
	g_assert_cmpint(symlink(ttyname(p->esclave), p->lien), ==, 0);
}

/* Le maître et l'esclave disparaissent, le lien aussi (comme le nœud d'un adaptateur USB débranché) : un lien laissé pendant vers
 * /dev/pts/N désignerait la prochaine pty créée sur la machine, que le noyau numérote au plus petit libre, et la liaison s'y reconnecterait. */
static void
port_debrancher(Port *p)
{
	close(p->esclave);
	close(p->maitre);
	p->esclave = p->maitre = -1;
	g_unlink(p->lien);
}

static void
port_detruire(Port *p)
{
	if(p->maitre >= 0)
		port_debrancher(p);
	g_unlink(p->lien);
	g_rmdir(p->dir);
	g_free(p->lien);
	g_free(p->dir);
}

static void
params_serie(TransportParams *tp, const gchar *chemin)
{
	memset(tp, 0, sizeof(*tp));
	tp->type = TRANSPORT_SERIAL;
	tp->port = g_strdup(chemin);
	tp->baudrate = 115200;
	tp->bits = 8;
	tp->stopbits = 1;
	tp->parity = GT_PARITY_NONE;
	tp->flow = GT_FLOW_NONE;
	tp->lock_port = FALSE;
}

static void
params_tcp(TransportParams *tp, TransportType type, guint16 port)
{
	memset(tp, 0, sizeof(*tp));
	tp->type = type;
	tp->host = g_strdup("127.0.0.1");
	tp->tcp_port = g_strdup_printf("%u", port);
}

static void
ouvrir_serie(GtkTermLink *link, Port *p)
{
	TransportParams tp;
	GError *err = NULL;

	params_serie(&tp, p->lien);
	g_assert_true(gt_link_open(link, &tp, &err));
	g_assert_no_error(err);
	transport_params_clear(&tp);
}

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

/* Écoute sur un port précis (serveur démarré après le client) */
static int
ecoute_sur(guint16 port)
{
	struct sockaddr_in adresse;
	int fd = socket(AF_INET, SOCK_STREAM, 0), un = 1;

	g_assert_cmpint(fd, >=, 0);
	g_assert_cmpint(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &un, sizeof(un)), ==, 0);
	memset(&adresse, 0, sizeof(adresse));
	adresse.sin_family = AF_INET;
	adresse.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	adresse.sin_port = htons(port);
	g_assert_cmpint(bind(fd, (struct sockaddr *)&adresse, sizeof(adresse)), ==, 0);
	g_assert_cmpint(listen(fd, 1), ==, 0);
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

typedef struct
{
	guint rx, tx;
	GtSendOrigin origine_tx;
	GString *rx_texte;
} Bilan;

static void
faire_bilan(const GtRecord *rec, gpointer data)
{
	Bilan *b = data;

	if(rec->kind == GT_RECORD_RX)
	{
		b->rx++;
		g_string_append_len(b->rx_texte, (const gchar *)rec->data, (gssize)rec->len);
	}
	else
	{
		b->tx++;
		b->origine_tx = rec->origin;
	}
}

/* ---- Ouverture ---------------------------------------------------------- */

static void
test_ouverture_reussie(void)
{
	Observateur o;
	Port p = PORT_INIT;
	GtkTermLink *link = nouvelle_liaison(&o);
	gchar *attendu;

	port_brancher(&p);
	ouvrir_serie(link, &p);
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_OPEN);
	attendu = g_strdup_printf("%s  115200-8-N-1", p.lien);
	g_assert_cmpstr(gt_link_get_status_text(link), ==, attendu);
	g_assert_cmpint(o.open_failed, ==, 0);
	g_assert_cmpint(gt_link_get_fd(link), >=, 0);
	g_free(attendu);
	liberer(link, &o);
	port_detruire(&p);
}

static void
test_ouverture_echouee(void)
{
	Observateur o;
	GtkTermLink *link = nouvelle_liaison(&o);
	TransportParams tp;
	GError *err = NULL;

	params_serie(&tp, "/nonexistent/gtkterm-test");
	g_assert_false(gt_link_open(link, &tp, &err));
	g_assert_error(err, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_OPEN);
	g_assert_cmpint(o.open_failed, ==, 1);
	g_assert_nonnull(strstr(o.dernier_message, "/nonexistent/gtkterm-test"));
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_CLOSED);
	g_assert_cmpstr(gt_link_get_status_text(link), ==, "No open port");
	g_clear_error(&err);
	transport_params_clear(&tp);
	liberer(link, &o);
}

/* Ouverture demandée qui échoue avec auto-reconnect : open-failed (la boîte), puis RECONNECTING (F3) */
static void
ouvrir_serie_echec(GtkTermLink *link, Port *p)
{
	TransportParams tp;
	GError *err = NULL;

	params_serie(&tp, p->lien);
	g_assert_false(gt_link_open(link, &tp, &err));
	g_assert_error(err, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_OPEN);
	g_clear_error(&err);
	transport_params_clear(&tp);
}

static void
test_ouverture_echouee_puis_branchement(void)
{
	Observateur o;
	Port p = PORT_INIT;
	GtkTermLink *link = nouvelle_liaison(&o);

	g_object_set(link, "auto-reconnect", TRUE, NULL);
	port_brancher(&p);
	port_debrancher(&p);		/* lien absent : l'adaptateur n'est pas encore branché */
	ouvrir_serie_echec(link, &p);
	g_assert_cmpint(o.open_failed, ==, 1);
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_RECONNECTING);
	g_assert_true(g_str_has_suffix(gt_link_get_status_text(link), " — reconnecting…"));
	iterer(200);	/* essais silencieux */
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_RECONNECTING);
	g_assert_cmpint(o.open_failed, ==, 1);
	port_brancher(&p);
	g_assert_true(attendre_etat(link, GT_LINK_OPEN, 2000));
	g_assert_cmpint(o.open_failed, ==, 1);
	g_assert_cmpint(write(p.maitre, "ok", 2), ==, 2);
	g_assert_true(attendre_octets(o.recu, 2, 2000));
	liberer(link, &o);
	port_detruire(&p);
}

static void
test_ouverture_echouee_puis_udev_add(void)
{
	Observateur o;
	Port p = PORT_INIT;
	GtkTermLink *link = nouvelle_liaison(&o);

	g_object_set(link, "auto-reconnect", TRUE, "reconnect-interval-ms", 60000, NULL);
	port_brancher(&p);
	port_debrancher(&p);
	ouvrir_serie_echec(link, &p);
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_RECONNECTING);
	port_brancher(&p);
	/* « add » du chemin configuré : essai immédiat, sans attendre la minuterie */
	gt_link_handle_device_event(link, "add", 0, p.lien);
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_OPEN);
	g_assert_cmpint(o.open_failed, ==, 1);
	liberer(link, &o);
	port_detruire(&p);
}

static void
fermer_sur_open_failed(GtkTermLink *link, const gchar *message, gpointer data)
{
	gt_link_close(link);
}

/* Le gestionnaire d'open-failed (boîte de dialogue, boucle imbriquée) ferme la liaison : pas de reconnexion ensuite */
static void
test_ouverture_echouee_fermee_par_gestionnaire(void)
{
	Observateur o;
	Port p = PORT_INIT;
	GtkTermLink *link = nouvelle_liaison(&o);

	g_object_set(link, "auto-reconnect", TRUE, NULL);
	g_signal_connect(link, "open-failed", G_CALLBACK(fermer_sur_open_failed), NULL);
	port_brancher(&p);
	port_debrancher(&p);
	ouvrir_serie_echec(link, &p);
	g_assert_cmpint(o.open_failed, ==, 1);
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_CLOSED);
	port_brancher(&p);
	iterer(200);
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_CLOSED);
	liberer(link, &o);
	port_detruire(&p);
}

static void
test_ouverture_echouee_sans_option(void)
{
	Observateur o;
	Port p = PORT_INIT;
	GtkTermLink *link = nouvelle_liaison(&o);

	port_brancher(&p);
	port_debrancher(&p);
	ouvrir_serie_echec(link, &p);
	g_assert_cmpint(o.open_failed, ==, 1);
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_CLOSED);
	port_brancher(&p);
	gt_link_handle_device_event(link, "add", 0, p.lien);
	iterer(200);
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_CLOSED);
	liberer(link, &o);
	port_detruire(&p);
}

/* ---- Réception, envoi, historique -------------------------------------- */

static void
test_reception_et_historique(void)
{
	Observateur o;
	Port p = PORT_INIT;
	GtkTermLink *link = nouvelle_liaison(&o);
	Bilan b = { 0, 0, GT_SEND_KEYBOARD, g_string_new(NULL) };

	port_brancher(&p);
	ouvrir_serie(link, &p);
	g_assert_cmpint(write(p.maitre, "abc", 3), ==, 3);
	g_assert_true(attendre_octets(o.recu, 3, 2000));
	g_assert_cmpmem(o.recu->data, o.recu->len, "abc", 3);
	gt_history_foreach(gt_link_get_history(link), faire_bilan, &b);
	g_assert_cmpuint(b.tx, ==, 0);
	g_assert_cmpstr(b.rx_texte->str, ==, "abc");
	g_string_free(b.rx_texte, TRUE);
	liberer(link, &o);
	port_detruire(&p);
}

static void
test_envoi_et_historique(void)
{
	Observateur o;
	Port p = PORT_INIT;
	GtkTermLink *link = nouvelle_liaison(&o);
	Bilan b = { 0, 0, GT_SEND_KEYBOARD, g_string_new(NULL) };
	guint8 buf[3];

	port_brancher(&p);
	ouvrir_serie(link, &p);
	g_assert_cmpint(gt_link_send(link, (const guint8 *)"xyz", 3, GT_SEND_MACRO), ==, 3);
	g_assert_cmpuint(lire(p.maitre, buf, 3, 2000), ==, 3);
	g_assert_cmpmem(buf, 3, "xyz", 3);
	g_assert_cmpmem(o.envoye->data, o.envoye->len, "xyz", 3);
	g_assert_cmpint(o.derniere_origine, ==, GT_SEND_MACRO);
	gt_history_foreach(gt_link_get_history(link), faire_bilan, &b);
	g_assert_cmpuint(b.tx, ==, 1);
	g_assert_cmpint(b.origine_tx, ==, GT_SEND_MACRO);
	g_string_free(b.rx_texte, TRUE);
	liberer(link, &o);
	port_detruire(&p);
}

static void
test_effacer_historique(void)
{
	Observateur o;
	Port p = PORT_INIT;
	GtkTermLink *link = nouvelle_liaison(&o);

	port_brancher(&p);
	ouvrir_serie(link, &p);
	g_assert_cmpint(write(p.maitre, "abc", 3), ==, 3);
	g_assert_true(attendre_octets(o.recu, 3, 2000));
	gt_link_clear_history(link);
	g_assert_cmpint(o.effacements, ==, 1);
	g_assert_cmpuint(gt_history_count(gt_link_get_history(link)), ==, 0);
	liberer(link, &o);
	port_detruire(&p);
}

/* ---- File d'émission ---------------------------------------------------- */

typedef struct
{
	GByteArray *octets;	/* octets TX de l'historique, dans l'ordre */
	GByteArray *origines;	/* origine de chaque octet TX */
} BilanTx;

static void
faire_bilan_tx(const GtRecord *rec, gpointer data)
{
	BilanTx *b = data;
	guint8 o = (guint8)rec->origin;

	if(rec->kind != GT_RECORD_TX)
		return;
	g_byte_array_append(b->octets, rec->data, (guint)rec->len);
	for(gsize i = 0; i < rec->len; i++)
		g_byte_array_append(b->origines, &o, 1);
}

static gsize
taille_tx(GtkTermLink *link)
{
	BilanTx b = { g_byte_array_new(), g_byte_array_new() };
	gsize n;

	gt_history_foreach(gt_link_get_history(link), faire_bilan_tx, &b);
	n = b.octets->len;
	g_byte_array_unref(b.octets);
	g_byte_array_unref(b.origines);
	return n;
}

static guint8 *
motif(gsize taille)
{
	guint8 *d = g_malloc(taille);

	for(gsize i = 0; i < taille; i++)
		d[i] = (guint8)('a' + (i % 26));
	return d;
}

/* Lit le maître (non bloquant) par morceaux en itérant la boucle, jusqu'à attendu octets ou ms écoulées ; *tx_max : plus grande taille de
 * l'historique TX relevée en cours de route (strictement croissante tant que la file se vide) */
static void
lire_lentement(int fd, GByteArray *dest, gsize attendu, guint ms, GtkTermLink *link, guint *paliers)
{
	gint64 fin = g_get_monotonic_time() + (gint64)ms * 1000;
	gsize dernier = taille_tx(link);
	guint8 buf[1000];

	while(dest->len < attendu && g_get_monotonic_time() < fin)
	{
		ssize_t n = read(fd, buf, sizeof(buf));
		gsize tx;

		if(n > 0)
			g_byte_array_append(dest, buf, (guint)n);
		g_main_context_iteration(NULL, FALSE);
		tx = taille_tx(link);
		g_assert_cmpuint(tx, >=, dernier);
		g_assert_cmpuint(tx, >=, dest->len);	/* jamais lu côté maître sans être entré dans l'historique */
		if(tx > dernier && paliers != NULL)
			(*paliers)++;
		dernier = tx;
		if(n <= 0)
			g_usleep(500);
	}
}

/* 64 Kio (collage) puis une frappe : tout accepté, l'historique TX grandit au fil de la lecture, origines conservées dans l'ordre ;
 * try_send attend que la file soit vide (-1 / EAGAIN, jamais 0) */
static void
test_file_emission(void)
{
	const gsize taille = 65536;
	Observateur o;
	Port p = PORT_INIT;
	GtkTermLink *link = nouvelle_liaison(&o);
	BilanTx b = { g_byte_array_new(), g_byte_array_new() };
	GByteArray *lu = g_byte_array_new();
	guint8 *d = motif(taille);
	guint paliers = 0;

	port_brancher(&p);
	g_assert_cmpint(fcntl(p.maitre, F_SETFL, fcntl(p.maitre, F_GETFL) | O_NONBLOCK), ==, 0);
	ouvrir_serie(link, &p);
	g_assert_cmpint(gt_link_send(link, d, taille, GT_SEND_KEYBOARD), ==, (gssize)taille);
	g_assert_cmpint(gt_link_send(link, (const guint8 *)"MACRO", 5, GT_SEND_MACRO), ==, 5);
	g_assert_cmpuint(gt_link_get_queued(link), >, 0);
	g_assert_cmpuint(taille_tx(link), <, taille);
	g_assert_cmpuint(taille_tx(link) + gt_link_get_queued(link), ==, taille + 5);
	errno = 0;
	g_assert_cmpint(gt_link_try_send(link, (const guint8 *)"F", 1, GT_SEND_FILE), ==, -1);
	g_assert_cmpint(errno, ==, EAGAIN);

	lire_lentement(p.maitre, lu, taille + 5, 10000, link, &paliers);
	g_assert_cmpuint(lu->len, ==, taille + 5);
	g_assert_cmpuint(paliers, >, 1);	/* l'historique a grandi en plusieurs fois, au fil de la lecture */
	g_assert_cmpmem(lu->data, taille, d, taille);
	g_assert_cmpmem(lu->data + taille, 5, "MACRO", 5);
	g_assert_cmpuint(gt_link_get_queued(link), ==, 0);
	gt_history_foreach(gt_link_get_history(link), faire_bilan_tx, &b);
	g_assert_cmpmem(b.octets->data, b.octets->len, lu->data, lu->len);
	for(gsize i = 0; i < b.origines->len; i++)
		g_assert_cmpuint(b.origines->data[i], ==, i < taille ? GT_SEND_KEYBOARD : GT_SEND_MACRO);
	g_assert_cmpmem(o.envoye->data, o.envoye->len, lu->data, lu->len);
	g_assert_cmpint(o.derniere_origine, ==, GT_SEND_MACRO);

	/* File vide : try_send écrit de nouveau directement, historique et « sent » compris */
	g_assert_cmpint(gt_link_try_send(link, (const guint8 *)"F", 1, GT_SEND_FILE), ==, 1);
	g_assert_cmpuint(taille_tx(link), ==, taille + 6);
	g_assert_cmpint(o.derniere_origine, ==, GT_SEND_FILE);

	g_byte_array_unref(b.octets);
	g_byte_array_unref(b.origines);
	g_byte_array_unref(lu);
	g_free(d);
	liberer(link, &o);
	port_detruire(&p);
}

/* File pleine : -1 / ENOBUFS, rien n'entre ; fermeture : la file est jetée, rien de plus dans l'historique */
static void
test_file_pleine_et_fermeture(void)
{
	Observateur o;
	Port p = PORT_INIT;
	GtkTermLink *link = nouvelle_liaison(&o);
	guint8 *d = motif(GT_TRANSPORT_QUEUE_MAX);
	GByteArray *lu = g_byte_array_new();
	gsize tx;

	port_brancher(&p);
	g_assert_cmpint(fcntl(p.maitre, F_SETFL, fcntl(p.maitre, F_GETFL) | O_NONBLOCK), ==, 0);
	ouvrir_serie(link, &p);
	g_assert_cmpint(gt_link_send(link, d, GT_TRANSPORT_QUEUE_MAX / 2, GT_SEND_KEYBOARD), ==, GT_TRANSPORT_QUEUE_MAX / 2);
	g_assert_cmpint(gt_link_send(link, d, GT_TRANSPORT_QUEUE_MAX / 2, GT_SEND_KEYBOARD), ==, GT_TRANSPORT_QUEUE_MAX / 2);
	tx = taille_tx(link);
	errno = 0;
	g_assert_cmpint(gt_link_send(link, d, GT_TRANSPORT_QUEUE_MAX / 2, GT_SEND_MACRO), ==, -1);
	g_assert_cmpint(errno, ==, ENOBUFS);
	g_assert_cmpuint(taille_tx(link) + gt_link_get_queued(link), ==, GT_TRANSPORT_QUEUE_MAX);

	gt_link_close(link);
	g_assert_cmpuint(gt_link_get_queued(link), ==, 0);
	lire_lentement(p.maitre, lu, GT_TRANSPORT_QUEUE_MAX, 300, link, NULL);
	g_assert_cmpuint(taille_tx(link), ==, tx);
	g_assert_cmpuint(lu->len, <=, tx);

	/* Rouverte : rien n'est rejoué */
	ouvrir_serie(link, &p);
	g_byte_array_set_size(lu, 0);
	lire_lentement(p.maitre, lu, 1, 300, link, NULL);
	g_assert_cmpuint(lu->len, ==, 0);
	g_assert_cmpuint(taille_tx(link), ==, tx);

	g_byte_array_unref(lu);
	g_free(d);
	liberer(link, &o);
	port_detruire(&p);
}

/* ---- Perte et reconnexion (M3) ----------------------------------------- */

static void
test_perte_sans_reconnexion(void)
{
	Observateur o;
	Port p = PORT_INIT;
	GtkTermLink *link = nouvelle_liaison(&o);

	port_brancher(&p);
	ouvrir_serie(link, &p);
	port_debrancher(&p);
	g_assert_true(attendre_etat(link, GT_LINK_LOST, 2000));
	g_assert_true(g_str_has_suffix(gt_link_get_status_text(link), " — lost"));
	g_assert_true(g_str_has_prefix(gt_link_get_status_text(link), p.lien));
	g_assert_cmpint(gt_link_send(link, (const guint8 *)"x", 1, GT_SEND_KEYBOARD), ==, 0);
	g_assert_cmpint(gt_link_get_fd(link), ==, -1);
	iterer(200);
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_LOST);
	g_assert_cmpint(o.open_failed, ==, 0);
	liberer(link, &o);
	port_detruire(&p);
}

static void
test_perte_puis_reconnexion(void)
{
	Observateur o;
	Port p = PORT_INIT;
	GtkTermLink *link = nouvelle_liaison(&o);

	g_object_set(link, "auto-reconnect", TRUE, NULL);
	port_brancher(&p);
	ouvrir_serie(link, &p);
	port_debrancher(&p);
	g_assert_true(attendre_etat(link, GT_LINK_RECONNECTING, 2000));
	g_assert_true(g_str_has_suffix(gt_link_get_status_text(link), " — reconnecting…"));
	iterer(200);	/* plusieurs essais silencieux sur un port absent */
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_RECONNECTING);
	port_brancher(&p);
	g_assert_true(attendre_etat(link, GT_LINK_OPEN, 2000));
	g_assert_cmpint(o.open_failed, ==, 0);
	g_assert_cmpint(write(p.maitre, "ok", 2), ==, 2);
	g_assert_true(attendre_octets(o.recu, 2, 2000));
	liberer(link, &o);
	port_detruire(&p);
}

static void
test_fermeture_annule_reconnexion(void)
{
	Observateur o;
	Port p = PORT_INIT;
	GtkTermLink *link = nouvelle_liaison(&o);

	g_object_set(link, "auto-reconnect", TRUE, NULL);
	port_brancher(&p);
	ouvrir_serie(link, &p);
	port_debrancher(&p);
	g_assert_true(attendre_etat(link, GT_LINK_RECONNECTING, 2000));
	gt_link_close(link);
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_CLOSED);
	g_assert_cmpstr(gt_link_get_status_text(link), ==, "No open port");
	port_brancher(&p);
	iterer(300);
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_CLOSED);
	liberer(link, &o);
	port_detruire(&p);
}

static void
test_bascule_auto_reconnect(void)
{
	Observateur o;
	Port p = PORT_INIT;
	GtkTermLink *link = nouvelle_liaison(&o);

	port_brancher(&p);
	ouvrir_serie(link, &p);
	port_debrancher(&p);
	g_assert_true(attendre_etat(link, GT_LINK_LOST, 2000));
	g_object_set(link, "auto-reconnect", TRUE, NULL);
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_RECONNECTING);
	g_object_set(link, "auto-reconnect", FALSE, NULL);
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_LOST);
	iterer(200);
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_LOST);
	liberer(link, &o);
	port_detruire(&p);
}

static void
test_destruction_pendant_reconnexion(void)
{
	Observateur o;
	Port p = PORT_INIT;
	GtkTermLink *link = nouvelle_liaison(&o);

	g_object_set(link, "auto-reconnect", TRUE, NULL);
	port_brancher(&p);
	ouvrir_serie(link, &p);
	port_debrancher(&p);
	g_assert_true(attendre_etat(link, GT_LINK_RECONNECTING, 2000));
	g_object_unref(link);		/* sans gt_link_close : la minuterie doit partir avec l'objet */
	iterer(200);
	g_byte_array_unref(o.recu);
	g_byte_array_unref(o.envoye);
	g_free(o.dernier_message);
	port_detruire(&p);
}

/* ---- Événements udev simulés (M2, M6) ---------------------------------- */

static void
test_udev_simule(void)
{
	Observateur o;
	Port p = PORT_INIT;
	GtkTermLink *link = nouvelle_liaison(&o);
	struct stat st;

	g_object_set(link, "auto-reconnect", TRUE, "reconnect-interval-ms", 60000, NULL);
	port_brancher(&p);
	ouvrir_serie(link, &p);
	g_assert_cmpint(stat(p.lien, &st), ==, 0);

	gt_link_handle_device_event(link, "remove", 0, "/dev/sans-rapport");
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_OPEN);

	/* Correspondance par numéro de périphérique, même sous un autre nom */
	gt_link_handle_device_event(link, "remove", st.st_rdev, "/dev/autre-nom");
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_RECONNECTING);

	gt_link_handle_device_event(link, "add", 0, "/dev/sans-rapport");
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_RECONNECTING);

	/* Retour du chemin configuré : essai immédiat, sans attendre la minuterie */
	gt_link_handle_device_event(link, "add", 0, p.lien);
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_OPEN);
	g_assert_cmpint(o.open_failed, ==, 0);
	liberer(link, &o);
	port_detruire(&p);
}

/* ---- TCP --------------------------------------------------------------- */

static void
test_tcp_client(void)
{
	Observateur o;
	GtkTermLink *link = nouvelle_liaison(&o);
	TransportParams tp;
	guint16 port;
	int ecoute = ecoute_locale(&port), pair;
	gchar *base;

	params_tcp(&tp, TRANSPORT_TCP_CLIENT, port);
	base = g_strdup_printf("tcp://127.0.0.1:%u", port);
	g_assert_true(gt_link_open(link, &tp, NULL));
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_CONNECTING);
	g_assert_true(g_str_has_suffix(gt_link_get_status_text(link), " — connecting…"));
	pair = accept(ecoute, NULL, NULL);
	g_assert_true(attendre_etat(link, GT_LINK_OPEN, 2000));
	g_assert_cmpstr(gt_link_get_status_text(link), ==, base);
	close(pair);
	g_assert_true(attendre_etat(link, GT_LINK_LOST, 2000));
	g_assert_cmpint(o.open_failed, ==, 0);
	g_free(base);
	transport_params_clear(&tp);
	liberer(link, &o);
	close(ecoute);
}

static void
test_tcp_client_refus(void)
{
	Observateur o;
	GtkTermLink *link = nouvelle_liaison(&o);
	TransportParams tp;

	params_tcp(&tp, TRANSPORT_TCP_CLIENT, port_libre());
	if(gt_link_open(link, &tp, NULL))
		g_assert_true(attendre_etat(link, GT_LINK_CLOSED, 2000));
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_CLOSED);
	g_assert_cmpint(o.open_failed, ==, 1);
	g_assert_nonnull(strstr(o.dernier_message, "Cannot connect"));
	transport_params_clear(&tp);
	liberer(link, &o);
}

/* Client TCP refusé avec auto-reconnect : open-failed puis RECONNECTING ; le serveur démarré ensuite est rejoint (F3) */
static void
test_tcp_client_refus_puis_serveur(void)
{
	Observateur o;
	GtkTermLink *link = nouvelle_liaison(&o);
	TransportParams tp;
	guint16 port = port_libre();
	int ecoute, pair;

	g_object_set(link, "auto-reconnect", TRUE, NULL);
	params_tcp(&tp, TRANSPORT_TCP_CLIENT, port);
	gt_link_open(link, &tp, NULL);
	g_assert_true(attendre_etat(link, GT_LINK_RECONNECTING, 2000));
	g_assert_cmpint(o.open_failed, ==, 1);
	iterer(200);	/* essais silencieux refusés */
	g_assert_cmpint(o.open_failed, ==, 1);
	ecoute = ecoute_sur(port);
	g_assert_true(attendre_etat(link, GT_LINK_OPEN, 2000));
	pair = accept(ecoute, NULL, NULL);
	g_assert_cmpint(pair, >=, 0);
	g_assert_cmpint(o.open_failed, ==, 1);
	close(pair);
	close(ecoute);
	transport_params_clear(&tp);
	liberer(link, &o);
}

static void
test_fermeture_pendant_connexion(void)
{
	Observateur o;
	GtkTermLink *link = nouvelle_liaison(&o);
	TransportParams tp;
	guint16 port;
	int ecoute = ecoute_locale(&port);

	params_tcp(&tp, TRANSPORT_TCP_CLIENT, port);
	g_assert_true(gt_link_open(link, &tp, NULL));
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_CONNECTING);
	gt_link_close(link);
	iterer(200);
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_CLOSED);
	g_assert_cmpint(o.open_failed, ==, 0);
	transport_params_clear(&tp);
	liberer(link, &o);
	close(ecoute);
}

static void
test_tcp_serveur(void)
{
	Observateur o;
	GtkTermLink *link = nouvelle_liaison(&o);
	TransportParams tp;
	guint16 port = port_libre();
	gchar *attendu;
	guint8 buf[2];
	int client;

	params_tcp(&tp, TRANSPORT_TCP_SERVER, port);
	g_assert_true(gt_link_open(link, &tp, NULL));
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_LISTENING);
	attendu = g_strdup_printf("tcp://127.0.0.1:%u (server, no client)", port);
	g_assert_cmpstr(gt_link_get_status_text(link), ==, attendu);
	g_free(attendu);

	client = connecter(port);
	g_assert_true(attendre_etat(link, GT_LINK_OPEN, 2000));
	attendu = g_strdup_printf("tcp://127.0.0.1:%u (server)", port);
	g_assert_cmpstr(gt_link_get_status_text(link), ==, attendu);
	g_free(attendu);
	g_assert_cmpint(gt_link_send(link, (const guint8 *)"hi", 2, GT_SEND_LUA), ==, 2);
	g_assert_cmpuint(lire(client, buf, 2, 2000), ==, 2);
	g_assert_cmpmem(buf, 2, "hi", 2);

	close(client);
	g_assert_true(attendre_etat(link, GT_LINK_LISTENING, 2000));
	transport_params_clear(&tp);
	liberer(link, &o);
}

/* ---- Série ouverte ------------------------------------------------------ */

static void
compter_notify(GObject *objet, GParamSpec *pspec, gpointer data)
{
	(*(gint *)data)++;
}

/* gt_link_is_serial_open : vrai seulement pour une liaison série ouverte. Passer d'un serveur TCP ouvert (client présent) à une liaison série
 * ouverte ne change pas « state » (OPEN → OPEN) : l'interface l'apprend par notify::status-text, qu'elle écoute avec notify::state. */
static void
test_serie_ouverte(void)
{
	Observateur o;
	Port p = PORT_INIT;
	GtkTermLink *link = nouvelle_liaison(&o);
	TransportParams tp;
	guint16 port = port_libre();
	gint notifications = 0;
	int client;

	g_assert_false(gt_link_is_serial_open(link));
	port_brancher(&p);
	ouvrir_serie(link, &p);
	g_assert_true(gt_link_is_serial_open(link));
	gt_link_close(link);
	g_assert_false(gt_link_is_serial_open(link));

	params_tcp(&tp, TRANSPORT_TCP_SERVER, port);
	g_assert_true(gt_link_open(link, &tp, NULL));
	g_assert_false(gt_link_is_serial_open(link));	/* LISTENING */
	client = connecter(port);
	g_assert_true(attendre_etat(link, GT_LINK_OPEN, 2000));
	g_assert_false(gt_link_is_serial_open(link));	/* OPEN, mais TCP */

	g_signal_connect(link, "notify::state", G_CALLBACK(compter_notify), &notifications);
	g_signal_connect(link, "notify::status-text", G_CALLBACK(compter_notify), &notifications);
	ouvrir_serie(link, &p);
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_OPEN);
	g_assert_true(gt_link_is_serial_open(link));
	g_assert_cmpint(notifications, >, 0);
	g_signal_handlers_disconnect_by_func(link, compter_notify, &notifications);

	port_debrancher(&p);
	g_assert_true(attendre_etat(link, GT_LINK_LOST, 2000));
	g_assert_false(gt_link_is_serial_open(link));

	close(client);
	transport_params_clear(&tp);
	liberer(link, &o);
	port_detruire(&p);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/link/ouverture-reussie", test_ouverture_reussie);
	g_test_add_func("/link/ouverture-echouee", test_ouverture_echouee);
	g_test_add_func("/link/ouverture-echouee-puis-branchement", test_ouverture_echouee_puis_branchement);
	g_test_add_func("/link/ouverture-echouee-puis-udev-add", test_ouverture_echouee_puis_udev_add);
	g_test_add_func("/link/ouverture-echouee-sans-option", test_ouverture_echouee_sans_option);
	g_test_add_func("/link/ouverture-echouee-fermee-par-gestionnaire", test_ouverture_echouee_fermee_par_gestionnaire);
	g_test_add_func("/link/reception-et-historique", test_reception_et_historique);
	g_test_add_func("/link/envoi-et-historique", test_envoi_et_historique);
	g_test_add_func("/link/effacer-historique", test_effacer_historique);
	g_test_add_func("/link/file-emission", test_file_emission);
	g_test_add_func("/link/file-pleine-et-fermeture", test_file_pleine_et_fermeture);
	g_test_add_func("/link/perte-sans-reconnexion", test_perte_sans_reconnexion);
	g_test_add_func("/link/perte-puis-reconnexion", test_perte_puis_reconnexion);
	g_test_add_func("/link/fermeture-annule-reconnexion", test_fermeture_annule_reconnexion);
	g_test_add_func("/link/bascule-auto-reconnect", test_bascule_auto_reconnect);
	g_test_add_func("/link/destruction-pendant-reconnexion", test_destruction_pendant_reconnexion);
	g_test_add_func("/link/udev-simule", test_udev_simule);
	g_test_add_func("/link/tcp-client", test_tcp_client);
	g_test_add_func("/link/tcp-client-refus", test_tcp_client_refus);
	g_test_add_func("/link/tcp-client-refus-puis-serveur", test_tcp_client_refus_puis_serveur);
	g_test_add_func("/link/fermeture-pendant-connexion", test_fermeture_pendant_connexion);
	g_test_add_func("/link/tcp-serveur", test_tcp_serveur);
	g_test_add_func("/link/serie-ouverte", test_serie_ouverte);
	return g_test_run();
}
