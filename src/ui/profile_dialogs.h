/* Profils (charger, enregistrer, supprimer) et fichier de macros (charger, enregistrer) : actions app.load-profile, app.save-profile,
 * app.delete-profile, app.load-macros, app.save-macros (sous-projet 6a, §6.4). Fil principal ; aucune boucle imbriquée. */
#ifndef GT_UI_PROFILE_DIALOGS_H_
#define GT_UI_PROFILE_DIALOGS_H_

#include <gtk/gtk.h>

void gt_profile_dialogs_install(GtkApplication *app);

#endif
