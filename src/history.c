/***********************************************************************/
/* history.c                                                           */
/*                                                                     */
/*   Purpose                                                           */
/*      Historique brut du port principal (sous-projet 2)              */
/*                                                                     */
/***********************************************************************/

#include <string.h>

#include "history.h"

typedef struct
{
	GtRecordKind kind;
	GtSendOrigin origin;
	gint64 wall_time_us;
	gsize len;
	guint8 data[];
} HistoryEntry;

struct _GtHistory
{
	gsize capacity;
	gsize size;		/* octets utiles retenus */
	GQueue entries;		/* HistoryEntry *, du plus ancien au plus récent */
};

GtHistory *gt_history_new(gsize capacity)
{
	GtHistory *h = g_new0(GtHistory, 1);

	h->capacity = capacity > 0 ? capacity : 1;
	g_queue_init(&h->entries);
	return h;
}

void gt_history_free(GtHistory *h)
{
	if(h == NULL)
		return;
	gt_history_clear(h);
	g_free(h);
}

void gt_history_append(GtHistory *h, GtRecordKind kind, GtSendOrigin origin, gint64 wall_time_us, const guint8 *data, gsize len)
{
	HistoryEntry *e;

	if(len == 0)
		return;
	if(len > h->capacity)
	{
		/* Plus grand que la capacité : seule la fin est gardée */
		data += len - h->capacity;
		len = h->capacity;
	}
	while(h->size + len > h->capacity)
	{
		HistoryEntry *old = g_queue_pop_head(&h->entries);

		h->size -= old->len;
		g_free(old);
	}
	e = g_malloc(sizeof(HistoryEntry) + len);
	e->kind = kind;
	e->origin = origin;
	e->wall_time_us = wall_time_us;
	e->len = len;
	memcpy(e->data, data, len);
	g_queue_push_tail(&h->entries, e);
	h->size += len;
}

void gt_history_foreach(GtHistory *h, GtHistoryFunc func, gpointer user_data)
{
	for(GList *l = h->entries.head; l != NULL; l = l->next)
	{
		HistoryEntry *e = l->data;
		GtRecord rec = { e->kind, e->origin, e->wall_time_us, e->data, e->len };

		func(&rec, user_data);
	}
}

void gt_history_clear(GtHistory *h)
{
	HistoryEntry *e;

	while((e = g_queue_pop_head(&h->entries)) != NULL)
		g_free(e);
	h->size = 0;
}

GBytes *gt_history_copy_rx(GtHistory *h)
{
	GByteArray *a = g_byte_array_sized_new((guint)h->size);

	for(GList *l = h->entries.head; l != NULL; l = l->next)
	{
		HistoryEntry *e = l->data;

		if(e->kind == GT_RECORD_RX)
			g_byte_array_append(a, e->data, (guint)e->len);
	}
	return g_byte_array_free_to_bytes(a);
}

gsize gt_history_size(GtHistory *h)
{
	return h->size;
}

guint gt_history_count(GtHistory *h)
{
	return h->entries.length;
}
