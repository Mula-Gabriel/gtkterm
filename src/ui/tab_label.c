/* Étiquette d'onglet : voir tab_label.h. */
#include <config.h>
#include <glib/gi18n.h>

#include "app.h"
#include "link.h"
#include "tab_label.h"

#define TAB_CLOSE_KEY "gt-tab-close"	/* sur la boîte : son bouton de fermeture (enfant, non possédé) */

/* notify::state de la liaison : peut tourner sous la surveillance du transport (link.h), ne fait que changer une classe CSS */
static void on_link_state(GtkTermLink *link, GParamSpec *pspec, gpointer box)
{
	GtLinkState st = gt_link_get_state(link);

	if(st == GT_LINK_LOST || st == GT_LINK_RECONNECTING)
		gtk_widget_add_css_class(GTK_WIDGET(box), "lost");
	else
		gtk_widget_remove_css_class(GTK_WIDGET(box), "lost");
}

/* Bouton de fermeture : même chemin que app.close-tab (confirmation si l'onglet est occupé) ; session gardée par référence faible */
static void on_close_clicked(GtkButton *button, gpointer user_data)
{
	GtSession *s = g_weak_ref_get(user_data);

	if(s != NULL)
	{
		gt_app_close_session(s);
		g_object_unref(s);
	}
}

static void weak_ref_free(gpointer data, GClosure *closure)
{
	g_weak_ref_clear(data);
	g_free(data);
}

GtkWidget *gt_tab_label_new(GtSession *s)
{
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	GtkWidget *name = gtk_label_new(NULL);
	GtkWidget *close = gtk_button_new_from_icon_name("window-close-symbolic");
	GtkTermLink *link = gt_session_get_link(s);
	GWeakRef *ref = g_new0(GWeakRef, 1);

	gtk_widget_set_name(box, "tab-label");
	gtk_widget_set_name(name, "tab-name");
	g_object_bind_property(s, "short-name", name, "label", G_BINDING_SYNC_CREATE);
	g_object_bind_property(s, "status-text", box, "tooltip-text", G_BINDING_SYNC_CREATE);
	gtk_box_append(GTK_BOX(box), name);

	gtk_widget_set_name(close, "tab-close");
	gtk_widget_add_css_class(close, "flat");
	gtk_widget_set_focus_on_click(close, FALSE);
	gtk_widget_set_tooltip_text(close, _("Close tab"));
	gtk_accessible_update_property(GTK_ACCESSIBLE(close), GTK_ACCESSIBLE_PROPERTY_LABEL, _("Close tab"), -1);
	g_weak_ref_init(ref, s);
	g_signal_connect_data(close, "clicked", G_CALLBACK(on_close_clicked), ref, weak_ref_free, 0);
	gtk_box_append(GTK_BOX(box), close);
	g_object_set_data(G_OBJECT(box), TAB_CLOSE_KEY, close);

	/* Déconnecté automatiquement à la destruction de l'étiquette */
	g_signal_connect_object(link, "notify::state", G_CALLBACK(on_link_state), box, 0);
	on_link_state(link, NULL, box);
	return box;
}

void gt_tab_label_set_closable(GtkWidget *label, gboolean closable)
{
	GtkWidget *close = label != NULL ? g_object_get_data(G_OBJECT(label), TAB_CLOSE_KEY) : NULL;

	if(close != NULL)
		gtk_widget_set_sensitive(close, closable);
}
