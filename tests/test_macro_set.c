/* Tests de macro_set.c : jeu de macros sans GTK (identifiants stables, arguments, polling, édition, expansion). */

#include <glib.h>
#include <string.h>

#include "macro_set.h"
#include "test_util.h"

static gchar *
expansion(const GtMacroSet *set, const GtMacro *m, const gchar *const *args, gint n)
{
	GBytes *b = gt_macro_expand(set, m, args, n);
	gsize len = 0;
	const gchar *data = g_bytes_get_data(b, &len);
	gchar *s = g_strndup(data != NULL ? data : "", len);

	g_bytes_unref(b);
	return s;
}

static void
test_normalisation(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint id = gt_macro_set_append(set, NULL, "None", NULL, NULL);
	const GtMacro *m = gt_macro_set_find(set, id);

	g_assert_nonnull(m);
	g_assert_cmpstr(m->label, ==, "");
	g_assert_cmpstr(m->shortcut, ==, "");
	g_assert_cmpstr(m->action, ==, "");
	g_assert_cmpstr(m->tab, ==, "");
	g_assert_null(m->args);
	g_assert_false(m->polling);
	g_assert_cmpuint(m->polling_period_ms, ==, GT_MACRO_PERIOD_DEFAULT_MS);
	gt_macro_set_free(set);
}

static void
test_identifiants(void)
{
	GtMacroSet *a = gt_macro_set_new(), *b;
	guint id1 = gt_macro_set_append(a, "A", "", "a", "");
	guint id2 = gt_macro_set_append(a, "B", "", "b", "");
	guint id3;

	g_assert_cmpuint(id1, !=, 0);
	g_assert_cmpuint(id2, !=, id1);
	g_assert_cmpuint(gt_macro_set_count(a), ==, 2);
	g_assert_cmpint(gt_macro_set_index_of(a, id2), ==, 1);
	g_assert_true(gt_macro_set_get(a, 0) == gt_macro_set_find(a, id1));
	g_assert_null(gt_macro_set_get(a, 2));
	g_assert_null(gt_macro_set_find(a, 0));
	g_assert_cmpint(gt_macro_set_index_of(a, 9999999), ==, -1);
	g_assert_true(gt_macro_set_find_label(a, "B") == gt_macro_set_find(a, id2));
	g_assert_null(gt_macro_set_find_label(a, "C"));
	g_assert_null(gt_macro_set_find_label(a, NULL));
	gt_macro_set_free(a);
	/* Un nouveau jeu ne réutilise jamais un identifiant d'un ancien jeu */
	b = gt_macro_set_new();
	id3 = gt_macro_set_append(b, "C", "", "c", "");
	g_assert_cmpuint(id3, !=, id1);
	g_assert_cmpuint(id3, !=, id2);
	gt_macro_set_free(b);
	gt_macro_set_free(NULL);
	g_assert_cmpuint(gt_macro_set_count(NULL), ==, 0);
}

static void
test_arguments(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint id = gt_macro_set_append(set, "V", "", "V%d,%s", "");
	guint sans = gt_macro_set_append(set, "P", "", "plain", "");
	const gchar *deux[] = { "1", "x" };
	const gchar *un[] = { "1" };
	const GtMacro *m;

	g_assert_true(gt_macro_set_set_arg(set, id, 1, "abc"));
	m = gt_macro_set_find(set, id);
	g_assert_nonnull(m->args);
	g_assert_cmpuint(g_strv_length(m->args), ==, 2);
	g_assert_cmpstr(m->args[0], ==, "");
	g_assert_cmpstr(m->args[1], ==, "abc");
	g_assert_false(gt_macro_set_set_arg(set, id, 2, "z"));
	g_assert_false(gt_macro_set_set_arg(set, id, -1, "z"));
	g_assert_false(gt_macro_set_set_arg(set, sans, 0, "z"));
	g_assert_false(gt_macro_set_set_arg(set, 9999999, 0, "z"));
	g_assert_true(gt_macro_set_set_arg(set, id, 0, NULL));
	g_assert_cmpstr(gt_macro_set_find(set, id)->args[0], ==, "");
	g_assert_true(gt_macro_set_set_args(set, id, deux, 2));
	g_assert_cmpstr(gt_macro_set_find(set, id)->args[1], ==, "x");
	g_assert_false(gt_macro_set_set_args(set, id, un, 1));
	g_assert_cmpstr(gt_macro_set_find(set, id)->args[0], ==, "1");
	g_assert_true(gt_macro_set_set_args(set, sans, NULL, 0));
	g_assert_null(gt_macro_set_find(set, sans)->args);
	g_assert_false(gt_macro_set_set_args(set, 9999999, NULL, 0));
	gt_macro_set_free(set);
}

static void
test_polling_bornes(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint id = gt_macro_set_append(set, "P", "", "p", "");

	g_assert_true(gt_macro_set_set_polling(set, id, TRUE, 0));
	g_assert_true(gt_macro_set_find(set, id)->polling);
	g_assert_cmpuint(gt_macro_set_find(set, id)->polling_period_ms, ==, GT_MACRO_PERIOD_DEFAULT_MS);
	g_assert_true(gt_macro_set_set_polling(set, id, TRUE, 1));
	g_assert_cmpuint(gt_macro_set_find(set, id)->polling_period_ms, ==, 1);
	g_assert_true(gt_macro_set_set_polling(set, id, FALSE, 86400000));
	g_assert_false(gt_macro_set_find(set, id)->polling);
	g_assert_cmpuint(gt_macro_set_find(set, id)->polling_period_ms, ==, 86400000);
	g_assert_true(gt_macro_set_set_polling(set, id, TRUE, 86400001));
	g_assert_cmpuint(gt_macro_set_find(set, id)->polling_period_ms, ==, GT_MACRO_PERIOD_MAX_MS);
	g_assert_false(gt_macro_set_set_polling(set, 9999999, TRUE, 10));
	gt_macro_set_free(set);
}

static void
test_periode_saisie(void)
{
	guint p = 77;

	g_assert_true(gt_macro_period_parse("2000", &p));
	g_assert_cmpuint(p, ==, 2000);
	g_assert_true(gt_macro_period_parse("1", &p));
	g_assert_cmpuint(p, ==, 1);
	g_assert_true(gt_macro_period_parse("86400000", &p));
	g_assert_cmpuint(p, ==, 86400000);
	p = 77;
	g_assert_false(gt_macro_period_parse("0", &p));
	g_assert_false(gt_macro_period_parse("86400001", &p));
	g_assert_false(gt_macro_period_parse("", &p));
	g_assert_false(gt_macro_period_parse("-5", &p));
	g_assert_false(gt_macro_period_parse("+5", &p));
	g_assert_false(gt_macro_period_parse(" 5", &p));
	g_assert_false(gt_macro_period_parse("5ms", &p));
	g_assert_false(gt_macro_period_parse("99999999999999999999", &p));
	g_assert_false(gt_macro_period_parse(NULL, &p));
	g_assert_cmpuint(p, ==, 77);
}

/* A (argument « 1 », raccourci), B (polling 250 ms), C (argument « 3 »). */
static GtMacroSet *
jeu_abc(guint *a, guint *b, guint *c)
{
	GtMacroSet *set = gt_macro_set_new();

	*a = gt_macro_set_append(set, "A", "<Control>F1", "A%d", "");
	*b = gt_macro_set_append(set, "B", "", "B", "");
	*c = gt_macro_set_append(set, "C", "", "C%d", "");
	g_assert_true(gt_macro_set_set_arg(set, *a, 0, "1"));
	g_assert_true(gt_macro_set_set_polling(set, *b, TRUE, 250));
	g_assert_true(gt_macro_set_set_arg(set, *c, 0, "3"));
	return set;
}

/* M16 : B supprimée, C (3e) ne devient pas la 2e macro « en polling » ; ses arguments la suivent. */
static void
test_edition_suppression(void)
{
	guint a, b, c;
	GtMacroSet *set = jeu_abc(&a, &b, &c);
	GtMacroEdit rows[] = { { a, "A", "<Control>F1", "A%d", "" }, { c, "C", "", "C%d", "" } };

	gt_macro_set_apply_edits(set, rows, G_N_ELEMENTS(rows), NULL);
	g_assert_cmpuint(gt_macro_set_count(set), ==, 2);
	g_assert_null(gt_macro_set_find(set, b));
	g_assert_cmpint(gt_macro_set_index_of(set, c), ==, 1);
	g_assert_cmpstr(gt_macro_set_find(set, c)->args[0], ==, "3");
	g_assert_false(gt_macro_set_find(set, c)->polling);
	g_assert_false(gt_macro_set_find(set, a)->polling);
	g_assert_cmpstr(gt_macro_set_find(set, a)->args[0], ==, "1");
	g_assert_cmpstr(gt_macro_set_find(set, a)->shortcut, ==, "<Control>F1");
	gt_macro_set_free(set);
}

static void
test_edition_deplacement_et_tri(void)
{
	guint a, b, c;
	GtMacroSet *set = jeu_abc(&a, &b, &c);
	GtMacroEdit rows[] = { { c, "C", "", "C%d", "" }, { b, "B", "", "B", "" }, { a, "A", "<Control>F1", "A%d", "" } };

	gt_macro_set_apply_edits(set, rows, G_N_ELEMENTS(rows), NULL);
	g_assert_cmpuint(gt_macro_set_count(set), ==, 3);
	g_assert_cmpuint(gt_macro_set_get(set, 0)->id, ==, c);
	g_assert_cmpuint(gt_macro_set_get(set, 1)->id, ==, b);
	g_assert_cmpuint(gt_macro_set_get(set, 2)->id, ==, a);
	g_assert_true(gt_macro_set_find(set, b)->polling);
	g_assert_cmpuint(gt_macro_set_find(set, b)->polling_period_ms, ==, 250);
	g_assert_false(gt_macro_set_find(set, a)->polling);
	g_assert_cmpstr(gt_macro_set_find(set, a)->args[0], ==, "1");
	g_assert_cmpstr(gt_macro_set_find(set, c)->args[0], ==, "3");
	gt_macro_set_free(set);
}

static void
test_edition_ajout_et_doublon(void)
{
	guint a, b, c, nouveau, doublon;
	GtMacroSet *set = jeu_abc(&a, &b, &c);
	GtMacroEdit rows[] = { { 0, "N", "None", "N%d", "T" }, { a, "A+", "<Control>F1", "A%d", "" }, { a, "A2", "", "A%d", "" } };

	gt_macro_set_apply_edits(set, rows, G_N_ELEMENTS(rows), NULL);
	g_assert_cmpuint(gt_macro_set_count(set), ==, 3);
	nouveau = gt_macro_set_get(set, 0)->id;
	doublon = gt_macro_set_get(set, 2)->id;
	g_assert_cmpuint(nouveau, !=, 0);
	g_assert_cmpuint(nouveau, !=, a);
	g_assert_cmpuint(nouveau, !=, b);
	g_assert_cmpuint(nouveau, !=, c);
	g_assert_cmpstr(gt_macro_set_get(set, 0)->shortcut, ==, "");
	g_assert_cmpstr(gt_macro_set_get(set, 0)->tab, ==, "T");
	g_assert_null(gt_macro_set_get(set, 0)->args);
	g_assert_false(gt_macro_set_get(set, 0)->polling);
	g_assert_cmpuint(gt_macro_set_get(set, 1)->id, ==, a);
	g_assert_cmpstr(gt_macro_set_get(set, 1)->label, ==, "A+");
	g_assert_cmpstr(gt_macro_set_get(set, 1)->args[0], ==, "1");
	/* Même identifiant sur deux lignes : la première le garde, la seconde est une macro nouvelle */
	g_assert_cmpuint(doublon, !=, a);
	g_assert_cmpstr(gt_macro_set_get(set, 2)->label, ==, "A2");
	g_assert_null(gt_macro_set_get(set, 2)->args);
	g_assert_null(gt_macro_set_find(set, b));
	g_assert_null(gt_macro_set_find(set, c));
	gt_macro_set_free(set);
}

static void
test_edition_change_arguments(void)
{
	guint a, b, c;
	GtMacroSet *set = jeu_abc(&a, &b, &c);
	GtMacroEdit rows[] = { { a, "A", "", "A%d %d", "" }, { b, "B", "", "B", "" }, { c, "C", "", "C%s", "" } };

	gt_macro_set_apply_edits(set, rows, G_N_ELEMENTS(rows), NULL);
	g_assert_null(gt_macro_set_find(set, a)->args);			/* 1 -> 2 spécificateurs */
	g_assert_cmpstr(gt_macro_set_find(set, c)->args[0], ==, "3");	/* toujours 1 spécificateur */
	g_assert_cmpstr(gt_macro_set_find(set, a)->shortcut, ==, "");
	gt_macro_set_free(set);
}

static void
test_listes_et_arguments(void)
{
	GtMacroSet *set = gt_macro_set_new();
	GtMacroLists *mode = gt_macro_lists_new(), *vide = gt_macro_lists_new();
	guint id = gt_macro_set_append(set, "M", "", "M %#Mode", "");
	GtMacroEdit rows[] = { { id, "M", "", "M %#Mode", "" } };

	/* Sans liste « Mode », « %#M » n'est pas un spécificateur complet : aucun argument */
	g_assert_false(gt_macro_set_set_arg(set, id, 0, "R"));
	gt_macro_lists_add(mode, "Mode", "Run", "R");
	gt_macro_set_take_lists(set, mode);
	g_assert_true(gt_macro_set_get_lists(set) == mode);
	g_assert_true(gt_macro_set_set_arg(set, id, 0, "R"));
	gt_macro_set_take_lists(set, vide);
	g_assert_null(gt_macro_set_find(set, id)->args);
	/* apply_edits prend les listes avant de recompter les arguments */
	mode = gt_macro_lists_new();
	gt_macro_lists_add(mode, "Mode", "Run", "R");
	gt_macro_set_take_lists(set, mode);
	g_assert_true(gt_macro_set_set_arg(set, id, 0, "R"));
	vide = gt_macro_lists_new();
	gt_macro_set_apply_edits(set, rows, 1, vide);
	g_assert_true(gt_macro_set_get_lists(set) == vide);
	g_assert_null(gt_macro_set_find(set, id)->args);
	gt_macro_set_take_lists(set, NULL);
	g_assert_nonnull(gt_macro_set_get_lists(set));
	gt_macro_set_free(set);
}

/* Reprendre les listes deja en place (meme pointeur) ne libere rien : elles restent lisibles et les arguments sont gardes */
static void
test_listes_reprises_identiques(void)
{
	GtMacroSet *set = gt_macro_set_new();
	GtMacroLists *mode = gt_macro_lists_new();
	guint id = gt_macro_set_append(set, "M", "", "M %#Mode", "");
	GtMacroEdit rows[] = { { id, "M", "", "M %#Mode", "" } };

	gt_macro_lists_add(mode, "Mode", "Run", "R");
	gt_macro_set_take_lists(set, mode);
	g_assert_true(gt_macro_set_set_arg(set, id, 0, "R"));
	gt_macro_set_take_lists(set, mode);
	g_assert_true(gt_macro_set_get_lists(set) == mode);
	g_assert_cmpint(gt_macro_lists_count(mode), ==, 1);
	g_assert_cmpstr(gt_macro_lists_name(mode, 0), ==, "Mode");
	g_assert_cmpstr(gt_macro_set_find(set, id)->args[0], ==, "R");
	gt_macro_set_apply_edits(set, rows, 1, mode);
	g_assert_true(gt_macro_set_get_lists(set) == mode);
	g_assert_cmpstr(gt_macro_lists_entry_value(mode, 0, 0), ==, "R");
	g_assert_cmpstr(gt_macro_set_find(set, id)->args[0], ==, "R");
	gt_macro_set_free(set);
}

static void
test_separateurs(void)
{
	gchar *label = (gchar *)"x";
	GtMacroSet *set = gt_macro_set_new();
	guint vide = gt_macro_set_append(set, "V", "", "", "");
	guint sep = gt_macro_set_append(set, "S", "", "[Titre]%&", "");
	guint ok = gt_macro_set_append(set, "O", "", "x", "");

	g_assert_true(gt_macro_action_is_separator("%&", &label));
	g_assert_null(label);
	g_assert_true(gt_macro_action_is_separator("[Titre]%&", &label));
	g_assert_cmpstr(label, ==, "Titre");
	g_free(label);
	g_assert_true(gt_macro_action_is_separator("[Titre]  %&", NULL));
	g_assert_false(gt_macro_action_is_separator("[Titre]x%&", NULL));
	g_assert_false(gt_macro_action_is_separator("%&x", NULL));
	g_assert_false(gt_macro_action_is_separator("[sans fin%&", NULL));
	g_assert_false(gt_macro_action_is_separator(NULL, NULL));
	g_assert_false(gt_macro_is_sendable(gt_macro_set_find(set, vide)));
	g_assert_false(gt_macro_is_sendable(gt_macro_set_find(set, sep)));
	g_assert_true(gt_macro_is_sendable(gt_macro_set_find(set, ok)));
	g_assert_false(gt_macro_is_sendable(NULL));
	gt_macro_set_free(set);
}

static void
test_expansion(void)
{
	GtMacroSet *set = gt_macro_set_new();
	GtMacroLists *mode = gt_macro_lists_new();
	guint v, nu, m, p;
	const gchar *sept[] = { "7" };
	gchar *s;

	gt_macro_lists_add(mode, "Mode", "Run", "R");
	gt_macro_set_take_lists(set, mode);
	v = gt_macro_set_append(set, "V", "", "V%d\\r", "");
	nu = gt_macro_set_append(set, "N", "", "N%d", "");
	m = gt_macro_set_append(set, "M", "", "M %#Mode", "");
	p = gt_macro_set_append(set, "P", "", "100%%", "");
	gt_macro_set_set_arg(set, v, 0, "42");
	gt_macro_set_set_arg(set, m, 0, "S");

	s = expansion(set, gt_macro_set_find(set, v), NULL, -1);
	g_assert_cmpstr(s, ==, "V42\r");
	g_free(s);
	s = expansion(set, gt_macro_set_find(set, v), sept, 1);
	g_assert_cmpstr(s, ==, "V7\r");
	g_free(s);
	s = expansion(set, gt_macro_set_find(set, v), NULL, 0);
	g_assert_cmpstr(s, ==, "V%d\r");
	g_free(s);
	s = expansion(set, gt_macro_set_find(set, nu), NULL, -1);
	g_assert_cmpstr(s, ==, "N%d");
	g_free(s);
	s = expansion(set, gt_macro_set_find(set, m), NULL, -1);
	g_assert_cmpstr(s, ==, "M S");
	g_free(s);
	s = expansion(set, gt_macro_set_find(set, p), NULL, -1);
	g_assert_cmpstr(s, ==, "100%");
	g_free(s);
	g_assert_null(gt_macro_expand(set, NULL, NULL, -1));
	gt_macro_set_free(set);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/macro_set/normalisation", test_normalisation);
	g_test_add_func("/macro_set/identifiants", test_identifiants);
	g_test_add_func("/macro_set/arguments", test_arguments);
	g_test_add_func("/macro_set/polling-bornes", test_polling_bornes);
	g_test_add_func("/macro_set/periode-saisie", test_periode_saisie);
	g_test_add_func("/macro_set/edition-suppression", test_edition_suppression);
	g_test_add_func("/macro_set/edition-deplacement-et-tri", test_edition_deplacement_et_tri);
	g_test_add_func("/macro_set/edition-ajout-et-doublon", test_edition_ajout_et_doublon);
	g_test_add_func("/macro_set/edition-change-arguments", test_edition_change_arguments);
	g_test_add_func("/macro_set/listes-et-arguments", test_listes_et_arguments);
	g_test_add_func("/macro_set/listes-reprises-identiques", test_listes_reprises_identiques);
	g_test_add_func("/macro_set/separateurs", test_separateurs);
	g_test_add_func("/macro_set/expansion", test_expansion);
	return g_test_run();
}
