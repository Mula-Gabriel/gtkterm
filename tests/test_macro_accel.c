/* Tests de macro_accel.c : forme canonique des raccourcis, capture d'une touche, propriétaires. Lié à GTK 4 sans gtk_init ni affichage
 * (test_env : GDK_BACKEND invalide) : seules des fonctions sans affichage sont appelées (analyse et nom d'accélérateur, valeurs de touches). */

#include <gtk/gtk.h>

#include "macro_accel.h"

static void test_forme_canonique(void)
{
	gchar *a = gt_macro_accel_normalize("<Control><Shift>q");
	gchar *b = gt_macro_accel_normalize("<Shift><Primary>Q");
	gchar *c = gt_macro_accel_normalize("<Control>F8");
	gchar *d = gt_macro_accel_normalize("F8");

	g_assert_nonnull(a);
	g_assert_cmpstr(a, ==, b);
	g_assert_nonnull(c);
	g_assert_cmpstr(c, !=, d);
	g_assert_null(gt_macro_accel_normalize(""));
	g_assert_null(gt_macro_accel_normalize(NULL));
	g_assert_null(gt_macro_accel_normalize("<Control>pasunetouche"));
	g_free(a);
	g_free(b);
	g_free(c);
	g_free(d);
}

static void test_touche_capturee(void)
{
	gchar *accel = NULL;
	gchar *attendu = gt_macro_accel_normalize("<Control><Shift>q");
	gchar *ctrl_f8 = gt_macro_accel_normalize("<Control>F8");

	g_assert_cmpint(gt_macro_accel_from_key(GDK_KEY_Q, GDK_CONTROL_MASK | GDK_SHIFT_MASK, &accel), ==, GT_ACCEL_KEY_ACCEPTED);
	g_assert_cmpstr(accel, ==, attendu);
	g_clear_pointer(&accel, g_free);
	g_assert_cmpint(gt_macro_accel_from_key(GDK_KEY_Control_L, GDK_CONTROL_MASK, &accel), ==, GT_ACCEL_KEY_MODIFIER);
	g_assert_null(accel);
	g_assert_cmpint(gt_macro_accel_from_key(GDK_KEY_Escape, 0, &accel), ==, GT_ACCEL_KEY_CANCEL);
	g_assert_cmpint(gt_macro_accel_from_key(GDK_KEY_Escape, GDK_LOCK_MASK, &accel), ==, GT_ACCEL_KEY_CANCEL);	/* verrouillage ignoré */
	g_assert_cmpint(gt_macro_accel_from_key(GDK_KEY_b, 0, &accel), ==, GT_ACCEL_KEY_NEEDS_MODIFIER);
	g_assert_cmpint(gt_macro_accel_from_key(GDK_KEY_B, GDK_SHIFT_MASK, &accel), ==, GT_ACCEL_KEY_NEEDS_MODIFIER);
	g_assert_cmpint(gt_macro_accel_from_key(GDK_KEY_Return, 0, &accel), ==, GT_ACCEL_KEY_NEEDS_MODIFIER);
	g_assert_null(accel);
	g_assert_cmpint(gt_macro_accel_from_key(GDK_KEY_F9, 0, &accel), ==, GT_ACCEL_KEY_ACCEPTED);	/* touche de fonction seule */
	g_assert_cmpstr(accel, ==, "F9");
	g_clear_pointer(&accel, g_free);
	g_assert_cmpint(gt_macro_accel_from_key(GDK_KEY_F8, GDK_CONTROL_MASK, &accel), ==, GT_ACCEL_KEY_ACCEPTED);
	g_assert_cmpstr(accel, ==, ctrl_f8);
	g_free(accel);
	g_free(attendu);
	g_free(ctrl_f8);
}

static void test_proprietaires(void)
{
	GArray *owners = gt_accel_owners_new();
	const GtAccelOwner *o;

	gt_accel_owners_add(owners, "<Control><Shift>q", "quit", TRUE);
	gt_accel_owners_add(owners, "<Control>F5", "M1", FALSE);
	gt_accel_owners_add(owners, "", "vide", FALSE);	/* illisible : ignoré */
	g_assert_cmpuint(owners->len, ==, 2);
	o = gt_accel_owners_find(owners, "<Shift><Primary>Q");
	g_assert_nonnull(o);
	g_assert_cmpstr(o->owner, ==, "quit");
	g_assert_true(o->is_action);
	o = gt_accel_owners_find(owners, "<Primary>F5");
	g_assert_nonnull(o);
	g_assert_cmpstr(o->owner, ==, "M1");
	g_assert_false(o->is_action);
	g_assert_null(gt_accel_owners_find(owners, "<Control>F8"));
	g_assert_null(gt_accel_owners_find(owners, "F5"));
	g_assert_null(gt_accel_owners_find(owners, "illisible<"));
	g_array_unref(owners);
}

int main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/macro_accel/forme-canonique", test_forme_canonique);
	g_test_add_func("/macro_accel/touche-capturee", test_touche_capturee);
	g_test_add_func("/macro_accel/proprietaires", test_proprietaires);
	return g_test_run();
}
