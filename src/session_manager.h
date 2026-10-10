/* Sessions ouvertes, dans l'ordre des onglets, et session active (sous-projet 6b, spec §4.3). Couche application ; fil principal seulement.
 * L'interface est maîtresse de l'ordre (plan, décision 5) : elle appelle add, remove et move ; le modèle n'est jamais modifié ailleurs. */
#ifndef SESSION_MANAGER_H_
#define SESSION_MANAGER_H_

#include <gio/gio.h>

#include "session.h"

G_BEGIN_DECLS

#define GT_TYPE_SESSION_MANAGER (gt_session_manager_get_type())
G_DECLARE_FINAL_TYPE(GtSessionManager, gt_session_manager, GT, SESSION_MANAGER, GObject)

GtSessionManager *gt_session_manager_new(void);
GListModel       *gt_session_manager_get_model(GtSessionManager *m);	/* GtSession, ordre des onglets */
guint             gt_session_manager_get_n(GtSessionManager *m);
GtSession        *gt_session_manager_get_nth(GtSessionManager *m, guint i);	/* empruntée ; NULL hors bornes */
gint              gt_session_manager_index_of(GtSessionManager *m, GtSession *s);	/* -1 si absente */
/* position < 0 ou au-delà : à la fin. Référence prise. La première session ajoutée devient active. */
void              gt_session_manager_add(GtSessionManager *m, GtSession *s, gint position);
/* Absente : rien. Active : l'active passe d'abord à la voisine de droite, sinon de gauche, sinon NULL ; puis retrait (référence rendue). */
void              gt_session_manager_remove(GtSessionManager *m, GtSession *s);
void              gt_session_manager_move(GtSessionManager *m, GtSession *s, guint position);	/* position bornée à n - 1 ; active inchangée */
GtSession        *gt_session_manager_get_active(GtSessionManager *m);	/* empruntée ; NULL seulement sans session */
void              gt_session_manager_set_active(GtSessionManager *m, GtSession *s);	/* s absente : ignoré ; propriété « active » notifiée si elle change */

G_END_DECLS

#endif
