/* Transformation des octets reçus à la lecture : CR/LF automatique et horodatage en début de ligne (sous-projet 2).
   Pure : aucune horloge interne, aucune E/S ; l'heure de réception est passée par l'appelant. */
#ifndef RX_TRANSFORM_H_
#define RX_TRANSFORM_H_

#include <glib.h>

typedef struct _GtRxTransform GtRxTransform;

GtRxTransform *gt_rx_transform_new(gboolean crlf_auto, gboolean timestamp);
void           gt_rx_transform_free(GtRxTransform *tr);
/* Change les options sans perdre l'état de ligne (CR en attente, début de ligne). */
void           gt_rx_transform_set_options(GtRxTransform *tr, gboolean crlf_auto, gboolean timestamp);
/* Début de ligne, aucun CR en attente. */
void           gt_rx_transform_reset(GtRxTransform *tr);
/* Ajoute à out le texte produit par data. */
void           gt_rx_transform_feed(GtRxTransform *tr, const guint8 *data, gsize len, gint64 wall_time_us, GString *out);
/* « [j.HHh.MMm.SSs.mmm] » (UTC) suivi d'une espace. */
void           gt_rx_transform_format_timestamp(gint64 wall_time_us, GString *out);

#endif
