/* Tests de la saisie du dialogue Port sans widget (port_form.c) : valeurs affichées depuis le profil, contrôles par transport, plages, attente de
 * caractère, champs non édités conservés, profil de sortie inchangé en cas d'erreur. */
#include <glib.h>
#include <string.h>

#include "port_form.h"

static void
profil_defaut(GtProfile *p)
{
	gt_profile_init_defaults(p);
}

static void
test_depuis_profil(void)
{
	GtProfile p;
	GtPortForm f;

	profil_defaut(&p);
	p.baudrate = 0;
	gt_port_form_from_profile(&f, &p);
	g_assert_cmpint(f.transport, ==, TRANSPORT_SERIAL);
	g_assert_cmpstr(f.device, ==, "/dev/ttyS0");
	g_assert_cmpstr(f.baudrate, ==, "115200");	/* débit absent : défaut affiché */
	g_assert_cmpuint(p.baudrate, ==, 0);	/* le profil n'est pas modifié */
	g_assert_cmpstr(f.host, ==, GT_PORT_FORM_DEFAULT_HOST);
	g_assert_cmpstr(f.tcp_port, ==, GT_PORT_FORM_DEFAULT_TCP_PORT);
	g_assert_false(f.wait_char_enabled);
	g_assert_cmpstr(f.wait_char, ==, "");
	g_assert_cmpint(f.bits, ==, 8);
	g_assert_cmpint(f.stopbits, ==, 1);
	gt_port_form_clear(&f);

	gt_profile_set_string(&p.host, "example.org");
	gt_profile_set_string(&p.tcp_port, "8080");
	p.wait_char = '>';
	p.wait_delay_ms = 900;	/* hors plage dans le fichier : borné à l'affichage */
	gt_port_form_from_profile(&f, &p);
	g_assert_cmpstr(f.host, ==, "example.org");
	g_assert_cmpstr(f.tcp_port, ==, "8080");
	g_assert_true(f.wait_char_enabled);
	g_assert_cmpstr(f.wait_char, ==, ">");
	g_assert_cmpint(f.wait_delay_ms, ==, GT_PORT_FORM_DELAY_MAX);
	gt_port_form_clear(&f);

	p.wait_char = 0;	/* NUL écrit par l'ancien dialogue (case cochée, entrée vide) : case cochée, entrée vide, OK refusera */
	gt_port_form_from_profile(&f, &p);
	g_assert_true(f.wait_char_enabled);
	g_assert_cmpstr(f.wait_char, ==, "");
	gt_port_form_clear(&f);
	gt_profile_clear(&p);
}

static void
test_serie(void)
{
	GtProfile base, out = { 0 };
	GtPortForm f;
	GError *error = NULL;

	profil_defaut(&base);
	base.lock_port = FALSE;
	gt_profile_set_string(&base.host, "garde");
	gt_port_form_from_profile(&f, &base);
	gt_profile_set_string(&f.device, "  /dev/ttyUSB3 ");
	gt_profile_set_string(&f.baudrate, " 9600 ");
	f.parity = GT_PARITY_EVEN;
	f.bits = 7;
	f.stopbits = 2;
	f.flow = GT_FLOW_RS485;
	f.rs485_before_ms = 10;
	f.rs485_after_ms = 20;
	f.wait_delay_ms = 100;
	g_assert_true(gt_port_form_apply(&f, &base, &out, &error));
	g_assert_no_error(error);
	g_assert_cmpstr(out.port, ==, "/dev/ttyUSB3");
	g_assert_cmpuint(out.baudrate, ==, 9600);
	g_assert_cmpint(out.parity, ==, GT_PARITY_EVEN);
	g_assert_cmpint(out.bits, ==, 7);
	g_assert_cmpint(out.stopbits, ==, 2);
	g_assert_cmpint(out.flow, ==, GT_FLOW_RS485);
	g_assert_cmpint(out.rs485_before_ms, ==, 10);
	g_assert_cmpint(out.rs485_after_ms, ==, 20);
	g_assert_cmpint(out.wait_delay_ms, ==, 100);
	g_assert_cmpint(out.wait_char, ==, -1);
	g_assert_cmpstr(out.host, ==, "garde");	/* champs TCP non lus en série */
	g_assert_false(out.lock_port);	/* champs non édités : ceux de base */
	g_assert_cmpstr(out.font, ==, base.font);

	gt_profile_set_string(&f.baudrate, "12345");	/* débit non standard : accepté */
	g_assert_true(gt_port_form_apply(&f, &base, &out, &error));
	g_assert_cmpuint(out.baudrate, ==, 12345);
	gt_port_form_clear(&f);
	gt_profile_clear(&out);
	gt_profile_clear(&base);
}

static void
refuse(GtPortForm *f, const GtProfile *base, gint code)
{
	GtProfile out = { 0 };
	GError *error = NULL;

	out.baudrate = 7;	/* témoin : un échec ne touche pas au profil de sortie */
	g_assert_false(gt_port_form_apply(f, base, &out, &error));
	g_assert_error(error, GT_PORT_FORM_ERROR, code);
	g_assert_true(error->message != NULL && error->message[0] != '\0');
	g_assert_cmpuint(out.baudrate, ==, 7);
	g_error_free(error);
}

static void
test_refus_serie(void)
{
	static const gchar *const debits[] = { "", "abc", "0", "-1", "+9600", "96 00", "99999999999", NULL };
	GtProfile base;
	GtPortForm f;

	profil_defaut(&base);
	gt_port_form_from_profile(&f, &base);
	gt_profile_set_string(&f.device, "   ");
	refuse(&f, &base, GT_PORT_FORM_ERROR_DEVICE);
	gt_profile_set_string(&f.device, "/dev/ttyUSB0");
	for(gint i = 0; debits[i] != NULL; i++)
	{
		gt_profile_set_string(&f.baudrate, debits[i]);
		refuse(&f, &base, GT_PORT_FORM_ERROR_BAUDRATE);
	}
	gt_profile_set_string(&f.baudrate, "9600");
	f.bits = 9;
	refuse(&f, &base, GT_PORT_FORM_ERROR_SERIAL);
	f.bits = 8;
	f.stopbits = 3;
	refuse(&f, &base, GT_PORT_FORM_ERROR_SERIAL);
	f.stopbits = 1;
	f.rs485_before_ms = -1;
	refuse(&f, &base, GT_PORT_FORM_ERROR_DELAY);
	f.rs485_before_ms = 0;
	f.rs485_after_ms = GT_PORT_FORM_DELAY_MAX + 1;
	refuse(&f, &base, GT_PORT_FORM_ERROR_DELAY);
	gt_port_form_clear(&f);
	gt_profile_clear(&base);
}

static void
test_tcp(void)
{
	static const gchar *const ports[] = { "", "0", "65536", "80a", "-80", NULL };
	GtProfile base, out = { 0 };
	GtPortForm f;
	GError *error = NULL;

	profil_defaut(&base);
	gt_port_form_from_profile(&f, &base);
	f.transport = TRANSPORT_TCP_CLIENT;
	gt_profile_set_string(&f.device, "");	/* champ série vide : ignoré en TCP */
	gt_profile_set_string(&f.baudrate, "abc");
	gt_profile_set_string(&f.host, "  ");
	refuse(&f, &base, GT_PORT_FORM_ERROR_HOST);
	gt_profile_set_string(&f.host, " localhost ");
	for(gint i = 0; ports[i] != NULL; i++)
	{
		gt_profile_set_string(&f.tcp_port, ports[i]);
		refuse(&f, &base, GT_PORT_FORM_ERROR_TCP_PORT);
	}
	gt_profile_set_string(&f.tcp_port, "0");
	{
		GError *e0 = NULL;

		g_assert_false(gt_port_form_apply(&f, &base, &out, &e0));
		g_assert_nonnull(strstr(e0->message, "65535"));	/* le message donne la plage */
		g_error_free(e0);
	}
	gt_profile_set_string(&f.tcp_port, "080");
	g_assert_true(gt_port_form_apply(&f, &base, &out, &error));
	g_assert_cmpint(out.transport, ==, TRANSPORT_TCP_CLIENT);
	g_assert_cmpstr(out.host, ==, "localhost");
	g_assert_cmpstr(out.tcp_port, ==, "80");
	g_assert_cmpstr(out.port, ==, base.port);	/* champs série non lus en TCP */
	g_assert_cmpuint(out.baudrate, ==, base.baudrate);

	f.transport = TRANSPORT_TCP_SERVER;	/* serveur : hôte vide = toutes les interfaces */
	gt_profile_set_string(&f.host, "");
	gt_profile_set_string(&f.tcp_port, "65535");
	g_assert_true(gt_port_form_apply(&f, &base, &out, &error));
	g_assert_cmpstr(out.host, ==, "");
	g_assert_cmpstr(out.tcp_port, ==, "65535");
	gt_port_form_clear(&f);
	gt_profile_clear(&out);
	gt_profile_clear(&base);
}

static void
test_attente(void)
{
	static const gchar *const mauvais[] = { "", "ab", "é", NULL };
	GtProfile base, out = { 0 };
	GtPortForm f;
	GError *error = NULL;

	profil_defaut(&base);
	gt_port_form_from_profile(&f, &base);
	f.wait_delay_ms = 100;
	f.wait_char_enabled = TRUE;
	for(gint i = 0; mauvais[i] != NULL; i++)
	{
		gt_profile_set_string(&f.wait_char, mauvais[i]);
		refuse(&f, &base, GT_PORT_FORM_ERROR_WAIT_CHAR);
	}
	gt_profile_set_string(&f.wait_char, ">");
	g_assert_true(gt_port_form_apply(&f, &base, &out, &error));
	g_assert_cmpint(out.wait_char, ==, '>');
	g_assert_cmpint(out.wait_delay_ms, ==, 0);	/* attente de caractère : pas de délai (comme GTK 3) */

	f.wait_char_enabled = FALSE;	/* décochée : texte ignoré, délai gardé */
	gt_profile_set_string(&f.wait_char, "ab");
	g_assert_true(gt_port_form_apply(&f, &base, &out, &error));
	g_assert_cmpint(out.wait_char, ==, -1);
	g_assert_cmpint(out.wait_delay_ms, ==, 100);

	f.wait_delay_ms = GT_PORT_FORM_DELAY_MAX + 1;
	refuse(&f, &base, GT_PORT_FORM_ERROR_DELAY);
	f.transport = TRANSPORT_TCP_CLIENT;	/* envoi de fichier : contrôlé dans tous les transports */
	refuse(&f, &base, GT_PORT_FORM_ERROR_DELAY);
	gt_port_form_clear(&f);
	gt_profile_clear(&out);
	gt_profile_clear(&base);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/port-form/depuis-profil", test_depuis_profil);
	g_test_add_func("/port-form/serie", test_serie);
	g_test_add_func("/port-form/refus-serie", test_refus_serie);
	g_test_add_func("/port-form/tcp", test_tcp);
	g_test_add_func("/port-form/attente", test_attente);
	return g_test_run();
}
