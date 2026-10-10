/* Journal des octets reçus vers un fichier (sous-projet 6a) : démarrer, pause, reprise, vider, arrêter.
 * Sans GTK ni globale ; alimenté par le signal « received » de la liaison. Le texte écrit est celui de la vue ASCII : CR/LF auto et
 * horodatage par un GtRxTransform propre au journal, nourri aussi en pause (rien n'est écrit, mais l'état de ligne avance : la
 * reprise ne coupe pas une ligne et ne répète pas d'horodatage). Écritures vérifiées (fwrite puis fflush) ; une erreur arrête le
 * journal et est rendue à l'appelant, qui l'affiche (crochet message, depuis un idle s'il est dans un rappel de la liaison). */
#ifndef RX_LOGGER_H_
#define RX_LOGGER_H_

#include <glib.h>

typedef struct _GtRxLogger GtRxLogger;

GtRxLogger  *gt_rx_logger_new(void);
/* Ferme le fichier s'il est ouvert ; NULL accepté */
void         gt_rx_logger_free(GtRxLogger *l);
/* Arrête le journal en cours puis crée ou tronque path ; état de ligne remis à zéro, pas en pause.
 * Erreur : FALSE et G_FILE_ERROR (errno), journal inactif. */
gboolean     gt_rx_logger_start(GtRxLogger *l, const gchar *path, gboolean crlf_auto, gboolean timestamp, GError **error);
/* Options de la transformation, sans perdre l'état de ligne */
void         gt_rx_logger_set_options(GtRxLogger *l, gboolean crlf_auto, gboolean timestamp);
/* Sans effet si le journal est inactif */
void         gt_rx_logger_set_paused(GtRxLogger *l, gboolean paused);
gboolean     gt_rx_logger_is_active(GtRxLogger *l);
/* FALSE si le journal est inactif */
gboolean     gt_rx_logger_is_paused(GtRxLogger *l);
/* Transforme puis écrit (rien en pause ni si inactif : TRUE). FALSE : écriture échouée, G_FILE_ERROR, journal arrêté. */
gboolean     gt_rx_logger_feed(GtRxLogger *l, const guint8 *data, gsize len, gint64 wall_time_us, GError **error);
/* Tronque le fichier ouvert (TRUE si inactif) ; état de ligne remis à zéro. Erreur : FALSE, G_FILE_ERROR, journal arrêté. */
gboolean     gt_rx_logger_clear(GtRxLogger *l, GError **error);
/* Ferme le fichier ; sans effet si inactif */
void         gt_rx_logger_stop(GtRxLogger *l);
/* Chemin du journal ouvert, NULL si inactif */
const gchar *gt_rx_logger_get_path(GtRxLogger *l);

#endif
