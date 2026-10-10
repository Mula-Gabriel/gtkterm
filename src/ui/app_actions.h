/* Actions d'onglet de l'interface (sous-projet 6a, §6.2 ; groupe de session en 6b, plan décision 4) et raccourcis des actions app.* d'onglet.
 * Les actions de la page qui dépendent du terminal (clear-scrollback, copy, paste, select-all) vont dans le groupe d'actions de la session ;
 * l'application les expose en app.* par des relais vers la session active (app.c). */
#ifndef GT_UI_APP_ACTIONS_H_
#define GT_UI_APP_ACTIONS_H_

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* startup : raccourcis des actions app.* d'onglet (relayées), une seule fois */
void gt_app_actions_set_accels(GtkApplication *app);
/* Page : clear-scrollback, copy (sensible selon la sélection de ce terminal), paste, select-all ajoutées à tab_actions ; menu contextuel
 * « terminal-context » posé sur le VTE de terminal_view */
void gt_app_actions_install(GActionMap *tab_actions, GtkWidget *terminal_view);
/* Arrêt de la page : ces actions retirées de tab_actions, plus aucun lien vers le terminal. Idempotente ; sans installation : rien. */
void gt_app_actions_uninstall(GActionMap *tab_actions);

G_END_DECLS

#endif
