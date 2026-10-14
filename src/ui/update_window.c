/* Fenêtre « Updates » et vérification au démarrage (mise à jour par versions taguées) : voir update_window.h. Logique dans update_versions.c,
 * update_fetch.c et update_check.c (terminal) ; ici, seulement l'interface. */

#include "config.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <unistd.h>

#include "update_window.h"
#include "app.h"
#include "app_config.h"
#include "dialogs.h"
#include "project.h"
#include "session.h"
#include "session_manager.h"
#include "git_describe.h"
#include "git_revision.h"
#include "update_check.h"
#include "update_fetch.h"
#include "update_versions.h"

struct _GtUpdateWindow
{
	GtkWindow parent_instance;
	GtkWidget *url;	/* update-url */
	GtkWidget *startup;	/* update-startup */
	GtkWidget *check;	/* update-check */
	GtkWidget *versions;	/* update-version : GtkDropDown sur un GtkStringList de libellés, même ordre que releases->tags */
	GtkWidget *notes;	/* update-notes : GtkBox vertical de GtkLabel */
	GtkWidget *result;	/* update-result */
	GtkWidget *skip;	/* update-skip */
	GtkWidget *install;	/* update-install */
	GtReleases *releases;	/* dernière actualisation réussie ; NULL avant */
	GPtrArray *sections;	/* gt_news_parse(releases->news) */
	gchar *shown_url;	/* URL de la liste affichée */
	gchar *refresh_url;	/* URL de l'actualisation en cours */
	GCancellable *cancellable;	/* actualisation en cours ; annulée à la fermeture ou par une nouvelle actualisation */
};

G_DEFINE_FINAL_TYPE(GtUpdateWindow, gt_update_window, GTK_TYPE_WINDOW)

static GtUpdateWindow *instance;	/* pointeur faible : la fenêtre ouverte */
static GtkWindow *startup_window;	/* pointeur faible : fenêtre principale, parent de l'ouverture au démarrage */
static GCancellable *startup_cancellable;
static gboolean shutting_down;	/* posé par gt_update_shutdown() : plus d'ouverture au démarrage, confirmation tardive ignorée */

static void
installed_version(GtVersion *v)
{
	gt_version_installed(GIT_DESCRIBE, VERSION, v);
}

/* ---- lancement du script ------------------------------------------- */

/* Succès : la fenêtre « Updates » se ferme et le message d'information est transitoire de la fenêtre principale ; erreurs : elle reste le parent */
static void
run_update_script(GtkWindow *parent, const gchar *tag)
{
	gchar *terminal = gt_update_find_terminal();
	gchar **argv = NULL;
	GError *error = NULL;

	/* Arguments en liste (M34) : g_spawn_async, aucun shell n'interprète l'URL, le préfixe ni le tag (refusé s'il n'est pas vX.Y.Z) */
	if(terminal != NULL)
		argv = gt_update_terminal_argv(terminal, PACKAGE_DATA_DIR "/gtkterm-update.sh", settings.update_url, settings.update_prefix, getpid(), tag);
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
	{
		GtkWindow *main_window = gtk_window_get_transient_for(parent);

		if(main_window == NULL)
			main_window = gt_app_get_main_window();
		gt_ui_message(main_window, GT_APP_MSG_INFO, _("The update is running in a terminal window. GTKTerm will be closed and restarted when it finishes."));
		gtk_window_close(parent);
	}
	g_strfreev(argv);
}

/* ---- notes ---------------------------------------------------------- */

static void
notes_clear(GtUpdateWindow *self)
{
	GtkWidget *child;

	while((child = gtk_widget_get_first_child(self->notes)) != NULL)
		gtk_box_remove(GTK_BOX(self->notes), child);
}

/* Texte de NEWS tel quel : jamais de balisage Pango */
static void
notes_add(GtUpdateWindow *self, const gchar *text, const gchar *css_class, gint indent)
{
	GtkWidget *l = gtk_label_new(text);

	gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
	gtk_label_set_wrap(GTK_LABEL(l), TRUE);
	gtk_label_set_selectable(GTK_LABEL(l), TRUE);
	gtk_widget_set_focusable(l, FALSE);	/* le texte reste copiable à la souris, mais ne prend pas le focus (ni sélection automatique) */
	gtk_widget_set_margin_start(l, indent);
	if(css_class != NULL)
	{
		gchar **classes = g_strsplit(css_class, " ", -1);

		for(gint i = 0; classes[i] != NULL; i++)
			gtk_widget_add_css_class(l, classes[i]);
		g_strfreev(classes);
	}
	gtk_box_append(GTK_BOX(self->notes), l);
}

static const gchar *
selected_tag(GtUpdateWindow *self)
{
	guint i = gtk_drop_down_get_selected(GTK_DROP_DOWN(self->versions));

	if(self->releases == NULL || i == GTK_INVALID_LIST_POSITION || i >= self->releases->tags->len)
		return NULL;
	return g_ptr_array_index(self->releases->tags, i);
}

/* Seul point de décision de la sensibilité d'Installer : une version choisie, aucune actualisation en cours, et une liste qui vient de l'URL
 * enregistrée (le script reçoit settings.update_url : une liste d'une autre source ne doit pas servir) */
static gboolean
install_allowed(GtUpdateWindow *self)
{
	return selected_tag(self) != NULL && self->cancellable == NULL && g_strcmp0(self->shown_url, settings.update_url) == 0;
}

static void
update_install_sensitivity(GtUpdateWindow *self)
{
	gtk_widget_set_sensitive(self->install, install_allowed(self));
}

/* Sections retenues pour tag (pointeurs empruntés) ; NULL sans sélection */
static GPtrArray *
selected_sections(GtUpdateWindow *self, const gchar *tag, GtNewsDirection *dir)
{
	GtVersion installed, target;

	if(tag == NULL || self->releases == NULL || !gt_version_parse(tag, &target))
		return NULL;
	installed_version(&installed);
	return gt_news_select(self->sections, self->releases->tags, &installed, &target, dir);
}

static void
refresh_selection(GtUpdateWindow *self)
{
	const gchar *tag = selected_tag(self);
	GtNewsDirection dir = GT_NEWS_SAME;
	GPtrArray *sel = selected_sections(self, tag, &dir);
	gchar *label;

	notes_clear(self);
	if(sel == NULL)
	{
		gtk_widget_set_sensitive(self->install, FALSE);
		gtk_widget_set_visible(self->skip, FALSE);
		return;
	}
	if(dir == GT_NEWS_SAME)
		notes_add(self, _("This version is installed."), NULL, 0);
	if(gt_news_has_warning(sel))
	{
		notes_add(self, _("Warnings"), "update-warning heading", 0);
		for(guint i = 0; i < sel->len; i++)
		{
			GtNewsSection *s = g_ptr_array_index(sel, i);

			for(guint j = 0; j < s->entries->len; j++)
			{
				GtNewsEntry *e = g_ptr_array_index(s->entries, j);

				if(e->kind == GT_NEWS_WARNING)
				{
					gchar *t = g_strdup_printf("%s — %s", s->tag, e->text);

					notes_add(self, t, "update-warning", 12);
					g_free(t);
				}
			}
		}
	}
	if(dir == GT_NEWS_ROLLBACK && sel->len > 0)
		notes_add(self, _("You are leaving these versions:"), "heading", 0);
	/* Les avertissements ne sont montrés qu'une fois, en tête : les sections n'en reprennent que les nouveautés et le texte */
	for(guint i = 0; i < sel->len; i++)
	{
		GtNewsSection *s = g_ptr_array_index(sel, i);

		notes_add(self, s->title, "heading", 0);
		for(guint j = 0; j < s->entries->len; j++)
		{
			GtNewsEntry *e = g_ptr_array_index(s->entries, j);
			gchar *t;

			if(e->kind == GT_NEWS_WARNING)
				continue;
			t = e->kind == GT_NEWS_ITEM ? g_strconcat("• ", e->text, NULL) : g_strdup(e->text);
			notes_add(self, t, NULL, 12);
			g_free(t);
		}
	}
	if(dir == GT_NEWS_UPGRADE)
		label = g_strdup_printf(_("Update to %s"), tag);
	else if(dir == GT_NEWS_ROLLBACK)
		label = g_strdup_printf(_("Roll back to %s"), tag);
	else
		label = g_strdup_printf(_("Reinstall %s"), tag);
	gtk_button_set_label(GTK_BUTTON(self->install), label);
	g_free(label);
	update_install_sensitivity(self);
	/* Le verdict du démarrage ne compare que la plus récente : Skip n'est proposé que pour elle, et seulement si elle est plus récente que l'installée */
	gtk_widget_set_visible(self->skip, dir == GT_NEWS_UPGRADE && gtk_drop_down_get_selected(GTK_DROP_DOWN(self->versions)) == 0);
	g_ptr_array_unref(sel);
}

static void
set_result(GtUpdateWindow *self, const gchar *text)
{
	gtk_label_set_text(GTK_LABEL(self->result), text);
}

static void
set_result_for(GtUpdateWindow *self, GtReleases *r)
{
	GtVersion installed, newest;

	installed_version(&installed);
	if(r->tags->len == 0)
		set_result(self, _("No published version found."));
	else if(gt_version_parse(g_ptr_array_index(r->tags, 0), &newest) && gt_version_compare(&newest, &installed) > 0)
	{
		gchar *t = g_strdup_printf(_("A new version is available (%s)."), (const gchar *)g_ptr_array_index(r->tags, 0));

		set_result(self, t);
		g_free(t);
	}
	else
		set_result(self, _("GTKTerm is up to date."));
}

/* Prend possession de r */
static void
set_releases(GtUpdateWindow *self, GtReleases *r)
{
	GtkStringList *labels = gtk_string_list_new(NULL);
	GtVersion installed;

	gt_releases_free(self->releases);
	g_clear_pointer(&self->sections, g_ptr_array_unref);
	self->releases = r;
	g_free(self->shown_url);
	self->shown_url = g_strdup(self->refresh_url != NULL ? self->refresh_url : settings.update_url);
	self->sections = gt_news_parse(r->news);
	installed_version(&installed);
	for(guint i = 0; i < r->tags->len; i++)
	{
		const gchar *tag = g_ptr_array_index(r->tags, i);
		GtVersion v;
		gboolean latest = i == 0, inst;
		gchar *text;

		gt_version_parse(tag, &v);
		inst = gt_version_compare(&v, &installed) == 0;
		if(latest && inst)
			text = g_strdup_printf(_("%s — latest, installed"), tag);
		else if(latest)
			text = g_strdup_printf(_("%s — latest"), tag);
		else if(inst)
			text = g_strdup_printf(_("%s — installed"), tag);
		else
			text = g_strdup(tag);
		gtk_string_list_append(labels, text);
		g_free(text);
	}
	gtk_drop_down_set_model(GTK_DROP_DOWN(self->versions), G_LIST_MODEL(labels));
	g_object_unref(labels);
	gtk_widget_set_sensitive(self->versions, r->tags->len > 0);
	if(r->tags->len > 0)
		gtk_drop_down_set_selected(GTK_DROP_DOWN(self->versions), 0);
	refresh_selection(self);
	if(gtk_widget_get_sensitive(self->install))
		gtk_window_set_focus(GTK_WINDOW(self), self->install);	/* sinon : Check now (posé à la construction) */
}

/* ---- actualisation -------------------------------------------------- */

typedef struct
{
	GtUpdateWindow *self;	/* référence forte : les widgets ne sont touchés que si l'actualisation n'est pas annulée */
	GCancellable *cancellable;	/* celui de cette actualisation */
} CheckData;

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
	GtReleases *r = gt_releases_fetch_finish(res, &error);

	/* Fenêtre fermée (close-request/dispose ont annulé) ou actualisation remplacée : rien à afficher */
	if(!g_cancellable_is_cancelled(d->cancellable))
	{
		gtk_widget_set_sensitive(self->check, TRUE);
		gtk_widget_set_sensitive(self->url, TRUE);
		g_clear_object(&self->cancellable);
		if(r == NULL)
		{
			gchar *text = g_strdup_printf(_("Could not check for updates: %s"), error->message);

			set_result(self, text);
			g_free(text);
			/* Liste précédente gardée ; Installer n'a de sens que si elle vient de l'URL utilisée (le script reçoit settings.update_url) */
			refresh_selection(self);
			gtk_window_set_focus(GTK_WINDOW(self), self->check);	/* le focus posé à la construction a été perdu pendant l'actualisation */
		}
		else
		{
			set_result_for(self, r);
			set_releases(self, r);
			r = NULL;
		}
		g_clear_pointer(&self->refresh_url, g_free);
		gtk_widget_set_sensitive(self->versions, self->releases != NULL && self->releases->tags->len > 0);
		update_install_sensitivity(self);
	}
	gt_releases_free(r);
	g_clear_error(&error);
	g_object_unref(d->cancellable);
	g_object_unref(d->self);
	g_free(d);
}

/* « Check now », Entrée dans le champ, ouverture : actualise l'URL AFFICHÉE, après l'avoir enregistrée */
static void
start_check(GtUpdateWindow *self)
{
	gchar *url = g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(self->url))));
	gchar *cache;
	CheckData *d;

	if(self->cancellable != NULL)	/* actualisation en cours : la demande est ignorée */
	{
		g_free(url);
		return;
	}
	if(*url == '\0')
	{
		set_result(self, _("Please enter the URL of the source repository."));
		g_free(url);
		return;
	}
	config_save_update_url(url);	/* n'écrit que si elle a changé ; le script lancé ensuite reçoit settings.update_url = cette URL */
	self->cancellable = g_cancellable_new();
	gtk_widget_set_sensitive(self->check, FALSE);
	gtk_widget_set_sensitive(self->url, FALSE);
	gtk_widget_set_sensitive(self->install, FALSE);
	gtk_widget_set_sensitive(self->versions, FALSE);
	gtk_widget_set_visible(self->skip, FALSE);	/* rétabli par refresh_selection() à la fin de l'actualisation */
	g_free(self->refresh_url);
	self->refresh_url = g_strdup(url);
	set_result(self, _("Checking…"));
	d = g_new0(CheckData, 1);
	d->self = g_object_ref(self);
	d->cancellable = g_object_ref(self->cancellable);
	cache = gt_releases_cache_dir();
	gt_releases_fetch_async(url, cache, self->cancellable, on_check_done, d);
	g_free(cache);
	g_free(url);
}

/* ---- boutons -------------------------------------------------------- */

typedef struct
{
	GtUpdateWindow *self;	/* pointeur faible */
	gchar *tag;
} InstallData;

static void
on_install_confirmed(gboolean accepted, gpointer user_data)
{
	InstallData *d = user_data;

	if(accepted && !shutting_down && d->self != NULL)
		run_update_script(GTK_WINDOW(d->self), d->tag);
	if(d->self != NULL)
		g_object_remove_weak_pointer(G_OBJECT(d->self), (gpointer *)&d->self);
	g_free(d->tag);
	g_free(d);
}

/* À la fin de la mise à jour le script arrête GTKTerm (SIGTERM : rien n'est enregistré) puis le relance : un projet modifié ou un onglet occupé le perdrait */
static gboolean
session_loss_risk(void)
{
	GtProject *project = gt_app_get_project();
	GtSessionManager *sessions = gt_app_get_sessions();

	if(project != NULL)
		gt_project_check_now(project);	/* « modifié » est recalculé en différé : le lire à jour */
	if(project != NULL && gt_project_is_modified(project))
		return TRUE;
	if(sessions != NULL)
	{
		for(guint i = 0; i < gt_session_manager_get_n(sessions); i++)
			if(gt_session_is_busy(gt_session_manager_get_nth(sessions, i)))
				return TRUE;
	}
	return FALSE;
}

static void
on_install_clicked(GtUpdateWindow *self)
{
	const gchar *tag = selected_tag(self);
	GPtrArray *sel = selected_sections(self, tag, NULL);
	gboolean warn = gt_news_has_warning(sel);
	gboolean risk = session_loss_risk();
	InstallData *d;
	gchar *question, *detail;

	if(sel != NULL)
		g_ptr_array_unref(sel);
	if(tag == NULL || !install_allowed(self))
		return;
	if(!warn && !risk)
	{
		run_update_script(GTK_WINDOW(self), tag);
		return;
	}
	/* Avertissements ou travail en cours : confirmation (non bloquante) avant de lancer le script */
	d = g_new0(InstallData, 1);
	d->self = self;
	g_object_add_weak_pointer(G_OBJECT(self), (gpointer *)&d->self);
	d->tag = g_strdup(tag);
	question = g_strdup_printf(_("Install %s?"), tag);
	if(warn && risk)
		detail = g_strdup(_("See the warnings above. GTKTerm will be closed and restarted at the end of the update: unsaved project changes and running operations will be lost."));
	else if(risk)
		detail = g_strdup(_("GTKTerm will be closed and restarted at the end of the update: unsaved project changes and running operations will be lost."));
	else
		detail = g_strdup(_("See the warnings above."));
	gt_ui_confirm(GTK_WINDOW(self), question, detail, _("Install"), on_install_confirmed, d);
	g_free(question);
	g_free(detail);
}

static void
on_skip_clicked(GtUpdateWindow *self)
{
	const gchar *tag = selected_tag(self);

	if(tag == NULL)
		return;
	/* « Skip this version » survit au redémarrage */
	config_save_skipped_version(tag);
	gtk_window_close(GTK_WINDOW(self));
}

static void
on_startup_toggled(GtkCheckButton *button, gpointer user_data)
{
	config_save_update_check_startup(gtk_check_button_get_active(button));
}

static void
on_version_selected(GObject *dropdown, GParamSpec *pspec, gpointer user_data)
{
	refresh_selection(GT_UPDATE_WINDOW(user_data));
}

/* ---- classe --------------------------------------------------------- */

static gboolean
gt_update_window_close_request(GtkWindow *window)
{
	GtUpdateWindow *self = GT_UPDATE_WINDOW(window);

	cancel_check(self);
	/* La fenêtre est toujours détruite ici : elle ne doit plus être présentée, même si dispose est retardé par la référence de CheckData */
	if(instance == self)
	{
		g_object_remove_weak_pointer(G_OBJECT(self), (gpointer *)&instance);
		instance = NULL;
	}
	return FALSE;	/* fermeture acceptée (hide-on-close FALSE : la fenêtre est détruite) */
}

static void
gt_update_window_dispose(GObject *object)
{
	GtUpdateWindow *self = GT_UPDATE_WINDOW(object);

	cancel_check(self);
	gt_releases_free(self->releases);
	self->releases = NULL;
	g_clear_pointer(&self->sections, g_ptr_array_unref);
	g_clear_pointer(&self->shown_url, g_free);
	g_clear_pointer(&self->refresh_url, g_free);
	/* Détruite sans close-request (parent détruit) : même nettoyage ; sans effet si close_request l'a déjà fait */
	if(instance == self)
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

/* Avertissements de NEWS : en gras et en couleur d'erreur du thème (ni Default ni Yaru ne stylent « warning » sur un GtkLabel) ; installé une fois */
static void
install_css(GtkWidget *widget)
{
	static gboolean done;
	GtkCssProvider *css;

	if(done)
		return;
	done = TRUE;
	css = gtk_css_provider_new();
	gtk_css_provider_load_from_string(css, ".update-warning { color: @error_color; font-weight: bold; }");
	gtk_style_context_add_provider_for_display(gtk_widget_get_display(widget), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(css);
}

static void
gt_update_window_init(GtUpdateWindow *self)
{
	GtkWidget *grid = gtk_grid_new();
	GtkWidget *installed, *row, *scroll, *buttons, *spacer, *close;
	gchar *installed_text;

	gtk_window_set_title(GTK_WINDOW(self), _("Updates"));
	gtk_window_set_default_size(GTK_WINDOW(self), 640, 520);
	gtk_widget_set_name(GTK_WIDGET(self), "update-window");
	gt_ui_close_on_escape(GTK_WINDOW(self));	/* dialogs.c : window.close → close_request */

	gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
	gtk_widget_set_margin_top(grid, 12);
	gtk_widget_set_margin_bottom(grid, 12);
	gtk_widget_set_margin_start(grid, 12);
	gtk_widget_set_margin_end(grid, 12);

	gtk_grid_attach(GTK_GRID(grid), row_label(_("Installed version:")), 0, 0, 1, 1);
	installed_text = g_strdup_printf("%s (%.7s)", GIT_DESCRIBE, GIT_REVISION);
	installed = row_label(installed_text);
	g_free(installed_text);
	gtk_widget_set_name(installed, "update-installed");
	gtk_label_set_selectable(GTK_LABEL(installed), TRUE);
	gtk_widget_set_focusable(installed, FALSE);	/* sélectionnable à la souris, jamais prise de focus */
	gtk_grid_attach(GTK_GRID(grid), installed, 1, 0, 1, 1);

	gtk_grid_attach(GTK_GRID(grid), row_label(_("Update source:")), 0, 1, 1, 1);
	self->url = gtk_entry_new();
	gtk_widget_set_name(self->url, "update-url");
	gtk_widget_set_hexpand(self->url, TRUE);
	gtk_editable_set_width_chars(GTK_EDITABLE(self->url), 50);
	gtk_editable_set_text(GTK_EDITABLE(self->url), settings.update_url != NULL ? settings.update_url : "");
	gtk_accessible_update_property(GTK_ACCESSIBLE(self->url), GTK_ACCESSIBLE_PROPERTY_LABEL, _("Update source"), -1);
	gtk_grid_attach(GTK_GRID(grid), self->url, 1, 1, 1, 1);

	row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	self->startup = gtk_check_button_new_with_label(_("Check for updates at startup"));
	gtk_widget_set_name(self->startup, "update-startup");
	gtk_widget_set_hexpand(self->startup, TRUE);
	/* État posé avant le branchement du signal : ouvrir la fenêtre n'écrit rien */
	gtk_check_button_set_active(GTK_CHECK_BUTTON(self->startup), settings.update_check_startup);
	self->check = gtk_button_new_with_label(_("Check now"));
	gtk_widget_set_name(self->check, "update-check");
	gtk_box_append(GTK_BOX(row), self->startup);
	gtk_box_append(GTK_BOX(row), self->check);
	gtk_grid_attach(GTK_GRID(grid), row, 0, 2, 2, 1);

	gtk_grid_attach(GTK_GRID(grid), row_label(_("Version:")), 0, 3, 1, 1);
	self->versions = gtk_drop_down_new(G_LIST_MODEL(gtk_string_list_new(NULL)), NULL);
	gtk_widget_set_name(self->versions, "update-version");
	gtk_widget_set_sensitive(self->versions, FALSE);
	gtk_accessible_update_property(GTK_ACCESSIBLE(self->versions), GTK_ACCESSIBLE_PROPERTY_LABEL, _("Version"), -1);
	gtk_grid_attach(GTK_GRID(grid), self->versions, 1, 3, 1, 1);

	scroll = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 220);
	gtk_widget_set_vexpand(scroll, TRUE);
	self->notes = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_widget_set_name(self->notes, "update-notes");
	gtk_widget_set_margin_start(self->notes, 6);
	gtk_widget_set_margin_end(self->notes, 6);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), self->notes);
	gtk_grid_attach(GTK_GRID(grid), scroll, 0, 4, 2, 1);

	self->result = row_label("");
	gtk_widget_set_name(self->result, "update-result");
	gtk_label_set_wrap(GTK_LABEL(self->result), TRUE);
	gtk_grid_attach(GTK_GRID(grid), self->result, 0, 5, 2, 1);

	buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	self->skip = gtk_button_new_with_label(_("Skip this version"));
	gtk_widget_set_name(self->skip, "update-skip");
	gtk_widget_set_visible(self->skip, FALSE);
	spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_hexpand(spacer, TRUE);
	self->install = gtk_button_new_with_label(_("Install"));
	gtk_widget_set_name(self->install, "update-install");
	gtk_widget_add_css_class(self->install, "suggested-action");
	gtk_widget_set_sensitive(self->install, FALSE);
	close = gtk_button_new_with_label(_("Close"));
	gtk_widget_set_name(close, "update-close");
	gtk_box_append(GTK_BOX(buttons), self->skip);
	gtk_box_append(GTK_BOX(buttons), spacer);
	gtk_box_append(GTK_BOX(buttons), self->install);
	gtk_box_append(GTK_BOX(buttons), close);
	gtk_grid_attach(GTK_GRID(grid), buttons, 0, 6, 2, 1);

	gtk_window_set_child(GTK_WINDOW(self), grid);
	install_css(GTK_WIDGET(self));

	g_signal_connect_swapped(self->check, "clicked", G_CALLBACK(start_check), self);
	g_signal_connect_swapped(self->url, "activate", G_CALLBACK(start_check), self);
	g_signal_connect(self->startup, "toggled", G_CALLBACK(on_startup_toggled), NULL);
	g_signal_connect(self->versions, "notify::selected", G_CALLBACK(on_version_selected), self);
	g_signal_connect_swapped(self->skip, "clicked", G_CALLBACK(on_skip_clicked), self);
	g_signal_connect_swapped(self->install, "clicked", G_CALLBACK(on_install_clicked), self);
	g_signal_connect_swapped(close, "clicked", G_CALLBACK(gtk_window_close), self);
	/* Focus initial : pas sur l'étiquette sélectionnable de la version installée ; Installer devient sensible après l'actualisation (voir set_releases) */
	gtk_window_set_focus(GTK_WINDOW(self), self->check);
}

static GtUpdateWindow *
window_new(GtkWindow *parent)
{
	instance = g_object_new(GT_TYPE_UPDATE_WINDOW, "transient-for", parent, "destroy-with-parent", TRUE, NULL);
	g_object_add_weak_pointer(G_OBJECT(instance), (gpointer *)&instance);
	return instance;
}

void
gt_update_window_show(GtkWindow *parent)
{
	if(instance == NULL)
	{
		/* Fenêtre créée à la main : l'actualisation de démarrage en cours est abandonnée (la fenêtre actualise elle-même) */
		if(startup_cancellable != NULL)
			g_cancellable_cancel(startup_cancellable);
		start_check(window_new(parent));
	}
	gtk_window_present(GTK_WINDOW(instance));
}

/* ---- vérification au démarrage ------------------------------------- */

static void
on_startup_fetched(GObject *source, GAsyncResult *res, gpointer user_data)
{
	GCancellable *c = user_data;
	GError *error = NULL;
	GtReleases *r = gt_releases_fetch_finish(res, &error);

	/* Fenêtre déjà ouverte à la main : elle actualise elle-même */
	if(r != NULL && !g_cancellable_is_cancelled(c) && !shutting_down && startup_window != NULL && instance == NULL)
	{
		GtVersion installed;
		const gchar *newest = r->tags->len > 0 ? g_ptr_array_index(r->tags, 0) : NULL;

		installed_version(&installed);
		/* Silencieuse sauf version plus récente et non ignorée */
		if(gt_release_verdict(&installed, newest, settings.update_skipped_version) == GT_RELEASE_AVAILABLE)
		{
			GtUpdateWindow *w = window_new(startup_window);

			set_result_for(w, r);
			set_releases(w, r);
			r = NULL;
			gtk_window_present(GTK_WINDOW(w));
		}
	}
	if(error != NULL)
		g_debug("update check: %s", error->message);
	g_clear_error(&error);
	gt_releases_free(r);
	if(startup_cancellable == c)
		g_clear_object(&startup_cancellable);
	g_object_unref(c);
}

static gboolean
startup_check_idle(gpointer user_data)
{
	gchar *cache;

	if(startup_window == NULL || !settings.update_check_startup || shutting_down || instance != NULL)	/* fenêtre « Updates » déjà ouverte : elle actualise elle-même */
		return G_SOURCE_REMOVE;
	startup_cancellable = g_cancellable_new();
	cache = gt_releases_cache_dir();
	gt_releases_fetch_async(settings.update_url, cache, startup_cancellable, on_startup_fetched, g_object_ref(startup_cancellable));
	g_free(cache);
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
	if(instance != NULL)
		cancel_check(instance);	/* une actualisation manuelle ne survit pas à l'arrêt */
	shutting_down = TRUE;	/* confirmations ouvertes : fermées avec les fenêtres, leur réponse tardive est ignorée */
	if(startup_window != NULL)
	{
		g_object_remove_weak_pointer(G_OBJECT(startup_window), (gpointer *)&startup_window);
		startup_window = NULL;
	}
}
