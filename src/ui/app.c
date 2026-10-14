/* Application GTK 4 : identifiant, actions app.* et raccourcis, ouverture du port, fermeture unique (spec §4.4).
 *
 * Démarrage : main() (main.c) installe les crochets GtAppUi, crée le gestionnaire de sessions et la première session (liaison, options,
 * macros, moteur Lua), lit la ligne de commande dans cette session (profil chargé et validé ; messages mis en file) puis lance l'application.
 * « startup » : actions d'application (dont new-tab, close-tab, next-tab, previous-tab, active-tab, project-*), raccourcis (y compris ceux des
 * actions d'onglet), crochet de test.
 * « activate » : fenêtre principale (barre de menus + GtkNotebook) ; projet de démarrage (6c, spec §3.2-14 : --project, sinon le dernier projet
 * si la ligne de commande ne donne aucun profil) lu en entier : la première session est alors remplacée par les onglets du projet ; sinon (ou en
 * cas de refus, message mis en file) page de la première session (barres, panneaux de macros et de script, raccourcis de macros, actions de page
 * dans le groupe de la session) et son étiquette, profil appliqué. Puis relais app.* des actions d'onglet vers la session active, titre, fenêtre
 * présentée, messages en attente affichés, port ouvert, SIGUSR1/SIGUSR2.
 * Onglets (6b) : le GtkNotebook est maître de l'ordre et de la page affichée (plan, décision 5) ; switch-page → session active du gestionnaire,
 * page-reordered → gt_session_manager_move ; notify::active → relais reciblés, titre, état de app.active-tab, focus au terminal de la page.
 * Nouvel onglet : gt_app_add_session ; fermeture d'un onglet : gt_app_close_session (confirmation s'il est occupé).
 * Projet (6c, plan, décisions 9 à 11) : un seul GtProject (état de l'application). New, Open et Close project passent par une opération en attente
 * (PendingOp) : confirmation si un onglet est occupé, puis Save / Don't save / Cancel si le projet est modifié, puis tous les onglets remplacés
 * (replace_all_tabs) ; chaque question répond par un rappel, jamais par une boucle imbriquée. Titre : « GTKTerm - <état> » sans projet,
 * « <projet>[ •] - <onglet> » avec.
 * Fermeture (une seule) : app.quit → gtk_window_close(fenêtre principale) → « close-request » : projet ouvert et modifié → fermeture arrêtée et
 * question posée (Don't save ou Save réussi : gtk_window_close de nouveau, confirmée) ; sinon état de fenêtre et panneau de script de la
 * page active enregistrés, chaque page arrêtée, file des messages close, arrêt des services (puis chaque session : moteur Lua, macros, liaison) ; GTK détruit la fenêtre,
 * l'application n'a plus de fenêtre et s'arrête. La croix de la barre de titre suit le même chemin. « shutdown » : arrêt des services s'il n'a pas eu lieu, libérations. */
#include <config.h>
#include <glib/gi18n.h>
#include <glib-unix.h>
#include <signal.h>
#include <string.h>
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
#include "project.h"
#include "terminal_dialog.h"
#include "script_engine.h"
#include "script_panel.h"
#include "session_view.h"
#include "status_bar.h"
#include "tab_label.h"
#include "terminal_view.h"
#include "update_window.h"
#ifdef GTKTERM_WITH_TEST_HOOKS
#include "test_hooks.h"
#endif

/* Opération en attente de réponses (confirmation d'onglet occupé, Save / Don't save / Cancel), exécutée par perform() */
typedef enum { OP_NONE, OP_NEW, OP_OPEN, OP_CLOSE, OP_QUIT } PendingOp;

/* État de l'application (statiques permises, contraintes globales) : gestionnaire de sessions, projet, fenêtre, sources SIGUSR */
typedef struct
{
	const GtCmdline *cmd;		/* -f : fichier proposé au premier envoi de fichier */
	gboolean send_file_taken;	/* -f déjà rendu à la première page */
	GtSessionManager *sessions;	/* référence, rendue à la finalisation de l'application */
	GtkWidget *main_window;		/* pointeur faible */
	GtkWidget *notebook;		/* GtkNotebook « tabs » de la fenêtre (pointeur faible) */
	gulong switch_id, reorder_id;	/* switch-page, page-reordered du notebook */
	GSimpleAction *close_tab_action, *active_tab_action;	/* tenues par l'application (actions app.close-tab, app.active-tab) */
	GSimpleAction *project_close_action;	/* tenue par l'application (app.project-close, sensible avec un projet ouvert) */
	GtkWidget *focus_view;		/* page dont le terminal prend le focus à la prochaine inactivité (pointeur faible) */
	guint focus_id;
	guint sigusr1_id, sigusr2_id;
	GPtrArray *relays;		/* GtActionRelay des actions d'onglet (app.*), ciblant la session active */
	gulong active_id;		/* notify::active du gestionnaire */
	GtSession *title_session;	/* session active dont status-text et short-name font le titre (référence) */
	gulong title_status_id, title_short_id;
	GtProject *project;		/* projet de la fenêtre (6c) : créé dans gt_app_new, rendu à la finalisation de l'application */
	PendingOp pending_op;		/* OP_NONE : aucune question en cours */
	GtProjectData *pending_data;	/* OP_OPEN : projet lu et validé, en attente des réponses */
	gchar *pending_path;
	GtFileStamp pending_stamp;
	gboolean close_confirmed;	/* question de fermeture répondue (Don't save, Save réussi) : close-request ne la repose pas */
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

GtProject *gt_app_get_project(void)
{
	return gt_app.project;
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

/* close-tab (et boutons de fermeture des étiquettes) sensibles à partir de deux onglets ; indicateur d'activité de la barre d'état seulement avec
 * un onglet (sinon il est dans les onglets) ; état de app.active-tab = index de la page affichée. Appelée après tout ajout ou retrait de page */
static void update_tab_state(void)
{
	GtkNotebook *nb = gt_app.notebook != NULL ? GTK_NOTEBOOK(gt_app.notebook) : NULL;
	gint n = nb != NULL ? gtk_notebook_get_n_pages(nb) : 0;
	gint cur = nb != NULL ? gtk_notebook_get_current_page(nb) : -1;

	if(gt_app.close_tab_action != NULL)
		g_simple_action_set_enabled(gt_app.close_tab_action, n > 1 && !gt_app.services_stopped);
	for(gint i = 0; i < n; i++)
	{
		GtkWidget *view = gtk_notebook_get_nth_page(nb, i);

		gt_tab_label_set_closable(gtk_notebook_get_tab_label(nb, view), n > 1);
		if(!gt_session_view_is_shut_down(view))
			gt_status_bar_set_activity_visible(gt_session_view_get_slot(view, "status-bar"), n == 1);
	}
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

/* Sans projet : « GTKTerm - <status-text de l'onglet actif> » (6b). Avec un projet : « <nom> - <nom court de l'onglet actif> », « <nom> • - … »
 * s'il est modifié (plan, décision 10). Rien sans fenêtre ou après l'arrêt. */
static void title_update(void)
{
	GtSession *s = gt_app.title_session;
	gchar *title;

	if(gt_app.main_window == NULL || gt_app.services_stopped)
		return;
	if(gt_app.project != NULL && gt_project_is_open(gt_app.project))
	{
		gchar *name = gt_project_get_name(gt_app.project);
		const gchar *tab = s != NULL ? gt_session_get_short_name(s) : NULL;

		title = g_strdup_printf("%s%s - %s", name, gt_project_is_modified(gt_app.project) ? " •" : "", tab != NULL ? tab : "");
		g_free(name);
	}
	else
	{
		gchar *status = NULL;

		if(s != NULL)
			g_object_get(s, "status-text", &status, NULL);
		title = g_strdup_printf("GTKTerm - %s", status != NULL ? status : "");
		g_free(status);
	}
	gtk_window_set_title(GTK_WINDOW(gt_app.main_window), title);
	g_free(title);
}

/* notify::status-text, notify::short-name de la session du titre ; notify::open, ::path, ::modified du projet. status-text peut être notifié sous la
 * surveillance du transport : on ne fait que changer le titre. */
static void on_title_notify(GObject *object, GParamSpec *pspec, gpointer user_data)
{
	title_update();
}

static void title_unbind(void)
{
	if(gt_app.title_session == NULL)
		return;
	g_clear_signal_handler(&gt_app.title_status_id, gt_app.title_session);
	g_clear_signal_handler(&gt_app.title_short_id, gt_app.title_session);
	g_clear_object(&gt_app.title_session);
}

/* Titre suivant la session s (NULL : plus de suivi) */
static void title_bind(GtSession *s)
{
	title_unbind();
	if(s == NULL || gt_app.main_window == NULL)
		return;
	gt_app.title_session = g_object_ref(s);
	gt_app.title_status_id = g_signal_connect(s, "notify::status-text", G_CALLBACK(on_title_notify), NULL);
	gt_app.title_short_id = g_signal_connect(s, "notify::short-name", G_CALLBACK(on_title_notify), NULL);
	title_update();
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
	/* « Non lu » : seule la session affichée est vue (une réception ailleurs met son nom en gras) */
	for(guint i = 0; i < gt_session_manager_get_n(GT_SESSION_MANAGER(sessions)); i++)
	{
		GtSession *other = gt_session_manager_get_nth(GT_SESSION_MANAGER(sessions), i);

		gt_port_activity_set_viewed(gt_session_get_activity(other), other == s);
	}
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
	from = gt_app_get_active_session();	/* au OK du dialogue Port (modal) : c'est l'onglet qui l'a prérempli */
	s = gt_session_new(profile, profile_name);
	if(gt_app.project != NULL && gt_project_is_open(gt_app.project))
	{
		/* Onglet d'un projet (spec §3.2-4, 3.2-5) : copie du jeu de l'onglet actif, aucun fichier ; le gestionnaire la passe en in-project à
		 * l'ajout, avant la page : le panneau de script démarre vide */
		if(from != NULL)
			gt_macros_replace_set(gt_session_get_macros(s), gt_macro_set_copy(gt_macros_get_set(gt_session_get_macros(from))));
	}
	else
	{
		/* Fichier de macros : celui du profil ; sans fichier dans le profil, celui de l'onglet actif (le « fichier en cours » que load_profile
		 * relirait), et aucun si l'onglet actif n'en a pas chargé : jamais le fichier par défaut d'office, le nouvel onglet suit le premier */
		macros_path = profile->macros_file != NULL && profile->macros_file[0] != '\0' ? profile->macros_file
		              : from != NULL ? gt_macros_get_path(gt_session_get_macros(from)) : NULL;
		if(macros_path != NULL && macros_path[0] != '\0')
			gt_macros_load_file(gt_session_get_macros(s), macros_path, FALSE);	/* non interactif : un échec s'affiche en message */
	}
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

/* Arrêt et retrait de l'onglet de s, de page view (NULL : aucune), même le dernier (replace_all_tabs) : plan, décision 3, page puis session */
static void remove_session_view(GtSession *s, GtkWidget *view)
{
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

static void remove_session(GtSession *s)
{
	remove_session_view(s, gt_session_view_lookup(s));	/* avant l'arrêt de la page : lookup ne rend plus une page arrêtée */
}

static void on_close_confirmed(gboolean accepted, gpointer user_data)
{
	GtSession *s = g_weak_ref_get(user_data);

	g_weak_ref_clear(user_data);
	g_free(user_data);
	/* Session fermée entre-temps, application arrêtée, ou dernier onglet : rien */
	if(accepted && s != NULL && !gt_session_is_shut_down(s) && !gt_app.services_stopped && gt_app.main_window != NULL && gt_app.sessions != NULL
	   && gt_session_manager_index_of(gt_app.sessions, s) >= 0 && gt_session_manager_get_n(gt_app.sessions) > 1)
		remove_session(s);
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
		remove_session(s);
		return;
	}
	ref = g_new0(GWeakRef, 1);
	g_weak_ref_init(ref, s);
	gt_ui_confirm(gt_app_get_main_window(), _("Close this tab?"), _("A script is running or a file is being sent in this tab."), _("Close tab"),
	              on_close_confirmed, ref);
}

/* ---------- Projet (6c) ---------- */

/* Rappel de capture du projet : état de la page de s (séparateurs, visibilités, disposition du panneau de script) ; sans page, les défauts */
static void project_page_func(GtSession *s, GtProjectTab *tab, gpointer user_data)
{
	GtkWidget *view = gt_session_view_lookup(s);

	if(view != NULL)
		gt_session_view_fill_project_tab(view, tab);
}

/* Fenêtre et services encore là : chaque rappel de question le vérifie avant d'agir */
static gboolean app_alive(void)
{
	return gt_app.main_window != NULL && !gt_app.services_stopped && gt_app.sessions != NULL && gt_app.project != NULL;
}

static void pending_clear(void)
{
	gt_app.pending_op = OP_NONE;
	g_clear_pointer(&gt_app.pending_data, gt_project_data_free);
	g_clear_pointer(&gt_app.pending_path, g_free);
	memset(&gt_app.pending_stamp, 0, sizeof gt_app.pending_stamp);
}

/* Remplace tous les onglets par ceux de d (plan, décision 9) : pages arrêtées, moteurs Lua prévenus, puis chaque session arrêtée et retirée (ordre 6b) ;
 * puis, dans l'ordre, une session par onglet (profil validé et averti comme un profil chargé), sa page (séparateurs et visibilités de l'onglet,
 * [layout] pour les positions du panneau de script, disposition de l'onglet), profil appliqué, port ouvert ; enfin l'onglet d->active affiché.
 * in_project : onglets de projet (in-project et texte du script posés avant la page, macros copiées de l'onglet), projet commencé sur path, stamp,
 * d, et last_project écrit si path ; sinon onglets libres (projet fermé d'abord, macros chargées comme au démarrage : app_config_load_profile_macros). */
static void replace_all_tabs(const GtProjectData *d, const gchar *path, const GtFileStamp *stamp, gboolean in_project)
{
	GtkNotebook *nb = GTK_NOTEBOOK(gt_app.notebook);
	GtSession *active = gt_app_get_active_session();
	gboolean lock_port = active != NULL ? gt_session_get_profile(active)->lock_port : TRUE;	/* -L suit les onglets, comme New port */
	GtSession *show = NULL;
	GtkWidget *view;
	GPtrArray *views = g_ptr_array_new_with_free_func(g_object_unref);

	/* Pages relevées avant leur arrêt (gt_session_view_lookup ne rend plus une page arrêtée), toutes arrêtées, puis chaque session arrêtée
	 * et retirée avec sa page ; enfin les sessions sans page (la première, au démarrage) */
	for(gint i = 0; i < gtk_notebook_get_n_pages(nb); i++)
		g_ptr_array_add(views, g_object_ref(gtk_notebook_get_nth_page(nb, i)));
	gt_app_foreach_view(view_shutdown, NULL);
	for(guint i = 0; i < gt_session_manager_get_n(gt_app.sessions); i++)	/* demande non bloquante : les moteurs s'arrêtent ensemble */
		gt_script_engine_stop(gt_session_get_script_engine(gt_session_manager_get_nth(gt_app.sessions, i)));
	for(guint i = 0; i < views->len; i++)
	{
		GtkWidget *page = g_ptr_array_index(views, i);
		GtSession *s = gt_session_view_get_session(page);

		if(s != NULL && gt_session_manager_index_of(gt_app.sessions, s) >= 0)
			remove_session_view(s, page);
		else
			gt_main_window_remove_view(gt_app.main_window, page);
	}
	g_ptr_array_unref(views);
	while(gt_session_manager_get_n(gt_app.sessions) > 0)
		remove_session(gt_session_manager_get_nth(gt_app.sessions, 0));
	gt_project_end(gt_app.project);	/* les sessions créées ci-dessous ne passent en projet que par in_project, puis gt_project_begin */

	for(guint i = 0; i < d->tabs->len; i++)
	{
		GtProjectTab *t = g_ptr_array_index(d->tabs, i);
		GtSession *s = gt_session_new(&t->profile, t->profile_name);

		gt_session_get_profile(s)->lock_port = lock_port;
		if(in_project)
		{
			gt_session_set_in_project(s, TRUE);	/* avant la page : le panneau de script prend le texte de la session, jamais [session] */
			gt_macros_replace_set(gt_session_get_macros(s), gt_macro_set_copy(t->macros));
			gt_session_set_script_text(s, t->script);
		}
		else
			app_config_load_profile_macros(s);	/* comme au démarrage : fichier du profil, sinon le fichier par défaut ; un échec s'affiche en message */
		Verify_configuration(s);
		gt_session_manager_add(gt_app.sessions, s, -1);	/* avant la page : switch-page la trouve dans le gestionnaire */
		view = session_page_new(s, &t->page, NULL, -1);
		gt_session_view_apply_project_tab(view, t);
		gt_session_apply_profile(s);
		gt_session_view_apply_profile(view);
		gt_session_open_port(s);	/* échec : open-failed, boîte non bloquante */
		if(i == d->active)
			show = s;
		g_object_unref(s);	/* tenue par le gestionnaire et par la page */
	}
	if(show != NULL)
	{
		view = gt_session_view_lookup(show);
		gtk_notebook_set_current_page(nb, gtk_notebook_page_num(nb, view));
		gt_session_manager_set_active(gt_app.sessions, show);	/* déjà fait par switch-page, sauf au démarrage (relié ensuite) */
		focus_terminal_later(view);
	}
	update_tab_state();
	if(in_project)
	{
		gt_project_begin(gt_app.project, path, stamp, d);
		if(path != NULL)
			app_config_set_last_project(path);
	}
}

/* Un onglet (New project, Close project) : profil « default » de .gtktermrc (ou les défauts), état de la page active ; for_project : sans macros */
static GtProjectData *single_tab_data(gboolean for_project)
{
	GtProjectData *d = gt_project_data_new();
	GtProjectTab *t = gt_project_tab_new();
	GtkWidget *view = gt_app_get_active_view();

	if(app_config_has_profile("default"))
		gt_config_store_get_profile(config_get_store(), "default", &t->profile);
	if(for_project)
		gt_profile_set_string(&t->profile.macros_file, NULL);
	if(view != NULL)
		gt_session_view_fill_project_tab(view, t);
	g_ptr_array_add(d->tabs, t);
	return d;
}

/* Toutes les réponses données : l'opération en attente s'exécute */
static void perform(void)
{
	PendingOp op = gt_app.pending_op;
	GtProjectData *d;

	if(!app_alive())
	{
		pending_clear();
		return;
	}
	switch(op)
	{
	case OP_NEW:
		pending_clear();
		d = single_tab_data(TRUE);
		replace_all_tabs(d, NULL, NULL, TRUE);	/* « Untitled project » */
		gt_project_data_free(d);
		break;
	case OP_OPEN:
		/* Fichier réécrit depuis sa lecture, avant les questions (Save du projet courant qu'on rouvre, Save as vers le fichier qu'on ouvre) :
		 * relu, l'ouverture prend ce qui vient d'être enregistré ; relecture refusée : message, rien ne change */
		if(gt_file_stamp_changed(gt_app.pending_path, &gt_app.pending_stamp))
		{
			GError *error = NULL;

			g_clear_pointer(&gt_app.pending_data, gt_project_data_free);
			if(!gt_project_data_load(gt_app.pending_path, &gt_app.pending_data, &gt_app.pending_stamp, &error))
			{
				gt_ui_message(gt_app_get_main_window(), GT_APP_MSG_ERROR, error->message);
				g_error_free(error);
				pending_clear();
				break;
			}
		}
		replace_all_tabs(gt_app.pending_data, gt_app.pending_path, &gt_app.pending_stamp, TRUE);
		pending_clear();
		break;
	case OP_CLOSE:
		pending_clear();
		d = single_tab_data(FALSE);
		replace_all_tabs(d, NULL, NULL, FALSE);	/* retour à l'état 6b */
		gt_project_data_free(d);
		app_config_set_last_project(NULL);
		break;
	case OP_QUIT:
		pending_clear();
		gt_app.close_confirmed = TRUE;
		gtk_window_close(GTK_WINDOW(gt_app.main_window));	/* close-request ne repose pas la question */
		break;
	case OP_NONE:
		break;
	}
}

static void project_save(gboolean as, gboolean then);

/* Fin d'un enregistrement ; then : l'opération en attente suit un enregistrement réussi, elle est abandonnée sinon */
static void save_finish(GtProjectSaveResult r, const GError *error, gboolean then)
{
	if(r == GT_PROJECT_SAVED)
	{
		app_config_set_last_project(gt_project_get_path(gt_app.project));
		if(then)
			perform();
		return;
	}
	if(error != NULL)
		gt_ui_message(gt_app_get_main_window(), GT_APP_MSG_ERROR, error->message);
	if(then)
		pending_clear();
}

/* Reload (conflit) : le projet relu depuis le disque remplace les onglets, sans autre question ; refus : message, rien ne change */
static void project_reload(void)
{
	gchar *path = g_strdup(gt_project_get_path(gt_app.project));
	GtProjectData *d = NULL;
	GtFileStamp stamp;
	GError *error = NULL;

	if(gt_project_data_load(path, &d, &stamp, &error))
	{
		replace_all_tabs(d, path, &stamp, TRUE);
		gt_project_data_free(d);
	}
	else
	{
		gt_ui_message(gt_app_get_main_window(), GT_APP_MSG_ERROR, error->message);
		g_error_free(error);
	}
	g_free(path);
}

static gboolean any_session_busy(void);

/* Reload confirmé (onglet occupé) : le même projet est toujours ouvert sur le même fichier */
static void on_reload_confirmed(gboolean accepted, gpointer user_data)
{
	gchar *path = user_data;

	if(accepted && app_alive() && gt_project_is_open(gt_app.project) && g_strcmp0(gt_project_get_path(gt_app.project), path) == 0)
		project_reload();
	g_free(path);
}

static void on_conflict_chosen(gint choice, gpointer user_data)
{
	gboolean then = GPOINTER_TO_INT(user_data);
	GError *error = NULL;

	if(!app_alive() || !gt_project_is_open(gt_app.project) || gt_project_get_path(gt_app.project) == NULL)
	{
		if(then)
			pending_clear();
		return;
	}
	if(choice == 0)	/* Overwrite */
	{
		save_finish(gt_project_save(gt_app.project, NULL, TRUE, &error), error, then);
		g_clear_error(&error);
		return;
	}
	if(then)
		pending_clear();	/* Reload ou Cancel : l'opération en attente est abandonnée */
	if(choice != 1)
		return;
	/* Reload remplace tous les onglets : même confirmation qu'Open, New et Close project si un script tourne ou qu'un fichier part */
	if(any_session_busy())
		gt_ui_confirm(gt_app_get_main_window(), _("Close all tabs?"), _("A script is running or a file is being sent."), _("Close tabs"),
		              on_reload_confirmed, g_strdup(gt_project_get_path(gt_app.project)));
	else
		project_reload();
}

/* Save project as… vers path, écrasement déjà confirmé */
static void save_as_write(const gchar *path, gboolean then)
{
	GError *error = NULL;

	if(!gt_project_is_open(gt_app.project))	/* onglets libres : ils deviennent des onglets de projet (macros intégrées, script de la session) */
		gt_project_begin(gt_app.project, NULL, NULL, NULL);
	save_finish(gt_project_save(gt_app.project, path, TRUE, &error), error, then);
	g_clear_error(&error);
}

typedef struct
{
	gchar *path;
	gboolean then;
} SaveAsReplace;

static void on_save_as_replace(gboolean accepted, gpointer user_data)
{
	SaveAsReplace *r = user_data;

	if(accepted && app_alive())
		save_as_write(r->path, r->then);
	else if(r->then)
		pending_clear();
	g_free(r->path);
	g_free(r);
}

static void on_save_as_chosen(GFile *file, gpointer user_data)
{
	gboolean then = GPOINTER_TO_INT(user_data);
	gchar *path;

	path = file != NULL && app_alive() ? g_file_get_path(file) : NULL;
	if(path == NULL)
	{
		if(then)
			pending_clear();
		return;
	}
	if(!g_str_has_suffix(path, ".gtkterm"))	/* plan, décision 11 */
	{
		gchar *full = g_strconcat(path, ".gtkterm", NULL);

		g_free(path);
		path = full;
		/* Le sélecteur n'a confirmé l'écrasement que pour le nom tapé : le fichier suffixé existant fait l'objet de sa propre question */
		if(g_file_test(path, G_FILE_TEST_EXISTS))
		{
			SaveAsReplace *r = g_new0(SaveAsReplace, 1);

			r->path = path;
			r->then = then;
			gt_ui_confirm(gt_app_get_main_window(), _("A project file with this name already exists. Replace it?"), path, _("Replace"),
			              on_save_as_replace, r);
			return;
		}
	}
	save_as_write(path, then);	/* nom tapé avec son suffixe : le sélecteur a demandé l'écrasement */
	g_free(path);
}

/* Save project (as : Save project as…) ; sans projet ou sans fichier : dialogue d'enregistrement. then : voir save_finish */
static void project_save(gboolean as, gboolean then)
{
	const gchar *path = gt_project_is_open(gt_app.project) ? gt_project_get_path(gt_app.project) : NULL;
	GError *error = NULL;
	GtProjectSaveResult r;

	if(as || path == NULL)
	{
		GFile *folder = NULL;
		gchar *name;

		if(path != NULL)
		{
			gchar *dir = g_path_get_dirname(path);
			gchar *base = gt_project_get_name(gt_app.project);

			folder = g_file_new_for_path(dir);
			name = g_strconcat(base, ".gtkterm", NULL);
			g_free(base);
			g_free(dir);
		}
		else
			name = g_strdup("project.gtkterm");
		gt_ui_save_file(gt_app_get_main_window(), _("Save project"), folder, name, on_save_as_chosen, GINT_TO_POINTER(then));
		g_clear_object(&folder);
		g_free(name);
		return;
	}
	r = gt_project_save(gt_app.project, NULL, FALSE, &error);
	if(r == GT_PROJECT_CONFLICT)
	{
		const gchar *const labels[] = { _("Overwrite"), _("Reload"), _("Cancel"), NULL };

		/* Cancel par défaut, Overwrite destructeur : Entrée n'écrase jamais le travail du tiers */
		gt_ui_choose(gt_app_get_main_window(), _("The project file changed on disk."), path, labels, 2, 0, on_conflict_chosen, GINT_TO_POINTER(then));
		return;
	}
	save_finish(r, error, then);
	g_clear_error(&error);
}

static void on_modified_chosen(gint choice, gpointer user_data)
{
	if(!app_alive())
	{
		pending_clear();
		return;
	}
	if(choice == 0)	/* Save : l'opération suit un enregistrement réussi */
		project_save(FALSE, TRUE);
	else if(choice == 1)	/* Don't save */
		perform();
	else
		pending_clear();
}

/* Projet ouvert et modifié (recalculé d'office) : Save / Don't save / Cancel ; sinon l'opération s'exécute */
static void ask_modified(void)
{
	gt_project_check_now(gt_app.project);
	if(gt_project_is_open(gt_app.project) && gt_project_is_modified(gt_app.project))
	{
		const gchar *const labels[] = { _("Save"), _("Don't save"), _("Cancel"), NULL };
		gchar *name = gt_project_get_name(gt_app.project);

		gt_ui_choose(gt_app_get_main_window(), _("Save changes to the project?"), name, labels, 0, -1, on_modified_chosen, NULL);
		g_free(name);
	}
	else
		perform();
}

static void on_busy_confirmed(gboolean accepted, gpointer user_data)
{
	if(!accepted || !app_alive())
	{
		pending_clear();
		return;
	}
	ask_modified();
}

static gboolean any_session_busy(void)
{
	for(guint i = 0; i < gt_session_manager_get_n(gt_app.sessions); i++)
		if(gt_session_is_busy(gt_session_manager_get_nth(gt_app.sessions, i)))
			return TRUE;
	return FALSE;
}

/* Opération op (données d'OP_OPEN : data, path et stamp, possédés) : confirmation si un onglet est occupé (sauf OP_QUIT : la fermeture ne pose
 * que la question « modifié »), puis Save / Don't save / Cancel. Une autre opération attend déjà ses réponses : celle-ci est abandonnée
 * (données libérées, aucune boîte) ; pour OP_QUIT, close-request rend alors TRUE sans question (la boîte déjà ouverte décide). */
static void request(PendingOp op, GtProjectData *data, gchar *path, const GtFileStamp *stamp)
{
	if(!app_alive() || gt_app.pending_op != OP_NONE)
	{
		gt_project_data_free(data);
		g_free(path);
		return;
	}
	gt_app.pending_op = op;
	gt_app.pending_data = data;
	gt_app.pending_path = path;
	if(stamp != NULL)
		gt_app.pending_stamp = *stamp;
	if(op != OP_QUIT && any_session_busy())
		gt_ui_confirm(gt_app_get_main_window(), _("Close all tabs?"), _("A script is running or a file is being sent."), _("Close tabs"),
		              on_busy_confirmed, NULL);
	else
		ask_modified();
}

/* Open project : fichier lu et validé en entier avant toute question (refus : message, rien ne change) */
static void on_open_chosen(GFile *file, gpointer user_data)
{
	GtProjectData *d = NULL;
	GtFileStamp stamp;
	GError *error = NULL;
	gchar *path;

	path = file != NULL && app_alive() ? g_file_get_path(file) : NULL;
	if(path == NULL)
		return;
	if(!gt_project_data_load(path, &d, &stamp, &error))
	{
		gt_ui_message(gt_app_get_main_window(), GT_APP_MSG_ERROR, error->message);
		g_error_free(error);
		g_free(path);
		return;
	}
	request(OP_OPEN, d, path, &stamp);
}

static void project_actions_update(void)
{
	if(gt_app.project_close_action != NULL && gt_app.project != NULL)
		g_simple_action_set_enabled(gt_app.project_close_action, gt_project_is_open(gt_app.project) && !gt_app.services_stopped);
}

/* notify::open, ::path, ::modified du projet */
static void on_project_notify(GObject *object, GParamSpec *pspec, gpointer user_data)
{
	project_actions_update();
	title_update();
}

/* Arrêt dans l'ordre de la spec (§4.4, plan 6b décision 3 : interface d'abord, puis les sessions) ; une seule fois (close-request,
 * sinon shutdown) */
static void app_stop_services(void)
{
	if(gt_app.services_stopped)
		return;
	gt_app.services_stopped = TRUE;
	pending_clear();	/* une question encore ouverte est détruite avec la fenêtre : son rappel ne fera rien */
	project_actions_update();
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

	/* Projet ouvert et modifié (recalculé d'office, spec §3.2-8) : fermeture arrêtée, Save / Don't save / Cancel (plan, décision 9) ;
	 * la réponse rappelle gtk_window_close avec close_confirmed. Sans projet : comportement 6b, aucune question.
	 * Une opération de projet attend déjà ses réponses (boîte ou sélecteur de New, Open, Close project, ou cette même question) : request()
	 * ignore la demande, et la fermeture est refusée sans nouvelle question ; il faut d'abord répondre à la boîte ouverte. */
	if(!gt_app.close_confirmed && !gt_app.services_stopped && gt_app.project != NULL && gt_project_is_open(gt_app.project))
	{
		gt_project_check_now(gt_app.project);
		if(gt_project_is_modified(gt_app.project))
		{
			request(OP_QUIT, NULL, NULL, NULL);
			return TRUE;
		}
	}
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

/* Projet de démarrage (spec §3.2-14) : --project, sinon le dernier projet si la ligne de commande ne donne ni profil ni réglage de port ; à libérer */
static gchar *startup_project_path(void)
{
	if(gt_app.cmd != NULL && gt_app.cmd->project != NULL)
		return g_strdup(gt_app.cmd->project);
	if(gt_app.cmd != NULL && gt_app.cmd->profile_given)
		return NULL;
	return app_config_get_last_project();
}

static void on_activate(GApplication *app, gpointer user_data)
{
	GtSession *session;
	GtProjectData *data = NULL;
	GtFileStamp stamp;
	GtWindowState st;
	GtkWidget *view;
	GtkCssProvider *css;
	gchar *project_path;

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

	/* Projet de démarrage lu en entier ; refus : message (mis en file, affiché après la présentation) et démarrage 6b */
	project_path = startup_project_path();
	if(project_path != NULL)
	{
		GError *error = NULL;

		if(!gt_project_data_load(project_path, &data, &stamp, &error))
		{
			/* Dernier projet disparu (son dossier existe encore) : last_project effacé, sinon la même boîte reviendrait à chaque démarrage
			 * (Close project est désactivé sans projet). Autre refus (droits, dossier absent car support démonté, fichier abîmé) : la clé reste.
			 * --project ne la touche pas. */
			gchar *dir = g_path_get_dirname(project_path);
			gboolean forget = g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT) && g_file_test(dir, G_FILE_TEST_IS_DIR)
					  && (gt_app.cmd == NULL || gt_app.cmd->project == NULL);

			g_free(dir);
			if(forget)
			{
				app_config_set_last_project(NULL);
				gt_app_message(GT_APP_MSG_ERROR, _("Cannot open the project: %s\nIt is no longer reopened at start-up."), error->message);
			}
			else
				gt_app_message(GT_APP_MSG_ERROR, _("Cannot open the project: %s"), error->message);
			g_error_free(error);
			data = NULL;
		}
	}
	if(data != NULL)
	{
		/* La première session (main.c, sans page) est arrêtée et retirée, puis les onglets du projet créés, ports ouverts (open-failed en file) */
		replace_all_tabs(data, project_path, &stamp, TRUE);
	}
	else
	{
		/* Page de la session : terminal, barres, panneaux, raccourcis de macros, actions de page ; séparateurs et visibilités de [window],
		 * disposition du panneau de script de [layout] et [session] (première page) */
		app_config_load_window_state(&st);
		session_page_new(gt_app_get_active_session(), &st, NULL, -1);	/* étiquette invisible avec un seul onglet ; open-failed suivi */
	}
	session = gt_app_get_active_session();
	view = gt_app_get_active_view();
	/* Après le gestionnaire par défaut : la page courante du notebook est déjà la nouvelle (index de app.active-tab) */
	gt_app.switch_id = g_signal_connect_after(gt_app.notebook, "switch-page", G_CALLBACK(on_switch_page), NULL);
	gt_app.reorder_id = g_signal_connect(gt_app.notebook, "page-reordered", G_CALLBACK(on_page_reordered), NULL);

	install_relays(app, session);	/* toutes les actions d'onglet existent : relais app.* vers la session active */
	title_bind(session);
	update_tab_state();
	project_actions_update();

	if(data == NULL)
	{
		gt_session_apply_profile(session);	/* un seul « changed » des options : un seul redessin du terminal */
		gt_session_view_apply_profile(view);
	}
	/* La frappe part sur le port dès l'ouverture */
	gtk_window_set_focus(GTK_WINDOW(gt_app.main_window), GTK_WIDGET(gt_terminal_view_get_vte(gt_session_view_get_terminal_view(view))));
	gtk_window_present(GTK_WINDOW(gt_app.main_window));
	gt_ui_main_window_ready(GTK_WINDOW(gt_app.main_window));	/* messages mis en file avant la fenêtre : affichés maintenant, dans l'ordre */
	if(data == NULL)
		gt_session_open_port(session);	/* projet : chaque onglet a déjà ouvert le sien */
	gt_app.sigusr1_id = g_unix_signal_add(SIGUSR1, on_sigusr1, NULL);
	gt_app.sigusr2_id = g_unix_signal_add(SIGUSR2, on_sigusr2, NULL);
	gt_update_startup_check(GTK_WINDOW(gt_app.main_window));	/* différée (g_idle_add), dernière étape */
	gt_project_data_free(data);
	g_free(project_path);
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

/* File › New project, Open project…, Save project, Save project as…, Close project (spec §3.2-10 à 3.2-13) */
static void action_project_new(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	request(OP_NEW, NULL, NULL, NULL);
}

static void action_project_open(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	const gchar *path;
	GListStore *filters;
	GtkFileFilter *f;
	GFile *initial;

	if(!app_alive())
		return;
	path = gt_project_is_open(gt_app.project) ? gt_project_get_path(gt_app.project) : NULL;
	initial = path != NULL ? g_file_new_for_path(path) : NULL;
	filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
	f = gtk_file_filter_new();
	gtk_file_filter_set_name(f, _("GTKTerm projects"));
	gtk_file_filter_add_pattern(f, "*.gtkterm");
	g_list_store_append(filters, f);
	g_object_unref(f);
	f = gtk_file_filter_new();
	gtk_file_filter_set_name(f, _("All files"));
	gtk_file_filter_add_pattern(f, "*");
	g_list_store_append(filters, f);
	g_object_unref(f);
	gt_ui_open_file(gt_app_get_main_window(), _("Open project"), initial, G_LIST_MODEL(filters), on_open_chosen, NULL);
	g_object_unref(filters);
	g_clear_object(&initial);
}

static void action_project_save(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	if(app_alive())
		project_save(FALSE, FALSE);
}

static void action_project_save_as(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	if(app_alive())
		project_save(TRUE, FALSE);
}

static void action_project_close(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	if(app_alive() && gt_project_is_open(gt_app.project))
		request(OP_CLOSE, NULL, NULL, NULL);
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
	{ .name = "project-new", .activate = action_project_new },
	{ .name = "project-open", .activate = action_project_open },
	{ .name = "project-save", .activate = action_project_save },
	{ .name = "project-save-as", .activate = action_project_save_as },
	{ .name = "project-close", .activate = action_project_close },
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
	{ "app.project-open", "<Control><Shift>o" },
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
	gt_app.project_close_action = G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(app), "project-close"));
	g_simple_action_set_enabled(gt_app.close_tab_action, FALSE);	/* un seul onglet */
	project_actions_update();	/* aucun projet ouvert */
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
	gt_app.close_tab_action = gt_app.active_tab_action = gt_app.project_close_action = NULL;	/* tenues par l'application */
	pending_clear();
	g_clear_object(&gt_app.project);	/* avant le gestionnaire : il en tient une référence */
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
	gt_app.project = gt_project_new(sessions);	/* aucun projet ouvert */
	gt_project_set_page_func(gt_app.project, project_page_func, NULL);
	g_signal_connect(gt_app.project, "notify::open", G_CALLBACK(on_project_notify), NULL);
	g_signal_connect(gt_app.project, "notify::path", G_CALLBACK(on_project_notify), NULL);
	g_signal_connect(gt_app.project, "notify::modified", G_CALLBACK(on_project_notify), NULL);
	g_object_weak_ref(G_OBJECT(app), on_app_finalized, NULL);
	g_signal_connect(app, "startup", G_CALLBACK(on_startup), NULL);
	g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
	g_signal_connect(app, "shutdown", G_CALLBACK(on_shutdown), NULL);
	return app;
}
