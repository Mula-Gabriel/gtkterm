/* Tests de macro_row.c : lignes de « Configure Macros » (GObject sans GTK), conversions depuis et vers le jeu, déplacements. */

#include <gio/gio.h>

#include "macro_row.h"
#include "macro_set.h"

static guint notifications;

static void compter(GObject *object, GParamSpec *pspec, gpointer data)
{
	notifications++;
}

static void test_proprietes(void)
{
	GtMacroRow *row = gt_macro_row_new(7, NULL, "<Control>F5", "A%d", NULL);
	GtMacroListRow *entry = gt_macro_list_row_new("Vitesses", NULL, "1");
	gchar *label = NULL;
	guint id = 0;

	g_assert_cmpuint(gt_macro_row_get_id(row), ==, 7);
	g_assert_cmpstr(gt_macro_row_get_label(row), ==, "");
	g_assert_cmpstr(gt_macro_row_get_tab(row), ==, "");
	g_assert_cmpstr(gt_macro_row_get_action(row), ==, "A%d");
	g_object_get(row, "id", &id, "label", &label, NULL);
	g_assert_cmpuint(id, ==, 7);
	g_assert_cmpstr(label, ==, "");
	g_free(label);
	notifications = 0;
	g_signal_connect(row, "notify::shortcut", G_CALLBACK(compter), NULL);
	gt_macro_row_set_shortcut(row, "<Control>F5");
	g_assert_cmpuint(notifications, ==, 0);	/* même valeur : pas de notification */
	gt_macro_row_set_shortcut(row, NULL);
	g_assert_cmpuint(notifications, ==, 1);
	g_assert_cmpstr(gt_macro_row_get_shortcut(row), ==, "");
	g_object_set(row, "label", "Vitesse", "tab", "T1", NULL);
	g_assert_cmpstr(gt_macro_row_get_label(row), ==, "Vitesse");
	g_assert_cmpstr(gt_macro_row_get_tab(row), ==, "T1");
	g_assert_cmpstr(gt_macro_list_row_get_list(entry), ==, "Vitesses");
	g_assert_cmpstr(gt_macro_list_row_get_display(entry), ==, "");
	g_object_set(entry, "display", "Vif", NULL);
	g_assert_cmpstr(gt_macro_list_row_get_display(entry), ==, "Vif");
	g_object_unref(entry);
	g_object_unref(row);
}

/* Libellés des lignes séparés par des virgules (g_free) */
static gchar *ordre(GListStore *store)
{
	GString *s = g_string_new(NULL);

	for(guint i = 0; i < g_list_model_get_n_items(G_LIST_MODEL(store)); i++)
	{
		GtMacroRow *row = g_list_model_get_item(G_LIST_MODEL(store), i);

		g_string_append_printf(s, i ? ",%s" : "%s", gt_macro_row_get_label(row));
		g_object_unref(row);
	}
	return g_string_free(s, FALSE);
}

static void assert_ordre(GListStore *store, const gchar *attendu)
{
	gchar *s = ordre(store);

	g_assert_cmpstr(s, ==, attendu);
	g_free(s);
}

static GListStore *cinq_lignes(void)
{
	GListStore *store = g_list_store_new(GT_TYPE_MACRO_ROW);

	for(guint i = 0; i < 5; i++)
	{
		gchar nom[2] = { (gchar)('0' + i), '\0' };
		GtMacroRow *row = gt_macro_row_new(0, nom, "", "", "");

		g_list_store_append(store, row);
		g_object_unref(row);
	}
	return store;
}

static void test_deplacements(void)
{
	GListStore *store = cinq_lignes();
	guint haut[] = { 1, 3 };
	guint bord[] = { 0, 2 };
	guint bas[] = { 3, 4 };
	guint fin[] = { 2, 3 };

	g_assert_true(gt_macro_rows_move(store, haut, 2, -1));
	assert_ordre(store, "1,0,3,2,4");
	g_assert_cmpuint(haut[0], ==, 0);
	g_assert_cmpuint(haut[1], ==, 2);
	g_assert_false(gt_macro_rows_move(store, bord, 2, -1));	/* une ligne en tête : rien ne bouge */
	assert_ordre(store, "1,0,3,2,4");
	g_assert_cmpuint(bord[0], ==, 0);
	g_assert_false(gt_macro_rows_move(store, bas, 2, +1));	/* une ligne en bas : rien ne bouge */
	assert_ordre(store, "1,0,3,2,4");
	g_assert_true(gt_macro_rows_move(store, fin, 2, +1));
	assert_ordre(store, "1,0,4,3,2");
	g_assert_cmpuint(fin[0], ==, 3);
	g_assert_cmpuint(fin[1], ==, 4);
	g_assert_false(gt_macro_rows_move(store, fin, 0, +1));
	g_object_unref(store);
}

static void test_suppression_et_insertion(void)
{
	GListStore *store = cinq_lignes();
	guint sel[] = { 0, 2, 4 };
	guint une[] = { 1 };

	gt_macro_rows_remove(store, sel, 3);
	assert_ordre(store, "1,3");
	g_assert_cmpuint(gt_macro_rows_insert_position(NULL, 0, 2), ==, 2);	/* sans sélection : à la fin */
	g_assert_cmpuint(gt_macro_rows_insert_position(une, 1, 2), ==, 2);
	g_assert_cmpuint(gt_macro_rows_insert_position(sel, 2, 5), ==, 3);	/* après la dernière sélectionnée */
	g_object_unref(store);
}

static void test_aller_retour_jeu(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint a = gt_macro_set_append(set, "A", "", "<A>", "");
	guint b = gt_macro_set_append(set, "B", "<Control>F5", "<B%d>", "T");
	guint c = gt_macro_set_append(set, "C", "", "<C%d>", "");
	guint pos[] = { 2 };
	GListStore *store;
	GtMacroEdit *edits;
	guint n = 0;

	g_assert_true(gt_macro_set_set_arg(set, c, 0, "7"));
	store = gt_macro_rows_from_set(set);
	assert_ordre(store, "A,B,C");
	g_assert_true(gt_macro_rows_move(store, pos, 1, -1));
	g_assert_true(gt_macro_rows_move(store, pos, 1, -1));
	edits = gt_macro_rows_to_edits(G_LIST_MODEL(store), &n);
	g_assert_cmpuint(n, ==, 3);
	g_assert_cmpuint(edits[0].id, ==, c);
	g_assert_cmpuint(edits[1].id, ==, a);
	g_assert_cmpuint(edits[2].id, ==, b);
	g_assert_cmpstr(edits[2].shortcut, ==, "<Control>F5");
	g_assert_cmpstr(edits[2].tab, ==, "T");
	gt_macro_set_apply_edits(set, edits, n, NULL);
	g_free(edits);
	g_assert_cmpuint(gt_macro_set_get(set, 0)->id, ==, c);
	g_assert_cmpstr(gt_macro_set_get(set, 0)->args[0], ==, "7");	/* l'argument suit l'identifiant (M16) */
	g_assert_cmpuint(gt_macro_set_get(set, 2)->id, ==, b);
	g_object_unref(store);
	store = gt_macro_rows_from_set(NULL);
	g_assert_cmpuint(g_list_model_get_n_items(G_LIST_MODEL(store)), ==, 0);
	g_object_unref(store);
	gt_macro_set_free(set);
}

static void test_listes(void)
{
	GtMacroLists *lists = gt_macro_lists_new();
	GtMacroLists *back;
	GListStore *store;
	GtMacroListRow *row;

	gt_macro_lists_add(lists, "Vitesses", "Lent", "0");
	gt_macro_lists_add(lists, "Vitesses", "Vif", "1");
	gt_macro_lists_add(lists, "Modes", "A", "a");
	store = gt_macro_list_rows_from_lists(lists);
	g_assert_cmpuint(g_list_model_get_n_items(G_LIST_MODEL(store)), ==, 3);
	row = g_list_model_get_item(G_LIST_MODEL(store), 1);
	g_assert_cmpstr(gt_macro_list_row_get_list(row), ==, "Vitesses");
	g_assert_cmpstr(gt_macro_list_row_get_display(row), ==, "Vif");
	g_assert_cmpstr(gt_macro_list_row_get_value(row), ==, "1");
	g_object_unref(row);
	back = gt_macro_list_rows_to_lists(G_LIST_MODEL(store));
	g_assert_cmpint(gt_macro_lists_count(back), ==, 2);
	g_assert_cmpint(gt_macro_lists_entry_count(back, gt_macro_lists_find(back, "Vitesses")), ==, 2);
	g_assert_cmpint(gt_macro_lists_entry_count(back, gt_macro_lists_find(back, "Modes")), ==, 1);
	gt_macro_lists_free(back);
	gt_macro_lists_free(lists);
	g_object_unref(store);
}

int main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/macro_row/proprietes", test_proprietes);
	g_test_add_func("/macro_row/deplacements", test_deplacements);
	g_test_add_func("/macro_row/suppression-et-insertion", test_suppression_et_insertion);
	g_test_add_func("/macro_row/aller-retour-jeu", test_aller_retour_jeu);
	g_test_add_func("/macro_row/listes", test_listes);
	return g_test_run();
}
