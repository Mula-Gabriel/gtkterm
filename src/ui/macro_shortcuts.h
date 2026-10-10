/* macro_shortcuts.h — raccourcis clavier des macros (interface GTK 4). */

#ifndef GT_UI_MACRO_SHORTCUTS_H_
#define GT_UI_MACRO_SHORTCUTS_H_

#include <gtk/gtk.h>

void gt_macro_shortcuts_attach(GtkWidget *main_window);	/* contrôleur global en phase de capture, rempli depuis le jeu courant ; une seule fois */
void gt_macro_shortcuts_rebuild(void);	/* crochet macros_changed ; avant attach ou après detach : rien */
void gt_macro_shortcuts_detach(void);	/* fermeture : plus aucune entrée (le contrôleur part avec la fenêtre) */

#endif
