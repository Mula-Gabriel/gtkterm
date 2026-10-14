/* Étiquette d'onglet (sous-projet 6b, spec §3.1-8) : nom court de la session, infobulle = texte d'état, bouton de fermeture. */
#ifndef GT_UI_TAB_LABEL_H_
#define GT_UI_TAB_LABEL_H_

#include <gtk/gtk.h>

#include "session.h"

G_BEGIN_DECLS

/* GtkBox horizontale nommée « tab-label » : indicateur d'activité « tab-activity » (activity_icon.h), GtkLabel « tab-name » lié à short-name,
 * infobulle liée à status-text, bouton plat « tab-close » (nom accessible « Close tab ») qui appelle gt_app_close_session(s). Classes CSS :
 * « lost » tant que la liaison est perdue ou en reconnexion (sans rendu propre), « dead » (port muet : nom grisé), « unread » (reçu hors vue :
 * nom en gras). */
GtkWidget *gt_tab_label_new(GtSession *s);
void       gt_tab_label_set_closable(GtkWidget *label, gboolean closable);	/* bouton de fermeture sensible ou non (suivi par app.c) */

G_END_DECLS

#endif
