/* Tests de profile_keyfile.c : profil ↔ groupe GKeyFile, texte identique à celui de 6b (config_store.c avant 6c). */

#include <glib.h>

#include "profile_keyfile.h"

/* Texte exact écrit par config_store.c de 6b pour un profil par défaut avec macros_file=/tmp/m.ini (référence figée), plus les deux clés
 * d'activité du port ajoutées après les délais RS-485 (indicateurs d'activité) */
static const gchar *const reference_6b =
	"[profile:default]\n"
	"transport=serial\n"
	"port=/dev/ttyS0\n"
	"speed=115200\n"
	"bits=8\n"
	"stopbits=1\n"
	"parity=none\n"
	"flow=none\n"
	"wait_delay=0\n"
	"wait_char=-1\n"
	"rs485_rts_time_before_tx=30\n"
	"rs485_rts_time_after_tx=30\n"
	"activity_idle_timeout=10\n"
	"activity_no_data_timeout=60\n"
	"socket_host=\n"
	"socket_port=\n"
	"echo=false\n"
	"crlfauto=false\n"
	"autoreconnect_enabled=false\n"
	"esc_clear_screen=false\n"
	"timestamp=false\n"
	"font=Monospace 12\n"
	"term_block_cursor=true\n"
	"term_columns=80\n"
	"term_rows=25\n"
	"term_scrollback=10000\n"
	"term_visual_bell=false\n"
	"term_foreground=0;0;0;1;\n"
	"term_background=1;1;1;1;\n"
	"macros_file=/tmp/m.ini\n";

static void test_identique_6b(void)
{
	GtProfile p;
	GKeyFile *kf = g_key_file_new();
	gchar *text;

	gt_profile_init_defaults(&p);
	gt_profile_set_string(&p.macros_file, "/tmp/m.ini");
	gt_profile_to_key_file(&p, kf, "profile:default");
	text = g_key_file_to_data(kf, NULL, NULL);
	g_assert_cmpstr(text, ==, reference_6b);
	g_free(text);
	g_key_file_free(kf);
	gt_profile_clear(&p);
}

static void test_aller_retour(void)
{
	GtProfile a, b;
	GKeyFile *kf = g_key_file_new();

	gt_profile_init_defaults(&a);
	a.transport = TRANSPORT_TCP_CLIENT;
	gt_profile_set_string(&a.host, "h\303\264te;=[x]");
	gt_profile_set_string(&a.tcp_port, "4000");
	a.baudrate = 9600;
	a.echo = TRUE;
	a.wait_char = 13;
	a.foreground = (GtColor){ 0.25, 0.5, 0.75, 1.0 };
	gt_profile_to_key_file(&a, kf, "tab:0");
	gt_profile_init_defaults(&b);
	gt_profile_from_key_file(kf, "tab:0", &b);
	g_assert_cmpint(b.transport, ==, TRANSPORT_TCP_CLIENT);
	g_assert_cmpstr(b.host, ==, a.host);
	g_assert_cmpstr(b.tcp_port, ==, "4000");
	g_assert_cmpuint(b.baudrate, ==, 9600);
	g_assert_true(b.echo);
	g_assert_cmpint(b.wait_char, ==, 13);
	g_assert_cmpfloat(b.foreground.green, ==, 0.5);
	g_assert_null(b.macros_file);
	/* macros_file NULL écrit dans un groupe qui en a déjà une : la clé est retirée */
	gt_profile_set_string(&a.macros_file, "/tmp/m.ini");
	gt_profile_to_key_file(&a, kf, "tab:0");
	g_assert_true(g_key_file_has_key(kf, "tab:0", "macros_file", NULL));
	gt_profile_set_string(&a.macros_file, NULL);
	gt_profile_to_key_file(&a, kf, "tab:0");
	g_assert_false(g_key_file_has_key(kf, "tab:0", "macros_file", NULL));
	gt_profile_clear(&a);
	gt_profile_clear(&b);
	g_key_file_free(kf);
}

static void test_cles_absentes_gardees(void)
{
	GtProfile p;
	GKeyFile *kf = g_key_file_new();

	g_key_file_set_string(kf, "g", "speed", "abc");	/* illisible : valeur gardée */
	g_key_file_set_string(kf, "g", "echo", "peut-etre");
	gt_profile_init_defaults(&p);
	p.baudrate = 57600;
	p.echo = TRUE;
	gt_profile_from_key_file(kf, "g", &p);
	g_assert_cmpuint(p.baudrate, ==, 57600);
	g_assert_true(p.echo);
	gt_profile_clear(&p);
	g_key_file_free(kf);
}

int main(int argc, char *argv[])
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/profile_keyfile/identique-6b", test_identique_6b);
	g_test_add_func("/profile_keyfile/aller-retour", test_aller_retour);
	g_test_add_func("/profile_keyfile/cles-absentes-gardees", test_cles_absentes_gardees);
	return g_test_run();
}
