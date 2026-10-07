/* macro_poller.h — ordonnanceur de polling des macros : une minuterie GLib (contexte par défaut) par macro en cours.
 * Les macros sont envoyées par la fonction fournie (dans l'application : macros_send, arguments enregistrés, vers la liaison
 * de l'application). Utilisé uniquement sur le fil principal. */

#ifndef MACRO_POLLER_H_
#define MACRO_POLLER_H_

#include <glib.h>

#include "macro_set.h"

typedef struct _GtMacroPoller GtMacroPoller;

typedef gboolean (*GtMacroPollerSendFunc)  (guint id, gpointer user_data);
typedef void     (*GtMacroPollerStateFunc) (guint id, gboolean running, gpointer user_data);

GtMacroPoller *gt_macro_poller_new           (GtMacroSet *set, GtMacroPollerSendFunc send, GtMacroPollerStateFunc state_changed, gpointer user_data);
void           gt_macro_poller_free          (GtMacroPoller *poller);	/* arrête tout, sans rappel d'état */
void           gt_macro_poller_set_set       (GtMacroPoller *poller, GtMacroSet *set);	/* arrête tout (rappels émis) puis change de jeu */
gboolean       gt_macro_poller_start         (GtMacroPoller *poller, guint id);	/* envoi immédiat puis à chaque période */
void           gt_macro_poller_stop          (GtMacroPoller *poller, guint id);
gboolean       gt_macro_poller_toggle        (GtMacroPoller *poller, guint id);	/* renvoie le nouvel état */
gboolean       gt_macro_poller_is_running    (const GtMacroPoller *poller, guint id);
guint          gt_macro_poller_running_count (const GtMacroPoller *poller);
void           gt_macro_poller_reset         (GtMacroPoller *poller);
void           gt_macro_poller_sync          (GtMacroPoller *poller);	/* après une modification du jeu */
void           gt_macro_poller_period_changed(GtMacroPoller *poller, guint id);

#endif
