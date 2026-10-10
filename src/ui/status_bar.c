/***********************************************************************/
/* status_bar.c                                                        */
/*                                                                     */
/*   Purpose                                                           */
/*      Barre d'état : texte de la liaison, message temporaire,        */
/*      voyants des lignes modem (sous-projet 6a)                      */
/*                                                                     */
/***********************************************************************/

#include <config.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <sys/ioctl.h>	/* TIOCM_* */

#include "status_bar.h"

#define LED_POLL_MS 100
#define STATUS_BAR_KEY "gt-status-bar"

typedef struct
{
	GtkWidget *box;		/* la barre (non possédée) */
	GtkTermLink *link;	/* référence */
	GtkWidget *text;	/* status-text */
	GtkWidget *flash;	/* status-flash */
	GtkWidget *leds[6];
	GBinding *text_binding;	/* pointeur faible */
	gboolean detached;
	guint flash_id;
	guint poll_id;
} StatusBar;

static const struct
{
	const gchar *name;
	gint bit;
} led_defs[6] =
{
	{ "DTR", TIOCM_DTR }, { "RTS", TIOCM_RTS }, { "CTS", TIOCM_CTS }, { "CD", TIOCM_CD }, { "DSR", TIOCM_DSR }, { "RI", TIOCM_RI },
};

static void leds_set(StatusBar *sb, gint lines);	/* voyant i sensible ssi lines >= 0 et (lines & led_defs[i].bit) */
static gboolean leds_poll(gpointer data);	/* leds_set(sb, gt_link_get_modem_lines(sb->link)) ; G_SOURCE_CONTINUE */
static void leds_update(StatusBar *sb);	/* série ouverte : voyants visibles, minuterie armée ; sinon voyants cachés et éteints, minuterie retirée */
static void on_link_notify(GObject *object, GParamSpec *pspec, gpointer user_data);	/* leds_update ; rien d'autre (réentrance, link.h) */
static gboolean flash_end(gpointer data);	/* flash_id = 0, status-flash caché, status-text visible ; G_SOURCE_REMOVE */
static void on_destroy(GtkWidget *box, gpointer user_data);	/* retire les deux minuteries, déconnecte la liaison, libère */

GtkWidget *gt_status_bar_new(GtkTermLink *link)
{
	StatusBar *sb = g_new0(StatusBar, 1);

	sb->link = g_object_ref(link);
	sb->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_margin_start(sb->box, 6);
	gtk_widget_set_margin_end(sb->box, 6);
	gtk_widget_set_margin_top(sb->box, 2);
	gtk_widget_set_margin_bottom(sb->box, 2);

	sb->text = gtk_label_new(NULL);
	gtk_widget_set_name(sb->text, "status-text");
	gtk_widget_set_hexpand(sb->text, TRUE);
	gtk_label_set_xalign(GTK_LABEL(sb->text), 0.0);
	gtk_label_set_ellipsize(GTK_LABEL(sb->text), PANGO_ELLIPSIZE_END);
	gtk_box_append(GTK_BOX(sb->box), sb->text);

	sb->flash = gtk_label_new(NULL);
	gtk_widget_set_name(sb->flash, "status-flash");
	gtk_widget_set_hexpand(sb->flash, TRUE);
	gtk_label_set_xalign(GTK_LABEL(sb->flash), 0.0);
	gtk_widget_set_visible(sb->flash, FALSE);
	gtk_box_append(GTK_BOX(sb->box), sb->flash);

	for(guint i = 0; i < G_N_ELEMENTS(led_defs); i++)
	{
		gchar *name = g_strdup_printf("led-%s", led_defs[i].name);

		sb->leds[i] = gtk_label_new(led_defs[i].name);
		gtk_widget_set_name(sb->leds[i], name);
		gtk_widget_set_visible(sb->leds[i], FALSE);
		gtk_box_append(GTK_BOX(sb->box), sb->leds[i]);
		g_free(name);
	}

	sb->text_binding = g_object_bind_property(link, "status-text", sb->text, "label", G_BINDING_SYNC_CREATE);
	g_object_add_weak_pointer(G_OBJECT(sb->text_binding), (gpointer *)&sb->text_binding);
	g_signal_connect(link, "notify::state", G_CALLBACK(on_link_notify), sb);
	g_signal_connect(link, "notify::status-text", G_CALLBACK(on_link_notify), sb);
	g_object_set_data(G_OBJECT(sb->box), STATUS_BAR_KEY, sb);
	g_signal_connect(sb->box, "destroy", G_CALLBACK(on_destroy), sb);
	leds_update(sb);
	return sb->box;
}

static void leds_set(StatusBar *sb, gint lines)
{
	for(guint i = 0; i < G_N_ELEMENTS(led_defs); i++)
		gtk_widget_set_sensitive(sb->leds[i], lines >= 0 && (lines & led_defs[i].bit) != 0);
}

static gboolean leds_poll(gpointer data)
{
	StatusBar *sb = data;

	leds_set(sb, gt_link_get_modem_lines(sb->link));
	return G_SOURCE_CONTINUE;
}

static void leds_update(StatusBar *sb)
{
	gboolean serial = !sb->detached && gt_link_is_serial_open(sb->link);

	for(guint i = 0; i < G_N_ELEMENTS(led_defs); i++)
		gtk_widget_set_visible(sb->leds[i], serial);
	if(serial)
	{
		/* Seule la minuterie est armée : le premier relevé (ioctl, qui peut marquer la liaison perdue) se fait hors du gestionnaire de notification */
		if(sb->poll_id == 0)
			sb->poll_id = g_timeout_add(LED_POLL_MS, leds_poll, sb);
	}
	else
	{
		g_clear_handle_id(&sb->poll_id, g_source_remove);
		leds_set(sb, -1);
	}
}

static void on_link_notify(GObject *object, GParamSpec *pspec, gpointer user_data)
{
	leds_update(user_data);
}

static gboolean flash_end(gpointer data)
{
	StatusBar *sb = data;

	sb->flash_id = 0;
	gtk_widget_set_visible(sb->flash, FALSE);
	gtk_widget_set_visible(sb->text, TRUE);
	return G_SOURCE_REMOVE;
}

static void on_destroy(GtkWidget *box, gpointer user_data)
{
	StatusBar *sb = user_data;

	g_clear_handle_id(&sb->flash_id, g_source_remove);
	g_clear_handle_id(&sb->poll_id, g_source_remove);
	g_signal_handlers_disconnect_by_data(sb->link, sb);
	g_object_unref(sb->link);
	if(sb->text_binding != NULL)
		g_object_remove_weak_pointer(G_OBJECT(sb->text_binding), (gpointer *)&sb->text_binding);
	g_object_set_data(G_OBJECT(box), STATUS_BAR_KEY, NULL);
	g_free(sb);
}

void gt_status_bar_flash(GtkWidget *bar, const gchar *text, guint ms)
{
	StatusBar *sb = bar != NULL ? g_object_get_data(G_OBJECT(bar), STATUS_BAR_KEY) : NULL;

	if(sb == NULL || sb->detached)
		return;
	g_clear_handle_id(&sb->flash_id, g_source_remove);
	gtk_label_set_text(GTK_LABEL(sb->flash), text);
	gtk_widget_set_visible(sb->text, FALSE);
	gtk_widget_set_visible(sb->flash, TRUE);
	sb->flash_id = g_timeout_add(MAX(ms, 1), flash_end, sb);
}

void gt_status_bar_detach(GtkWidget *bar)
{
	StatusBar *sb = bar != NULL ? g_object_get_data(G_OBJECT(bar), STATUS_BAR_KEY) : NULL;

	if(sb == NULL || sb->detached)
		return;
	sb->detached = TRUE;
	if(sb->text_binding != NULL)
		g_binding_unbind(sb->text_binding);
	g_signal_handlers_disconnect_by_data(sb->link, sb);
	g_clear_handle_id(&sb->flash_id, g_source_remove);
	g_clear_handle_id(&sb->poll_id, g_source_remove);
}
