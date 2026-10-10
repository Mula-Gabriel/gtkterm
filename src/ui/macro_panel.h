/* macro_panel.h — panneau des boutons de macros (interface GTK 4) : un par page d'onglet (sous-projet 6b), vue des macros de sa session. */

#ifndef GT_UI_MACRO_PANEL_H_
#define GT_UI_MACRO_PANEL_H_

#include <gtk/gtk.h>

#include "session.h"

/* Panneau « macro-panel » sur les macros de s (gt_session_get_macros), visible ou non ; ajoute l'action show-macro-panel au groupe d'actions de
 * la session (tab_actions, relayée en app.show-macro-panel). L'état du panneau est attaché à la boîte rendue. */
GtkWidget *gt_macro_panel_new(GtSession *s, GActionMap *tab_actions, gboolean visible);
void       gt_macro_panel_rebuild(GtkWidget *panel);	/* crochet macros_changed de sa session ; après l'arrêt : rien */
void       gt_macro_panel_polling_changed(GtkWidget *panel, guint id, gboolean running);	/* crochet macro_polling_changed ; après l'arrêt : rien */
/* Arrêt de la page (plan, décision 3, étape a), avant gt_session_shutdown : minuterie retirée, popovers détachés, show-macro-panel retirée du
 * groupe, plus aucun rappel vers le modèle. Idempotent ; NULL accepté. */
void       gt_macro_panel_shutdown(GtkWidget *panel);

#endif
