/* Tests de serial_termios_apply : aucun réglage du port n'est hérité de son état précédent (bogue C4). */

#include <glib.h>
#include <string.h>
#include <termios.h>

#include "serial_termios.h"

static void
test_efface_l_etat_herite(void)
{
	struct termios tio;

	memset(&tio, 0, sizeof(tio));
	tio.c_cflag = CS8 | PARENB | PARODD | CSTOPB | CRTSCTS;
	tio.c_iflag = IXON | IXOFF | ICRNL;
	serial_termios_apply(&tio, 7, 0, 1, 0);
	g_assert_cmpuint(tio.c_cflag & CSIZE, ==, CS7);
	g_assert_cmpuint(tio.c_cflag & (PARENB | PARODD | CSTOPB | CRTSCTS), ==, 0);
	g_assert_cmpuint(tio.c_cflag & (CREAD | CLOCAL), ==, CREAD | CLOCAL);
	g_assert_cmpuint(tio.c_iflag, ==, IGNPAR | IGNBRK);
	g_assert_cmpuint(tio.c_cc[VMIN], ==, 1);
	g_assert_cmpuint(tio.c_cc[VTIME], ==, 0);
}

static void
test_toutes_les_combinaisons(void)
{
	static const tcflag_t tailles[] = { CS5, CS6, CS7, CS8 };

	for(gint bits = 5; bits <= 8; bits++)
		for(gint parite = 0; parite <= 2; parite++)
			for(gint stop = 1; stop <= 2; stop++)
				for(gint flux = 0; flux <= 3; flux++)
				{
					struct termios tio;

					memset(&tio, 0, sizeof(tio));
					tio.c_cflag = CSIZE | PARENB | PARODD | CSTOPB | CRTSCTS | B9600;
					tio.c_iflag = IXON | IXOFF;
					serial_termios_apply(&tio, bits, parite, stop, flux);
					g_assert_cmpuint(tio.c_cflag & CSIZE, ==, tailles[bits - 5]);
					g_assert_cmpuint(!!(tio.c_cflag & PARENB), ==, parite != 0);
					g_assert_cmpuint(!!(tio.c_cflag & PARODD), ==, parite == 1);
					g_assert_cmpuint(!!(tio.c_cflag & CSTOPB), ==, stop == 2);
					g_assert_cmpuint(!!(tio.c_cflag & CRTSCTS), ==, flux == 2);
					g_assert_cmpuint(!!(tio.c_iflag & IXON), ==, flux == 1);
					g_assert_cmpuint(tio.c_cflag & CBAUD, ==, B9600);
				}
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/serial_termios/efface-l-etat-herite", test_efface_l_etat_herite);
	g_test_add_func("/serial_termios/toutes-les-combinaisons", test_toutes_les_combinaisons);
	return g_test_run();
}
