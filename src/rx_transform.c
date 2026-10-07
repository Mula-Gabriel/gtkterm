/***********************************************************************/
/* rx_transform.c                                                      */
/*                                                                     */
/*   Purpose                                                           */
/*      CR/LF automatique et horodatage appliqués à la lecture         */
/*      (règles de l'ancien put_chars de buffer.c, sous-projet 2)      */
/*                                                                     */
/***********************************************************************/

#include "rx_transform.h"

struct _GtRxTransform
{
	gboolean crlf_auto;
	gboolean timestamp;
	gboolean cr_received;		/* dernier octet reçu : CR (CR/LF automatique) */
	gboolean need_timestamp;	/* prochain octet en début de ligne */
};

GtRxTransform *gt_rx_transform_new(gboolean crlf_auto, gboolean timestamp)
{
	GtRxTransform *tr = g_new0(GtRxTransform, 1);

	gt_rx_transform_set_options(tr, crlf_auto, timestamp);
	gt_rx_transform_reset(tr);
	return tr;
}

void gt_rx_transform_free(GtRxTransform *tr)
{
	g_free(tr);
}

void gt_rx_transform_set_options(GtRxTransform *tr, gboolean crlf_auto, gboolean timestamp)
{
	tr->crlf_auto = crlf_auto;
	tr->timestamp = timestamp;
}

void gt_rx_transform_reset(GtRxTransform *tr)
{
	tr->cr_received = FALSE;
	tr->need_timestamp = TRUE;
}

void gt_rx_transform_format_timestamp(gint64 wall_time_us, GString *out)
{
	gint64 sec = wall_time_us / G_USEC_PER_SEC;

	g_string_append_printf(out, "[%d.%02uh.%02um.%02us.%03u] ", (gint)(sec / 86400), (guint)((sec / 3600) % 24), (guint)((sec / 60) % 60),
	                       (guint)(sec % 60), (guint)((wall_time_us / 1000) % 1000));
}

void gt_rx_transform_feed(GtRxTransform *tr, const guint8 *data, gsize len, gint64 wall_time_us, GString *out)
{
	if(!tr->crlf_auto && !tr->timestamp)
	{
		/* Sans option, le texte est l'octet reçu ; on suit seulement les fins de ligne pour une activation ultérieure */
		g_string_append_len(out, (const gchar *)data, (gssize)len);
		if(len > 0)
			tr->need_timestamp = (data[len - 1] == '\n');
		tr->cr_received = FALSE;
		return;
	}

	for(gsize i = 0; i < len; i++)
	{
		guint8 c = data[i];

		if(tr->crlf_auto)
		{
			if(c == '\r')
			{
				/* Deux CR de suite : on insère un saut de ligne */
				if(tr->cr_received)
				{
					g_string_append_c(out, '\n');
					tr->need_timestamp = TRUE;
				}
				tr->cr_received = TRUE;
			}
			else
			{
				/* LF sans CR : on insère le CR ; caractère ordinaire après un CR : on insère le LF */
				if(c == '\n' && !tr->cr_received)
					g_string_append_c(out, '\r');
				else if(c != '\n' && tr->cr_received)
				{
					g_string_append_c(out, '\n');
					tr->need_timestamp = TRUE;
				}
				tr->cr_received = FALSE;
			}
		}

		if(tr->need_timestamp)
		{
			if(tr->timestamp)
				gt_rx_transform_format_timestamp(wall_time_us, out);
			tr->need_timestamp = FALSE;
		}
		if(c == '\n')
			tr->need_timestamp = TRUE;
		g_string_append_c(out, (gchar)c);
	}
}
