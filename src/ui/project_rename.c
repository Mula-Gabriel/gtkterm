/* Fenêtre « Rename project… » (spec nom de projet §1.2) : nom du projet, avec ou sans renommage du fichier. */
#include <config.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "app_config.h"
#include "dialogs.h"
#include "project.h"
#include "project_file.h"
#include "project_rename.h"

typedef struct
{
	GtkWindow *window;
	GtProject *project;	/* référence */
	GtkEditable *entry;
	GtkWidget *rename_file;
	GtkLabel *error;
	GtkWidget *ok;
	gulong open_handler;	/* notify::open du projet, retiré avec l'état */
} RenameDialog;

static GtkWindow *rename_window = NULL;	/* instance unique (pointeur faible) */

static void rename_dialog_free(gpointer data)
{
	RenameDialog *rd = data;

	g_clear_signal_handler(&rd->open_handler, rd->project);
	g_clear_object(&rd->project);
	g_free(rd);
}

/* Projet fermé : la fenêtre se ferme sans rien appliquer ; le gestionnaire se retire d'abord (une seule fermeture) */
static void on_project_open_changed(GObject *project, GParamSpec *pspec, RenameDialog *rd)
{
	g_clear_signal_handler(&rd->open_handler, rd->project);
	gtk_window_destroy(rd->window);
}

static void show_error(RenameDialog *rd, const gchar *text)
{
	gtk_label_set_text(rd->error, text != NULL ? text : "");
	gtk_widget_set_visible(GTK_WIDGET(rd->error), text != NULL && text[0] != '\0');
}

/* À chaque frappe : nom invalide → classe « error » sur l'entrée, raison affichée, OK insensible */
static void on_name_changed(GtkEditable *editable, RenameDialog *rd)
{
	gchar *reason = NULL, *name = gt_project_name_validate(gtk_editable_get_text(editable), &reason);

	if(name != NULL)
		gtk_widget_remove_css_class(GTK_WIDGET(editable), "error");
	else
		gtk_widget_add_css_class(GTK_WIDGET(editable), "error");
	gtk_widget_set_sensitive(rd->ok, name != NULL);
	show_error(rd, reason);
	g_free(name);
	g_free(reason);
}

static void on_rename_ok(GtkButton *button, RenameDialog *rd)
{
	gchar *reason = NULL, *name = gt_project_name_validate(gtk_editable_get_text(rd->entry), &reason);
	const gchar *path = gt_project_get_path(rd->project);
	GError *error = NULL;

	if(name == NULL || !gt_project_is_open(rd->project))
	{
		show_error(rd, reason);
		g_free(reason);
		g_free(name);
		return;
	}
	g_free(reason);
	if(path != NULL && gtk_widget_get_visible(rd->rename_file) && gtk_check_button_get_active(GTK_CHECK_BUTTON(rd->rename_file)))
	{
		gchar *dir = g_path_get_dirname(path), *base = gt_project_file_name_for(name), *target = g_build_filename(dir, base, NULL);
		gchar *old = g_strdup(path);	/* le chemin du projet change avec le déplacement */
		gboolean ok = gt_project_move_file(rd->project, target, &error);

		if(ok && g_strcmp0(target, old) != 0)
			app_config_set_last_project(target);	/* relire → modifier → enregistrer (app_config) */
		g_free(old);
		g_free(target);
		g_free(base);
		g_free(dir);
		if(!ok)
		{
			show_error(rd, error->message);
			g_error_free(error);
			g_free(name);
			return;	/* rien ne change, la fenêtre reste ouverte */
		}
	}
	gt_project_set_name(rd->project, name);
	g_free(name);
	gtk_window_destroy(rd->window);
}

void gt_project_rename_show(GtkWindow *parent, GtProject *project)
{
	RenameDialog *rd;
	GtkWidget *box, *label, *entry, *buttons, *cancel, *error;
	gchar *current;

	if(rename_window != NULL)
	{
		gtk_window_present(rename_window);
		return;
	}
	if(project == NULL || !gt_project_is_open(project))
		return;
	rd = g_new0(RenameDialog, 1);
	rd->project = g_object_ref(project);
	rd->window = GTK_WINDOW(gtk_window_new());
	gtk_widget_set_name(GTK_WIDGET(rd->window), "rename-project");
	gtk_window_set_title(rd->window, _("Rename project"));
	gtk_window_set_modal(rd->window, TRUE);
	gtk_window_set_transient_for(rd->window, parent);
	gtk_window_set_destroy_with_parent(rd->window, TRUE);
	gtk_window_set_default_size(rd->window, 360, -1);
	g_object_set_data_full(G_OBJECT(rd->window), "rename-dialog", rd, rename_dialog_free);
	/* Gestionnaire retiré avec l'état (rename_dialog_free), jamais laissé à la finalisation de la fenêtre */
	rd->open_handler = g_signal_connect(project, "notify::open", G_CALLBACK(on_project_open_changed), rd);
	rename_window = rd->window;
	g_object_add_weak_pointer(G_OBJECT(rd->window), (gpointer *)&rename_window);

	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_margin_top(box, 12);
	gtk_widget_set_margin_bottom(box, 12);
	gtk_widget_set_margin_start(box, 12);
	gtk_widget_set_margin_end(box, 12);

	label = gtk_label_new_with_mnemonic(_("Project _name:"));
	entry = gtk_entry_new();
	gtk_widget_set_name(entry, "project-name");
	gtk_accessible_update_property(GTK_ACCESSIBLE(entry), GTK_ACCESSIBLE_PROPERTY_LABEL, _("Project name"), -1);
	current = gt_project_get_name(project);
	gtk_editable_set_text(GTK_EDITABLE(entry), current);
	g_free(current);
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
	gtk_label_set_mnemonic_widget(GTK_LABEL(label), entry);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	rd->entry = GTK_EDITABLE(entry);
	gtk_box_append(GTK_BOX(box), label);
	gtk_box_append(GTK_BOX(box), entry);

	rd->rename_file = gtk_check_button_new_with_mnemonic(_("_Also rename the file"));
	gtk_widget_set_name(rd->rename_file, "project-rename-file");
	gtk_check_button_set_active(GTK_CHECK_BUTTON(rd->rename_file), TRUE);
	gtk_widget_set_visible(rd->rename_file, gt_project_get_path(project) != NULL);
	gtk_box_append(GTK_BOX(box), rd->rename_file);

	error = gtk_label_new(NULL);
	gtk_widget_set_name(error, "rename-error");
	gtk_label_set_wrap(GTK_LABEL(error), TRUE);
	gtk_label_set_xalign(GTK_LABEL(error), 0.0);
	gtk_widget_add_css_class(error, "error");
	gtk_widget_set_visible(error, FALSE);
	rd->error = GTK_LABEL(error);
	gtk_box_append(GTK_BOX(box), error);

	buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign(buttons, GTK_ALIGN_END);
	cancel = gtk_button_new_with_mnemonic(_("_Cancel"));
	gtk_widget_set_name(cancel, "rename-cancel");
	g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(gtk_window_destroy), rd->window);
	rd->ok = gtk_button_new_with_mnemonic(_("_OK"));
	gtk_widget_set_name(rd->ok, "rename-ok");
	gtk_widget_add_css_class(rd->ok, "suggested-action");
	g_signal_connect(rd->ok, "clicked", G_CALLBACK(on_rename_ok), rd);
	gtk_box_append(GTK_BOX(buttons), cancel);
	gtk_box_append(GTK_BOX(buttons), rd->ok);
	gtk_box_append(GTK_BOX(box), buttons);
	g_signal_connect(entry, "changed", G_CALLBACK(on_name_changed), rd);

	gtk_window_set_default_widget(rd->window, rd->ok);
	gt_ui_close_on_escape(rd->window);
	gtk_window_set_child(rd->window, box);
	gtk_window_present(rd->window);
	gtk_widget_grab_focus(entry);
	gtk_editable_select_region(rd->entry, 0, -1);
}
