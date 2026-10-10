/* Fenêtre « Updates » (action app.updates), vérification au démarrage et proposition de mise à jour (sous-projet 6a, §6.7). */

#ifndef GT_UI_UPDATE_WINDOW_H_
#define GT_UI_UPDATE_WINDOW_H_

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GT_TYPE_UPDATE_WINDOW (gt_update_window_get_type())
G_DECLARE_FINAL_TYPE(GtUpdateWindow, gt_update_window, GT, UPDATE_WINDOW, GtkWindow)

/* Une seule instance, transitoire de parent ; présentée de nouveau si elle est déjà ouverte. */
void gt_update_window_show(GtkWindow *parent);
/* À appeler depuis activate : diffère (g_idle_add) puis vérifie settings.update_url ; silencieuse sauf mise à jour disponible et non ignorée. */
void gt_update_startup_check(GtkWindow *main_window);
/* Boîte à trois choix « Update now / Later / Skip this version » pour remote_rev (gt_ui_choose de dialogs.c, fenêtre construite par GTKTerm) ;
 * non bloquante. */
void gt_update_offer(GtkWindow *parent, const gchar *remote_rev);
/* Annule la vérification de démarrage en cours et les propositions ouvertes (app_stop_services, à la fermeture). */
void gt_update_shutdown(void);

G_END_DECLS

#endif /* GT_UI_UPDATE_WINDOW_H_ */
