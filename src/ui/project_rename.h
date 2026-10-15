/* Fenêtre « Rename project… » (spec nom de projet §1.2) : une seule instance modale. Fil principal ; aucune boucle imbriquée. */
#ifndef GT_UI_PROJECT_RENAME_H_
#define GT_UI_PROJECT_RENAME_H_

#include <gtk/gtk.h>
#include "project.h"

void gt_project_rename_show(GtkWindow *parent, GtProject *project);

#endif
