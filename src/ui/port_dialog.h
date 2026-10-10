/* Dialogue Port (§6.4) : action app.port, raccourci Ctrl+Maj+S. */
#ifndef GT_UI_PORT_DIALOG_H_
#define GT_UI_PORT_DIALOG_H_

#include <gtk/gtk.h>

void gt_port_dialog_install(GtkApplication *app);
void gt_port_dialog_show(GtkWindow *parent);	/* instance unique : une seconde demande la ramène au premier plan (C12) */

#endif
