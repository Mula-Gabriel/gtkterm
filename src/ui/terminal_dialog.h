/* Dialogue « Terminal » (menu Configuration › Main window, action app.terminal). */
#ifndef GT_UI_TERMINAL_DIALOG_H_
#define GT_UI_TERMINAL_DIALOG_H_

#include <gtk/gtk.h>

void gt_terminal_dialog_install(GtkApplication *app);
void gt_terminal_dialog_show(GtkWindow *parent);	/* instance unique : une seconde demande la ramène au premier plan */

#endif
