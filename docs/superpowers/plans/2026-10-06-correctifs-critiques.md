# Correctifs critiques — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** corriger les défauts critiques C1–C13 et les majeurs à correction locale de la revue, chacun prouvé par un test qui échoue avant le correctif ou par un essai manuel listé dans la spec.

**Architecture :**
- Un harnais de bouchons (`tests/stubs/`) permet de compiler le vrai `transport.c`, le vrai `script_engine.c` et le vrai `macros.c` dans des tests, sans fenêtre.
- Deux petites extractions vers `gtkterm_core` : `serial_termios.c` (construction termios) et `config_file.c` (lecture des profils, noms réservés, suppression de section, préparation du fichier).
- Les correctifs purement graphiques sont vérifiés par essais manuels.

**Tech Stack :** C (gnu11), GTK 3 (sans `gtk_init` dans les tests), GLib GTest/TAP, Lua 5.4, openpty, `-Wl,--wrap=ioctl`, meson.

**Spec :** `docs/superpowers/specs/2026-10-06-correctifs-critiques-design.md` (revue : `docs/superpowers/reviews/2026-10-06-revue-code.md`).

## Global Constraints

- Branche `fix/correctifs-critiques`, déjà créée ; elle contient la spec. Ne pas pousser ni fusionner dans ce plan. Le remote de publication est `Maison`, jamais `origin`.
- Messages de commit en français, terminés par `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Lignes de 200 caractères au plus.
- Respecter le style du fichier touché :
  - tabulations, Allman, `if(` : `transport.c`, `term_config.c`, `parsecfg.c`, `gtkterm.c`, `cmdline.c`, `files.c` ;
  - GNU 2 espaces : `macros.c`, `macros_format.c` ;
  - 4 espaces K&R : `script_engine.c`.
  Les nouveaux fichiers suivent `.clang-format` (tabulations, Allman, `if(`). Aucun reformatage.
- Codage du port inchangé : parité 0/1/2 (aucune/impaire/paire), flux 0/1/2/3 (aucun/Xon-Xoff/RTS-CTS/RS485).
- Les tests sont déclarés en TAP (`args : ['--tap'], protocol : 'tap'`), suite `unit`, `env : test_env`, derrière l'option meson `tests`.
- Un cas encore marqué (`test_conclude(..., "<ID>", ...)`) qui devient conforme fait échouer le test : chaque correctif d'un défaut marqué commence par remplacer l'identifiant par `NULL`. C'est le test rouge.
- Les tests tournent en locale C. Les messages de test sont donc en ASCII, puisque GLib remplace le non-ASCII par `?`. La liste des cas encore marqués s'obtient par `grep '# TODO' build/meson-logs/testlog.txt`.
- Ne jamais lancer l'application graphique soi-même : les essais manuels EM-n sont pour l'utilisateur (Task 18).
- Un nouveau défaut découvert en chemin va dans la revue (M43 et suivants) et n'est corrigé que s'il est critique ou s'il bloque un correctif de ce plan.

## Review Focus

1. **Fermer GTKTerm pendant qu'un script Lua attend le thread principal** (`run_on_main_sync`) : la fermeture ne doit pas se bloquer. Task 8, étape dédiée : vérifier qu'aucun code de fermeture ne joint le thread Lua depuis le thread principal (`grep g_thread_join`) et le noter dans le commit.
2. **`.gtktermrc` existants**, avec des groupes réservés, des sections ne différant que par la casse déjà présentes, ou un `wait_char` négatif (ancien codage signé) : ils doivent toujours se charger. Couvert aux Tasks 2 (conversion de `wait_char`, test `test_wait_char_ancien_codage` sur `config_wait_char_from_file`) et 6 (`test_config_file`, lecture avec groupes réservés).
3. **Macros existantes qui stockent un argument décimal pour un `%x`** : même sortie qu'avant. Couvert à la Task 5 (ligne `%x` [255] → `ff` conservée, nouvelle ligne `%X` [0x1f] → `1F`).
4. **Toute l'application sous `LANG=fr_FR.UTF-8`**, maintenant que la locale n'est plus forcée à « C » : nombres, couleurs, Lua, traductions. Couvert aux Tasks 3, 4 et 8 (tests sous `fr_FR`) et par EM-16 et EM-17.
5. **Profil chargé par la ligne de commande avec une casse différente** (`gtkterm -c Default`) : il doit être trouvé. Couvert à la Task 6 (`Load_configuration_from_file` passe à une comparaison insensible à la casse, et `test_config_file` teste `config_file_find_profile`).

---

### Task 1 : construction termios pure (`serial_termios.c`) — C4

**Files :**
- Create : `src/serial_termios.h`, `src/serial_termios.c`
- Create : `tests/test_serial_termios.c`
- Modify : `src/meson.build` (ajout à `core_sources`), `tests/meson.build` (dictionnaire `unit_tests`), `src/transport.c` (inclusion ; remplacement du bloc de construction termios dans `transport_open_serial`, actuellement `src/transport.c:289-344`)

**Interfaces :**
- Produces : `void serial_termios_apply(struct termios *tio, gint bits, gint parity, gint stopbits, gint flow);` dans `gtkterm_core`.
  - Efface `CSIZE`, `PARENB`, `PARODD`, `CSTOPB`, `CRTSCTS` de `c_cflag`.
  - Positionne la taille (5 à 8, 8 par défaut), la parité (1 impaire, 2 paire), `CSTOPB` si `stopbits == 2`, puis `CREAD | CLOCAL`.
  - `c_iflag = IGNPAR | IGNBRK`, avec en plus `IXON | IXOFF` si `flow == 1` ; `CRTSCTS` si `flow == 2`.
  - `c_oflag = 0`, `c_lflag = 0`, `VTIME = 0`, `VMIN = 1`.
  - Les bits de débit (`CBAUD`) ne sont pas touchés.

- [ ] **Step 1 : écrire le test**

Créer `tests/test_serial_termios.c` :

```c
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
```

Dans `tests/meson.build`, ajouter au dictionnaire `unit_tests` la ligne `'test_serial_termios' : [],`.

- [ ] **Step 2 : constater l'échec**

Run : `meson test -C build test_serial_termios 2>&1 | grep -E "serial_termios.h|FAIL|ERROR" | head -3`
Expected : erreur de compilation, `serial_termios.h: No such file or directory`.

- [ ] **Step 3 : créer le module**

`src/serial_termios.h` :

```c
/* Construction pure des réglages termios d'un port série (aucune entrée-sortie). */
#ifndef SERIAL_TERMIOS_H_
#define SERIAL_TERMIOS_H_

#include <glib.h>
#include <termios.h>

/* Applique à tio les bits de données (5 à 8), la parité (0 aucune, 1 impaire, 2 paire), les bits de stop (1 ou 2)
 * et le contrôle de flux (0 aucun, 1 Xon/Xoff, 2 RTS/CTS, 3 RS485 semi-duplex sans contrôle matériel).
 * Les champs concernés sont d'abord effacés : rien n'est hérité de l'état précédent du port. Le débit n'est pas touché. */
void serial_termios_apply(struct termios *tio, gint bits, gint parity, gint stopbits, gint flow);

#endif
```

`src/serial_termios.c` :

```c
/* Construction pure des réglages termios d'un port série. */

#include "serial_termios.h"

void
serial_termios_apply(struct termios *tio, gint bits, gint parity, gint stopbits, gint flow)
{
	tio->c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB);
#ifdef CRTSCTS
	tio->c_cflag &= ~CRTSCTS;
#endif
#ifdef CRTS_IFLOW
	tio->c_cflag &= ~CRTS_IFLOW;
#endif
#ifdef CCTS_OFLOW
	tio->c_cflag &= ~CCTS_OFLOW;
#endif

	switch(bits)
	{
	case 5:
		tio->c_cflag |= CS5;
		break;
	case 6:
		tio->c_cflag |= CS6;
		break;
	case 7:
		tio->c_cflag |= CS7;
		break;
	default:
		tio->c_cflag |= CS8;
		break;
	}

	if(parity == 1)
		tio->c_cflag |= PARENB | PARODD;
	else if(parity == 2)
		tio->c_cflag |= PARENB;

	if(stopbits == 2)
		tio->c_cflag |= CSTOPB;

	tio->c_cflag |= CREAD | CLOCAL;
	tio->c_iflag = IGNPAR | IGNBRK;

	if(flow == 1)
		tio->c_iflag |= IXON | IXOFF;
	else if(flow == 2)
	{
#ifdef CRTSCTS
		tio->c_cflag |= CRTSCTS;
#endif
#ifdef CRTS_IFLOW
		tio->c_cflag |= CRTS_IFLOW;
#endif
#ifdef CCTS_OFLOW
		tio->c_cflag |= CCTS_OFLOW;
#endif
	}

	tio->c_oflag = 0;
	tio->c_lflag = 0;
	tio->c_cc[VTIME] = 0;
	tio->c_cc[VMIN] = 1;
}
```

Dans `src/meson.build`, dans `core_sources`, après `'parsecfg.h',`, ajouter :

```meson
	'serial_termios.c',
	'serial_termios.h',
```

- [ ] **Step 4 : brancher `transport.c`**

Dans `src/transport.c`, après `#include "transport.h"`, ajouter `#include "serial_termios.h"`.

Dans `transport_open_serial`, remplacer tout le bloc compris entre `switch(config.bits)` (inclus) et `termios_p.c_cc[VMIN] = 1;` (inclus) par :

```c
	serial_termios_apply(&termios_p, config.bits, config.parite, config.stops, config.flux);
```

Les lignes `tcgetattr(fd, &termios_p);` et `memcpy(&termios_save, …)` (avant) et `tcsetattr(fd, TCSANOW, &termios_p);` (après) restent en place.

Run : `grep -n "CS7\|CRTSCTS\|IGNPAR" src/transport.c`
Expected : aucune sortie.

- [ ] **Step 5 : construire et tester**

Run : `ninja -C build 2>&1 | grep -E "error|transport.c.*warning" ; meson test -C build --suite unit 2>&1 | grep -E "serial_termios|^(Ok|Fail)"`
Expected : aucune erreur ; `test_serial_termios OK`, `Fail: 0`.

- [ ] **Step 6 : commit**

```bash
git add src/serial_termios.c src/serial_termios.h src/meson.build src/transport.c tests/test_serial_termios.c tests/meson.build
git commit -m "transport : construction termios pure (serial_termios_apply), plus d'état hérité du port (C4)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2 : harnais de bouchons et `test_transport` — C2, C3, C4 (intégration), M7

**Files :**
- Create : `tests/stubs/stubs.h`, `tests/stubs/stub_journal.c`, `tests/stubs/stub_ui.c`, `tests/stubs/stub_display.c`, `tests/test_transport.c`
- Modify :
  - `tests/meson.build` : variables des bouchons, dictionnaire `integration_tests`, `G_TEST_SRCDIR` ;
  - `src/transport.c` : `transport_send` (`MSG_NOSIGNAL`), `transport_get_signals` (C3), `transport_read_cb` (M7) ;
  - `src/gtkterm.c` : `main`, SIGPIPE ;
  - `src/term_config.h:58` (`car` en `gint`) ;
  - `src/term_config.c` : affichage du caractère dans `Config_Port_Fenetre` (`gchar car_str[2]`), lecture dans `Lis_Config` (`config.car = *gtk_entry_get_text…`), chargement dans `Load_configuration_from_file` (`(signed char)wait_char[i]`) ;
  - `src/config_file.c` / `.h` : création ici de `config_wait_char_from_file`, complété à la Task 6 ;
  - `src/cmdline.c` (`config.car = *optarg;`).

**Interfaces :**
- Produces (pour les Tasks 7 et 8, `tests/stubs/stubs.h`) :

```c
void         stub_reset(void);                              /* vide le journal et les octets enregistrés */
void         stub_record(const gchar *nom, const gchar *texte);
gint         stub_count(const gchar *nom);                  /* nombre d'appels enregistrés sous ce nom */
const gchar *stub_last(const gchar *nom);                   /* texte du dernier appel, NULL si aucun */
GByteArray  *stub_sent(void);                               /* octets passés à send_serial (stub_display.c) */
GThread     *stub_send_thread(void);                        /* thread du dernier send_serial */
GByteArray  *stub_received(void);                           /* octets passés à put_chars */
void         stub_inject_rx(const gchar *data, guint len);  /* appelle la fonction enregistrée par set_tap_func */
```

- `stub_ui.c` définit :
  - `struct configuration_port config` et `display_config_t term_conf` ;
  - `gboolean waiting_for_char` et `GtkAccelGroup *shortcuts` (NULL) ;
  - `show_message`, `Set_status_message`, `Set_window_title`, `Set_local_echo`, `Put_temp_message`, `rebuild_macro_buttons`, `add_input`.
  Chaque fonction enregistre son nom et son texte.
- `stub_display.c` définit `send_serial`, `put_chars`, `set_tap_func`, `unset_tap_func`, `clear_buffer`.
- Dans `tests/meson.build` :
  - variables `stub_journal`, `stub_ui`, `stub_display` (des `files(...)`) ;
  - dictionnaire `integration_tests` de la forme `nom → { 'sources' : […], 'deps' : […], 'link_args' : […] }`, que les Tasks 7 et 8 complètent.
- `src/term_config.h` : `gint car;  // caractère attendu (0-255), -1 : aucun`.

- [ ] **Step 1 : écrire les bouchons**

`tests/stubs/stubs.h` :

```c
/* Bouchons des tests d'intégration : remplacent les fonctions de l'interface (GTK/VTE) et enregistrent chaque appel. */
#ifndef STUBS_H_
#define STUBS_H_

#include <glib.h>

void         stub_reset(void);
void         stub_record(const gchar *nom, const gchar *texte);
gint         stub_count(const gchar *nom);
const gchar *stub_last(const gchar *nom);

GByteArray  *stub_sent(void);
GThread     *stub_send_thread(void);
GByteArray  *stub_received(void);
void         stub_inject_rx(const gchar *data, guint len);

#endif
```

`tests/stubs/stub_journal.c` :

```c
/* Journal des appels aux bouchons (sûr entre threads : le moteur Lua peut appeler un bouchon depuis son thread). */

#include <string.h>

#include "stubs.h"

static GMutex journal_mutex;
static GPtrArray *journal = NULL;	/* chaînes « nom\ttexte » */

void
stub_record(const gchar *nom, const gchar *texte)
{
	g_mutex_lock(&journal_mutex);
	if(journal == NULL)
		journal = g_ptr_array_new_with_free_func(g_free);
	g_ptr_array_add(journal, g_strdup_printf("%s\t%s", nom, texte ? texte : ""));
	g_mutex_unlock(&journal_mutex);
}

gint
stub_count(const gchar *nom)
{
	gint n = 0;
	gchar *prefixe = g_strdup_printf("%s\t", nom);

	g_mutex_lock(&journal_mutex);
	for(guint i = 0; journal != NULL && i < journal->len; i++)
		if(g_str_has_prefix(g_ptr_array_index(journal, i), prefixe))
			n++;
	g_mutex_unlock(&journal_mutex);
	g_free(prefixe);
	return n;
}

const gchar *
stub_last(const gchar *nom)
{
	const gchar *texte = NULL;
	gchar *prefixe = g_strdup_printf("%s\t", nom);

	g_mutex_lock(&journal_mutex);
	for(guint i = 0; journal != NULL && i < journal->len; i++)
		if(g_str_has_prefix(g_ptr_array_index(journal, i), prefixe))
			texte = (const gchar *)g_ptr_array_index(journal, i) + strlen(prefixe);
	g_mutex_unlock(&journal_mutex);
	g_free(prefixe);
	return texte;
}

void stub_display_reset(void);

void
stub_reset(void)
{
	g_mutex_lock(&journal_mutex);
	if(journal != NULL)
		g_ptr_array_set_size(journal, 0);
	g_mutex_unlock(&journal_mutex);
	stub_display_reset();
}
```

`tests/stubs/stub_ui.c` :

```c
/* Bouchons de l'interface : configuration globale et fonctions d'affichage, sans fenêtre. */

#include <gtk/gtk.h>

#include "interface.h"
#include "term_config.h"
#include "files.h"
#include "macro_panel.h"
#include "stubs.h"

struct configuration_port config;
display_config_t term_conf;
gboolean waiting_for_char = FALSE;
GtkAccelGroup *shortcuts = NULL;

void
show_message(gchar *message, gint type_msg)
{
	stub_record("show_message", message);
}

void
Set_status_message(gchar *msg)
{
	stub_record("Set_status_message", msg);
}

void
Set_window_title(gchar *msg)
{
	stub_record("Set_window_title", msg);
}

void
Set_local_echo(gboolean echo)
{
	stub_record("Set_local_echo", echo ? "1" : "0");
}

void
Put_temp_message(const gchar *msg, gint duree)
{
	stub_record("Put_temp_message", msg);
}

void
rebuild_macro_buttons(void)
{
	stub_record("rebuild_macro_buttons", NULL);
}

void
add_input(void)
{
	stub_record("add_input", NULL);
}
```

`tests/stubs/stub_display.c` :

```c
/* Bouchons de l'affichage et du tampon : enregistrent les octets envoyés et reçus. */

#include <glib.h>

#include "buffer.h"
#include "stubs.h"

static GMutex octets_mutex;
static GByteArray *envoyes = NULL;
static GByteArray *recus = NULL;
static GThread *thread_envoi = NULL;
static void (*ecoute)(const char *, unsigned int) = NULL;

void
stub_display_reset(void)
{
	g_mutex_lock(&octets_mutex);
	if(envoyes != NULL)
		g_byte_array_set_size(envoyes, 0);
	if(recus != NULL)
		g_byte_array_set_size(recus, 0);
	thread_envoi = NULL;
	g_mutex_unlock(&octets_mutex);
}

gint
send_serial(gchar *string, gint len)
{
	g_mutex_lock(&octets_mutex);
	if(envoyes == NULL)
		envoyes = g_byte_array_new();
	g_byte_array_append(envoyes, (const guint8 *)string, len);
	thread_envoi = g_thread_self();
	g_mutex_unlock(&octets_mutex);
	stub_record("send_serial", NULL);
	return len;
}

void
put_chars(const char *chars, unsigned int size, gboolean crlf_auto)
{
	g_mutex_lock(&octets_mutex);
	if(recus == NULL)
		recus = g_byte_array_new();
	g_byte_array_append(recus, (const guint8 *)chars, size);
	g_mutex_unlock(&octets_mutex);
}

void
set_tap_func(void (*func)(const char *, unsigned int))
{
	ecoute = func;
}

void
unset_tap_func(void)
{
	ecoute = NULL;
}

void
clear_buffer(void)
{
	stub_record("clear_buffer", NULL);
}

GByteArray *
stub_sent(void)
{
	if(envoyes == NULL)
		envoyes = g_byte_array_new();
	return envoyes;
}

GThread *
stub_send_thread(void)
{
	return thread_envoi;
}

GByteArray *
stub_received(void)
{
	if(recus == NULL)
		recus = g_byte_array_new();
	return recus;
}

void
stub_inject_rx(const gchar *data, guint len)
{
	if(ecoute != NULL)
		ecoute(data, len);
}
```

- [ ] **Step 2 : écrire `tests/test_transport.c`**

```c
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
#include "stubs.h"

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
	tio.c_cflag = (tio.c_cflag & ~CSIZE) | CS8 | PARENB | CRTSCTS;
	g_assert_cmpint(tcsetattr(p.esclave, TCSANOW, &tio), ==, 0);

	config_serie(ttyname(p.esclave));
	config.bits = 7;
	g_assert_true(transport_open());
	g_assert_cmpint(tcgetattr(transport_get_fd(), &tio), ==, 0);
	g_assert_cmpuint(tio.c_cflag & CSIZE, ==, CS7);
	g_assert_cmpuint(tio.c_cflag & (PARENB | CRTSCTS), ==, 0);
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

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/transport/termios-sans-heritage", test_termios_sans_heritage);
	g_test_add_func("/transport/rs485-rts-stable", test_rs485_rts_stable);
	g_test_add_func("/transport/caractere-attendu-ff", test_caractere_attendu_ff);
	g_test_add_func("/transport/tcp-pair-ferme", test_tcp_pair_ferme);
	return g_test_run();
}
```

- [ ] **Step 3 : déclarer les tests d'intégration**

Dans `tests/meson.build`, juste avant la section `# Syntaxe des exemples Lua`, ajouter :

```meson
# Tests d'intégration : vrais modules de src/ compilés avec des bouchons d'interface (tests/stubs/), sans fenêtre.
stub_journal = files('stubs/stub_journal.c')
stub_ui = files('stubs/stub_ui.c')
stub_display = files('stubs/stub_display.c')
stubs_inc = include_directories('stubs')

integration_tests = {
	'test_transport' : {
		'sources' : files('../src/transport.c') + stub_journal + stub_ui + stub_display,
		'deps' : [gtk_deps, util_dep],
		'link_args' : ['-Wl,--wrap=ioctl'],
	},
}

foreach name, t : integration_tests
	exe = executable(name, [name + '.c'] + t['sources'],
		include_directories : stubs_inc,
		dependencies : [gtkterm_core_dep] + t['deps'],
		link_args : t['link_args'])
	test(name, exe, args : ['--tap'], protocol : 'tap', suite : 'unit', env : test_env, timeout : 60)
endforeach
```

Puis, juste après la ligne `test_env.set('UBSAN_OPTIONS', …)`, ajouter :

```meson
test_env.set('G_TEST_SRCDIR', meson.current_source_dir())
test_env.set('G_TEST_BUILDDIR', meson.current_build_dir())
```

- [ ] **Step 4 : constater les échecs**

Run : `meson test -C build test_transport 2>&1 | grep -E "test_transport|error" | head -5; ./build/tests/test_transport --tap 2>&1 | grep -E "^(ok|not ok)|Bail|signal" | head`
Expected :
- `termios-sans-heritage` OK : la Task 1 a déjà corrigé C4, et le test sert ici de preuve d'intégration ;
- `rs485-rts-stable` en échec (`changements_rts` vaut 10, pas 0) ;
- `caractere-attendu-ff` en échec (`add_input` jamais appelé) ;
- le binaire meurt sur `tcp-pair-ferme` (SIGPIPE), et le test est déclaré en ERROR.

Si la compilation échoue sur `config.car = 0xFF`, seul un avertissement est attendu (`overflow`) ; une erreur doit être lue et corrigée dans le test, jamais dans `src/`.

- [ ] **Step 5 : corriger C2 (SIGPIPE)**

Dans `src/transport.c`, dans `transport_send`, remplacer :

```c
	bytes_written = write(fd, buf, len);
```

par :

```c
	/* En TCP, MSG_NOSIGNAL : un pair déjà fermé renvoie EPIPE au lieu de tuer GTKTerm par SIGPIPE (C2). */
	if(config.transport_type == TRANSPORT_SERIAL)
		bytes_written = write(fd, buf, len);
	else
		bytes_written = send(fd, buf, len, MSG_NOSIGNAL);
```

Dans `src/gtkterm.c`, ajouter `#include <signal.h>` après `#include <stdlib.h>`. Dans `main`, juste avant `config_file_init();`, ajouter :

```c
	/* Une écriture vers un pair TCP fermé (port principal ou port Lua) ne doit jamais tuer l'application (C2). */
	signal(SIGPIPE, SIG_IGN);
```

- [ ] **Step 6 : corriger C3 (RTS)**

Dans `src/transport.c`, dans `transport_get_signals`, supprimer le bloc :

```c
	if(config.flux == 3)
	{
		transport_set_signal(1);
	}

```

- [ ] **Step 7 : corriger M7 (caractère d'attente non signé)**

Dans `src/term_config.h`, remplacer `	gchar car;                   // caractere attendre` par :

```c
	gint car;                    // caractère attendu (0-255), -1 : aucun
```

Dans `src/transport.c`, `transport_read_cb` : `if(c[i] == config.car)` devient `if((guchar)c[i] == config.car)`.

Dans `src/cmdline.c` : `config.car = *optarg;` devient `config.car = (guchar)*optarg;`.

Dans `src/term_config.c` :
- `gchar car_str[2] = {config.car, '\0'};` devient `gchar car_str[2] = {(gchar)config.car, '\0'};` ;
- `config.car = *gtk_entry_get_text(GTK_ENTRY(Entry));` devient `config.car = (guchar)*gtk_entry_get_text(GTK_ENTRY(Entry));` ;
- le bloc de chargement suivant :

```c
				if(wait_char[i] != 0)
					config.car = (signed char)wait_char[i];
				else
					config.car = -1;
```

devient :

```c
				config.car = config_wait_char_from_file(wait_char[i]);
```

Ce dernier remplacement exige d'ajouter `#include "config_file.h"` en tête de `term_config.c` et de créer **dès cette tâche** `src/config_file.c` / `src/config_file.h` avec cette seule fonction. La Task 6 complète ensuite ces fichiers. Créer `src/config_file.h` :

```c
/* Fichier de configuration .gtktermrc : fonctions sans GTK (testées par tests/test_config_file.c). */
#ifndef CONFIG_FILE_H_
#define CONFIG_FILE_H_

#include <glib.h>

/* Caractère d'attente lu dans .gtktermrc : 1-255 tel quel ; les anciennes valeurs négatives (char signé, ex. -23 pour 0xE9)
 * sont ramenées à 0-255 ; 0 et -1 signifient « aucun » (-1). */
gint config_wait_char_from_file(gint valeur);

#endif
```

et `src/config_file.c` :

```c
/* Fichier de configuration .gtktermrc : fonctions sans GTK. */

#include "config_file.h"

gint
config_wait_char_from_file(gint valeur)
{
	if(valeur >= 1 && valeur <= 255)
		return valeur;
	if(valeur <= -2 && valeur >= -128)
		return (guchar)valeur;
	return -1;
}
```

Ajouter à `core_sources` (`src/meson.build`), après `'serial_termios.h',` : `'config_file.c',` et `'config_file.h',`.

Ajouter dans `tests/test_transport.c`, avant `main`, ce cas, qui couvre la Review Focus 2 :

```c
static void
test_wait_char_ancien_codage(void)
{
	g_assert_cmpint(config_wait_char_from_file(0), ==, -1);
	g_assert_cmpint(config_wait_char_from_file(-1), ==, -1);
	g_assert_cmpint(config_wait_char_from_file(65), ==, 65);
	g_assert_cmpint(config_wait_char_from_file(255), ==, 255);
	g_assert_cmpint(config_wait_char_from_file(-23), ==, 233);
}
```

avec `#include "config_file.h"` en tête, et `g_test_add_func("/transport/wait-char-ancien-codage", test_wait_char_ancien_codage);` dans `main`.

Note : un 0xFF enregistré avec l'ancien codage valait -1, ce qui signifiait « aucun ». Ce cas reste « aucun » : il est impossible de distinguer les deux sens d'une même valeur.

- [ ] **Step 8 : vérifier**

Run : `ninja -C build 2>&1 | grep -E "error|warning: .*(transport|term_config|cmdline|gtkterm)\.c" ; meson test -C build --suite unit 2>&1 | grep -E "test_transport|^(Ok|Fail)"`
Expected : aucune erreur ; `test_transport OK` (5 sous-tests) ; `Fail: 0`.

- [ ] **Step 9 : commit**

```bash
git add tests/stubs tests/test_transport.c tests/meson.build src/transport.c src/gtkterm.c src/term_config.h src/term_config.c src/cmdline.c src/config_file.c src/config_file.h src/meson.build
git commit -m "transport : SIGPIPE ignoré et MSG_NOSIGNAL (C2), RTS stable en RS485 (C3), caractère d'attente non signé (M7) ; harnais de bouchons et test_transport

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3 : parsecfg — C7 (indice de section) et M9 (locale)

**Files :**
- Modify : `src/parsecfg.c` (`cfgStoreValue`, `store_value`, `cfgDump`/`dump_ini`), `tests/test_parsecfg.c` (marquages C7 et M9, `setrlimit`), `tests/lsan.supp`, `src/term_config.c` (`config_fg_color`, `config_bg_color` : suppression des `cfgStoreValue` inutiles)

**Interfaces :**
- Consumes : `test_conclude` (`tests/test_util.h`).
- Produces : `cfgStoreValue` renvoie -1 (et affiche « Internal error ») pour un indice de section hors de `[0, nombre de sections[` en mode INI ; parsecfg ne modifie plus la locale du processus.

- [ ] **Step 1 : rendre les tests C7 et M9 exigeants**

Dans `tests/test_parsecfg.c` :
- remplacer `test_conclude(g_strcmp0(apres, "C.UTF-8") == 0, "M9", detail);` par `test_conclude(g_strcmp0(apres, "C.UTF-8") == 0, NULL, detail);` ;
- remplacer `test_conclude(g_test_trap_has_passed(), "C7", "cfgStoreValue apres la lecture d'un fichier vide ne doit pas planter");` par `test_conclude(g_test_trap_has_passed(), NULL, "cfgStoreValue apres la lecture d'un fichier vide ne doit pas planter");`.
- dans la branche `if(g_test_subprocess())` de `test_fichier_vide_puis_ecriture`, ajouter en première ligne :

```c
		/* Pas de fichier core si le sous-processus plante (remarque de la relecture du sous-projet 0). */
		struct rlimit sans_core = { 0, 0 };
		setrlimit(RLIMIT_CORE, &sans_core);
```

  et l'en-tête `#include <sys/resource.h>`. Après `cfgStoreValue(cfg, "port", "/dev/ttyS0", CFG_INI, 0);`, ajouter la vérification du retour : remplacer cette ligne par `g_assert_cmpint(cfgStoreValue(cfg, "port", "/dev/ttyS0", CFG_INI, 0), ==, -1);`.

Run : `meson test -C build test_parsecfg 2>&1 | grep -E "test_parsecfg"; grep -E "not ok" build/meson-logs/testlog.txt | grep parsecfg`
Expected : `test_parsecfg FAIL` ; `not ok … /parsecfg/locale-preservee` et `not ok … /parsecfg/fichier-vide-puis-ecriture`.

- [ ] **Step 2 : corriger C7**

Dans `src/parsecfg.c`, `cfgStoreValue`, avant `result = store_value(...)`, ajouter :

```c
	/* Une section qui n'existe pas (fichier vide, indice périmé) : erreur au lieu d'écrire dans un tableau absent (C7). */
	if (type == CFG_INI && (section < 0 || section >= parsecfg_maximum_section))
	{
		cfgFatalFunc(CFG_INTERNAL_ERROR, "unknown", 0, "");
		return (-1);
	}
```

Vérifier par `grep -n "parsecfg_maximum_section" src/parsecfg.c` que la variable est déclarée **avant** `cfgStoreValue`. Elle l'est aux lignes 70-71 ; sinon, déplacer la déclaration.

Dans `src/term_config.c`, dans `config_fg_color` et `config_bg_color`, supprimer les quatre groupes `string = g_strdup_printf (…); cfgStoreValue (cfg, …, CFG_INI, 0); g_free (string);` de chaque fonction, ainsi que la déclaration `gchar *string;` devenue inutile. Les couleurs restent enregistrées par `Copy_configuration` à l'enregistrement du profil.

- [ ] **Step 3 : corriger M9**

Dans `src/parsecfg.c` :
1. Renommer la fonction `static int store_value(...)` en `static int store_value_in_c_locale(...)`, en conservant son corps, et y supprimer la ligne `setlocale(LC_ALL,"C");` ainsi que le bloc de commentaires `//first step is to set locale …` qui la précède.
2. Ajouter la déclaration anticipée `static int store_value_in_c_locale(cfgStruct cfg[], const char *parameter, const char *value, cfgFileType type, int section);` à côté de celle de `store_value` (ligne 52), puis, juste avant la définition de `store_value_in_c_locale`, la nouvelle définition :

```c
/* Les nombres du fichier s'écrivent toujours avec un point : lecture sous une locale « C » propre au thread,
   sans toucher à la locale du processus (M9 : setlocale(LC_ALL,"C") cassait les traductions et toute l'interface). */
static int store_value(cfgStruct cfg[], const char *parameter, const char *value, cfgFileType type, int section)
{
	locale_t c_locale = newlocale(LC_ALL_MASK, "C", (locale_t)0);
	locale_t previous = uselocale(c_locale);
	int result = store_value_in_c_locale(cfg, parameter, value, type, section);

	uselocale(previous);
	freelocale(c_locale);
	return result;
}
```

3. Dans `dump_ini`, supprimer `setlocale(LC_ALL,"C");` et son bloc de commentaires. Dans `cfgDump`, remplacer :

```c
	case CFG_INI:
		retcode = dump_ini(fp, cfg, type, max_section);
		break;
```

par :

```c
	case CFG_INI:
		{
			/* Flottants écrits avec un point quelle que soit la langue, sans changer la locale du processus (M9). */
			locale_t c_locale = newlocale(LC_ALL_MASK, "C", (locale_t)0);
			locale_t previous = uselocale(c_locale);

			retcode = dump_ini(fp, cfg, type, max_section);
			uselocale(previous);
			freelocale(c_locale);
		}
		break;
```

Run : `grep -n "setlocale" src/parsecfg.c`
Expected : aucune sortie.

- [ ] **Step 4 : suppressions LeakSanitizer**

Remplacer le contenu de `tests/lsan.supp` par :

```
# Fuites connues de parsecfg.c (revue §5 : chaque cfgParse abandonne les tableaux précédents).
# parsecfg est remplacé par GKeyFile au sous-projet 3 : supprimer ce fichier à ce moment-là.
# Filtre sur le fichier source : ne masque rien hors de parsecfg.c.
leak:parsecfg.c
```

- [ ] **Step 5 : vérifier**

Run : `ninja -C build 2>&1 | grep -E "error|warning: .*parsecfg"; meson test -C build --suite unit 2>&1 | grep -E "test_parsecfg|^(Ok|Fail)"; grep -c "TODO bogue C7\|TODO bogue M9" build/meson-logs/testlog.txt`
Expected : aucune erreur ; `test_parsecfg OK` ; `Fail: 0` ; `0`.

- [ ] **Step 6 : commit**

```bash
git add src/parsecfg.c src/term_config.c tests/test_parsecfg.c tests/lsan.supp
git commit -m "parsecfg : indice de section vérifié (C7), locale C propre au thread au lieu de setlocale (M9)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4 : conséquences de M9 — flottants des macros et couleurs indépendants de la langue

**Files :**
- Modify : `src/macros_format.c` (`apply_spec`, `macro_type_value_valid`), `src/term_config.c` (`Copy_configuration`, les huit couleurs), `tests/test_macros_format.c`

**Interfaces :**
- Consumes : rien de nouveau.
- Produces : `macros_format.c` lit les flottants par `g_ascii_strtod` et les met en forme sous locale C propre au thread ; `Copy_configuration` écrit les couleurs par `g_ascii_formatd`.

- [ ] **Step 1 : écrire le test sous `fr_FR`**

Dans `tests/test_macros_format.c`, avant la section « Enregistrement », ajouter :

```c
/* M9 : la locale n'est plus forcée à « C » par parsecfg ; le point reste le séparateur décimal des macros. */
static void
test_flottants_sous_locale_fr(void)
{
	gchar *ancienne = g_strdup(setlocale(LC_ALL, NULL));
	const gchar *args[1] = { "3.14159" };
	gchar *texte;
	gboolean point_valide, virgule_valide;

	if(setlocale(LC_ALL, "fr_FR.UTF-8") == NULL)
	{
		g_free(ancienne);
		g_test_skip("locale fr_FR.UTF-8 indisponible");
		return;
	}
	texte = format_action_with_args("%.2f", args, 1);
	point_valide = macro_type_value_valid("f", "2.5");
	virgule_valide = macro_type_value_valid("f", "2,5");
	setlocale(LC_ALL, ancienne);
	g_free(ancienne);

	g_assert_cmpstr(texte, ==, "3.14");
	g_assert_true(point_valide);
	g_assert_false(virgule_valide);
	g_free(texte);
}
```

et dans `main`, après `g_test_add_func("/macros_format/types-flottants", …);` : `g_test_add_func("/macros_format/flottants-sous-locale-fr", test_flottants_sous_locale_fr);`.

Run : `meson test -C build test_macros_format 2>&1 | grep test_macros_format; grep "not ok" build/meson-logs/testlog.txt | grep locale-fr`
Expected : FAIL ; `not ok … flottants-sous-locale-fr`, avec `"3,00"` (ou `"3,14"`) au lieu de `"3.14"` dans le message.

- [ ] **Step 2 : corriger `macros_format.c`**

Ajouter `#include <locale.h>` après `#include <limits.h>`.

Juste avant `static gchar *apply_spec`, ajouter :

```c
/* Met en forme un flottant sous une locale « C » propre au thread : le point décimal est imposé quelle que soit la langue (M9). */
static gchar *
format_double_c_locale (const gchar *spec, gboolean long_double, gdouble value)
{
  locale_t c_locale = newlocale (LC_ALL_MASK, "C", (locale_t) 0);
  locale_t previous = uselocale (c_locale);
  gchar *out = long_double ? g_strdup_printf (spec, (long double) value) : g_strdup_printf (spec, value);

  uselocale (previous);
  freelocale (c_locale);
  return out;
}
```

Dans `apply_spec`, branche des flottants, remplacer :

```c
      if (fmt_type[0] == 'L')
        {
          gchar *lspec = spec_rebuild (spec, 'L');
          gchar *out = g_strdup_printf (lspec, strtold (arg_str, NULL));
          g_free (lspec);
          return out;
        }

      gchar *dspec = spec_rebuild (spec, '\0');
      gchar *out = g_strdup_printf (dspec, strtod (arg_str, NULL));
      g_free (dspec);
      return out;
```

par :

```c
      gboolean long_double = (fmt_type[0] == 'L');
      gchar *rspec = spec_rebuild (spec, long_double ? 'L' : '\0');
      gchar *out = format_double_c_locale (rspec, long_double, g_ascii_strtod (arg_str, NULL));
      g_free (rspec);
      return out;
```

Dans `macro_type_value_valid`, remplacer `strtod (value, &end);` par `g_ascii_strtod (value, &end);`.

- [ ] **Step 3 : couleurs de `Copy_configuration`**

Dans `src/term_config.c`, `Copy_configuration`, remplacer chacun des huit groupes :

```c
	string = g_strdup_printf("%f", term_conf.foreground_color.red);
	cfgStoreValue(cfg, "term_foreground_red", string, CFG_INI, pos);
	g_free(string);
```

(red, green, blue, alpha, pour `foreground_color` et `background_color`) par une ligne de la forme :

```c
	cfgStoreValue(cfg, "term_foreground_red", g_ascii_formatd(nombre, sizeof(nombre), "%f", term_conf.foreground_color.red), CFG_INI, pos);
```

et déclarer en tête de `Copy_configuration` : `gchar nombre[G_ASCII_DTOSTR_BUF_SIZE];` avec le commentaire `/* Flottants toujours écrits avec un point : la locale de l'utilisateur est active (M9). */`. La transformation peut se faire avec ce script, puis se vérifier :

```bash
python3 - <<'EOF'
import re
p='src/term_config.c'; s=open(p,encoding='utf-8').read()
pat=re.compile(r'\tstring = g_strdup_printf\("%f", (term_conf\.(?:fore|back)ground_color\.(?:red|green|blue|alpha))\);\n\tcfgStoreValue\(cfg, "([a-z_]+)", string, CFG_INI, pos\);\n\tg_free\(string\);\n')
s,n=pat.subn(lambda m:'\tcfgStoreValue(cfg, "%s", g_ascii_formatd(nombre, sizeof(nombre), "%%f", %s), CFG_INI, pos);\n'%(m.group(2),m.group(1)),s)
assert n==8,n
open(p,'w',encoding='utf-8').write(s)
EOF
grep -c 'g_ascii_formatd' src/term_config.c
```
Expected : `8`. Ajouter ensuite la déclaration de `nombre` à la main.

- [ ] **Step 4 : vérifier**

Run : `ninja -C build 2>&1 | grep -E "error|warning: .*(macros_format|term_config)"; meson test -C build --suite unit 2>&1 | grep -E "test_macros_format|^(Ok|Fail)"; grep -n 'g_strdup_printf("%f"' src/term_config.c`
Expected : aucune erreur ; `test_macros_format OK` ; `Fail: 0` ; aucune ligne pour le dernier grep.

- [ ] **Step 5 : commit**

```bash
git add src/macros_format.c src/term_config.c tests/test_macros_format.c
git commit -m "macros, config : flottants indépendants de la langue (g_ascii_strtod, locale C par thread, g_ascii_formatd) — suite de M9, essais EM-16/EM-17

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5 : expansion des macros — M18, M19, M20, M23

**Files :**
- Modify : `src/macros_format.c` (`try_parse_list_spec`, `apply_spec`, `macro_type_value_valid`, `format_action_with_args`), `tests/test_macros_format.c` (tables)

**Interfaces :**
- Consumes : `macro_list_find` (`macros_list.h`).
- Produces : décisions 2 à 5 de la spec.
  - Entiers saisis en décimal ou en `0x…`, jamais en octal.
  - `%#Nom` n'est une liste que si elle existe.
  - Antislashs protégés après printf.
  - Troncature des libellés exacte.

- [ ] **Step 1 : rendre les tables exigeantes (rouge)**

Dans `tests/test_macros_format.c`, table `cas_expansions` :
- remplacer `{ "%x", "ff", NULL, "ff", "M23" },` par `{ "%x", "ff", NULL, "0", NULL },	/* « ff » sans 0x n'est pas un nombre : refusé à la validation */` ;
- remplacer `{ "%x", "010", NULL, "10", "M23" },` par `{ "%x", "010", NULL, "a", NULL },	/* décision 2 : décimal, jamais d'octal */` ;
- sur les trois lignes `"M18"`, les deux lignes `"M19"` et la ligne `"M20"`, remplacer l'identifiant par `NULL` ;
- ajouter, après la ligne `{ "%x", "0x1f", NULL, "1f", NULL },` :

```c
	{ "%X", "0x1f", NULL, "1F", NULL },
	{ "%d", "0x10", NULL, "16", NULL },		/* décision 2 : 0x accepté pour tous les entiers */
	{ "%u", "0x10", NULL, "16", NULL },
	{ "%d", "010", NULL, "10", NULL },
	{ "%o", "010", NULL, "12", NULL },
```

Table `cas_validations` :
- remplacer `{ "x", "ff", TRUE, "M23" },` par `{ "x", "ff", FALSE, NULL },` ;
- remplacer `{ "x", "09", TRUE, "M23" },` par `{ "x", "09", TRUE, NULL },` ;
- ajouter après la ligne `{ "x", "0x10", TRUE, NULL },` :

```c
	{ "x", "010", TRUE, NULL },
	{ "d", "0x10", TRUE, NULL },
	{ "u", "0x10", TRUE, NULL },
```

Run : `meson test -C build test_macros_format >/dev/null 2>&1; grep "^not ok" build/meson-logs/testlog.txt | grep macros_format | wc -l`
Expected : un nombre ≥ 9. En échec : M18 ×3, M19 ×2, M20 ×1, `%x [010]`, `%d [0x10]`, `%u [0x10]`, et les validations `x 09`, `d 0x10`, `u 0x10`.

- [ ] **Step 2 : M23, base de lecture des entiers**

Dans `src/macros_format.c`, juste avant `static gchar *apply_spec`, ajouter :

```c
/* Base de lecture d'un argument entier : hexadécimal avec le préfixe 0x/0X, décimal sinon — jamais d'octal (M23). */
static gint
int_arg_base (const gchar *s)
{
  while (*s == ' ' || *s == '\t')
    s++;
  if (*s == '+' || *s == '-')
    s++;
  return (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) ? 16 : 10;
}
```

Dans `apply_spec`, dans les trois branches entières (`d/i`, `u/o`, `x/X`), remplacer le dernier argument de chaque appel `strtol`, `strtoll`, `strtoul` et `strtoull` (`10` ou `0`) par `int_arg_base (arg_str)`. Par script :

```bash
python3 - <<'EOF'
import re
p='src/macros_format.c'; s=open(p,encoding='utf-8').read()
a=s.index('static gchar *\napply_spec'); b=s.index('if (strchr ("fFeEgGaA", conv))', a)
seg=s[a:b]
seg2,n=re.subn(r'(strto(?:l|ll|ul|ull) \(arg_str, NULL, )(?:10|0)\)', r'\1int_arg_base (arg_str))', seg)
assert n==15, n
open(p,'w',encoding='utf-8').write(s[:a]+seg2+s[b:])
EOF
```

Dans `macro_type_value_valid` :
- remplacer `gint64 v = g_ascii_strtoll (value, &end, 10);` par `gint64 v = g_ascii_strtoll (value, &end, int_arg_base (value));` ;
- remplacer `gint base = (conv == 'x' || conv == 'X') ? 0 : 10;` par `gint base = int_arg_base (value);`.

`int_arg_base` est défini avant `apply_spec`, qui précède `macro_type_value_valid` dans le fichier : l'ordre convient.

- [ ] **Step 3 : M18, liste seulement si elle existe**

Dans `try_parse_list_spec`, remplacer :

```c
  gchar *name = g_strndup (&action[name_start], j - name_start);
  if (name_out)
    *name_out = name;
```

par :

```c
  gchar *name = g_strndup (&action[name_start], j - name_start);
  /* « %#x » n'est une liste que si une liste de ce nom existe ; sinon « # » est le drapeau printf (M18). */
  if (macro_list_find (name) < 0)
    {
      g_free (name);
      return FALSE;
    }
  if (name_out)
    *name_out = name;
  else
    g_free (name);
```

- [ ] **Step 4 : M19 et M20 dans `format_action_with_args`**

Juste avant `gchar *\nformat_action_with_args`, ajouter :

```c
/* Nombre de caractères que action[from..to[ a produits dans le résultat : « %% » n'en produit qu'un (M20). */
static gsize
expanded_length (const gchar *action, gint from, gint to)
{
  gsize n = 0;

  for (gint k = from; k < to; k++)
    {
      if (action[k] == '%' && k + 1 < to && action[k + 1] == '%')
        k++;
      n++;
    }
  return n;
}
```

Dans `format_action_with_args`, remplacer :

```c
          g_string_truncate (result, result->len - (i - label_start));
```

par :

```c
          g_string_truncate (result, result->len - expanded_length (action, label_start, i));
```

et remplacer :

```c
          gchar *escaped_arg = escape_arg_backslashes (args[arg_idx]);
          gchar *formatted = apply_spec (spec, fmt_type, escaped_arg);
          g_string_append (result, formatted);
          g_free (formatted);
          g_free (escaped_arg);
```

par :

```c
          /* Mise en forme d'abord (largeur, précision), protection des antislashs ensuite (M19). */
          gchar *formatted = apply_spec (spec, fmt_type, args[arg_idx]);
          gchar *escaped = escape_arg_backslashes (formatted);
          g_string_append (result, escaped);
          g_free (escaped);
          g_free (formatted);
```

- [ ] **Step 5 : vérifier**

Run : `ninja -C build 2>&1 | grep -E "error|warning: .*macros_format"; meson test -C build --suite unit 2>&1 | grep -E "test_macros_format|^(Ok|Fail)"; grep -cE "TODO bogue (M18|M19|M20|M23)" build/meson-logs/testlog.txt`
Expected : aucune erreur ; `test_macros_format OK` ; `Fail: 0` ; `0`.

- [ ] **Step 6 : commit**

```bash
git add src/macros_format.c tests/test_macros_format.c
git commit -m "macros : %#x drapeau printf hors liste (M18), antislashs protégés après printf (M19), troncature des libellés (M20), entiers décimaux ou 0x sans octal (M23)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6 : `config_file.c` — C5, C6, C13, M11, M13

**Files :**
- Modify : `src/config_file.c`, `src/config_file.h` (créés à la Task 2), `src/term_config.c`, `tests/meson.build`
- Create : `tests/test_config_file.c`

**Interfaces :**
- Consumes : `config_wait_char_from_file` (Task 2) ; `cfgParse`, `cfgSectionNumberToName`, `cfgAllocForNewSection`, `cfgStoreValue`, `cfgDump` (`parsecfg.h`).
- Produces (dans `src/config_file.h`) :

```c
#include "parsecfg.h"

gchar   *config_file_prepare(const gchar *config_dir, const gchar *home_dir);         /* crée config_dir, migre home_dir/.gtktermrc, renvoie le chemin (à libérer) */
gint     config_file_parse_profiles(const gchar *path, cfgStruct cfg[]);             /* lecture des profils sans les groupes réservés ; fichier non modifié */
gint     config_file_find_profile(const gchar *name, gint max);                      /* indice du profil (casse ignorée) ou -1 */
gboolean config_file_is_reserved_name(const gchar *name);                            /* window, colors, layout, session (casse ignorée) */
gint     config_file_remove_section(const gchar *path, const gchar *section);        /* 0 si retirée, -1 sinon ; réécriture atomique */
gchar   *config_file_strip_reserved_groups(const gchar *path);                       /* retire les groupes réservés du fichier, les renvoie (à libérer) */
void     config_file_restore_reserved_groups(const gchar *path, const gchar *extras);/* les remet en fin de fichier */
```

- [ ] **Step 1 : écrire le test**

Créer `tests/test_config_file.c` :

```c
/* Tests de config_file.c : profils de .gtktermrc et groupes GKeyFile réservés (C5, C6, C13, M11, M13, C7 côté config). */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "config_file.h"
#include "test_util.h"

static gchar **port;
static gchar **socket_host;
static guint *speed;

static cfgStruct cfg[] =
{
	{"port", CFG_STRING, &port},
	{"socket_host", CFG_STRING, &socket_host},
	{"speed", CFG_UINT, &speed},
	{NULL, CFG_END, NULL}
};

static gchar *
lire(const gchar *chemin)
{
	gchar *contenu = NULL;

	g_assert_true(g_file_get_contents(chemin, &contenu, NULL, NULL));
	return contenu;
}

static void
test_noms_reserves(void)
{
	g_assert_true(config_file_is_reserved_name("window"));
	g_assert_true(config_file_is_reserved_name("Colors"));
	g_assert_true(config_file_is_reserved_name("LAYOUT"));
	g_assert_true(config_file_is_reserved_name("session"));
	g_assert_false(config_file_is_reserved_name("default"));
	g_assert_false(config_file_is_reserved_name(NULL));
}

static void
test_lecture_avec_session_contenant_une_espace(void)
{
	static const gchar *contenu = "[default]\nport = /dev/ttyS0\n\n[session]\nscript_path=/home/x/mon script.lua\n";
	gchar *dir = g_dir_make_tmp("gtkterm-test-XXXXXX", NULL);
	gchar *chemin = test_write_file(dir, "rc", contenu);
	gchar *apres;

	g_assert_cmpint(config_file_parse_profiles(chemin, cfg), ==, 1);
	g_assert_cmpstr(port[0], ==, "/dev/ttyS0");
	apres = lire(chemin);
	g_assert_cmpstr(apres, ==, contenu);
	g_free(apres);
	g_free(chemin);
	test_rmdir(dir);
	g_free(dir);
}

static void
test_groupes_reserves_absents_de_la_liste(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-test-XXXXXX", NULL);
	gchar *chemin = test_write_file(dir, "rc", "[default]\nport = /dev/a\n[window]\nx=1\n[usb]\nport = /dev/b\n[Colors]\nbg=1\n");

	g_assert_cmpint(config_file_parse_profiles(chemin, cfg), ==, 2);
	g_assert_cmpstr(cfgSectionNumberToName(0), ==, "default");
	g_assert_cmpstr(cfgSectionNumberToName(1), ==, "usb");
	g_free(chemin);
	test_rmdir(dir);
	g_free(dir);
}

static void
test_recherche_sans_casse(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-test-XXXXXX", NULL);
	gchar *chemin = test_write_file(dir, "rc", "[default]\nport = /dev/a\n[usb]\nport = /dev/b\n");
	gint max = config_file_parse_profiles(chemin, cfg);

	g_assert_cmpint(max, ==, 2);
	g_assert_cmpint(config_file_find_profile("Default", max), ==, 0);
	g_assert_cmpint(config_file_find_profile("USB", max), ==, 1);
	g_assert_cmpint(config_file_find_profile("absent", max), ==, -1);
	g_free(chemin);
	test_rmdir(dir);
	g_free(dir);
}

static const gchar *trois_profils = "[a]\nport = /dev/a\n\n[b]\nport = /dev/b\nsocket_host = [::1]\n\n[c]\nport = /dev/c";

static void
test_retrait_section_du_milieu(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-test-XXXXXX", NULL);
	gchar *chemin = test_write_file(dir, "rc", trois_profils);

	g_assert_cmpint(config_file_remove_section(chemin, "B"), ==, 0);
	g_assert_cmpint(config_file_parse_profiles(chemin, cfg), ==, 2);
	g_assert_cmpstr(cfgSectionNumberToName(0), ==, "a");
	g_assert_cmpstr(cfgSectionNumberToName(1), ==, "c");
	g_assert_cmpstr(port[1], ==, "/dev/c");
	g_free(chemin);
	test_rmdir(dir);
	g_free(dir);
}

static void
test_retrait_derniere_section_sans_fin_de_ligne(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-test-XXXXXX", NULL);
	gchar *chemin = test_write_file(dir, "rc", trois_profils);

	g_assert_cmpint(config_file_remove_section(chemin, "c"), ==, 0);
	g_assert_cmpint(config_file_parse_profiles(chemin, cfg), ==, 2);
	g_assert_cmpstr(socket_host[1], ==, "[::1]");
	g_free(chemin);
	test_rmdir(dir);
	g_free(dir);
}

static void
test_retrait_section_absente_ou_fichier_vide(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-test-XXXXXX", NULL);
	gchar *chemin = test_write_file(dir, "rc", trois_profils);
	gchar *vide = test_write_file(dir, "vide", "");
	gchar *apres;

	g_assert_cmpint(config_file_remove_section(chemin, "z"), ==, -1);
	apres = lire(chemin);
	g_assert_cmpstr(apres, ==, trois_profils);
	g_free(apres);
	g_assert_cmpint(config_file_remove_section(vide, "a"), ==, -1);
	g_free(vide);
	g_free(chemin);
	test_rmdir(dir);
	g_free(dir);
}

static void
test_retrait_puis_remise_des_groupes_reserves(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-test-XXXXXX", NULL);
	gchar *chemin = test_write_file(dir, "rc", "[default]\nport = /dev/a\n[window]\nx=1\n[session]\nscript_path=/a b.lua\n");
	gchar *extras, *sans, *avec;

	extras = config_file_strip_reserved_groups(chemin);
	sans = lire(chemin);
	g_assert_null(strstr(sans, "[window]"));
	g_assert_nonnull(strstr(extras, "[window]"));
	g_assert_nonnull(strstr(extras, "[session]"));
	config_file_restore_reserved_groups(chemin, extras);
	avec = lire(chemin);
	g_assert_nonnull(strstr(avec, "[default]"));
	g_assert_nonnull(strstr(avec, "script_path=/a b.lua"));
	g_free(avec);
	g_free(sans);
	g_free(extras);
	g_free(chemin);
	test_rmdir(dir);
	g_free(dir);
}

static void
test_preparation_cree_le_repertoire_et_migre(void)
{
	gchar *base = g_dir_make_tmp("gtkterm-test-XXXXXX", NULL);
	gchar *home = g_build_filename(base, "home", NULL);
	gchar *conf = g_build_filename(base, "conf", "sous", NULL);
	gchar *ancien, *chemin, *contenu;

	g_assert_cmpint(g_mkdir(home, 0700), ==, 0);
	ancien = test_write_file(home, ".gtktermrc", "[default]\n");
	chemin = config_file_prepare(conf, home);
	g_assert_true(g_str_has_suffix(chemin, "/conf/sous/.gtktermrc"));
	g_assert_true(g_file_test(conf, G_FILE_TEST_IS_DIR));
	contenu = lire(chemin);
	g_assert_cmpstr(contenu, ==, "[default]\n");
	g_assert_false(g_file_test(ancien, G_FILE_TEST_EXISTS));

	g_unlink(chemin);
	g_rmdir(conf);
	{
		gchar *parent = g_path_get_dirname(conf);
		g_rmdir(parent);
		g_free(parent);
	}
	g_rmdir(home);
	g_rmdir(base);
	g_free(contenu);
	g_free(chemin);
	g_free(ancien);
	g_free(conf);
	g_free(home);
	g_free(base);
}

static void
test_premier_profil_dans_un_fichier_vide(void)
{
	/* C7 côté configuration : premier enregistrement dans un .gtktermrc vide. */
	gchar *dir = g_dir_make_tmp("gtkterm-test-XXXXXX", NULL);
	gchar *chemin = test_write_file(dir, "rc", "");
	gint idx;

	g_assert_cmpint(config_file_parse_profiles(chemin, cfg), ==, 0);
	idx = cfgAllocForNewSection(cfg, "default") - 1;
	g_assert_cmpint(idx, ==, 0);
	g_assert_cmpint(cfgStoreValue(cfg, "port", "/dev/ttyS0", CFG_INI, idx), ==, 0);
	g_assert_cmpint(cfgDump(chemin, cfg, CFG_INI, idx + 1), ==, 0);
	g_assert_cmpint(config_file_parse_profiles(chemin, cfg), ==, 1);
	g_assert_cmpstr(port[0], ==, "/dev/ttyS0");
	g_free(chemin);
	test_rmdir(dir);
	g_free(dir);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/config_file/noms-reserves", test_noms_reserves);
	g_test_add_func("/config_file/lecture-avec-session-contenant-une-espace", test_lecture_avec_session_contenant_une_espace);
	g_test_add_func("/config_file/groupes-reserves-absents-de-la-liste", test_groupes_reserves_absents_de_la_liste);
	g_test_add_func("/config_file/recherche-sans-casse", test_recherche_sans_casse);
	g_test_add_func("/config_file/retrait-section-du-milieu", test_retrait_section_du_milieu);
	g_test_add_func("/config_file/retrait-derniere-section-sans-fin-de-ligne", test_retrait_derniere_section_sans_fin_de_ligne);
	g_test_add_func("/config_file/retrait-section-absente-ou-fichier-vide", test_retrait_section_absente_ou_fichier_vide);
	g_test_add_func("/config_file/retrait-puis-remise-des-groupes-reserves", test_retrait_puis_remise_des_groupes_reserves);
	g_test_add_func("/config_file/preparation-cree-le-repertoire-et-migre", test_preparation_cree_le_repertoire_et_migre);
	g_test_add_func("/config_file/premier-profil-dans-un-fichier-vide", test_premier_profil_dans_un_fichier_vide);
	return g_test_run();
}
```

Ajouter `'test_config_file' : [],` au dictionnaire `unit_tests` de `tests/meson.build`.

- [ ] **Step 2 : constater l'échec**

Run : `meson test -C build test_config_file 2>&1 | grep -E "error|FAIL" | head -3`
Expected : erreurs de compilation, fonctions `config_file_parse_profiles` etc. non déclarées.

- [ ] **Step 3 : écrire le module**

Compléter `src/config_file.h` en y ajoutant, après `#include <glib.h>`, `#include "parsecfg.h"`, puis, après la déclaration de `config_wait_char_from_file`, les sept déclarations de la section Interfaces ci-dessus, chacune précédée de son commentaire.

Remplacer `src/config_file.c` par :

```c
/* Fichier de configuration .gtktermrc : profils (parsecfg) et groupes GKeyFile réservés, sans GTK. */

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <glib.h>
#include <glib/gstdio.h>

#include "config_file.h"

#define CONFIG_FILE_NAME ".gtktermrc"

/* Groupes écrits par GKeyFile dans le même fichier : jamais des profils. */
static const gchar * const reserved_names[] = { "window", "colors", "layout", "session", NULL };

gint
config_wait_char_from_file(gint valeur)
{
	if(valeur >= 1 && valeur <= 255)
		return valeur;
	if(valeur <= -2 && valeur >= -128)
		return (guchar)valeur;
	return -1;
}

gboolean
config_file_is_reserved_name(const gchar *name)
{
	if(name == NULL)
		return FALSE;
	for(gint i = 0; reserved_names[i] != NULL; i++)
		if(g_ascii_strcasecmp(name, reserved_names[i]) == 0)
			return TRUE;
	return FALSE;
}

/* Nom de la section si la ligne est un en-tête « [nom] » (blancs initiaux tolérés), NULL sinon ; à libérer. */
static gchar *
section_name(const gchar *line)
{
	const gchar *end;

	while(*line == ' ' || *line == '\t')
		line++;
	if(*line != '[')
		return NULL;
	end = strchr(line + 1, ']');
	if(end == NULL)
		return NULL;
	return g_strstrip(g_strndup(line + 1, end - line - 1));
}

/* Sépare le contenu : renvoie les lignes hors groupes réservés, place les groupes réservés dans *extras_out. */
static gchar *
split_reserved(const gchar *contents, gchar **extras_out)
{
	GString *kept = g_string_new(NULL);
	GString *extras = g_string_new(NULL);
	gchar **lines = g_strsplit(contents, "\n", -1);
	gboolean in_reserved = FALSE;

	for(gint i = 0; lines[i] != NULL; i++)
	{
		gchar *name = section_name(lines[i]);
		GString *dest;

		if(name != NULL)
		{
			in_reserved = config_file_is_reserved_name(name);
			g_free(name);
		}
		dest = in_reserved ? extras : kept;
		g_string_append(dest, lines[i]);
		if(lines[i + 1] != NULL)
			g_string_append_c(dest, '\n');
	}
	g_strfreev(lines);
	*extras_out = g_string_free(extras, FALSE);
	return g_string_free(kept, FALSE);
}

gchar *
config_file_strip_reserved_groups(const gchar *path)
{
	gchar *contents = NULL;
	gchar *extras = NULL;
	gchar *kept;

	if(!g_file_get_contents(path, &contents, NULL, NULL))
		return g_strdup("");
	kept = split_reserved(contents, &extras);
	g_file_set_contents(path, kept, -1, NULL);
	g_free(kept);
	g_free(contents);
	return extras;
}

void
config_file_restore_reserved_groups(const gchar *path, const gchar *extras)
{
	gchar *existing = NULL;
	gsize length = 0;
	GString *out;

	if(extras == NULL || extras[0] == '\0')
		return;
	if(!g_file_get_contents(path, &existing, &length, NULL))
	{
		existing = g_strdup("");
		length = 0;
	}
	out = g_string_new_len(existing, length);
	g_free(existing);
	if(out->len > 0 && out->str[out->len - 1] != '\n')
		g_string_append_c(out, '\n');
	g_string_append(out, extras);
	g_file_set_contents(path, out->str, out->len, NULL);
	g_string_free(out, TRUE);
}

gint
config_file_parse_profiles(const gchar *path, cfgStruct cfg[])
{
	gchar *contents = NULL;
	gchar *extras = NULL;
	gchar *kept;
	gchar *tmp_path = NULL;
	gint fd;
	gint max = -1;

	/* Fichier absent ou illisible : même comportement qu'avant (cfgParse signale l'erreur). */
	if(!g_file_get_contents(path, &contents, NULL, NULL))
		return cfgParse(path, cfg, CFG_INI);

	/* Lecture sur une copie sans les groupes GKeyFile : le fichier de l'utilisateur n'est jamais modifié (C5, C13). */
	kept = split_reserved(contents, &extras);
	fd = g_file_open_tmp("gtkterm-XXXXXX.ini", &tmp_path, NULL);
	if(fd >= 0)
	{
		close(fd);
		if(g_file_set_contents(tmp_path, kept, -1, NULL))
			max = cfgParse(tmp_path, cfg, CFG_INI);
		g_unlink(tmp_path);
	}
	g_free(tmp_path);
	g_free(kept);
	g_free(extras);
	g_free(contents);
	return max;
}

gint
config_file_find_profile(const gchar *name, gint max)
{
	for(gint i = 0; i < max; i++)
		if(g_ascii_strcasecmp(name, cfgSectionNumberToName(i)) == 0)
			return i;
	return -1;
}

gint
config_file_remove_section(const gchar *path, const gchar *section)
{
	gchar *contents = NULL;
	gchar **lines;
	GString *out;
	gboolean skipping = FALSE;
	gboolean found = FALSE;
	gint result = -1;

	if(!g_file_get_contents(path, &contents, NULL, NULL))
		return -1;
	out = g_string_new(NULL);
	lines = g_strsplit(contents, "\n", -1);
	for(gint i = 0; lines[i] != NULL; i++)
	{
		gchar *name = section_name(lines[i]);

		if(name != NULL)
		{
			skipping = (g_ascii_strcasecmp(name, section) == 0);
			found = found || skipping;
			g_free(name);
		}
		if(!skipping)
		{
			g_string_append(out, lines[i]);
			if(lines[i + 1] != NULL)
				g_string_append_c(out, '\n');
		}
	}
	/* En-têtes reconnus seulement en début de ligne (M11 : une valeur « [::1] » ne coupe plus la section) ; écriture atomique. */
	if(found && g_file_set_contents(path, out->str, out->len, NULL))
		result = 0;
	g_strfreev(lines);
	g_string_free(out, TRUE);
	g_free(contents);
	return result;
}

gchar *
config_file_prepare(const gchar *config_dir, const gchar *home_dir)
{
	gchar *path = g_build_filename(config_dir, CONFIG_FILE_NAME, NULL);
	gchar *old_path = g_build_filename(home_dir, CONFIG_FILE_NAME, NULL);

	/* ~/.config peut ne pas exister (M13). */
	if(g_mkdir_with_parents(config_dir, 0700) != 0)
		g_printerr("Cannot create %s: %s\n", config_dir, g_strerror(errno));

	/* Ancien emplacement $HOME/.gtktermrc : déplacé vers le nouveau s'il n'y a rien là-bas. */
	if(!g_file_test(path, G_FILE_TEST_EXISTS) && g_file_test(old_path, G_FILE_TEST_EXISTS) && g_rename(old_path, path) != 0)
	{
		gchar *contents = NULL;
		gsize length = 0;

		if(g_file_get_contents(old_path, &contents, &length, NULL) && g_file_set_contents(path, contents, length, NULL))
			g_unlink(old_path);
		g_free(contents);
	}
	g_free(old_path);
	return path;
}
```

- [ ] **Step 4 : constater que le module passe ses tests**

Run : `meson test -C build test_config_file 2>&1 | grep -E "test_config_file"`
Expected : `test_config_file OK` (10 sous-tests).

- [ ] **Step 5 : brancher `term_config.c`**

`#include "config_file.h"` est déjà présent depuis la Task 2. Dans `src/term_config.c` :

1. `config_file_init` : remplacer tout le corps par :

```c
	gchar *path = config_file_prepare(g_get_user_config_dir(), g_get_home_dir());

	config_file = g_file_new_for_path(path);
	g_free(path);
```

et supprimer le commentaire d'en-tête périmé du corps, ou le garder résumé en une ligne : `/* $XDG_CONFIG_HOME/.gtktermrc ; ancien $HOME/.gtktermrc migré (config_file_prepare). */`.
2. Supprimer le tableau `kf_groups`, les fonctions statiques `cfg_strip_kf_sections` et `cfg_restore_kf_sections`, la déclaration `static gint remove_section(const gchar *, gchar *);` et la fonction `remove_section` en entier.
3. Remplacements mécaniques, à vérifier un par un après coup :
   - `cfg_strip_kf_sections()` → `config_file_strip_reserved_groups(path)` (dans `save_config_silent` et `really_save_config`, où `path` existe déjà) ;
   - `cfg_restore_kf_sections(kf_backup)` → `config_file_restore_reserved_groups(path, kf_backup)` ;
   - `remove_section(path, (char *)name)` → `config_file_remove_section(path, name)` ;
   - `remove_section(path, (char *)data)` → `config_file_remove_section(path, (const gchar *)data)` ;
   - `remove_section(g_file_get_path(config_file), txt)` (dans `delete_config`) → `config_file_remove_section(get_config_file_path(), txt)`.
4. Recherche de profil (C6) :
   - dans `save_config_silent`, remplacer la boucle `cfg_num = -1; for(i = 0; i < max; i++) { if(!strcmp(name, cfgSectionNumberToName(i))) cfg_num = i; }` par `cfg_num = config_file_find_profile(name, max);` ;
   - dans `really_save_config`, remplacer la boucle équivalente (sur `(char *)data`) par `cfg_num = config_file_find_profile((const gchar *)data, max);` ;
   - dans `Load_configuration_from_file`, remplacer `if(!strcmp(config_name, cfgSectionNumberToName(i)))` par `if(g_ascii_strcasecmp(config_name, cfgSectionNumberToName(i)) == 0)`.
5. Lectures en lecture seule (C5, C13) : dans `Select_config`, `save_config` et `Load_configuration_from_file`, remplacer `cfgParse(g_file_get_path(config_file), cfg, CFG_INI)` par `config_file_parse_profiles(get_config_file_path(), cfg)`.
6. `save_config` (C6, C13) : remplacer le bloc qui va de `config_name = gtk_entry_get_text(GTK_ENTRY(edit));` jusqu'à la fin du `if(i == max) … really_save_config(…);` par :

```c
		config_name = gtk_entry_get_text(GTK_ENTRY(edit));

		/* window, colors, layout, session : groupes GKeyFile du même fichier, jamais des profils (C13). */
		if(config_file_is_reserved_name(config_name))
		{
			show_message(_("This configuration name is reserved."), MSG_ERR);
			return;
		}

		/* Comparaison sans casse, comme parsecfg : « Default » propose d'écraser « default » (C6). */
		if(config_file_find_profile(config_name, max) >= 0)
		{
			GtkWidget *message_dialog;
			message_dialog = gtk_message_dialog_new_with_markup(GTK_WINDOW(Fenetre),
			                 GTK_DIALOG_DESTROY_WITH_PARENT,
			                 GTK_MESSAGE_QUESTION,
			                 GTK_BUTTONS_NONE,
			                 _("<b>Section [%s] already exists.</b>\n\nDo you want to overwrite it ?"),
			                 config_name);

			gtk_dialog_add_buttons(GTK_DIALOG(message_dialog),
			                       "_Cancel",
			                       GTK_RESPONSE_NONE,
			                       "_Yes",
			                       GTK_RESPONSE_ACCEPT,
			                       NULL);

			if (gtk_dialog_run(GTK_DIALOG(message_dialog)) == GTK_RESPONSE_ACCEPT)
				really_save_config(Fenetre, GTK_RESPONSE_ACCEPT, (gpointer)config_name);

			gtk_widget_destroy(message_dialog);
		}
		else
			really_save_config(Fenetre, GTK_RESPONSE_ACCEPT, (gpointer)config_name);
```

et supprimer la variable `i` de `save_config` si elle n'est plus utilisée. `show_message` attend un `gchar *` : écrire `show_message((gchar *)_("This configuration name is reserved."), MSG_ERR);` si le compilateur avertit.

Run : `grep -n "cfg_strip_kf_sections\|cfg_restore_kf_sections\|remove_section(\|kf_groups\|cfgParse(g_file_get_path" src/term_config.c`
Expected : aucune sortie. Les `cfgParse(path, cfg, CFG_INI)` de `save_config_silent` et `really_save_config` restent : elles lisent le fichier déjà débarrassé des groupes réservés.

- [ ] **Step 6 : vérifier**

Run : `ninja -C build 2>&1 | grep -E "error|warning: .*(term_config|config_file)"; meson test -C build 2>&1 | grep -E "^(Ok|Fail)"`
Expected : aucune erreur ; `Fail: 0`.

- [ ] **Step 7 : commit**

```bash
git add src/config_file.c src/config_file.h src/term_config.c tests/test_config_file.c tests/meson.build
git commit -m "config : config_file.c — lecture des profils sans groupes GKeyFile (C5, C13), noms sans casse (C6), retrait de section ancré et atomique (M11), répertoire créé (M13) ; essai EM-3

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7 : envoi des macros et raccourcis — C8, M42, M15, M17

**Files :**
- Create : `tests/test_macros_send.c`
- Modify :
  - `src/macros.c` : `send_macro_with_args`, `shortcut_callback`, `create_shortcuts`, `add_shortcuts`, `macros_destroy`, `remove_shortcuts`, `Save_shortcuts` ;
  - `tests/meson.build` (`integration_tests`) ;
  - `docs/superpowers/specs/2026-10-06-correctifs-critiques-design.md` (essai EM-18).

**Interfaces :**
- Consumes : bouchons de la Task 2 (`stub_sent`, `stub_reset`, `stub_count`, `shortcuts`).
- Produces :
  - `shortcut_callback` envoie avec les arguments enregistrés de la macro, par le même chemin que le panneau ;
  - indice hors bornes ignoré ;
  - un raccourci n'est connecté qu'une fois ;
  - aucune entrée de `macros` n'a de `label` ou de `shortcut` NULL hors de la sentinelle.

- [ ] **Step 1 : écrire le test**

Créer `tests/test_macros_send.c` :

```c
/* Tests d'intégration du chemin d'envoi de macros.c (bouchons d'interface ; GTK lié sans gtk_init ni fenêtre). */

#include <gtk/gtk.h>
#include <string.h>

#include "interface.h"
#include "macros.h"
#include "stubs.h"

static void
installer_macros(void)
{
	macro_t tab[4];
	gchar *args[] = { "42", NULL };

	memset(tab, 0, sizeof(tab));
	tab[0].label = g_strdup("Vitesse");
	tab[0].shortcut = g_strdup("<Control>F5");
	tab[0].action = g_strdup("V%d\\r");
	tab[0].args = g_strdupv(args);
	tab[1].label = NULL;					/* C8 : macro sans libellé */
	tab[1].shortcut = g_strdup("<Control>F6");
	tab[1].action = g_strdup("SANS_LIBELLE");
	tab[2].label = g_strdup("Pourcent");
	tab[2].shortcut = g_strdup("None");
	tab[2].action = g_strdup("100%%");
	create_shortcuts(tab, 3);
}

static gchar *
envoye(void)
{
	GByteArray *octets = stub_sent();

	return g_strndup((const gchar *)octets->data, octets->len);
}

static void
test_raccourci_avec_arguments_enregistres(void)
{
	gchar *s;

	stub_reset();
	installer_macros();
	shortcut_callback(GINT_TO_POINTER(0));
	s = envoye();
	g_assert_cmpstr(s, ==, "V42\r");
	g_free(s);
	remove_shortcuts();
}

static void
test_raccourci_double_pourcent(void)
{
	gchar *s;

	stub_reset();
	installer_macros();
	shortcut_callback(GINT_TO_POINTER(2));
	s = envoye();
	g_assert_cmpstr(s, ==, "100%");
	g_free(s);
	remove_shortcuts();
}

static void
test_raccourci_sans_libelle(void)
{
	gchar *s;

	stub_reset();
	installer_macros();
	shortcut_callback(GINT_TO_POINTER(1));
	s = envoye();
	g_assert_cmpstr(s, ==, "SANS_LIBELLE");
	g_assert_cmpint(stub_count("Put_temp_message"), ==, 1);
	g_free(s);
	remove_shortcuts();
}

static void
test_indice_hors_bornes(void)
{
	stub_reset();
	installer_macros();
	shortcut_callback(GINT_TO_POINTER(99));
	shortcut_callback(GINT_TO_POINTER(-1));
	g_assert_cmpuint(stub_sent()->len, ==, 0);
	remove_shortcuts();
}

static void
test_raccourci_connecte_une_seule_fois(void)
{
	guint touche, n = 0;
	GdkModifierType modificateurs;

	stub_reset();
	shortcuts = gtk_accel_group_new();
	installer_macros();
	add_shortcuts();
	add_shortcuts();
	gtk_accelerator_parse("<Control>F6", &touche, &modificateurs);
	g_assert_cmpuint(touche, !=, 0);
	gtk_accel_group_query(shortcuts, touche, modificateurs, &n);
	g_assert_cmpuint(n, ==, 1);
	remove_shortcuts();
	gtk_accel_group_query(shortcuts, touche, modificateurs, &n);
	g_assert_cmpuint(n, ==, 0);
	g_clear_object(&shortcuts);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/macros_send/raccourci-avec-arguments-enregistres", test_raccourci_avec_arguments_enregistres);
	g_test_add_func("/macros_send/raccourci-double-pourcent", test_raccourci_double_pourcent);
	g_test_add_func("/macros_send/raccourci-sans-libelle", test_raccourci_sans_libelle);
	g_test_add_func("/macros_send/indice-hors-bornes", test_indice_hors_bornes);
	g_test_add_func("/macros_send/raccourci-connecte-une-seule-fois", test_raccourci_connecte_une_seule_fois);
	return g_test_run();
}
```

Dans `tests/meson.build`, dictionnaire `integration_tests`, ajouter :

```meson
	'test_macros_send' : {
		'sources' : files('../src/macros.c') + stub_journal + stub_ui + stub_display,
		'deps' : [gtk_deps],
		'link_args' : [],
	},
```

- [ ] **Step 2 : constater les échecs**

Run : `meson test -C build test_macros_send >/dev/null 2>&1; ./build/tests/test_macros_send --tap 2>&1 | grep -E "^(ok|not ok)|Bail|ERROR" | head`
Expected :
- `raccourci-avec-arguments-enregistres` en échec (`"V%d\r"` envoyé) ;
- `raccourci-double-pourcent` en échec (`"100%%"`) ;
- `raccourci-sans-libelle` plante (`strlen(NULL)`) et interrompt le binaire. C'est le rouge attendu ; pour voir les cas suivants, les lancer un par un avec `-p /macros_send/<nom>`.

Si l'édition de liens signale un symbole manquant de `macros.c` (autre que ceux des bouchons) : l'ajouter à `stub_ui.c`, enregistrement de l'appel sans logique, et le consigner.

Si `gtk_accelerator_parse` renvoie 0 sans `gtk_init` (touche inconnue) : remplacer `"<Control>F6"` par `"<Control>a"` dans `installer_macros` et dans le test, et le consigner.

- [ ] **Step 3 : corriger `macros.c`**

Juste avant `void\nsend_macro_with_args`, ajouter :

```c
/* Nom affiché d'une macro : son libellé, à défaut son raccourci (C8 : le libellé peut être absent). */
static const gchar *
macro_display_name (gint macro_index)
{
  const gchar *label = macros[macro_index].label;

  if (label != NULL && label[0] != '\0')
    return label;
  return macros[macro_index].shortcut != NULL ? macros[macro_index].shortcut : "";
}
```

Dans `send_macro_with_args` :
- remplacer `if (macro_index >= nb_macros || macros[macro_index].action == NULL)` par `if (macro_index < 0 || macro_index >= nb_macros || macros[macro_index].action == NULL)` ;
- remplacer l'argument `strlen (macros[macro_index].label) > 0 ? macros[macro_index].label : macros[macro_index].shortcut` par `macro_display_name (macro_index)`.

Remplacer toute la fonction `shortcut_callback` par :

```c
/* Fonction principale de callback pour l'exécution d'une macro (raccourci clavier, polling sans arguments) */
void
shortcut_callback (gpointer number)
{
  gint macro_index = GPOINTER_TO_INT (number);
  gint nb_macros = 0;
  gchar **args;

  get_shortcuts (&nb_macros);
  if (macro_index < 0 || macro_index >= nb_macros || macros[macro_index].action == NULL)
    return;

  /* Même chemin que le panneau : expansion avec les arguments enregistrés de la macro (M15). */
  args = macros[macro_index].args;
  send_macro_with_args (macro_index, (const gchar **) args, args != NULL ? (gint) g_strv_length (args) : 0);
}
```

Dans `create_shortcuts`, remplacer `macros = g_malloc ((size + 1) * sizeof (macro_t));` par `macros = g_malloc0 ((size + 1) * sizeof (macro_t));`.

Remplacer toute la fonction `add_shortcuts` par :

```c
void
add_shortcuts (void)
{
  gint nb_macros = 0;
  guint acc_key;
  GdkModifierType mod;

  if (macros == NULL)
    return;

  get_shortcuts (&nb_macros);
  for (gint i = 0; i < nb_macros; i++)
    {
      /* add_shortcuts est appelé à plusieurs endroits : une seule connexion par macro (M17). */
      if (macros[i].closure != NULL)
        {
          gtk_accel_group_disconnect (shortcuts, macros[i].closure);
          macros[i].closure = NULL;
        }
      if (macros[i].shortcut == NULL)
        continue;
      gtk_accelerator_parse (macros[i].shortcut, &acc_key, &mod);
      if (acc_key == 0)
        continue;
      macros[i].closure = g_cclosure_new_swap (G_CALLBACK (shortcut_callback), GINT_TO_POINTER (i), NULL);
      gtk_accel_group_connect (shortcuts, acc_key, mod, GTK_ACCEL_MASK, macros[i].closure);
    }
}
```

Dans `macros_destroy`, remplacer la boucle `while (macros[i].shortcut != NULL) { … i++; }` par une boucle sur le nombre de macros :

```c
  gint nb_macros = 0;

  get_shortcuts (&nb_macros);
  for (gint i = 0; i < nb_macros; i++)
    {
      g_free (macros[i].shortcut);
      g_free (macros[i].action);
      g_free (macros[i].label);
      g_free (macros[i].tab);
      g_strfreev (macros[i].args);
    }
```

en supprimant l'ancienne déclaration `gint i = 0;` et le commentaire `/* g_closure_unref … */`.

Remplacer le corps de `remove_shortcuts` par :

```c
  gint nb_macros = 0;

  if (macros == NULL)
    return;

  get_shortcuts (&nb_macros);
  for (gint i = 0; i < nb_macros; i++)
    if (macros[i].closure != NULL)
      {
        gtk_accel_group_disconnect (shortcuts, macros[i].closure);
        macros[i].closure = NULL;
      }

  macros_destroy ();
```

Dans `Save_shortcuts`, juste après l'appel `gtk_tree_model_get (model, &iter, COLUMN_LABEL, …, -1);` de la boucle qui remplit `macros[i]`, ajouter :

```c
              /* Une ligne sans libellé ni raccourci ne doit pas ressembler à la sentinelle de fin du tableau (M42). */
              if (macros[i].label == NULL)
                macros[i].label = g_strdup ("");
              if (macros[i].shortcut == NULL)
                macros[i].shortcut = g_strdup ("None");
              if (macros[i].action == NULL)
                macros[i].action = g_strdup ("");
```

- [ ] **Step 4 : vérifier**

Run : `ninja -C build 2>&1 | grep -E "error|warning: .*macros\.c"; meson test -C build --suite unit 2>&1 | grep -E "test_macros_send|^(Ok|Fail)"`
Expected : aucune erreur ; `test_macros_send OK` (5 sous-tests) ; `Fail: 0`.

- [ ] **Step 5 : ajouter l'essai manuel EM-18 à la spec**

Dans la spec, tableau §3, après la ligne EM-17, ajouter :

```markdown
| EM-18 | M42 | Fenêtre des macros : « Ajouter » une ligne laissée vide entre deux macros à raccourci, valider | Les macros et raccourcis situés après la ligne vide fonctionnent toujours |
```

et dans le critère d'acceptation 4, remplacer `EM-16 et EM-17` par `EM-16 à EM-18`.

- [ ] **Step 6 : commit**

```bash
git add src/macros.c tests/test_macros_send.c tests/meson.build docs/superpowers/specs/2026-10-06-correctifs-critiques-design.md
git commit -m "macros : raccourcis envoyés avec les arguments enregistrés (M15), connectés une seule fois et bornés (M17), libellé absent toléré (C8, M42) ; essais EM-5/EM-18

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8 : moteur Lua — C1, C2, C10, C11, M26, M27, M30, M9 (Lua)

**Files :**
- Create : `tests/stubs/stub_macros.c`, `tests/stubs/stub_macros.h`, `tests/test_script_engine.c`
- Modify : `src/script_engine.c`, `tests/meson.build` (`integration_tests`)

**Interfaces :**
- Consumes : `stub_sent`, `stub_send_thread`, `stub_inject_rx`, `stub_reset` (Task 2).
- Produces :
  - `tests/stubs/stub_macros.h` : `void stub_set_macros(macro_t *tab, gint n);` et `GThread *stub_get_shortcuts_thread(void);` ;
  - dans `script_engine.c` : `run_on_main_sync(GSourceFunc, gpointer)` (statique) ; `lua_port_close(int handle)` (statique) ;
  - `gtkterm.send(0, …)`, `send_hex(0, …)`, `send_macro` renvoient le vrai résultat de l'envoi.

- [ ] **Step 1 : écrire le bouchon de macros**

`tests/stubs/stub_macros.h` :

```c
/* Bouchon de get_shortcuts pour test_script_engine (macros fournies par le test). */
#ifndef STUB_MACROS_H_
#define STUB_MACROS_H_

#include "macros.h"

void     stub_set_macros(macro_t *tab, gint n);
GThread *stub_get_shortcuts_thread(void);

#endif
```

`tests/stubs/stub_macros.c` :

```c
/* Bouchon de get_shortcuts : renvoie le tableau fourni par le test et mémorise le thread appelant. */

#include "stub_macros.h"

static macro_t *macros_test = NULL;
static gint nb_macros_test = 0;
static GThread *thread_get_shortcuts = NULL;

void
stub_set_macros(macro_t *tab, gint n)
{
	macros_test = tab;
	nb_macros_test = n;
}

GThread *
stub_get_shortcuts_thread(void)
{
	return thread_get_shortcuts;
}

macro_t *
get_shortcuts(gint *size)
{
	thread_get_shortcuts = g_thread_self();
	*size = nb_macros_test;
	return macros_test;
}
```

- [ ] **Step 2 : écrire `tests/test_script_engine.c`**

```c
/* Tests d'intégration du vrai moteur Lua (script_engine.c) sans interface : bouchons pour send_serial, la fonction
 * d'écoute du port principal et get_shortcuts. Le thread principal fait tourner une GMainLoop pendant le script. */

#include <glib.h>
#include <glib/gstdio.h>
#include <locale.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "script_engine.h"
#include "macros_list.h"
#include "stubs.h"
#include "stub_macros.h"

static GThread *thread_principal;
static GMainLoop *boucle;
static GString *journal;
static gboolean fini;

static void
sur_journal(const char *texte, gpointer data)
{
	g_string_append(journal, texte);
}

static void
sur_fin(gpointer data)
{
	fini = TRUE;
	g_main_loop_quit(boucle);
}

static gboolean
sur_delai(gpointer data)
{
	g_main_loop_quit(boucle);
	return G_SOURCE_REMOVE;
}

/* Lance le script et fait tourner la boucle jusqu'à sa fin (TRUE) ou jusqu'au délai (FALSE). */
static gboolean
executer(const gchar *script, guint delai_ms)
{
	guint delai;

	g_string_truncate(journal, 0);
	fini = FALSE;
	boucle = g_main_loop_new(NULL, FALSE);
	delai = g_timeout_add(delai_ms, sur_delai, NULL);
	script_engine_run(script);
	g_main_loop_run(boucle);
	if(fini)
		g_source_remove(delai);
	g_main_loop_unref(boucle);
	return fini;
}

static guint
compter_descripteurs(void)
{
	GDir *d = g_dir_open("/proc/self/fd", 0, NULL);
	guint n = 0;

	g_assert_nonnull(d);
	while(g_dir_read_name(d) != NULL)
		n++;
	g_dir_close(d);
	return n;
}

/* ---- C1 : erreur avec des ports ouverts ------------------------------- */

static void
test_erreur_avec_ports_ouverts(void)
{
	guint avant = compter_descripteurs();

	stub_reset();
	g_assert_true(executer("assert(gtkterm.open(1, 'tcp_server', 0))\n"
	                       "assert(gtkterm.open(2, 'tcp_server', 0))\n"
	                       "error('boum')\n", 5000));
	g_assert_nonnull(strstr(journal->str, "boum"));
	g_assert_cmpuint(compter_descripteurs(), ==, avant);
}

/* ---- C10, C11, M27 : thread principal et ordre ------------------------ */

static macro_t macros_test[1];

static void
installer_macro_a(void)
{
	memset(macros_test, 0, sizeof(macros_test));
	macros_test[0].label = "A";
	macros_test[0].shortcut = "None";
	macros_test[0].action = "A";
	stub_set_macros(macros_test, 1);
}

static void
test_envois_sur_le_thread_principal_et_dans_l_ordre(void)
{
	GByteArray *octets;

	stub_reset();
	installer_macro_a();
	g_assert_true(executer("assert(gtkterm.send_macro('A'))\n"
	                       "assert(gtkterm.send(0, 'B'))\n", 5000));
	octets = stub_sent();
	g_assert_cmpuint(octets->len, ==, 2);
	g_assert_cmpmem(octets->data, octets->len, "AB", 2);
	g_assert_true(stub_send_thread() == thread_principal);
}

static void
test_get_macros_sur_le_thread_principal(void)
{
	stub_reset();
	installer_macro_a();
	g_assert_true(executer("local m = gtkterm.get_macros()\n"
	                       "gtkterm.log(#m .. ' ' .. m[1].label)\n", 5000));
	g_assert_nonnull(strstr(journal->str, "1 A"));
	g_assert_true(stub_get_shortcuts_thread() == thread_principal);
}

static void
test_macro_absente(void)
{
	stub_reset();
	installer_macro_a();
	g_assert_true(executer("local ok, err = gtkterm.send_macro('absente')\n"
	                       "gtkterm.log(tostring(ok) .. ' ' .. err)\n", 5000));
	g_assert_nonnull(strstr(journal->str, "false macro not found"));
}

static void
test_get_lists(void)
{
	macro_lists_free();
	macro_list_add("Mode", "Marche", "M");
	g_assert_true(executer("local l = gtkterm.get_lists()\n"
	                       "gtkterm.log(l[1].name .. ' ' .. l[1].entries[1].value)\n", 5000));
	g_assert_nonnull(strstr(journal->str, "Mode M"));
	macro_lists_free();
}

/* ---- M26 : wait_for binaire ------------------------------------------- */

static gboolean
injecter_binaire(gpointer data)
{
	stub_inject_rx("\x01\x00\x02OK", 5);
	return G_SOURCE_REMOVE;
}

static void
test_wait_for_binaire(void)
{
	stub_reset();
	g_timeout_add(200, injecter_binaire, NULL);
	g_assert_true(executer("local r = gtkterm.wait_for(0, '\\0\\2', 2000)\n"
	                       "gtkterm.log('longueur ' .. (r and #r or -1))\n", 5000));
	g_assert_nonnull(strstr(journal->str, "longueur 3"));
}

/* ---- C2 : pair TCP fermé sur un port Lua ------------------------------- */

static int ecoute_c2 = -1;

static gboolean
fermer_le_pair(gpointer data)
{
	int pair = accept(ecoute_c2, NULL, NULL);

	if(pair >= 0)
		close(pair);
	return G_SOURCE_REMOVE;
}

static void
test_pair_tcp_ferme(void)
{
	struct sockaddr_in adresse;
	socklen_t taille = sizeof(adresse);
	gchar *script;

	ecoute_c2 = socket(AF_INET, SOCK_STREAM, 0);
	memset(&adresse, 0, sizeof(adresse));
	adresse.sin_family = AF_INET;
	adresse.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	g_assert_cmpint(bind(ecoute_c2, (struct sockaddr *)&adresse, sizeof(adresse)), ==, 0);
	g_assert_cmpint(listen(ecoute_c2, 1), ==, 0);
	g_assert_cmpint(getsockname(ecoute_c2, (struct sockaddr *)&adresse, &taille), ==, 0);

	script = g_strdup_printf("assert(gtkterm.open(1, 'tcp_client', '127.0.0.1', %u))\n"
	                         "gtkterm.sleep(400)\n"
	                         "local r\n"
	                         "for i = 1, 3 do r = gtkterm.send(1, 'abc'); gtkterm.sleep(50) end\n"
	                         "gtkterm.log('envoi ' .. tostring(r))\n", ntohs(adresse.sin_port));
	g_timeout_add(150, fermer_le_pair, NULL);
	g_assert_true(executer(script, 5000));
	g_assert_nonnull(strstr(journal->str, "envoi false"));
	g_free(script);
	close(ecoute_c2);
}

/* ---- M30 : exécutions successives ------------------------------------- */

static void
test_executions_successives(void)
{
	g_assert_true(executer("gtkterm.log('un')\n", 5000));
	g_assert_true(executer("gtkterm.log('deux')\n", 5000));
	g_assert_false(script_engine_is_running());
}

/* ---- M9 : nombres Lua indépendants de la langue ----------------------- */

static void
test_nombres_lua_sous_locale_fr(void)
{
	gchar *ancienne = g_strdup(setlocale(LC_ALL, NULL));

	if(setlocale(LC_ALL, "fr_FR.UTF-8") == NULL)
	{
		g_free(ancienne);
		g_test_skip("locale fr_FR.UTF-8 indisponible");
		return;
	}
	g_assert_true(executer("gtkterm.log(tostring(3.5))\n", 5000));
	setlocale(LC_ALL, ancienne);
	g_free(ancienne);
	g_assert_nonnull(strstr(journal->str, "3.5"));
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	thread_principal = g_thread_self();
	journal = g_string_new(NULL);
	script_engine_setup();
	script_engine_set_log_func(sur_journal, NULL);
	script_engine_set_done_func(sur_fin, NULL);

	g_test_add_func("/script_engine/erreur-avec-ports-ouverts", test_erreur_avec_ports_ouverts);
	g_test_add_func("/script_engine/envois-sur-le-thread-principal-et-dans-l-ordre", test_envois_sur_le_thread_principal_et_dans_l_ordre);
	g_test_add_func("/script_engine/get-macros-sur-le-thread-principal", test_get_macros_sur_le_thread_principal);
	g_test_add_func("/script_engine/macro-absente", test_macro_absente);
	g_test_add_func("/script_engine/get-lists", test_get_lists);
	g_test_add_func("/script_engine/wait-for-binaire", test_wait_for_binaire);
	g_test_add_func("/script_engine/pair-tcp-ferme", test_pair_tcp_ferme);
	g_test_add_func("/script_engine/executions-successives", test_executions_successives);
	g_test_add_func("/script_engine/nombres-lua-sous-locale-fr", test_nombres_lua_sous_locale_fr);
	return g_test_run();
}
```

Dans `tests/meson.build`, dictionnaire `integration_tests`, ajouter :

```meson
	'test_script_engine' : {
		'sources' : files('../src/script_engine.c', 'stubs/stub_macros.c') + stub_journal + stub_display,
		'deps' : [gtk_deps, lua_dep],
		'link_args' : [],
	},
```

- [ ] **Step 3 : constater les échecs**

Run : `meson test -C build test_script_engine >/dev/null 2>&1; for t in erreur-avec-ports-ouverts envois-sur-le-thread-principal-et-dans-l-ordre get-macros-sur-le-thread-principal wait-for-binaire pair-tcp-ferme nombres-lua-sous-locale-fr; do ./build/tests/test_script_engine -p /script_engine/$t >/dev/null 2>&1; echo "$t rc=$?"; done`
Expected : code de sortie non nul pour chacun :
- `erreur-avec-ports-ouverts` : abandon par panique Lua ;
- `envois-…` : ordre « BA » et mauvais thread ;
- `get-macros-…` : mauvais thread ;
- `wait-for-binaire` : longueur 0 ;
- `pair-tcp-ferme` : SIGPIPE ;
- `nombres-lua-…` : « 3,5 ».

`macro-absente`, `get-lists` et `executions-successives` peuvent déjà passer : ils protègent contre les régressions.

- [ ] **Step 4 : corriger `script_engine.c`**

1. En-têtes : ajouter `#include <locale.h>` après `#include <errno.h>`.
2. Après la section « Logging helpers » (après `engine_log`), ajouter :

```c
/* ------------------------------------------------------------------ */
/* Exécution synchrone sur le thread principal                         */
/* ------------------------------------------------------------------ */

/* Le thread Lua ne touche jamais GTK ni l'état du thread principal (macros, listes, port principal) : il y fait exécuter
 * func et attend sa fin (C10, C11). La source est ajoutée au contexte par défaut, que seul le thread principal fait tourner. */
typedef struct {
    GSourceFunc func;
    gpointer    data;
    gboolean    done;
    GMutex      mutex;
    GCond       cond;
} MainCall;

static gboolean main_call_trampoline(gpointer user_data)
{
    MainCall *mc = user_data;
    mc->func(mc->data);
    g_mutex_lock(&mc->mutex);
    mc->done = TRUE;
    g_cond_signal(&mc->cond);
    g_mutex_unlock(&mc->mutex);
    return G_SOURCE_REMOVE;
}

static void run_on_main_sync(GSourceFunc func, gpointer data)
{
    MainCall mc;
    mc.func = func;
    mc.data = data;
    mc.done = FALSE;
    g_mutex_init(&mc.mutex);
    g_cond_init(&mc.cond);
    g_idle_add(main_call_trampoline, &mc);
    g_mutex_lock(&mc.mutex);
    while (!mc.done)
        g_cond_wait(&mc.cond, &mc.mutex);
    g_mutex_unlock(&mc.mutex);
    g_mutex_clear(&mc.mutex);
    g_cond_clear(&mc.cond);
}

/* Envoi sur le port principal, exécuté sur le thread principal. */
typedef struct {
    const char *data;
    gint        len;
    gint        written;
} MainSendData;

static gboolean main_send_cb(gpointer user_data)
{
    MainSendData *d = user_data;
    d->written = d->len > 0 ? send_serial((gchar *)d->data, d->len) : 0;
    return G_SOURCE_REMOVE;
}

static gint main_port_send(const char *data, gint len)
{
    MainSendData d = { data, len, 0 };
    run_on_main_sync(main_send_cb, &d);
    return d.written;
}

/* Écriture sur un port Lua : MSG_NOSIGNAL en TCP, un pair fermé renvoie EPIPE au lieu de tuer GTKTerm (C2). */
static ssize_t lua_port_write(lua_port_t *p, const void *data, size_t len)
{
    if (p->type == PORT_SERIAL)
        return write(p->fd, data, len);
    return send(p->fd, data, len, MSG_NOSIGNAL);
}
```

   `lua_port_t` est défini plus haut dans le fichier : l'ordre convient.

3. Remplacer `l_close` par une fonction C de fermeture, plus son appel Lua :

```c
/* Ferme un port Lua ; appelée par gtkterm.close et par le nettoyage de fin de script, sans passer par la pile Lua (C1). */
static void lua_port_close(int handle)
{
    if (handle < 1 || handle >= MAX_LUA_PORTS) return;

    lua_port_t *p = &ports[handle];
    if (!p->open) return;

    if (p->watch_in)  { g_source_remove(p->watch_in);  p->watch_in  = 0; }
    if (p->watch_err) { g_source_remove(p->watch_err); p->watch_err = 0; }
    if (p->chan_in)   { g_io_channel_unref(p->chan_in); p->chan_in   = NULL; }
    if (p->fd >= 0)   { close(p->fd); p->fd = -1; }
    if (p->listen_fd >= 0) { close(p->listen_fd); p->listen_fd = -1; }

    g_mutex_lock(&p->mutex);
    g_string_truncate(p->rx_buf, 0);
    g_mutex_unlock(&p->mutex);

    p->open = FALSE;
}

static int l_close(lua_State *L)
{
    lua_port_close((int)luaL_checkinteger(L, 1));
    return 0;
}
```

4. `l_send` : remplacer le bloc `if (handle == 0) { send_serial(...); lua_pushboolean(L, 1); return 1; }` par :

```c
    if (handle == 0) {
        lua_pushboolean(L, main_port_send(data, (gint)len) == (gint)len);
        return 1;
    }
```

   et remplacer `ssize_t written = write(p->fd, data, len);` par `ssize_t written = lua_port_write(p, data, len);`.

5. `l_send_hex` : remplacer le bloc d'envoi, de `if (handle == 0) {` jusqu'à `lua_pushboolean(L, 1);` inclus, par :

```c
    gboolean ok = FALSE;
    if (handle == 0) {
        ok = main_port_send(bin, (gint)bin_len) == (gint)bin_len;
    } else if (handle >= 1 && handle < MAX_LUA_PORTS) {
        lua_port_t *p = &ports[handle];
        if (p->open && p->fd >= 0)
            ok = lua_port_write(p, bin, bin_len) == (ssize_t)bin_len;
    }

    g_free(bin);
    lua_pushboolean(L, ok);
```

   en conservant le `return 1;` qui suit.

6. `l_wait_for` (M26) :
   - remplacer `const char *pat = luaL_checkstring(L, 2);` par `size_t pat_len; const char *pat = luaL_checklstring(L, 2, &pat_len);` ;
   - remplacer le bloc qui va de `char *found = g_strstr_len(...)` jusqu'à `return 1;` (fin du `if (found)`) par :

```c
        char *found = memmem(p->rx_buf->str, p->rx_buf->len, pat, pat_len);
        if (found) {
            /* Données binaires : recherche et résultat avec longueurs explicites, un octet nul n'arrête rien (M26). */
            size_t end = (size_t)(found - p->rx_buf->str) + pat_len;
            lua_pushlstring(L, p->rx_buf->str, end);
            g_string_erase(p->rx_buf, 0, (gssize)end);
            g_mutex_unlock(&p->mutex);
            return 1;
        }
```

   et corriger le commentaire d'en-tête `-> matched, data` en `-> data (jusqu'au motif inclus) ou nil`.

7. `l_get_lists` et `l_get_macros` : instantané pris sur le thread principal. Avant `l_get_lists`, ajouter :

```c
/* Instantanés des listes et des macros, pris sur le thread principal puis poussés vers Lua (C10). */
typedef struct {
    gchar     *name;
    GPtrArray *displays;
    GPtrArray *values;
} ListSnapshot;

static void list_snapshot_free(gpointer p)
{
    ListSnapshot *s = p;
    g_free(s->name);
    g_ptr_array_unref(s->displays);
    g_ptr_array_unref(s->values);
    g_free(s);
}

static gboolean main_snapshot_lists_cb(gpointer user_data)
{
    GPtrArray *out = user_data;
    gint n_lists = macro_list_count();
    for (int i = 0; i < n_lists; i++) {
        ListSnapshot *s = g_new0(ListSnapshot, 1);
        s->name = g_strdup(macro_list_name(i) ? macro_list_name(i) : "");
        s->displays = g_ptr_array_new_with_free_func(g_free);
        s->values = g_ptr_array_new_with_free_func(g_free);
        for (int e = 0; e < macro_list_entry_count(i); e++) {
            const gchar *disp = macro_list_entry_display(i, e);
            const gchar *val  = macro_list_entry_value(i, e);
            g_ptr_array_add(s->displays, g_strdup(disp ? disp : ""));
            g_ptr_array_add(s->values, g_strdup(val ? val : ""));
        }
        g_ptr_array_add(out, s);
    }
    return G_SOURCE_REMOVE;
}

typedef struct {
    gchar            *label;
    gint              n_args;
    macro_arg_info_t *infos;
} MacroSnapshot;

static void macro_snapshot_free(gpointer p)
{
    MacroSnapshot *s = p;
    g_free(s->label);
    macro_arg_infos_free(s->infos, s->n_args);
    g_free(s);
}

static gboolean main_snapshot_macros_cb(gpointer user_data)
{
    GPtrArray *out = user_data;
    gint count = 0;
    macro_t *macros = get_shortcuts(&count);
    for (int i = 0; macros != NULL && i < count; i++) {
        MacroSnapshot *s = g_new0(MacroSnapshot, 1);
        s->label = g_strdup(macros[i].label ? macros[i].label : "");
        s->infos = macros[i].action ? macro_get_arg_infos(macros[i].action, &s->n_args) : NULL;
        g_ptr_array_add(out, s);
    }
    return G_SOURCE_REMOVE;
}
```

   Puis remplacer le corps de `l_get_lists` par :

```c
    GPtrArray *lists = g_ptr_array_new_with_free_func(list_snapshot_free);
    run_on_main_sync(main_snapshot_lists_cb, lists);
    lua_newtable(L);
    for (guint i = 0; i < lists->len; i++) {
        ListSnapshot *s = g_ptr_array_index(lists, i);
        lua_newtable(L);
        lua_pushstring(L, s->name);
        lua_setfield(L, -2, "name");
        lua_newtable(L);
        for (guint e = 0; e < s->displays->len; e++) {
            lua_newtable(L);
            lua_pushstring(L, g_ptr_array_index(s->displays, e));
            lua_setfield(L, -2, "display");
            lua_pushstring(L, g_ptr_array_index(s->values, e));
            lua_setfield(L, -2, "value");
            lua_rawseti(L, -2, e + 1);
        }
        lua_setfield(L, -2, "entries");
        lua_rawseti(L, -2, i + 1);
    }
    g_ptr_array_unref(lists);
    return 1;
```

   et le corps de `l_get_macros` par :

```c
    GPtrArray *snap = g_ptr_array_new_with_free_func(macro_snapshot_free);
    run_on_main_sync(main_snapshot_macros_cb, snap);
    lua_newtable(L);
    for (guint i = 0; i < snap->len; i++) {
        MacroSnapshot *s = g_ptr_array_index(snap, i);
        lua_newtable(L);
        lua_pushinteger(L, i);
        lua_setfield(L, -2, "index");
        lua_pushstring(L, s->label);
        lua_setfield(L, -2, "label");
        lua_pushinteger(L, s->n_args);
        lua_setfield(L, -2, "n_args");
        lua_newtable(L);
        for (int a = 0; a < s->n_args; a++) {
            lua_newtable(L);
            lua_pushstring(L, s->infos[a].type      ? s->infos[a].type      : "s");
            lua_setfield(L, -2, "type");
            lua_pushstring(L, s->infos[a].label     ? s->infos[a].label     : "");
            lua_setfield(L, -2, "label");
            lua_pushstring(L, s->infos[a].list_name ? s->infos[a].list_name : "");
            lua_setfield(L, -2, "list");
            lua_rawseti(L, -2, a + 1);
        }
        lua_setfield(L, -2, "args");
        lua_rawseti(L, -2, i + 1);
    }
    g_ptr_array_unref(snap);
    return 1;
```

8. `send_macro` (C10, M27) : remplacer `SendMacroData`, `send_macro_idle` et `l_send_macro` par :

```c
typedef struct {
    int          index;     /* -1 : chercher par libellé */
    const char  *label;
    int          n_args;
    gchar      **args;
    gboolean     found;
    gint         expected;
    gint         written;
} MainMacroData;

/* Résolution, mise en forme et envoi sur le thread principal : l'ordre avec gtkterm.send est respecté (M27). */
static gboolean main_send_macro_cb(gpointer user_data)
{
    MainMacroData *d = user_data;
    gint count = 0;
    macro_t *macros = get_shortcuts(&count);
    int idx = d->index;

    if (idx < 0 && d->label && macros) {
        for (int i = 0; i < count; i++)
            if (macros[i].label && strcmp(macros[i].label, d->label) == 0) { idx = i; break; }
    }
    d->found = (macros != NULL && idx >= 0 && idx < count && macros[idx].action != NULL);
    if (!d->found)
        return G_SOURCE_REMOVE;

    gchar *formatted = format_action_with_args(macros[idx].action, (const gchar **)d->args, d->n_args);
    GByteArray *buf = parse_macro_string(formatted);
    g_free(formatted);
    d->expected = (gint)buf->len;
    d->written = buf->len > 0 ? send_serial((gchar *)buf->data, (gint)buf->len) : 0;
    g_byte_array_free(buf, TRUE);
    return G_SOURCE_REMOVE;
}

static int l_send_macro(lua_State *L)
{
    MainMacroData d = { -1, NULL, 0, NULL, FALSE, 0, 0 };

    if (lua_type(L, 1) == LUA_TNUMBER)
        d.index = (int)lua_tointeger(L, 1);
    else
        d.label = luaL_checkstring(L, 1);

    d.n_args = lua_gettop(L) - 1;
    d.args = g_new0(gchar *, d.n_args + 1);
    for (int i = 0; i < d.n_args; i++) {
        const char *s = lua_tostring(L, i + 2);
        d.args[i] = g_strdup(s ? s : "");
    }

    run_on_main_sync(main_send_macro_cb, &d);
    g_strfreev(d.args);

    if (!d.found) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "macro not found");
        return 2;
    }
    lua_pushboolean(L, d.written == d.expected);
    return 1;
}
```

   Note : un index numérique négatif reste « introuvable » comme avant, puisque `idx < 0` sans libellé.

9. Fin de script et thread (C1, M30, M9) :
   - au début de `lua_thread_func`, après `char *script = user_data;`, ajouter :

```c
    /* Nombres Lua avec un point quelle que soit la langue (M9) : locale « C » propre à ce thread. */
    locale_t c_locale = newlocale(LC_ALL_MASK, "C", (locale_t)0);
    uselocale(c_locale);
```

   - remplacer la boucle de fermeture des ports :

```c
    for (int i = 1; i < MAX_LUA_PORTS; i++) {
        if (ports[i].open) {
            lua_pushinteger(L, i);
            l_close(L);
            lua_pop(L, 0);
        }
    }
```

     par :

```c
    for (int i = 1; i < MAX_LUA_PORTS; i++)
        lua_port_close(i);
```

   - dans le bloc final, supprimer la ligne `lua_thread  = NULL;`, qui sera remise à NULL par le rappel de fin après jointure, et ajouter avant `g_idle_add(done_idle_cb, NULL);` :

```c
    uselocale(LC_GLOBAL_LOCALE);
    freelocale(c_locale);
```

   - remplacer `done_idle_cb` par :

```c
static gboolean done_idle_cb(gpointer user_data)
{
    GThread *finished;

    /* Le thread Lua a fini : on le joint ici, sur le thread principal, jamais pendant qu'il peut attendre ce thread (M30). */
    g_mutex_lock(&engine_mutex);
    finished = lua_thread;
    lua_thread = NULL;
    g_mutex_unlock(&engine_mutex);
    if (finished)
        g_thread_join(finished);

    if (done_func)
        done_func(done_data);
    return G_SOURCE_REMOVE;
}
```

10. Review Focus 1 : Run `grep -rn "g_thread_join\|script_engine_stop\|script_engine_is_running" src/ | grep -v "^src/script_engine.c"`.
    Expected : aucun `g_thread_join` hors de `script_engine.c`. Si un appel de fermeture d'application **attend** la fin du script (boucle sur `script_engine_is_running`), le signaler dans le commit et la revue : le thread Lua peut attendre le thread principal dans `run_on_main_sync`.

- [ ] **Step 5 : vérifier**

Run : `ninja -C build 2>&1 | grep -E "error|warning: .*script_engine"; meson test -C build --suite unit 2>&1 | grep -E "test_script_engine|^(Ok|Fail)"`
Expected : aucune erreur ; `test_script_engine OK` (9 sous-tests) ; `Fail: 0`.

- [ ] **Step 6 : commit**

```bash
git add src/script_engine.c tests/stubs/stub_macros.c tests/stubs/stub_macros.h tests/test_script_engine.c tests/meson.build
git commit -m "lua : ports fermés en C en fin de script (C1), MSG_NOSIGNAL (C2), port principal, macros et listes sur le thread principal (C10, C11, M27), wait_for binaire (M26), thread joint (M30), nombres sous locale C (M9)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9 : exemples Lua et `SKILL.md` — M29

**Files :**
- Modify : `scripts/10_firmware_update.lua`, `scripts/08_stress_loop.lua`, `scripts/09_multi_port.lua`, `.claude/skills/lua-script/SKILL.md`, `tests/test_script_engine.c`, `tests/meson.build`

**Interfaces :**
- Consumes : harnais `executer` et dépendances de `test_script_engine` (Task 8) ; `G_TEST_SRCDIR` (Task 2).
- Produces : rien pour les autres tâches.

- [ ] **Step 1 : écrire le test de l'exemple 10 contre un équipement simulé**

Dans `tests/test_script_engine.c`, ajouter `#include <pty.h>`, `#include <stdio.h>` et `#include <stdlib.h>` en tête, puis avant `main` :

```c
/* ---- M29 : l'exemple 10 contre un équipement simulé sur paire pty ---- */

static void
repondre(int maitre, const gchar *ligne)
{
	gchar *reponse = NULL;
	gint numero;
	gchar hex[256];

	if(g_strcmp0(ligne, "VERSION") == 0)
		reponse = g_strdup("VER: 1.2.3\r\n");
	else if(g_strcmp0(ligne, "BOOTLOADER") == 0)
		reponse = g_strdup("READY\r\n");
	else if(sscanf(ligne, "PAGE %d %255s", &numero, hex) == 2)
	{
		guint somme = 0;

		for(gsize i = 0; hex[i] != '\0' && hex[i + 1] != '\0'; i += 2)
		{
			gchar octet[3] = { hex[i], hex[i + 1], '\0' };
			somme += (guint)strtoul(octet, NULL, 16);
		}
		reponse = g_strdup_printf("ACK %02X\r\n", somme % 256);
	}
	else if(g_strcmp0(ligne, "COMMIT") == 0 || g_strcmp0(ligne, "ABORT") == 0)
		reponse = g_strdup("OK\r\n");
	if(reponse != NULL && write(maitre, reponse, strlen(reponse)) < 0)
		g_printerr("ecriture du repondeur impossible\n");
	g_free(reponse);
}

static gpointer
repondeur(gpointer data)
{
	int maitre = GPOINTER_TO_INT(data);
	GString *ligne = g_string_new(NULL);
	gchar c;

	while(read(maitre, &c, 1) == 1)
	{
		if(c == '\n')
		{
			repondre(maitre, ligne->str);
			g_string_truncate(ligne, 0);
		}
		else if(c != '\r')
			g_string_append_c(ligne, c);
	}
	g_string_free(ligne, TRUE);
	return NULL;
}

static void
test_exemple_10_mise_a_jour(void)
{
	gchar *chemin = g_test_build_filename(G_TEST_DIST, "..", "scripts", "10_firmware_update.lua", NULL);
	gchar *script = NULL, *adapte;
	gchar **morceaux;
	int maitre, esclave;
	GThread *th;

	if(openpty(&maitre, &esclave, NULL, NULL, NULL) != 0)
	{
		g_test_skip("openpty indisponible");
		g_free(chemin);
		return;
	}
	g_assert_true(g_file_get_contents(chemin, &script, NULL, NULL));
	morceaux = g_strsplit(script, "/dev/ttyUSB0", -1);
	adapte = g_strjoinv(ttyname(esclave), morceaux);
	th = g_thread_new("repondeur", repondeur, GINT_TO_POINTER(maitre));

	stub_reset();
	g_assert_true(executer(adapte, 15000));
	g_assert_nonnull(strstr(journal->str, "Version courante : 1.2.3"));
	g_assert_null(strstr(journal->str, "MISMATCH"));
	g_assert_nonnull(strstr(journal->str, "Mise à jour réussie"));

	/* Fermer l'esclave d'abord : le read() du répondeur sur le maître renvoie alors EIO et le thread se termine. */
	close(esclave);
	g_thread_join(th);
	close(maitre);
	g_strfreev(morceaux);
	g_free(adapte);
	g_free(script);
	g_free(chemin);
}
```

Ajouter `g_test_add_func("/script_engine/exemple-10-mise-a-jour", test_exemple_10_mise_a_jour);` dans `main`. Dans `tests/meson.build`, entrée `test_script_engine`, remplacer `'deps' : [gtk_deps, lua_dep],` par `'deps' : [gtk_deps, lua_dep, util_dep],`.

Note : le script Lua ouvre l'esclave par son nom et le ferme à la fin. Le test ferme ensuite son propre descripteur esclave, attend le répondeur, puis ferme le maître.

- [ ] **Step 2 : constater l'échec**

Run : `meson test -C build test_script_engine >/dev/null 2>&1; ./build/tests/test_script_engine -p /script_engine/exemple-10-mise-a-jour 2>&1 | tail -3`
Expected : échec. Le journal ne contient pas « Version courante : 1.2.3 », car le script plante sur la concaténation d'un `nil`.

- [ ] **Step 3 : corriger les exemples**

`scripts/10_firmware_update.lua` :
- dans `send_cmd`, remplacer :

```lua
        local r = gtkterm.wait_for(1, expect, timeout_ms or 2000)
        if r then
            return r
        end
```

  par :

```lua
        local r = gtkterm.wait_for(1, expect, timeout_ms or 2000)
        if r then
            -- wait_for s'arrête juste après le motif : lire aussi la fin de la ligne de réponse
            return r .. (gtkterm.wait_for(1, "\n", 500) or "")
        end
```

- remplacer `gtkterm.log("Version courante : " .. ver:match("VER:([^\r\n]+)") or "?")` par :

```lua
local version = ver:match("VER:%s*([^\r\n]+)")
gtkterm.log("Version courante : " .. (version or "?"))
```

- remplacer `local remote_crc = ack:match("ACK%s+([%x]+)")` par `local remote_crc = ack:match("ACK%s+(%x+)")`.

`scripts/08_stress_loop.lua` : remplacer `local TIMEOUT_MS = 10      -- timeout echo par cycle` par `local TIMEOUT_MS = 500       -- timeout écho par cycle (10 ms ne laissait pas le temps à l'écho)`.

`scripts/09_multi_port.lua` : remplacer `    -- Port 0 → Port 2 : ce que l'utilisateur tape dans GTKTerm part en TCP` par `    -- Port 0 → Port 2 : ce qui est reçu sur le port principal part en TCP`.

`.claude/skills/lua-script/SKILL.md`, table « Engine API » :
- `| \`log(...)\` | – | logs any value(s) to the GtkTerm log view |` devient `| \`log(value)\` | – | logs one value (converted with tostring) to the GtkTerm script console |` ;
- `| \`send(handle, str)\` | ok, err | send a string |` devient `| \`send(handle, str)\` | boolean | send a string; true if fully written (handle 0 = main port) |` ;
- `| \`send_hex(handle, "DEADBEEF")\` | ok | send raw bytes from a hex string |` devient `| \`send_hex(handle, "DEADBEEF")\` | boolean | send raw bytes from a hex string; true if fully written |` ;
- `| \`open(handle, "tcp_server", host, port)\` | ok, err | |` devient `| \`open(handle, "tcp_server", port)\` | ok, err | listen on all interfaces; the first client is accepted |` ;
- `| \`clear()\` | – | clear the terminal/buffer |` devient `| \`clear(handle)\` | – | empty the receive buffer of handle (not the terminal) |` ;
- `| \`save_csv(file, col1, col2, ...)\` | – | append a CSV row; first call with header strings |` devient `| \`save_csv(file, col1, col2, ...)\` | ok, err | append a CSV row; first call with header strings |` ;
- `| \`send_macro(index_or_label, ...args)\` | ok, err | fire a configured macro by index or label |` devient `| \`send_macro(index_or_label, ...args)\` | ok, err | send a configured macro (0-based index or label); true once actually sent |`.

- [ ] **Step 4 : vérifier**

Run : `meson test -C build 2>&1 | grep -E "test_script_engine|lua-syntax|^(Ok|Fail)"`
Expected : `test_script_engine OK` (10 sous-tests), `lua-syntax OK`, `Fail: 0`.

- [ ] **Step 5 : commit**

```bash
git add scripts/08_stress_loop.lua scripts/09_multi_port.lua scripts/10_firmware_update.lua .claude/skills/lua-script/SKILL.md tests/test_script_engine.c tests/meson.build
git commit -m "lua : exemples 08, 09 et 10 et SKILL.md alignés sur l'API réelle, exemple 10 testé contre un équipement simulé (M29)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

Les tâches 10 à 16 couvrent des chemins graphiques sans test automatique dans ce sous-projet. Chacune constate le défaut statiquement, corrige, construit, vérifie, relance `meson test` et cite dans le commit l'essai manuel qui prouve le correctif. Chaque bloc « Remplacer : » est copié du code de `ad0958c` et ne doit y apparaître qu'une fois. Si une tâche précédente de ce plan a modifié les lignes voisines au point qu'un bloc ne correspond plus, appliquer la même intention au code en place et consigner l'écart. Les numéros de ligne sont indicatifs. L'ordre 10 → 16 est imposé : `interface.c` est partagé par les tâches 10, 11 et 14, et `files.c` par les tâches 11 et 16.

### Task 10 : chaînes de format non maîtrisées (M38)

**Files :**
- Modify : `src/interface.c:1056-1081` (`show_message`, remplacée en entier)
- Modify : `src/search.c:71-76` (`search_callback`)

**Interfaces :**
- Consumes : rien de nouveau.
- Produces : rien (signatures inchangées).

Inventaire : une compilation de tous les `src/*.c` avec `-Wformat=2 -Wformat-nonliteral -Wformat-security` (drapeaux de `build/compile_commands.json`) ne signale que trois appels à format non littéral hors `macros_format.c` : `interface.c:1066`, `interface.c:1074` (les deux branches de `show_message`) et `search.c:76`. Les avertissements de `macros_format.c:231-294` sont voulus (format printf construit puis validé par le module, hors périmètre) ; les appels `_()` sont reconnus comme littéraux (attribut `format_arg`). Aucun `g_printerr`/`printf`/`luaL_error`/`lua_pushfstring`/`g_warning` ne reçoit de format variable.

Essai manuel : **EM-10**.

- [ ] **Step 1 : constater le défaut**

Run :
```bash
grep -n "message, NULL);" src/interface.c
grep -n "error->message,$" src/search.c
```
Expected :
```
1066:		                                     message, NULL);
1074:		                                     message, NULL);
75:													   error->message,
```
Le texte affiché sert de format à `gtk_message_dialog_new` : un `%s`/`%n` dans un nom de port, un chemin ou un `strerror` provoque un comportement indéfini.

- [ ] **Step 2 : `show_message`**

Dans `src/interface.c`, `show_message` (deux branches au texte identique hormis le type de message : les blocs incluent la ligne `GTK_MESSAGE_*` pour être uniques).

Remplacer :
```c
		                                     GTK_MESSAGE_ERROR,
		                                     GTK_BUTTONS_OK,
		                                     message, NULL);
```
par :
```c
		                                     GTK_MESSAGE_ERROR,
		                                     GTK_BUTTONS_OK,
		                                     "%s", message);	/* le message n'est jamais un format (M38) */
```

Remplacer :
```c
		                                     GTK_MESSAGE_WARNING,
		                                     GTK_BUTTONS_OK,
		                                     message, NULL);
```
par :
```c
		                                     GTK_MESSAGE_WARNING,
		                                     GTK_BUTTONS_OK,
		                                     "%s", message);	/* le message n'est jamais un format (M38) */
```

- [ ] **Step 3 : `search.c`**

Dans `src/search.c`, `search_callback`.

Remplacer :
```c
													   GTK_BUTTONS_OK,
													   error->message,
													   NULL);
```
par :
```c
													   GTK_BUTTONS_OK,
													   "%s",
													   error->message);
```

- [ ] **Step 4 : construire**

Run : `ninja -C build 2>&1 | grep -E "error|(interface|search)\.c.*warning"`
Expected : seulement les deux `cast-function-type` déjà présents d'`interface.c` (`control_signals_read` vers l'ex-ligne 865 et `pop_message` vers l'ex-ligne 1134 ; le second disparaît à la tâche 11), aucune erreur, rien pour `search.c`.

- [ ] **Step 5 : vérifier statiquement**

Run :
```bash
python3 - <<'EOF'
import json, shlex, subprocess
for e in json.load(open('build/compile_commands.json')):
    f = e['file']
    if not f.startswith('../src/') or f.endswith('macros_format.c'):
        continue
    args, skip = [], False
    for a in shlex.split(e['command']):
        if skip: skip = False; continue
        if a in ('-o', '-MF', '-MQ'): skip = True; continue
        if a == '-MD': continue
        args.append(a)
    r = subprocess.run(args + ['-fsyntax-only', '-Wformat=2', '-Wformat-nonliteral'], cwd=e['directory'], capture_output=True, text=True)
    for l in r.stderr.splitlines():
        if 'format-nonliteral' in l or 'format-security' in l:
            print(l)
print('fin')
EOF
```
Expected : seulement `fin` (plus aucun format non littéral hors `macros_format.c`).

- [ ] **Step 6 : non-régression**

Run : `meson test -C build`
Expected : `Fail: 0`.

- [ ] **Step 7 : commiter**

```bash
git add src/interface.c src/search.c
git commit -m "interface, search : le texte des boîtes de message passe en argument d'un format \"%s\" (M38)

show_message et l'erreur d'expression régulière de la recherche passaient le texte comme chaîne de format : un % dans un nom de port,
un chemin ou un message d'erreur provoquait un comportement indéfini. Ce sont les seuls formats non littéraux de src/ hors macros_format.c.

Preuve : essai manuel EM-10.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 11 : barre d'état (M40) et ouverture du journal (M39)

**Files :**
- Modify : `src/interface.c:161` (prototype de `pop_message`), `src/interface.c:1130-1142` (`Put_temp_message`, `pop_message`)
- Modify : `src/files.c:58` (nouvelle variable statique), `src/files.c:122` (`send_raw_file`), `src/files.c:288` (`close_all`)
- Modify : `src/logging.c:65` (`OpenLogFile`), `src/logging.c:131-140` (`logging_clear`)

**Interfaces :**
- Consumes : `StatusBar`, `id` (`interface.h:46-47`) ; `toggle_logging_sensitivity(gboolean)`, `toggle_logging_pause_resume(gboolean)` (`interface.h:42-43`, déjà appelées dans `logging.c`).
- Produces : `gboolean pop_message(gpointer data)` remplace `gboolean pop_message(void)` (interne à `interface.c`, signature `GSourceFunc`, plus de transtypage) ; `static guint transfer_msg_id` dans `files.c`.

Essais manuels : **EM-12** (M40), **EM-11** (M39), et EM-14 pour la fin d'envoi de fichier.

#### 11.1 — M40

- [ ] **Step 1 : constater le défaut**

Run : `grep -n "gtk_statusbar_pop\|gtk_statusbar_remove\|pop_message" src/interface.c src/files.c`
Expected :
```
src/files.c:288:	gtk_statusbar_pop(GTK_STATUSBAR(StatusBar), id);
src/interface.c:161:gboolean pop_message(void);
src/interface.c:1023:	gtk_statusbar_pop(GTK_STATUSBAR(StatusBar), id);
src/interface.c:1134:	g_timeout_add(time, (GSourceFunc)pop_message, NULL);
src/interface.c:1137:gboolean pop_message(void)
src/interface.c:1139:	gtk_statusbar_pop(GTK_STATUSBAR(StatusBar), id);
```
Aucun `gtk_statusbar_remove` : le délai d'un message temporaire retire le sommet de la pile, quel qu'il soit. Si `Set_status_message` (reconnexion, changement de port) a remplacé le sommet entre-temps, c'est le nouveau nom de port qui est retiré.

- [ ] **Step 2 : prototype de `pop_message`**

Dans `src/interface.c`, remplacer :
```c
gboolean pop_message(void);
```
par :
```c
gboolean pop_message(gpointer data);
```

- [ ] **Step 3 : retirer le message temporaire par son identifiant**

Remplacer :
```c
void Put_temp_message(const gchar *text, gint time)
{
	/* time in ms */
	gtk_statusbar_push(GTK_STATUSBAR(StatusBar), id, text);
	g_timeout_add(time, (GSourceFunc)pop_message, NULL);
}

gboolean pop_message(void)
{
	gtk_statusbar_pop(GTK_STATUSBAR(StatusBar), id);

	return FALSE;
}
```
par :
```c
void Put_temp_message(const gchar *text, gint time)
{
	guint msg_id;

	/* time in ms */
	msg_id = gtk_statusbar_push(GTK_STATUSBAR(StatusBar), id, text);
	g_timeout_add(time, pop_message, GUINT_TO_POINTER(msg_id));
}

/* Retire ce message temporaire-là (M40), et non le sommet de la pile, qui peut être entre-temps le nom du port poussé par
   Set_status_message. Sans effet si le message n'est plus dans la pile. */
gboolean pop_message(gpointer data)
{
	gtk_statusbar_remove(GTK_STATUSBAR(StatusBar), id, GPOINTER_TO_UINT(data));

	return FALSE;
}
```

- [ ] **Step 4 : message de transfert de fichier retiré par identifiant (voir « Écarts à la spec », point 2)**

Dans `src/files.c`, remplacer :
```c
gchar *str = NULL;
FILE *Fic;
```
par :
```c
gchar *str = NULL;
FILE *Fic;
static guint transfer_msg_id = 0;	/* message « transfert en cours » de la barre d'état, 0 si aucun */
```

Remplacer :
```c
			gtk_statusbar_push(GTK_STATUSBAR(StatusBar), id, msg);
```
par :
```c
			transfer_msg_id = gtk_statusbar_push(GTK_STATUSBAR(StatusBar), id, msg);
```

Remplacer :
```c
	waiting_for_timer = FALSE;
	gtk_statusbar_pop(GTK_STATUSBAR(StatusBar), id);
	close(Fichier);
```
par :
```c
	waiting_for_timer = FALSE;
	/* Retirer le message du transfert lui-même : un message temporaire (macro en polling) peut être au sommet de la pile */
	if(transfer_msg_id != 0)
	{
		gtk_statusbar_remove(GTK_STATUSBAR(StatusBar), id, transfer_msg_id);
		transfer_msg_id = 0;
	}
	close(Fichier);
```

- [ ] **Step 5 : construire**

Run : `ninja -C build 2>&1 | grep -E "error|(interface|files)\.c.*warning"`
Expected : pour `interface.c`, seulement le `cast-function-type` de `control_signals_read` (ex-ligne 865) : celui de `pop_message` a disparu ; pour `files.c`, seulement les avertissements déjà présents (`cast-function-type` de `(GIOFunc)ecriture`, traité à la tâche 16, et `sign-compare` de `write_ascii_file`) ; aucune erreur.

- [ ] **Step 6 : vérifier statiquement**

Run : `grep -n "gtk_statusbar_pop\|gtk_statusbar_remove\|pop_message" src/interface.c src/files.c`
Expected : `files.c` : un seul `gtk_statusbar_remove(... transfer_msg_id)` et plus de `gtk_statusbar_pop` ; `interface.c` : le prototype `gboolean pop_message(gpointer data);`, `g_timeout_add(time, pop_message, GUINT_TO_POINTER(msg_id));`, la définition `gboolean pop_message(gpointer data)`, son `gtk_statusbar_remove(...)`, et un seul `gtk_statusbar_pop` restant, celui de `Set_status_message` (ex-ligne 1023).

- [ ] **Step 7 : non-régression**

Run : `meson test -C build`
Expected : `Fail: 0`.

- [ ] **Step 8 : commiter**

```bash
git add src/interface.c src/files.c
git commit -m "interface, files : les messages temporaires et de transfert sont retirés de la barre d'état par leur identifiant (M40)

Put_temp_message retirait le sommet de la pile à l'échéance : si le nom du port avait été remplacé entre-temps, c'est lui qui disparaissait.
Le message de transfert de fichier est retiré de la même façon, sinon un message temporaire arrivé pendant l'envoi le laisserait affiché.
pop_message prend la signature GSourceFunc (un avertissement cast-function-type en moins).

Preuve : essais manuels EM-12 et EM-14.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

#### 11.2 — M39

- [ ] **Step 9 : constater le défaut**

Run :
```bash
grep -n 'fopen(LoggingFileName\|set_do_overwrite_confirmation' src/logging.c
sed -n '/^void logging_clear/,/^}/p' src/logging.c | grep -c "toggle_logging"
```
Expected : `65:	LoggingFile = fopen(LoggingFileName, "a");` et la ligne `gtk_file_chooser_set_do_overwrite_confirmation(..., TRUE);` : le sélecteur demande « remplacer ? » mais le fichier est ouvert en ajout. Puis `0` : après un `freopen` en échec, `logging_clear` laisse `LoggingFile` à NULL sans remettre les menus (« Vers un fichier... » reste grisé, « Pause », « Arrêter », « Effacer » restent actifs et sans effet).

- [ ] **Step 10 : ouvrir en écriture quand l'utilisateur a choisi d'écraser**

Dans `src/logging.c`, `OpenLogFile`, remplacer :
```c
	LoggingFile = fopen(LoggingFileName, "a");
```
par :
```c
	/* Le sélecteur a fait confirmer le remplacement d'un fichier existant (set_do_overwrite_confirmation) : écraser, pas ajouter (M39) */
	LoggingFile = fopen(LoggingFileName, "w");
```

- [ ] **Step 11 : remettre les menus après un échec de réouverture**

Dans `logging_clear`, remplacer :
```c
		g_free(LoggingFileName);
		LoggingFileName = NULL;
		Logging = FALSE;
	}
}
```
par :
```c
		g_free(LoggingFileName);
		LoggingFileName = NULL;
		Logging = FALSE;
		/* Plus de journal ouvert : menus remis dans leur état initial, sinon on ne pourrait plus en relancer un (M39) */
		toggle_logging_sensitivity(Logging);
		toggle_logging_pause_resume(Logging);
	}
}
```

- [ ] **Step 12 : construire**

Run : `ninja -C build 2>&1 | grep -E "error|logging\.c.*warning"`
Expected : aucune sortie.

- [ ] **Step 13 : vérifier statiquement**

Run :
```bash
grep -n 'fopen(LoggingFileName' src/logging.c
sed -n '/^void logging_clear/,/^}/p' src/logging.c | grep -c "toggle_logging"
```
Expected : `LoggingFile = fopen(LoggingFileName, "w");` ; puis `2`.

- [ ] **Step 14 : non-régression**

Run : `meson test -C build`
Expected : `Fail: 0`.

- [ ] **Step 15 : commiter**

```bash
git add src/logging.c
git commit -m "logging : écraser le fichier quand l'utilisateur l'a confirmé, menus remis après un échec de réouverture (M39)

Le sélecteur demandait « remplacer ? » puis ouvrait le fichier en ajout. Après un échec de freopen dans « Effacer », le journal était fermé
mais les menus restaient dans l'état « journal actif », sans moyen d'en relancer un.

Preuve : essai manuel EM-11.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 12 : polling et reconstruction du panneau de macros (C9)

**Files :**
- Modify : `src/macro_panel.c:565-580` (`rebuild_macro_buttons`, juste après le `return` initial)

**Interfaces :**
- Consumes : `macro_button_table`, `macro_polling_table` (statiques, créées par `create_macro_panel`, `macro_panel.c:1024-1025`, avec `free_polling_args` comme destructeur de valeur) ; `restore_macro_polling` (`macro_panel.c:248`, ne restaure rien si la macro n'a pas de bouton).
- Produces : rien de nouveau. Après l'appel, les deux tables ne contiennent que des boutons vivants ; aucun polling ne tourne.

Essais manuels : **EM-1**, **EM-2** (sous `build-asan`).

- [ ] **Step 1 : constater le défaut**

Run :
```bash
grep -c "g_hash_table_remove_all" src/macro_panel.c
sed -n '/^void rebuild_macro_buttons/,/^}/p' src/macro_panel.c | grep -n "gtk_notebook_remove_page\|macro_button_table\|macro_polling_table\|restore_macro_polling"
```
Expected : `0` ; puis seulement `gtk_notebook_remove_page` (destruction des pages et de leurs boutons), les `g_hash_table_insert(macro_button_table, ...)` et `restore_macro_polling` : rien ne vide les tables. Les boutons d'un onglet masqué restent dans `macro_button_table`, et `polling_blink_callback` (`macro_panel.c:281`) appelle `gtk_widget_get_style_context(ps->button)` sur un bouton détruit ; après chargement d'un autre `.ini`, une entrée `running` de `macro_polling_table` continue d'envoyer la macro de même indice du nouveau fichier.

- [ ] **Step 2 : vider les tables avant de détruire les pages**

Dans `src/macro_panel.c`, `rebuild_macro_buttons`.

Remplacer :
```c
	if (macro_tab_flowbox == NULL || macro_notebook == NULL)
		return;

	/* Supprimer tous les boutons-onglets et pages existants */
```
par :
```c
	if (macro_tab_flowbox == NULL || macro_notebook == NULL)
		return;

	/* Les boutons vont être détruits : oublier leurs références et arrêter tout polling (C9). Sinon le clignotement toucherait des boutons
	   libérés, et après le chargement d'un autre .ini l'ancien polling enverrait la macro de même indice du nouveau fichier.
	   Le polling est restauré en fin de fonction, depuis macro_t, pour les seules macros qui ont encore un bouton. */
	g_hash_table_remove_all(macro_button_table);
	g_hash_table_remove_all(macro_polling_table);

	/* Supprimer tous les boutons-onglets et pages existants */
```

(Le `return` initial reste avant : `create_macro_panel` crée les deux tables avant `macro_tab_flowbox`, donc les tables existent dès que le panneau existe. La boucle finale « Restore polling state from macro_t » est déjà correcte : `restore_macro_polling` sort sans rien faire si la macro n'a pas de bouton, par exemple dans un onglet masqué, et garde alors `polling_enabled` dans `macro_t` pour le réaffichage.)

- [ ] **Step 3 : construire**

Run : `ninja -C build 2>&1 | grep -E "error|macro_panel\.c.*warning"`
Expected : aucune sortie.

- [ ] **Step 4 : vérifier statiquement**

Run :
```bash
sed -n '/^void rebuild_macro_buttons/,/^}/p' src/macro_panel.c | grep -n "g_hash_table_remove_all\|gtk_notebook_remove_page"
```
Expected : les deux lignes `g_hash_table_remove_all(macro_button_table);` et `g_hash_table_remove_all(macro_polling_table);` apparaissent **avant** la ligne `gtk_notebook_remove_page`.

- [ ] **Step 5 : non-régression**

Run : `meson test -C build`
Expected : `Fail: 0`.

- [ ] **Step 6 : commiter**

```bash
git add src/macro_panel.c
git commit -m "macro_panel : rebuild_macro_buttons vide les tables de boutons et de polling avant de détruire les pages (C9)

Les tables gardaient des pointeurs vers les boutons détruits (accès après libération par le clignotement du polling quand on masque un
onglet), et un polling lancé continuait après le chargement d'un autre fichier .ini en envoyant la macro de même indice du nouveau fichier.
Le polling n'est restauré, arrêté, que pour les macros qui ont encore un bouton.

Preuve : essais manuels EM-1 et EM-2 (sous build-asan).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 13 : dialogue Port unique, modal et transitoire (C12)

**Files :**
- Modify : `src/term_config.c:646-681` (`Config_Port_Fenetre` : déclarations, début de fonction, création du dialogue)

**Interfaces :**
- Consumes : `Fenetre` (`interface.c:99`, déclarée `extern` dans `interface.h:45`, déjà utilisée dans `term_config.c:2141`) ; `Lis_Config(GtkWidget *bouton, GtkWidget **Combos)` (inchangée).
- Produces : rien de public. Le tableau `Combos` n'est plus statique : il est alloué par dialogue et libéré avec lui (`g_object_set_data_full`) ; un pointeur faible `port_dialog` désigne le dialogue ouvert.

Essai manuel : **EM-7**.

- [ ] **Step 1 : constater le défaut**

Run :
```bash
grep -n "static GtkWidget \*Combos" src/term_config.c
sed -n '/^void Config_Port_Fenetre/,/^}/p' src/term_config.c | grep -c "gtk_window_set_modal\|gtk_window_set_transient_for\|gtk_window_present"
```
Expected : `654:	static GtkWidget *Combos[13];` ; puis `0`. Chaque ouverture crée un nouveau dialogue non modal qui réécrit le même tableau statique : ouvrir A, ouvrir B, annuler B, valider A → `Lis_Config` lit les widgets détruits de B.

- [ ] **Step 2 : déclarations**

Dans `src/term_config.c`, `Config_Port_Fenetre`.

Remplacer :
```c
	static GtkWidget *Combos[13];
	GtkAdjustment *adj;
```
par :
```c
	static GtkWidget *port_dialog = NULL;	/* dialogue ouvert, remis à NULL à sa destruction (pointeur faible) */
	GtkWidget **Combos;
	GtkAdjustment *adj;
```

- [ ] **Step 3 : une seconde demande ramène le dialogue existant**

Remplacer :
```c
	int speed_index;

	device_paths = get_device_paths();
```
par :
```c
	int speed_index;

	/* Un seul dialogue Port à la fois (C12) : une seconde demande le ramène au premier plan au lieu d'en créer un autre */
	if(port_dialog != NULL)
	{
		gtk_window_present(GTK_WINDOW(port_dialog));
		return;
	}

	device_paths = get_device_paths();
```

- [ ] **Step 4 : dialogue modal, transitoire, tableau lié à sa durée de vie**

Remplacer :
```c
	Dialogue = gtk_dialog_new();
	content_area = gtk_dialog_get_content_area(GTK_DIALOG(Dialogue));
```
par :
```c
	Dialogue = gtk_dialog_new();
	port_dialog = Dialogue;
	g_object_add_weak_pointer(G_OBJECT(Dialogue), (gpointer *)&port_dialog);
	gtk_window_set_modal(GTK_WINDOW(Dialogue), TRUE);
	gtk_window_set_transient_for(GTK_WINDOW(Dialogue), GTK_WINDOW(Fenetre));

	/* Widgets lus par Lis_Config : tableau propre à ce dialogue et libéré avec lui, il ne peut plus être relu après sa destruction */
	Combos = g_new0(GtkWidget *, 13);
	g_object_set_data_full(G_OBJECT(Dialogue), "combos", Combos, g_free);

	content_area = gtk_dialog_get_content_area(GTK_DIALOG(Dialogue));
```

(Tous les `Combos[n] = ...` suivent cette création, et le `g_signal_connect(... Lis_Config, (gpointer)Combos)` reste inchangé. `Lis_Config` est connectée avant `gtk_widget_destroy` sur le bouton OK, donc exécutée avant la destruction : elle lit toujours un tableau vivant. Fermer par la croix ou Échap détruit aussi le dialogue (gestionnaire `delete-event` par défaut de `GtkDialog`), ce qui remet `port_dialog` à NULL.)

- [ ] **Step 5 : construire**

Run : `ninja -C build 2>&1 | grep -E "error|term_config\.c.*warning"`
Expected : seulement les trois avertissements `sign-compare` déjà présents (ex-lignes 766, 774 et 2075, décalés vers le bas par les lignes ajoutées), aucun nouveau, aucune erreur.

- [ ] **Step 6 : vérifier statiquement**

Run :
```bash
grep -c "static GtkWidget \*Combos" src/term_config.c
sed -n '/^void Config_Port_Fenetre/,/^}/p' src/term_config.c | grep -n "port_dialog\|gtk_window_set_modal\|gtk_window_set_transient_for\|Combos = g_new0\|g_object_set_data_full"
```
Expected : `0` ; puis, dans l'ordre : la déclaration de `port_dialog`, `if(port_dialog != NULL)`, `gtk_window_present(GTK_WINDOW(port_dialog));`, `port_dialog = Dialogue;`, `g_object_add_weak_pointer(... &port_dialog)`, `gtk_window_set_modal`, `gtk_window_set_transient_for`, `Combos = g_new0(GtkWidget *, 13);`, `g_object_set_data_full(... "combos", Combos, g_free);`.

- [ ] **Step 7 : non-régression**

Run : `meson test -C build`
Expected : `Fail: 0`.

- [ ] **Step 8 : commiter**

```bash
git add src/term_config.c
git commit -m "term_config : dialogue Port unique, modal et transitoire pour la fenêtre principale (C12)

Une seconde demande d'ouverture ramène le dialogue existant au premier plan. Le tableau des widgets lus par Lis_Config n'est plus statique :
il appartient au dialogue et disparaît avec lui, il ne peut donc plus désigner les widgets détruits d'un autre dialogue.

Preuve : essai manuel EM-7.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 14 : journal dupliqué au changement de vue (M31) et touche Échap (M32)

**Files :**
- Modify : `src/logging.c:39` (nouvelle variable), `src/logging.c:150-170` (nouvelle fonction, condition de `log_chars`)
- Modify : `src/logging.h:21`
- Modify : `src/interface.c:216-238` (`set_view`)
- Modify : `src/terminal_display.c:110-160` (`send_serial`, `Got_Input`)

**Interfaces :**
- Consumes : `write_buffer(void)` (`buffer.h:28`) ; `clear_buffer(void)` (`buffer.h:27`, déjà inclus par `terminal_display.c`) ; `esc_clear_screen_on` (`interface.c:92`, déjà `extern` dans `terminal_display.c:26`).
- Produces : `void logging_set_suspended(gboolean suspended)` (`logging.h`) : tant que `suspended` vaut TRUE, `log_chars` n'écrit rien. N'ouvre ni ne ferme rien, ne touche pas aux menus.

Essais manuels : **EM-8** (M31), **EM-9** (M32).

#### 14.1 — M31

- [ ] **Step 1 : constater le défaut**

Run :
```bash
sed -n '/^void set_view/,/^}/p' src/interface.c | grep -n "write_buffer"
grep -n "log_chars" src/terminal_display.c
```
Expected : `write_buffer();` en fin de `set_view` ; `log_chars (data_byte, 3);` dans `put_hexadecimal` et `log_chars (string, size);` dans `put_text`. Le rejeu du tampon (jusqu'à 128 Ko) passe par la fonction d'affichage, qui journalise : chaque changement de vue réécrit tout le tampon dans le journal (en hexadécimal, sous forme de texte « XX »).

- [ ] **Step 2 : `logging_set_suspended`**

Dans `src/logging.h`, remplacer :
```c
void log_chars(const gchar *chars, guint size);
```
par :
```c
void log_chars(const gchar *chars, guint size);
void logging_set_suspended(gboolean suspended);
```

Dans `src/logging.c`, remplacer :
```c
static gchar     *logfile_default = NULL;
```
par :
```c
static gchar     *logfile_default = NULL;
static gboolean   LoggingSuspended = FALSE;	/* vrai pendant le rejeu du tampon : ces octets sont déjà journalisés */
```

Remplacer :
```c
void log_chars(const gchar *chars, guint size)
{
	guint writeAttempts = 0;
	guint bytesWritten = 0;

	/* if we are not logging exit */
	if(LoggingFile == NULL || Logging == FALSE)
	{
```
par :
```c
/* Suspend (TRUE) ou reprend (FALSE) la journalisation sans fermer le journal (M31) */
void logging_set_suspended(gboolean suspended)
{
	LoggingSuspended = suspended;
}

void log_chars(const gchar *chars, guint size)
{
	guint writeAttempts = 0;
	guint bytesWritten = 0;

	/* if we are not logging exit */
	if(LoggingFile == NULL || Logging == FALSE || LoggingSuspended)
	{
```

- [ ] **Step 3 : suspendre pendant le rejeu (écart validé : suspension dans `set_view`, voir la spec)**

Dans `src/interface.c`, `set_view`, remplacer :
```c
	default:
		set_display_func(NULL);
	}
	write_buffer();
}
```
par :
```c
	default:
		set_display_func(NULL);
	}
	/* Rejeu du tampon dans la nouvelle vue : ces octets ont été journalisés à leur réception (M31) */
	logging_set_suspended(TRUE);
	write_buffer();
	logging_set_suspended(FALSE);
}
```

- [ ] **Step 4 : construire**

Run : `ninja -C build 2>&1 | grep -E "error|(logging|interface)\.c.*warning"`
Expected : seulement le `cast-function-type` déjà présent d'`interface.c` (`control_signals_read`), aucune erreur.

- [ ] **Step 5 : vérifier statiquement**

Run :
```bash
grep -rn "logging_set_suspended" src/
sed -n '/^void set_view/,/^}/p' src/interface.c | grep -n "logging_set_suspended\|write_buffer"
```
Expected : la déclaration dans `logging.h`, la définition dans `logging.c`, deux appels dans `interface.c` ; dans `set_view`, l'ordre `logging_set_suspended(TRUE);`, `write_buffer();`, `logging_set_suspended(FALSE);`.

- [ ] **Step 6 : non-régression**

Run : `meson test -C build`
Expected : `Fail: 0`.

- [ ] **Step 7 : commiter**

```bash
git add src/logging.c src/logging.h src/interface.c
git commit -m "logging, interface : pas de journalisation pendant le rejeu du tampon au changement de vue (M31)

set_view rejoue le tampon de réception dans la nouvelle vue, et la fonction d'affichage journalise : chaque bascule ASCII/hexa réécrivait
jusqu'à 128 Ko déjà journalisés. Nouvelle fonction logging_set_suspended, appelée autour du rejeu.

Preuve : essai manuel EM-8.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

#### 14.2 — M32

- [ ] **Step 8 : constater le défaut**

Run :
```bash
grep -n "esc_clear_screen_on" src/terminal_display.c
grep -n '"commit"' src/interface.c
```
Expected : `26:extern gboolean esc_clear_screen_on;` et `116:  if (esc_clear_screen_on && len == 1 && (guchar)string[0] == '\x1b')` (dans `send_serial`, appelé aussi par les macros, Lua, la saisie hexadécimale et l'envoi de fichier) ; `863:	g_signal_connect_after(GTK_WIDGET(display), "commit", G_CALLBACK(Got_Input), NULL);` : `Got_Input` est le gestionnaire de saisie clavier.

- [ ] **Step 9 : retirer la règle de `send_serial`**

Dans `src/terminal_display.c`, remplacer :
```c
  gint bytes_written;

  if (esc_clear_screen_on && len == 1 && (guchar)string[0] == '\x1b')
    {
      clear_buffer ();
      return 1;
    }

  bytes_written = Send_chars (string, len);
```
par :
```c
  gint bytes_written;

  bytes_written = Send_chars (string, len);
```

- [ ] **Step 10 : l'appliquer à la saisie clavier**

Remplacer :
```c
void
Got_Input (VteTerminal *widget, gchar *text, guint length, gpointer ptr)
{
  send_serial (text, length);
}
```
par :
```c
void
Got_Input (VteTerminal *widget, gchar *text, guint length, gpointer ptr)
{
  /* « ESC efface l'écran » : seule la touche Échap tapée dans le terminal efface l'écran local, sans rien envoyer (M32).
     Les macros, Lua, la saisie hexadécimale et l'envoi de fichier passent par send_serial et envoient toujours 0x1B. */
  if (esc_clear_screen_on && length == 1 && (guchar)text[0] == '\x1b')
    {
      clear_buffer ();
      return;
    }

  send_serial (text, length);
}
```

(`Envoie_car`, juste en dessous, n'est connectée nulle part : elle n'est pas modifiée.)

- [ ] **Step 11 : construire**

Run : `ninja -C build 2>&1 | grep -E "error|terminal_display\.c.*warning"`
Expected : seulement les `sign-compare` déjà présents de `put_hexadecimal` (ex-lignes 56, 60, 88, 94), aucune erreur.

- [ ] **Step 12 : vérifier statiquement**

Run :
```bash
sed -n '/^send_serial/,/^}/p' src/terminal_display.c | grep -c "esc_clear_screen_on"
sed -n '/^Got_Input/,/^}/p' src/terminal_display.c | grep -n "if (esc_clear_screen_on\|clear_buffer ();\|send_serial (text"
```
Expected : `0` ; puis, dans l'ordre, `if (esc_clear_screen_on && length == 1 && (guchar)text[0] == '\x1b')`, `clear_buffer ();` et `send_serial (text, length);`.

- [ ] **Step 13 : non-régression**

Run : `meson test -C build`
Expected : `Fail: 0`.

- [ ] **Step 14 : commiter**

```bash
git add src/terminal_display.c
git commit -m "terminal_display : « ESC efface l'écran » ne concerne plus que la touche Échap tapée au clavier (M32)

La règle était dans send_serial : tout envoi d'un octet 0x1B seul (macro, Lua, saisie hexadécimale, fichier) était avalé et effaçait
l'écran. Elle passe dans Got_Input, le gestionnaire « commit » de VTE ; send_serial envoie désormais toujours 0x1B.

Preuve : essai manuel EM-9.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 15 : ne pas réécrire `.gtktermrc` après un échec de lecture (M12)

**Files :**
- Modify : `src/term_config.c:267-271` (`save_window_state`)
- Modify : `src/script_panel.c:140-144` (`save_script_colors`)

**Interfaces :**
- Consumes : `get_config_file_path()` ; `g_error_matches`, `G_FILE_ERROR_NOENT` (GLib).
- Produces : rien. Comportement : fichier absent → écriture comme avant ; fichier présent mais illisible (encodage, syntaxe, droits) → rien n'est écrit, l'erreur est affichée par `g_printerr`.

Essai manuel : **EM-4**.

- [ ] **Step 1 : constater le défaut**

Run : `grep -n "g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)" src/term_config.c src/script_panel.c`
Expected :
```
src/term_config.c:271:	g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL); /* load existing, preserve other sections */
src/script_panel.c:144:    g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL);
```
(La ligne 96 de `script_panel.c`, en lecture seule, n'est pas concernée.) Le résultat est ignoré : si `.gtktermrc` contient une ligne que GKeyFile refuse (ni clé=valeur, ni groupe, ni commentaire → `G_KEY_FILE_ERROR_PARSE`), GKeyFile ne garde que ce qui précède cette ligne, et le fichier est réécrit sans tout ce qui suit : avec la ligne en tête de fichier, tous les profils sont perdus.

- [ ] **Step 2 : `save_window_state`** (fichier en tabulations, Allman, espace avant `(` dans cette fonction)

Dans `src/term_config.c`, remplacer :
```c
	GKeyFile *kf = g_key_file_new();
	g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL); /* load existing, preserve other sections */
```
par :
```c
	GKeyFile *kf = g_key_file_new();
	GError *lerr = NULL;

	/* Charger l'existant pour préserver les autres sections. Fichier présent mais illisible (M12) : ne rien écrire,
	   sinon il serait remplacé par la seule section [window] et les profils seraient perdus. */
	if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, &lerr))
	{
		if (!g_error_matches(lerr, G_FILE_ERROR, G_FILE_ERROR_NOENT))
		{
			g_printerr("Cannot save window state: cannot read %s: %s\n", path, lerr->message);
			g_error_free(lerr);
			g_key_file_free(kf);
			return;
		}
		g_clear_error(&lerr);
	}
```

- [ ] **Step 3 : `save_script_colors`** (fichier en 4 espaces, accolades K&R)

Dans `src/script_panel.c`, remplacer :
```c
    const gchar *path = get_config_file_path();
    GKeyFile *kf   = g_key_file_new();
    g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL);
```
par :
```c
    const gchar *path = get_config_file_path();
    GKeyFile *kf   = g_key_file_new();
    GError *lerr   = NULL;

    /* Fichier présent mais illisible (M12) : ne rien écrire, sinon il serait remplacé par les seules sections [colors], [layout] et [session]. */
    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, &lerr)) {
        if (!g_error_matches(lerr, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            g_printerr("Cannot save script state: cannot read %s: %s\n", path, lerr->message);
            g_error_free(lerr);
            g_key_file_free(kf);
            /* Comme après une sauvegarde : les couleurs de la session restent celles de l'éditeur */
            sc_has_custom_bg = TRUE;
            sc_has_custom_fg = TRUE;
            return;
        }
        g_clear_error(&lerr);
    }
```

- [ ] **Step 4 : construire**

Run : `ninja -C build 2>&1 | grep -E "error|(term_config|script_panel)\.c.*warning"`
Expected : pour `term_config.c`, seulement les `sign-compare` déjà présents ; rien pour `script_panel.c` ; aucune erreur.

- [ ] **Step 5 : vérifier statiquement**

Run :
```bash
grep -n "g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)" src/term_config.c src/script_panel.c
grep -n "G_FILE_ERROR_NOENT" src/term_config.c src/script_panel.c
```
Expected : aucune sortie pour la première commande ; une ligne par fichier pour la seconde.

Contrôle de la cause (facultatif, sans toucher au vrai `.gtktermrc`) :
```bash
S=/tmp/claude-1315401155/-home-METRO-gabriel-git-gtkterm/e7fc7d21-128e-4797-b30d-8c870b9c4643/scratchpad/m12
mkdir -p $S && printf 'ligne invalide\n[default]\nport=/dev/ttyS0\n[window]\nx=1\n' > $S/rc && python3 -c "
import gi; gi.require_version('GLib','2.0'); from gi.repository import GLib
k=GLib.KeyFile()
try: k.load_from_file('$S/rc', GLib.KeyFileFlags.NONE)
except GLib.Error as e: print(e.domain, e.code)
k.set_integer('window','x',2); print(repr(k.to_data()[0]))"
```
Expected : `g-key-file-error-quark 1` (`G_KEY_FILE_ERROR_PARSE`, différent de `G_FILE_ERROR_NOENT` : c'est la branche qui n'écrit plus rien), puis `'[window]\nx=2\n'` : ce que l'ancien code aurait écrit, sans le profil `[default]`. (Si `python3-gi` est absent, sauter ce contrôle.)

- [ ] **Step 6 : non-régression**

Run : `meson test -C build`
Expected : `Fail: 0`.

- [ ] **Step 7 : commiter**

```bash
git add src/term_config.c src/script_panel.c
git commit -m "term_config, script_panel : pas de réécriture de .gtktermrc s'il existe mais ne se relit pas (M12)

save_window_state et save_script_colors ignoraient l'échec de g_key_file_load_from_file : GKeyFile ne garde alors que ce qui précède la
ligne refusée, et le fichier était réécrit sans le reste (avec une ligne invalide en tête, tous les profils étaient perdus). Seul « fichier absent » autorise désormais
l'écriture ; les autres erreurs sont affichées sur la sortie d'erreur.

Preuve : essai manuel EM-4.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 16 : vérification de mise à jour (M34), envoi de fichier (M35), script de mise à jour (M8)

**Files :**
- Modify : `src/update.c:131-135` (nouvelle fonction après `CheckCtx`), `src/update.c:161-212` (`on_ls_remote_done`), `src/update.c:224-246` (`update_check`)
- Modify : `src/files.c:64` (prototype), `src/files.c:164-248` (`ecriture`, remplacée en entier), `src/files.c:260-272` (`add_input`)
- Modify : `data/gtkterm-update.sh:97-98`

**Interfaces :**
- Consumes : `config.update_url` ; `g_subprocess_launcher_new`, `g_subprocess_launcher_setenv`, `g_subprocess_launcher_spawn`, `g_object_add_weak_pointer`, `g_object_remove_weak_pointer` (GLib/GIO) ; `transport_get_fd()`, `send_serial()`, `close_all()`, `remove_input()`.
- Produces :
  - `static void check_ctx_free(CheckCtx *ctx)` dans `update.c` : retire le pointeur faible puis libère le contexte ;
  - `gboolean ecriture(GIOChannel *source, GIOCondition condition, gpointer data)` (signature `GIOFunc`, interne à `files.c`) : renvoie `TRUE` tant que la surveillance doit continuer, `FALSE` quand le transfert est terminé, interrompu ou mis en attente (source déjà retirée).

Essais manuels : **EM-13** (M34), **EM-14** (M35), **EM-15** (M8, à la prochaine mise à jour réelle).

#### 16.1 — M34

- [ ] **Step 1 : constater le défaut**

Run :
```bash
grep -n '"ls-remote"\|GIT_TERMINAL_PROMPT\|weak_pointer\|ctx->parent' src/update.c
```
Expected :
```
174:			GtkWidget *m = gtk_message_dialog_new(ctx->parent,
199:			GtkWidget *m = gtk_message_dialog_new(ctx->parent,
208:		offer_update(ctx->parent, remote_sha);
228:			"git", "ls-remote", config.update_url, "HEAD", NULL);
245:	ctx->parent = parent;
```
Pas de `--` devant l'URL (une URL commençant par `-` serait lue comme une option de git), pas de `GIT_TERMINAL_PROMPT=0` (un dépôt demandant une authentification attend une saisie invisible), et `ctx->parent` est un pointeur brut vers « À propos » : fermé pendant la vérification, il est utilisé après destruction.

- [ ] **Step 2 : libération du contexte avec retrait du pointeur faible**

Dans `src/update.c`, remplacer :
```c
typedef struct
{
	GtkWindow *parent;
	gboolean manual;
} CheckCtx;
```
par :
```c
typedef struct
{
	GtkWindow *parent;	/* pointeur faible : remis à NULL si le dialogue parent est détruit pendant la vérification (M34) */
	gboolean manual;
} CheckCtx;

static void check_ctx_free(CheckCtx *ctx)
{
	if(ctx->parent != NULL)
		g_object_remove_weak_pointer(G_OBJECT(ctx->parent), (gpointer *)&ctx->parent);
	g_free(ctx);
}
```

- [ ] **Step 3 : `on_ls_remote_done` libère par `check_ctx_free`**

Remplacer :
```c
		g_clear_error(&error);
		g_free(stdout_buf);
		g_free(ctx);
		return;
```
par :
```c
		g_clear_error(&error);
		g_free(stdout_buf);
		check_ctx_free(ctx);
		return;
```

Remplacer :
```c
		offer_update(ctx->parent, remote_sha);
	}

	g_free(ctx);
}
```
par :
```c
		offer_update(ctx->parent, remote_sha);
	}

	check_ctx_free(ctx);
}
```

(Tant que le contexte vit, `ctx->parent` vaut NULL si « À propos » a été détruit : les messages s'affichent alors sans parent, `gtk_message_dialog_new` et `gtk_dialog_new_with_buttons` acceptant un parent NULL. Les dialogues ouverts ensuite sont modaux : pendant qu'ils sont affichés, « À propos » ne peut plus être fermé.)

- [ ] **Step 4 : `update_check` : `--`, `GIT_TERMINAL_PROMPT=0`, pointeur faible**

Remplacer :
```c
	GError *error = NULL;
	GSubprocess *proc = g_subprocess_new(
			G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
			&error,
			"git", "ls-remote", config.update_url, "HEAD", NULL);
```
par :
```c
	GError *error = NULL;
	/* « -- » : l'URL configurée ne peut pas être prise pour une option de git ; GIT_TERMINAL_PROMPT=0 : un dépôt qui demande
	   une authentification échoue au lieu d'attendre une saisie dans un terminal invisible (M34). */
	GSubprocessLauncher *launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE);
	g_subprocess_launcher_setenv(launcher, "GIT_TERMINAL_PROMPT", "0", TRUE);
	GSubprocess *proc = g_subprocess_launcher_spawn(launcher, &error, "git", "ls-remote", "--", config.update_url, "HEAD", NULL);
	g_object_unref(launcher);
```

Remplacer :
```c
	CheckCtx *ctx = g_new0(CheckCtx, 1);
	ctx->parent = parent;
	ctx->manual = manual;
```
par :
```c
	CheckCtx *ctx = g_new0(CheckCtx, 1);
	ctx->parent = parent;
	if(parent != NULL)
		g_object_add_weak_pointer(G_OBJECT(parent), (gpointer *)&ctx->parent);
	ctx->manual = manual;
```

- [ ] **Step 5 : construire**

Run : `ninja -C build 2>&1 | grep -E "error|update\.c.*warning"`
Expected : aucune sortie.

- [ ] **Step 6 : vérifier statiquement et la syntaxe de git**

Run :
```bash
grep -n '"ls-remote"\|GIT_TERMINAL_PROMPT\|weak_pointer\|g_free(ctx)\|check_ctx_free' src/update.c
GIT_TERMINAL_PROMPT=0 git ls-remote -- . HEAD | head -1
```
Expected : `"ls-remote", "--", config.update_url` (plus la ligne de commentaire qui cite `GIT_TERMINAL_PROMPT=0`) ; `g_subprocess_launcher_setenv(launcher, "GIT_TERMINAL_PROMPT", "0", TRUE);` ; un `g_object_add_weak_pointer` (dans `update_check`) et un `g_object_remove_weak_pointer` (dans `check_ctx_free`) ; plus aucun `g_free(ctx)` hors de `check_ctx_free` (une seule occurrence, dans cette fonction) ; deux appels `check_ctx_free(ctx);`. Puis `<sha de HEAD>	HEAD` : git accepte bien `--` devant le dépôt.

Facultatif (réseau requis) : `GIT_TERMINAL_PROMPT=0 git ls-remote -- https://github.com/Mula-Gabriel/depot-inexistant-xyz.git HEAD; echo rc=$?`
Expected : `fatal: could not read Username for 'https://github.com': terminal prompts disabled` et `rc=128`, immédiatement, sans attente de saisie.

- [ ] **Step 7 : non-régression**

Run : `meson test -C build`
Expected : `Fail: 0`.

- [ ] **Step 8 : commiter**

```bash
git add src/update.c
git commit -m "update : git ls-remote avec « -- » et GIT_TERMINAL_PROMPT=0, dialogue parent suivi par un pointeur faible (M34)

Fermer « À propos » pendant la vérification laissait un pointeur vers une fenêtre détruite, utilisée à l'affichage du résultat ; le
résultat s'affiche désormais sans parent. L'URL ne peut plus être lue comme une option de git, et un dépôt demandant une authentification
échoue au lieu d'attendre une saisie invisible.

Preuve : essai manuel EM-13.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

#### 16.2 — M35

- [ ] **Step 9 : constater le défaut**

Run :
```bash
grep -n "void ecriture\|(GIOFunc)\|g_io_channel_unix_new\|g_io_channel_unref\|while(\*car" src/files.c
touch src/files.c && ninja -C build 2>&1 | grep "files.c.*cast-function-type"
```
Expected :
```
64:void ecriture(gpointer data, gint source);
164:void ecriture(gpointer data, gint source)
202:			while(*car != LINE_FEED && bytes_to_write < bytes_read)
265:		callback_handler = g_io_add_watch_full(g_io_channel_unix_new(transport_get_fd()),
268:		                                       (GIOFunc)ecriture,
```
puis l'avertissement `files.c:268:56: warning: cast between incompatible function types from 'void (*)(void *, gint)' ... [-Wcast-function-type]`. Le rappel ne renvoie rien alors que GLib lit un `gboolean` (surveillance gardée ou retirée au hasard) ; `read` à 0 ou -1 laisse `current_buffer_position == bytes_read` et le rappel tourne à vide indéfiniment (ou envoie une longueur négative) ; la boucle de recherche de LF teste `*car` avant la borne et lit `buffer[bytes_read]`, au-delà des données (hors du tableau si `bytes_read == BUFFER_EMISSION`) ; le canal créé par `g_io_channel_unix_new` n'est jamais libéré.

- [ ] **Step 10 : prototype**

Dans `src/files.c`, remplacer :
```c
void ecriture(gpointer data, gint source);
```
par :
```c
gboolean ecriture(GIOChannel *source, GIOCondition condition, gpointer data);
```

- [ ] **Step 11 : `ecriture` (remplacée en entier)**

Remplacer :
```c
void ecriture(gpointer data, gint source)
{
	static gchar buffer[BUFFER_EMISSION];
	static gchar *current_buffer;
	static gint bytes_to_write;
	gint bytes_written;
	gchar *car;

	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ProgressBar), (gfloat)car_written/(gfloat)nb_car );

	if(car_written < nb_car)
	{
		/* Read the file only if buffer totally sent or if buffer empty */
		if(current_buffer_position == bytes_read)
		{
			bytes_read = read(Fichier, buffer, BUFFER_EMISSION);

			current_buffer_position = 0;
			current_buffer = buffer;
			bytes_to_write = bytes_read;
		}

		if(current_buffer == NULL)
		{
			/* something went wrong... */
			g_free(str);
			str = g_strdup_printf(_("Error sending file\n"));
			show_message(str, MSG_ERR);
			close_all();
			return;
		}

		car = current_buffer;

		if(config.delai != 0 || config.car != -1)
		{
			/* search for next LF */
			bytes_to_write = current_buffer_position;
			while(*car != LINE_FEED && bytes_to_write < bytes_read)
			{
				car++;
				bytes_to_write++;
			}
			if(*car == LINE_FEED)
				bytes_to_write++;
		}

		/* write to serial port */
		bytes_written = send_serial(current_buffer, bytes_to_write - current_buffer_position);

		if(bytes_written == -1)
		{
			/* Problem while writing, stop file transfer */
			g_free(str);
			str = g_strdup_printf(_("Error sending file: %s\n"), strerror(errno));
			show_message(str, MSG_ERR);
			close_all();
			return;
		}

		car_written += bytes_written;
		current_buffer_position += bytes_written;
		current_buffer += bytes_written;

		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ProgressBar), (gfloat)car_written/(gfloat)nb_car );

		if(config.delai != 0 && *car == LINE_FEED)
		{
			remove_input();
			g_timeout_add(config.delai, (GSourceFunc)timer, NULL);
			waiting_for_timer = TRUE;
		}
		else if(config.car != -1 && *car == LINE_FEED)
		{
			remove_input();
			waiting_for_char = TRUE;
		}
	}
	else
	{
		close_all();
		return;
	}
	return;
}
```
par :
```c
/* Rappel GIOFunc de la surveillance G_IO_OUT (M35) : TRUE pour continuer, FALSE quand le transfert est terminé,
   interrompu ou mis en attente (la source est alors déjà retirée par close_all ou remove_input). */
gboolean ecriture(GIOChannel *source, GIOCondition condition, gpointer data)
{
	static gchar buffer[BUFFER_EMISSION];
	static gchar *current_buffer;
	static gint bytes_to_write;
	gint bytes_written;
	gchar *car;
	gboolean lf_found = FALSE;

	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ProgressBar), (gfloat)car_written/(gfloat)nb_car );

	if(car_written < nb_car)
	{
		/* Read the file only if buffer totally sent or if buffer empty */
		if(current_buffer_position == bytes_read)
		{
			bytes_read = read(Fichier, buffer, BUFFER_EMISSION);

			if(bytes_read <= 0)
			{
				/* 0 : fin du fichier avant la taille annoncée (fichier raccourci) : fin normale du transfert.
				   -1 : erreur de lecture : transfert interrompu avec un message. Fermer d'abord : show_message fait tourner
				   une boucle imbriquée pendant laquelle la surveillance rappellerait ce rappel. */
				gint read_errno = errno;

				close_all();
				if(bytes_read < 0)
				{
					g_free(str);
					str = g_strdup_printf(_("Error sending file: %s\n"), strerror(read_errno));
					show_message(str, MSG_ERR);
				}
				return FALSE;
			}

			current_buffer_position = 0;
			current_buffer = buffer;
			bytes_to_write = bytes_read;
		}

		if(current_buffer == NULL)
		{
			/* something went wrong... */
			g_free(str);
			str = g_strdup_printf(_("Error sending file\n"));
			show_message(str, MSG_ERR);
			close_all();
			return FALSE;
		}

		car = current_buffer;

		if(config.delai != 0 || config.car != -1)
		{
			/* search for next LF, sans lire au-delà des données (borne testée avant *car) */
			bytes_to_write = current_buffer_position;
			while(bytes_to_write < bytes_read && *car != LINE_FEED)
			{
				car++;
				bytes_to_write++;
			}
			if(bytes_to_write < bytes_read)
			{
				/* *car est le LF : il part avec sa ligne */
				lf_found = TRUE;
				bytes_to_write++;
			}
		}

		/* write to serial port */
		bytes_written = send_serial(current_buffer, bytes_to_write - current_buffer_position);

		if(bytes_written == -1)
		{
			/* Problem while writing, stop file transfer */
			g_free(str);
			str = g_strdup_printf(_("Error sending file: %s\n"), strerror(errno));
			show_message(str, MSG_ERR);
			close_all();
			return FALSE;
		}

		car_written += bytes_written;
		current_buffer_position += bytes_written;
		current_buffer += bytes_written;

		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ProgressBar), (gfloat)car_written/(gfloat)nb_car );

		if(config.delai != 0 && lf_found)
		{
			remove_input();
			g_timeout_add(config.delai, (GSourceFunc)timer, NULL);
			waiting_for_timer = TRUE;
			return FALSE;
		}
		else if(config.car != -1 && lf_found)
		{
			remove_input();
			waiting_for_char = TRUE;
			return FALSE;
		}
	}
	else
	{
		close_all();
		return FALSE;
	}
	return TRUE;
}
```

- [ ] **Step 12 : `add_input` : plus de transtypage, canal libéré**

Remplacer :
```c
void add_input(void)
{
	if(input_running == FALSE)
	{
		input_running = TRUE;
		callback_handler = g_io_add_watch_full(g_io_channel_unix_new(transport_get_fd()),
		                                       10,
		                                       G_IO_OUT,
		                                       (GIOFunc)ecriture,
		                                       NULL, NULL);

	}
}
```
par :
```c
void add_input(void)
{
	GIOChannel *channel;

	if(input_running == FALSE)
	{
		input_running = TRUE;
		channel = g_io_channel_unix_new(transport_get_fd());
		callback_handler = g_io_add_watch_full(channel,
		                                       10,
		                                       G_IO_OUT,
		                                       ecriture,
		                                       NULL, NULL);
		/* La source garde sa propre référence sur le canal (M35) ; g_io_channel_unix_new ne ferme pas le descripteur à la libération */
		g_io_channel_unref(channel);
	}
}
```

- [ ] **Step 13 : construire**

Run : `ninja -C build 2>&1 | grep -E "error|files\.c.*warning"`
Expected : seulement le `sign-compare` déjà présent de `write_ascii_file` ; plus de `cast-function-type` ; aucune erreur.

- [ ] **Step 14 : vérifier statiquement**

Run :
```bash
grep -n "ecriture\|(GIOFunc)\|g_io_channel_unref\|while(bytes_to_write < bytes_read\|\*car == LINE_FEED" src/files.c
```
Expected : la ligne 17 du cartouche (« modified ecriture() … », inchangée) ; le prototype et la définition `gboolean ecriture(GIOChannel *source, GIOCondition condition, gpointer data)` ; `ecriture,` sans transtypage dans `add_input` ; `g_io_channel_unref(channel);` ; `while(bytes_to_write < bytes_read && *car != LINE_FEED)` ; aucune occurrence de `*car == LINE_FEED` ; aucun `(GIOFunc)`.

- [ ] **Step 15 : non-régression**

Run : `meson test -C build`
Expected : `Fail: 0`.

- [ ] **Step 16 : commiter**

```bash
git add src/files.c
git commit -m "files : rappel d'envoi de fichier au type GIOFunc, fin de lecture traitée, recherche de LF bornée, canal libéré (M35)

ecriture ne renvoyait rien alors que GLib lit un gboolean ; read à 0 ou -1 faisait tourner le rappel à vide (blocage à 100 %) ; la
recherche de fin de ligne lisait l'octet qui suit les données ; le GIOChannel n'était jamais libéré. read à 0 termine désormais le
transfert, -1 l'interrompt avec un message. L'avertissement cast-function-type de files.c disparaît.

Preuve : essai manuel EM-14.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

#### 16.3 — M8

- [ ] **Step 17 : constater le défaut**

Run :
```bash
head -1 data/gtkterm-update.sh
grep -n "PREFIX=\|^gtkterm\|disown" data/gtkterm-update.sh
dash -c 'disown'; echo "rc=$?"
```
Expected : `#!/bin/sh` ; `13:PREFIX="${2:-/usr/local}"`, `97:gtkterm &`, `98:disown` ; puis `dash: 1: disown: not found` et `rc=127`. Sous `/bin/sh` (dash) avec `set -eu`, la dernière commande échoue ; et `gtkterm &` relance le premier `gtkterm` du `PATH`, pas forcément celui qui vient d'être installé dans `$PREFIX`.

- [ ] **Step 18 : relancer l'exécutable installé, détaché**

Dans `data/gtkterm-update.sh`, remplacer :
```sh
gtkterm &
disown
```
par :
```sh
# Relancer l'exécutable qui vient d'être installé (pas le premier gtkterm du PATH). nohup le protège du SIGHUP envoyé à la fermeture
# du terminal de mise à jour ; « disown » n'existe pas sous /bin/sh (dash) (M8).
nohup "$PREFIX/bin/gtkterm" >/dev/null 2>&1 &
```

- [ ] **Step 19 : vérifier**

Run :
```bash
dash -n data/gtkterm-update.sh && echo "syntaxe OK"
grep -c "disown" data/gtkterm-update.sh
grep -n 'nohup' data/gtkterm-update.sh
command -v checkbashisms >/dev/null && checkbashisms data/gtkterm-update.sh || echo "checkbashisms absent"
```
Expected : `syntaxe OK` ; `1` (le seul « disown » restant est dans le commentaire) ; `nohup "$PREFIX/bin/gtkterm" >/dev/null 2>&1 &` ; `checkbashisms absent` (ou aucune remarque s'il est installé).

- [ ] **Step 20 : non-régression**

Run : `meson test -C build`
Expected : `Fail: 0`.

- [ ] **Step 21 : commiter**

```bash
git add data/gtkterm-update.sh
git commit -m "gtkterm-update.sh : relance de l'exécutable installé par nohup, sans disown (M8)

disown n'existe pas sous /bin/sh (dash) : avec set -eu, le script se terminait en erreur. La relance visait aussi le premier gtkterm du
PATH au lieu de \$PREFIX/bin/gtkterm, qui vient d'être installé.

Preuve : dash -n ; essai manuel EM-15 à la prochaine mise à jour réelle.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 17 : outillage — remarques reportées du sous-projet 0

**Files :**
- Create : `tests/lua-syntax.sh`
- Modify : `.gitignore`, `meson.build` (`glib_dep`), `tests/meson.build` (test `lua-syntax`), `tests/README.md`, `tests/test_macros_list.c`, `tests/test_macros_format.c` (noms des cas), `docs/superpowers/reviews/2026-10-06-revue-code.md`

**Interfaces :**
- Consumes : rien.
- Produces : test `lua-syntax` qui vérifie tous les `scripts/*.lua` présents au moment du test, et qui se déclare sauté (code 77) sans `luac`.

- [ ] **Step 1 : cas `macro_lists_init` (rouge attendu : aucun, c'est une couverture)**

Dans `tests/test_macros_list.c`, avant `main`, ajouter :

```c
static void
test_init_idempotent(void)
{
	macro_lists_free();
	macro_lists_init();
	macro_list_add("L", "x", NULL);
	macro_lists_init();
	g_assert_cmpint(macro_list_count(), ==, 1);
	g_assert_cmpint(macro_list_entry_count(0), ==, 1);
	macro_lists_free();
}
```

et `g_test_add_func("/macros_list/init-idempotent", test_init_idempotent);` dans `main`.

- [ ] **Step 2 : noms lisibles des cas de table**

Dans `tests/test_macros_format.c`, juste avant la macro `AJOUTER_TABLE`, ajouter :

```c
/* Étiquette de chemin GTest à partir d'un texte : lettres et chiffres gardés, le reste remplacé par « - », 24 caractères au plus. */
static gchar *
etiquette(const gchar *texte)
{
	GString *e = g_string_new(NULL);

	for(const gchar *p = texte; *p != '\0' && e->len < 24; p++)
		g_string_append_c(e, g_ascii_isalnum(*p) ? *p : '-');
	return g_string_free(e, FALSE);
}
```

et remplacer la macro `AJOUTER_TABLE` et ses appels par :

```c
#define AJOUTER_TABLE(prefixe, table, champ, fonction) \
	for(guint i = 0; i < G_N_ELEMENTS(table); i++) \
	{ \
		gchar *nom = etiquette(table[i].champ); \
		gchar *chemin = g_strdup_printf("/macros_format/" prefixe "/%02u-%s", i, nom); \
		g_test_add_data_func(chemin, &table[i], fonction); \
		g_free(chemin); \
		g_free(nom); \
	}
```

```c
	AJOUTER_TABLE("echappement", cas_echappements, entree, test_echappement);
	AJOUTER_TABLE("expansion", cas_expansions, action, test_expansion);
	AJOUTER_TABLE("validation", cas_validations, valeur, test_validation);
	AJOUTER_TABLE("arguments", cas_arguments, action, test_arguments);
```

- [ ] **Step 3 : `lua-syntax` sur tous les exemples, sauté sans `luac`**

Créer `tests/lua-syntax.sh` :

```sh
#!/bin/sh
# Vérifie la syntaxe de tous les exemples Lua présents (luac -p : analyse seule, aucun fichier produit).
# Code 77 : test déclaré « sauté » par meson quand luac est absent.
LUAC=$(command -v luac5.4 || command -v luac) || exit 77
exec "$LUAC" -p "$1"/*.lua
```

Dans `tests/meson.build`, remplacer tout le bloc `# Syntaxe des exemples Lua …` (de `luac = find_program(` à `endif`) par :

```meson
# Syntaxe de tous les exemples Lua de scripts/ au moment du test ; sauté (code 77) si luac est absent.
test('lua-syntax', find_program('sh'), args : [files('lua-syntax.sh'), meson.project_source_root() / 'scripts'], suite : 'lint')
```

Run : `meson test -C build lua-syntax 2>&1 | grep lua-syntax; env PATH=/nonexistent /bin/sh tests/lua-syntax.sh scripts; echo "code=$?"`
Expected : `lua-syntax OK`, puis `code=77`. Sans `luac` dans le PATH, le script se déclare sauté.

- [ ] **Step 4 : `.gitignore`, version de GLib, README, revue**

`.gitignore` :

```
*~
/build*/
/graphify-out/
```

`meson.build` : remplacer `glib_dep = dependency('glib-2.0')` par `glib_dep = dependency('glib-2.0', version : '>= 2.70')	# g_test_incomplete_printf / g_test_fail_printf`.

`tests/README.md` :
- remplacer `meson test -C build test_parsecfg -v   # un test, sortie TAP détaillée` par `meson test -C build test_parsecfg -v   # un test, statut de chaque sous-test (le détail est dans build/meson-logs/testlog.txt)` ;
- sous la commande valgrind, ajouter la phrase : `Les fuites ne sont pas vérifiées par valgrind ici (--leak-check=no) : celles, connues, de parsecfg.c le feraient échouer ; elles sont vérifiées par LeakSanitizer (build-asan), qui les ignore grâce à tests/lsan.supp.` ;
- dans « Écrire un test », ajouter une puce : `Test d'intégration d'un module de src/ qui dépend de l'interface : compiler le vrai fichier avec les bouchons de tests/stubs/ (dictionnaire integration_tests de tests/meson.build) ; un bouchon enregistre l'appel (stub_record) et ne contient aucune logique métier.`

`docs/superpowers/reviews/2026-10-06-revue-code.md` : après la ligne M42, ajouter `- Note (relecture du sous-projet 0) : \`git -c safe.directory=*\` n'est respecté qu'à partir de git 2.38 ; accepté, puisque l'installation se fait par \`meson install --no-rebuild\` et ne relance plus git en root.`

- [ ] **Step 5 : vérifier et commiter**

Run : `meson setup --reconfigure build >/dev/null && meson test -C build 2>&1 | grep -E "^(Ok|Fail|Skipped)"; git status --short`
Expected : `Fail: 0`.

```bash
git add .gitignore meson.build tests/lua-syntax.sh tests/meson.build tests/README.md tests/test_macros_list.c tests/test_macros_format.c docs/superpowers/reviews/2026-10-06-revue-code.md
git commit -m "tests : remarques reportées du sous-projet 0 (lua-syntax dynamique et sauté sans luac, noms de cas lisibles, macro_lists_init, .gitignore ancré, GLib >= 2.70, README)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 18 : vérification d'acceptation

**Files :**
- Modify (seulement si un problème est révélé) : `docs/superpowers/reviews/2026-10-06-revue-code.md`, tests concernés.

**Interfaces :**
- Consumes : tout ce qui précède.

- [ ] **Step 1 : configuration neuve et suite complète (critères 1 et 3)**

Run :
```bash
N=/tmp/claude-1315401155/-home-METRO-gabriel-git-gtkterm/e7fc7d21-128e-4797-b30d-8c870b9c4643/scratchpad/build-accept-sp1
rm -rf $N && meson setup $N >/dev/null && ninja -C $N 2>&1 | grep -cE "error" ; meson test -C $N 2>&1 | grep -E "^(Ok|Fail|Skipped)"; grep -oE "TODO bogue (C7|M9|M18|M19|M20|M23)" $N/meson-logs/testlog.txt | sort | uniq -c
```
Expected : `0` erreur ; `Fail: 0` ; aucune ligne pour le dernier grep.

- [ ] **Step 2 : ASan + UBSan (critère 2)**

Run :
```bash
S=/tmp/claude-1315401155/-home-METRO-gabriel-git-gtkterm/e7fc7d21-128e-4797-b30d-8c870b9c4643/scratchpad/build-asan-sp1
rm -rf $S && meson setup $S -Db_sanitize=address,undefined >/dev/null && ninja -C $S >/dev/null 2>&1; meson test -C $S 2>&1 | grep -E "unit /|^(Ok|Fail)"
```
Expected : `Fail: 0`. Si un rapport apparaît, lire `$S/meson-logs/testlog.txt` :
- une fuite dans `parsecfg.c` est déjà couverte par `leak:parsecfg.c` ;
- tout autre rapport est un défaut, à corriger s'il vient d'un correctif de ce plan, à consigner (M43…) sinon.

- [ ] **Step 3 : valgrind (erreurs mémoire)**

Run : `meson test -C build --suite unit --wrap='valgrind --error-exitcode=1 --leak-check=no' 2>&1 | grep -E "^(Ok|Fail)"`
Expected : `Fail: 0`.

- [ ] **Step 4 : couverture de la spec (critère 3)**

Run : `for t in test_transport test_script_engine test_macros_send test_config_file test_serial_termios; do grep -q "'$t'" tests/meson.build && echo "$t déclaré"; done`
Expected : les cinq lignes « déclaré ».

- [ ] **Step 5 : essais manuels (critère 4)**

Ne pas lancer l'application soi-même. Construire `build` et `build-asan-sp1`, puis demander à l'utilisateur de dérouler EM-1 à EM-14 et EM-16 à EM-18 de la spec (§3), en indiquant la commande de lancement :
- `./build/src/gtkterm` ;
- `LANG=fr_FR.UTF-8 ./build/src/gtkterm` pour EM-16 et EM-17 ;
- `$S/src/gtkterm` (version ASan) pour EM-1 et EM-2.

Consigner chaque réponse. Tout écart est un défaut à traiter avant la fusion.
