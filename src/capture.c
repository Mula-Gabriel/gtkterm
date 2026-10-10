/***********************************************************************/
/* capture.c                                                           */
/*                                                                     */
/*   Purpose                                                           */
/*      Captures RAW et ASCII de l'historique (sous-projet 6a)         */
/*                                                                     */
/***********************************************************************/

#include <errno.h>
#include <stdio.h>

#include <config.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include "capture.h"
#include "rx_transform.h"

/* Écrit len octets dans un fichier créé ou remplacé ; fwrite et fclose vérifiés (un disque plein n'apparaît souvent qu'à la fermeture) */
static gboolean write_whole_file(const gchar *path, const guint8 *data, gsize len, GError **error)
{
	FILE *f;
	gint err = 0;

	f = g_fopen(path, "wb");
	if(f == NULL)
	{
		err = errno;
		g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(err), _("Cannot open file %s: %s"), path, g_strerror(err));
		return FALSE;
	}
	if(len > 0 && fwrite(data, 1, len, f) != len)
		err = errno != 0 ? errno : EIO;
	if(fclose(f) != 0 && err == 0)
		err = errno != 0 ? errno : EIO;
	if(err != 0)
	{
		g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(err), _("Cannot write file %s: %s"), path, g_strerror(err));
		return FALSE;
	}
	return TRUE;
}

gboolean gt_capture_save_raw(GtHistory *h, const gchar *path, GError **error)
{
	GBytes *rx;
	gsize n;
	const guint8 *p;
	gboolean ok;

	g_return_val_if_fail(h != NULL && path != NULL, FALSE);
	rx = gt_history_copy_rx(h);
	p = g_bytes_get_data(rx, &n);
	ok = write_whole_file(path, p, n, error);
	g_bytes_unref(rx);
	return ok;
}

typedef struct
{
	GtRxTransform *transform;
	GString *text;
} AsciiCapture;

/* Les envois (TX) ne font pas partie de la capture */
static void feed_rx_record(const GtRecord *rec, gpointer user_data)
{
	AsciiCapture *c = user_data;

	if(rec->kind == GT_RECORD_RX)
		gt_rx_transform_feed(c->transform, rec->data, rec->len, rec->wall_time_us, c->text);
}

static gboolean keep_ascii_byte(guint8 c)
{
	return c == '\t' || c == '\r' || c == '\n' || (c >= 0x20 && c != 0x7F);
}

gboolean gt_capture_save_ascii(GtHistory *h, gboolean crlf_auto, gboolean timestamp, const gchar *path, GError **error)
{
	AsciiCapture c;
	GString *text;
	gsize kept = 0;
	gboolean ok;

	g_return_val_if_fail(h != NULL && path != NULL, FALSE);
	c.transform = gt_rx_transform_new(crlf_auto, timestamp);
	c.text = text = g_string_new(NULL);
	gt_history_foreach(h, feed_rx_record, &c);
	/* Filtrage sur place : octets non signés, l'UTF-8 (≥ 0x80) reste */
	for(gsize i = 0; i < text->len; i++)
	{
		guint8 b = (guint8)text->str[i];

		if(keep_ascii_byte(b))
			text->str[kept++] = (gchar)b;
	}
	g_string_truncate(text, kept);
	ok = write_whole_file(path, (const guint8 *)text->str, text->len, error);
	g_string_free(text, TRUE);
	gt_rx_transform_free(c.transform);
	return ok;
}
