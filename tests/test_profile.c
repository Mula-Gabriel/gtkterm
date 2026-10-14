/* Tests unitaires du modèle de profil (profile.c) : défauts, copie, validation, conversion, noms des enums. */

#include <glib.h>
#include <string.h>

#include "profile.h"

static void
test_defauts(void)
{
	GtProfile p;

	gt_profile_init_defaults(&p);
	g_assert_cmpint(p.transport, ==, TRANSPORT_SERIAL);
	g_assert_cmpstr(p.port, ==, "/dev/ttyS0");
	g_assert_cmpuint(p.baudrate, ==, 115200);
	g_assert_cmpint(p.bits, ==, 8);
	g_assert_cmpint(p.stopbits, ==, 1);
	g_assert_cmpint(p.parity, ==, GT_PARITY_NONE);
	g_assert_cmpint(p.flow, ==, GT_FLOW_NONE);
	g_assert_cmpint(p.rs485_before_ms, ==, 30);
	g_assert_cmpint(p.rs485_after_ms, ==, 30);
	g_assert_true(p.lock_port);
	g_assert_cmpstr(p.host, ==, "");
	g_assert_cmpstr(p.tcp_port, ==, "");
	g_assert_cmpint(p.wait_delay_ms, ==, 0);
	g_assert_cmpint(p.wait_char, ==, -1);
	g_assert_false(p.echo || p.crlf_auto || p.auto_reconnect || p.esc_clear_screen || p.timestamp);
	g_assert_cmpstr(p.font, ==, "Monospace 12");
	g_assert_true(p.block_cursor);
	g_assert_cmpint(p.columns, ==, 80);
	g_assert_cmpint(p.rows, ==, 25);
	g_assert_cmpint(p.scrollback, ==, 10000);
	g_assert_false(p.visual_bell);
	g_assert_cmpfloat(p.foreground.red, ==, 0.0);
	g_assert_cmpfloat(p.foreground.alpha, ==, 1.0);
	g_assert_cmpfloat(p.background.red, ==, 1.0);
	g_assert_cmpfloat(p.background.alpha, ==, 1.0);
	g_assert_null(p.macros_file);
	gt_profile_clear(&p);
}

static void
test_copie_profonde(void)
{
	GtProfile a, b;

	gt_profile_init_defaults(&a);
	gt_profile_init_defaults(&b);
	gt_profile_set_string(&a.port, "/dev/ttyUSB3");
	gt_profile_set_string(&a.macros_file, "/tmp/m.ini");
	a.baudrate = 9600;
	gt_profile_copy(&b, &a);
	g_assert_cmpstr(b.port, ==, "/dev/ttyUSB3");
	g_assert_cmpstr(b.macros_file, ==, "/tmp/m.ini");
	g_assert_cmpuint(b.baudrate, ==, 9600);
	g_assert_true(b.port != a.port);
	gt_profile_set_string(&a.port, "/dev/autre");
	g_assert_cmpstr(b.port, ==, "/dev/ttyUSB3");
	gt_profile_copy(&b, &b);
	g_assert_cmpstr(b.port, ==, "/dev/ttyUSB3");
	gt_profile_clear(&a);
	gt_profile_clear(&b);
	g_assert_null(b.port);
}

static void
test_profil_a_zero_est_vide(void)
{
	GtProfile p;

	memset(&p, 0, sizeof(p));
	gt_profile_clear(&p);
	gt_profile_init_defaults(&p);
	gt_profile_clear(&p);
}

static void
test_validation_corrige(void)
{
	GtProfile p;
	GPtrArray *avert = g_ptr_array_new_with_free_func(g_free);

	gt_profile_init_defaults(&p);
	g_assert_true(gt_profile_validate(&p, avert));
	g_assert_cmpuint(avert->len, ==, 0);

	p.bits = 9;
	p.stopbits = 3;
	p.wait_delay_ms = 600;
	g_free(p.font);
	p.font = NULL;
	p.columns = 0;
	p.rows = -1;
	p.scrollback = 0;
	p.wait_char = 300;
	g_assert_false(gt_profile_validate(&p, avert));
	g_assert_cmpuint(avert->len, ==, 3);
	g_assert_cmpint(p.bits, ==, 8);
	g_assert_cmpint(p.stopbits, ==, 1);
	g_assert_cmpint(p.wait_delay_ms, ==, 0);
	g_assert_cmpstr(p.font, ==, "Monospace 12");
	g_assert_cmpint(p.columns, ==, 80);
	g_assert_cmpint(p.rows, ==, 25);
	g_assert_cmpint(p.scrollback, ==, 10000);
	g_assert_cmpint(p.wait_char, ==, -1);
	g_assert_nonnull(strstr(g_ptr_array_index(avert, 0), "stop-bits: 3"));
	g_assert_nonnull(strstr(g_ptr_array_index(avert, 1), "bits: 9"));
	gt_profile_clear(&p);
	g_ptr_array_unref(avert);
}

static void
test_validation_debit_non_standard(void)
{
	GtProfile p;
	GPtrArray *avert = g_ptr_array_new_with_free_func(g_free);

	gt_profile_init_defaults(&p);
	p.baudrate = 123457;
	g_assert_false(gt_profile_validate(&p, avert));
	g_assert_cmpuint(avert->len, ==, 1);
	g_assert_nonnull(strstr(g_ptr_array_index(avert, 0), "123457"));
	g_assert_cmpuint(p.baudrate, ==, 123457);	/* signalé, pas corrigé */
	gt_profile_clear(&p);
	g_ptr_array_unref(avert);
}

static void
test_conversion_transport(void)
{
	GtProfile p;
	TransportParams tp;

	gt_profile_init_defaults(&p);
	p.transport = TRANSPORT_TCP_CLIENT;
	gt_profile_set_string(&p.port, "/dev/ttyACM1");
	gt_profile_set_string(&p.host, "10.0.0.2");
	gt_profile_set_string(&p.tcp_port, "2000");
	p.baudrate = 57600;
	p.bits = 7;
	p.stopbits = 2;
	p.parity = GT_PARITY_EVEN;
	p.flow = GT_FLOW_RS485;
	p.rs485_before_ms = 5;
	p.rs485_after_ms = 6;
	p.lock_port = FALSE;
	gt_profile_to_transport_params(&p, &tp);
	g_assert_cmpint(tp.type, ==, TRANSPORT_TCP_CLIENT);
	g_assert_cmpstr(tp.port, ==, "/dev/ttyACM1");
	g_assert_true(tp.port != p.port);
	g_assert_cmpstr(tp.host, ==, "10.0.0.2");
	g_assert_cmpstr(tp.tcp_port, ==, "2000");
	g_assert_cmpuint(tp.baudrate, ==, 57600);
	g_assert_cmpint(tp.bits, ==, 7);
	g_assert_cmpint(tp.stopbits, ==, 2);
	g_assert_cmpint(tp.parity, ==, GT_PARITY_EVEN);
	g_assert_cmpint(tp.flow, ==, GT_FLOW_RS485);
	g_assert_cmpint(tp.rs485_before_ms, ==, 5);
	g_assert_cmpint(tp.rs485_after_ms, ==, 6);
	g_assert_false(tp.lock_port);
	transport_params_clear(&tp);
	gt_profile_clear(&p);
}

static void
test_noms_des_enums(void)
{
	GtParity par = GT_PARITY_NONE;
	GtFlow flux = GT_FLOW_NONE;
	TransportType tr = TRANSPORT_SERIAL;

	g_assert_cmpstr(gt_parity_to_string(GT_PARITY_ODD), ==, "odd");
	g_assert_cmpstr(gt_flow_to_string(GT_FLOW_XONXOFF), ==, "xon");
	g_assert_cmpstr(gt_flow_to_string(GT_FLOW_RTSCTS), ==, "rts");
	g_assert_cmpstr(gt_flow_to_string(GT_FLOW_RS485), ==, "rs485");
	g_assert_cmpstr(gt_transport_to_string(TRANSPORT_TCP_SERVER), ==, "tcp-server");
	g_assert_true(gt_parity_from_string("EVEN", &par));
	g_assert_cmpint(par, ==, GT_PARITY_EVEN);
	g_assert_true(gt_flow_from_string("Rs485", &flux));
	g_assert_cmpint(flux, ==, GT_FLOW_RS485);
	g_assert_true(gt_transport_from_string("tcp-client", &tr));
	g_assert_cmpint(tr, ==, TRANSPORT_TCP_CLIENT);
	g_assert_false(gt_parity_from_string("mark", &par));
	g_assert_cmpint(par, ==, GT_PARITY_EVEN);	/* inchangé */
	g_assert_false(gt_flow_from_string(NULL, &flux));
	g_assert_false(gt_transport_from_string("udp", &tr));
}

/* Spec §3.2-13 : nom rogné, 1 à 64 caractères (pas octets), tout caractère imprimable sauf « [ » et « ] » */
static void
test_nom_valide(void)
{
	GError *err = NULL;
	gchar *n = NULL;
	gchar *long64 = g_strnfill(64, 'a');
	gchar *entoure = g_strconcat("  ", long64, "  ", NULL);
	GString *e64 = g_string_new(NULL);

	g_assert_true(gt_profile_name_validate("atelier é", &n, &err));
	g_assert_no_error(err);
	g_assert_cmpstr(n, ==, "atelier é");
	g_free(n);
	g_assert_true(gt_profile_name_validate("  \tbanc 2 \n", &n, NULL));
	g_assert_cmpstr(n, ==, "banc 2");
	g_free(n);
	g_assert_true(gt_profile_name_validate(long64, NULL, NULL));		/* la limite est incluse */
	g_assert_true(gt_profile_name_validate(entoure, &n, NULL));		/* rogné avant de compter */
	g_assert_cmpstr(n, ==, long64);
	g_free(n);
	for(gint i = 0; i < 64; i++)
		g_string_append(e64, "é");					/* 64 caractères, 128 octets */
	g_assert_true(gt_profile_name_validate(e64->str, NULL, NULL));
	g_assert_true(gt_profile_name_validate("windows", NULL, NULL));
	g_assert_true(gt_profile_name_validate("Default", NULL, NULL));
	g_assert_true(gt_profile_name_validate("a=b;c#d", NULL, NULL));
	g_string_free(e64, TRUE);
	g_free(entoure);
	g_free(long64);
}

static void
verifier_refus(const gchar *nom, GtProfileError code)
{
	GError *err = NULL;
	gchar *n = (gchar *)"inchange";

	g_assert_false(gt_profile_name_validate(nom, &n, &err));
	g_assert_error(err, GT_PROFILE_ERROR, (gint)code);
	g_assert_cmpstr(err->message, !=, "");
	g_assert_null(n);
	g_clear_error(&err);
	g_assert_false(gt_profile_name_validate(nom, NULL, NULL));
}

static void
test_nom_refuse(void)
{
	gchar *long65 = g_strnfill(65, 'a');
	GString *e65 = g_string_new(NULL);

	for(gint i = 0; i < 65; i++)
		g_string_append(e65, "é");
	verifier_refus("", GT_PROFILE_ERROR_NAME_EMPTY);
	verifier_refus("   \t ", GT_PROFILE_ERROR_NAME_EMPTY);
	verifier_refus(NULL, GT_PROFILE_ERROR_NAME_EMPTY);
	verifier_refus(long65, GT_PROFILE_ERROR_NAME_TOO_LONG);
	verifier_refus(e65->str, GT_PROFILE_ERROR_NAME_TOO_LONG);
	verifier_refus("a[b", GT_PROFILE_ERROR_NAME_INVALID_CHAR);
	verifier_refus("a]b", GT_PROFILE_ERROR_NAME_INVALID_CHAR);
	verifier_refus("a\001b", GT_PROFILE_ERROR_NAME_INVALID_CHAR);
	verifier_refus("a\tb", GT_PROFILE_ERROR_NAME_INVALID_CHAR);
	verifier_refus("a\177", GT_PROFILE_ERROR_NAME_INVALID_CHAR);
	verifier_refus("\xff\xfe", GT_PROFILE_ERROR_NAME_INVALID_CHAR);	/* UTF-8 invalide */
	verifier_refus("Window", GT_PROFILE_ERROR_NAME_RESERVED);
	verifier_refus("COLORS", GT_PROFILE_ERROR_NAME_RESERVED);
	verifier_refus("layout", GT_PROFILE_ERROR_NAME_RESERVED);
	verifier_refus(" session ", GT_PROFILE_ERROR_NAME_RESERVED);
	g_string_free(e65, TRUE);
	g_free(long65);
}

static void
test_delais_activite(void)
{
	GtProfile p;
	GPtrArray *w = g_ptr_array_new_with_free_func(g_free);

	gt_profile_init_defaults(&p);
	g_assert_cmpint(p.idle_timeout_s, ==, 10);
	g_assert_cmpint(p.no_data_timeout_s, ==, 60);
	p.idle_timeout_s = 0;
	p.no_data_timeout_s = 3600;
	g_assert_true(gt_profile_validate(&p, w));
	g_assert_cmpint(p.idle_timeout_s, ==, 0);
	p.idle_timeout_s = -1;
	p.no_data_timeout_s = 3601;
	g_assert_false(gt_profile_validate(&p, w));
	g_assert_cmpuint(w->len, ==, 2);
	g_assert_cmpint(p.idle_timeout_s, ==, 10);
	g_assert_cmpint(p.no_data_timeout_s, ==, 60);
	gt_profile_clear(&p);
	g_ptr_array_unref(w);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/profile/defauts", test_defauts);
	g_test_add_func("/profile/copie-profonde", test_copie_profonde);
	g_test_add_func("/profile/profil-a-zero-est-vide", test_profil_a_zero_est_vide);
	g_test_add_func("/profile/validation-corrige", test_validation_corrige);
	g_test_add_func("/profile/validation-debit-non-standard", test_validation_debit_non_standard);
	g_test_add_func("/profile/conversion-transport", test_conversion_transport);
	g_test_add_func("/profile/noms-des-enums", test_noms_des_enums);
	g_test_add_func("/profile/delais-activite", test_delais_activite);
	g_test_add_func("/profile/nom-valide", test_nom_valide);
	g_test_add_func("/profile/nom-refuse", test_nom_refuse);
	return g_test_run();
}
