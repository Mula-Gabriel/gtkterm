/* Relais d'action (sous-projet 6b, spec §4.4) : GAction qui délègue à l'action de même nom d'un GActionGroup cible, changeable.
 * Nom, type de paramètre et type d'état sont fixés à la création (pris sur l'action du groupe modèle). « enabled » et « state » reflètent
 * l'action cible et sont notifiés seulement s'ils changent ; sans cible ou sans l'action dans la cible : désactivé, dernier état gardé.
 * activate et change_state ne font rien quand le relais est désactivé. Fil principal seulement. */
#ifndef ACTION_RELAY_H_
#define ACTION_RELAY_H_

#include <gio/gio.h>

G_BEGIN_DECLS

#define GT_TYPE_ACTION_RELAY (gt_action_relay_get_type())
G_DECLARE_FINAL_TYPE(GtActionRelay, gt_action_relay, GT, ACTION_RELAY, GObject)

/* NULL si model n'a pas d'action name ; sinon relais ciblant model */
GtActionRelay *gt_action_relay_new(GActionGroup *model, const gchar *name);
void           gt_action_relay_set_target(GtActionRelay *relay, GActionGroup *target);	/* NULL : aucune cible */
GActionGroup  *gt_action_relay_get_target(GtActionRelay *relay);

G_END_DECLS

#endif
