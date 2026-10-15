/* Tests de macro_override.c : valeurs propres d'un onglet lié à un modèle de macros
 * (docs/superpowers/specs/2026-10-15-modeles-macros-design.md, §2.3, §2.4, §2.8). */

#include <glib.h>
#include <string.h>

#include "macro_set.h"
#include "macro_override.h"
#include "test_util.h"

/* Valeur par défaut k d'une macro, NULL ≡ "" */
static const gchar *
def_at(const GtMacro *m, guint k)
{
	return m->args != NULL && k < g_strv_length(m->args) ? m->args[k] : "";
}

/* Valeur propre de rang k d'une entrée (NULL : rang non surchargé) */
static const gchar *
ov_arg(const GtMacroOverride *o, guint k)
{
	return k < o->args->len ? g_ptr_array_index(o->args, k) : NULL;
}

/* Base : V (%d,%s), D (×2, %d), « » (libellé vide, %d), AT (sans argument), S (séparateur), M (%#Mode) */
static GtMacroSet *
base_new(void)
{
	GtMacroSet *set = gt_macro_set_new();
	GtMacroLists *l = gt_macro_lists_new();
	const gchar *mx[] = { "100", "", NULL };

	gt_macro_lists_add(l, "Mode", "Auto", "A");
	gt_macro_lists_add(l, "Mode", "Manuel", "M");
	gt_macro_set_take_lists(set, l);
	{
		guint v = gt_macro_set_append(set, "V", "", "V%d,%s\\r", "");

		g_assert_cmpint(gt_macro_set_set_limits(set, v, NULL, mx, NULL, 2), ==, 0);
	}
	gt_macro_set_append(set, "D", "", "D%d", "");
	gt_macro_set_append(set, "D", "", "E%d", "");
	gt_macro_set_append(set, "", "", "N%d", "");
	gt_macro_set_append(set, "AT", "", "AT\\r", "");
	gt_macro_set_append(set, "S", "", "[Groupe] %&", "");
	gt_macro_set_append(set, "M", "", "M%#Mode", "");
	return set;
}

static guint
id_at(const GtMacroSet *set, guint i)
{
	return gt_macro_set_get(set, i)->id;
}

static void
test_diff(void)
{
	GtMacroSet *base = base_new(), *eff, *autre;
	GtMacroOverrides *o;
	const GtMacroOverride *e;

	/* Identiques : aucune valeur propre */
	eff = gt_macro_set_copy(base);
	o = gt_macro_overrides_diff(base, eff);
	g_assert_nonnull(o);
	g_assert_cmpuint(gt_macro_overrides_count(o), ==, 0);
	gt_macro_overrides_free(o);
	/* args NULL ≡ vecteur de "" */
	g_assert_true(gt_macro_set_set_arg(eff, id_at(eff, 0), 1, ""));
	o = gt_macro_overrides_diff(base, eff);
	g_assert_cmpuint(gt_macro_overrides_count(o), ==, 0);
	gt_macro_overrides_free(o);
	/* Rang 0 surchargé, rang 1 non ; seconde D (occurrence 1) ; libellé vide ; polling seul sur AT */
	g_assert_true(gt_macro_set_set_arg(eff, id_at(eff, 0), 0, "42"));
	g_assert_true(gt_macro_set_set_arg(eff, id_at(eff, 2), 0, "3"));
	g_assert_true(gt_macro_set_set_arg(eff, id_at(eff, 3), 0, "9"));
	g_assert_true(gt_macro_set_set_polling(eff, id_at(eff, 4), TRUE, 500));
	o = gt_macro_overrides_diff(base, eff);
	g_assert_cmpuint(gt_macro_overrides_count(o), ==, 4);
	e = gt_macro_overrides_get(o, 0);
	g_assert_cmpstr(e->label, ==, "V");
	g_assert_cmpuint(e->occurrence, ==, 0);
	g_assert_cmpstr(ov_arg(e, 0), ==, "42");
	g_assert_null(ov_arg(e, 1));
	g_assert_false(e->has_polling);
	e = gt_macro_overrides_get(o, 1);
	g_assert_cmpstr(e->label, ==, "D");
	g_assert_cmpuint(e->occurrence, ==, 1);
	g_assert_cmpstr(ov_arg(e, 0), ==, "3");
	e = gt_macro_overrides_get(o, 2);
	g_assert_cmpstr(e->label, ==, "");
	g_assert_cmpuint(e->occurrence, ==, 0);
	g_assert_cmpstr(ov_arg(e, 0), ==, "9");
	e = gt_macro_overrides_get(o, 3);
	g_assert_cmpstr(e->label, ==, "AT");
	g_assert_cmpuint(e->args->len, ==, 0);
	g_assert_true(e->has_polling);
	g_assert_true(e->polling);
	g_assert_cmpuint(e->polling_period_ms, ==, 500);
	g_assert_null(gt_macro_overrides_get(o, 4));
	gt_macro_overrides_free(o);
	gt_macro_set_free(eff);
	/* Chaîne vide surchargée : le modèle a une valeur, le projet la vide */
	g_assert_true(gt_macro_set_set_arg(base, id_at(base, 0), 1, "abc"));
	eff = gt_macro_set_copy(base);
	g_assert_true(gt_macro_set_set_arg(eff, id_at(eff, 0), 1, ""));
	o = gt_macro_overrides_diff(base, eff);
	g_assert_cmpuint(gt_macro_overrides_count(o), ==, 1);
	e = gt_macro_overrides_get(o, 0);
	g_assert_null(ov_arg(e, 0));
	g_assert_cmpstr(ov_arg(e, 1), ==, "");
	gt_macro_overrides_free(o);
	/* Seule la période change (mode inchangé) : valeur propre de polling */
	g_assert_true(gt_macro_set_set_polling(eff, id_at(eff, 0), FALSE, 20));
	o = gt_macro_overrides_diff(base, eff);
	e = gt_macro_overrides_get(o, 0);
	g_assert_true(e->has_polling);
	g_assert_false(e->polling);
	g_assert_cmpuint(e->polling_period_ms, ==, 20);
	gt_macro_overrides_free(o);
	/* Structures différentes : NULL */
	autre = gt_macro_set_copy(base);
	gt_macro_set_append(autre, "X", "", "x", "");
	g_assert_null(gt_macro_overrides_diff(base, autre));
	gt_macro_set_free(autre);
	gt_macro_set_free(eff);
	gt_macro_set_free(base);
	gt_macro_overrides_free(NULL);
	g_assert_cmpuint(gt_macro_overrides_count(NULL), ==, 0);
}

/* Aller-retour diff → apply : mêmes valeurs par défaut et même polling que le jeu effectif */
static void
test_apply_aller_retour(void)
{
	GtMacroSet *base = base_new(), *eff, *res;
	GtMacroOverrides *o;
	guint dropped = 99;

	g_assert_true(gt_macro_set_set_arg(base, id_at(base, 0), 1, "abc"));
	eff = gt_macro_set_copy(base);
	g_assert_true(gt_macro_set_set_arg(eff, id_at(eff, 0), 0, "42"));
	g_assert_true(gt_macro_set_set_arg(eff, id_at(eff, 0), 1, ""));
	g_assert_true(gt_macro_set_set_arg(eff, id_at(eff, 2), 0, "3"));
	g_assert_true(gt_macro_set_set_arg(eff, id_at(eff, 3), 0, "9"));
	g_assert_true(gt_macro_set_set_arg(eff, id_at(eff, 6), 0, "M"));
	g_assert_true(gt_macro_set_set_polling(eff, id_at(eff, 4), TRUE, 500));
	g_assert_true(gt_macro_set_set_polling(eff, id_at(eff, 1), FALSE, 30));
	o = gt_macro_overrides_diff(base, eff);
	res = gt_macro_overrides_apply(o, base, &dropped);
	g_assert_cmpuint(dropped, ==, 0);
	g_assert_true(gt_macro_set_same_structure(res, eff));
	for(guint i = 0; i < gt_macro_set_count(eff); i++)
	{
		const GtMacro *a = gt_macro_set_get(eff, i), *b = gt_macro_set_get(res, i);

		/* Nouveaux identifiants */
		g_assert_cmpuint(a->id, !=, b->id);
		for(guint k = 0; k < 2; k++)
			g_assert_cmpstr(def_at(a, k), ==, def_at(b, k));
		g_assert_cmpint(a->polling, ==, b->polling);
		g_assert_cmpuint(a->polling_period_ms, ==, b->polling_period_ms);
	}
	gt_macro_set_free(res);
	/* La base n'est pas touchée ; NULL : simple copie */
	g_assert_cmpstr(def_at(gt_macro_set_get(base, 0), 0), ==, "");
	res = gt_macro_overrides_apply(NULL, base, &dropped);
	g_assert_cmpuint(dropped, ==, 0);
	g_assert_true(gt_macro_set_same_structure(res, base));
	g_assert_cmpstr(def_at(gt_macro_set_get(res, 0), 1), ==, "abc");
	gt_macro_set_free(res);
	/* dropped NULL accepté */
	res = gt_macro_overrides_apply(o, base, NULL);
	gt_macro_set_free(res);
	gt_macro_overrides_free(o);
	gt_macro_set_free(eff);
	gt_macro_set_free(base);
}

static void
test_apply_abandons(void)
{
	GtMacroSet *base = base_new(), *res;
	GtMacroOverrides *o = gt_macro_overrides_new();
	guint dropped = 0;

	/* Macro disparue : chaque élément compte (2 arguments + polling) */
	gt_macro_overrides_set_arg(o, "Disparue", 0, 0, "1");
	gt_macro_overrides_set_arg(o, "Disparue", 0, 2, "2");
	gt_macro_overrides_set_polling(o, "Disparue", 0, TRUE, 100);
	/* Occurrence inexistante (deux D seulement) */
	gt_macro_overrides_set_arg(o, "D", 2, 0, "1");
	/* Rang dépassé (V a 2 arguments) ; limite durcie (max 100) ; rang 1 accepté */
	gt_macro_overrides_set_arg(o, "V", 0, 2, "x");
	gt_macro_overrides_set_arg(o, "V", 0, 0, "150");
	gt_macro_overrides_set_arg(o, "V", 0, 1, "ok");
	/* Valeur invalide pour le type */
	gt_macro_overrides_set_arg(o, "D", 0, 0, "abc");
	/* Période hors bornes (jamais ramenée) */
	gt_macro_overrides_set_polling(o, "D", 1, TRUE, 0);
	gt_macro_overrides_set_polling(o, "", 0, TRUE, GT_MACRO_PERIOD_MAX_MS + 1);
	/* Polling sur un séparateur ; une macro sans argument garde le sien */
	gt_macro_overrides_set_polling(o, "S", 0, TRUE, 100);
	gt_macro_overrides_set_polling(o, "AT", 0, TRUE, GT_MACRO_PERIOD_MAX_MS);
	/* Liste : non vérifiée */
	gt_macro_overrides_set_arg(o, "M", 0, 0, "ZZ");
	res = gt_macro_overrides_apply(o, base, &dropped);
	g_assert_cmpuint(dropped, ==, 3 + 1 + 1 + 1 + 1 + 1 + 1 + 1);
	g_assert_true(gt_macro_set_same_structure(res, base));
	g_assert_cmpstr(def_at(gt_macro_set_get(res, 0), 0), ==, "");
	g_assert_cmpstr(def_at(gt_macro_set_get(res, 0), 1), ==, "ok");
	g_assert_cmpstr(def_at(gt_macro_set_get(res, 1), 0), ==, "");
	g_assert_false(gt_macro_set_get(res, 2)->polling);
	g_assert_false(gt_macro_set_get(res, 3)->polling);
	g_assert_true(gt_macro_set_get(res, 4)->polling);
	g_assert_cmpuint(gt_macro_set_get(res, 4)->polling_period_ms, ==, GT_MACRO_PERIOD_MAX_MS);
	g_assert_false(gt_macro_set_get(res, 5)->polling);
	g_assert_cmpstr(def_at(gt_macro_set_get(res, 6), 0), ==, "ZZ");
	gt_macro_set_free(res);
	gt_macro_overrides_free(o);
	gt_macro_set_free(base);
}

/* Ajout, remplacement en place, fusion rang par rang, copie */
/* Un %s refusé par son jeu de caractères ou sa longueur maximale (limites du modèle) est abandonné */
static void
test_apply_texte_limites(void)
{
	GtMacroSet *base = gt_macro_set_new(), *res;
	GtMacroOverrides *o = gt_macro_overrides_new();
	const gchar *mx[] = { "3", "", NULL }, *cs[] = { "", "printable", NULL };
	guint id = gt_macro_set_append(base, "T", "", "%s,%s", ""), dropped = 0;

	g_assert_cmpint(gt_macro_set_set_limits(base, id, NULL, mx, cs, 2), ==, 0);
	gt_macro_overrides_set_arg(o, "T", 0, 0, "abcd");
	gt_macro_overrides_set_arg(o, "T", 0, 1, "\xc3\xa9");
	res = gt_macro_overrides_apply(o, base, &dropped);
	g_assert_cmpuint(dropped, ==, 2);
	g_assert_cmpstr(def_at(gt_macro_set_get(res, 0), 0), ==, "");
	g_assert_cmpstr(def_at(gt_macro_set_get(res, 0), 1), ==, "");
	gt_macro_set_free(res);
	/* Dans les limites : appliquées */
	gt_macro_overrides_set_arg(o, "T", 0, 0, "abc");
	gt_macro_overrides_set_arg(o, "T", 0, 1, "e~");
	res = gt_macro_overrides_apply(o, base, &dropped);
	g_assert_cmpuint(dropped, ==, 0);
	g_assert_cmpstr(def_at(gt_macro_set_get(res, 0), 0), ==, "abc");
	g_assert_cmpstr(def_at(gt_macro_set_get(res, 0), 1), ==, "e~");
	gt_macro_set_free(res);
	gt_macro_overrides_free(o);
	gt_macro_set_free(base);
}

/* Valeur propre vide d'un argument facultatif (omis à l'envoi) : acceptée malgré les limites du modèle, à l'application comme à la fusion ;
 * un argument non facultatif vide reste refusé par ses limites */
static void
test_apply_facultatif_vide(void)
{
	GtMacroSet *base = gt_macro_set_new(), *after = NULL, *res;
	GtMacroOverrides *o = gt_macro_overrides_new();
	const gchar *mn[] = { "2", "1", NULL }, *def[] = { "ab", "5", NULL }, *opt[] = { "1", "1", NULL }, *pre[] = { ",", ",", NULL };
	guint id = gt_macro_set_append(base, "T", "", "T%s%d", ""), dropped = 0;

	g_assert_cmpint(gt_macro_set_set_limits(base, id, mn, NULL, NULL, 2), ==, 0);
	g_assert_true(gt_macro_set_set_args(base, id, def, 2));
	/* Non facultatifs : "" refusé par la longueur minimale 2 et par le minimum 1 */
	gt_macro_overrides_set_arg(o, "T", 0, 0, "");
	gt_macro_overrides_set_arg(o, "T", 0, 1, "");
	res = gt_macro_overrides_apply(o, base, &dropped);
	g_assert_cmpuint(dropped, ==, 2);
	g_assert_cmpstr(def_at(gt_macro_set_get(res, 0), 0), ==, "ab");
	gt_macro_set_free(res);
	/* Facultatifs : "" appliqué, rien d'abandonné */
	g_assert_true(gt_macro_set_set_optional(base, id, opt, pre, 2));
	res = gt_macro_overrides_apply(o, base, &dropped);
	g_assert_cmpuint(dropped, ==, 0);
	g_assert_cmpstr(def_at(gt_macro_set_get(res, 0), 0), ==, "");
	g_assert_cmpstr(def_at(gt_macro_set_get(res, 0), 1), ==, "");
	/* Fusion (modèle aux défauts vides, valeurs propres "xy" et "7", édition qui ne touche pas ces défauts) : le défaut vide du modèle est
	 * repris, la valeur propre ne fuit pas dans le modèle */
	gt_macro_set_free(base);
	base = gt_macro_set_copy(res);
	{
		const gchar *propres[] = { "xy", "7", NULL };
		GtMacroSet *before = gt_macro_set_clone(base), *merged;

		g_assert_true(gt_macro_set_set_args(before, gt_macro_set_get(before, 0)->id, propres, 2));
		after = gt_macro_set_clone(before);
		merged = gt_macro_overrides_merge_template(base, before, after);
		g_assert_cmpstr(def_at(gt_macro_set_get(merged, 0), 0), ==, "");
		g_assert_cmpstr(def_at(gt_macro_set_get(merged, 0), 1), ==, "");
		gt_macro_set_free(merged);
		gt_macro_set_free(before);
	}
	gt_macro_set_free(after);
	gt_macro_set_free(res);
	gt_macro_overrides_free(o);
	gt_macro_set_free(base);
}

static void
test_set_et_copie(void)
{
	GtMacroOverrides *o = gt_macro_overrides_new(), *c;
	const GtMacroOverride *e;

	gt_macro_overrides_set_arg(o, "A", 0, 2, "x");
	gt_macro_overrides_set_arg(o, "B", 0, 0, "y");
	gt_macro_overrides_set_arg(o, "A", 0, 0, "z");
	gt_macro_overrides_set_arg(o, "A", 1, 0, "w");
	gt_macro_overrides_set_polling(o, "A", 0, TRUE, 300);
	gt_macro_overrides_set_arg(o, "A", 0, 2, NULL);
	g_assert_cmpuint(gt_macro_overrides_count(o), ==, 3);
	e = gt_macro_overrides_get(o, 0);
	g_assert_cmpstr(e->label, ==, "A");
	g_assert_cmpuint(e->args->len, ==, 3);
	g_assert_cmpstr(ov_arg(e, 0), ==, "z");
	g_assert_null(ov_arg(e, 1));
	g_assert_cmpstr(ov_arg(e, 2), ==, "");
	g_assert_true(e->has_polling);
	g_assert_cmpuint(e->polling_period_ms, ==, 300);
	g_assert_cmpstr(gt_macro_overrides_get(o, 1)->label, ==, "B");
	g_assert_cmpuint(gt_macro_overrides_get(o, 2)->occurrence, ==, 1);
	c = gt_macro_overrides_copy(o);
	gt_macro_overrides_free(o);
	g_assert_cmpuint(gt_macro_overrides_count(c), ==, 3);
	e = gt_macro_overrides_get(c, 0);
	g_assert_cmpstr(ov_arg(e, 0), ==, "z");
	g_assert_true(e->polling);
	gt_macro_overrides_free(c);
	c = gt_macro_overrides_copy(NULL);
	g_assert_cmpuint(gt_macro_overrides_count(c), ==, 0);
	gt_macro_overrides_free(c);
}

/* Lignes d'édition reprenant un jeu (comme l'éditeur) ; elles pointent dans ce jeu, qui ne doit pas être celui qu'on édite */
static GtMacroEdit *
rows_of(const GtMacroSet *set)
{
	GtMacroEdit *rows = g_new0(GtMacroEdit, gt_macro_set_count(set));

	for(guint i = 0; i < gt_macro_set_count(set); i++)
	{
		const GtMacro *m = gt_macro_set_get(set, i);

		rows[i].id = m->id;
		rows[i].label = m->label;
		rows[i].shortcut = m->shortcut;
		rows[i].action = m->action;
		rows[i].tab = m->tab;
		rows[i].args = (const gchar *const *)m->args;
		rows[i].arg_min = (const gchar *const *)m->arg_min;
		rows[i].arg_max = (const gchar *const *)m->arg_max;
		rows[i].arg_charset = (const gchar *const *)m->arg_charset;
	}
	return rows;
}

static void
test_merge_template(void)
{
	GtMacroSet *base = gt_macro_set_new(), *before, *after, *res;
	GtMacroOverrides *o;
	GtMacroEdit *rows;
	const gchar *b_v[] = { "1", "a", NULL }, *b_w[] = { "2", NULL }, *b_t[] = { "3", NULL };
	const gchar *e_v[] = { "7", "b", NULL }, *e_w[] = { "8", NULL }, *e_t[] = { "9", NULL };
	const gchar *n_v[] = { "7", "c", NULL }, *n_w[] = { "5", NULL }, *n_t[] = { "9", NULL };
	const gchar *mx[] = { "6", "", NULL };
	guint v, w, t, p;

	v = gt_macro_set_append(base, "V", "", "V%d,%s", "");
	w = gt_macro_set_append(base, "W", "", "W%d", "");
	t = gt_macro_set_append(base, "T", "", "T%d", "");
	p = gt_macro_set_append(base, "P", "", "P", "");
	gt_macro_set_append(base, "Z", "", "Z", "");
	gt_macro_set_set_args(base, v, b_v, 2);
	gt_macro_set_set_args(base, w, b_w, 1);
	gt_macro_set_set_args(base, t, b_t, 1);
	gt_macro_set_set_polling(base, p, FALSE, 1000);
	/* Jeu effectif : valeurs propres du projet sur V, W, T et polling sur P */
	before = gt_macro_set_copy(base);
	gt_macro_set_set_args(before, id_at(before, 0), e_v, 2);
	gt_macro_set_set_args(before, id_at(before, 1), e_w, 1);
	gt_macro_set_set_args(before, id_at(before, 2), e_t, 1);
	gt_macro_set_set_polling(before, id_at(before, 3), TRUE, 200);
	/* Édition : V rang 0 inchangé, rang 1 changé ; W changé ; T passe de %d à %s (type changé, valeur inchangée) ; ordre inversé
	 * pour V et W ; Z supprimée ; N ajoutée */
	after = gt_macro_set_clone(before);
	rows = rows_of(before);
	{
		GtMacroEdit nouvelles[5] = { rows[1], rows[0], rows[2], rows[3],
		                             { 0, "N", "", "N%d", "", NULL, NULL, NULL, NULL } };
		const gchar *n_def[] = { "4", NULL };

		nouvelles[1].args = n_v;
		nouvelles[0].args = n_w;
		nouvelles[2].action = "T%s";
		nouvelles[2].args = n_t;
		nouvelles[4].args = n_def;
		gt_macro_set_apply_edits(after, nouvelles, 5, NULL);
	}
	g_free(rows);
	res = gt_macro_overrides_merge_template(base, before, after);
	g_assert_true(gt_macro_set_same_structure(res, after));
	/* W : changé → la valeur de l'édition va au modèle */
	g_assert_cmpstr(gt_macro_set_get(res, 0)->label, ==, "W");
	g_assert_cmpstr(def_at(gt_macro_set_get(res, 0), 0), ==, "5");
	/* V : rang 0 inchangé → valeur du modèle (la valeur propre ne fuit pas) ; rang 1 changé → édition */
	g_assert_cmpstr(def_at(gt_macro_set_get(res, 1), 0), ==, "1");
	g_assert_cmpstr(def_at(gt_macro_set_get(res, 1), 1), ==, "c");
	/* T : type changé → valeur de l'édition */
	g_assert_cmpstr(def_at(gt_macro_set_get(res, 2), 0), ==, "9");
	/* P : polling du modèle */
	g_assert_false(gt_macro_set_get(res, 3)->polling);
	g_assert_cmpuint(gt_macro_set_get(res, 3)->polling_period_ms, ==, 1000);
	/* N : nouvelle, telle quelle */
	g_assert_cmpstr(def_at(gt_macro_set_get(res, 4), 0), ==, "4");
	g_assert_cmpuint(gt_macro_set_count(res), ==, 5);
	/* La valeur propre restante (V rang 0) se retrouve par diff avec le jeu effectif édité */
	o = gt_macro_overrides_diff(res, after);
	g_assert_nonnull(o);
	g_assert_cmpuint(gt_macro_overrides_count(o), ==, 2);
	g_assert_cmpstr(gt_macro_overrides_get(o, 0)->label, ==, "V");
	g_assert_cmpstr(ov_arg(gt_macro_overrides_get(o, 0), 0), ==, "7");
	g_assert_null(ov_arg(gt_macro_overrides_get(o, 0), 1));
	g_assert_cmpstr(gt_macro_overrides_get(o, 1)->label, ==, "P");
	g_assert_true(gt_macro_overrides_get(o, 1)->has_polling);
	gt_macro_overrides_free(o);
	gt_macro_set_free(res);
	gt_macro_set_free(after);
	/* Nouvelle limite (max 0) qui refuse la valeur du modèle (1) → la valeur de l'édition est gardée */
	after = gt_macro_set_clone(before);
	{
		const gchar *mx0[] = { "0", "", NULL };

		rows = rows_of(before);
		rows[0].arg_max = mx0;
		gt_macro_set_apply_edits(after, rows, gt_macro_set_count(before), NULL);
		g_free(rows);
	}
	res = gt_macro_overrides_merge_template(base, before, after);
	g_assert_cmpstr(def_at(gt_macro_set_get(res, 0), 0), ==, "7");
	g_assert_cmpstr(def_at(gt_macro_set_get(res, 0), 1), ==, "a");
	gt_macro_set_free(res);
	gt_macro_set_free(after);
	/* Limite qui accepte la valeur du modèle (max 6 : 1 passe) → valeur du modèle */
	after = gt_macro_set_clone(before);
	rows = rows_of(before);
	rows[0].arg_max = mx;
	gt_macro_set_apply_edits(after, rows, gt_macro_set_count(before), NULL);
	g_free(rows);
	res = gt_macro_overrides_merge_template(base, before, after);
	g_assert_cmpstr(def_at(gt_macro_set_get(res, 0), 0), ==, "1");
	gt_macro_set_free(res);
	gt_macro_set_free(after);
	gt_macro_set_free(before);
	gt_macro_set_free(base);
}

/* Rang de liste : même liste → valeur du modèle reprise sans vérification ; liste renommée → valeur de l'édition */
static void
test_merge_template_listes(void)
{
	GtMacroSet *base = gt_macro_set_new(), *before, *after, *res;
	GtMacroLists *l = gt_macro_lists_new(), *l2;
	GtMacroEdit *rows;
	const gchar *b_def[] = { "ZZ", NULL }, *e_def[] = { "M", NULL };
	guint id;

	gt_macro_lists_add(l, "Mode", "Auto", "A");
	gt_macro_lists_add(l, "Mode", "Manuel", "M");
	gt_macro_set_take_lists(base, l);
	id = gt_macro_set_append(base, "M", "", "M%#Mode", "");
	g_assert_true(gt_macro_set_set_args(base, id, b_def, 1));
	before = gt_macro_set_copy(base);
	g_assert_true(gt_macro_set_set_args(before, id_at(before, 0), e_def, 1));
	/* Même liste, valeur inchangée : « ZZ » (hors liste) reprise du modèle */
	after = gt_macro_set_clone(before);
	rows = rows_of(before);
	gt_macro_set_apply_edits(after, rows, 1, NULL);
	res = gt_macro_overrides_merge_template(base, before, after);
	g_assert_cmpstr(def_at(gt_macro_set_get(res, 0), 0), ==, "ZZ");
	gt_macro_set_free(res);
	gt_macro_set_free(after);
	/* Liste renommée : la valeur de l'édition va au modèle */
	after = gt_macro_set_clone(before);
	l2 = gt_macro_lists_copy(gt_macro_set_get_lists(before));
	gt_macro_lists_add(l2, "Mode2", "Auto", "A");
	gt_macro_lists_add(l2, "Mode2", "Manuel", "M");
	rows[0].action = "M%#Mode2";
	gt_macro_set_apply_edits(after, rows, 1, l2);
	g_assert_cmpstr(def_at(gt_macro_set_get(after, 0), 0), ==, "M");
	res = gt_macro_overrides_merge_template(base, before, after);
	g_assert_cmpstr(def_at(gt_macro_set_get(res, 0), 0), ==, "M");
	gt_macro_set_free(res);
	gt_macro_set_free(after);
	g_free(rows);
	gt_macro_set_free(before);
	gt_macro_set_free(base);
}

static gchar *
kf_text(GKeyFile *kf)
{
	return g_key_file_to_data(kf, NULL, NULL);
}

static void
test_key_file_aller_retour(void)
{
	GtMacroOverrides *o = gt_macro_overrides_new(), *lu;
	GKeyFile *kf = g_key_file_new();
	const GtMacroOverride *e;
	gchar *texte;

	gt_macro_overrides_set_arg(o, "V", 0, 0, "4 2");
	gt_macro_overrides_set_arg(o, "V", 0, 2, "");
	gt_macro_overrides_set_arg(o, "D", 1, 0, "a;b\\c\n");
	gt_macro_overrides_set_polling(o, "AT", 0, TRUE, 250);
	gt_macro_overrides_set_polling(o, "", 3, FALSE, 7);
	/* Groupes d'un autre onglet et ancien groupe du même préfixe (retiré à l'écriture) */
	g_key_file_set_string(kf, "tab:1:override:1", "label", "Autre");
	g_key_file_set_string(kf, "tab:0:override:9", "label", "Ancien");
	g_key_file_set_string(kf, "tab:0:macro:1", "label", "X");
	gt_macro_overrides_to_key_file(o, kf, "tab:0:");
	g_assert_false(g_key_file_has_group(kf, "tab:0:override:9"));
	g_assert_true(g_key_file_has_group(kf, "tab:1:override:1"));
	g_assert_true(g_key_file_has_group(kf, "tab:0:macro:1"));
	texte = kf_text(kf);
	g_assert_nonnull(strstr(texte, "[tab:0:override:1]\nlabel=V\narg_1=4 2\narg_3=\n"));
	g_assert_nonnull(strstr(texte, "[tab:0:override:2]\nlabel=D\noccurrence=1\n"));
	g_assert_nonnull(strstr(texte, "[tab:0:override:3]\nlabel=AT\npolling=true\npolling_period_ms=250\n"));
	g_assert_nonnull(strstr(texte, "[tab:0:override:4]\nlabel=\noccurrence=3\npolling=false\npolling_period_ms=7\n"));
	g_free(texte);
	lu = gt_macro_overrides_from_key_file(kf, "tab:0:", "p.gtkterm");
	g_assert_cmpuint(gt_macro_overrides_count(lu), ==, 4);
	e = gt_macro_overrides_get(lu, 0);
	g_assert_cmpstr(e->label, ==, "V");
	g_assert_cmpuint(e->occurrence, ==, 0);
	g_assert_cmpuint(e->args->len, ==, 3);
	g_assert_cmpstr(ov_arg(e, 0), ==, "4 2");
	g_assert_null(ov_arg(e, 1));
	g_assert_cmpstr(ov_arg(e, 2), ==, "");
	g_assert_false(e->has_polling);
	e = gt_macro_overrides_get(lu, 1);
	g_assert_cmpuint(e->occurrence, ==, 1);
	g_assert_cmpstr(ov_arg(e, 0), ==, "a;b\\c\n");
	e = gt_macro_overrides_get(lu, 2);
	g_assert_true(e->has_polling);
	g_assert_true(e->polling);
	g_assert_cmpuint(e->polling_period_ms, ==, 250);
	e = gt_macro_overrides_get(lu, 3);
	g_assert_cmpstr(e->label, ==, "");
	g_assert_cmpuint(e->occurrence, ==, 3);
	g_assert_false(e->polling);
	g_assert_cmpuint(e->polling_period_ms, ==, 7);
	gt_macro_overrides_free(lu);
	/* Préfixe tab:1: ne lit que son groupe */
	lu = gt_macro_overrides_from_key_file(kf, "tab:1:", "p.gtkterm");
	g_assert_cmpuint(gt_macro_overrides_count(lu), ==, 1);
	g_assert_cmpstr(gt_macro_overrides_get(lu, 0)->label, ==, "Autre");
	gt_macro_overrides_free(lu);
	/* Préfixe vide : aucun groupe « tab:… » */
	lu = gt_macro_overrides_from_key_file(kf, "", "p.gtkterm");
	g_assert_cmpuint(gt_macro_overrides_count(lu), ==, 0);
	gt_macro_overrides_free(lu);
	/* Écrire une liste vide retire les groupes du préfixe */
	gt_macro_overrides_free(o);
	o = gt_macro_overrides_new();
	gt_macro_overrides_to_key_file(o, kf, "tab:0:");
	g_assert_false(g_key_file_has_group(kf, "tab:0:override:1"));
	g_assert_true(g_key_file_has_group(kf, "tab:1:override:1"));
	gt_macro_overrides_free(o);
	g_key_file_free(kf);
}

static void
test_key_file_tolerant(void)
{
	const gchar *texte =
		"[tab:0:override:1]\n"
		"arg_1=sans libelle\n"
		"[tab:0:override:2]\n"
		"label=A\n"
		"occurrence=-1\n"
		"arg_1=x\n"
		"[tab:0:override:3]\n"
		"label=B\n"
		"occurrence=deux\n"
		"[tab:0:override:4]\n"
		"label=C\n"
		"arg_0=zero\n"
		"arg_x=x\n"
		"arg_2=deux\n"
		"arg_01=zero en tete\n"
		"arg_1025=trop loin\n"
		"inconnue=ignoree\n"
		"polling=true\n"
		"[tab:0:override:5]\n"
		"label=D\n"
		"polling_period_ms=100\n"
		"[tab:0:override:6]\n"
		"label=E\n"
		"polling=peut-etre\n"
		"polling_period_ms=100\n"
		"[tab:0:override:7]\n"
		"label=F\n"
		"polling=true\n"
		"polling_period_ms=abc\n"
		"[tab:0:override:8]\n"
		"label=G\n"
		"polling=true\n"
		"polling_period_ms=0\n"
		"[tab:0:override:9]\n"
		"label=C\n"
		"occurrence=0\n"
		"arg_1=derniere\n"
		"[tab:0:overrideX]\n"
		"label=X\n"
		"[tab:0:override]\n"
		"label=Y\n"
		"[tab:0:override:10]\n"
		"label=H\n"
		"arg_1=bad\\escape\n";
	GKeyFile *kf = g_key_file_new();
	GtMacroOverrides *lu;
	const GtMacroOverride *e;

	g_assert_true(g_key_file_load_from_data(kf, texte, -1, G_KEY_FILE_NONE, NULL));
	/* Groupe 1 sans libellé ; occurrence négative ; occurrence illisible (groupes entiers ignorés) ; arg_0 ; arg_x ; arg_01 ; arg_1025 ;
	 * polling seul ; période seule ; polling illisible ; période illisible ; doublon de C (le dernier gagne) ; échappement invalide.
	 * « overrideX » et « override » (sans deux-points) ne sont pas des groupes de valeurs propres. */
	for(guint i = 0; i < 11; i++)
		g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*p.gtkterm: [tab:0:override:*invalid value ignored*");
	g_test_expect_message(NULL, G_LOG_LEVEL_WARNING,
	                      "p.gtkterm: [tab:0:override:9] duplicates [tab:0:override:4]: the earlier group is ignored");
	g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*p.gtkterm: [tab:0:override:10] arg_1*invalid value ignored*");
	lu = gt_macro_overrides_from_key_file(kf, "tab:0:", "p.gtkterm");
	g_test_assert_expected_messages();
	g_assert_cmpuint(gt_macro_overrides_count(lu), ==, 6);
	/* C : en place de sa première lecture, remplacée entièrement par le doublon */
	e = gt_macro_overrides_get(lu, 0);
	g_assert_cmpstr(e->label, ==, "C");
	g_assert_cmpuint(e->args->len, ==, 1);
	g_assert_cmpstr(ov_arg(e, 0), ==, "derniere");
	g_assert_false(e->has_polling);
	e = gt_macro_overrides_get(lu, 1);
	g_assert_cmpstr(e->label, ==, "D");
	g_assert_false(e->has_polling);
	g_assert_cmpstr(gt_macro_overrides_get(lu, 2)->label, ==, "E");
	g_assert_false(gt_macro_overrides_get(lu, 2)->has_polling);
	g_assert_cmpstr(gt_macro_overrides_get(lu, 3)->label, ==, "F");
	g_assert_false(gt_macro_overrides_get(lu, 3)->has_polling);
	/* Période hors bornes gardée telle quelle (apply l'abandonne) */
	e = gt_macro_overrides_get(lu, 4);
	g_assert_cmpstr(e->label, ==, "G");
	g_assert_true(e->has_polling);
	g_assert_cmpuint(e->polling_period_ms, ==, 0);
	e = gt_macro_overrides_get(lu, 5);
	g_assert_cmpstr(e->label, ==, "H");
	g_assert_null(ov_arg(e, 0));
	gt_macro_overrides_free(lu);
	g_key_file_free(kf);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/macro_override/diff", test_diff);
	g_test_add_func("/macro_override/apply-aller-retour", test_apply_aller_retour);
	g_test_add_func("/macro_override/apply-abandons", test_apply_abandons);
	g_test_add_func("/macro_override/apply-texte-limites", test_apply_texte_limites);
	g_test_add_func("/macro_override/apply-facultatif-vide", test_apply_facultatif_vide);
	g_test_add_func("/macro_override/set-et-copie", test_set_et_copie);
	g_test_add_func("/macro_override/merge-template", test_merge_template);
	g_test_add_func("/macro_override/merge-template-listes", test_merge_template_listes);
	g_test_add_func("/macro_override/key-file-aller-retour", test_key_file_aller_retour);
	g_test_add_func("/macro_override/key-file-tolerant", test_key_file_tolerant);
	return g_test_run();
}
