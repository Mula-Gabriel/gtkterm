/* Barre d'état (sous-projet 6a, §6.3) : indicateur d'activité (un seul onglet), texte d'état de la liaison, message temporaire, voyants des
 * lignes modem en série. */
#ifndef GT_UI_STATUS_BAR_H_
#define GT_UI_STATUS_BAR_H_

#include <gtk/gtk.h>

#include "app_ui.h"
#include "link.h"
#include "port_activity.h"

G_BEGIN_DECLS

/* Barre d'une page d'onglet (une par page) : indicateur d'activité « status-activity » (caché au départ), texte suivant status-text de link ;
 * le titre de la fenêtre est lié par app.c */
GtkWidget *gt_status_bar_new(GtkTermLink *link, GtPortActivity *activity);
/* Indicateur d'activité visible ; app.c le montre seulement avec un onglet */
void       gt_status_bar_set_activity_visible(GtkWidget *bar, gboolean visible);
/* Remplace le texte d'état par text pendant ms, puis le rend ; un nouveau message remplace l'ancien (pas de pile) */
void       gt_status_bar_flash(GtkWidget *bar, const gchar *text, guint ms);
/* Arrêt de la page (gt_session_view_shutdown, avant l'arrêt de la session) : le texte ne suit plus la liaison, voyants et messages arrêtés ;
 * idempotente */
void       gt_status_bar_detach(GtkWidget *bar);

G_END_DECLS

#endif
