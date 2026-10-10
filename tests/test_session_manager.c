/* Tests de GtSessionManager (src/session_manager.c, sous-projet 6b) : ordre, active, retrait, déplacement. */
#include "session_manager.h"
#include "test_util.h"

static void on_notify(GObject *o, GParamSpec *p, gpointer user_data)
{
	(*(guint *)user_data)++;
}

static void test_ordre_et_active(void)
{
	GtSessionManager *m = gt_session_manager_new();
	GtSession *a = gt_session_new(NULL, NULL), *b = gt_session_new(NULL, NULL), *c = gt_session_new(NULL, NULL);
	guint n = 0;

	g_signal_connect(m, "notify::active", G_CALLBACK(on_notify), &n);
	g_assert_null(gt_session_manager_get_active(m));
	gt_session_manager_add(m, a, -1);
	g_assert_true(gt_session_manager_get_active(m) == a);	/* la première devient active */
	g_assert_cmpuint(n, ==, 1);
	gt_session_manager_add(m, c, -1);
	gt_session_manager_add(m, b, 1);	/* a, b, c */
	g_assert_cmpuint(gt_session_manager_get_n(m), ==, 3);
	g_assert_true(gt_session_manager_get_nth(m, 1) == b);
	g_assert_cmpint(gt_session_manager_index_of(m, c), ==, 2);
	g_assert_null(gt_session_manager_get_nth(m, 3));
	g_assert_true(gt_session_manager_get_active(m) == a);
	gt_session_manager_set_active(m, a);	/* même : pas de notification */
	g_assert_cmpuint(n, ==, 1);
	gt_session_manager_set_active(m, c);
	g_assert_cmpuint(n, ==, 2);
	g_assert_cmpuint(gt_session_get_id(b), >, gt_session_get_id(a));
	g_object_unref(a);
	g_object_unref(b);
	g_object_unref(c);
	g_object_unref(m);	/* le gestionnaire tenait ses références */
}

static void test_retrait(void)
{
	GtSessionManager *m = gt_session_manager_new();
	GtSession *a = gt_session_new(NULL, NULL), *b = gt_session_new(NULL, NULL), *c = gt_session_new(NULL, NULL);

	gt_session_manager_add(m, a, -1);
	gt_session_manager_add(m, b, -1);
	gt_session_manager_add(m, c, -1);
	gt_session_manager_set_active(m, b);
	gt_session_manager_remove(m, b);	/* voisine de droite */
	g_assert_true(gt_session_manager_get_active(m) == c);
	gt_session_manager_remove(m, c);	/* plus de droite : gauche */
	g_assert_true(gt_session_manager_get_active(m) == a);
	gt_session_manager_remove(m, c);	/* absente : rien */
	g_assert_cmpuint(gt_session_manager_get_n(m), ==, 1);
	gt_session_manager_remove(m, a);
	g_assert_null(gt_session_manager_get_active(m));
	g_object_unref(a);
	g_object_unref(b);
	g_object_unref(c);
	g_object_unref(m);
}

/* NULL n'est accepté comme active que sans session (en-tête) ; une session déjà présente n'est pas ajoutée deux fois */
static void test_active_null_et_doublon(void)
{
	GtSessionManager *m = gt_session_manager_new();
	GtSession *a = gt_session_new(NULL, NULL), *b = gt_session_new(NULL, NULL);
	guint n = 0;

	gt_session_manager_add(m, a, -1);
	gt_session_manager_add(m, b, -1);
	g_signal_connect(m, "notify::active", G_CALLBACK(on_notify), &n);
	gt_session_manager_set_active(m, NULL);
	g_assert_true(gt_session_manager_get_active(m) == a);
	g_object_set(m, "active", NULL, NULL);
	g_assert_true(gt_session_manager_get_active(m) == a);
	g_assert_cmpuint(n, ==, 0);
	g_test_expect_message(NULL, G_LOG_LEVEL_CRITICAL, "*index_of*");
	gt_session_manager_add(m, b, 0);
	g_test_assert_expected_messages();
	g_assert_cmpuint(gt_session_manager_get_n(m), ==, 2);
	g_assert_true(gt_session_manager_get_nth(m, 1) == b);
	g_object_unref(a);
	g_object_unref(b);
	g_object_unref(m);
}

static void test_deplacement(void)
{
	GtSessionManager *m = gt_session_manager_new();
	GtSession *a = gt_session_new(NULL, NULL), *b = gt_session_new(NULL, NULL), *c = gt_session_new(NULL, NULL);

	gt_session_manager_add(m, a, -1);
	gt_session_manager_add(m, b, -1);
	gt_session_manager_add(m, c, -1);
	gt_session_manager_move(m, a, 99);	/* b, c, a */
	g_assert_true(gt_session_manager_get_nth(m, 2) == a);
	g_assert_true(gt_session_manager_get_active(m) == a);
	gt_session_manager_move(m, c, 0);	/* c, b, a */
	g_assert_true(gt_session_manager_get_nth(m, 0) == c);
	g_object_unref(a);
	g_object_unref(b);
	g_object_unref(c);
	g_object_unref(m);
}

int main(int argc, char *argv[])
{
	gchar *racine = test_private_dirs();	/* HOME et XDG privés : jamais le vrai ~/.config */
	gint ret;

	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/sessions/ordre-et-active", test_ordre_et_active);
	g_test_add_func("/sessions/retrait", test_retrait);
	g_test_add_func("/sessions/deplacement", test_deplacement);
	g_test_add_func("/sessions/active-null-et-doublon", test_active_null_et_doublon);
	ret = g_test_run();
	test_rmdir_recursive(racine);
	g_free(racine);
	return ret;
}
