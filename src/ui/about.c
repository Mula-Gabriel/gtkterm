/* Boîte À propos GTK 4 (sous-projet 6a, §3.2-10) : la source de mise à jour a quitté cette boîte pour la fenêtre « Updates ». */

#include "config.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "about.h"
#include "dialogs.h"
#include "git_revision.h"

/* Chemin du logo dans les ressources de l'interface GTK 4 (src/ui/gtkterm.gresource.xml, préfixe /eu/mulagabriel/gtkterm/ui) */
#define GT_ABOUT_LOGO "/eu/mulagabriel/gtkterm/ui/gtkterm_64x64.png"

static GtkWidget *about_window;	/* pointeur faible */

void
gt_about_show(GtkWindow *parent)
{
	static const char *authors[] = {"Julien Schmitt", "Zach Davis", "Florian Euchner", "Stephan Enderlein", "Kevin Picot", NULL};
	GtkWidget *about;
	gchar *comments;

	if(about_window != NULL)
	{
		gtk_window_present(GTK_WINDOW(about_window));
		return;
	}
	about = gtk_about_dialog_new();
	gtk_widget_set_name(about, "about-dialog");
	gtk_window_set_transient_for(GTK_WINDOW(about), parent);
	gtk_window_set_modal(GTK_WINDOW(about), TRUE);
	gtk_window_set_destroy_with_parent(GTK_WINDOW(about), TRUE);
	gtk_window_set_hide_on_close(GTK_WINDOW(about), FALSE);
	gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(about), "GTKTerm fork MGU");
	gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(about), VERSION);
	comments = g_strdup_printf("Git: %s\n\n%s", GIT_REVISION, _("GTKTerm is a simple GTK+ terminal used to communicate with the serial port."));
	gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(about), comments);
	g_free(comments);
	gtk_about_dialog_set_copyright(GTK_ABOUT_DIALOG(about), "Copyright © Julien Schmitt");
	gtk_about_dialog_set_authors(GTK_ABOUT_DIALOG(about), authors);
	gtk_about_dialog_set_license_type(GTK_ABOUT_DIALOG(about), GTK_LICENSE_LGPL_3_0);
	if(g_resources_get_info(GT_ABOUT_LOGO, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL, NULL, NULL))
	{
		GdkTexture *logo = gdk_texture_new_from_resource(GT_ABOUT_LOGO);

		gtk_about_dialog_set_logo(GTK_ABOUT_DIALOG(about), GDK_PAINTABLE(logo));
		g_object_unref(logo);
	}
	/* Après le nom du programme, qui réécrit le titre */
	gtk_window_set_title(GTK_WINDOW(about), _("About GTKTerm"));

	gt_ui_close_on_escape(GTK_WINDOW(about));	/* Échap ferme la boîte, quelle que soit la version de GTK (dialogs.c) */

	about_window = about;
	g_object_add_weak_pointer(G_OBJECT(about), (gpointer *)&about_window);
	gtk_window_present(GTK_WINDOW(about));
}
