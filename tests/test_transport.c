/* Tests d'intégration du vrai transport.c (bouchons d'interface) sur paire pty et TCP local.
 * ioctl est intercepté (-Wl,--wrap=ioctl) pour simuler les lignes modem RTS/DTR qu'une pty n'a pas. */

#include <glib.h>
#include <gtk/gtk.h>
#include <errno.h>
#include <pty.h>
#include <stdarg.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include "term_config.h"
#include "transport.h"
#include "files.h"
#include "config_file.h"
#include "stubs.h"

extern struct configuration_port config;

/* ---- Lignes modem simulées ------------------------------------------- */

int __real_ioctl(int fd, unsigned long requete, ...);

static int modem_fd = -1;
static int modem_etat = 0;
static int changements_rts = 0;

int
__wrap_ioctl(int fd, unsigned long requete, ...)
{
	va_list ap;
	void *arg;

	va_start(ap, requete);
	arg = va_arg(ap, void *);
	va_end(ap);

	if(fd == modem_fd && (requete == TIOCMGET || requete == TIOCMSET || requete == TIOCMBIS || requete == TIOCMBIC))
	{
		int ancien = modem_etat;

		if(requete == TIOCMGET)
			*(int *)arg = modem_etat;
		else if(requete == TIOCMSET)
			modem_etat = *(int *)arg;
		else if(requete == TIOCMBIS)
			modem_etat |= *(int *)arg;
		else
			modem_etat &= ~*(int *)arg;
		if((ancien ^ modem_etat) & TIOCM_RTS)
			changements_rts++;
		return 0;
	}
	return __real_ioctl(fd, requete, arg);
}

/* ---- Outils ------------------------------------------------------------ */

static gboolean
attendre(gboolean (*condition)(void), guint ms)
{
	gint64 fin = g_get_monotonic_time() + (gint64)ms * 1000;

	while(!condition() && g_get_monotonic_time() < fin)
	{
		g_main_context_iteration(NULL, FALSE);
		g_usleep(1000);
	}
	return condition();
}

static gboolean
add_input_appele(void)
{
	return stub_count("add_input") > 0;
}

static gboolean
connexion_annoncee(void)
{
	return stub_count("Set_status_message") > 0;
}

static void
config_serie(const gchar *port)
{
	memset(&config, 0, sizeof(config));
	config.transport_type = TRANSPORT_SERIAL;
	g_strlcpy(config.port, port, sizeof(config.port));
	config.vitesse = 115200;
	config.bits = 8;
	config.stops = 1;
	config.parite = 0;
	config.flux = 0;
	config.car = -1;
	config.disable_port_lock = TRUE;
}

typedef struct
{
	int maitre;
	int esclave;
} paire_pty_t;

static gboolean
ouvrir_pty(paire_pty_t *p)
{
	return openpty(&p->maitre, &p->esclave, NULL, NULL, NULL) == 0;
}

static void
fermer_pty(paire_pty_t *p)
{
	close(p->esclave);
	close(p->maitre);
}

/* ---- C4 : aucun réglage hérité ----------------------------------------- */

static void
test_termios_sans_heritage(void)
{
	paire_pty_t p;
	struct termios tio;

	if(!ouvrir_pty(&p))
	{
		g_test_skip("openpty indisponible");
		return;
	}
	stub_reset();
	g_assert_cmpint(tcgetattr(p.esclave, &tio), ==, 0);
	/* Une pty impose CS8 et sans parité : seuls CRTSCTS et CSTOPB y sont observables. */
	tio.c_cflag = (tio.c_cflag & ~CSIZE) | CS8 | CSTOPB | CRTSCTS;
	g_assert_cmpint(tcsetattr(p.esclave, TCSANOW, &tio), ==, 0);

	config_serie(ttyname(p.esclave));
	g_assert_true(transport_open());
	g_assert_cmpint(tcgetattr(transport_get_fd(), &tio), ==, 0);
	g_assert_cmpuint(tio.c_cflag & CSIZE, ==, CS8);
	g_assert_cmpuint(tio.c_cflag & (CSTOPB | CRTSCTS), ==, 0);
	transport_close();
	fermer_pty(&p);
}

/* ---- C3 : RTS stable en RS485 ------------------------------------------ */

static void
test_rs485_rts_stable(void)
{
	paire_pty_t p;

	if(!ouvrir_pty(&p))
	{
		g_test_skip("openpty indisponible");
		return;
	}
	stub_reset();
	config_serie(ttyname(p.esclave));
	config.flux = 3;
	g_assert_true(transport_open());
	modem_fd = transport_get_fd();
	modem_etat = TIOCM_DTR;
	changements_rts = 0;

	for(gint i = 0; i < 10; i++)
		transport_get_signals();
	g_assert_cmpint(changements_rts, ==, 0);

	g_assert_cmpint(transport_send("x", 1), ==, 1);
	g_assert_cmpint(changements_rts, ==, 2);
	g_assert_cmpint(modem_etat & TIOCM_RTS, ==, 0);

	modem_fd = -1;
	transport_close();
	fermer_pty(&p);
}

/* ---- M7 : caractère d'attente 0xFF ------------------------------------- */

static void
test_caractere_attendu_ff(void)
{
	paire_pty_t p;

	if(!ouvrir_pty(&p))
	{
		g_test_skip("openpty indisponible");
		return;
	}
	stub_reset();
	config_serie(ttyname(p.esclave));
	config.car = 0xFF;
	waiting_for_char = TRUE;
	g_assert_true(transport_open());
	g_assert_cmpint(write(p.maitre, "\xff", 1), ==, 1);
	g_assert_true(attendre(add_input_appele, 2000));
	waiting_for_char = FALSE;
	transport_close();
	fermer_pty(&p);
}

/* ---- C2 : pair TCP fermé ------------------------------------------------ */

static void
test_tcp_pair_ferme(void)
{
	struct sockaddr_in adresse;
	socklen_t taille = sizeof(adresse);
	int ecoute, pair, resultat = 0;

	stub_reset();
	ecoute = socket(AF_INET, SOCK_STREAM, 0);
	g_assert_cmpint(ecoute, >=, 0);
	memset(&adresse, 0, sizeof(adresse));
	adresse.sin_family = AF_INET;
	adresse.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	g_assert_cmpint(bind(ecoute, (struct sockaddr *)&adresse, sizeof(adresse)), ==, 0);
	g_assert_cmpint(listen(ecoute, 1), ==, 0);
	g_assert_cmpint(getsockname(ecoute, (struct sockaddr *)&adresse, &taille), ==, 0);

	memset(&config, 0, sizeof(config));
	config.transport_type = TRANSPORT_TCP_CLIENT;
	g_strlcpy(config.socket_host, "127.0.0.1", sizeof(config.socket_host));
	g_snprintf(config.socket_port, sizeof(config.socket_port), "%u", ntohs(adresse.sin_port));
	config.car = -1;
	g_assert_true(transport_open());
	pair = accept(ecoute, NULL, NULL);
	g_assert_cmpint(pair, >=, 0);
	g_assert_true(attendre(connexion_annoncee, 2000));

	close(pair);
	g_usleep(50000);
	/* Le premier envoi peut encore réussir (RST pas encore reçu) ; un suivant échoue en EPIPE au lieu de tuer le processus. */
	for(gint i = 0; i < 3; i++)
	{
		resultat = transport_send("abc", 3);
		g_usleep(20000);
	}
	g_assert_cmpint(resultat, <, 0);

	transport_close();
	close(ecoute);
}

static void
test_wait_char_ancien_codage(void)
{
	g_assert_cmpint(config_wait_char_from_file(0), ==, -1);
	g_assert_cmpint(config_wait_char_from_file(-1), ==, -1);
	g_assert_cmpint(config_wait_char_from_file(65), ==, 65);
	g_assert_cmpint(config_wait_char_from_file(255), ==, 255);
	g_assert_cmpint(config_wait_char_from_file(-23), ==, 233);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/transport/termios-sans-heritage", test_termios_sans_heritage);
	g_test_add_func("/transport/rs485-rts-stable", test_rs485_rts_stable);
	g_test_add_func("/transport/caractere-attendu-ff", test_caractere_attendu_ff);
	g_test_add_func("/transport/tcp-pair-ferme", test_tcp_pair_ferme);
	g_test_add_func("/transport/wait-char-ancien-codage", test_wait_char_ancien_codage);
	return g_test_run();
}
