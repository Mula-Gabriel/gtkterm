/* Fenêtre principale GTK 4 : voir main_window.h. */
#include <config.h>
#include <glib/gi18n.h>

#include "main_window.h"
#include "session_view.h"

#define MAIN_WINDOW_KEY "gt-main-window"

typedef struct
{
	GtWindowState initial;	/* lu au démarrage */
	GtkWidget *notebook;	/* « tabs » */
} MainWindow;

static MainWindow *main_window_get(GtkWidget *win)
{
	return g_object_get_data(G_OBJECT(win), MAIN_WINDOW_KEY);
}

/* Barre d'onglets seulement à partir de deux pages */
static void update_show_tabs(GtkNotebook *nb, GtkWidget *child, guint page_num, gpointer user_data)
{
	gtk_notebook_set_show_tabs(nb, gtk_notebook_get_n_pages(nb) >= 2);
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

	mw->notebook = gtk_notebook_new();
	gtk_widget_set_name(mw->notebook, "tabs");
	gtk_widget_set_hexpand(mw->notebook, TRUE);
	gtk_widget_set_vexpand(mw->notebook, TRUE);
	gtk_notebook_set_scrollable(GTK_NOTEBOOK(mw->notebook), TRUE);
	gtk_notebook_set_show_border(GTK_NOTEBOOK(mw->notebook), FALSE);
	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(mw->notebook), FALSE);
	g_signal_connect(mw->notebook, "page-added", G_CALLBACK(update_show_tabs), NULL);
	g_signal_connect(mw->notebook, "page-removed", G_CALLBACK(update_show_tabs), NULL);
	gtk_box_append(GTK_BOX(box), mw->notebook);
	gtk_window_set_child(GTK_WINDOW(win), box);

	/* État de [window] : tailles aberrantes déjà écartées par app_config_load_window_state ; x et y ne sont plus lus */
	app_config_load_window_state(&mw->initial);
	gtk_window_set_default_size(GTK_WINDOW(win), mw->initial.width, mw->initial.height);
	if(mw->initial.maximized)
		gtk_window_maximize(GTK_WINDOW(win));
	return win;
}

GtkNotebook *gt_main_window_get_notebook(GtkWidget *win)
{
	return GTK_NOTEBOOK(main_window_get(win)->notebook);
}

void gt_main_window_insert_view(GtkWidget *win, GtkWidget *view, GtkWidget *tab_label, gint position)
{
	GtkNotebook *nb = gt_main_window_get_notebook(win);

	gtk_notebook_insert_page(nb, view, tab_label, position);
	gtk_notebook_set_tab_reorderable(nb, view, TRUE);
}

void gt_main_window_remove_view(GtkWidget *win, GtkWidget *view)
{
	GtkNotebook *nb = gt_main_window_get_notebook(win);
	gint n = gtk_notebook_page_num(nb, view);

	if(n >= 0)
		gtk_notebook_remove_page(nb, n);
}

GtkWidget *gt_main_window_get_active_view(GtkWidget *win)
{
	GtkNotebook *nb = gt_main_window_get_notebook(win);
	gint n = gtk_notebook_get_current_page(nb);

	return n >= 0 ? gtk_notebook_get_nth_page(nb, n) : NULL;
}

void gt_main_window_get_state(GtkWidget *win, GtWindowState *out)
{
	MainWindow *mw = main_window_get(win);
	GtkWidget *view = gt_main_window_get_active_view(win);
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
	if(view != NULL)
		gt_session_view_get_state(view, out);
}
