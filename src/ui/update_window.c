/* Fenêtre « Updates », vérification au démarrage et proposition de mise à jour (sous-projet 6a, §6.7, M33/M34).
 * La logique (git ls-remote, comparaison, commande du script) est dans update_check.c ; ici, seulement l'interface. */

#include "config.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <unistd.h>

#include "update_window.h"
#include "app.h"
#include "app_config.h"
#include "dialogs.h"
#include "git_revision.h"
#include "update_check.h"

struct _GtUpdateWindow
{
	GtkWindow parent_instance;
	GtkWidget *url;	/* update-url */
	GtkWidget *startup;	/* update-startup */
	GtkWidget *check;	/* update-check */
	GtkWidget *result;	/* update-result */
	GCancellable *cancellable;	/* vérification en cours ; annulée à la fermeture ou par une nouvelle vérification */
};

G_DEFINE_FINAL_TYPE(GtUpdateWindow, gt_update_window, GTK_TYPE_WINDOW)

static GtUpdateWindow *instance;	/* pointeur faible : la fenêtre ouverte */
static GtkWindow *startup_window;	/* pointeur faible : fenêtre principale pour la proposition au démarrage */
static GCancellable *startup_cancellable;

/* ---- lancement du script ------------------------------------------- */

static void
run_update_script(GtkWindow *parent)
{
	gchar *terminal = gt_update_find_terminal();
	gchar **argv = NULL;
	GError *error = NULL;

	/* Arguments en liste (M34) : g_spawn_async, aucun shell n'interprète l'URL ni le préfixe */
	if(terminal != NULL)
		argv = gt_update_terminal_argv(terminal, PACKAGE_DATA_DIR "/gtkterm-update.sh", settings.update_url, settings.update_prefix, getpid());
	g_free(terminal);
	if(argv == NULL)
	{
		gt_ui_message(parent, GT_APP_MSG_ERROR, _("No terminal emulator found. Please install xterm or similar."));
		return;
	}
	if(!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error))
	{
		gchar *text = g_strdup_printf(_("Failed to launch update terminal:\n%s"), error->message);

		gt_ui_message(parent, GT_APP_MSG_ERROR, text);
		g_free(text);
		g_clear_error(&error);
	}
	else
		gt_ui_message(parent, GT_APP_MSG_INFO, _("The update is running in a terminal window.\nFollow the instructions there and restart GTKTerm when done."));
	g_strfreev(argv);
}

/* ---- proposition --------------------------------------------------- */

static gboolean shutting_down;	/* posé par gt_update_shutdown() : plus aucune proposition, tout choix tardif est ignoré */

typedef struct
{
	GtkWindow *parent;	/* pointeur faible */
	gchar *remote;
} OfferData;

static void
on_offer_chosen(gint choice, gpointer user_data)
{
	OfferData *d = user_data;

	/* -1 : fermée sans choix (Échap, parent détruit). Après l'arrêt (rappel de la vidange finale) : rien */
	if(!shutting_down)
	{
		if(choice == 0)
			run_update_script(d->parent != NULL ? d->parent : gt_app_get_main_window());
		else if(choice == 2)
			/* « Skip this version » survit au redémarrage (M34) */
			config_save_skipped_revision(d->remote);
	}
	if(d->parent != NULL)
		g_object_remove_weak_pointer(G_OBJECT(d->parent), (gpointer *)&d->parent);
	g_free(d->remote);
	g_free(d);
}

void
gt_update_offer(GtkWindow *parent, const gchar *remote_rev)
{
	const char *labels[] = {_("Update now"), _("Later"), _("Skip this version"), NULL};
	gchar *detail;
	OfferData *d;

	if(shutting_down)
		return;
	detail = g_strdup_printf(_("Latest revision: %.12s\nInstalled revision: %.12s"), remote_rev, GIT_REVISION);
	d = g_new0(OfferData, 1);
	d->parent = parent;
	if(parent != NULL)
		g_object_add_weak_pointer(G_OBJECT(parent), (gpointer *)&d->parent);
	d->remote = g_strdup(remote_rev);
	gt_ui_choose(parent, _("A new version of GTKTerm is available."), detail, labels, on_offer_chosen, d);
	g_free(detail);
}

/* ---- fenêtre « Updates » ------------------------------------------- */

typedef struct
{
	GtUpdateWindow *self;	/* référence forte : la structure reste valide, les widgets ne sont touchés que si la vérification n'est pas annulée */
	GCancellable *cancellable;	/* celui de cette vérification */
} CheckData;

static void
set_result(GtUpdateWindow *self, const gchar *text)
{
	gtk_label_set_text(GTK_LABEL(self->result), text);
}

static void
cancel_check(GtUpdateWindow *self)
{
	if(self->cancellable != NULL)
	{
		g_cancellable_cancel(self->cancellable);
		g_clear_object(&self->cancellable);
	}
}

static void
on_check_done(GObject *source, GAsyncResult *res, gpointer user_data)
{
	CheckData *d = user_data;
	GtUpdateWindow *self = d->self;
	GError *error = NULL;
	gchar *remote = gt_update_fetch_remote_finish(res, &error);
	gchar *text = NULL;

	/* Fenêtre fermée (close-request/dispose ont annulé) ou vérification remplacée : rien à afficher */
	if(g_cancellable_is_cancelled(d->cancellable))
		goto out;
	gtk_widget_set_sensitive(self->check, TRUE);
	if(remote == NULL)
	{
		text = g_strdup_printf(_("Could not check for updates: %s"), error->message);
		set_result(self, text);
		goto out;
	}

	gchar *cache = gt_update_cache_src_dir();
	/* Vérification manuelle : la révision ignorée ne s'applique pas (proposition toujours faite) ; M33 : locale plus récente = à jour */
	GtUpdateVerdict verdict = gt_update_compare(GIT_REVISION, remote, NULL, gt_update_local_is_newer(cache, GIT_REVISION, remote));

	g_free(cache);
	switch(verdict)
	{
		case GT_UPDATE_UP_TO_DATE:
			set_result(self, _("GTKTerm is up to date."));
			break;
		case GT_UPDATE_LOCAL_UNKNOWN:
			text = g_strdup_printf(_("The installed revision is unknown; the latest revision is %.12s."), remote);
			set_result(self, text);
			gt_update_offer(GTK_WINDOW(self), remote);
			break;
		case GT_UPDATE_AVAILABLE:
		case GT_UPDATE_SKIPPED:
			text = g_strdup_printf(_("A new version is available (%.12s)."), remote);
			set_result(self, text);
			gt_update_offer(GTK_WINDOW(self), remote);
			break;
	}
out:
	g_free(text);
	g_free(remote);
	g_clear_error(&error);
	g_object_unref(d->cancellable);
	g_object_unref(d->self);
	g_free(d);
}

/* « Check now » et Entrée dans le champ : vérifie l'URL AFFICHÉE, après l'avoir enregistrée */
static void
start_check(GtUpdateWindow *self)
{
	gchar *url = g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(self->url))));
	CheckData *d;

	if(*url == '\0')
	{
		set_result(self, _("Please enter the URL of the source repository."));
		g_free(url);
		return;
	}
	config_save_update_url(url);	/* n'écrit que si elle a changé ; le script lancé ensuite reçoit settings.update_url = cette URL */
	cancel_check(self);
	self->cancellable = g_cancellable_new();
	gtk_widget_set_sensitive(self->check, FALSE);
	set_result(self, _("Checking…"));
	d = g_new0(CheckData, 1);
	d->self = g_object_ref(self);
	d->cancellable = g_object_ref(self->cancellable);
	gt_update_fetch_remote_async(url, self->cancellable, on_check_done, d);
	g_free(url);
}

static void
on_startup_toggled(GtkCheckButton *button, gpointer user_data)
{
	config_save_update_check_startup(gtk_check_button_get_active(button));
}

static gboolean
gt_update_window_close_request(GtkWindow *window)
{
	cancel_check(GT_UPDATE_WINDOW(window));
	return FALSE;	/* fermeture acceptée (hide-on-close FALSE : la fenêtre est détruite) */
}

static void
gt_update_window_dispose(GObject *object)
{
	cancel_check(GT_UPDATE_WINDOW(object));
	/* Le pointeur faible ne tombe qu'à la finalisation, retardée par la référence de CheckData jusqu'au rappel annulé : une fenêtre détruite
	 * ne doit plus être présentée par gt_update_window_show */
	if(instance == GT_UPDATE_WINDOW(object))
	{
		g_object_remove_weak_pointer(object, (gpointer *)&instance);
		instance = NULL;
	}
	G_OBJECT_CLASS(gt_update_window_parent_class)->dispose(object);
}

static void
gt_update_window_class_init(GtUpdateWindowClass *klass)
{
	G_OBJECT_CLASS(klass)->dispose = gt_update_window_dispose;
	GTK_WINDOW_CLASS(klass)->close_request = gt_update_window_close_request;
}

static GtkWidget *
row_label(const gchar *text)
{
	GtkWidget *l = gtk_label_new(text);

	gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
	return l;
}

static void
gt_update_window_init(GtUpdateWindow *self)
{
	GtkWidget *grid = gtk_grid_new();
	GtkWidget *revision, *buttons, *close;

	gtk_window_set_title(GTK_WINDOW(self), _("Updates"));
	gtk_window_set_default_size(GTK_WINDOW(self), 560, -1);
	gtk_widget_set_name(GTK_WIDGET(self), "update-window");
	gt_ui_close_on_escape(GTK_WINDOW(self));	/* dialogs.c : window.close → close_request */

	gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
	gtk_widget_set_margin_top(grid, 12);
	gtk_widget_set_margin_bottom(grid, 12);
	gtk_widget_set_margin_start(grid, 12);
	gtk_widget_set_margin_end(grid, 12);

	gtk_grid_attach(GTK_GRID(grid), row_label(_("Installed revision:")), 0, 0, 1, 1);
	revision = row_label(GIT_REVISION);
	gtk_label_set_selectable(GTK_LABEL(revision), TRUE);
	gtk_grid_attach(GTK_GRID(grid), revision, 1, 0, 1, 1);

	gtk_grid_attach(GTK_GRID(grid), row_label(_("Update source:")), 0, 1, 1, 1);
	self->url = gtk_entry_new();
	gtk_widget_set_name(self->url, "update-url");
	gtk_widget_set_hexpand(self->url, TRUE);
	gtk_editable_set_width_chars(GTK_EDITABLE(self->url), 50);
	gtk_editable_set_text(GTK_EDITABLE(self->url), settings.update_url != NULL ? settings.update_url : "");
	gtk_accessible_update_property(GTK_ACCESSIBLE(self->url), GTK_ACCESSIBLE_PROPERTY_LABEL, _("Update source"), -1);
	gtk_grid_attach(GTK_GRID(grid), self->url, 1, 1, 1, 1);

	self->startup = gtk_check_button_new_with_label(_("Check for updates at startup"));
	gtk_widget_set_name(self->startup, "update-startup");
	/* État posé avant le branchement du signal : ouvrir la fenêtre n'écrit rien */
	gtk_check_button_set_active(GTK_CHECK_BUTTON(self->startup), settings.update_check_startup);
	gtk_grid_attach(GTK_GRID(grid), self->startup, 0, 2, 2, 1);

	self->result = gtk_label_new("");
	gtk_widget_set_name(self->result, "update-result");
	gtk_label_set_xalign(GTK_LABEL(self->result), 0.0f);
	gtk_label_set_wrap(GTK_LABEL(self->result), TRUE);
	gtk_grid_attach(GTK_GRID(grid), self->result, 0, 3, 2, 1);

	buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign(buttons, GTK_ALIGN_END);
	self->check = gtk_button_new_with_label(_("Check now"));
	gtk_widget_set_name(self->check, "update-check");
	close = gtk_button_new_with_label(_("Close"));
	gtk_widget_set_name(close, "update-close");
	gtk_box_append(GTK_BOX(buttons), self->check);
	gtk_box_append(GTK_BOX(buttons), close);
	gtk_grid_attach(GTK_GRID(grid), buttons, 0, 4, 2, 1);

	gtk_window_set_child(GTK_WINDOW(self), grid);
	gtk_window_set_default_widget(GTK_WINDOW(self), self->check);

	g_signal_connect_swapped(self->check, "clicked", G_CALLBACK(start_check), self);
	g_signal_connect_swapped(self->url, "activate", G_CALLBACK(start_check), self);
	g_signal_connect(self->startup, "toggled", G_CALLBACK(on_startup_toggled), NULL);
	g_signal_connect_swapped(close, "clicked", G_CALLBACK(gtk_window_close), self);
}

void
gt_update_window_show(GtkWindow *parent)
{
	if(instance == NULL)
	{
		instance = g_object_new(GT_TYPE_UPDATE_WINDOW, "transient-for", parent, "destroy-with-parent", TRUE, NULL);
		g_object_add_weak_pointer(G_OBJECT(instance), (gpointer *)&instance);
	}
	gtk_window_present(GTK_WINDOW(instance));
}

/* ---- vérification au démarrage ------------------------------------- */

static void
on_startup_fetched(GObject *source, GAsyncResult *res, gpointer user_data)
{
	GCancellable *c = user_data;
	GError *error = NULL;
	gchar *remote = gt_update_fetch_remote_finish(res, &error);

	if(remote != NULL && !g_cancellable_is_cancelled(c) && startup_window != NULL)
	{
		gchar *cache = gt_update_cache_src_dir();

		/* Silencieuse sauf mise à jour disponible et non ignorée (M33, M34) */
		if(gt_update_compare(GIT_REVISION, remote, settings.update_skipped_revision, gt_update_local_is_newer(cache, GIT_REVISION, remote))
		   == GT_UPDATE_AVAILABLE)
			gt_update_offer(startup_window, remote);
		g_free(cache);
	}
	if(error != NULL)
		g_debug("update check: %s", error->message);
	g_clear_error(&error);
	g_free(remote);
	if(startup_cancellable == c)
		g_clear_object(&startup_cancellable);
	g_object_unref(c);
}

static gboolean
startup_check_idle(gpointer user_data)
{
	if(startup_window == NULL || !settings.update_check_startup || g_strcmp0(GIT_REVISION, "unknown") == 0)
		return G_SOURCE_REMOVE;
	startup_cancellable = g_cancellable_new();
	gt_update_fetch_remote_async(settings.update_url, startup_cancellable, on_startup_fetched, g_object_ref(startup_cancellable));
	return G_SOURCE_REMOVE;
}

void
gt_update_startup_check(GtkWindow *main_window)
{
	g_return_if_fail(main_window != NULL && startup_window == NULL);
	startup_window = main_window;
	g_object_add_weak_pointer(G_OBJECT(main_window), (gpointer *)&startup_window);
	g_idle_add(startup_check_idle, NULL);
}

void
gt_update_shutdown(void)
{
	if(startup_cancellable != NULL)
		g_cancellable_cancel(startup_cancellable);
	shutting_down = TRUE;	/* propositions ouvertes : fermées avec les fenêtres, leur choix tardif est ignoré */
	if(startup_window != NULL)
	{
		g_object_remove_weak_pointer(G_OBJECT(startup_window), (gpointer *)&startup_window);
		startup_window = NULL;
	}
}
