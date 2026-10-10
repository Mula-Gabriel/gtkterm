/* Boîte À propos GTK 4 (action app.about) : standard, sans URL ; n'écrit jamais rien dans la configuration. */

#ifndef GT_UI_ABOUT_H_
#define GT_UI_ABOUT_H_

#include <gtk/gtk.h>

void gt_about_show(GtkWindow *parent);	/* une seule instance, présentée de nouveau si elle est ouverte */

#endif /* GT_UI_ABOUT_H_ */
