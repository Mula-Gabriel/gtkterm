/* Fenêtre principale GTK 4 : voir main_window.h. */
#include <config.h>
#include <glib/gi18n.h>

#include "app.h"
#include "main_window.h"
#include "terminal_view.h"

#define MAIN_WINDOW_KEY "gt-main-window"

typedef struct
{
	GtWindowState initial;	/* lu au démarrage : valeurs des emplacements encore vides */
	GtkWidget *terminal_view;
	GtkWidget *h_paned;	/* terminal | panneau de macros */
	GtkWidget *v_paned;	/* (terminal | macros) / panneau de script */
	GtkWidget *search_box, *hex_box, *status_box;	/* conteneurs des emplacements de la boîte principale */
} MainWindow;

static MainWindow *main_window_get(GtkWidget *win)
{
	return g_object_get_data(G_OBJECT(win), MAIN_WINDOW_KEY);
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

GtkWidget *gt_main_window_new(GtkApplication *app)
{
	MainWindow *mw = g_new0(MainWindow, 1);
	GtkWidget *win = gtk_application_window_new(app);
	GtkBuilder *builder = gtk_builder_new_from_resource("/eu/mulagabriel/gtkterm/ui/menus.ui");
	GtkWidget *menubar = gtk_popover_menu_bar_new_from_model(G_MENU_MODEL(gtk_builder_get_object(builder, "menubar")));
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	g_object_set_data_full(G_OBJECT(win), MAIN_WINDOW_KEY, mw, g_free);
	gtk_widget_set_name(win, "main-window");
	gtk_window_set_title(GTK_WINDOW(win), "GTKTerm");	/* remplacé dès « activate » par la liaison du titre à status-text */
	gtk_application_window_set_show_menubar(GTK_APPLICATION_WINDOW(win), FALSE);
	gtk_widget_set_name(menubar, "menubar");
	gtk_box_append(GTK_BOX(box), menubar);
	g_object_unref(builder);

	mw->search_box = slot_box();
	gtk_box_append(GTK_BOX(box), mw->search_box);

	mw->terminal_view = gt_terminal_view_new(gt_app_get_link(), gt_app_get_options());
	mw->h_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_paned_set_start_child(GTK_PANED(mw->h_paned), mw->terminal_view);
	gtk_paned_set_resize_start_child(GTK_PANED(mw->h_paned), TRUE);
	gtk_paned_set_shrink_start_child(GTK_PANED(mw->h_paned), FALSE);
	mw->v_paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
	gtk_paned_set_start_child(GTK_PANED(mw->v_paned), mw->h_paned);
	gtk_paned_set_resize_start_child(GTK_PANED(mw->v_paned), TRUE);
	gtk_paned_set_shrink_start_child(GTK_PANED(mw->v_paned), FALSE);
	gtk_widget_set_vexpand(mw->v_paned, TRUE);
	gtk_box_append(GTK_BOX(box), mw->v_paned);

	mw->hex_box = slot_box();
	gtk_box_append(GTK_BOX(box), mw->hex_box);
	mw->status_box = slot_box();
	gtk_box_append(GTK_BOX(box), mw->status_box);
	gtk_window_set_child(GTK_WINDOW(win), box);

	/* État de [window] : tailles et séparateurs aberrants déjà écartés par app_config_load_window_state ; x et y ne sont plus lus */
	app_config_load_window_state(&mw->initial);
	gtk_window_set_default_size(GTK_WINDOW(win), mw->initial.width, mw->initial.height);
	if(mw->initial.maximized)
		gtk_window_maximize(GTK_WINDOW(win));
	if(mw->initial.h_paned >= 0)
		gtk_paned_set_position(GTK_PANED(mw->h_paned), mw->initial.h_paned);
	else
		g_signal_connect(mw->h_paned, "notify::max-position", G_CALLBACK(on_h_paned_max_position), NULL);
	if(mw->initial.v_paned >= 0)
		gtk_paned_set_position(GTK_PANED(mw->v_paned), mw->initial.v_paned);

	/* La frappe part sur le port dès l'ouverture */
	gtk_window_set_focus(GTK_WINDOW(win), GTK_WIDGET(gt_terminal_view_get_vte(mw->terminal_view)));
	return win;
}

GtkWidget *gt_main_window_get_terminal_view(GtkWidget *win)
{
	return main_window_get(win)->terminal_view;
}

void gt_main_window_focus_terminal(GtkWidget *win)
{
	gtk_widget_grab_focus(GTK_WIDGET(gt_terminal_view_get_vte(gt_main_window_get_terminal_view(win))));
}

static void box_set_child(GtkWidget *box, GtkWidget *child)
{
	GtkWidget *old;

	while((old = gtk_widget_get_first_child(box)) != NULL)
		gtk_box_remove(GTK_BOX(box), old);
	if(child != NULL)
		gtk_box_append(GTK_BOX(box), child);
}

void gt_main_window_set_slot(GtkWidget *win, const char *slot, GtkWidget *child)
{
	MainWindow *mw = main_window_get(win);

	if(g_strcmp0(slot, "macro-panel") == 0)
	{
		gtk_paned_set_end_child(GTK_PANED(mw->h_paned), child);
		gtk_paned_set_resize_end_child(GTK_PANED(mw->h_paned), FALSE);
		gtk_paned_set_shrink_end_child(GTK_PANED(mw->h_paned), FALSE);
		if(child != NULL)
			gtk_widget_set_visible(child, mw->initial.macro_panel_visible);
	}
	else if(g_strcmp0(slot, "script-panel") == 0)
	{
		gtk_paned_set_end_child(GTK_PANED(mw->v_paned), child);
		gtk_paned_set_resize_end_child(GTK_PANED(mw->v_paned), FALSE);
		gtk_paned_set_shrink_end_child(GTK_PANED(mw->v_paned), TRUE);
		if(child != NULL)
			gtk_widget_set_visible(child, mw->initial.script_panel_visible);
	}
	else if(g_strcmp0(slot, "hex-bar") == 0)
	{
		box_set_child(mw->hex_box, child);
		if(child != NULL)
			gtk_widget_set_visible(child, mw->initial.hex_bar_visible);
	}
	else if(g_strcmp0(slot, "status-bar") == 0)
		box_set_child(mw->status_box, child);
	else if(g_strcmp0(slot, "search-bar") == 0)
		box_set_child(mw->search_box, child);
	else
		g_critical("gt_main_window_set_slot: emplacement inconnu « %s »", slot);
}

GtkWidget *gt_main_window_get_slot(GtkWidget *win, const char *slot)
{
	MainWindow *mw = main_window_get(win);

	if(g_strcmp0(slot, "macro-panel") == 0)
		return gtk_paned_get_end_child(GTK_PANED(mw->h_paned));
	if(g_strcmp0(slot, "script-panel") == 0)
		return gtk_paned_get_end_child(GTK_PANED(mw->v_paned));
	if(g_strcmp0(slot, "hex-bar") == 0)
		return gtk_widget_get_first_child(mw->hex_box);
	if(g_strcmp0(slot, "status-bar") == 0)
		return gtk_widget_get_first_child(mw->status_box);
	if(g_strcmp0(slot, "search-bar") == 0)
		return gtk_widget_get_first_child(mw->search_box);
	g_critical("gt_main_window_get_slot: emplacement inconnu « %s »", slot);
	return NULL;
}

void gt_main_window_get_state(GtkWidget *win, GtWindowState *out)
{
	MainWindow *mw = main_window_get(win);
	GtkWidget *macro = gt_main_window_get_slot(win, "macro-panel");
	GtkWidget *script = gt_main_window_get_slot(win, "script-panel");
	GtkWidget *hex = gt_main_window_get_slot(win, "hex-bar");
	gint w = 0, h = 0;

	*out = mw->initial;
	/* GTK 4 tient la taille par défaut à jour quand l'utilisateur redimensionne (taille hors maximisation) */
	gtk_window_get_default_size(GTK_WINDOW(win), &w, &h);
	if(w > 0 && h > 0)
	{
		out->width = w;
		out->height = h;
	}
	out->maximized = gtk_window_is_maximized(GTK_WINDOW(win));
	if(macro != NULL)
	{
		out->h_paned = gtk_paned_get_position(GTK_PANED(mw->h_paned));
		out->macro_panel_visible = gtk_widget_get_visible(macro);
	}
	if(script != NULL)
	{
		out->v_paned = gtk_paned_get_position(GTK_PANED(mw->v_paned));
		out->script_panel_visible = gtk_widget_get_visible(script);
	}
	if(hex != NULL)
		out->hex_bar_visible = gtk_widget_get_visible(hex);
}
