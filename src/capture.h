/* Captures de l'historique du port vers un fichier (sous-projet 6a) : « Save RAW » et « Save ASCII ».
 * Sans GTK ; écriture vérifiée jusqu'à la fermeture du fichier. Le fichier est créé ou remplacé (le sélecteur a confirmé). */
#ifndef CAPTURE_H_
#define CAPTURE_H_

#include <glib.h>

#include "history.h"

/* Octets reçus seulement, intacts, dans l'ordre (ni écho, ni horodatage). Erreur : FALSE et G_FILE_ERROR (errno). */
gboolean gt_capture_save_raw(GtHistory *h, const gchar *path, GError **error);
/* Texte reçu comme la vue ASCII le montre : CR/LF auto et horodatage par GtRxTransform, puis seuls TAB, CR, LF, les caractères
 * imprimables 0x20-0x7E et les octets ≥ 0x80 (UTF-8) sont gardés ; les autres contrôles (0x00-0x1F, 0x7F) sont retirés.
 * Erreur : FALSE et G_FILE_ERROR (errno). */
gboolean gt_capture_save_ascii(GtHistory *h, gboolean crlf_auto, gboolean timestamp, const gchar *path, GError **error);

#endif
