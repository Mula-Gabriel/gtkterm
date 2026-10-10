/* Barre d'état (sous-projet 6a, §6.3) : texte d'état de la liaison, message temporaire, voyants des lignes modem en série. */
#ifndef GT_UI_STATUS_BAR_H_
#define GT_UI_STATUS_BAR_H_

#include <gtk/gtk.h>

#include "link.h"

G_BEGIN_DECLS

/* window non NULL : barre de la fenêtre principale — son titre suit aussi status-text, et le crochet status_flash vise cette barre */
GtkWidget *gt_status_bar_new(GtkTermLink *link, GtkWindow *window);
/* Remplace le texte d'état par text pendant ms, puis le rend ; un nouveau message remplace l'ancien (pas de pile) */
void       gt_status_bar_flash(GtkWidget *bar, const gchar *text, guint ms);
/* Crochet GtAppUi.status_flash : barre de la fenêtre principale, sans effet sans elle */
void       gt_status_bar_app_flash(const gchar *text, guint ms);
/* Fermeture (close-request, avant l'arrêt des services) : titre et texte ne suivent plus la liaison, voyants et messages arrêtés ; idempotente */
void       gt_status_bar_detach(GtkWidget *bar);

G_END_DECLS

#endif
