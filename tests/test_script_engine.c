/* Tests d'intégration du vrai moteur Lua (script_engine.c) sans interface : bouchons pour la liaison (envois, signal
 * « received ») et get_shortcuts. Le thread principal fait tourner une GMainLoop pendant le script. */

#include <glib.h>
#include <glib/gstdio.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pty.h>
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
	guint avant;

	/* Le contexte par défaut crée son eventfd au premier usage : on le fait avant de compter, pour ne mesurer que les ports. */
	g_main_context_default();
	avant = compter_descripteurs();
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
	g_assert_cmpint(stub_last_origin(), ==, GT_SEND_LUA);
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
	g_test_add_func("/script_engine/exemple-10-mise-a-jour", test_exemple_10_mise_a_jour);
	return g_test_run();
}
