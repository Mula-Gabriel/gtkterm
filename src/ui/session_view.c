/* Page d'onglet (sous-projet 6b) : voir session_view.h. Reprend la construction sous la barre de menus de main_window.c (6a). */
#include <config.h>

#include "app.h"
#include "app_actions.h"
#include "file_actions.h"
#include "hex_send_bar.h"
#include "macro_panel.h"
#include "macro_shortcuts.h"
#include "script_panel.h"
#include "search_bar.h"
#include "session_view.h"
#include "status_bar.h"
#include "terminal_view.h"

#define SESSION_VIEW_KEY "gt-session-view-data"
#define SESSION_PAGE_KEY "gt-session-view"	/* sur la session : sa page (non possédée), retirée à la destruction de la page */

typedef struct
{
	GtkWidget *view;	/* la page (non possédée) */
	GtSession *session;	/* référence */
	GtWindowState initial;	/* séparateurs et visibilités initiaux : valeurs des emplacements encore vides */
	GtkWidget *terminal_view;
	GtkWidget *h_paned;	/* terminal | panneau de macros */
	GtkWidget *v_paned;	/* (terminal | macros) / panneau de script */
	GtkWidget *search_box, *hex_box, *status_box;	/* conteneurs des emplacements */
	/* Barre d'état de la page, pointeur faible posé à la création : l'arrêt ne passe jamais par les emplacements, car à l'émission de « destroy »
	 * (filet on_view_destroy) gtk_box_dispose a déjà détaché et libéré les enfants de la page */
	GtkWidget *status_bar;
	GtkWidget *macro_panel, *script_panel;	/* pointeurs faibles, pour la même raison */
	gboolean shut_down;
} SessionView;

/* Actions de page posées directement ici (les autres sont retirées par leur module : app_actions.c, file_actions.c) */
static const gchar *const own_action_names[] = { "find", "show-hex-bar" };

static SessionView *session_view_get(GtkWidget *view)
{
	return view != NULL ? g_object_get_data(G_OBJECT(view), SESSION_VIEW_KEY) : NULL;
}

static GtkWidget *slot_box(void)
{
	return gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
}

/* Séparateur horizontal sans position enregistrée : le GtkPaned de GTK 4 ne laisse au panneau de macros que sa largeur minimale (sa barre
 * d'onglets ; ses pages défilent). Une seule fois, à la première allocation où le panneau est visible : position qui lui laisse sa largeur
 * naturelle, sans dépasser la moitié de la largeur. Une position enregistrée ([window] paned_position) est appliquée telle quelle. */
static void on_h_paned_max_position(GObject *paned, GParamSpec *pspec, gpointer data)
{
	GtkWidget *end = gtk_paned_get_end_child(GTK_PANED(paned));
	gint width = gtk_widget_get_width(GTK_WIDGET(paned));
	gint max_pos = 0, min_w = 0, nat_w = 0;

	if(end == NULL || !gtk_widget_get_visible(end) || width <= 1)
		return;
	g_object_get(paned, "max-position", &max_pos, NULL);
	gtk_widget_measure(end, GTK_ORIENTATION_HORIZONTAL, -1, &min_w, &nat_w, NULL, NULL);
	g_signal_handlers_disconnect_by_func(paned, on_h_paned_max_position, data);
	gtk_paned_set_position(GTK_PANED(paned), MAX(max_pos - (nat_w - min_w), width / 2));
}

#define SCRIPT_FIT_KEY "gt-script-fit"	/* sur le séparateur vertical : hauteur du panneau de script à vérifier dès la prochaine allocation */
#define SCRIPT_PANEL_MIN_HEIGHT 150	/* hauteur utile du panneau de script (barre d'outils, éditeur, console) */

/* Panneau de script visible alors que le séparateur vertical ne lui laisse pas une hauteur utile (GtkPaned de GTK 4 : affiché à l'exécution
 * sans position enregistrée, il n'aurait que sa barre d'outils et l'éditeur, réduit à zéro, ne serait plus dessiné) : position qui lui donne un
 * tiers de la hauteur, au moins SCRIPT_PANEL_MIN_HEIGHT. Une position posée (enregistrée ou déplacée) qui lui laisse cette hauteur est gardée
 * telle quelle. Rien tant que le séparateur n'a pas de taille : la vérification attend la prochaine allocation (notify::max-position). */
static void script_panel_fit(GtkPaned *paned)
{
	GtkWidget *end = gtk_paned_get_end_child(paned);
	gint height = gtk_widget_get_height(GTK_WIDGET(paned));
	gboolean position_set = FALSE;

	if(g_object_get_data(G_OBJECT(paned), SCRIPT_FIT_KEY) == NULL || end == NULL || !gtk_widget_get_visible(end) || height <= 1)
		return;
	g_object_set_data(G_OBJECT(paned), SCRIPT_FIT_KEY, NULL);
	g_object_get(paned, "position-set", &position_set, NULL);	/* position jamais posée : celle que GTK calculera ne laisse que la barre d'outils */
	if(!position_set || height - gtk_paned_get_position(paned) < SCRIPT_PANEL_MIN_HEIGHT)
		gtk_paned_set_position(paned, MAX(0, height - MAX(height / 3, SCRIPT_PANEL_MIN_HEIGHT)));	/* bornée par max-position */
}

static void on_v_paned_max_position(GObject *paned, GParamSpec *pspec, gpointer data)
{
	script_panel_fit(GTK_PANED(paned));
}

/* notify::visible du panneau de script (user_data : le séparateur vertical, rappel débranché à sa finalisation) */
static void on_script_panel_visible(GObject *panel, GParamSpec *pspec, gpointer paned)
{
	if(!gtk_widget_get_visible(GTK_WIDGET(panel)))
		return;
	g_object_set_data(G_OBJECT(paned), SCRIPT_FIT_KEY, GINT_TO_POINTER(1));
	script_panel_fit(GTK_PANED(paned));
}

/* Destruction de la page : arrêt s'il n'a pas eu lieu, la session ne la désigne plus. GTK 4 émet « destroy » depuis gtk_widget_dispose, après
 * le dispose de GtkBox : les enfants de la page (emplacements, paneds, terminal) peuvent déjà être libérés ; l'arrêt n'y touche que par
 * pointeurs faibles (barre d'état ici, VTE dans app_actions.c) ou par des objets qui ne sont pas des enfants (session, groupe d'actions,
 * données de file_actions.c attachées à la page elle-même). */
static void on_view_destroy(GtkWidget *view, gpointer user_data)
{
	SessionView *sv = user_data;

	gt_session_view_shutdown(view);
	if(g_object_get_data(G_OBJECT(sv->session), SESSION_PAGE_KEY) == view)
		g_object_set_data(G_OBJECT(sv->session), SESSION_PAGE_KEY, NULL);
}

static void session_view_free(gpointer data)
{
	SessionView *sv = data;

	if(sv->status_bar != NULL)
		g_object_remove_weak_pointer(G_OBJECT(sv->status_bar), (gpointer *)&sv->status_bar);
	if(sv->macro_panel != NULL)
		g_object_remove_weak_pointer(G_OBJECT(sv->macro_panel), (gpointer *)&sv->macro_panel);
	if(sv->script_panel != NULL)
		g_object_remove_weak_pointer(G_OBJECT(sv->script_panel), (gpointer *)&sv->script_panel);
	g_object_unref(sv->session);
	g_free(sv);
}

GtkWidget *gt_session_view_new(GtkApplication *app, GtSession *s, const GtWindowState *st, GtkWidget *layout_from)
{
	SessionView *sv = g_new0(SessionView, 1);
	GtkTermLink *link = gt_session_get_link(s);
	GActionMap *tab_actions = G_ACTION_MAP(gt_session_get_actions(s));
	GtkWidget *view = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget *status, *search, *hex, *macros, *script;

	sv->view = view;
	sv->session = g_object_ref(s);
	sv->initial = *st;
	gtk_widget_set_name(view, "session-view");
	gtk_widget_set_hexpand(view, TRUE);
	gtk_widget_set_vexpand(view, TRUE);
	g_object_set_data_full(G_OBJECT(view), SESSION_VIEW_KEY, sv, session_view_free);

	sv->search_box = slot_box();
	gtk_box_append(GTK_BOX(view), sv->search_box);

	sv->terminal_view = gt_terminal_view_new(link, gt_session_get_options(s));
	sv->h_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_paned_set_start_child(GTK_PANED(sv->h_paned), sv->terminal_view);
	gtk_paned_set_resize_start_child(GTK_PANED(sv->h_paned), TRUE);
	gtk_paned_set_shrink_start_child(GTK_PANED(sv->h_paned), FALSE);
	sv->v_paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
	gtk_paned_set_start_child(GTK_PANED(sv->v_paned), sv->h_paned);
	gtk_paned_set_resize_start_child(GTK_PANED(sv->v_paned), TRUE);
	gtk_paned_set_shrink_start_child(GTK_PANED(sv->v_paned), FALSE);
	gtk_widget_set_vexpand(sv->v_paned, TRUE);
	gtk_box_append(GTK_BOX(view), sv->v_paned);

	sv->hex_box = slot_box();
	gtk_box_append(GTK_BOX(view), sv->hex_box);
	sv->status_box = slot_box();
	gtk_box_append(GTK_BOX(view), sv->status_box);

	/* Séparateurs : valeurs aberrantes déjà écartées par app_config_load_window_state */
	if(sv->initial.h_paned >= 0)
		gtk_paned_set_position(GTK_PANED(sv->h_paned), sv->initial.h_paned);
	else
		g_signal_connect(sv->h_paned, "notify::max-position", G_CALLBACK(on_h_paned_max_position), NULL);
	if(sv->initial.v_paned >= 0)
		gtk_paned_set_position(GTK_PANED(sv->v_paned), sv->initial.v_paned);
	g_signal_connect(sv->v_paned, "notify::max-position", G_CALLBACK(on_v_paned_max_position), NULL);

	/* Barres de la page et leurs actions, dans le groupe de la session */
	status = gt_status_bar_new(link);
	search = gt_search_bar_new(gt_terminal_view_get_vte(sv->terminal_view));
	hex = gt_hex_send_bar_new(link, s);
	gt_session_view_set_slot(view, "status-bar", status);
	sv->status_bar = status;
	g_object_add_weak_pointer(G_OBJECT(status), (gpointer *)&sv->status_bar);
	gt_session_view_set_slot(view, "search-bar", search);
	gt_session_view_set_slot(view, "hex-bar", hex);	/* visibilité initiale (st) */
	gt_search_bar_install_action(tab_actions, search);
	gt_hex_send_bar_install_action(tab_actions, hex);
	gt_app_actions_install(tab_actions, sv->terminal_view);
	gt_file_actions_new(view, s, gt_app_take_initial_send_file());

	/* Panneau de macros, raccourcis et panneau de script de la session (show-macro-panel, show-script-panel, script-layout dans son groupe) */
	macros = gt_macro_panel_new(s, tab_actions, sv->initial.macro_panel_visible);
	gt_session_view_set_slot(view, "macro-panel", macros);
	sv->macro_panel = macros;
	g_object_add_weak_pointer(G_OBJECT(macros), (gpointer *)&sv->macro_panel);
	gt_macro_shortcuts_attach(view, s);
	script = gt_script_panel_new(app, s, tab_actions, layout_from != NULL ? gt_session_view_get_slot(layout_from, "script-panel") : NULL);
	gt_session_view_set_slot(view, "script-panel", script);	/* visibilité initiale (st) */
	sv->script_panel = script;
	g_object_add_weak_pointer(G_OBJECT(script), (gpointer *)&sv->script_panel);

	g_object_set_data(G_OBJECT(s), SESSION_PAGE_KEY, view);
	g_signal_connect(view, "destroy", G_CALLBACK(on_view_destroy), sv);
	return view;
}

GtSession *gt_session_view_get_session(GtkWidget *view)
{
	SessionView *sv = session_view_get(view);

	return sv != NULL ? sv->session : NULL;
}

GtkWidget *gt_session_view_lookup(GtSession *s)
{
	GtkWidget *view = s != NULL ? g_object_get_data(G_OBJECT(s), SESSION_PAGE_KEY) : NULL;

	return view != NULL && !gt_session_view_is_shut_down(view) ? view : NULL;
}

GtkWidget *gt_session_view_get_terminal_view(GtkWidget *view)
{
	return session_view_get(view)->terminal_view;
}

void gt_session_view_focus_terminal(GtkWidget *view)
{
	SessionView *sv = session_view_get(view);

	if(sv != NULL)
		gtk_widget_grab_focus(GTK_WIDGET(gt_terminal_view_get_vte(sv->terminal_view)));
}

static void box_set_child(GtkWidget *box, GtkWidget *child)
{
	GtkWidget *old;

	while((old = gtk_widget_get_first_child(box)) != NULL)
		gtk_box_remove(GTK_BOX(box), old);
	if(child != NULL)
		gtk_box_append(GTK_BOX(box), child);
}

void gt_session_view_set_slot(GtkWidget *view, const char *slot, GtkWidget *child)
{
	SessionView *sv = session_view_get(view);

	if(g_strcmp0(slot, "macro-panel") == 0)
	{
		gtk_paned_set_end_child(GTK_PANED(sv->h_paned), child);
		gtk_paned_set_resize_end_child(GTK_PANED(sv->h_paned), FALSE);
		gtk_paned_set_shrink_end_child(GTK_PANED(sv->h_paned), FALSE);
		if(child != NULL)
			gtk_widget_set_visible(child, sv->initial.macro_panel_visible);
	}
	else if(g_strcmp0(slot, "script-panel") == 0)
	{
		gtk_paned_set_end_child(GTK_PANED(sv->v_paned), child);
		gtk_paned_set_resize_end_child(GTK_PANED(sv->v_paned), FALSE);
		gtk_paned_set_shrink_end_child(GTK_PANED(sv->v_paned), TRUE);
		if(child != NULL)
		{
			gtk_widget_set_visible(child, sv->initial.script_panel_visible);
			/* Visible dès la création sans position enregistrée : hauteur vérifiée à la première allocation ; puis à chaque affichage */
			if(sv->initial.script_panel_visible && sv->initial.v_paned < 0)
				g_object_set_data(G_OBJECT(sv->v_paned), SCRIPT_FIT_KEY, GINT_TO_POINTER(1));
			g_signal_connect_object(child, "notify::visible", G_CALLBACK(on_script_panel_visible), sv->v_paned, 0);
		}
	}
	else if(g_strcmp0(slot, "hex-bar") == 0)
	{
		box_set_child(sv->hex_box, child);
		if(child != NULL)
			gtk_widget_set_visible(child, sv->initial.hex_bar_visible);
	}
	else if(g_strcmp0(slot, "status-bar") == 0)
		box_set_child(sv->status_box, child);
	else if(g_strcmp0(slot, "search-bar") == 0)
		box_set_child(sv->search_box, child);
	else
		g_critical("gt_session_view_set_slot: emplacement inconnu « %s »", slot);
}

GtkWidget *gt_session_view_get_slot(GtkWidget *view, const char *slot)
{
	SessionView *sv = session_view_get(view);

	if(g_strcmp0(slot, "macro-panel") == 0)
		return gtk_paned_get_end_child(GTK_PANED(sv->h_paned));
	if(g_strcmp0(slot, "script-panel") == 0)
		return gtk_paned_get_end_child(GTK_PANED(sv->v_paned));
	if(g_strcmp0(slot, "hex-bar") == 0)
		return gtk_widget_get_first_child(sv->hex_box);
	if(g_strcmp0(slot, "status-bar") == 0)
		return gtk_widget_get_first_child(sv->status_box);
	if(g_strcmp0(slot, "search-bar") == 0)
		return gtk_widget_get_first_child(sv->search_box);
	g_critical("gt_session_view_get_slot: emplacement inconnu « %s »", slot);
	return NULL;
}

void gt_session_view_get_state(GtkWidget *view, GtWindowState *out)
{
	SessionView *sv = session_view_get(view);
	GtkWidget *macro = gt_session_view_get_slot(view, "macro-panel");
	GtkWidget *script = gt_session_view_get_slot(view, "script-panel");
	GtkWidget *hex = gt_session_view_get_slot(view, "hex-bar");

	out->h_paned = sv->initial.h_paned;
	out->v_paned = sv->initial.v_paned;
	out->macro_panel_visible = sv->initial.macro_panel_visible;
	out->script_panel_visible = sv->initial.script_panel_visible;
	out->hex_bar_visible = sv->initial.hex_bar_visible;
	if(macro != NULL)
	{
		out->h_paned = gtk_paned_get_position(GTK_PANED(sv->h_paned));
		out->macro_panel_visible = gtk_widget_get_visible(macro);
	}
	if(script != NULL)
	{
		out->v_paned = gtk_paned_get_position(GTK_PANED(sv->v_paned));
		out->script_panel_visible = gtk_widget_get_visible(script);
	}
	if(hex != NULL)
		out->hex_bar_visible = gtk_widget_get_visible(hex);
}

void gt_session_view_fill_project_tab(GtkWidget *view, GtProjectTab *tab)
{
	GtkWidget *script;

	g_return_if_fail(tab != NULL);
	gt_session_view_get_state(view, &tab->page);
	script = gt_session_view_get_slot(view, "script-panel");
	g_free(tab->script_layout);
	tab->script_layout = g_strdup(gt_script_panel_get_layout(script));
}

void gt_session_view_apply_project_tab(GtkWidget *view, const GtProjectTab *tab)
{
	GtkWidget *script = gt_session_view_get_slot(view, "script-panel");

	if(script != NULL && tab != NULL && tab->script_layout != NULL)
		gt_script_panel_set_layout(script, tab->script_layout);
}

void gt_session_view_apply_profile(GtkWidget *view)
{
	SessionView *sv = session_view_get(view);

	if(sv == NULL || sv->shut_down)
		return;
	gt_terminal_view_apply_profile(sv->terminal_view, gt_session_get_profile(sv->session));
	gt_script_panel_refresh_colors(sv->script_panel);
}

void gt_session_view_shutdown(GtkWidget *view)
{
	SessionView *sv = session_view_get(view);
	GActionMap *tab_actions;

	if(sv == NULL || sv->shut_down)
		return;
	sv->shut_down = TRUE;
	tab_actions = G_ACTION_MAP(gt_session_get_actions(sv->session));
	gt_file_actions_shutdown(view);	/* données attachées à la page : envoi annulé sans message (file-sending rendu), journal arrêté, actions retirées */
	/* Panneaux par pointeurs faibles (NULL : déjà libérés, leurs données avec eux) ; raccourcis : données attachées à la page elle-même */
	gt_macro_panel_shutdown(sv->macro_panel);	/* minuterie, popovers, show-macro-panel retirée */
	gt_macro_shortcuts_detach(view);
	gt_script_panel_shutdown(sv->script_panel);	/* journal et fin du moteur débranchés, show-script-panel et script-layout retirées */
	if(sv->status_bar != NULL)	/* pointeur faible : déjà libérée si la page est en destruction (elle s'est alors débranchée seule) */
		gt_status_bar_detach(sv->status_bar);	/* la fermeture de la liaison ne touche plus la page */
	/* Actions de page retirées avant l'arrêt de la session : les GPropertyAction gardent leurs widgets en vie, et « copy » ne peut plus être
	 * réactivée par une sélection après gt_session_shutdown */
	for(gsize i = 0; i < G_N_ELEMENTS(own_action_names); i++)
		g_action_map_remove_action(tab_actions, own_action_names[i]);
	gt_app_actions_uninstall(tab_actions);
}

gboolean gt_session_view_is_shut_down(GtkWidget *view)
{
	SessionView *sv = session_view_get(view);

	return sv == NULL || sv->shut_down;
}
