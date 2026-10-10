/* Saisie hexadécimale de la barre d'envoi (sous-projet 6a) : analyse d'une ligne en octets et historique des saisies.
 * Sans GTK ; remplace le sscanf("%02X") de l'ancienne interface (revue §5). */
#ifndef HEX_INPUT_H_
#define HEX_INPUT_H_

#include <glib.h>

#define GT_HEX_INPUT_ERROR (gt_hex_input_error_quark())

typedef enum
{
	GT_HEX_INPUT_ERROR_INVALID	/* jeton qui n'est pas 1 ou 2 chiffres hexadécimaux */
} GtHexInputError;

GQuark  gt_hex_input_error_quark(void);
/* Jetons de 1 ou 2 chiffres hexadécimaux (casse indifférente) séparés par des blancs et/ou « ; ». Saisie vide ou faite seulement
 * de séparateurs : GBytes de 0 octet. Jeton invalide : NULL et GT_HEX_INPUT_ERROR_INVALID, message = le jeton fautif tel quel. */
GBytes *gt_hex_input_parse(const gchar *text, GError **error);

typedef struct _GtHexHistory GtHexHistory;

/* max : nombre d'entrées gardées (au moins 1) */
GtHexHistory *gt_hex_history_new(guint max);
void          gt_hex_history_free(GtHexHistory *h);
/* Ignore une saisie vide ou blanche et un doublon de la dernière entrée ; au-delà de max, retire la plus ancienne ;
 * remet toujours le curseur de navigation à la fin (après la dernière entrée). */
void          gt_hex_history_add(GtHexHistory *h, const gchar *text);
/* Entrée précédente (flèche haut) ; NULL s'il n'y a rien avant le curseur (curseur inchangé). */
const gchar  *gt_hex_history_previous(GtHexHistory *h);
/* Entrée suivante (flèche bas) ; "" quand le curseur passe ou reste après la dernière (ligne de saisie vidée). */
const gchar  *gt_hex_history_next(GtHexHistory *h);

#endif
