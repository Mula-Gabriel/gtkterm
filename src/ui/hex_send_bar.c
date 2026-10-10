/***********************************************************************/
/* hex_send_bar.c                                                      */
/*                                                                     */
/*   Purpose                                                           */
/*      Envoi de données hexadécimales (sous-projet 6a)                */
/*                                                                     */
/***********************************************************************/

#include <config.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "app_ui.h"
#include "hex_input.h"
#include "hex_send_bar.h"

typedef struct
{
	GtkWidget *box;		/* « hex-bar » (non possédée) */
	GtkWidget *entry;	/* « hex-entry » */
	GtkTermLink *link;	/* référence */
	GtHexHistory *history;
} HexSendBar;

static void on_activate(GtkEntry *entry, HexSendBar *hb);
	/* gt_hex_input_parse ; erreur : flash 3000 ms « Invalid hexadecimal byte "%s", nothing sent » (jeton), saisie gardée, retour ;
	 * succès : n = len > 0 ? gt_link_send(link, data, len, GT_SEND_HEX) : 0 ; flash 2000 ms « %d byte(s) sent! » (MAX(n, 0)) ;
	 * gt_hex_history_add(hb->history, texte) AVANT gtk_editable_set_text(entry, "") (le texte appartient à l'entrée) */
static gboolean on_key_pressed(GtkEventControllerKey *c, guint keyval, guint keycode, GdkModifierType state, HexSendBar *hb);
	/* GDK_KEY_Up / KP_Up : gt_hex_history_previous, texte posé si non NULL, curseur en fin (gtk_editable_set_position(-1)), TRUE ;
	 * GDK_KEY_Down / KP_Down : gt_hex_history_next, texte posé, curseur en fin, TRUE ; autre touche : FALSE */
static void on_destroy(GtkWidget *box, HexSendBar *hb);	/* gt_hex_history_free, g_object_unref(link), libère */

GtkWidget *gt_hex_send_bar_new(GtkTermLink *link)
{
	HexSendBar *hb = g_new0(HexSendBar, 1);
	GtkWidget *label;
	GtkEventController *keys;

	hb->link = g_object_ref(link);
	hb->history = gt_hex_history_new(GT_HEX_SEND_HISTORY_MAX);
	hb->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_name(hb->box, "hex-bar");
	gtk_widget_set_margin_start(hb->box, 6);
	gtk_widget_set_margin_end(hb->box, 6);
	gtk_widget_set_margin_top(hb->box, 2);
	gtk_widget_set_margin_bottom(hb->box, 2);

	label = gtk_label_new(_("Hexadecimal data to send (separator: ';' or space): "));
	gtk_box_append(GTK_BOX(hb->box), label);
	hb->entry = gtk_entry_new();
	gtk_widget_set_name(hb->entry, "hex-entry");
	gtk_widget_set_hexpand(hb->entry, TRUE);
	gtk_accessible_update_relation(GTK_ACCESSIBLE(hb->entry), GTK_ACCESSIBLE_RELATION_LABELLED_BY, label, NULL, -1);
	gtk_box_append(GTK_BOX(hb->box), hb->entry);

	g_signal_connect(hb->entry, "activate", G_CALLBACK(on_activate), hb);
	keys = gtk_event_controller_key_new();
	gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
	g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key_pressed), hb);
	gtk_widget_add_controller(hb->entry, keys);
	g_signal_connect(hb->box, "destroy", G_CALLBACK(on_destroy), hb);
	gtk_widget_set_visible(hb->box, FALSE);
	return hb->box;
}

void gt_hex_send_bar_install_action(GtkApplication *app, GtkWidget *bar)
{
	GPropertyAction *action = g_property_action_new("show-hex-bar", bar, "visible");

	g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(action));
	g_object_unref(action);
}

static void on_activate(GtkEntry *entry, HexSendBar *hb)
{
	const gchar *text = gtk_editable_get_text(GTK_EDITABLE(entry));
	GError *err = NULL;
	GBytes *data = gt_hex_input_parse(text, &err);
	gsize len;
	gint n = 0;

	if(data == NULL)
	{
		gt_app_status_flash(3000, _("Invalid hexadecimal byte \"%s\", nothing sent"), err->message);
		g_error_free(err);
		return;
	}
	len = g_bytes_get_size(data);
	if(len > 0)
		n = gt_link_send(hb->link, g_bytes_get_data(data, NULL), len, GT_SEND_HEX);
	g_bytes_unref(data);
	gt_app_status_flash(2000, _("%d byte(s) sent!"), MAX(n, 0));
	gt_hex_history_add(hb->history, text);	/* avant l'effacement : le texte appartient à l'entrée */
	gtk_editable_set_text(GTK_EDITABLE(entry), "");
}

static gboolean on_key_pressed(GtkEventControllerKey *c, guint keyval, guint keycode, GdkModifierType state, HexSendBar *hb)
{
	const gchar *text;

	if(keyval == GDK_KEY_Up || keyval == GDK_KEY_KP_Up)
		text = gt_hex_history_previous(hb->history);
	else if(keyval == GDK_KEY_Down || keyval == GDK_KEY_KP_Down)
		text = gt_hex_history_next(hb->history);
	else
		return FALSE;
	if(text != NULL)
	{
		gtk_editable_set_text(GTK_EDITABLE(hb->entry), text);
		gtk_editable_set_position(GTK_EDITABLE(hb->entry), -1);
	}
	return TRUE;
}

static void on_destroy(GtkWidget *box, HexSendBar *hb)
{
	gt_hex_history_free(hb->history);
	g_object_unref(hb->link);
	g_free(hb);
}
