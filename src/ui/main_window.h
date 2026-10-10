/* Fenêtre principale GTK 4 (spec §6.1, onglets 6b §3.1-8) : barre de menus (menus.ui) + GtkNotebook « tabs » dont chaque page est une page
 * d'onglet (session_view.c). La barre d'onglets n'est montrée qu'à partir de deux pages. */
#ifndef GT_UI_MAIN_WINDOW_H_
#define GT_UI_MAIN_WINDOW_H_

#include <gtk/gtk.h>

#include "app_config.h"

G_BEGIN_DECLS

/* GtkApplicationWindow nommée « main-window » ; taille et maximisation restaurées depuis app_config_load_window_state */
GtkWidget   *gt_main_window_new(GtkApplication *app);
GtkNotebook *gt_main_window_get_notebook(GtkWidget *win);
/* Page insérée à position (< 0 : à la fin), onglet réordonnable */
void         gt_main_window_insert_view(GtkWidget *win, GtkWidget *view, GtkWidget *tab_label, gint position);
void         gt_main_window_remove_view(GtkWidget *win, GtkWidget *view);
GtkWidget   *gt_main_window_get_active_view(GtkWidget *win);	/* page affichée, NULL sans page */
/* État à enregistrer : taille et maximisation de la fenêtre ; séparateurs et visibilités de la page active (gt_session_view_get_state),
 * valeurs lues au démarrage sans page */
void         gt_main_window_get_state(GtkWidget *win, GtWindowState *out);

G_END_DECLS

#endif
