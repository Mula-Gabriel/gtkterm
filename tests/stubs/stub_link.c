/* Bouchon de la liaison (link.c) pour les tests d'intégration : enregistre les envois, simule la réception. */

#include <glib-object.h>

#include "link.h"
#include "stubs.h"

struct _GtkTermLink
{
	GObject parent_instance;
};

G_DEFINE_TYPE(GtkTermLink, gt_link, G_TYPE_OBJECT)

static guint signal_received;
static GtkTermLink *lien_defaut = NULL;
static GMutex octets_mutex;
static GByteArray *envoyes = NULL;
static GThread *thread_envoi = NULL;
static GtSendOrigin derniere_origine = GT_SEND_KEYBOARD;
static gboolean ecriture_en_echec = FALSE;	/* port fermé ou perdu simulé : rien n'est écrit */

static void
gt_link_class_init(GtkTermLinkClass *klass)
{
	signal_received = g_signal_new("received", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
	                               G_TYPE_NONE, 2, G_TYPE_BYTES, G_TYPE_INT64);
}

static void
gt_link_init(GtkTermLink *link)
{
}

GtkTermLink *
gt_link_get_default(void)
{
	if(lien_defaut == NULL)
		lien_defaut = g_object_new(GT_TYPE_LINK, NULL);
	return lien_defaut;
}

gssize
gt_link_send(GtkTermLink *link, const guint8 *data, gsize len, GtSendOrigin origin)
{
	if(ecriture_en_echec)
	{
		stub_record("gt_link_send", "echec");
		return 0;
	}
	g_mutex_lock(&octets_mutex);
	if(envoyes == NULL)
		envoyes = g_byte_array_new();
	g_byte_array_append(envoyes, data, (guint)len);
	thread_envoi = g_thread_self();
	derniere_origine = origin;
	g_mutex_unlock(&octets_mutex);
	stub_record("gt_link_send", NULL);
	return (gssize)len;
}

void
stub_link_reset(void)
{
	g_mutex_lock(&octets_mutex);
	if(envoyes != NULL)
		g_byte_array_set_size(envoyes, 0);
	thread_envoi = NULL;
	derniere_origine = GT_SEND_KEYBOARD;
	ecriture_en_echec = FALSE;
	g_mutex_unlock(&octets_mutex);
}

/* Simule un port fermé ou perdu : gt_link_send n'écrit rien et renvoie 0 (remis à FALSE par stub_reset) */
void
stub_link_set_write_fails(gboolean fails)
{
	ecriture_en_echec = fails;
}

GByteArray *
stub_sent(void)
{
	if(envoyes == NULL)
		envoyes = g_byte_array_new();
	return envoyes;
}

GThread *
stub_send_thread(void)
{
	return thread_envoi;
}

GtSendOrigin
stub_last_origin(void)
{
	return derniere_origine;
}

/* Émet « received » sur la liaison par défaut, comme la vraie liaison à la réception */
void
stub_inject_rx(const gchar *data, guint len)
{
	GBytes *octets = g_bytes_new(data, len);

	g_signal_emit(gt_link_get_default(), signal_received, 0, octets, g_get_real_time());
	g_bytes_unref(octets);
}
