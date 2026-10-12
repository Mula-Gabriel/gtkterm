/* Fenêtre « Updates » (ligne GTK 3) : voir update.h. Logique dans update_versions.c (versions, notes) et update_fetch.c (git) ; ici,
 * seulement l'interface et le lancement du script. */

#include "config.h"

#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <string.h>
#include <unistd.h>

#include "update.h"
#include "update_fetch.h"
#include "update_versions.h"
#include "term_config.h"

extern struct configuration_port config;

typedef struct
{
	GtkWidget *window;
	GtkWidget *url;
	GtkWidget *startup;
	GtkWidget *check;
	GtkWidget *versions;	/* GtkComboBoxText : identifiant = tag, texte = libellé */
	GtkWidget *notes;	/* GtkBox vertical de GtkLabel */
	GtkWidget *result;
	GtkWidget *skip;
	GtkWidget *install;
	GtReleases *releases;	/* dernière actualisation réussie ; NULL avant */
	GPtrArray *sections;	/* gt_news_parse(releases->news) */
	GCancellable *cancellable;	/* actualisation en cours */
} UpdateWindow;

static UpdateWindow *win;	/* fenêtre ouverte ; NULL sinon */
static GtkWindow *startup_parent;
static GCancellable *startup_cancellable;

static void
installed_version(GtVersion *v)
{
	gt_version_installed(GIT_DESCRIBE, VERSION, v);
}

static void
show_message(GtkWindow *parent, GtkMessageType type, const gchar *text)
{
	GtkWidget *m = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, type, GTK_BUTTONS_OK, "%s", text);

	g_signal_connect(m, "response", G_CALLBACK(gtk_widget_destroy), NULL);
	gtk_widget_show(m);
}

/* ---- lancer le script dans un terminal visible -------------------- */

typedef struct
{
	const gchar *name;
	const gchar *flag;	/* "-e", "--", ou NULL (kitty) */
	const gchar *extra;	/* options (police, zoom) placées avant -e / -- */
} TerminalDef;

static const TerminalDef terminals[] =
{
	{"xterm", "-e", "-fa Monospace -fs 12"},
	{"x-terminal-emulator", "-e", NULL},
	{"gnome-terminal", "--", "--zoom=1.5"},
	{"xfce4-terminal", "-x", "--font 'Monospace 12'"},
	{"konsole", "-e", NULL},
	{"lxterminal", "-e", NULL},
	{"qterminal", "-e", NULL},
	{"urxvt", "-e", "-fn 'xft:Monospace:size=12'"},
	{"kitty", NULL, "-o font_size=14"},
	{NULL, NULL, NULL}
};

static const TerminalDef *
find_terminal(void)
{
	for(gint i = 0; terminals[i].name != NULL; i++)
	{
		gchar *path = g_find_program_in_path(terminals[i].name);

		if(path != NULL)
		{
			g_free(path);
			return &terminals[i];
		}
	}
	return NULL;
}

static void
run_update_script(GtkWindow *parent, const gchar *tag)
{
	const TerminalDef *def;
	gchar *q_script, *q_url, *q_prefix, *q_tag, *inner_cmd, *inner, *cmd;
	GError *error = NULL;
	gboolean launched = FALSE;
	GtkWindow *main_parent = NULL;

	/* Seule une version publiée vX.Y.Z part vers le script */
	if(!gt_version_is_release_tag(tag))
		return;
	def = find_terminal();
	if(def == NULL)
	{
		show_message(parent, GTK_MESSAGE_ERROR, _("No terminal emulator found. Please install xterm or similar."));
		return;
	}
	/* Chaque valeur passe par g_shell_quote : une apostrophe dans l'URL ou le préfixe ne casse plus la commande. « read _ » : sous dash,
	 * « read » sans nom échoue et le terminal se fermait avant que l'on puisse lire la sortie. */
	q_script = g_shell_quote(PACKAGE_DATA_DIR "/gtkterm-update.sh");
	q_url = g_shell_quote(config.update_url);
	q_prefix = g_shell_quote(config.update_prefix);
	q_tag = g_shell_quote(tag);
	inner_cmd = g_strdup_printf("sh %s %s %s %d %s; echo; echo '>>> Appuyez sur Entrée pour fermer...'; read _", q_script, q_url, q_prefix, (gint)getpid(), q_tag);
	inner = g_shell_quote(inner_cmd);
	if(def->flag == NULL)
		cmd = g_strdup_printf("%s %s sh -c %s", def->name, def->extra ? def->extra : "", inner);
	else
		cmd = g_strdup_printf("%s %s %s sh -c %s", def->name, def->extra ? def->extra : "", def->flag, inner);
	if(!g_spawn_command_line_async(cmd, &error))
	{
		gchar *text = g_strdup_printf(_("Failed to launch update terminal:\n%s"), error->message);

		show_message(parent, GTK_MESSAGE_ERROR, text);
		g_free(text);
		g_clear_error(&error);
	}
	else
	{
		/* Message transitoire pour la fenêtre principale (celle de « Updates » va disparaître), puis fermeture de « Updates » :
		 * on ne peut pas cliquer deux fois sur Install (deux mises à jour concurrentes) */
		main_parent = win != NULL ? gtk_window_get_transient_for(GTK_WINDOW(win->window)) : NULL;
		show_message(main_parent, GTK_MESSAGE_INFO, _("The update is running in a terminal window.\nFollow the instructions there and restart GTKTerm when done."));
		launched = TRUE;
	}
	g_free(cmd);
	g_free(inner);
	g_free(inner_cmd);
	g_free(q_tag);
	g_free(q_prefix);
	g_free(q_url);
	g_free(q_script);
	if(launched && win != NULL)
		gtk_widget_destroy(win->window);
}

/* ---- notes ---------------------------------------------------------- */

static void
install_css(void)
{
	static gboolean done;
	GtkCssProvider *css;

	if(done)
		return;
	done = TRUE;
	css = gtk_css_provider_new();
	gtk_css_provider_load_from_data(css, ".gt-update-warning { color: #c01c28; font-weight: bold; } .gt-update-heading { font-weight: bold; }", -1, NULL);
	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(css);
}

static void
notes_clear(void)
{
	GList *children = gtk_container_get_children(GTK_CONTAINER(win->notes));

	for(GList *l = children; l != NULL; l = l->next)
		gtk_widget_destroy(GTK_WIDGET(l->data));
	g_list_free(children);
}

/* Texte de NEWS tel quel : jamais de balisage Pango */
static void
notes_add(const gchar *text, const gchar *css_class, gint indent)
{
	GtkWidget *l = gtk_label_new(text);

	gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
	gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
	gtk_label_set_selectable(GTK_LABEL(l), TRUE);
	gtk_widget_set_margin_start(l, indent);
	if(css_class != NULL)
		gtk_style_context_add_class(gtk_widget_get_style_context(l), css_class);
	gtk_box_pack_start(GTK_BOX(win->notes), l, FALSE, FALSE, 0);
	gtk_widget_show(l);
}

static const gchar *
selected_tag(void)
{
	return gtk_combo_box_get_active_id(GTK_COMBO_BOX(win->versions));
}

/* Sections retenues pour le tag choisi (pointeurs empruntés) ; NULL sans sélection */
static GPtrArray *
selected_sections(const gchar *tag, GtNewsDirection *dir)
{
	GtVersion installed, target;

	if(tag == NULL || win->releases == NULL || !gt_version_parse(tag, &target))
		return NULL;
	installed_version(&installed);
	return gt_news_select(win->sections, win->releases->tags, &installed, &target, dir);
}

static void
refresh_selection(void)
{
	const gchar *tag = selected_tag();
	GtNewsDirection dir = GT_NEWS_SAME;
	GPtrArray *sel = selected_sections(tag, &dir);
	gchar *label;

	notes_clear();
	if(sel == NULL)
	{
		gtk_widget_set_sensitive(win->install, FALSE);
		gtk_widget_hide(win->skip);
		return;
	}
	if(dir == GT_NEWS_SAME)
		notes_add(_("This version is installed."), NULL, 0);
	if(gt_news_has_warning(sel))
	{
		notes_add(_("Warnings"), "gt-update-warning", 0);
		for(guint i = 0; i < sel->len; i++)
		{
			GtNewsSection *s = g_ptr_array_index(sel, i);

			for(guint j = 0; j < s->entries->len; j++)
			{
				GtNewsEntry *e = g_ptr_array_index(s->entries, j);

				if(e->kind == GT_NEWS_WARNING)
				{
					gchar *t = g_strdup_printf("%s — %s", s->tag, e->text);

					notes_add(t, "gt-update-warning", 12);
					g_free(t);
				}
			}
		}
	}
	if(dir == GT_NEWS_ROLLBACK && sel->len > 0)
		notes_add(_("You are leaving these versions:"), "gt-update-heading", 0);
	for(guint i = 0; i < sel->len; i++)
	{
		GtNewsSection *s = g_ptr_array_index(sel, i);

		notes_add(s->title, "gt-update-heading", 0);
		for(guint j = 0; j < s->entries->len; j++)
		{
			GtNewsEntry *e = g_ptr_array_index(s->entries, j);
			gchar *t = e->kind == GT_NEWS_ITEM ? g_strconcat("• ", e->text, NULL) : e->kind == GT_NEWS_WARNING ? g_strconcat("⚠ ", e->text, NULL) : g_strdup(e->text);

			notes_add(t, e->kind == GT_NEWS_WARNING ? "gt-update-warning" : NULL, 12);
			g_free(t);
		}
	}
	if(dir == GT_NEWS_UPGRADE)
		label = g_strdup_printf(_("Update to %s"), tag);
	else if(dir == GT_NEWS_ROLLBACK)
		label = g_strdup_printf(_("Roll back to %s"), tag);
	else
		label = g_strdup_printf(_("Reinstall %s"), tag);
	gtk_button_set_label(GTK_BUTTON(win->install), label);
	g_free(label);
	gtk_widget_set_sensitive(win->install, TRUE);
	/* « Skip this version » : seulement la plus récente publiée (c'est la seule que la vérification du démarrage compare) et plus récente que l'installée */
	gtk_widget_set_visible(win->skip, dir == GT_NEWS_UPGRADE && gtk_combo_box_get_active(GTK_COMBO_BOX(win->versions)) == 0);
	g_ptr_array_unref(sel);
}

/* Prend possession de r */
static void
set_releases(GtReleases *r)
{
	GtVersion installed;

	gt_releases_free(win->releases);
	if(win->sections != NULL)
		g_ptr_array_unref(win->sections);
	win->releases = r;
	win->sections = gt_news_parse(r->news);
	installed_version(&installed);
	gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(win->versions));
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
		gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(win->versions), tag, text);
		g_free(text);
	}
	gtk_widget_set_sensitive(win->versions, r->tags->len > 0);
	if(r->tags->len > 0)
		gtk_combo_box_set_active(GTK_COMBO_BOX(win->versions), 0);
	refresh_selection();
}

static void
set_result_for(GtReleases *r)
{
	GtVersion installed, newest;

	installed_version(&installed);
	if(r->tags->len == 0)
		gtk_label_set_text(GTK_LABEL(win->result), _("No published version found."));
	else if(gt_version_parse(g_ptr_array_index(r->tags, 0), &newest) && gt_version_compare(&newest, &installed) > 0)
	{
		gchar *t = g_strdup_printf(_("A new version is available (%s)."), (const gchar *)g_ptr_array_index(r->tags, 0));

		gtk_label_set_text(GTK_LABEL(win->result), t);
		g_free(t);
	}
	else
		gtk_label_set_text(GTK_LABEL(win->result), _("GTKTerm is up to date."));
}

/* ---- actualisation -------------------------------------------------- */

static void
on_fetched(GObject *source, GAsyncResult *res, gpointer user_data)
{
	GCancellable *c = user_data;
	GError *error = NULL;
	GtReleases *r = gt_releases_fetch_finish(res, &error);

	/* Fenêtre fermée (annulée) ou actualisation remplacée : rien à afficher */
	if(!g_cancellable_is_cancelled(c) && win != NULL && win->cancellable == c)
	{
		g_clear_object(&win->cancellable);
		gtk_widget_set_sensitive(win->check, TRUE);
		gtk_widget_set_sensitive(win->url, TRUE);
		if(r == NULL)
		{
			gchar *t = g_strdup_printf(_("Could not check for updates: %s"), error->message);

			gtk_label_set_text(GTK_LABEL(win->result), t);
			g_free(t);
		}
		else
		{
			set_result_for(r);
			set_releases(r);
			r = NULL;
		}
	}
	gt_releases_free(r);
	g_clear_error(&error);
	g_object_unref(c);
}

/* « Check now », Entrée dans le champ, ouverture : actualise l'URL AFFICHÉE, après l'avoir enregistrée si elle a changé */
static void
start_check(void)
{
	gchar *url;
	gchar *cache;

	/* Une actualisation est déjà en cours : pas de second git sur la même copie de travail */
	if(win->cancellable != NULL)
		return;
	url = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(win->url))));

	if(*url == '\0')
	{
		gtk_label_set_text(GTK_LABEL(win->result), _("Please enter the URL of the source repository."));
		g_free(url);
		return;
	}
	if(g_strcmp0(url, config.update_url) != 0)
	{
		g_strlcpy(config.update_url, url, sizeof(config.update_url));
		save_config_silent();
	}
	win->cancellable = g_cancellable_new();
	gtk_widget_set_sensitive(win->check, FALSE);
	gtk_widget_set_sensitive(win->url, FALSE);
	gtk_label_set_text(GTK_LABEL(win->result), _("Checking…"));
	cache = gt_releases_cache_dir();
	gt_releases_fetch_async(url, cache, win->cancellable, on_fetched, g_object_ref(win->cancellable));
	g_free(cache);
	g_free(url);
}

/* ---- boutons -------------------------------------------------------- */

static void
on_confirm_response(GtkDialog *dialog, gint response, gpointer user_data)
{
	gchar *tag = g_strdup(g_object_get_data(G_OBJECT(dialog), "tag"));

	gtk_widget_destroy(GTK_WIDGET(dialog));
	if(response == GTK_RESPONSE_YES && win != NULL)
		run_update_script(GTK_WINDOW(win->window), tag);
	g_free(tag);
}

static void
on_install_clicked(GtkButton *button, gpointer user_data)
{
	const gchar *tag = selected_tag();
	GPtrArray *sel = selected_sections(tag, NULL);
	gboolean warn = gt_news_has_warning(sel);

	if(sel != NULL)
		g_ptr_array_unref(sel);
	if(tag == NULL)
		return;
	if(!warn)
	{
		run_update_script(GTK_WINDOW(win->window), tag);
		return;
	}
	/* Avertissements : confirmation (non bloquante) avant de lancer le script */
	GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(win->window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO,
	                                      _("Install %s?"), tag);

	gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s", _("See the warnings above."));
	gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_NO);
	g_object_set_data_full(G_OBJECT(d), "tag", g_strdup(tag), g_free);
	g_signal_connect(d, "response", G_CALLBACK(on_confirm_response), NULL);
	gtk_widget_show(d);
}

static void
on_skip_clicked(GtkButton *button, gpointer user_data)
{
	const gchar *tag = selected_tag();

	if(tag == NULL)
		return;
	g_strlcpy(config.update_skipped_version, tag, sizeof(config.update_skipped_version));
	save_config_silent();
	gtk_widget_destroy(win->window);
}

static void
on_startup_toggled(GtkToggleButton *button, gpointer user_data)
{
	config.update_check_startup = gtk_toggle_button_get_active(button);
	save_config_silent();
}

static void
on_check_clicked(GtkWidget *widget, gpointer user_data)
{
	start_check();
}

static void
on_versions_changed(GtkComboBox *combo, gpointer user_data)
{
	refresh_selection();
}

static void
on_destroy(GtkWidget *widget, gpointer user_data)
{
	if(win->cancellable != NULL)
	{
		g_cancellable_cancel(win->cancellable);
		g_clear_object(&win->cancellable);
	}
	gt_releases_free(win->releases);
	if(win->sections != NULL)
		g_ptr_array_unref(win->sections);
	g_free(win);
	win = NULL;
}

static GtkWidget *
row_label(const gchar *text)
{
	GtkWidget *l = gtk_label_new(text);

	gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
	return l;
}

static void
build_window(GtkWindow *parent)
{
	GtkWidget *grid = gtk_grid_new();
	GtkWidget *installed_label, *row, *scroll, *buttons, *close;
	gchar *installed_text;

	install_css();
	win = g_new0(UpdateWindow, 1);
	win->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(win->window), _("Updates"));
	gtk_window_set_transient_for(GTK_WINDOW(win->window), parent);
	gtk_window_set_destroy_with_parent(GTK_WINDOW(win->window), TRUE);
	gtk_window_set_default_size(GTK_WINDOW(win->window), 640, 520);

	gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
	gtk_container_set_border_width(GTK_CONTAINER(grid), 12);

	gtk_grid_attach(GTK_GRID(grid), row_label(_("Installed version:")), 0, 0, 1, 1);
	installed_text = g_strdup_printf("%s (%.7s)", GIT_DESCRIBE, GIT_REVISION);
	installed_label = row_label(installed_text);
	g_free(installed_text);
	gtk_label_set_selectable(GTK_LABEL(installed_label), TRUE);
	gtk_grid_attach(GTK_GRID(grid), installed_label, 1, 0, 1, 1);

	gtk_grid_attach(GTK_GRID(grid), row_label(_("Update source:")), 0, 1, 1, 1);
	win->url = gtk_entry_new();
	gtk_widget_set_hexpand(win->url, TRUE);
	gtk_entry_set_width_chars(GTK_ENTRY(win->url), 50);
	gtk_entry_set_text(GTK_ENTRY(win->url), config.update_url);
	gtk_grid_attach(GTK_GRID(grid), win->url, 1, 1, 1, 1);

	row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	win->startup = gtk_check_button_new_with_label(_("Check for updates at startup"));
	/* État posé avant le branchement du signal : ouvrir la fenêtre n'écrit rien */
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(win->startup), config.update_check_startup);
	win->check = gtk_button_new_with_label(_("Check now"));
	gtk_box_pack_start(GTK_BOX(row), win->startup, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(row), win->check, FALSE, FALSE, 0);
	gtk_grid_attach(GTK_GRID(grid), row, 0, 2, 2, 1);

	gtk_grid_attach(GTK_GRID(grid), row_label(_("Version:")), 0, 3, 1, 1);
	win->versions = gtk_combo_box_text_new();
	gtk_widget_set_sensitive(win->versions, FALSE);
	gtk_grid_attach(GTK_GRID(grid), win->versions, 1, 3, 1, 1);

	scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 220);
	gtk_widget_set_vexpand(scroll, TRUE);
	win->notes = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_container_set_border_width(GTK_CONTAINER(win->notes), 6);
	gtk_container_add(GTK_CONTAINER(scroll), win->notes);
	gtk_grid_attach(GTK_GRID(grid), scroll, 0, 4, 2, 1);

	win->result = row_label("");
	gtk_label_set_line_wrap(GTK_LABEL(win->result), TRUE);
	gtk_grid_attach(GTK_GRID(grid), win->result, 0, 5, 2, 1);

	buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	win->skip = gtk_button_new_with_label(_("Skip this version"));
	win->install = gtk_button_new_with_label(_("Install"));
	gtk_widget_set_sensitive(win->install, FALSE);
	close = gtk_button_new_with_label(_("Close"));
	gtk_box_pack_start(GTK_BOX(buttons), win->skip, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(buttons), close, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(buttons), win->install, FALSE, FALSE, 0);
	gtk_grid_attach(GTK_GRID(grid), buttons, 0, 6, 2, 1);

	gtk_container_add(GTK_CONTAINER(win->window), grid);

	g_signal_connect(win->window, "destroy", G_CALLBACK(on_destroy), NULL);
	g_signal_connect(win->check, "clicked", G_CALLBACK(on_check_clicked), NULL);
	g_signal_connect(win->url, "activate", G_CALLBACK(on_check_clicked), NULL);
	g_signal_connect(win->startup, "toggled", G_CALLBACK(on_startup_toggled), NULL);
	g_signal_connect(win->versions, "changed", G_CALLBACK(on_versions_changed), NULL);
	g_signal_connect(win->skip, "clicked", G_CALLBACK(on_skip_clicked), NULL);
	g_signal_connect(win->install, "clicked", G_CALLBACK(on_install_clicked), NULL);
	g_signal_connect_swapped(close, "clicked", G_CALLBACK(gtk_widget_destroy), win->window);

	gtk_widget_show_all(win->window);
	gtk_widget_hide(win->skip);
}

void
update_window_show(GtkWindow *parent)
{
	if(win == NULL)
	{
		/* Un contrôle de démarrage encore en cours est annulé : jamais deux git sur le cache à la fois */
		if(startup_cancellable != NULL)
			g_cancellable_cancel(startup_cancellable);
		build_window(parent);
		start_check();
	}
	gtk_window_present(GTK_WINDOW(win->window));
}

/* ---- démarrage ------------------------------------------------------ */

static void
on_startup_fetched(GObject *source, GAsyncResult *res, gpointer user_data)
{
	GError *error = NULL;
	GtReleases *r = gt_releases_fetch_finish(res, &error);
	gboolean cancelled = startup_cancellable == NULL || g_cancellable_is_cancelled(startup_cancellable);

	/* Contrôle annulé (fenêtre ouverte à la main, qui actualise elle-même) : résultat simplement libéré */
	if(!cancelled && r != NULL && win == NULL)
	{
		GtVersion installed;
		const gchar *newest = r->tags->len > 0 ? g_ptr_array_index(r->tags, 0) : NULL;

		installed_version(&installed);
		if(gt_release_verdict(&installed, newest, config.update_skipped_version[0] != '\0' ? config.update_skipped_version : NULL) == GT_RELEASE_AVAILABLE)
		{
			build_window(startup_parent);
			set_result_for(r);
			set_releases(r);
			r = NULL;
			gtk_window_present(GTK_WINDOW(win->window));
		}
	}
	if(error != NULL)
		g_debug("update check: %s", error->message);
	g_clear_error(&error);
	gt_releases_free(r);
	g_clear_object(&startup_cancellable);
}

void
update_startup_check(GtkWindow *parent)
{
	gchar *cache;

	if(!config.update_check_startup || startup_cancellable != NULL)
		return;
	startup_parent = parent;
	startup_cancellable = g_cancellable_new();
	cache = gt_releases_cache_dir();
	gt_releases_fetch_async(config.update_url, cache, startup_cancellable, on_startup_fetched, NULL);
	g_free(cache);
}
