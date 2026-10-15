/* Activité d'un port (indicateurs d'onglet et de barre d'état) : état calculé à partir d'événements datés (heure monotone, µs). Cœur, sans GTK
 * ni liaison ; fil principal seulement. Voir docs/superpowers/specs/2026-10-14-activite-ports-design.md §1. */
#ifndef PORT_ACTIVITY_H_
#define PORT_ACTIVITY_H_

#include <glib-object.h>

G_BEGIN_DECLS

#define GT_PORT_ACTIVITY_RX_PULSE_US 300000	/* durée de l'état « réception » après le dernier octet */
#define GT_PORT_ACTIVITY_TIMEOUT_MAX 3600	/* s : délais de repos et sans données */

typedef enum
{
	GT_PORT_ACTIVITY_CLOSED,
	GT_PORT_ACTIVITY_CONNECTING,	/* client TCP en connexion, ou serveur TCP sans client */
	GT_PORT_ACTIVITY_WAITING,	/* ouvert, rien reçu encore */
	GT_PORT_ACTIVITY_ACTIVE,	/* ouvert, a reçu il y a moins que le délai de repos */
	GT_PORT_ACTIVITY_RECEIVING,	/* dernier octet il y a moins de 300 ms */
	GT_PORT_ACTIVITY_IDLE,	/* a reçu, puis plus rien depuis le délai de repos */
	GT_PORT_ACTIVITY_LOST,
	GT_PORT_ACTIVITY_RECONNECTING,
	GT_PORT_ACTIVITY_DEAD	/* ouvert sans un octet, ou perdu, depuis le délai sans données ; ou ouverture échouée */
} GtPortActivityState;

/* État de la liaison vu par le modèle (converti depuis GtLinkState par la session : le cœur n'inclut pas link.h) */
typedef enum
{
	GT_PORT_LINK_CLOSED,
	GT_PORT_LINK_CONNECTING,
	GT_PORT_LINK_OPEN,
	GT_PORT_LINK_LISTENING,
	GT_PORT_LINK_LOST,
	GT_PORT_LINK_RECONNECTING
} GtPortActivityLink;

GType gt_port_activity_state_get_type(void);
#define GT_TYPE_PORT_ACTIVITY_STATE (gt_port_activity_state_get_type())

#define GT_TYPE_PORT_ACTIVITY (gt_port_activity_get_type())
G_DECLARE_FINAL_TYPE(GtPortActivity, gt_port_activity, GT, PORT_ACTIVITY, GObject)

/* Propriétés : « state » (GtPortActivityState, nicks closed, connecting, waiting, active, receiving, idle, lost, reconnecting, dead), « unread »
 * (booléen), notifiées seulement sur changement ; « use-timer » (construction, vrai par défaut : échéances par une source GLib ; faux : les tests
 * appellent tick). Délais par défaut 10 s et 60 s ; « vu » vrai par défaut. */
GtPortActivity     *gt_port_activity_new(void);
void                gt_port_activity_set_link_state(GtPortActivity *a, GtPortActivityLink st, gint64 now_us);
void                gt_port_activity_received(GtPortActivity *a, gint64 now_us);	/* ignoré hors GT_PORT_LINK_OPEN */
/* Ouverture échouée : dead tout de suite tant que la liaison est fermée ou perdue ; effacé par reset et par l'entrée en OPEN ou LISTENING */
void                gt_port_activity_open_failed(GtPortActivity *a, gint64 now_us);
void                gt_port_activity_reset(GtPortActivity *a, gint64 now_us);	/* ouverture ou fermeture demandée : repart de zéro (échec oublié) */
/* 0 désactive l'état (idle ou dead) ; valeurs bornées à GT_PORT_ACTIVITY_TIMEOUT_MAX ; état réévalué à now_us */
void                gt_port_activity_set_timeouts(GtPortActivity *a, guint idle_s, guint no_data_s, gint64 now_us);
void                gt_port_activity_set_viewed(GtPortActivity *a, gboolean viewed);
void                gt_port_activity_tick(GtPortActivity *a, gint64 now_us);
GtPortActivityState gt_port_activity_get_state(GtPortActivity *a);
gboolean            gt_port_activity_get_unread(GtPortActivity *a);
const gchar        *gt_port_activity_state_nick(GtPortActivityState st);

G_END_DECLS

#endif
