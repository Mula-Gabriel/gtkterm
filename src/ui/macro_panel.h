/* macro_panel.h — panneau des boutons de macros (interface GTK 4). Un seul panneau (fenêtre principale). */

#ifndef GT_UI_MACRO_PANEL_H_
#define GT_UI_MACRO_PANEL_H_

#include <gtk/gtk.h>

/* Crée le panneau « macro-panel » sur le jeu courant, le met dans le slot "macro-panel" de la fenêtre, applique sa visibilité de [window]
 * et ajoute l'action app.show-macro-panel. Une seule fois. */
void gt_macro_panel_install(GtkApplication *app, GtkWidget *main_window);
void gt_macro_panel_rebuild(void);	/* crochet macros_changed ; sans panneau : rien */
void gt_macro_panel_polling_changed(guint id, gboolean running);	/* crochet macro_polling_changed ; sans panneau : rien */
void gt_macro_panel_shutdown(void);	/* fermeture, avant macros_shutdown() : minuterie retirée, popovers détachés, plus de rappel vers le modèle */

#endif
