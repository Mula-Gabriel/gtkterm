/* Application GTK 4 : identifiant, actions app.* et raccourcis, ouverture du port, fermeture unique (spec §4.4).
 *
 * Démarrage : main() (main.c) installe les crochets GtAppUi, crée la liaison par défaut, macros_init(), lit la ligne de commande
 * (profil chargé et validé ; messages mis en file) puis lance l'application.
 * « startup » : GtTermOptions, actions app.*, raccourcis, crochet de test.
 * « activate » : fenêtre principale, profil appliqué, titre lié à status-text, fenêtre présentée, messages en attente affichés,
 * port ouvert, SIGUSR1/SIGUSR2.
 * Fermeture (une seule) : app.quit → gtk_window_close(fenêtre principale) → « close-request » : état de fenêtre enregistré, file des
 * messages close, arrêt des services (moteur Lua, macros, liaison) ; GTK détruit la fenêtre, l'application n'a plus de fenêtre et
 * s'arrête. La croix de la barre de titre suit le même chemin. « shutdown » : arrêt des services s'il n'a pas eu lieu, libérations. */
#include <config.h>
#include <glib/gi18n.h>
#include <glib-unix.h>
#include <signal.h>
#include <unistd.h>

#include "about.h"
#include "app.h"
#include "app_actions.h"
#include "app_config.h"
#include "config_store.h"
#include "dialogs.h"
#include "file_actions.h"
#include "macro_editor.h"
#include "macro_panel.h"
#include "macro_shortcuts.h"
#include "macros.h"
#include "hex_send_bar.h"
#include "main_window.h"
#include "port_dialog.h"
#include "profile_dialogs.h"
#include "search_bar.h"
#include "status_bar.h"
#include "terminal_dialog.h"
#include "script_engine.h"
#include "script_panel.h"
#include "terminal_view.h"
#include "update_window.h"
#ifdef GTKTERM_WITH_TEST_HOOKS
#include "test_hooks.h"
#endif

typedef struct
{
	const GtCmdline *cmd;		/* -f : fichier proposé au premier envoi de fichier */
	GtTermOptions *options;
	GtkWidget *main_window;		/* pointeur faible */
	gulong open_failed_id;
	guint sigusr1_id, sigusr2_id;
	gboolean services_stopped;
} GtApp;

static GtApp gt_app;

GtkTermLink *gt_app_get_link(void)
{
	return gt_link_get_default();
}

GtTermOptions *gt_app_get_options(void)
{
	return gt_app.options;
}

GtkWindow *gt_app_get_main_window(void)
{
	return gt_app.main_window != NULL ? GTK_WINDOW(gt_app.main_window) : NULL;
}

void gt_app_open_port(void)
{
	TransportParams params;

	gt_profile_to_transport_params(&config, &params);
	gt_link_open(gt_link_get_default(), &params, NULL);	/* échec : open-failed (on_open_failed), une boîte non bloquante */
	transport_params_clear(&params);
}

void gt_app_close_port(void)
{
	gt_link_close(gt_link_get_default());
}

void gt_app_apply_profile(void)
{
	if(gt_app.options != NULL)
		gt_term_options_apply_profile(gt_app.options, &config);	/* un seul « changed » : un seul redessin du terminal */
	g_object_set(gt_link_get_default(), "auto-reconnect", config.auto_reconnect, NULL);
	if(gt_app.main_window != NULL)
		gt_terminal_view_apply_profile(gt_main_window_get_terminal_view(gt_app.main_window), &config);
	gt_script_panel_refresh_colors();
}

/* ---------- Liaison ---------- */

/* Émis seulement dans gt_link_open ou depuis une source d'inactivité (link.h) : une boîte non bloquante est permise */
static void on_open_failed(GtkTermLink *link, const gchar *message, gpointer user_data)
{
	gt_ui_message(gt_app_get_main_window(), GT_APP_MSG_ERROR, message);
}

static gboolean on_sigusr1(gpointer user_data)
{
	gt_app_open_port();
	return G_SOURCE_CONTINUE;
}

static gboolean on_sigusr2(gpointer user_data)
{
	gt_app_close_port();
	return G_SOURCE_CONTINUE;
}

/* Arrêt dans l'ordre de la spec (§4.4) ; une seule fois (close-request, sinon shutdown) */
static void app_stop_services(void)
{
	GtkTermLink *link = gt_link_get_default();

	if(gt_app.services_stopped)
		return;
	gt_app.services_stopped = TRUE;
	gt_file_actions_shutdown();
	gt_update_shutdown();
	g_clear_handle_id(&gt_app.sigusr1_id, g_source_remove);
	g_clear_handle_id(&gt_app.sigusr2_id, g_source_remove);
	if(gt_app.open_failed_id != 0)
		g_signal_handler_disconnect(link, gt_app.open_failed_id);
	gt_app.open_failed_id = 0;
	script_engine_shutdown();
	gt_macro_panel_shutdown();
	gt_macro_shortcuts_detach();
	macros_shutdown();
	gt_link_close(link);
}

/* ---------- Fenêtre principale ---------- */

static gboolean on_main_close_request(GtkWindow *win, gpointer user_data)
{
	GtWindowState st;

	gt_main_window_get_state(GTK_WIDGET(win), &st);
	config_reload_store();	/* relu ici : app_config_save_window_state ne relira plus (fichier inchangé) et garde ces groupes */
	gt_script_panel_store_state(gt_config_store_get_key_file(config_get_store()));	/* [colors], [layout], [session], sans écriture */
	app_config_save_window_state(&st);	/* relit, écrit [window], retire x et y, enregistre : seule écriture de la fermeture */
	/* La GPropertyAction garde la barre en vie : retirée après l'enregistrement (show-script-panel aussi, ci-dessous ; show-macro-panel : gt_macro_panel_shutdown) */
	g_action_map_remove_action(G_ACTION_MAP(gtk_window_get_application(win)), "show-hex-bar");
	g_action_map_remove_action(G_ACTION_MAP(g_application_get_default()), "show-script-panel");	/* la GPropertyAction garde le panneau en vie */
	gt_app_actions_detach_terminal();
	gt_status_bar_detach(gt_main_window_get_slot(GTK_WIDGET(win), "status-bar"));	/* la fermeture de la liaison ne touche plus la fenêtre */
	gt_ui_messages_shutdown();
	app_stop_services();
	return FALSE;
}

static void on_activate(GApplication *app, gpointer user_data)
{
	GtkTermLink *link = gt_link_get_default();

	if(gt_app.main_window != NULL)
	{
		gtk_window_present(GTK_WINDOW(gt_app.main_window));
		return;
	}
	gt_app.main_window = gt_main_window_new(GTK_APPLICATION(app));
	g_object_add_weak_pointer(G_OBJECT(gt_app.main_window), (gpointer *)&gt_app.main_window);
	g_signal_connect(gt_app.main_window, "close-request", G_CALLBACK(on_main_close_request), NULL);
	gt_macro_shortcuts_attach(gt_app.main_window);
	gt_macro_panel_install(GTK_APPLICATION(app), gt_app.main_window);
	gt_app_actions_attach_terminal(GTK_APPLICATION(app), gt_main_window_get_terminal_view(gt_app.main_window));
	{
		GtkWidget *win = gt_app.main_window;
		GtkWidget *status = gt_status_bar_new(link, GTK_WINDOW(win));
		GtkWidget *search = gt_search_bar_new(gt_terminal_view_get_vte(gt_main_window_get_terminal_view(win)));
		GtkWidget *hex = gt_hex_send_bar_new(link);

		gt_main_window_set_slot(win, "status-bar", status);
		gt_main_window_set_slot(win, "search-bar", search);
		gt_main_window_set_slot(win, "hex-bar", hex);	/* visibilité lue dans [window] par le slot */
		gt_search_bar_install_action(GTK_APPLICATION(app), search);
		gt_hex_send_bar_install_action(GTK_APPLICATION(app), hex);
	}
	{
		GtkWidget *script_panel = gt_script_panel_new(GTK_APPLICATION(app));
		GPropertyAction *show_script = g_property_action_new("show-script-panel", script_panel, "visible");

		gt_main_window_set_slot(gt_app.main_window, "script-panel", script_panel);	/* visibilité lue dans [window] par le slot */
		g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(show_script));
		g_object_unref(show_script);
	}
	gt_app.open_failed_id = g_signal_connect(link, "open-failed", G_CALLBACK(on_open_failed), NULL);

	gt_app_apply_profile();
	gtk_window_present(GTK_WINDOW(gt_app.main_window));
	gt_ui_main_window_ready(GTK_WINDOW(gt_app.main_window));	/* messages mis en file avant la fenêtre : affichés maintenant, dans l'ordre */
	gt_app_open_port();
	gt_app.sigusr1_id = g_unix_signal_add(SIGUSR1, on_sigusr1, NULL);
	gt_app.sigusr2_id = g_unix_signal_add(SIGUSR2, on_sigusr2, NULL);
	gt_update_startup_check(GTK_WINDOW(gt_app.main_window));	/* différée (g_idle_add), dernière étape */
}

/* ---------- Actions ---------- */

static void action_quit(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	if(gt_app.main_window != NULL)
		gtk_window_close(GTK_WINDOW(gt_app.main_window));	/* même chemin que la croix de la barre de titre */
	else
		g_application_quit(G_APPLICATION(user_data));
}

static void action_about(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	gt_about_show(gt_app_get_main_window());
}

static void action_updates(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	gt_update_window_show(gt_app_get_main_window());
}

static const GActionEntry app_entries[] =
{
	{ .name = "quit", .activate = action_quit },
	{ .name = "about", .activate = action_about },
	{ .name = "updates", .activate = action_updates },
};

static const struct
{
	const gchar *action;
	const gchar *accel;
} app_accels[] =
{
	{ "app.quit", "<Control><Shift>q" },
};

static void on_startup(GApplication *app, gpointer user_data)
{
	gt_app.options = gt_term_options_new();
	gt_app_actions_install(GTK_APPLICATION(app), gt_app_get_link(), gt_app.options);
	gt_file_actions_install(GTK_APPLICATION(app), gt_app_get_link(), gt_app.options, gt_app.cmd != NULL ? gt_app.cmd->send_file : NULL);
	gt_profile_dialogs_install(GTK_APPLICATION(app));
	gt_port_dialog_install(GTK_APPLICATION(app));
	gt_terminal_dialog_install(GTK_APPLICATION(app));
	gt_macro_editor_install_action(GTK_APPLICATION(app));
	g_action_map_add_action_entries(G_ACTION_MAP(app), app_entries, G_N_ELEMENTS(app_entries), app);
	for(gsize i = 0; i < G_N_ELEMENTS(app_accels); i++)
	{
		const gchar *accels[] = { app_accels[i].accel, NULL };

		gtk_application_set_accels_for_action(GTK_APPLICATION(app), app_accels[i].action, accels);
	}
#ifdef GTKTERM_WITH_TEST_HOOKS
	if(gt_test_hooks_enabled())
		gt_test_hooks_install(GTK_APPLICATION(app));
#endif
}

static void on_shutdown(GApplication *app, gpointer user_data)
{
	gt_ui_messages_shutdown();
	app_stop_services();
	gt_app_actions_shutdown();
	g_clear_object(&gt_app.options);
}

GtkApplication *gt_app_new(const GtCmdline *cmd)
{
	gboolean test = FALSE;
	GtkApplication *app;
	gchar *id;

#ifdef GTKTERM_WITH_TEST_HOOKS
	test = gt_test_hooks_enabled();
#endif
	id = test ? g_strdup_printf(GT_APP_ID ".test.p%d", (int)getpid()) : g_strdup(GT_APP_ID);
	app = gtk_application_new(id, test ? G_APPLICATION_DEFAULT_FLAGS : G_APPLICATION_NON_UNIQUE);
	g_free(id);
	/* Chemin de ressources fixe (indépendant de l'identifiant de test) ; rien sous …/gtk/ : pas de menus chargés d'office */
	g_application_set_resource_base_path(G_APPLICATION(app), "/eu/mulagabriel/gtkterm");
	gt_app.cmd = cmd;
	g_signal_connect(app, "startup", G_CALLBACK(on_startup), NULL);
	g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
	g_signal_connect(app, "shutdown", G_CALLBACK(on_shutdown), NULL);
	return app;
}
