/* Fenêtre « Updates » (ligne GTK 3) et vérification au démarrage : versions publiées (tags vX.Y.Z), notes de NEWS, installation d'une version
 * au choix, retour en arrière compris. Spec : docs/superpowers/specs/2026-10-12-maj-par-tags-design.md. */

#ifndef UPDATE_H_
#define UPDATE_H_

#include <gtk/gtk.h>

/* Aide › Check for updates… : ouvre la fenêtre (ou la présente si elle est ouverte) et actualise la liste des versions. */
void update_window_show(GtkWindow *parent);
/* Démarrage (si update_check_startup) : actualise en arrière-plan ; ouvre la fenêtre seulement si une version plus récente que l'installée
 * existe et n'est pas ignorée. Silencieuse sinon, erreurs comprises. */
void update_startup_check(GtkWindow *parent);

#endif /* UPDATE_H_ */
