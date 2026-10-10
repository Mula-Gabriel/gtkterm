/* Bouchon de la liaison (link.c) pour les tests d'intégration : enregistre les envois, simule la réception. */

#include <glib-object.h>

#include "link.h"
#include "stubs.h"

struct _GtkTermLink
{
	GObject parent_instance;
	GMutex mutex;
	GByteArray *sent;	/* octets envoyés */
	GThread *send_thread;
	GtSendOrigin last_origin;
	gboolean write_fails;	/* port fermé ou perdu simulé : rien n'est écrit */
	GtLinkState state;	/* état rendu par gt_link_get_state (GT_LINK_OPEN après stub_reset) */
};

G_DEFINE_TYPE(GtkTermLink, gt_link, G_TYPE_OBJECT)

static guint signal_received;
static GtkTermLink *lien_defaut = NULL;

static void
gt_link_finalize(GObject *object)
{
	GtkTermLink *link = (GtkTermLink *)object;

	g_byte_array_free(link->sent, TRUE);
	g_mutex_clear(&link->mutex);
	G_OBJECT_CLASS(gt_link_parent_class)->finalize(object);
}

static void
gt_link_class_init(GtkTermLinkClass *klass)
{
	G_OBJECT_CLASS(klass)->finalize = gt_link_finalize;
	signal_received = g_signal_new("received", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
	                               G_TYPE_NONE, 2, G_TYPE_BYTES, G_TYPE_INT64);
}

static void
gt_link_init(GtkTermLink *link)
{
	g_mutex_init(&link->mutex);
	link->sent = g_byte_array_new();
	link->last_origin = GT_SEND_KEYBOARD;
	link->state = GT_LINK_OPEN;
}

GtkTermLink *
gt_link_new(void)
{
	return g_object_new(GT_TYPE_LINK, NULL);
}

/* Créée au premier appel, jamais libérée */
GtkTermLink *
stub_link_default(void)
{
	if(lien_defaut == NULL)
		lien_defaut = gt_link_new();
	return lien_defaut;
}

gssize
gt_link_send(GtkTermLink *link, const guint8 *data, gsize len, GtSendOrigin origin)
{
	if(link->write_fails)
	{
		stub_record("gt_link_send", "echec");
		return 0;
	}
	g_mutex_lock(&link->mutex);
	g_byte_array_append(link->sent, data, (guint)len);
	link->send_thread = g_thread_self();
	link->last_origin = origin;
	g_mutex_unlock(&link->mutex);
	stub_record("gt_link_send", NULL);
	return (gssize)len;
}

void
stub_link_reset(void)
{
	GtkTermLink *link = stub_link_default();

	g_mutex_lock(&link->mutex);
	g_byte_array_set_size(link->sent, 0);
	link->send_thread = NULL;
	link->last_origin = GT_SEND_KEYBOARD;
	link->write_fails = FALSE;
	link->state = GT_LINK_OPEN;
	g_mutex_unlock(&link->mutex);
}

/* Simule un port fermé ou perdu : gt_link_send n'écrit rien et renvoie 0 (remis à FALSE par stub_reset) */
void
stub_link_set_write_fails(gboolean fails)
{
	stub_link_default()->write_fails = fails;
}

GtLinkState
gt_link_get_state(GtkTermLink *link)
{
	return link->state;
}

/* Simule l'état de la liaison (GtLinkState) vu par gtkterm.status(0) et par l'envoi sur le port 0 */
void
stub_link_set_state(gint state)
{
	stub_link_default()->state = (GtLinkState)state;
}

GByteArray *
stub_link_sent(GtkTermLink *link)
{
	return link->sent;
}

void
stub_link_inject_rx(GtkTermLink *link, const gchar *data, guint len)
{
	GBytes *octets = g_bytes_new(data, len);

	g_signal_emit(link, signal_received, 0, octets, g_get_real_time());
	g_bytes_unref(octets);
}

GByteArray *
stub_sent(void)
{
	return stub_link_sent(stub_link_default());
}

GThread *
stub_send_thread(void)
{
	return stub_link_default()->send_thread;
}

GtSendOrigin
stub_last_origin(void)
{
	return stub_link_default()->last_origin;
}

/* Émet « received » sur la liaison par défaut, comme la vraie liaison à la réception */
void
stub_inject_rx(const gchar *data, guint len)
{
	stub_link_inject_rx(stub_link_default(), data, len);
}
