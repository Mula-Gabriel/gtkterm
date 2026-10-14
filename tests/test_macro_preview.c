/* Tests de macro_preview.c : runs de l'aperçu de la ligne envoyée (texte, arguments, caractères spéciaux nommés) et octets en hexadécimal. */

#include <glib.h>
#include <string.h>

#include "macro_preview.h"
#include "macro_set.h"

/* Runs écrits « sorte[argument]=texte » séparés par « | » (T, A, S). */
static gchar *
runs_texte(const gchar *data, gsize len, const GtMacroSegment *segs, guint n_segs)
{
	GArray *a = NULL;
	GPtrArray *runs;
	GString *s = g_string_new(NULL);

	if(segs != NULL)
	{
		a = g_array_new(FALSE, FALSE, sizeof(GtMacroSegment));
		g_array_append_vals(a, segs, n_segs);
	}
	runs = gt_macro_preview_runs((const guint8 *)data, len, a);
	for(guint i = 0; i < runs->len; i++)
	{
		const GtPreviewRun *r = g_ptr_array_index(runs, i);

		if(i > 0)
			g_string_append_c(s, '|');
		g_string_append_c(s, "TAS"[r->kind]);
		if(r->kind == GT_PREVIEW_ARG)
			g_string_append_printf(s, "%d", r->arg);
		else
			g_assert_cmpint(r->arg, ==, -1);
		g_string_append_printf(s, "=%s", r->text);
	}
	g_ptr_array_unref(runs);
	if(a != NULL)
		g_array_unref(a);
	return g_string_free(s, FALSE);
}

static void
test_runs(void)
{
	gchar *t;

	g_assert_cmpint(GT_PREVIEW_TEXT, ==, 0);
	g_assert_cmpint(GT_PREVIEW_ARG, ==, 1);
	g_assert_cmpint(GT_PREVIEW_SPECIAL, ==, 2);

	/* Sans segments : tout est texte, les contrôles nommés sont spéciaux */
	t = runs_texte("AB\r\n", 4, NULL, 0);
	g_assert_cmpstr(t, ==, "T=AB|S=CR LF");
	g_free(t);

	/* Tous les noms ; DEL ; autres contrôles en \xHH majuscule */
	t = runs_texte("\0\a\b\t\n\v\f\r\x1b\x7f\x01\x1f", 12, NULL, 0);
	g_assert_cmpstr(t, ==, "S=NUL BEL BS TAB LF VT FF CR ESC DEL \\x01 \\x1F");
	g_free(t);

	/* UTF-8 valide tel quel ; 0xE9 seul : \xE9 */
	t = runs_texte("\xc3\xa9t\xc3\xa9", 5, NULL, 0);
	g_assert_cmpstr(t, ==, "T=\xc3\xa9t\xc3\xa9");
	g_free(t);
	t = runs_texte("a\xe9" "b", 3, NULL, 0);
	g_assert_cmpstr(t, ==, "T=a|S=\\xE9|T=b");
	g_free(t);
	/* Séquence UTF-8 tronquée */
	t = runs_texte("\xc3", 1, NULL, 0);
	g_assert_cmpstr(t, ==, "S=\\xC3");
	g_free(t);

	/* Segments : PWM 50 R\r\n — texte, argument, échappement */
	{
		const GtMacroSegment segs[] =
		{
			{ GT_MACRO_SEG_TEXT, -1, 0, 4 },
			{ GT_MACRO_SEG_ARG, 0, 4, 2 },
			{ GT_MACRO_SEG_TEXT, -1, 6, 1 },
			{ GT_MACRO_SEG_ARG, 1, 7, 1 },
			{ GT_MACRO_SEG_ESCAPE, -1, 8, 2 },
		};

		t = runs_texte("PWM 50 R\r\n", 10, segs, G_N_ELEMENTS(segs));
		g_assert_cmpstr(t, ==, "T=PWM |A0=50|T= |A1=R|S=CR LF");
		g_free(t);
	}
	/* Échappement imprimable (\x41) : spécial ; argument avec un contrôle et un octet latin1 : reste argument, noms compris */
	{
		const GtMacroSegment segs[] =
		{
			{ GT_MACRO_SEG_ESCAPE, -1, 0, 1 },
			{ GT_MACRO_SEG_ARG, 0, 1, 3 },
			{ GT_MACRO_SEG_ARG, 1, 4, 1 },
		};

		t = runs_texte("Ax\x1b\xe9y", 5, segs, G_N_ELEMENTS(segs));
		g_assert_cmpstr(t, ==, "S=A|A0=x ESC \\xE9|A1=y");
		g_free(t);
	}
	/* Vide */
	t = runs_texte("", 0, NULL, 0);
	g_assert_cmpstr(t, ==, "");
	g_free(t);
}

static void
test_hexa(void)
{
	gchar *t;

	t = gt_macro_preview_hex((const guint8 *)"PWM", 3, 256);
	g_assert_cmpstr(t, ==, "50 57 4D");
	g_free(t);
	t = gt_macro_preview_hex((const guint8 *)"\r\n\xe9", 3, 256);
	g_assert_cmpstr(t, ==, "0D 0A E9");
	g_free(t);
	t = gt_macro_preview_hex((const guint8 *)"ABCD", 4, 4);
	g_assert_cmpstr(t, ==, "41 42 43 44");
	g_free(t);
	t = gt_macro_preview_hex((const guint8 *)"ABCDE", 5, 4);
	g_assert_cmpstr(t, ==, "41 42 43 44 \xe2\x80\xa6");
	g_free(t);
	t = gt_macro_preview_hex(NULL, 0, 256);
	g_assert_cmpstr(t, ==, "");
	g_free(t);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/macro_preview/runs", test_runs);
	g_test_add_func("/macro_preview/hexa", test_hexa);
	return g_test_run();
}
