/***********************************************************************/
/* app_actions.c                                                       */
/*                                                                     */
/*   Purpose                                                           */
/*      Actions de page du terminal (groupe de la session) et          */
/*      raccourcis des actions app.* d'onglet (sous-projets 6a, 6b)    */
/*                                                                     */
/***********************************************************************/

#include <config.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <vte/vte.h>

#include "app_actions.h"
#include "terminal_view.h"

/* Actions de page d'un terminal : rangées comme donnée du groupe de la session (aucune statique d'onglet) ; libérées par le retrait
 * (gt_app_actions_uninstall) ou avec le groupe */
#define PAGE_ACTIONS_KEY "gt-app-actions"

typedef struct
{
	GtkWidget *view;	/* vue du terminal de la page (pointeur faible) */
	VteTerminal *vte;	/* son VteTerminal (pointeur faible) */
	gulong selection_handler;
	GSimpleAction *copy;	/* non possédée : le groupe la tient tant que cette structure vit */
} PageActions;

static const gchar *const page_action_names[] = { "clear-scrollback", "copy", "paste", "select-all" };

static void update_copy(PageActions *pa)
{
	g_simple_action_set_enabled(pa->copy, pa->vte != NULL && vte_terminal_get_has_selection(pa->vte));
}

static void on_selection_changed(VteTerminal *vte, gpointer user_data)
{
	update_copy(user_data);
}

/* Ne vide que le défilement du terminal : un réaffichage le fait revenir depuis l'historique (spec §3.2-11) */
static void on_clear_scrollback(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	PageActions *pa = user_data;

	if(pa->view != NULL)
		gt_terminal_view_clear_scrollback(pa->view);
}

static void on_copy(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	PageActions *pa = user_data;

	if(pa->vte != NULL && vte_terminal_get_has_selection(pa->vte))
		vte_terminal_copy_clipboard_format(pa->vte, VTE_FORMAT_TEXT);
}

/* Le texte collé revient par « commit » et part sur le port comme une frappe */
static void on_paste(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	PageActions *pa = user_data;

	if(pa->vte != NULL)
		vte_terminal_paste_clipboard(pa->vte);
}

static void on_select_all(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	PageActions *pa = user_data;

	if(pa->vte != NULL)
		vte_terminal_select_all(pa->vte);
}

static const GActionEntry entries[] =
{
	{ .name = "clear-scrollback", .activate = on_clear_scrollback },
	{ .name = "copy", .activate = on_copy },
	{ .name = "paste", .activate = on_paste },
	{ .name = "select-all", .activate = on_select_all },
};

/* Raccourcis de 6a, inchangés (actions de session et de page, toutes relayées en app.*) */
static const struct
{
	const gchar *action;
	const gchar *accels[2];
} accels[] =
{
	{ "app.clear-screen", { "<Control><Shift>l", NULL } },
	{ "app.clear-scrollback", { "<Control><Shift>k", NULL } },
	{ "app.copy", { "<Control><Shift>c", NULL } },
	{ "app.paste", { "<Control><Shift>v", NULL } },
	{ "app.select-all", { "<Control><Shift>a", NULL } },
	{ "app.send-break", { "<Control><Shift>b", NULL } },	/* plus de Ctrl+B : STX doit pouvoir partir sur le port (spec §3.2-8) */
	{ "app.open-port", { "F5", NULL } },
	{ "app.close-port", { "F6", NULL } },
	{ "app.toggle-dtr", { "F7", NULL } },
	{ "app.toggle-rts", { "F8", NULL } },
	{ "app.find", { "<Control><Shift>f", NULL } },
	{ "app.send-file", { "<Control><Shift>r", NULL } },
};

void gt_app_actions_set_accels(GtkApplication *app)
{
	for(gsize i = 0; i < G_N_ELEMENTS(accels); i++)
		gtk_application_set_accels_for_action(app, accels[i].action, accels[i].accels);
}

/* Destruction (retrait ou groupe finalisé) : plus aucun lien vers le terminal */
static void page_actions_free(gpointer data)
{
	PageActions *pa = data;

	if(pa->vte != NULL)
	{
		g_clear_signal_handler(&pa->selection_handler, pa->vte);
		g_object_remove_weak_pointer(G_OBJECT(pa->vte), (gpointer *)&pa->vte);
	}
	if(pa->view != NULL)
		g_object_remove_weak_pointer(G_OBJECT(pa->view), (gpointer *)&pa->view);
	g_free(pa);
}

void gt_app_actions_install(GActionMap *tab_actions, GtkWidget *terminal_view)
{
	PageActions *pa;
	GtkBuilder *builder;

	g_return_if_fail(G_IS_ACTION_MAP(tab_actions) && GTK_IS_WIDGET(terminal_view));
	g_return_if_fail(g_object_get_data(G_OBJECT(tab_actions), PAGE_ACTIONS_KEY) == NULL);

	pa = g_new0(PageActions, 1);
	pa->view = terminal_view;
	g_object_add_weak_pointer(G_OBJECT(pa->view), (gpointer *)&pa->view);
	pa->vte = gt_terminal_view_get_vte(terminal_view);
	g_object_add_weak_pointer(G_OBJECT(pa->vte), (gpointer *)&pa->vte);
	g_action_map_add_action_entries(tab_actions, entries, G_N_ELEMENTS(entries), pa);
	pa->copy = G_SIMPLE_ACTION(g_action_map_lookup_action(tab_actions, "copy"));
	g_object_set_data_full(G_OBJECT(tab_actions), PAGE_ACTIONS_KEY, pa, page_actions_free);
	pa->selection_handler = g_signal_connect(pa->vte, "selection-changed", G_CALLBACK(on_selection_changed), pa);

	/* menus.ui n'est pas chargé d'office par GtkApplication (ressource sous …/ui/) : lu ici, comme la barre de menus ; le VTE garde le modèle */
	builder = gtk_builder_new_from_resource("/eu/mulagabriel/gtkterm/ui/menus.ui");
	vte_terminal_set_context_menu_model(pa->vte, G_MENU_MODEL(gtk_builder_get_object(builder, "terminal-context")));
	g_object_unref(builder);
	update_copy(pa);
}

void gt_app_actions_uninstall(GActionMap *tab_actions)
{
	if(g_object_get_data(G_OBJECT(tab_actions), PAGE_ACTIONS_KEY) == NULL)
		return;
	/* Actions retirées d'abord (elles ne peuvent plus être activées), puis la structure qu'elles désignent */
	for(gsize i = 0; i < G_N_ELEMENTS(page_action_names); i++)
		g_action_map_remove_action(tab_actions, page_action_names[i]);
	g_object_set_data(G_OBJECT(tab_actions), PAGE_ACTIONS_KEY, NULL);
}
