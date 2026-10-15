/* Tests de macro_view.c : règles d'affichage du panneau de macros (onglets, sortes de ligne, macros d'un onglet), sans GTK. */

#include <glib.h>

#include "macro_set.h"
#include "macro_view.h"

static guint id_a, id_sans_libelle, id_sans_action, id_sep, id_l, id_m, id_n, id_p;

static GtMacroSet *jeu(void)
{
	GtMacroSet *set = gt_macro_set_new();
	GtMacroLists *lists = gt_macro_lists_new();

	gt_macro_lists_add(lists, "Bin", "Lent", "0");
	gt_macro_lists_add(lists, "Bin", "Vif", "1");
	gt_macro_lists_add(lists, "Trois", "a", "1");
	gt_macro_lists_add(lists, "Trois", "b", "2");
	gt_macro_lists_add(lists, "Trois", "c", "3");
	gt_macro_set_take_lists(set, lists);
	id_a = gt_macro_set_append(set, "A", "", "<A>", "T2");
	id_sans_libelle = gt_macro_set_append(set, "", "<Control>F5", "<X>", "T9");
	id_sans_action = gt_macro_set_append(set, "B", "", "", "T9");
	id_sep = gt_macro_set_append(set, "Sep", "", "[Titre] %&", "");
	id_l = gt_macro_set_append(set, "L", "", "S %#Bin", "T1");
	id_m = gt_macro_set_append(set, "M", "", "S %#Trois", "T1");
	id_n = gt_macro_set_append(set, "N", "", "N%d %s", "T2");
	id_p = gt_macro_set_append(set, "P", "", "100%%", "");
	return set;
}

static gchar *joindre(GPtrArray *tabs)
{
	g_ptr_array_add(tabs, NULL);
	return g_strjoinv(",", (gchar **)tabs->pdata);
}

static void test_onglets(void)
{
	GtMacroSet *set = jeu();
	GHashTable *masques = g_hash_table_new(g_str_hash, g_str_equal);
	GPtrArray *tabs = gt_macro_view_tabs(set, "General", NULL);
	gchar *s = joindre(tabs);

	g_assert_cmpstr(s, ==, "T2,General,T1");	/* ordre d'apparition ; T9 n'a que des macros non affichées */
	g_free(s);
	g_ptr_array_unref(tabs);
	g_hash_table_add(masques, "T2");
	tabs = gt_macro_view_tabs(set, "General", masques);
	s = joindre(tabs);
	g_assert_cmpstr(s, ==, "General,T1");
	g_free(s);
	g_ptr_array_unref(tabs);
	g_hash_table_unref(masques);
	gt_macro_set_free(set);
}

static void test_sortes(void)
{
	GtMacroSet *set = jeu();
	const gchar *const facultatif[] = { "1", NULL };
	const gchar *const prefixe[] = { "", NULL };
	guint id_f = gt_macro_set_append(set, "F", "", "S%#Bin", "T1");

	g_assert_cmpint(gt_macro_view_kind(set, gt_macro_set_find(set, id_a)), ==, GT_MACRO_VIEW_BUTTON);
	g_assert_cmpint(gt_macro_view_kind(set, gt_macro_set_find(set, id_sans_libelle)), ==, GT_MACRO_VIEW_HIDDEN);
	g_assert_cmpint(gt_macro_view_kind(set, gt_macro_set_find(set, id_sans_action)), ==, GT_MACRO_VIEW_HIDDEN);
	g_assert_cmpint(gt_macro_view_kind(set, gt_macro_set_find(set, id_sep)), ==, GT_MACRO_VIEW_SEPARATOR);
	g_assert_cmpint(gt_macro_view_kind(set, gt_macro_set_find(set, id_l)), ==, GT_MACRO_VIEW_TWO_VALUES);
	g_assert_cmpint(gt_macro_view_kind(set, gt_macro_set_find(set, id_m)), ==, GT_MACRO_VIEW_ARGS);	/* liste de trois valeurs */
	g_assert_cmpint(gt_macro_view_kind(set, gt_macro_set_find(set, id_f)), ==, GT_MACRO_VIEW_TWO_VALUES);
	g_assert_true(gt_macro_set_set_optional(set, id_f, facultatif, prefixe, 1));
	g_assert_cmpint(gt_macro_view_kind(set, gt_macro_set_find(set, id_f)), ==, GT_MACRO_VIEW_ARGS);	/* liste facultative de deux valeurs : liste déroulante « (none) » */
	g_assert_cmpint(gt_macro_view_kind(set, gt_macro_set_find(set, id_n)), ==, GT_MACRO_VIEW_ARGS);
	g_assert_cmpint(gt_macro_view_kind(set, gt_macro_set_find(set, id_p)), ==, GT_MACRO_VIEW_BUTTON);	/* « %% » n'est pas un argument */
	g_assert_false(gt_macro_view_is_displayed(NULL));
	g_assert_cmpstr(gt_macro_view_tab(gt_macro_set_find(set, id_sep), "General"), ==, "General");
	g_assert_cmpstr(gt_macro_view_tab(gt_macro_set_find(set, id_l), "General"), ==, "T1");
	gt_macro_set_free(set);
}

static void test_macros_d_un_onglet(void)
{
	GtMacroSet *set = jeu();
	GArray *ids = gt_macro_view_tab_ids(set, "T9", "General");

	g_assert_cmpuint(ids->len, ==, 2);	/* macros non affichées comprises : leur polling doit aussi s'arrêter */
	g_assert_cmpuint(g_array_index(ids, guint, 0), ==, id_sans_libelle);
	g_assert_cmpuint(g_array_index(ids, guint, 1), ==, id_sans_action);
	g_array_unref(ids);
	ids = gt_macro_view_tab_ids(set, "General", "General");
	g_assert_cmpuint(ids->len, ==, 2);
	g_assert_cmpuint(g_array_index(ids, guint, 0), ==, id_sep);
	g_assert_cmpuint(g_array_index(ids, guint, 1), ==, id_p);
	g_array_unref(ids);
	gt_macro_set_free(set);
}

int main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/macro_view/onglets", test_onglets);
	g_test_add_func("/macro_view/sortes", test_sortes);
	g_test_add_func("/macro_view/macros-d-un-onglet", test_macros_d_un_onglet);
	return g_test_run();
}
