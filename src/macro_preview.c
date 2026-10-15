/* macro_preview.c — aperçu de la ligne envoyée par une macro (voir macro_preview.h). */

#include <string.h>

#include "macro_preview.h"

static void
run_free(gpointer data)
{
	GtPreviewRun *run = data;

	g_free(run->text);
	g_free(run);
}

/* Nom d'un octet de contrôle, NULL s'il n'en a pas. */
static const gchar *
control_name(guint8 b)
{
	switch(b)
	{
	case 0x00: return "NUL";
	case 0x07: return "BEL";
	case 0x08: return "BS";
	case 0x09: return "TAB";
	case 0x0A: return "LF";
	case 0x0B: return "VT";
	case 0x0C: return "FF";
	case 0x0D: return "CR";
	case 0x1B: return "ESC";
	case 0x7F: return "DEL";
	default:   return NULL;
	}
}

/* Ajoute un élément (caractère tel quel ou nom) au dernier run s'il est de même sorte et même argument, sinon ouvre un run.
 * *last_named : le dernier élément du dernier run était un nom ; un nom est séparé de ses voisins par une espace. */
static void
add_token(GPtrArray *runs, gboolean *last_named, GtPreviewKind kind, gint arg, const gchar *text, gsize len, gboolean named)
{
	GtPreviewRun *last = runs->len > 0 ? g_ptr_array_index(runs, runs->len - 1) : NULL;

	if(last != NULL && last->kind == kind && last->arg == arg)
	{
		gchar *joined;

		if(named || *last_named)
			joined = g_strdup_printf("%s %.*s", last->text, (gint)len, text);
		else
			joined = g_strdup_printf("%s%.*s", last->text, (gint)len, text);
		g_free(last->text);
		last->text = joined;
	}
	else
	{
		GtPreviewRun *run = g_new0(GtPreviewRun, 1);

		run->kind = kind;
		run->arg = arg;
		run->text = g_strndup(text, len);
		g_ptr_array_add(runs, run);
	}
	*last_named = named;
}

/* Découpe data[start..end[ d'un segment de sorte seg_kind en éléments. */
static void
add_segment(GPtrArray *runs, gboolean *last_named, const guint8 *data, gsize start, gsize end, GtMacroSegKind seg_kind, gint arg)
{
	gsize i = start;

	while(i < end)
	{
		guint8 b = data[i];
		gsize n = 0;
		const gchar *name = NULL;
		gchar hex[5];

		if(b >= 0x20 && b <= 0x7E)
			n = 1;
		else if(b >= 0x80)
		{
			gunichar c = g_utf8_get_char_validated((const gchar *)data + i, (gssize)(end - i));

			if(c != (gunichar)-1 && c != (gunichar)-2)
				n = g_utf8_skip[b];
		}
		if(n == 0)
		{
			name = control_name(b);
			if(name == NULL)
			{
				g_snprintf(hex, sizeof hex, "\\x%02X", b);
				name = hex;
			}
		}

		GtPreviewKind kind;

		if(seg_kind == GT_MACRO_SEG_ARG)
			kind = GT_PREVIEW_ARG;
		else if(seg_kind == GT_MACRO_SEG_ESCAPE || name != NULL)
			kind = GT_PREVIEW_SPECIAL;
		else
			kind = GT_PREVIEW_TEXT;
		if(kind != GT_PREVIEW_ARG)
			arg = -1;
		if(name != NULL)
			add_token(runs, last_named, kind, arg, name, strlen(name), TRUE);
		else
			add_token(runs, last_named, kind, arg, (const gchar *)data + i, n, FALSE);
		i += name != NULL ? 1 : n;
	}
}

GPtrArray *
gt_macro_preview_runs(const guint8 *data, gsize len, const GArray *segments)
{
	GPtrArray *runs = g_ptr_array_new_with_free_func(run_free);
	gboolean last_named = FALSE;

	if(data == NULL || len == 0)
		return runs;
	if(segments == NULL)
	{
		add_segment(runs, &last_named, data, 0, len, GT_MACRO_SEG_TEXT, -1);
		return runs;
	}
	for(guint k = 0; k < segments->len; k++)
	{
		const GtMacroSegment *seg = &g_array_index(segments, GtMacroSegment, k);
		gsize start = MIN((gsize)seg->start, len);

		if(seg->kind == GT_MACRO_SEG_OMITTED)
		{
			GtPreviewRun *run = g_new0(GtPreviewRun, 1);

			run->kind = GT_PREVIEW_OMITTED;
			run->arg = seg->arg;
			run->text = g_strdup("\xe2\x88\x85");
			g_ptr_array_add(runs, run);
			last_named = FALSE;
			continue;
		}

		gsize end = MIN((gsize)seg->start + seg->len, len);

		add_segment(runs, &last_named, data, start, end, seg->kind, seg->kind == GT_MACRO_SEG_ARG ? seg->arg : -1);
	}
	return runs;
}

gchar *
gt_macro_preview_hex(const guint8 *data, gsize len, gsize max)
{
	GString *s = g_string_new(NULL);
	gsize shown = MIN(len, max);

	for(gsize i = 0; i < shown; i++)
		g_string_append_printf(s, i ? " %02X" : "%02X", data[i]);
	if(len > max)
		g_string_append(s, shown > 0 ? " \xe2\x80\xa6" : "\xe2\x80\xa6");
	return g_string_free(s, FALSE);
}
