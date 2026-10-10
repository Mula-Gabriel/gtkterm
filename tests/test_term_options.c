/* Tests de term_options.c (cœur) : état unique des options du terminal (sous-projet 6a). */

#include <gio/gio.h>
#include <string.h>

#include "term_options.h"

typedef struct
{
	guint changed;
	guint notify_echo;
	guint notify_view_mode;
	guint notify_bytes;
	guint notify_total;
	guint changed_vu_par_notify;	/* valeur de changed lue dans le dernier gestionnaire notify */
	guint notify_vu_par_changed;	/* valeur de notify_total lue dans le dernier gestionnaire changed */
} Compteurs;

static void
sur_changed(GtTermOptions *o, gpointer data)
{
	Compteurs *c = data;

	c->changed++;
	c->notify_vu_par_changed = c->notify_total;
}

static void
sur_notify(GObject *obj, GParamSpec *pspec, gpointer data)
{
	Compteurs *c = data;

	c->notify_total++;
	c->changed_vu_par_notify = c->changed;
	if(strcmp(pspec->name, "echo") == 0)
		c->notify_echo++;
	else if(strcmp(pspec->name, "view-mode") == 0)
		c->notify_view_mode++;
	else if(strcmp(pspec->name, "hex-bytes-per-line") == 0)
		c->notify_bytes++;
}

static GtTermOptions *
nouvelles_options(Compteurs *c)
{
	GtTermOptions *o = gt_term_options_new();

	memset(c, 0, sizeof(*c));
	g_signal_connect(o, "changed", G_CALLBACK(sur_changed), c);
	g_signal_connect(o, "notify", G_CALLBACK(sur_notify), c);
	return o;
}

static void
test_defauts(void)
{
	GtTermOptions *o = gt_term_options_new();
	gboolean echo = TRUE, crlf = TRUE, ts = TRUE, esc = TRUE, index = TRUE;
	GtViewMode mode = GT_VIEW_HEX;
	guint octets = 0;

	g_object_get(o, "echo", &echo, "crlf-auto", &crlf, "timestamp", &ts, "esc-clear-screen", &esc, "hex-show-index", &index,
	             "view-mode", &mode, "hex-bytes-per-line", &octets, NULL);
	g_assert_false(echo);
	g_assert_false(crlf);
	g_assert_false(ts);
	g_assert_false(esc);
	g_assert_false(index);
	g_assert_cmpint(mode, ==, GT_VIEW_ASCII);
	g_assert_cmpuint(octets, ==, 16);
	g_assert_false(gt_term_options_get_esc_clear_screen(o));
	g_object_unref(o);
}

static void
test_notify_sur_changement(void)
{
	Compteurs c;
	GtTermOptions *o = nouvelles_options(&c);

	g_object_set(o, "echo", FALSE, NULL);
	g_assert_cmpuint(c.notify_echo, ==, 0);
	g_assert_cmpuint(c.changed, ==, 0);

	g_object_set(o, "echo", TRUE, NULL);
	g_assert_cmpuint(c.notify_echo, ==, 1);
	g_assert_cmpuint(c.changed, ==, 1);

	g_object_set(o, "echo", TRUE, NULL);
	g_assert_cmpuint(c.notify_echo, ==, 1);
	g_assert_cmpuint(c.changed, ==, 1);

	g_object_set(o, "view-mode", GT_VIEW_HEX, NULL);
	g_object_set(o, "view-mode", GT_VIEW_HEX, NULL);
	g_assert_cmpuint(c.notify_view_mode, ==, 1);
	g_assert_cmpuint(c.changed, ==, 2);

	g_object_set(o, "hex-bytes-per-line", 16, NULL);
	g_assert_cmpuint(c.notify_bytes, ==, 0);
	g_object_set(o, "hex-bytes-per-line", 24, NULL);
	g_assert_cmpuint(c.notify_bytes, ==, 1);
	g_assert_cmpuint(c.changed, ==, 3);
	g_assert_cmpuint(c.notify_total, ==, 3);
	g_object_unref(o);
}

/* « changed » après les « notify » : dans notify, changed n'a pas encore été émis ; dans changed, tous les notify ont eu lieu ;
 * un g_object_set de plusieurs propriétés et un apply_profile n'émettent qu'un changed */
static void
test_ordre_notify_changed(void)
{
	Compteurs c;
	GtTermOptions *o = nouvelles_options(&c);
	GtProfile p;

	g_object_set(o, "echo", TRUE, NULL);
	g_assert_cmpuint(c.changed_vu_par_notify, ==, 0);
	g_assert_cmpuint(c.notify_vu_par_changed, ==, 1);
	g_assert_cmpuint(c.changed, ==, 1);

	g_object_set(o, "crlf-auto", TRUE, "timestamp", TRUE, "view-mode", GT_VIEW_HEX, NULL);
	g_assert_cmpuint(c.notify_total, ==, 4);
	g_assert_cmpuint(c.changed_vu_par_notify, ==, 1);
	g_assert_cmpuint(c.notify_vu_par_changed, ==, 4);
	g_assert_cmpuint(c.changed, ==, 2);

	gt_profile_init_defaults(&p);
	p.echo = FALSE;
	p.crlf_auto = FALSE;
	p.timestamp = FALSE;
	p.esc_clear_screen = TRUE;
	gt_term_options_apply_profile(o, &p);
	g_assert_cmpuint(c.notify_total, ==, 8);
	g_assert_cmpuint(c.changed_vu_par_notify, ==, 2);
	g_assert_cmpuint(c.notify_vu_par_changed, ==, 8);
	g_assert_cmpuint(c.changed, ==, 3);

	/* Gel explicite : un seul changed au dégel, après les notify */
	g_object_freeze_notify(G_OBJECT(o));
	g_object_set(o, "echo", TRUE, NULL);
	g_object_set(o, "hex-show-index", TRUE, NULL);
	g_assert_cmpuint(c.changed, ==, 3);
	g_object_thaw_notify(G_OBJECT(o));
	g_assert_cmpuint(c.notify_total, ==, 10);
	g_assert_cmpuint(c.notify_vu_par_changed, ==, 10);
	g_assert_cmpuint(c.changed, ==, 4);
	gt_profile_clear(&p);
	g_object_unref(o);
}

static void
test_octets_refuses(void)
{
	static const guint refusees[] = { 0, 7, 12, 64 };
	Compteurs c;
	GtTermOptions *o = nouvelles_options(&c);
	guint octets = 0;

	for(guint i = 0; i < G_N_ELEMENTS(refusees); i++)
	{
		g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*hex-bytes-per-line*refused*");
		g_object_set(o, "hex-bytes-per-line", refusees[i], NULL);
		g_test_assert_expected_messages();
		g_object_get(o, "hex-bytes-per-line", &octets, NULL);
		g_assert_cmpuint(octets, ==, 16);
	}
	g_assert_cmpuint(c.notify_total, ==, 0);
	g_assert_cmpuint(c.changed, ==, 0);

	/* Les cinq valeurs admises passent */
	static const guint admises[] = { 8, 10, 24, 32, 16 };
	for(guint i = 0; i < G_N_ELEMENTS(admises); i++)
	{
		g_object_set(o, "hex-bytes-per-line", admises[i], NULL);
		g_object_get(o, "hex-bytes-per-line", &octets, NULL);
		g_assert_cmpuint(octets, ==, admises[i]);
	}
	g_assert_cmpuint(c.changed, ==, 5);
	g_object_unref(o);
}

static void
test_apply_profile_un_seul_changed(void)
{
	Compteurs c;
	GtTermOptions *o = nouvelles_options(&c);
	GtProfile p;

	gt_profile_init_defaults(&p);
	p.echo = TRUE;
	p.crlf_auto = TRUE;
	p.timestamp = TRUE;
	p.esc_clear_screen = TRUE;
	gt_term_options_apply_profile(o, &p);
	g_assert_cmpuint(c.changed, ==, 1);
	g_assert_cmpuint(c.notify_total, ==, 4);

	/* Rien ne change : ni notify ni changed */
	gt_term_options_apply_profile(o, &p);
	g_assert_cmpuint(c.changed, ==, 1);
	g_assert_cmpuint(c.notify_total, ==, 4);

	/* Un seul drapeau change : un notify, un changed */
	p.timestamp = FALSE;
	gt_term_options_apply_profile(o, &p);
	g_assert_cmpuint(c.changed, ==, 2);
	g_assert_cmpuint(c.notify_total, ==, 5);

	/* Le profil ne touche ni au mode de vue, ni aux octets par ligne, ni à l'index */
	g_object_set(o, "view-mode", GT_VIEW_HEX, "hex-bytes-per-line", 8, "hex-show-index", TRUE, NULL);
	c.changed = 0;
	gt_term_options_apply_profile(o, &p);
	g_assert_cmpuint(c.changed, ==, 0);
	{
		GtViewOptions v;

		gt_term_options_get_view(o, &v);
		g_assert_cmpint(v.mode, ==, GT_VIEW_HEX);
		g_assert_cmpuint(v.bytes_per_line, ==, 8);
		g_assert_true(v.show_index);
	}
	gt_profile_clear(&p);
	g_object_unref(o);
}

static void
test_aller_retour_profil(void)
{
	GtTermOptions *o = gt_term_options_new();
	GtProfile p, q;

	gt_profile_init_defaults(&p);
	p.echo = TRUE;
	p.crlf_auto = FALSE;
	p.timestamp = TRUE;
	p.esc_clear_screen = TRUE;
	gt_term_options_apply_profile(o, &p);

	gt_profile_init_defaults(&q);
	q.echo = FALSE;
	q.crlf_auto = TRUE;
	q.timestamp = FALSE;
	q.esc_clear_screen = FALSE;
	q.baudrate = 9600;
	gt_term_options_store_profile(o, &q);
	g_assert_true(q.echo);
	g_assert_false(q.crlf_auto);
	g_assert_true(q.timestamp);
	g_assert_true(q.esc_clear_screen);
	/* Les autres champs du profil sont intacts */
	g_assert_cmpuint(q.baudrate, ==, 9600);
	g_assert_cmpstr(q.port, ==, GT_PROFILE_DEFAULT_PORT);

	/* Une modification par propriété est relue par store_profile */
	g_object_set(o, "crlf-auto", TRUE, "echo", FALSE, NULL);
	gt_term_options_store_profile(o, &q);
	g_assert_false(q.echo);
	g_assert_true(q.crlf_auto);
	gt_profile_clear(&p);
	gt_profile_clear(&q);
	g_object_unref(o);
}

static void
test_get_view_et_rx(void)
{
	GtTermOptions *o = gt_term_options_new();
	GtViewOptions v;
	gboolean crlf = FALSE, ts = FALSE;

	g_object_set(o, "echo", TRUE, "crlf-auto", TRUE, "timestamp", FALSE, "esc-clear-screen", TRUE, "hex-show-index", TRUE,
	             "view-mode", GT_VIEW_HEX, "hex-bytes-per-line", 10, NULL);
	memset(&v, 0, sizeof(v));
	gt_term_options_get_view(o, &v);
	g_assert_cmpint(v.mode, ==, GT_VIEW_HEX);
	g_assert_true(v.echo);
	g_assert_true(v.crlf_auto);
	g_assert_false(v.timestamp);
	g_assert_cmpuint(v.bytes_per_line, ==, 10);
	g_assert_true(v.show_index);

	gt_term_options_get_rx(o, &crlf, &ts);
	g_assert_true(crlf);
	g_assert_false(ts);
	g_object_set(o, "timestamp", TRUE, NULL);
	gt_term_options_get_rx(o, NULL, &ts);
	g_assert_true(ts);
	gt_term_options_get_rx(o, &crlf, NULL);
	g_assert_true(crlf);
	g_assert_true(gt_term_options_get_esc_clear_screen(o));
	g_object_unref(o);
}

static void
test_enum_view_mode(void)
{
	GEnumClass *k = g_type_class_ref(GT_TYPE_VIEW_MODE);
	GEnumValue *ascii = g_enum_get_value_by_nick(k, "ascii");
	GEnumValue *hex = g_enum_get_value_by_nick(k, "hex");

	g_assert_true(G_TYPE_IS_ENUM(GT_TYPE_VIEW_MODE));
	g_assert_nonnull(ascii);
	g_assert_nonnull(hex);
	g_assert_cmpint(ascii->value, ==, GT_VIEW_ASCII);
	g_assert_cmpint(hex->value, ==, GT_VIEW_HEX);
	g_assert_cmpuint(k->n_values, ==, 2);
	g_type_class_unref(k);
}

/* Les menus de l'interface sont des GPropertyAction : l'enum devient un état chaîne (nick), le guint un état « u » */
static void
test_property_action(void)
{
	Compteurs c;
	GtTermOptions *o = nouvelles_options(&c);
	GPropertyAction *vue = g_property_action_new("view-mode", o, "view-mode");
	GPropertyAction *octets = g_property_action_new("hex-bytes", o, "hex-bytes-per-line");
	GPropertyAction *echo = g_property_action_new("local-echo", o, "echo");
	GVariant *etat;
	GtViewMode mode;
	guint n;

	g_assert_true(g_variant_type_equal(g_action_get_state_type(G_ACTION(vue)), G_VARIANT_TYPE_STRING));
	g_assert_true(g_variant_type_equal(g_action_get_parameter_type(G_ACTION(vue)), G_VARIANT_TYPE_STRING));
	etat = g_action_get_state(G_ACTION(vue));
	g_assert_cmpstr(g_variant_get_string(etat, NULL), ==, "ascii");
	g_variant_unref(etat);

	g_action_activate(G_ACTION(vue), g_variant_new_string("hex"));
	g_object_get(o, "view-mode", &mode, NULL);
	g_assert_cmpint(mode, ==, GT_VIEW_HEX);
	g_assert_cmpuint(c.changed, ==, 1);
	g_object_set(o, "view-mode", GT_VIEW_ASCII, NULL);
	etat = g_action_get_state(G_ACTION(vue));
	g_assert_cmpstr(g_variant_get_string(etat, NULL), ==, "ascii");
	g_variant_unref(etat);

	g_assert_true(g_variant_type_equal(g_action_get_state_type(G_ACTION(octets)), G_VARIANT_TYPE_UINT32));
	etat = g_action_get_state(G_ACTION(octets));
	g_assert_cmpuint(g_variant_get_uint32(etat), ==, 16);
	g_variant_unref(etat);
	g_action_change_state(G_ACTION(octets), g_variant_new_uint32(32));
	g_object_get(o, "hex-bytes-per-line", &n, NULL);
	g_assert_cmpuint(n, ==, 32);

	g_assert_true(g_variant_type_equal(g_action_get_state_type(G_ACTION(echo)), G_VARIANT_TYPE_BOOLEAN));
	g_action_activate(G_ACTION(echo), NULL);	/* booléen sans paramètre : bascule */
	etat = g_action_get_state(G_ACTION(echo));
	g_assert_true(g_variant_get_boolean(etat));
	g_variant_unref(etat);

	g_object_unref(vue);
	g_object_unref(octets);
	g_object_unref(echo);
	g_object_unref(o);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/term_options/defauts", test_defauts);
	g_test_add_func("/term_options/notify-sur-changement", test_notify_sur_changement);
	g_test_add_func("/term_options/ordre-notify-changed", test_ordre_notify_changed);
	g_test_add_func("/term_options/octets-refuses", test_octets_refuses);
	g_test_add_func("/term_options/apply-profile-un-seul-changed", test_apply_profile_un_seul_changed);
	g_test_add_func("/term_options/aller-retour-profil", test_aller_retour_profil);
	g_test_add_func("/term_options/get-view-et-rx", test_get_view_et_rx);
	g_test_add_func("/term_options/enum-view-mode", test_enum_view_mode);
	g_test_add_func("/term_options/property-action", test_property_action);
	return g_test_run();
}
