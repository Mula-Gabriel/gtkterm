/* Enregistrement des macros d'une session depuis l'interface (6c, spec §3.2-15) : onglet de projet → rien d'écrit (gt_macros_edited) ; onglet
 * libre → fichier courant (ou path), et en cas de conflit question Overwrite / Reload / Cancel sur parent ; le profil actif retient le fichier. */
#ifndef GT_UI_MACROS_SAVE_H
#define GT_UI_MACROS_SAVE_H

#include <gtk/gtk.h>

#include "session.h"

void gt_ui_macros_save(GtkWindow *parent, GtSession *s, const gchar *path);	/* path NULL : fichier courant */

#endif
