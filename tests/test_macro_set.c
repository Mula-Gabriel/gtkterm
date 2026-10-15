/* Tests de macro_set.c : jeu de macros sans GTK (identifiants stables, arguments, polling, édition, expansion). */

#include <glib.h>
#include <string.h>

#include "macro_set.h"
#include "macros_format.h"
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

static const gchar *const ARG1[] = { "1", NULL };
static const gchar *const ARG3[] = { "3", NULL };
static const gchar *const ARGR[] = { "R", NULL };

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
	GtMacroEdit rows[] = { { a, "A", "<Control>F1", "A%d", "", ARG1 }, { c, "C", "", "C%d", "", ARG3 } };

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
	GtMacroEdit rows[] = { { c, "C", "", "C%d", "", ARG3 }, { b, "B", "", "B", "" }, { a, "A", "<Control>F1", "A%d", "", ARG1 } };

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
	GtMacroEdit rows[] = { { 0, "N", "None", "N%d", "T" }, { a, "A+", "<Control>F1", "A%d", "", ARG1 }, { a, "A2", "", "A%d", "" } };

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
	GtMacroEdit rows[] = { { a, "A", "", "A%d %d", "", ARG1 }, { b, "B", "", "B", "" }, { c, "C", "", "C%s", "", ARG3 } };

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
	GtMacroEdit rows[] = { { id, "M", "", "M %#Mode", "", ARGR } };

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

static void
test_copie(void)
{
	GtMacroSet *set = gt_macro_set_new(), *copie;
	GtMacroLists *lists = gt_macro_lists_new();
	guint a, b;

	gt_macro_lists_add(lists, "Modes", "Lent", "0");
	gt_macro_set_take_lists(set, lists);
	a = gt_macro_set_append(set, "A", "<Control>F1", "A%d", "T1");
	b = gt_macro_set_append(set, "B", "", "B%#Modes", "");
	gt_macro_set_set_arg(set, a, 0, "5");
	gt_macro_set_set_polling(set, a, TRUE, 250);
	gt_macro_set_set_arg(set, b, 0, "0");
	copie = gt_macro_set_copy(set);
	g_assert_cmpuint(gt_macro_set_count(copie), ==, 2);
	for(guint i = 0; i < 2; i++)
	{
		const GtMacro *o = gt_macro_set_get(set, i), *c = gt_macro_set_get(copie, i);

		g_assert_cmpuint(c->id, !=, o->id);
		g_assert_cmpstr(c->label, ==, o->label);
		g_assert_cmpstr(c->shortcut, ==, o->shortcut);
		g_assert_cmpstr(c->action, ==, o->action);
		g_assert_cmpstr(c->tab, ==, o->tab);
		g_assert_cmpint(c->polling, ==, o->polling);
		g_assert_cmpuint(c->polling_period_ms, ==, o->polling_period_ms);
		g_assert_cmpstr(c->args[0], ==, o->args[0]);
	}
	g_assert_cmpuint(gt_macro_set_get(copie, 0)->polling_period_ms, ==, 250);
	gt_macro_set_free(set);
	g_assert_cmpint(gt_macro_lists_count(gt_macro_set_get_lists(copie)), ==, 1);
	g_assert_cmpstr(gt_macro_lists_entry_value(gt_macro_set_get_lists(copie), 0, 0), ==, "0");
	g_assert_cmpstr(gt_macro_set_get(copie, 1)->args[0], ==, "0");	/* la liste est prise avant les macros */
	gt_macro_set_free(copie);
}

static void
test_edits_avec_args(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint a = gt_macro_set_append(set, "A", "", "A%d;%s", "");
	const gchar *init[] = { "1", "x" };
	const gchar *deux[] = { "2", "y", NULL };
	const gchar *un[] = { "3", NULL };
	const gchar *sept[] = { "7", NULL };
	GtMacroEdit rows[] = { { a, "A", "", "A%d;%s", "", deux }, { 0, "N", "", "N%d", "", sept }, { 0, "Z", "", "Z%d", "", NULL } };

	gt_macro_set_set_args(set, a, init, 2);
	gt_macro_set_apply_edits(set, rows, G_N_ELEMENTS(rows), NULL);
	g_assert_cmpstr(gt_macro_set_get(set, 0)->args[0], ==, "2");
	g_assert_cmpstr(gt_macro_set_get(set, 0)->args[1], ==, "y");
	g_assert_cmpstr(gt_macro_set_get(set, 1)->args[0], ==, "7");
	g_assert_null(gt_macro_set_get(set, 2)->args);
	rows[0].args = un;	/* mauvais nombre */
	gt_macro_set_apply_edits(set, rows, 1, NULL);
	g_assert_null(gt_macro_set_get(set, 0)->args);
	gt_macro_set_free(set);
}

static void
test_limites(void)
{
	GtMacroSet *set = gt_macro_set_new(), *copie;
	guint a = gt_macro_set_append(set, "A", "", "A%d %s", "");
	guint b = gt_macro_set_append(set, "B", "", "B", "");
	const gchar *const mn[] = { "0", "2", NULL };
	const gchar *const mx[] = { "100", "8", NULL };
	const gchar *const cs[] = { "", "printable", NULL };
	const gchar *const court[] = { "0", NULL };
	const gchar *const mauvais[] = { "abc", "2", NULL };
	const gchar *const inverse[] = { "50", "9", NULL };
	const gchar *const bas[] = { "40", "3", NULL };
	const gchar *const vides[] = { "", "", NULL };
	const GtMacro *m;

	/* Pose valide */
	g_assert_cmpint(gt_macro_set_set_limits(set, a, mn, mx, cs, 2), ==, 0);
	m = gt_macro_set_find(set, a);
	g_assert_cmpstr(m->arg_min[1], ==, "2");
	g_assert_cmpstr(m->arg_max[0], ==, "100");
	g_assert_cmpstr(m->arg_charset[1], ==, "printable");
	g_assert_cmpstr(m->arg_charset[0], ==, "");

	/* Refus : longueur, macro inconnue, nombre d'arguments faux : rien ne change */
	g_assert_cmpint(gt_macro_set_set_limits(set, a, court, mx, cs, 2), ==, -1);
	g_assert_cmpint(gt_macro_set_set_limits(set, a, mn, mx, cs, 3), ==, -1);
	g_assert_cmpint(gt_macro_set_set_limits(set, 9999, mn, mx, cs, 2), ==, -1);
	g_assert_cmpint(gt_macro_set_set_limits(set, b, mn, mx, cs, 2), ==, -1);
	g_assert_cmpstr(m->arg_max[0], ==, "100");
	g_assert_cmpstr(m->arg_charset[1], ==, "printable");

	/* Entrée invalide : remise à "" et comptée ; le reste est gardé */
	g_assert_cmpint(gt_macro_set_set_limits(set, a, mauvais, mx, NULL, 2), ==, 1);
	m = gt_macro_set_find(set, a);
	g_assert_cmpstr(m->arg_min[0], ==, "");
	g_assert_cmpstr(m->arg_min[1], ==, "2");
	g_assert_null(m->arg_charset);

	/* min > max : les deux remis à "" */
	g_assert_cmpint(gt_macro_set_set_limits(set, a, inverse, mx, NULL, 2), ==, 2);
	m = gt_macro_set_find(set, a);
	g_assert_cmpstr(m->arg_min[0], ==, "50");
	g_assert_cmpstr(m->arg_max[0], ==, "100");
	g_assert_cmpstr(m->arg_min[1], ==, "");
	g_assert_cmpstr(m->arg_max[1], ==, "");

	/* Vecteur tout vide : NULL */
	g_assert_cmpint(gt_macro_set_set_limits(set, a, vides, NULL, NULL, 2), ==, 0);
	m = gt_macro_set_find(set, a);
	g_assert_null(m->arg_min);
	g_assert_null(m->arg_max);
	g_assert_null(m->arg_charset);

	/* Copie profonde */
	g_assert_cmpint(gt_macro_set_set_limits(set, a, mn, mx, cs, 2), ==, 0);
	copie = gt_macro_set_copy(set);
	m = gt_macro_set_get(copie, 0);
	g_assert_cmpstr(m->arg_min[1], ==, "2");
	g_assert_cmpstr(m->arg_max[0], ==, "100");
	g_assert_cmpstr(m->arg_charset[1], ==, "printable");
	g_assert_true(m->arg_min != gt_macro_set_find(set, a)->arg_min);
	gt_macro_set_free(copie);

	/* apply_edits : les limites de la ligne, validées par check_args */
	{
		GtMacroEdit lignes[] = { { a, "A", "", "A%d %s", "", NULL, bas, mx, cs } };

		gt_macro_set_apply_edits(set, lignes, 1, NULL);
		m = gt_macro_set_find(set, a);
		g_assert_cmpstr(m->arg_min[0], ==, "40");
		g_assert_cmpstr(m->arg_min[1], ==, "3");
		g_assert_cmpstr(m->arg_charset[1], ==, "printable");
	}
	{
		/* Pas de limites dans la ligne : abandonnées */
		GtMacroEdit lignes[] = { { a, "A", "", "A%d %s", "" } };

		gt_macro_set_apply_edits(set, lignes, 1, NULL);
		m = gt_macro_set_find(set, a);
		g_assert_null(m->arg_min);
		g_assert_null(m->arg_max);
		g_assert_null(m->arg_charset);
	}

	/* « %s %d » → « %d %s » : même nombre, types échangés ; limites invalides remises à "" */
	{
		GtMacroEdit l2[] = { { a, "A", "", "A%s %d", "", NULL, mn, mx, cs } };
		GtMacroEdit l3[] = { { a, "A", "", "A%d %s", "", NULL, mn, mx, cs } };

		gt_macro_set_apply_edits(set, l2, 1, NULL);
		m = gt_macro_set_find(set, a);
		/* %s : min "0" ok, max "100" ok ; %d : min "2" ok, max "8" ok, charset "printable" refusé sur %d */
		g_assert_cmpstr(m->arg_min[0], ==, "0");
		g_assert_cmpstr(m->arg_max[1], ==, "8");
		g_assert_null(m->arg_charset);	/* "printable" refusé sur %d, vecteur tout vide : NULL */
		gt_macro_set_apply_edits(set, l3, 1, NULL);
		m = gt_macro_set_find(set, a);
		g_assert_cmpstr(m->arg_charset[1], ==, "printable");
	}
	/* Nombre d'arguments changé : tout abandonné */
	{
		GtMacroEdit l[] = { { a, "A", "", "A%d", "", NULL, mn, mx, cs } };

		gt_macro_set_apply_edits(set, l, 1, NULL);
		m = gt_macro_set_find(set, a);
		g_assert_null(m->arg_min);
		g_assert_null(m->arg_max);
		g_assert_null(m->arg_charset);
	}
	gt_macro_set_free(set);
}


/* ---- Segments : les octets restent ceux de parse_macro_string(format_action_with_args()) ---- */

typedef struct
{
	const gchar *action;
	const gchar *arg1;	/* NULL : aucun argument */
	const gchar *arg2;	/* NULL : au plus un argument */
} cas_octets_t;

/* Actions de test_macros_format.c (expansion), plus les échappements à cheval sur un argument. */
static const cas_octets_t cas_octets[] =
{
	{ "V%d\\r", "42", NULL }, { "%hhd", "200", NULL }, { "%hhu", "300", NULL }, { "%hd", "70000", NULL },
	{ "%u", "-1", NULL }, { "%lu", "-1", NULL }, { "%d", "-5", NULL }, { "%+d", "5", NULL }, { "%-5d|", "5", NULL },
	{ "%i", "42", NULL }, { "%d", "12abc", NULL }, { "%d", "", NULL }, { "%x", "255", NULL }, { "%X", "255", NULL },
	{ "%02X", "10", NULL }, { "%x", "0x1f", NULL }, { "%X", "0x1f", NULL }, { "%d", "0x10", NULL }, { "%u", "0x10", NULL },
	{ "%d", "010", NULL }, { "%o", "010", NULL }, { "%x", "ff", NULL }, { "%x", "010", NULL }, { "%o", "8", NULL },
	{ "%#x", "255", NULL }, { "%#08x", "255", NULL }, { "%#o", "8", NULL }, { "%f", "2.00000", NULL }, { "%f", "1e3", NULL },
	{ "%f", "abc", NULL }, { "%.2f", "3.14159", NULL }, { "%5.1f", "2", NULL }, { "%lf", "2.50", NULL },
	{ "%e", "1234.5", NULL }, { "%hf", "2.0", NULL }, { "%5s", "abc", NULL }, { "%s%s", "a", "b" },
	{ "A\\r%sB", "x", NULL }, { "%s", "a\\b", NULL }, { "%s", "\\x41", NULL }, { "%s", "\\r", NULL },
	{ "%s", "50%", NULL }, { "%5s", "a\\b", NULL }, { "%cn", "\\", NULL }, { "%c", "A", NULL }, { "%c", "", NULL },
	{ "100%%", "1", NULL }, { "100%%", NULL, NULL }, { "%d %d", "1", NULL }, { "%d", NULL, NULL }, { "plain", NULL, NULL },
	{ "[Speed] %d", "5", NULL }, { "[a] %d [b] %d", "1", "2" }, { "XY[a]%d", "5", NULL }, { "XY[a%%]%d", "5", NULL },
	{ "arr[0] %d", "5", NULL }, { "50% off", "9", NULL }, { "%#Mode", "S", NULL }, { "%#Mode", "\\x02", NULL },
	{ "%d%&", "1", NULL },
	{ "\\x%02X", "65", NULL }, { "\\x4%d", "1", NULL }, { "\\%c", "r", NULL }, { "[a\\r] %d", "7", NULL },
	{ "%%d %d", "3", NULL }, { "%#Mode\\r\\n", "R", NULL }, { "[x] %s|%d", "\\", "4" }, { "[%d] %d", "123", "4" },
	{ "%s\\x4", "1", NULL }, { "\\", NULL, NULL }, { "", NULL, NULL },
};

/* Jeu à une macro, avec la liste « Mode » (Run=R, Stop=S). */
static GtMacroSet *
jeu_une_macro(const gchar *action, guint *id)
{
	GtMacroSet *set = gt_macro_set_new();
	GtMacroLists *lists = gt_macro_lists_new();

	gt_macro_lists_add(lists, "Mode", "Run", "R");
	gt_macro_lists_add(lists, "Mode", "Stop", "S");
	gt_macro_set_take_lists(set, lists);
	*id = gt_macro_set_append(set, "M", "", action, "");
	return set;
}

/* Les segments couvrent les octets bout à bout, sans vide, et deux voisins diffèrent par la sorte ou l'argument. */
static void
verifier_couverture(const GArray *segs, gsize len)
{
	guint pos = 0;

	for(guint i = 0; i < segs->len; i++)
	{
		const GtMacroSegment *g = &g_array_index(segs, GtMacroSegment, i);

		g_assert_cmpuint(g->start, ==, pos);
		g_assert_cmpuint(g->len, >, 0);
		g_assert_true(g->kind == GT_MACRO_SEG_ARG ? g->arg >= 0 : g->arg == -1);
		if(i > 0)
		{
			const GtMacroSegment *p = &g_array_index(segs, GtMacroSegment, i - 1);

			g_assert_true(p->kind != g->kind || p->arg != g->arg);
		}
		pos += g->len;
	}
	g_assert_cmpuint(pos, ==, len);
}

static void
test_segments_octets(void)
{
	for(guint i = 0; i < G_N_ELEMENTS(cas_octets); i++)
	{
		const cas_octets_t *c = &cas_octets[i];
		const gchar *args[2] = { c->arg1, c->arg2 };
		gint n = c->arg2 ? 2 : (c->arg1 ? 1 : 0);
		guint id;
		GtMacroSet *set = jeu_une_macro(c->action, &id);
		gchar *texte = format_action_with_args(gt_macro_set_get_lists(set), c->action, args, n);
		GByteArray *attendu = parse_macro_string(texte);
		GArray *segs = NULL;
		GBytes *b = gt_macro_expand_segments(set, gt_macro_set_find(set, id), args, n, &segs);
		GBytes *b2 = gt_macro_expand(set, gt_macro_set_find(set, id), args, n);
		gsize len = 0;
		const guint8 *data = g_bytes_get_data(b, &len);
		gchar *hex_attendu = test_hex(attendu->data, attendu->len);
		gchar *hex_obtenu = test_hex(data, len);

		if(g_strcmp0(hex_attendu, hex_obtenu) != 0)
			g_test_fail_printf("cas %u '%s' : attendu %s, obtenu %s", i, c->action, hex_attendu, hex_obtenu);
		g_assert_true(g_bytes_equal(b, b2));
		g_assert_nonnull(segs);
		verifier_couverture(segs, len);
		g_free(hex_obtenu);
		g_free(hex_attendu);
		g_array_unref(segs);
		g_bytes_unref(b2);
		g_bytes_unref(b);
		g_byte_array_free(attendu, TRUE);
		g_free(texte);
		gt_macro_set_free(set);
	}
}

/* Segments attendus, écrits « sorte:argument:longueur » (T, E, A). */
static gchar *
segments_texte(const GtMacroSet *set, guint id, const gchar *const *args, gint n)
{
	GArray *segs = NULL;
	GBytes *b = gt_macro_expand_segments(set, gt_macro_set_find(set, id), args, n, &segs);
	GString *s = g_string_new(NULL);

	for(guint i = 0; i < segs->len; i++)
	{
		const GtMacroSegment *g = &g_array_index(segs, GtMacroSegment, i);

		g_string_append_printf(s, "%s%c:%d:%u", i ? " " : "", "TEA"[g->kind], g->arg, g->len);
	}
	g_array_unref(segs);
	g_bytes_unref(b);
	return g_string_free(s, FALSE);
}

static void
test_segments_sortes(void)
{
	guint id;
	GtMacroSet *set = jeu_une_macro("PWM %d %.1f %#Mode\\r\\n", &id);
	gchar *t;

	g_assert_cmpint(GT_MACRO_SEG_TEXT, ==, 0);
	g_assert_cmpint(GT_MACRO_SEG_ESCAPE, ==, 1);
	g_assert_cmpint(GT_MACRO_SEG_ARG, ==, 2);
	{
		const gchar *args[] = { "50", "2.5", "R" };

		/* « PWM » T, « 50 » A0, « » T, « 2.5 » A1, « » T, « R » A2, CR LF E */
		t = segments_texte(set, id, args, 3);
		g_assert_cmpstr(t, ==, "T:-1:4 A:0:2 T:-1:1 A:1:3 T:-1:1 A:2:1 E:-1:2");
		g_free(t);
	}
	/* Arguments enregistrés (n < 0) et absents (NULL) : spécificateurs laissés en texte */
	t = segments_texte(set, id, NULL, 0);
	g_assert_cmpstr(t, ==, "T:-1:18 E:-1:2");
	g_free(t);
	gt_macro_set_free(set);

	/* \x%02X : l'échappement à cheval sur l'argument est un seul segment E */
	set = jeu_une_macro("\\x%02X", &id);
	{
		const gchar *args[] = { "65" };

		t = segments_texte(set, id, args, 1);
		g_assert_cmpstr(t, ==, "E:-1:1");
		g_free(t);
	}
	gt_macro_set_free(set);

	/* Antislash d'un argument (doublé en interne) : reste dans l'argument */
	set = jeu_une_macro("<%s>", &id);
	{
		const gchar *args[] = { "a\\b" };

		t = segments_texte(set, id, args, 1);
		g_assert_cmpstr(t, ==, "T:-1:1 A:0:3 T:-1:1");
		g_free(t);
	}
	gt_macro_set_free(set);
}

/* Jeu latin1 : un octet par caractère ; sans jeu, UTF-8. */
static void
test_segments_latin1(void)
{
	guint id;
	GtMacroSet *set = jeu_une_macro("%s|%5s", &id);
	const gchar *cs[] = { "latin1", "latin1", NULL };
	const gchar *args[] = { "\xc3\xa9\\", "\xc3\xa9" };
	gchar *t;

	t = expansion(set, gt_macro_set_find(set, id), args, 2);
	g_assert_cmpstr(t, ==, "\xc3\xa9\\|   \xc3\xa9");
	g_free(t);
	g_assert_cmpint(gt_macro_set_set_limits(set, id, NULL, NULL, cs, 2), ==, 0);
	t = expansion(set, gt_macro_set_find(set, id), args, 2);
	g_assert_cmpstr(t, ==, "\xe9\\|    \xe9");
	g_free(t);
	t = segments_texte(set, id, args, 2);
	g_assert_cmpstr(t, ==, "A:0:2 T:-1:1 A:1:5");
	g_free(t);
	gt_macro_set_free(set);
}

/* gt_macro_check_args : rang (0..) et raison du premier argument refusé ; listes ignorées. */
static void
test_verification(void)
{
	guint id;
	GtMacroSet *set = jeu_une_macro("%d %#Mode %s %c", &id);
	const GtMacro *m = gt_macro_set_find(set, id);
	const gchar *mn[] = { "0", "", "1", "", NULL };
	const gchar *mx[] = { "100", "", "4", "", NULL };
	const gchar *cs[] = { "", "", "printable", "", NULL };
	gint index = 99;
	gchar *raison = (gchar *)"x";

	/* Sans limites : tout nombre valide passe, un nombre invalide est refusé */
	{
		const gchar *ok[] = { "500", "zzz", "abcdefgh", "\xc3\xa9" };
		const gchar *ko[] = { "abc", "R", "x", "y" };

		g_assert_true(gt_macro_check_args(set, m, ok, 4, &index, &raison));
		g_assert_cmpint(index, ==, -1);
		g_assert_null(raison);
		g_assert_false(gt_macro_check_args(set, m, ko, 4, &index, &raison));
		g_assert_cmpint(index, ==, 0);
		g_assert_nonnull(raison);
		g_free(raison);
	}
	g_assert_cmpint(gt_macro_set_set_limits(set, id, mn, mx, cs, 4), ==, 0);
	m = gt_macro_set_find(set, id);
	{
		const gchar *ok[] = { "50", "R", "abcd", "" };
		const gchar *haut[] = { "101", "R", "abcd", "" };
		const gchar *long_[] = { "100", "R", "abcde", "" };
		const gchar *car[] = { "0", "R", "\xc3\xa9", "" };

		g_assert_true(gt_macro_check_args(set, m, ok, 4, &index, NULL));
		g_assert_cmpint(index, ==, -1);
		g_assert_false(gt_macro_check_args(set, m, haut, 4, &index, &raison));
		g_assert_cmpint(index, ==, 0);
		g_assert_cmpstr(raison, ==, "above maximum 100");
		g_free(raison);
		g_assert_false(gt_macro_check_args(set, m, long_, 4, &index, &raison));
		g_assert_cmpint(index, ==, 2);
		g_assert_nonnull(raison);
		g_free(raison);
		g_assert_false(gt_macro_check_args(set, m, car, 4, NULL, NULL));
		/* Moins d'arguments que la macro : seuls ceux fournis sont vérifiés ; plus : le surplus est ignoré */
		g_assert_true(gt_macro_check_args(set, m, ok, 1, &index, NULL));
		g_assert_false(gt_macro_check_args(set, m, haut, 1, &index, NULL));
		g_assert_cmpint(index, ==, 0);
		/* NULL : rien à vérifier */
		g_assert_true(gt_macro_check_args(set, m, NULL, 4, &index, &raison));
		g_assert_null(raison);
	}
	/* n < 0 : les valeurs par défaut de la macro */
	{
		const gchar *defauts[] = { "200", "R", "ab", "c", NULL };

		g_assert_true(gt_macro_set_set_args(set, id, defauts, 4));
		m = gt_macro_set_find(set, id);
		g_assert_false(gt_macro_check_args(set, m, NULL, -1, &index, NULL));
		g_assert_cmpint(index, ==, 0);
	}
	gt_macro_set_free(set);
}

/* §2.2 de la spec des modèles : même structure (identifiants et valeurs par défaut ignorés) */
static void
test_meme_structure(void)
{
	GtMacroSet *a = gt_macro_set_new(), *b;
	guint ida = gt_macro_set_append(a, "V", "Ctrl+1", "V%d\\r", "T");
	const gchar *def[] = { "5", NULL };
	const gchar *mn[] = { "0", NULL }, *vide[] = { "", NULL };
	GtMacroLists *l;
	guint idb;

	gt_macro_set_append(a, "W", "", "W", "");
	b = gt_macro_set_copy(a);
	g_assert_true(gt_macro_set_same_structure(a, b));
	g_assert_true(gt_macro_set_same_structure(a, a));
	/* Valeurs par défaut et polling : pas la structure */
	idb = gt_macro_set_get(b, 0)->id;
	g_assert_true(gt_macro_set_set_args(b, idb, def, 1));
	g_assert_true(gt_macro_set_set_polling(b, idb, TRUE, 50));
	g_assert_true(gt_macro_set_same_structure(a, b));
	/* Limites : NULL et vecteur entièrement vide sont égaux (set_limits rend NULL un vecteur vide) */
	g_assert_cmpint(gt_macro_set_set_limits(b, idb, vide, NULL, NULL, 1), ==, 0);
	g_assert_true(gt_macro_set_same_structure(a, b));
	g_assert_cmpint(gt_macro_set_set_limits(b, idb, mn, NULL, NULL, 1), ==, 0);
	g_assert_false(gt_macro_set_same_structure(a, b));
	g_assert_cmpint(gt_macro_set_set_limits(a, ida, mn, NULL, NULL, 1), ==, 0);
	g_assert_true(gt_macro_set_same_structure(a, b));
	gt_macro_set_free(b);
	/* Libellé, raccourci, action, onglet, nombre, ordre */
	{
		const gchar *champs[][4] = {
			{ "X", "Ctrl+1", "V%d\\r", "T" },
			{ "V", "Ctrl+2", "V%d\\r", "T" },
			{ "V", "Ctrl+1", "V%u\\r", "T" },
			{ "V", "Ctrl+1", "V%d\\r", "U" },
		};

		for(guint c = 0; c < G_N_ELEMENTS(champs); c++)
		{
			GtMacroEdit rows[2] = {
				{ 0, champs[c][0], champs[c][1], champs[c][2], champs[c][3], NULL, (const gchar *const *)mn, NULL, NULL },
				{ 0, "W", "", "W", "", NULL, NULL, NULL, NULL },
			};

			b = gt_macro_set_new();
			gt_macro_set_apply_edits(b, rows, 2, NULL);
			g_assert_false(gt_macro_set_same_structure(a, b));
			gt_macro_set_free(b);
		}
	}
	{
		GtMacroEdit rows[2] = {
			{ 0, "W", "", "W", "", NULL, NULL, NULL, NULL },
			{ 0, "V", "Ctrl+1", "V%d\\r", "T", NULL, (const gchar *const *)mn, NULL, NULL },
		};

		b = gt_macro_set_new();
		gt_macro_set_apply_edits(b, rows, 2, NULL);
		g_assert_false(gt_macro_set_same_structure(a, b));
		gt_macro_set_apply_edits(b, rows, 1, NULL);
		g_assert_false(gt_macro_set_same_structure(a, b));
		gt_macro_set_free(b);
	}
	/* Listes : noms, entrées, ordre */
	l = gt_macro_lists_new();
	gt_macro_lists_add(l, "Mode", "Auto", "A");
	gt_macro_lists_add(l, "Mode", "Manuel", "M");
	gt_macro_set_take_lists(a, l);
	b = gt_macro_set_copy(a);
	g_assert_true(gt_macro_set_same_structure(a, b));
	l = gt_macro_lists_new();
	gt_macro_lists_add(l, "Mode", "Manuel", "M");
	gt_macro_lists_add(l, "Mode", "Auto", "A");
	gt_macro_set_take_lists(b, l);
	g_assert_false(gt_macro_set_same_structure(a, b));
	l = gt_macro_lists_new();
	gt_macro_lists_add(l, "Mode", "Auto", "A");
	gt_macro_lists_add(l, "Mode", "Manuel", "X");
	gt_macro_set_take_lists(b, l);
	g_assert_false(gt_macro_set_same_structure(a, b));
	l = gt_macro_lists_new();
	gt_macro_lists_add(l, "Mode", "Auto", "A");
	gt_macro_lists_add(l, "Mode", "Manuel", "M");
	gt_macro_lists_add_list(l, "Vide");
	gt_macro_set_take_lists(b, l);
	g_assert_false(gt_macro_set_same_structure(a, b));
	l = gt_macro_lists_new();
	gt_macro_lists_add(l, "Mode", "Auto", "A");
	gt_macro_lists_add(l, "Mode", "Manuel", "M");
	gt_macro_set_take_lists(b, l);
	g_assert_true(gt_macro_set_same_structure(a, b));
	gt_macro_set_free(b);
	/* Jeux vides */
	b = gt_macro_set_new();
	g_assert_false(gt_macro_set_same_structure(a, b));
	gt_macro_set_free(a);
	a = gt_macro_set_new();
	g_assert_true(gt_macro_set_same_structure(a, b));
	gt_macro_set_free(a);
	gt_macro_set_free(b);
}

/* Clone : copie profonde qui garde les identifiants (simulations) */
static void
test_clone_garde_ids(void)
{
	GtMacroSet *set = gt_macro_set_new(), *clone;
	GtMacroLists *l = gt_macro_lists_new();
	guint id1, id2;
	const gchar *def[] = { "5", "A", NULL };
	const gchar *mn[] = { "1", "", NULL }, *mx[] = { "9", "", NULL };

	gt_macro_lists_add(l, "Mode", "Auto", "A");
	gt_macro_set_take_lists(set, l);
	id1 = gt_macro_set_append(set, "V", "Ctrl+1", "V%d %#Mode", "T");
	id2 = gt_macro_set_append(set, "W", "", "W", "");
	g_assert_true(gt_macro_set_set_args(set, id1, def, 2));
	g_assert_cmpint(gt_macro_set_set_limits(set, id1, mn, mx, NULL, 2), ==, 0);
	g_assert_true(gt_macro_set_set_polling(set, id2, TRUE, 250));
	clone = gt_macro_set_clone(set);
	g_assert_cmpuint(gt_macro_set_count(clone), ==, 2);
	g_assert_cmpuint(gt_macro_set_get(clone, 0)->id, ==, id1);
	g_assert_cmpuint(gt_macro_set_get(clone, 1)->id, ==, id2);
	g_assert_true(gt_macro_set_same_structure(set, clone));
	g_assert_true(gt_macro_set_get(clone, 0) != gt_macro_set_get(set, 0));
	g_assert_true(gt_macro_set_get_lists(clone) != gt_macro_set_get_lists(set));
	g_assert_cmpstr(gt_macro_set_get(clone, 0)->args[0], ==, "5");
	g_assert_cmpstr(gt_macro_set_get(clone, 0)->args[1], ==, "A");
	g_assert_cmpstr(gt_macro_set_get(clone, 0)->arg_max[0], ==, "9");
	g_assert_null(gt_macro_set_get(clone, 0)->arg_charset);
	g_assert_true(gt_macro_set_get(clone, 1)->polling);
	g_assert_cmpuint(gt_macro_set_get(clone, 1)->polling_period_ms, ==, 250);
	/* Indépendant de l'original */
	g_assert_true(gt_macro_set_set_arg(clone, id1, 0, "7"));
	g_assert_cmpstr(gt_macro_set_get(set, 0)->args[0], ==, "5");
	gt_macro_set_free(set);
	g_assert_cmpstr(gt_macro_set_get(clone, 0)->args[0], ==, "7");
	/* Une macro ajoutée au clone reçoit un identifiant neuf */
	g_assert_cmpuint(gt_macro_set_append(clone, "X", "", "x", ""), >, id2);
	gt_macro_set_free(clone);
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
	g_test_add_func("/macro_set/copie", test_copie);
	g_test_add_func("/macro_set/edits-avec-args", test_edits_avec_args);
	g_test_add_func("/macro_set/limites", test_limites);
	g_test_add_func("/macro_set/segments", test_segments_octets);
	g_test_add_func("/macro_set/segments-sortes", test_segments_sortes);
	g_test_add_func("/macro_set/segments-latin1", test_segments_latin1);
	g_test_add_func("/macro_set/verification", test_verification);
	g_test_add_func("/macro_set/same_structure", test_meme_structure);
	g_test_add_func("/macro_set/clone_keeps_ids", test_clone_garde_ids);
	return g_test_run();
}
