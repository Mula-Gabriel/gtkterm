/***********************************************************************/
/* app_actions.c                                                       */
/*                                                                     */
/*   Purpose                                                           */
/*      Actions app.* d'options, de vue, de liaison et d'édition       */
/*      (sous-projet 6a)                                               */
/*                                                                     */
/***********************************************************************/

#include <config.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <vte/vte.h>

#include "app.h"
#include "app_actions.h"
#include "app_ui.h"
#include "terminal_view.h"

typedef struct
{
	GtkApplication *app;	/* non possédée */
	GtkTermLink *link;	/* référence */
	GtTermOptions *opts;	/* référence */
	GtkWidget *view;	/* vue du terminal de la fenêtre principale (pointeur faible) */
	VteTerminal *vte;	/* son VteTerminal (pointeur faible) */
	gulong selection_handler;
	GMenuModel *context;	/* menu « terminal-context » de menus.ui (référence) */
} ActionsState;

static ActionsState st;

static void action_set_enabled(const gchar *name, gboolean enabled)
{
	GAction *a;

	if(st.app == NULL)
		return;
	a = g_action_map_lookup_action(G_ACTION_MAP(st.app), name);
	if(a != NULL)
		g_simple_action_set_enabled(G_SIMPLE_ACTION(a), enabled);
}

/* Send break, DTR, RTS : seulement sur une liaison série ouverte (d'après la liaison, plus d'après le profil) */
static void update_serial_actions(void)
{
	gboolean serial = st.link != NULL && gt_link_is_serial_open(st.link);

	action_set_enabled("send-break", serial);
	action_set_enabled("toggle-dtr", serial);
	action_set_enabled("toggle-rts", serial);
}

static void update_copy(void)
{
	action_set_enabled("copy", st.vte != NULL && vte_terminal_get_has_selection(st.vte));
}

/* notify::state et notify::status-text : peut tourner sous la surveillance du transport, ne fait que changer des sensibilités */
static void on_link_notify(GObject *object, GParamSpec *pspec, gpointer user_data)
{
	update_serial_actions();
}

static void on_selection_changed(VteTerminal *vte, gpointer user_data)
{
	update_copy();
}

static void on_clear_screen(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	gt_link_clear_history(st.link);
}

/* Ne vide que le défilement du terminal : un réaffichage le fait revenir depuis l'historique (spec §3.2-11) */
static void on_clear_scrollback(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	if(st.view != NULL)
		gt_terminal_view_clear_scrollback(st.view);
}

static void on_copy(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	if(st.vte != NULL && vte_terminal_get_has_selection(st.vte))
		vte_terminal_copy_clipboard_format(st.vte, VTE_FORMAT_TEXT);
}

/* Le texte collé revient par « commit » et part sur le port comme une frappe */
static void on_paste(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	if(st.vte != NULL)
		vte_terminal_paste_clipboard(st.vte);
}

static void on_select_all(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	if(st.vte != NULL)
		vte_terminal_select_all(st.vte);
}

static void on_open_port(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	gt_app_open_port();
}

static void on_close_port(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	gt_app_close_port();
}

static void on_send_break(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	if(!gt_link_is_serial_open(st.link))
		return;
	gt_link_send_break(st.link);
	gt_app_status_flash(800, "%s", _("Break signal sent!"));
}

static void on_toggle_dtr(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	if(gt_link_is_serial_open(st.link))
		gt_link_toggle_line(st.link, GT_MODEM_DTR);
}

static void on_toggle_rts(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	if(gt_link_is_serial_open(st.link))
		gt_link_toggle_line(st.link, GT_MODEM_RTS);
}

static const GActionEntry entries[] =
{
	{ "clear-screen", on_clear_screen, NULL, NULL, NULL, { 0 } },
	{ "clear-scrollback", on_clear_scrollback, NULL, NULL, NULL, { 0 } },
	{ "copy", on_copy, NULL, NULL, NULL, { 0 } },
	{ "paste", on_paste, NULL, NULL, NULL, { 0 } },
	{ "select-all", on_select_all, NULL, NULL, NULL, { 0 } },
	{ "open-port", on_open_port, NULL, NULL, NULL, { 0 } },
	{ "close-port", on_close_port, NULL, NULL, NULL, { 0 } },
	{ "send-break", on_send_break, NULL, NULL, NULL, { 0 } },
	{ "toggle-dtr", on_toggle_dtr, NULL, NULL, NULL, { 0 } },
	{ "toggle-rts", on_toggle_rts, NULL, NULL, NULL, { 0 } },
};

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
};

static void add_property_action(GtkApplication *app, const gchar *name, gpointer object, const gchar *property)
{
	GPropertyAction *action = g_property_action_new(name, object, property);

	g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(action));
	g_object_unref(action);
}

void gt_app_actions_install(GtkApplication *app, GtkTermLink *link, GtTermOptions *opts)
{
	g_return_if_fail(GTK_IS_APPLICATION(app) && GT_IS_LINK(link) && GT_IS_TERM_OPTIONS(opts));

	st.app = app;
	st.link = g_object_ref(link);
	st.opts = g_object_ref(opts);

	add_property_action(app, "local-echo", opts, "echo");
	add_property_action(app, "crlf-auto", opts, "crlf-auto");
	add_property_action(app, "timestamp", opts, "timestamp");
	add_property_action(app, "esc-clear-screen", opts, "esc-clear-screen");
	add_property_action(app, "hex-index", opts, "hex-show-index");
	add_property_action(app, "view-mode", opts, "view-mode");	/* état chaîne : nick « ascii » / « hex » */
	add_property_action(app, "hex-bytes", opts, "hex-bytes-per-line");	/* état « u » */
	add_property_action(app, "auto-reconnect", link, "auto-reconnect");

	g_action_map_add_action_entries(G_ACTION_MAP(app), entries, G_N_ELEMENTS(entries), NULL);
	for(gsize i = 0; i < G_N_ELEMENTS(accels); i++)
		gtk_application_set_accels_for_action(app, accels[i].action, accels[i].accels);

	g_signal_connect(link, "notify::state", G_CALLBACK(on_link_notify), NULL);
	g_signal_connect(link, "notify::status-text", G_CALLBACK(on_link_notify), NULL);
	update_serial_actions();
	update_copy();
}

void gt_app_actions_attach_terminal(GtkApplication *app, GtkWidget *terminal_view)
{
	/* menus.ui n'est pas chargé d'office par GtkApplication (ressource sous …/ui/) : lu ici une fois, comme la barre de menus */
	if(st.context == NULL)
	{
		GtkBuilder *builder = gtk_builder_new_from_resource("/eu/mulagabriel/gtkterm/ui/menus.ui");

		st.context = g_object_ref(G_MENU_MODEL(gtk_builder_get_object(builder, "terminal-context")));
		g_object_unref(builder);
	}
	gt_app_actions_detach_terminal();
	st.view = terminal_view;
	g_object_add_weak_pointer(G_OBJECT(st.view), (gpointer *)&st.view);
	st.vte = gt_terminal_view_get_vte(terminal_view);
	g_object_add_weak_pointer(G_OBJECT(st.vte), (gpointer *)&st.vte);
	st.selection_handler = g_signal_connect(st.vte, "selection-changed", G_CALLBACK(on_selection_changed), NULL);
	vte_terminal_set_context_menu_model(st.vte, st.context);
	update_copy();
}

void gt_app_actions_detach_terminal(void)
{
	if(st.vte != NULL)
	{
		g_clear_signal_handler(&st.selection_handler, st.vte);
		g_object_remove_weak_pointer(G_OBJECT(st.vte), (gpointer *)&st.vte);
		st.vte = NULL;
	}
	st.selection_handler = 0;
	if(st.view != NULL)
	{
		g_object_remove_weak_pointer(G_OBJECT(st.view), (gpointer *)&st.view);
		st.view = NULL;
	}
	update_copy();
}

void gt_app_actions_shutdown(void)
{
	gt_app_actions_detach_terminal();
	if(st.link != NULL)
		g_signal_handlers_disconnect_by_func(st.link, on_link_notify, NULL);
	g_clear_object(&st.link);
	g_clear_object(&st.opts);
	g_clear_object(&st.context);
	st.app = NULL;
}
