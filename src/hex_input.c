/***********************************************************************/
/* hex_input.c                                                         */
/*                                                                     */
/*   Purpose                                                           */
/*      Saisie hexadécimale : analyse et historique (sous-projet 6a)   */
/*                                                                     */
/***********************************************************************/

#include <string.h>

#include "hex_input.h"

G_DEFINE_QUARK(gt-hex-input-error-quark, gt_hex_input_error)

static gboolean is_separator(gchar c)
{
	return c == ';' || g_ascii_isspace(c);
}

GBytes *gt_hex_input_parse(const gchar *text, GError **error)
{
	GByteArray *out;
	gsize i = 0;

	g_return_val_if_fail(text != NULL, NULL);
	out = g_byte_array_new();
	for(;;)
	{
		gsize start;
		gsize len;

		while(text[i] != '\0' && is_separator(text[i]))
			i++;
		if(text[i] == '\0')
			break;
		start = i;
		while(text[i] != '\0' && !is_separator(text[i]))
			i++;
		len = i - start;
		/* 1 ou 2 chiffres hexadécimaux, rien d'autre (ni signe, ni préfixe 0x, ni troisième chiffre) */
		if(len > 2 || !g_ascii_isxdigit(text[start]) || (len == 2 && !g_ascii_isxdigit(text[start + 1])))
		{
			gchar *token = g_strndup(text + start, len);

			g_set_error_literal(error, GT_HEX_INPUT_ERROR, GT_HEX_INPUT_ERROR_INVALID, token);
			g_free(token);
			g_byte_array_unref(out);
			return NULL;
		}
		{
			guint8 octet = (guint8)g_ascii_xdigit_value(text[start]);

			if(len == 2)
				octet = (guint8)((octet << 4) | g_ascii_xdigit_value(text[start + 1]));
			g_byte_array_append(out, &octet, 1);
		}
	}
	return g_byte_array_free_to_bytes(out);
}

struct _GtHexHistory
{
	GPtrArray *entries;	/* gchar *, de la plus ancienne à la plus récente */
	guint max;
	guint cursor;		/* entries->len : après la dernière */
};

GtHexHistory *gt_hex_history_new(guint max)
{
	GtHexHistory *h = g_new0(GtHexHistory, 1);

	h->entries = g_ptr_array_new_with_free_func(g_free);
	h->max = MAX(max, 1);
	return h;
}

void gt_hex_history_free(GtHexHistory *h)
{
	if(h == NULL)
		return;
	g_ptr_array_unref(h->entries);
	g_free(h);
}

static gboolean is_blank(const gchar *text)
{
	for(const gchar *p = text; *p != '\0'; p++)
	{
		if(!g_ascii_isspace(*p))
			return FALSE;
	}
	return TRUE;
}

void gt_hex_history_add(GtHexHistory *h, const gchar *text)
{
	g_return_if_fail(h != NULL);

	if(text != NULL && !is_blank(text))
	{
		guint n = h->entries->len;

		if(n == 0 || strcmp(g_ptr_array_index(h->entries, n - 1), text) != 0)
		{
			g_ptr_array_add(h->entries, g_strdup(text));
			while(h->entries->len > h->max)
				g_ptr_array_remove_index(h->entries, 0);
		}
	}
	h->cursor = h->entries->len;
}

const gchar *gt_hex_history_previous(GtHexHistory *h)
{
	g_return_val_if_fail(h != NULL, NULL);

	if(h->cursor == 0)
		return NULL;
	h->cursor--;
	return g_ptr_array_index(h->entries, h->cursor);
}

const gchar *gt_hex_history_next(GtHexHistory *h)
{
	g_return_val_if_fail(h != NULL, "");

	if(h->cursor < h->entries->len)
		h->cursor++;
	if(h->cursor >= h->entries->len)
		return "";
	return g_ptr_array_index(h->entries, h->cursor);
}
