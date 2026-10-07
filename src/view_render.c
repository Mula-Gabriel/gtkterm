/***********************************************************************/
/* view_render.c                                                       */
/*                                                                     */
/*   Purpose                                                           */
/*      Rendu des vues ASCII et hexadécimale, écho compris             */
/*      (sous-projet 2 ; vue hexadécimale reprise de put_hexadecimal)  */
/*                                                                     */
/***********************************************************************/

#include <string.h>

#include "rx_transform.h"
#include "view_render.h"

#define COLOR_ECHO "\033[0;34m"
#define COLOR_RESET "\033[0m"

struct _GtViewRender
{
	GtViewOptions opts;
	GtRxTransform *transform;	/* vue ASCII */
	gboolean at_bol;		/* vue ASCII : la dernière sortie finit une ligne */
	guint col;			/* vue hexadécimale : colonne de la ligne reçue en cours */
	guint total_rx;			/* vue hexadécimale : octets reçus sur les lignes terminées (index) */
};

static void view_render_set_options(GtViewRender *r, const GtViewOptions *opts)
{
	r->opts = *opts;
	if(r->opts.bytes_per_line < 2)
		r->opts.bytes_per_line = 2;
	gt_rx_transform_set_options(r->transform, r->opts.crlf_auto, r->opts.timestamp);
	gt_rx_transform_reset(r->transform);
	r->at_bol = TRUE;
	r->col = 0;
	r->total_rx = 0;
}

GtViewRender *gt_view_render_new(const GtViewOptions *opts)
{
	GtViewRender *r = g_new0(GtViewRender, 1);

	r->transform = gt_rx_transform_new(opts->crlf_auto, opts->timestamp);
	view_render_set_options(r, opts);
	return r;
}

void gt_view_render_free(GtViewRender *r)
{
	if(r == NULL)
		return;
	gt_rx_transform_free(r->transform);
	g_free(r);
}

void gt_view_render_reset(GtViewRender *r, const GtViewOptions *opts)
{
	view_render_set_options(r, opts);
}

/* Une cellule de la vue hexadécimale : « XX », caractère dans la colonne ASCII, « - » au milieu de la ligne */
static void hex_put_byte(GtViewRender *r, guint8 c, guint *col, GString *out)
{
	guint bpl = r->opts.bytes_per_line;
	guint avance = (bpl - *col) * 3 + *col + 2;

	g_string_append_printf(out, "%02X ", c);
	g_string_append_printf(out, "\033[%uC", avance);
	g_string_append_c(out, (c >= 0x20 && c < 0x7F) ? (gchar)c : '.');
	g_string_append_printf(out, "\033[%uD", avance + 1);
	if(*col == bpl / 2 - 1)
		g_string_append(out, "- ");
	(*col)++;
}

void gt_view_render_rx(GtViewRender *r, const guint8 *data, gsize len, gint64 wall_time_us, GString *out)
{
	if(r->opts.mode == GT_VIEW_ASCII)
	{
		gsize start = out->len;

		gt_rx_transform_feed(r->transform, data, len, wall_time_us, out);
		if(out->len > start)
			r->at_bol = (out->str[out->len - 1] == '\n');
		return;
	}

	for(gsize i = 0; i < len; i++)
	{
		if(r->col == 0 && r->opts.show_index)
			g_string_append_printf(out, "%6u: ", r->total_rx);
		hex_put_byte(r, data[i], &r->col, out);
		if(r->col == r->opts.bytes_per_line)
		{
			g_string_append(out, "\r\n");
			r->total_rx += r->col;
			r->col = 0;
		}
	}
}

void gt_view_render_tx(GtViewRender *r, const guint8 *data, gsize len, GtSendOrigin origin, GString *out)
{
	if(!r->opts.echo || len == 0)
		return;

	if(r->opts.mode == GT_VIEW_ASCII)
	{
		if(origin == GT_SEND_KEYBOARD)
		{
			/* Frappe : répétée dans la ligne, sans préfixe ni retour à la ligne ajouté */
			g_string_append(out, COLOR_ECHO);
			g_string_append_len(out, (const gchar *)data, (gssize)len);
			g_string_append(out, COLOR_RESET);
			r->at_bol = (data[len - 1] == '\n');
		}
		else
		{
			/* Envoi en bloc : sa propre ligne « > », sans les CR/LF finaux */
			gsize n = len;

			while(n > 0 && (data[n - 1] == '\r' || data[n - 1] == '\n'))
				n--;
			if(!r->at_bol)
				g_string_append(out, "\r\n");
			g_string_append(out, COLOR_ECHO "> ");
			g_string_append_len(out, (const gchar *)data, (gssize)n);
			g_string_append(out, COLOR_RESET "\r\n");
			r->at_bol = TRUE;
		}
		return;
	}

	/* Vue hexadécimale : l'envoi a ses propres lignes et ne compte pas dans l'index des octets reçus */
	guint col = 0;

	if(r->col != 0)
	{
		g_string_append(out, "\r\n");
		r->total_rx += r->col;
		r->col = 0;
	}
	g_string_append(out, COLOR_ECHO);
	for(gsize i = 0; i < len; i++)
	{
		if(col == 0 && r->opts.show_index)
			g_string_append(out, "     >  ");
		hex_put_byte(r, data[i], &col, out);
		if(col == r->opts.bytes_per_line)
		{
			g_string_append(out, "\r\n");
			col = 0;
		}
	}
	if(col != 0)
		g_string_append(out, "\r\n");
	g_string_append(out, COLOR_RESET);
}

void gt_view_render_record(GtViewRender *r, const GtRecord *rec, GString *out)
{
	if(rec->kind == GT_RECORD_RX)
		gt_view_render_rx(r, rec->data, rec->len, rec->wall_time_us, out);
	else
		gt_view_render_tx(r, rec->data, rec->len, rec->origin, out);
}
