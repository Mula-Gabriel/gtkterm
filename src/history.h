/* Historique brut du port principal : octets reçus (RX) et envoyés (TX), dans l'ordre, avec leur heure (sous-projet 2). */
#ifndef HISTORY_H_
#define HISTORY_H_

#include <glib.h>

typedef enum
{
	GT_RECORD_RX,
	GT_RECORD_TX
} GtRecordKind;

/* Origine d'un envoi : décide de l'affichage de l'écho (frappe dans la ligne, le reste sur une ligne « > »). */
typedef enum
{
	GT_SEND_KEYBOARD,
	GT_SEND_MACRO,
	GT_SEND_LUA,
	GT_SEND_HEX,
	GT_SEND_FILE
} GtSendOrigin;

typedef struct
{
	GtRecordKind kind;
	GtSendOrigin origin;		/* TX seulement */
	gint64 wall_time_us;		/* heure de réception ou d'envoi (g_get_real_time) */
	const guint8 *data;
	gsize len;
} GtRecord;

typedef struct _GtHistory GtHistory;
typedef void (*GtHistoryFunc)(const GtRecord *record, gpointer user_data);

#define GT_HISTORY_DEFAULT_CAPACITY (128 * 1024)

GtHistory *gt_history_new(gsize capacity);
void       gt_history_free(GtHistory *h);
/* Ajoute un enregistrement ; retire les plus anciens en entier au-delà de la capacité (octets utiles). Un enregistrement
   plus grand que la capacité n'en garde que la fin. */
void       gt_history_append(GtHistory *h, GtRecordKind kind, GtSendOrigin origin, gint64 wall_time_us, const guint8 *data, gsize len);
/* Du plus ancien au plus récent ; func ne doit pas modifier l'historique. */
void       gt_history_foreach(GtHistory *h, GtHistoryFunc func, gpointer user_data);
void       gt_history_clear(GtHistory *h);
/* Octets reçus seulement, dans l'ordre (sauvegarde brute). */
GBytes    *gt_history_copy_rx(GtHistory *h);
gsize      gt_history_size(GtHistory *h);
guint      gt_history_count(GtHistory *h);

#endif
