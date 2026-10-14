/* Application GTK 4 : identifiant, actions app.* et raccourcis, ouverture du port, fermeture unique (spec §4.4).
 *
 * Démarrage : main() (main.c) installe les crochets GtAppUi, crée le gestionnaire de sessions et la première session (liaison, options,
 * macros, moteur Lua), lit la ligne de commande dans cette session (profil chargé et validé ; messages mis en file) puis lance l'application.
 * « startup » : actions d'application (dont new-tab, close-tab, next-tab, previous-tab, active-tab), raccourcis (y compris ceux des actions
 * d'onglet), crochet de test.
 * « activate » : fenêtre principale (barre de menus + GtkNotebook), page de la session active (barres, panneaux de macros et de script,
 * raccourcis de macros, actions de page dans le groupe de la session) et son étiquette, relais app.* des actions d'onglet vers la session active,
 * profil appliqué, titre lié à status-text de la session active, fenêtre présentée, messages en attente affichés, port ouvert, SIGUSR1/SIGUSR2.
 * Onglets (6b) : le GtkNotebook est maître de l'ordre et de la page affichée (plan, décision 5) ; switch-page → session active du gestionnaire,
 * page-reordered → gt_session_manager_move ; notify::active → relais reciblés, titre, état de app.active-tab, focus au terminal de la page.
 * Nouvel onglet : gt_app_add_session ; fermeture d'un onglet : gt_app_close_session (confirmation s'il est occupé).
 * Fermeture (une seule) : app.quit → gtk_window_close(fenêtre principale) → « close-request » : état de fenêtre et panneau de script de la
 * page active enregistrés, chaque page arrêtée, file des messages close, arrêt des services (puis chaque session : moteur Lua, macros, liaison) ; GTK détruit la fenêtre,
 * l'application n'a plus de fenêtre et s'arrête. La croix de la barre de titre suit le même chemin. « shutdown » : arrêt des services s'il n'a pas eu lieu, libérations. */
#include <config.h>
#include <glib/gi18n.h>
#include <glib-unix.h>
#include <signal.h>
#include <unistd.h>

#include "about.h"
#include "action_relay.h"
#include "app.h"
#include "app_actions.h"
#include "app_config.h"
#include "config_store.h"
#include "dialogs.h"
#include "macro_editor.h"
#include "macros.h"
#include "main_window.h"
#include "port_dialog.h"
#include "profile_dialogs.h"
#include "terminal_dialog.h"
#include "script_engine.h"
#include "script_panel.h"
#include "session_view.h"
#include "tab_label.h"
#include "terminal_view.h"
#include "update_window.h"
#ifdef GTKTERM_WITH_TEST_HOOKS
#include "test_hooks.h"
#endif

/* État de l'application (statiques permises, contraintes globales) : gestionnaire de sessions, fenêtre, sources SIGUSR */
typedef struct
{
	const GtCmdline *cmd;		/* -f : fichier proposé au premier envoi de fichier */
	gboolean send_file_taken;	/* -f déjà rendu à la première page */
	GtSessionManager *sessions;	/* référence, rendue à la finalisation de l'application */
	GtkWidget *main_window;		/* pointeur faible */
	GtkWidget *notebook;		/* GtkNotebook « tabs » de la fenêtre (pointeur faible) */
	gulong switch_id, reorder_id;	/* switch-page, page-reordered du notebook */
	GSimpleAction *close_tab_action, *active_tab_action;	/* tenues par l'application (actions app.close-tab, app.active-tab) */
	GtkWidget *focus_view;		/* page dont le terminal prend le focus à la prochaine inactivité (pointeur faible) */
	guint focus_id;
	guint sigusr1_id, sigusr2_id;
	GPtrArray *relays;		/* GtActionRelay des actions d'onglet (app.*), ciblant la session active */
	gulong active_id;		/* notify::active du gestionnaire */
	GBinding *title_binding;	/* titre de la fenêtre ← status-text de la session active (pointeur faible) */
	gboolean services_stopped;
} GtApp;

/* Actions d'onglet relayées en app.* (plan, décision 4) : colonnes « session » et « page » du tableau « Actions » */
static const gchar *const tab_action_names[] =
{
	/* session */
	"local-echo", "crlf-auto", "timestamp", "esc-clear-screen", "hex-index", "view-mode", "hex-bytes", "auto-reconnect",
	"clear-screen", "open-port", "close-port", "send-break", "toggle-dtr", "toggle-rts",
	/* page */
	"clear-scrollback", "copy", "paste", "select-all", "find", "show-hex-bar", "show-macro-panel", "show-script-panel", "script-layout",
	"send-file", "save-raw", "save-ascii", "log-start", "log-paused", "log-stop", "log-clear",
};

static GtApp gt_app;

GtSessionManager *gt_app_get_sessions(void)
{
	return gt_app.sessions;
}

GtSession *gt_app_get_active_session(void)
{
	return gt_app.sessions != NULL ? gt_session_manager_get_active(gt_app.sessions) : NULL;
}

GtkWindow *gt_app_get_main_window(void)
{
	return gt_app.main_window != NULL ? GTK_WINDOW(gt_app.main_window) : NULL;
}

GtkWidget *gt_app_get_active_view(void)
{
	return gt_app.main_window != NULL ? gt_main_window_get_active_view(gt_app.main_window) : NULL;
}

void gt_app_foreach_view(GFunc func, gpointer user_data)
{
	GtkNotebook *nb;

	if(gt_app.main_window == NULL)
		return;
	nb = gt_main_window_get_notebook(gt_app.main_window);
	for(gint i = 0; i < gtk_notebook_get_n_pages(nb); i++)
	{
		GtkWidget *view = gtk_notebook_get_nth_page(nb, i);

		if(!gt_session_view_is_shut_down(view))
			func(view, user_data);
	}
}

const gchar *gt_app_take_initial_send_file(void)
{
	if(gt_app.send_file_taken || gt_app.cmd == NULL)
		return NULL;
	gt_app.send_file_taken = TRUE;
	return gt_app.cmd->send_file;
}

static void view_shutdown(gpointer view, gpointer user_data)
{
	gt_session_view_shutdown(view);
}

/* ---------- Onglets : état des actions, focus ---------- */

/* close-tab (et boutons de fermeture des étiquettes) sensibles à partir de deux onglets ; état de app.active-tab = index de la page affichée */
static void update_tab_state(void)
{
	GtkNotebook *nb = gt_app.notebook != NULL ? GTK_NOTEBOOK(gt_app.notebook) : NULL;
	gint n = nb != NULL ? gtk_notebook_get_n_pages(nb) : 0;
	gint cur = nb != NULL ? gtk_notebook_get_current_page(nb) : -1;

	if(gt_app.close_tab_action != NULL)
		g_simple_action_set_enabled(gt_app.close_tab_action, n > 1 && !gt_app.services_stopped);
	for(gint i = 0; i < n; i++)
		gt_tab_label_set_closable(gtk_notebook_get_tab_label(nb, gtk_notebook_get_nth_page(nb, i)), n > 1);
	if(gt_app.active_tab_action != NULL && cur >= 0)
	{
		GVariant *st = g_action_get_state(G_ACTION(gt_app.active_tab_action));

		if(g_variant_get_int32(st) != cur)
			g_simple_action_set_state(gt_app.active_tab_action, g_variant_new_int32(cur));
		g_variant_unref(st);
	}
}

static void focus_view_set(GtkWidget *view)
{
	if(gt_app.focus_view != NULL)
		g_object_remove_weak_pointer(G_OBJECT(gt_app.focus_view), (gpointer *)&gt_app.focus_view);
	gt_app.focus_view = view;
	if(view != NULL)
		g_object_add_weak_pointer(G_OBJECT(view), (gpointer *)&gt_app.focus_view);
}

static gboolean on_focus_idle(gpointer user_data)
{
	gt_app.focus_id = 0;
	if(gt_app.focus_view != NULL && !gt_session_view_is_shut_down(gt_app.focus_view) && !gt_app.services_stopped)
		gt_session_view_focus_terminal(gt_app.focus_view);
	focus_view_set(NULL);
	return G_SOURCE_REMOVE;
}

/* Focus clavier au terminal de view, différé : un clic sur l'étiquette d'un onglet donne d'abord le focus au notebook (après switch-page), et
 * les raccourcis de macros (portée locale de la page) ne verraient pas les touches */
static void focus_terminal_later(GtkWidget *view)
{
	focus_view_set(view);
	if(gt_app.focus_id == 0)
		gt_app.focus_id = g_idle_add(on_focus_idle, NULL);
}

/* ---------- Titre de la fenêtre ---------- */

static gboolean title_from_status(GBinding *binding, const GValue *from, GValue *to, gpointer user_data)
{
	const gchar *status = g_value_get_string(from);

	g_value_take_string(to, g_strdup_printf("GTKTerm - %s", status != NULL ? status : ""));
	return TRUE;
}

static void title_unbind(void)
{
	if(gt_app.title_binding == NULL)
		return;
	g_object_remove_weak_pointer(G_OBJECT(gt_app.title_binding), (gpointer *)&gt_app.title_binding);
	g_binding_unbind(gt_app.title_binding);
	gt_app.title_binding = NULL;
}

/* Titre « GTKTerm - <texte d'état> » de la session s (NULL : plus de liaison) */
static void title_bind(GtSession *s)
{
	title_unbind();
	if(s == NULL || gt_app.main_window == NULL)
		return;
	gt_app.title_binding = g_object_bind_property_full(s, "status-text", gt_app.main_window, "title", G_BINDING_SYNC_CREATE, title_from_status, NULL,
	                                                   NULL, NULL);
	g_object_add_weak_pointer(G_OBJECT(gt_app.title_binding), (gpointer *)&gt_app.title_binding);
}

/* ---------- Relais des actions d'onglet ---------- */

/* Chaque relais vise le groupe d'actions de s (NULL : aucune cible, relais désactivés) */
static void gt_app_retarget_relays(GtSession *s)
{
	GActionGroup *target = s != NULL ? gt_session_get_actions(s) : NULL;

	for(guint i = 0; gt_app.relays != NULL && i < gt_app.relays->len; i++)
		gt_action_relay_set_target(g_ptr_array_index(gt_app.relays, i), target);
}

static void on_active_session(GObject *sessions, GParamSpec *pspec, gpointer user_data)
{
	GtSession *s = gt_session_manager_get_active(GT_SESSION_MANAGER(sessions));
	GtkWidget *view;

	gt_app_retarget_relays(s);
	if(gt_app.services_stopped)
		return;
	title_bind(s);
	update_tab_state();
	view = gt_session_view_lookup(s);
	if(view != NULL)
		focus_terminal_later(view);
}

/* activate, après la page de s (actions de page comprises) : un relais app.<nom> par action d'onglet */
static void install_relays(GApplication *app, GtSession *s)
{
	gt_app.relays = g_ptr_array_new_with_free_func(g_object_unref);
	for(gsize i = 0; i < G_N_ELEMENTS(tab_action_names); i++)
	{
		GtActionRelay *relay = gt_action_relay_new(gt_session_get_actions(s), tab_action_names[i]);

		g_assert(relay != NULL);	/* action d'onglet absente du groupe : module oublié */
		g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(relay));
		g_ptr_array_add(gt_app.relays, relay);
	}
	gt_app.active_id = g_signal_connect(gt_app.sessions, "notify::active", G_CALLBACK(on_active_session), NULL);
}

/* Arrêt : relais retirés de l'application puis sans cible (plus aucune référence vers une session) */
static void remove_relays(void)
{
	GApplication *app = g_application_get_default();

	if(gt_app.active_id != 0)
		g_signal_handler_disconnect(gt_app.sessions, gt_app.active_id);
	gt_app.active_id = 0;
	if(gt_app.relays == NULL)
		return;
	for(guint i = 0; i < gt_app.relays->len; i++)
	{
		GtActionRelay *relay = g_ptr_array_index(gt_app.relays, i);

		if(app != NULL)
			g_action_map_remove_action(G_ACTION_MAP(app), g_action_get_name(G_ACTION(relay)));
		gt_action_relay_set_target(relay, NULL);
	}
	g_clear_pointer(&gt_app.relays, g_ptr_array_unref);
}

/* ---------- Liaison ---------- */

/* Émis seulement dans gt_link_open ou depuis une source d'inactivité (link.h) : une boîte non bloquante est permise. user_data : la session
 * (handler retiré à son arrêt). Message préfixé du nom court de l'onglet s'il y en a plusieurs. */
static void on_open_failed(GtkTermLink *link, const gchar *message, gpointer user_data)
{
	GtSession *s = user_data;

	if(gt_app.sessions != NULL && gt_session_manager_get_n(gt_app.sessions) > 1)
	{
		gchar *text = g_strdup_printf("%s: %s", gt_session_get_short_name(s), message);

		gt_ui_message(gt_app_get_main_window(), GT_APP_MSG_ERROR, text);
		g_free(text);
	}
	else
		gt_ui_message(gt_app_get_main_window(), GT_APP_MSG_ERROR, message);
}

static void session_watch(GtSession *s)
{
	g_signal_connect(gt_session_get_link(s), "open-failed", G_CALLBACK(on_open_failed), s);
}

static void session_unwatch(GtSession *s)
{
	g_signal_handlers_disconnect_by_func(gt_session_get_link(s), on_open_failed, s);
}

/* SIGUSR1 / SIGUSR2 : port de chaque session ouvert / fermé (spec §3.2-15) */
static gboolean on_sigusr1(gpointer user_data)
{
	for(guint i = 0; gt_app.sessions != NULL && i < gt_session_manager_get_n(gt_app.sessions); i++)
		gt_session_open_port(gt_session_manager_get_nth(gt_app.sessions, i));
	return G_SOURCE_CONTINUE;
}

static gboolean on_sigusr2(gpointer user_data)
{
	for(guint i = 0; gt_app.sessions != NULL && i < gt_session_manager_get_n(gt_app.sessions); i++)
		gt_session_close_port(gt_session_manager_get_nth(gt_app.sessions, i));
	return G_SOURCE_CONTINUE;
}

/* ---------- Notebook ---------- */

/* Page affichée changée (clic, raccourci, app.active-tab, retrait de la page affichée) : sa session devient active */
static void on_switch_page(GtkNotebook *nb, GtkWidget *page, guint num, gpointer user_data)
{
	GtSession *s = gt_session_view_get_session(page);

	if(gt_app.services_stopped || gt_session_view_is_shut_down(page) || s == NULL)
		return;
	gt_session_manager_set_active(gt_app.sessions, s);	/* notify::active si elle change */
	update_tab_state();
	focus_terminal_later(page);
}

static void on_page_reordered(GtkNotebook *nb, GtkWidget *page, guint num, gpointer user_data)
{
	GtSession *s = gt_session_view_get_session(page);

	if(gt_app.services_stopped || s == NULL)
		return;
	gt_session_manager_move(gt_app.sessions, s, num);
	update_tab_state();
}

static void notebook_disconnect(void)
{
	if(gt_app.notebook != NULL)
	{
		if(gt_app.switch_id != 0)
			g_signal_handler_disconnect(gt_app.notebook, gt_app.switch_id);
		if(gt_app.reorder_id != 0)
			g_signal_handler_disconnect(gt_app.notebook, gt_app.reorder_id);
	}
	gt_app.switch_id = gt_app.reorder_id = 0;
}

/* ---------- Ajout et fermeture d'un onglet ---------- */

/* Page et étiquette de s insérées à position dans le notebook ; open-failed suivi */
static GtkWidget *session_page_new(GtSession *s, const GtWindowState *st, GtkWidget *layout_from, gint position)
{
	GtkWidget *view = gt_session_view_new(GTK_APPLICATION(g_application_get_default()), s, st, layout_from);

	session_watch(s);
	gt_main_window_insert_view(gt_app.main_window, view, gt_tab_label_new(s), position);
	return view;
}

GtSession *gt_app_add_session(const GtProfile *profile, const gchar *profile_name)
{
	GtkWidget *active, *view;
	GtWindowState st;
	GtSession *s, *from;
	const gchar *macros_path;
	gint pos;

	if(gt_app.main_window == NULL || gt_app.services_stopped || gt_app.sessions == NULL)
		return NULL;
	/* Fichier de macros : celui du profil ; sans fichier dans le profil, celui de l'onglet actif (le « fichier en cours » que load_profile
	 * relirait), et aucun si l'onglet actif n'en a pas chargé : jamais le fichier par défaut d'office, le nouvel onglet suit le premier */
	from = gt_app_get_active_session();	/* au OK du dialogue Port (modal) : c'est l'onglet qui l'a prérempli */
	macros_path = profile->macros_file != NULL && profile->macros_file[0] != '\0' ? profile->macros_file
	              : from != NULL ? gt_macros_get_path(gt_session_get_macros(from)) : NULL;
	s = gt_session_new(profile, profile_name);
	if(macros_path != NULL && macros_path[0] != '\0')
		gt_macros_load_file(gt_session_get_macros(s), macros_path, FALSE);	/* non interactif : un échec s'affiche en message */
	/* Séparateurs et visibilités de la page active ; disposition de son panneau de script */
	active = gt_app_get_active_view();
	app_config_load_window_state(&st);
	if(active != NULL)
		gt_session_view_get_state(active, &st);
	pos = active != NULL ? gtk_notebook_page_num(GTK_NOTEBOOK(gt_app.notebook), active) + 1 : -1;
	gt_session_manager_add(gt_app.sessions, s, pos);	/* avant la page : switch-page la trouve dans le gestionnaire */
	view = session_page_new(s, &st, active, pos);
	gtk_notebook_set_current_page(GTK_NOTEBOOK(gt_app.notebook), gtk_notebook_page_num(GTK_NOTEBOOK(gt_app.notebook), view));	/* active */
	update_tab_state();
	gt_session_apply_profile(s);
	gt_session_view_apply_profile(view);
	gt_session_open_port(s);	/* échec : open-failed, boîte non bloquante */
	focus_terminal_later(view);
	g_object_unref(s);	/* tenue par le gestionnaire et par la page */
	return s;
}

/* Arrêt et retrait de l'onglet de s (plan, décision 3 : page, puis session) */
static void close_session_now(GtSession *s)
{
	GtkWidget *view = gt_session_view_lookup(s);

	g_object_ref(s);
	session_unwatch(s);
	if(view != NULL)
		gt_session_view_shutdown(view);	/* étape a, explicite (le filet « destroy » de la page reste un secours) */
	gt_session_shutdown(s);	/* étape b : moteur Lua, macros, liaison, signal « closed » (dialogues liés fermés) */
	if(view != NULL)
		gt_main_window_remove_view(gt_app.main_window, view);	/* page affichée retirée : switch-page rend la voisine active */
	gt_session_manager_remove(gt_app.sessions, s);
	update_tab_state();
	g_object_unref(s);
}

static void on_close_confirmed(gboolean accepted, gpointer user_data)
{
	GtSession *s = g_weak_ref_get(user_data);

	g_weak_ref_clear(user_data);
	g_free(user_data);
	/* Session fermée entre-temps, application arrêtée, ou dernier onglet : rien */
	if(accepted && s != NULL && !gt_session_is_shut_down(s) && !gt_app.services_stopped && gt_app.main_window != NULL && gt_app.sessions != NULL
	   && gt_session_manager_index_of(gt_app.sessions, s) >= 0 && gt_session_manager_get_n(gt_app.sessions) > 1)
		close_session_now(s);
	g_clear_object(&s);
}

void gt_app_close_session(GtSession *s)
{
	GWeakRef *ref;

	if(s == NULL || gt_session_is_shut_down(s) || gt_app.services_stopped || gt_app.main_window == NULL || gt_app.sessions == NULL)
		return;
	if(gt_session_manager_index_of(gt_app.sessions, s) < 0 || gt_session_manager_get_n(gt_app.sessions) <= 1)
		return;
	if(!gt_session_is_busy(s))
	{
		close_session_now(s);
		return;
	}
	ref = g_new0(GWeakRef, 1);
	g_weak_ref_init(ref, s);
	gt_ui_confirm(gt_app_get_main_window(), _("Close this tab?"), _("A script is running or a file is being sent in this tab."), _("Close tab"),
	              on_close_confirmed, ref);
}

/* Arrêt dans l'ordre de la spec (§4.4, plan 6b décision 3 : interface d'abord, puis les sessions) ; une seule fois (close-request,
 * sinon shutdown) */
static void app_stop_services(void)
{
	if(gt_app.services_stopped)
		return;
	gt_app.services_stopped = TRUE;
	gt_app_foreach_view(view_shutdown, NULL);	/* déjà fait par close-request, sauf arrêt sans fermeture de la fenêtre */
	title_unbind();
	gt_update_shutdown();
	g_clear_handle_id(&gt_app.sigusr1_id, g_source_remove);
	g_clear_handle_id(&gt_app.sigusr2_id, g_source_remove);
	g_clear_handle_id(&gt_app.focus_id, g_source_remove);
	focus_view_set(NULL);
	notebook_disconnect();	/* la destruction des pages ne touche plus le gestionnaire */
	if(gt_app.close_tab_action != NULL)
		g_simple_action_set_enabled(gt_app.close_tab_action, FALSE);
	for(guint i = 0; gt_app.sessions != NULL && i < gt_session_manager_get_n(gt_app.sessions); i++)
		session_unwatch(gt_session_manager_get_nth(gt_app.sessions, i));
	remove_relays();
	/* Arrêt demandé à tous les moteurs Lua d'abord (non bloquant) : chacun s'arrête pendant que gt_session_shutdown attend le précédent,
	 * au lieu d'arrêts l'un après l'autre (seul un script bloqué plus de 3 s dans un appel C coûte encore ses 3 s) */
	for(guint i = 0; gt_app.sessions != NULL && i < gt_session_manager_get_n(gt_app.sessions); i++)
		gt_script_engine_stop(gt_session_get_script_engine(gt_session_manager_get_nth(gt_app.sessions, i)));
	/* Puis chaque session : moteur Lua, macros (ordonnanceur), actions, liaison fermée, signal « closed » */
	for(guint i = 0; gt_app.sessions != NULL && i < gt_session_manager_get_n(gt_app.sessions); i++)
		gt_session_shutdown(gt_session_manager_get_nth(gt_app.sessions, i));
}

/* ---------- Fenêtre principale ---------- */

static gboolean on_main_close_request(GtkWindow *win, gpointer user_data)
{
	GtkWidget *active = gt_app_get_active_view();
	GtWindowState st;

	gt_main_window_get_state(GTK_WIDGET(win), &st);
	config_reload_store();	/* relu ici : app_config_save_window_state ne relira plus (fichier inchangé) et garde ces groupes */
	if(active != NULL)	/* [colors], [layout], [session] du panneau de script de la page active, sans écriture */
		gt_script_panel_store_state(gt_session_view_get_slot(active, "script-panel"), gt_config_store_get_key_file(config_get_store()));
	app_config_save_window_state(&st);	/* relit, écrit [window], retire x et y, enregistre : seule écriture de la fermeture */
	/* Actions de page retirées du groupe de chaque session après l'enregistrement : les GPropertyAction gardent barres et panneaux en vie
	 * (gt_session_view_shutdown, page par page, avant l'arrêt des sessions) */
	gt_app_foreach_view(view_shutdown, NULL);
	title_unbind();	/* la fermeture des liaisons ne touche plus la fenêtre */
	gt_ui_messages_shutdown();
	app_stop_services();
	return FALSE;
}

static void on_activate(GApplication *app, gpointer user_data)
{
	GtSession *session = gt_app_get_active_session();
	GtWindowState st;
	GtkWidget *view;
	GtkCssProvider *css;

	if(gt_app.main_window != NULL)
	{
		gtk_window_present(GTK_WINDOW(gt_app.main_window));
		return;
	}
	gt_app.main_window = gt_main_window_new(GTK_APPLICATION(app));
	g_object_add_weak_pointer(G_OBJECT(gt_app.main_window), (gpointer *)&gt_app.main_window);
	g_signal_connect(gt_app.main_window, "close-request", G_CALLBACK(on_main_close_request), NULL);
	gt_app.notebook = GTK_WIDGET(gt_main_window_get_notebook(gt_app.main_window));
	g_object_add_weak_pointer(G_OBJECT(gt_app.notebook), (gpointer *)&gt_app.notebook);
	/* Indicateurs d'activité des ports (activity_icon.c) ; onglet muet grisé, onglet non lu en gras. Couleurs fixes : @accent_color n'existe
	 * pas dans tous les thèmes */
	css = gtk_css_provider_new();
	gtk_css_provider_load_from_string(css,
		".activity-dot { min-width: 8px; min-height: 8px; border-radius: 9999px; border: 1px solid alpha(currentColor, 0.4); }\n"
		".activity-dot.receiving { background-color: #2ec27e; border-color: #2ec27e; }\n"
		".activity-dot.lost { background-color: #e01b24; border-color: #e01b24; }\n"
		".activity-spinner.connecting { color: #3584e4; }\n"
		".activity-spinner.reconnecting { color: #e01b24; }\n"
		".activity-zzz { color: #3584e4; font-weight: 800; }\n"
		".activity-zzz .z1 { font-size: 10px; }\n"
		".activity-zzz .z2 { font-size: 8px; }\n"
		".activity-zzz .z3 { font-size: 6px; }\n"
		".dead #tab-name { opacity: 0.55; }\n"
		".unread #tab-name { font-weight: bold; }\n");
	gtk_style_context_add_provider_for_display(gtk_widget_get_display(gt_app.main_window), GTK_STYLE_PROVIDER(css),
	                                           GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(css);
	/* Tête de mort symbolique (recolorée) : icons/scalable/actions/ de la ressource */
	gtk_icon_theme_add_resource_path(gtk_icon_theme_get_for_display(gtk_widget_get_display(gt_app.main_window)),
	                                 "/eu/mulagabriel/gtkterm/ui/icons");

	/* Page de la session : terminal, barres, panneaux, raccourcis de macros, actions de page ; séparateurs et visibilités de [window],
	 * disposition du panneau de script de [layout] et [session] (première page) */
	app_config_load_window_state(&st);
	view = session_page_new(session, &st, NULL, -1);	/* étiquette invisible avec un seul onglet ; open-failed suivi */
	/* Après le gestionnaire par défaut : la page courante du notebook est déjà la nouvelle (index de app.active-tab) */
	gt_app.switch_id = g_signal_connect_after(gt_app.notebook, "switch-page", G_CALLBACK(on_switch_page), NULL);
	gt_app.reorder_id = g_signal_connect(gt_app.notebook, "page-reordered", G_CALLBACK(on_page_reordered), NULL);

	install_relays(app, session);	/* toutes les actions d'onglet existent : relais app.* vers la session active */
	title_bind(session);
	update_tab_state();

	gt_session_apply_profile(session);	/* un seul « changed » des options : un seul redessin du terminal */
	gt_session_view_apply_profile(view);
	/* La frappe part sur le port dès l'ouverture */
	gtk_window_set_focus(GTK_WINDOW(gt_app.main_window), GTK_WIDGET(gt_terminal_view_get_vte(gt_session_view_get_terminal_view(view))));
	gtk_window_present(GTK_WINDOW(gt_app.main_window));
	gt_ui_main_window_ready(GTK_WINDOW(gt_app.main_window));	/* messages mis en file avant la fenêtre : affichés maintenant, dans l'ordre */
	gt_session_open_port(session);
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

/* New tab : dialogue Port en mode création, prérempli d'une copie du profil de l'onglet actif (plan, décision 7) */
static void on_new_tab_profile(const GtProfile *profile, gpointer user_data)
{
	GtSession *active = gt_app_get_active_session();	/* nom de profil pris au OK : le dialogue Port est modal, l'onglet actif est celui qui l'a prérempli */

	gt_app_add_session(profile, active != NULL ? gt_session_get_profile_name(active) : NULL);
}

static void action_new_tab(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	GtSession *active = gt_app_get_active_session();

	if(gt_app.main_window == NULL || active == NULL || gt_app.services_stopped)
		return;
	gt_port_dialog_show_new(gt_app_get_main_window(), gt_session_get_profile(active), on_new_tab_profile, NULL);
}

static void action_close_tab(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	gt_app_close_session(gt_app_get_active_session());
}

/* next-tab / previous-tab : circulaires ; sans effet avec un seul onglet */
static void switch_tab_by(gint delta)
{
	GtkNotebook *nb = gt_app.notebook != NULL ? GTK_NOTEBOOK(gt_app.notebook) : NULL;
	gint n = nb != NULL ? gtk_notebook_get_n_pages(nb) : 0;

	if(n <= 1 || gt_app.services_stopped)
		return;
	gtk_notebook_set_current_page(nb, (gtk_notebook_get_current_page(nb) + delta + n) % n);
}

static void action_next_tab(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	switch_tab_by(1);
}

static void action_previous_tab(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	switch_tab_by(-1);
}

/* app.active-tab (plan, décision 8) : SetState(i) affiche l'onglet i ; hors bornes : ignoré. L'état suit la page affichée (update_tab_state). */
static void action_active_tab_change(GSimpleAction *action, GVariant *value, gpointer user_data)
{
	gint i = g_variant_get_int32(value);

	if(gt_app.notebook == NULL || gt_app.services_stopped || i < 0 || i >= gtk_notebook_get_n_pages(GTK_NOTEBOOK(gt_app.notebook)))
		return;
	gtk_notebook_set_current_page(GTK_NOTEBOOK(gt_app.notebook), i);
}

static void action_active_tab(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	g_action_change_state(G_ACTION(action), param);
}

static const GActionEntry app_entries[] =
{
	{ .name = "quit", .activate = action_quit },
	{ .name = "about", .activate = action_about },
	{ .name = "updates", .activate = action_updates },
	{ .name = "new-tab", .activate = action_new_tab },
	{ .name = "close-tab", .activate = action_close_tab },
	{ .name = "next-tab", .activate = action_next_tab },
	{ .name = "previous-tab", .activate = action_previous_tab },
	{ .name = "active-tab", .activate = action_active_tab, .parameter_type = "i", .state = "0", .change_state = action_active_tab_change },
};

static const struct
{
	const gchar *action;
	const gchar *accel;
} app_accels[] =
{
	{ "app.quit", "<Control><Shift>q" },
	{ "app.new-tab", "<Control><Shift>t" },
	{ "app.close-tab", "<Control><Shift>w" },
	{ "app.next-tab", "<Control><Shift>Page_Down" },
	{ "app.previous-tab", "<Control><Shift>Page_Up" },
};

static void on_startup(GApplication *app, gpointer user_data)
{
	gt_app_actions_set_accels(GTK_APPLICATION(app));	/* actions d'onglet relayées (créées dans activate) */
	gt_profile_dialogs_install(GTK_APPLICATION(app));
	gt_port_dialog_install(GTK_APPLICATION(app));
	gt_terminal_dialog_install(GTK_APPLICATION(app));
	gt_macro_editor_install_action(GTK_APPLICATION(app));
	g_action_map_add_action_entries(G_ACTION_MAP(app), app_entries, G_N_ELEMENTS(app_entries), app);
	gt_app.close_tab_action = G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(app), "close-tab"));
	gt_app.active_tab_action = G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(app), "active-tab"));
	g_simple_action_set_enabled(gt_app.close_tab_action, FALSE);	/* un seul onglet */
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
}

/* Application finalisée (main l'a libérée) : plus de session active */
static void on_app_finalized(gpointer user_data, GObject *where_the_object_was)
{
	gt_app.close_tab_action = gt_app.active_tab_action = NULL;	/* tenues par l'application */
	g_clear_object(&gt_app.sessions);
}

GtkApplication *gt_app_new(const GtCmdline *cmd, GtSessionManager *sessions)
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
	g_set_object(&gt_app.sessions, sessions);
	g_object_weak_ref(G_OBJECT(app), on_app_finalized, NULL);
	g_signal_connect(app, "startup", G_CALLBACK(on_startup), NULL);
	g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
	g_signal_connect(app, "shutdown", G_CALLBACK(on_shutdown), NULL);
	return app;
}
