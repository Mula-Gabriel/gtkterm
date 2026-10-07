/***********************************************************************/
/* rx_queue.c                                                           */
/*           GTKTerm Software                                           */
/*                                                                      */
/*   Purpose                                                            */
/*      File d'octets reçus bornée et sûre entre threads                */
/*      (cœur sans GTK, sous-projet 5)                                  */
/***********************************************************************/

#include <string.h>

#include "rx_queue.h"

/* Une attente relit l'annulation au moins à ce rythme */
#define WAIT_STEP_US (20 * G_TIME_SPAN_MILLISECOND)

struct _GtRxQueue
{
	GMutex mutex;
	GCond cond;
	GByteArray *data;
	gsize max_bytes;
	gsize dropped;		/* octets jetés depuis le dernier gt_rx_queue_take_dropped */
	gboolean closed;
};

GtRxQueue *
gt_rx_queue_new(gsize max_bytes)
{
	GtRxQueue *q;

	g_return_val_if_fail(max_bytes > 0 && max_bytes <= G_MAXUINT, NULL);
	q = g_new0(GtRxQueue, 1);
	g_mutex_init(&q->mutex);
	g_cond_init(&q->cond);
	q->data = g_byte_array_new();
	q->max_bytes = max_bytes;
	return q;
}

void
gt_rx_queue_free(GtRxQueue *q)
{
	if(q == NULL)
		return;
	g_byte_array_unref(q->data);
	g_cond_clear(&q->cond);
	g_mutex_clear(&q->mutex);
	g_free(q);
}

gsize
gt_rx_queue_push(GtRxQueue *q, const guint8 *data, gsize len)
{
	gsize drop = 0;

	g_return_val_if_fail(q != NULL, 0);
	if(len == 0)
		return 0;
	g_mutex_lock(&q->mutex);
	if(q->closed)
	{
		g_mutex_unlock(&q->mutex);
		return 0;
	}
	if(len >= q->max_bytes)
	{
		/* Le bloc remplit à lui seul la file : seuls ses max_bytes derniers octets sont gardés */
		drop = q->data->len + (len - q->max_bytes);
		g_byte_array_set_size(q->data, 0);
		g_byte_array_append(q->data, data + (len - q->max_bytes), (guint)q->max_bytes);
	}
	else
	{
		if(q->data->len + len > q->max_bytes)
		{
			drop = q->data->len + len - q->max_bytes;
			g_byte_array_remove_range(q->data, 0, (guint)drop);
		}
		g_byte_array_append(q->data, data, (guint)len);
	}
	q->dropped += drop;
	g_cond_broadcast(&q->cond);
	g_mutex_unlock(&q->mutex);
	return drop;
}

GBytes *
gt_rx_queue_take_all(GtRxQueue *q)
{
	GBytes *b;

	g_return_val_if_fail(q != NULL, NULL);
	g_mutex_lock(&q->mutex);
	b = g_bytes_new(q->data->data, q->data->len);
	g_byte_array_set_size(q->data, 0);
	g_mutex_unlock(&q->mutex);
	return b;
}

void
gt_rx_queue_clear(GtRxQueue *q)
{
	g_return_if_fail(q != NULL);
	g_mutex_lock(&q->mutex);
	g_byte_array_set_size(q->data, 0);
	g_mutex_unlock(&q->mutex);
}

gsize
gt_rx_queue_length(GtRxQueue *q)
{
	gsize n;

	g_return_val_if_fail(q != NULL, 0);
	g_mutex_lock(&q->mutex);
	n = q->data->len;
	g_mutex_unlock(&q->mutex);
	return n;
}

gsize
gt_rx_queue_take_dropped(GtRxQueue *q)
{
	gsize n;

	g_return_val_if_fail(q != NULL, 0);
	g_mutex_lock(&q->mutex);
	n = q->dropped;
	q->dropped = 0;
	g_mutex_unlock(&q->mutex);
	return n;
}

void
gt_rx_queue_set_closed(GtRxQueue *q, gboolean closed)
{
	g_return_if_fail(q != NULL);
	g_mutex_lock(&q->mutex);
	q->closed = closed;
	g_cond_broadcast(&q->cond);
	g_mutex_unlock(&q->mutex);
}

gboolean
gt_rx_queue_is_closed(GtRxQueue *q)
{
	gboolean closed;

	g_return_val_if_fail(q != NULL, TRUE);
	g_mutex_lock(&q->mutex);
	closed = q->closed;
	g_mutex_unlock(&q->mutex);
	return closed;
}

void
gt_rx_queue_reset(GtRxQueue *q)
{
	g_return_if_fail(q != NULL);
	g_mutex_lock(&q->mutex);
	g_byte_array_set_size(q->data, 0);
	q->dropped = 0;
	q->closed = FALSE;
	g_mutex_unlock(&q->mutex);
}

GtRxWaitResult
gt_rx_queue_wait_for(GtRxQueue *q, const guint8 *pattern, gsize pattern_len, gint64 deadline_us, const gint *cancel, GBytes **out)
{
	GtRxWaitResult res;

	g_return_val_if_fail(q != NULL && out != NULL, GT_RX_WAIT_CLOSED);
	*out = NULL;
	g_mutex_lock(&q->mutex);
	for(;;)
	{
		gint64 now;

		if(pattern_len == 0)
		{
			*out = g_bytes_new(NULL, 0);
			res = GT_RX_WAIT_FOUND;
			break;
		}
		if(q->data->len >= pattern_len)
		{
			/* Recherche binaire, longueurs explicites : un octet nul n'arrête rien */
			const guint8 *found = memmem(q->data->data, q->data->len, pattern, pattern_len);

			if(found != NULL)
			{
				gsize end = (gsize)(found - q->data->data) + pattern_len;

				*out = g_bytes_new(q->data->data, end);
				g_byte_array_remove_range(q->data, 0, (guint)end);
				res = GT_RX_WAIT_FOUND;
				break;
			}
		}
		if(cancel != NULL && g_atomic_int_get(cancel) != 0)
		{
			res = GT_RX_WAIT_CANCELLED;
			break;
		}
		if(q->closed)
		{
			res = GT_RX_WAIT_CLOSED;
			break;
		}
		now = g_get_monotonic_time();
		if(now >= deadline_us)
		{
			res = GT_RX_WAIT_TIMEOUT;
			break;
		}
		g_cond_wait_until(&q->cond, &q->mutex, MIN(deadline_us, now + WAIT_STEP_US));
	}
	g_mutex_unlock(&q->mutex);
	return res;
}
