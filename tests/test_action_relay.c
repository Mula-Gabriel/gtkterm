/* Tests du relais d'action (src/action_relay.c, sous-projet 6b) : délégation, reflet de l'état et de la sensibilité, changement de cible. */
#include <gio/gio.h>

#include "action_relay.h"
#include "term_options.h"

static void on_notify(GObject *obj, GParamSpec *pspec, gpointer user_data)
{
	(*(guint *)user_data)++;
}

static gulong watch(gpointer obj, const gchar *prop, guint *counter)
{
	gchar *signal = g_strconcat("notify::", prop, NULL);
	gulong id = g_signal_connect(obj, signal, G_CALLBACK(on_notify), counter);

	g_free(signal);
	return id;
}

static GSimpleActionGroup *group_with_bool(const gchar *name, gboolean value)
{
	GSimpleActionGroup *g = g_simple_action_group_new();
	GSimpleAction *a = g_simple_action_new_stateful(name, NULL, g_variant_new_boolean(value));

	g_action_map_add_action(G_ACTION_MAP(g), G_ACTION(a));
	g_object_unref(a);
	return g;
}

static gboolean state_bool(GAction *a)
{
	GVariant *v = g_action_get_state(a);
	gboolean b = g_variant_get_boolean(v);

	g_variant_unref(v);
	return b;
}

static void on_activate_count(GSimpleAction *a, GVariant *param, gpointer user_data)
{
	(*(guint *)user_data)++;
}

static void test_types(void)
{
	GSimpleActionGroup *g = group_with_bool("opt", TRUE);
	GtActionRelay *r = gt_action_relay_new(G_ACTION_GROUP(g), "opt");

	g_assert_nonnull(r);
	g_assert_cmpstr(g_action_get_name(G_ACTION(r)), ==, "opt");
	g_assert_null(g_action_get_parameter_type(G_ACTION(r)));
	g_assert_true(g_variant_type_equal(g_action_get_state_type(G_ACTION(r)), G_VARIANT_TYPE_BOOLEAN));
	g_assert_true(g_action_get_enabled(G_ACTION(r)));
	g_assert_true(state_bool(G_ACTION(r)));
	g_assert_true(gt_action_relay_get_target(r) == G_ACTION_GROUP(g));
	g_assert_null(gt_action_relay_new(G_ACTION_GROUP(g), "absente"));
	g_object_unref(r);
	g_object_unref(g);
}

static void test_activate(void)
{
	GSimpleActionGroup *g = g_simple_action_group_new();
	GSimpleAction *a = g_simple_action_new("go", NULL);
	guint n = 0;
	GtActionRelay *r;

	g_signal_connect(a, "activate", G_CALLBACK(on_activate_count), &n);
	g_action_map_add_action(G_ACTION_MAP(g), G_ACTION(a));
	r = gt_action_relay_new(G_ACTION_GROUP(g), "go");
	g_assert_null(g_action_get_state_type(G_ACTION(r)));
	g_assert_null(g_action_get_state(G_ACTION(r)));
	g_action_activate(G_ACTION(r), NULL);
	g_assert_cmpuint(n, ==, 1);
	/* Action cible désactivée : le relais l'est aussi et n'active rien */
	g_simple_action_set_enabled(a, FALSE);
	g_assert_false(g_action_get_enabled(G_ACTION(r)));
	g_action_activate(G_ACTION(r), NULL);
	g_assert_cmpuint(n, ==, 1);
	g_simple_action_set_enabled(a, TRUE);
	g_assert_true(g_action_get_enabled(G_ACTION(r)));
	g_object_unref(r);
	g_object_unref(a);
	g_object_unref(g);
}

static void test_change_state(void)
{
	GSimpleActionGroup *g = group_with_bool("opt", TRUE);
	GtActionRelay *r = gt_action_relay_new(G_ACTION_GROUP(g), "opt");
	guint n = 0;

	watch(r, "state", &n);
	g_action_change_state(G_ACTION(r), g_variant_new_boolean(FALSE));
	g_assert_false(state_bool(G_ACTION(g_action_map_lookup_action(G_ACTION_MAP(g), "opt"))));
	g_assert_false(state_bool(G_ACTION(r)));
	g_assert_cmpuint(n, ==, 1);
	/* La cible change seule : le relais suit */
	g_simple_action_set_state(G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(g), "opt")), g_variant_new_boolean(TRUE));
	g_assert_true(state_bool(G_ACTION(r)));
	g_assert_cmpuint(n, ==, 2);
	/* Même valeur : pas de notification */
	g_simple_action_set_state(G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(g), "opt")), g_variant_new_boolean(TRUE));
	g_assert_cmpuint(n, ==, 2);
	g_object_unref(r);
	g_object_unref(g);
}

static void test_retarget(void)
{
	GSimpleActionGroup *a = group_with_bool("opt", TRUE), *b = group_with_bool("opt", FALSE), *c = group_with_bool("opt", FALSE);
	GtActionRelay *r = gt_action_relay_new(G_ACTION_GROUP(a), "opt");
	guint n_state = 0, n_enabled = 0;

	watch(r, "state", &n_state);
	watch(r, "enabled", &n_enabled);
	gt_action_relay_set_target(r, G_ACTION_GROUP(b));
	g_assert_false(state_bool(G_ACTION(r)));
	g_assert_cmpuint(n_state, ==, 1);
	gt_action_relay_set_target(r, G_ACTION_GROUP(b));	/* même cible : rien */
	gt_action_relay_set_target(r, G_ACTION_GROUP(c));	/* même état : rien */
	g_assert_cmpuint(n_state, ==, 1);
	g_assert_cmpuint(n_enabled, ==, 0);
	/* L'ancienne cible ne fait plus bouger le relais */
	g_simple_action_set_state(G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(a), "opt")), g_variant_new_boolean(FALSE));
	g_simple_action_set_state(G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(b), "opt")), g_variant_new_boolean(TRUE));
	g_assert_false(state_bool(G_ACTION(r)));
	g_assert_cmpuint(n_state, ==, 1);
	/* Sans cible : désactivé, dernier état gardé */
	gt_action_relay_set_target(r, NULL);
	g_assert_false(g_action_get_enabled(G_ACTION(r)));
	g_assert_cmpuint(n_enabled, ==, 1);
	g_assert_false(state_bool(G_ACTION(r)));
	g_object_unref(r);
	g_object_unref(a);
	g_object_unref(b);
	g_object_unref(c);
}

static void test_missing_then_added(void)
{
	GSimpleActionGroup *model = group_with_bool("opt", TRUE), *empty = g_simple_action_group_new();
	GtActionRelay *r = gt_action_relay_new(G_ACTION_GROUP(model), "opt");
	GSimpleAction *later;

	gt_action_relay_set_target(r, G_ACTION_GROUP(empty));
	g_assert_false(g_action_get_enabled(G_ACTION(r)));
	g_assert_true(state_bool(G_ACTION(r)));	/* dernier état gardé */
	later = g_simple_action_new_stateful("opt", NULL, g_variant_new_boolean(FALSE));
	g_action_map_add_action(G_ACTION_MAP(empty), G_ACTION(later));
	g_assert_true(g_action_get_enabled(G_ACTION(r)));
	g_assert_false(state_bool(G_ACTION(r)));
	g_action_map_remove_action(G_ACTION_MAP(empty), "opt");
	g_assert_false(g_action_get_enabled(G_ACTION(r)));
	g_object_unref(later);
	g_object_unref(r);
	g_object_unref(model);
	g_object_unref(empty);
}

static void on_group_state(GActionGroup *g, const gchar *name, GVariant *value, gpointer user_data)
{
	(*(guint *)user_data)++;
}

/* Le relais rangé dans un groupe (l'application) : le groupe signale les changements d'état, comme l'exportateur D-Bus les voit */
static void test_in_map(void)
{
	GSimpleActionGroup *target = group_with_bool("opt", FALSE), *app = g_simple_action_group_new();
	GtActionRelay *r = gt_action_relay_new(G_ACTION_GROUP(target), "opt");
	guint n = 0;

	g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(r));
	g_signal_connect(app, "action-state-changed::opt", G_CALLBACK(on_group_state), &n);
	g_simple_action_set_state(G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(target), "opt")), g_variant_new_boolean(TRUE));
	g_assert_cmpuint(n, ==, 1);
	g_action_group_change_action_state(G_ACTION_GROUP(app), "opt", g_variant_new_boolean(FALSE));
	g_assert_false(state_bool(g_action_map_lookup_action(G_ACTION_MAP(target), "opt")));
	g_assert_cmpuint(n, ==, 2);
	g_object_unref(r);
	g_object_unref(app);
	g_object_unref(target);
}

/* GPropertyAction (actions d'options de la session) derrière le relais */
static void test_property_action(void)
{
	GtTermOptions *o = gt_term_options_new();
	GSimpleActionGroup *g = g_simple_action_group_new();
	GPropertyAction *pa = g_property_action_new("local-echo", o, "echo");
	GtActionRelay *r;
	gboolean echo = FALSE;

	g_action_map_add_action(G_ACTION_MAP(g), G_ACTION(pa));
	r = gt_action_relay_new(G_ACTION_GROUP(g), "local-echo");
	g_action_change_state(G_ACTION(r), g_variant_new_boolean(TRUE));
	g_object_get(o, "echo", &echo, NULL);
	g_assert_true(echo);
	g_object_set(o, "echo", FALSE, NULL);
	g_assert_false(state_bool(G_ACTION(r)));
	g_object_unref(r);
	g_object_unref(pa);
	g_object_unref(g);
	g_object_unref(o);
}

int main(int argc, char *argv[])
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/relais/types", test_types);
	g_test_add_func("/relais/activation", test_activate);
	g_test_add_func("/relais/changement-d-etat", test_change_state);
	g_test_add_func("/relais/changement-de-cible", test_retarget);
	g_test_add_func("/relais/action-absente-puis-ajoutee", test_missing_then_added);
	g_test_add_func("/relais/dans-un-groupe", test_in_map);
	g_test_add_func("/relais/action-de-propriete", test_property_action);
	return g_test_run();
}
