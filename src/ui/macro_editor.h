/* macro_editor.h — fenêtre « Configure Macros » (interface GTK 4). */

#ifndef GT_UI_MACRO_EDITOR_H_
#define GT_UI_MACRO_EDITOR_H_

#include <gtk/gtk.h>

void gt_macro_editor_install_action(GtkApplication *app);	/* action app.macros */
void gt_macro_editor_open(GtkWindow *parent);	/* instance unique : une seconde demande présente la fenêtre ouverte */
void gt_macro_editor_macros_changed(void);	/* crochet macros_changed : jeu remplacé pendant l'édition → fermeture sans enregistrer */

#endif
