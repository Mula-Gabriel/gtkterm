/* Tests des débits : table générée (baudrates.sh), correspondances débit <-> speed_t,
 * et réglage réel sur une paire pseudo-terminal (TCGETS2/BOTHER pour les débits non standard). */

#include <glib.h>
#include <limits.h>
#include <pty.h>
#include <termios.h>
#include <unistd.h>

#include "transport.h"

static void
test_table_triee(void)
{
	g_assert_cmpint(baudrate_count, >, 0);
	for(int i = 1; i < baudrate_count; i++)
		g_assert_cmpuint(baudrate_list[i - 1].baud, <, baudrate_list[i].baud);
	g_assert_cmpuint(baudrate_list[baudrate_count].baud, ==, 0);
}

static void
test_correspondances(void)
{
	for(int i = 0; i < baudrate_count; i++)
	{
		g_assert_cmpuint(find_standard_baudrate(baudrate_list[i].baud), ==, baudrate_list[i].speed);
		g_assert_cmpuint(speed_t_to_baud(baudrate_list[i].speed), ==, baudrate_list[i].baud);
	}
	g_assert_cmpuint(find_standard_baudrate(9600), ==, B9600);
	g_assert_cmpuint(find_standard_baudrate(115200), ==, B115200);
}

static void
test_hors_table(void)
{
	g_assert_cmpuint(find_standard_baudrate(0), ==, B0);
	g_assert_cmpuint(find_standard_baudrate(1), ==, B0);
	g_assert_cmpuint(find_standard_baudrate(250000), ==, B0);
	g_assert_cmpuint(find_standard_baudrate(UINT_MAX), ==, B0);
	g_assert_cmpuint(speed_t_to_baud(B0), ==, 0);
	/* speed_t « sain » (glibc >= 2.42 : B9600 == 9600) : toute valeur est un débit ; sinon, hors table = 0. */
	g_assert_cmpuint(speed_t_to_baud(0xdead), ==, speed_t_is_sane ? 0xdead : 0);
}

static gboolean
ouvrir_pty(int *maitre, int *esclave)
{
	return openpty(maitre, esclave, NULL, NULL, NULL) == 0;
}

static void
test_pty_debit_standard(void)
{
	int maitre, esclave;

	if(!ouvrir_pty(&maitre, &esclave))
	{
		g_test_skip("openpty indisponible");
		return;
	}
	/* set_port_baudrate passe toujours par termios2 + BOTHER (même pour un débit standard) :
	 * cfgetospeed() renverrait BOTHER. La valeur renvoyée est relue par TCGETS2. */
	g_assert_cmpuint(set_port_baudrate(115200, esclave), ==, 115200);
	close(esclave);
	close(maitre);
}

static void
test_pty_debit_non_standard(void)
{
	int maitre, esclave;

	if(!ouvrir_pty(&maitre, &esclave))
	{
		g_test_skip("openpty indisponible");
		return;
	}
	g_assert_cmpuint(set_port_baudrate(250000, esclave), ==, 250000);
	g_assert_cmpuint(set_port_baudrate(9600, esclave), ==, 9600);
	close(esclave);
	close(maitre);
}

static void
test_descripteur_invalide(void)
{
	g_assert_cmpuint(set_port_baudrate(9600, -1), ==, 0);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/baudrates/table-triee", test_table_triee);
	g_test_add_func("/baudrates/correspondances", test_correspondances);
	g_test_add_func("/baudrates/hors-table", test_hors_table);
	g_test_add_func("/baudrates/pty-debit-standard", test_pty_debit_standard);
	g_test_add_func("/baudrates/pty-debit-non-standard", test_pty_debit_non_standard);
	g_test_add_func("/baudrates/descripteur-invalide", test_descripteur_invalide);
	return g_test_run();
}
