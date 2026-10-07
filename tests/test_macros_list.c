/* Tests de macros_list.c : listes nommées (instance GtMacroLists) utilisées par %#NomListe. */

#include <glib.h>

#include "macros_list.h"

static void
test_vide(void)
{
	GtMacroLists *l = gt_macro_lists_new();

	g_assert_cmpint(gt_macro_lists_count(l), ==, 0);
	g_assert_cmpint(gt_macro_lists_find(l, "Mode"), ==, -1);
	g_assert_cmpint(gt_macro_lists_find(l, NULL), ==, -1);
	g_assert_cmpint(gt_macro_lists_find(NULL, "Mode"), ==, -1);
	g_assert_null(gt_macro_lists_name(l, 0));
	g_assert_cmpint(gt_macro_lists_entry_count(l, 0), ==, 0);
	g_assert_null(gt_macro_lists_entry_display(l, 0, 0));
	g_assert_null(gt_macro_lists_entry_value(l, 0, 0));
	g_assert_cmpint(gt_macro_lists_count(NULL), ==, 0);
	gt_macro_lists_free(l);
	gt_macro_lists_free(NULL);
}

static void
test_ajout_cree_liste(void)
{
	GtMacroLists *l = gt_macro_lists_new();

	gt_macro_lists_add(l, "Mode", "Run", "R");
	g_assert_cmpint(gt_macro_lists_count(l), ==, 1);
	g_assert_cmpint(gt_macro_lists_find(l, "Mode"), ==, 0);
	g_assert_cmpstr(gt_macro_lists_name(l, 0), ==, "Mode");
	g_assert_cmpint(gt_macro_lists_entry_count(l, 0), ==, 1);
	g_assert_cmpstr(gt_macro_lists_entry_display(l, 0, 0), ==, "Run");
	g_assert_cmpstr(gt_macro_lists_entry_value(l, 0, 0), ==, "R");
	gt_macro_lists_free(l);
}

static void
test_ajout_liste_existante(void)
{
	GtMacroLists *l = gt_macro_lists_new();

	gt_macro_lists_add(l, "Mode", "Run", "R");
	gt_macro_lists_add(l, "Mode", "Stop", "S");
	g_assert_cmpint(gt_macro_lists_count(l), ==, 1);
	g_assert_cmpint(gt_macro_lists_entry_count(l, 0), ==, 2);
	g_assert_cmpstr(gt_macro_lists_entry_display(l, 0, 1), ==, "Stop");
	g_assert_cmpstr(gt_macro_lists_entry_value(l, 0, 1), ==, "S");
	gt_macro_lists_free(l);
}

static void
test_valeur_par_defaut(void)
{
	GtMacroLists *l = gt_macro_lists_new();

	gt_macro_lists_add(l, "Mode", "Idle", NULL);
	g_assert_cmpstr(gt_macro_lists_entry_value(l, 0, 0), ==, "Idle");
	gt_macro_lists_free(l);
}

static void
test_arguments_nuls_ignores(void)
{
	GtMacroLists *l = gt_macro_lists_new();

	gt_macro_lists_add(l, NULL, "x", "y");
	gt_macro_lists_add(l, "Mode", NULL, "y");
	gt_macro_lists_add(NULL, "Mode", "x", "y");
	g_assert_cmpint(gt_macro_lists_count(l), ==, 0);
	gt_macro_lists_free(l);
}

static void
test_recherche_sensible_casse(void)
{
	GtMacroLists *l = gt_macro_lists_new();

	gt_macro_lists_add(l, "Mode", "Run", "R");
	g_assert_cmpint(gt_macro_lists_find(l, "mode"), ==, -1);
	g_assert_cmpint(gt_macro_lists_find(l, "Mode"), ==, 0);
	gt_macro_lists_free(l);
}

static void
test_ordre_des_listes(void)
{
	GtMacroLists *l = gt_macro_lists_new();

	gt_macro_lists_add(l, "B", "b", "1");
	gt_macro_lists_add(l, "A", "a", "2");
	gt_macro_lists_add(l, "B", "c", "3");
	g_assert_cmpint(gt_macro_lists_count(l), ==, 2);
	g_assert_cmpstr(gt_macro_lists_name(l, 0), ==, "B");
	g_assert_cmpstr(gt_macro_lists_name(l, 1), ==, "A");
	g_assert_cmpint(gt_macro_lists_entry_count(l, 0), ==, 2);
	gt_macro_lists_free(l);
}

static void
test_indices_hors_bornes(void)
{
	GtMacroLists *l = gt_macro_lists_new();

	gt_macro_lists_add(l, "Mode", "Run", "R");
	g_assert_null(gt_macro_lists_name(l, -1));
	g_assert_null(gt_macro_lists_name(l, 1));
	g_assert_cmpint(gt_macro_lists_entry_count(l, -1), ==, 0);
	g_assert_cmpint(gt_macro_lists_entry_count(l, 1), ==, 0);
	g_assert_null(gt_macro_lists_entry_display(l, 0, 1));
	g_assert_null(gt_macro_lists_entry_display(l, 0, -1));
	g_assert_null(gt_macro_lists_entry_value(l, 1, 0));
	gt_macro_lists_free(l);
}

static void
test_copie_independante(void)
{
	GtMacroLists *l = gt_macro_lists_new(), *c;

	gt_macro_lists_add(l, "Mode", "Run", "R");
	c = gt_macro_lists_copy(l);
	gt_macro_lists_add(l, "Mode", "Stop", "S");
	gt_macro_lists_add(c, "Autre", "x", "1");
	g_assert_cmpint(gt_macro_lists_entry_count(c, 0), ==, 1);
	g_assert_cmpint(gt_macro_lists_count(c), ==, 2);
	g_assert_cmpint(gt_macro_lists_count(l), ==, 1);
	gt_macro_lists_free(l);
	g_assert_cmpstr(gt_macro_lists_entry_value(c, 0, 0), ==, "R");
	gt_macro_lists_free(c);
	c = gt_macro_lists_copy(NULL);
	g_assert_nonnull(c);
	g_assert_cmpint(gt_macro_lists_count(c), ==, 0);
	gt_macro_lists_free(c);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/macros_list/vide", test_vide);
	g_test_add_func("/macros_list/ajout-cree-liste", test_ajout_cree_liste);
	g_test_add_func("/macros_list/ajout-liste-existante", test_ajout_liste_existante);
	g_test_add_func("/macros_list/valeur-par-defaut", test_valeur_par_defaut);
	g_test_add_func("/macros_list/arguments-nuls-ignores", test_arguments_nuls_ignores);
	g_test_add_func("/macros_list/recherche-sensible-casse", test_recherche_sensible_casse);
	g_test_add_func("/macros_list/ordre-des-listes", test_ordre_des_listes);
	g_test_add_func("/macros_list/indices-hors-bornes", test_indices_hors_bornes);
	g_test_add_func("/macros_list/copie-independante", test_copie_independante);
	return g_test_run();
}
