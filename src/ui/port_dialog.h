/* Dialogue Port (§6.4) : action app.port, raccourci Ctrl+Maj+S. */
#ifndef GT_UI_PORT_DIALOG_H_
#define GT_UI_PORT_DIALOG_H_

#include <gtk/gtk.h>

#include "profile.h"

void gt_port_dialog_install(GtkApplication *app);
/* Réglage de la session active ; instance unique : une seconde demande (même en création) ramène le dialogue ouvert, tel quel (C12) */
void gt_port_dialog_show(GtkWindow *parent);
/* Création d'onglet (6b, décision 7) : même dialogue (mêmes champs, même validation, même instance unique), titre « New tab », prérempli d'une
 * copie de initial ; OK applique les champs à cette copie, ferme le dialogue puis appelle cb(profil, user_data) (profil à copier, libéré au
 * retour) ; Annuler ferme sans rappel. */
typedef void (*GtPortDialogNewCb)(const GtProfile *profile, gpointer user_data);
void gt_port_dialog_show_new(GtkWindow *parent, const GtProfile *initial, GtPortDialogNewCb cb, gpointer user_data);

#endif
