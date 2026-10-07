/* Tests unitaires de la correspondance d'un événement udev avec le port ouvert (device_match.c). */

#include <glib.h>
#include <sys/sysmacros.h>

#include "device_match.h"

static void
test_numero_egal(void)
{
	g_assert_true(device_match(makedev(166, 0), "/dev/ttyACM0", makedev(166, 0), "/dev/autre", NULL));
}

static void
test_numero_nul_ignore(void)
{
	g_assert_false(device_match(0, "/dev/ttyACM0", 0, "/dev/autre", NULL));
}

static void
test_chemin_egal(void)
{
	g_assert_true(device_match(0, "/dev/ttyUSB0", makedev(188, 0), "/dev/ttyUSB0", NULL));
}

static void
test_lien_egal(void)
{
	const gchar *const liens[] = { "/dev/serial/by-id/usb-x", "/dev/ttyM5", NULL };

	g_assert_true(device_match(makedev(166, 1), "/dev/ttyM5", makedev(166, 2), "/dev/ttyACM0", liens));
}

static void
test_aucune_correspondance(void)
{
	const gchar *const liens[] = { "/dev/serial/by-id/usb-x", NULL };

	g_assert_false(device_match(makedev(166, 1), "/dev/ttyM5", makedev(166, 2), "/dev/ttyACM0", liens));
}

static void
test_chemin_vide(void)
{
	g_assert_false(device_match(0, "", 0, "", NULL));
	g_assert_false(device_match(0, NULL, 0, "/dev/ttyACM0", NULL));
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/device_match/numero-egal", test_numero_egal);
	g_test_add_func("/device_match/numero-nul-ignore", test_numero_nul_ignore);
	g_test_add_func("/device_match/chemin-egal", test_chemin_egal);
	g_test_add_func("/device_match/lien-egal", test_lien_egal);
	g_test_add_func("/device_match/aucune-correspondance", test_aucune_correspondance);
	g_test_add_func("/device_match/chemin-vide", test_chemin_vide);
	return g_test_run();
}
