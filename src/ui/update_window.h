/* Fenêtre « Updates » (action app.updates) : versions publiées (tags vX.Y.Z), notes de NEWS, installation d'une version au choix (retour en
 * arrière compris) ; vérification au démarrage qui ouvre cette fenêtre quand une version plus récente existe et n'est pas ignorée. */

#ifndef GT_UI_UPDATE_WINDOW_H_
#define GT_UI_UPDATE_WINDOW_H_

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GT_TYPE_UPDATE_WINDOW (gt_update_window_get_type())
G_DECLARE_FINAL_TYPE(GtUpdateWindow, gt_update_window, GT, UPDATE_WINDOW, GtkWindow)

/* Une seule instance, transitoire de parent ; créée puis actualisée (settings.update_url), ou présentée de nouveau si elle est ouverte. */
void gt_update_window_show(GtkWindow *parent);
/* À appeler depuis activate : diffère (g_idle_add) puis actualise ; silencieuse sauf version plus récente et non ignorée (la fenêtre s'ouvre). */
void gt_update_startup_check(GtkWindow *main_window);
/* Annule l'actualisation de démarrage ; toute confirmation encore ouverte est ignorée (app_stop_services, à la fermeture). */
void gt_update_shutdown(void);

G_END_DECLS

#endif /* GT_UI_UPDATE_WINDOW_H_ */
