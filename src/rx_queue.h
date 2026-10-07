/* File d'octets reçus, bornée, sûre entre threads (sous-projet 5) : réception d'un port Lua ou du port 0 du moteur.
 * Le producteur (thread principal : transport ou signal « received ») ajoute ; le thread Lua lit, attend un motif, vide.
 * Chaque fonction prend et rend le verrou elle-même et n'appelle aucun code extérieur sous ce verrou : l'appelant ne le tient
 * jamais, en particulier pas pendant un appel synchrone vers le thread principal. */
#ifndef RX_QUEUE_H_
#define RX_QUEUE_H_

#include <glib.h>

typedef struct _GtRxQueue GtRxQueue;

typedef enum
{
	GT_RX_WAIT_FOUND,	/* motif trouvé : *out = données jusqu'au motif inclus, retirées de la file */
	GT_RX_WAIT_TIMEOUT,	/* échéance atteinte sans motif */
	GT_RX_WAIT_CLOSED,	/* file fermée (port perdu ou fermé) et motif absent des données restantes */
	GT_RX_WAIT_CANCELLED	/* *cancel est devenu non nul pendant l'attente */
} GtRxWaitResult;

GtRxQueue      *gt_rx_queue_new          (gsize max_bytes);
void            gt_rx_queue_free         (GtRxQueue *q);
/* Ajoute ; au-delà de max_bytes, les octets les plus anciens sont jetés. Renvoie le nombre d'octets jetés par cet appel. */
gsize           gt_rx_queue_push         (GtRxQueue *q, const guint8 *data, gsize len);
GBytes         *gt_rx_queue_take_all     (GtRxQueue *q);
void            gt_rx_queue_clear        (GtRxQueue *q);
gsize           gt_rx_queue_length       (GtRxQueue *q);
/* Octets jetés (débordement) depuis l'appel précédent, remis à zéro. */
gsize           gt_rx_queue_take_dropped (GtRxQueue *q);
/* Fermée : les attentes en cours et futures se terminent par CLOSED si le motif est absent ; push est sans effet. */
void            gt_rx_queue_set_closed   (GtRxQueue *q, gboolean closed);
gboolean        gt_rx_queue_is_closed    (GtRxQueue *q);
/* Vide, rouvre, remet le compteur de pertes à zéro. */
void            gt_rx_queue_reset        (GtRxQueue *q);
/* deadline_us : temps monotone (g_get_monotonic_time) ; cancel : lu atomiquement au moins toutes les 20 ms, peut être NULL.
 * Ordre des issues à chaque réveil : motif trouvé → FOUND (même fermée ou annulée) ; annulation → CANCELLED ; fermée → CLOSED ;
 * échéance passée → TIMEOUT. Motif vide : FOUND immédiat avec des données vides. *out vaut NULL sauf pour FOUND. */
GtRxWaitResult  gt_rx_queue_wait_for     (GtRxQueue *q, const guint8 *pattern, gsize pattern_len, gint64 deadline_us,
                                          const gint *cancel, GBytes **out);

#endif
