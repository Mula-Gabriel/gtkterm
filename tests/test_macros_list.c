/* Tests de macros_list.c : modèle des listes nommées utilisées par %#NomListe. */

#include <glib.h>

#include "macros_list.h"

static void
test_vide(void)
{
	macro_lists_free();
	g_assert_cmpint(macro_list_count(), ==, 0);
	g_assert_cmpint(macro_list_find("Mode"), ==, -1);
	g_assert_cmpint(macro_list_find(NULL), ==, -1);
	g_assert_null(macro_list_name(0));
	g_assert_cmpint(macro_list_entry_count(0), ==, 0);
	g_assert_null(macro_list_entry_display(0, 0));
	g_assert_null(macro_list_entry_value(0, 0));
}

static void
test_ajout_cree_liste(void)
{
	macro_lists_free();
	macro_list_add("Mode", "Run", "R");
	g_assert_cmpint(macro_list_count(), ==, 1);
	g_assert_cmpint(macro_list_find("Mode"), ==, 0);
	g_assert_cmpstr(macro_list_name(0), ==, "Mode");
	g_assert_cmpint(macro_list_entry_count(0), ==, 1);
	g_assert_cmpstr(macro_list_entry_display(0, 0), ==, "Run");
	g_assert_cmpstr(macro_list_entry_value(0, 0), ==, "R");
	macro_lists_free();
}

static void
test_ajout_liste_existante(void)
{
	macro_lists_free();
	macro_list_add("Mode", "Run", "R");
	macro_list_add("Mode", "Stop", "S");
	g_assert_cmpint(macro_list_count(), ==, 1);
	g_assert_cmpint(macro_list_entry_count(0), ==, 2);
	g_assert_cmpstr(macro_list_entry_display(0, 1), ==, "Stop");
	g_assert_cmpstr(macro_list_entry_value(0, 1), ==, "S");
	macro_lists_free();
}

static void
test_valeur_par_defaut(void)
{
	macro_lists_free();
	macro_list_add("Mode", "Idle", NULL);
	g_assert_cmpstr(macro_list_entry_value(0, 0), ==, "Idle");
	macro_lists_free();
}

static void
test_arguments_nuls_ignores(void)
{
	macro_lists_free();
	macro_list_add(NULL, "x", "y");
	macro_list_add("Mode", NULL, "y");
	g_assert_cmpint(macro_list_count(), ==, 0);
	macro_lists_free();
}

static void
test_recherche_sensible_casse(void)
{
	/* Comportement actuel : la recherche distingue la casse. */
	macro_lists_free();
	macro_list_add("Mode", "Run", "R");
	g_assert_cmpint(macro_list_find("mode"), ==, -1);
	macro_lists_free();
}

static void
test_ordre_des_listes(void)
{
	macro_lists_free();
	macro_list_add("A", "a1", NULL);
	macro_list_add("B", "b1", NULL);
	macro_list_add("A", "a2", NULL);
	g_assert_cmpint(macro_list_count(), ==, 2);
	g_assert_cmpstr(macro_list_name(0), ==, "A");
	g_assert_cmpstr(macro_list_name(1), ==, "B");
	g_assert_cmpint(macro_list_entry_count(0), ==, 2);
	g_assert_cmpint(macro_list_entry_count(1), ==, 1);
	macro_lists_free();
}

static void
test_suppression_entree(void)
{
	macro_lists_free();
	macro_list_add("L", "1", NULL);
	macro_list_add("L", "2", NULL);
	macro_list_add("L", "3", NULL);
	macro_list_remove_entry(0, 1);
	g_assert_cmpint(macro_list_entry_count(0), ==, 2);
	g_assert_cmpstr(macro_list_entry_display(0, 0), ==, "1");
	g_assert_cmpstr(macro_list_entry_display(0, 1), ==, "3");
	macro_list_remove_entry(0, 5);
	macro_list_remove_entry(0, -1);
	macro_list_remove_entry(3, 0);
	g_assert_cmpint(macro_list_entry_count(0), ==, 2);
	macro_lists_free();
}

static void
test_indices_hors_bornes(void)
{
	macro_lists_free();
	macro_list_add("L", "x", NULL);
	g_assert_null(macro_list_name(-1));
	g_assert_null(macro_list_name(1));
	g_assert_cmpint(macro_list_entry_count(-1), ==, 0);
	g_assert_null(macro_list_entry_display(0, -1));
	g_assert_null(macro_list_entry_display(0, 1));
	g_assert_null(macro_list_entry_value(1, 0));
	macro_lists_free();
}

static void
test_liberation_puis_reutilisation(void)
{
	macro_lists_free();
	macro_list_add("L", "x", NULL);
	macro_lists_free();
	g_assert_cmpint(macro_list_count(), ==, 0);
	g_assert_cmpint(macro_list_find("L"), ==, -1);
	macro_list_add("M", "y", NULL);
	g_assert_cmpint(macro_list_count(), ==, 1);
	g_assert_cmpint(macro_list_find("M"), ==, 0);
	macro_lists_free();
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
	g_test_add_func("/macros_list/suppression-entree", test_suppression_entree);
	g_test_add_func("/macros_list/indices-hors-bornes", test_indices_hors_bornes);
	g_test_add_func("/macros_list/liberation-puis-reutilisation", test_liberation_puis_reutilisation);
	return g_test_run();
}
