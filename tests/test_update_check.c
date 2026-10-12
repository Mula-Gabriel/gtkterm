/* Tests de update_check.c (cœur) : commande du script en liste d'arguments (M34), tag compris, et recherche du terminal. */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "update_check.h"

/* Valeurs piégées : apostrophe, espaces, non-ASCII, séparateur de commandes et substitution ; un shell qui les interpréterait créerait « pirate* » */
#define URL_PIEGE "file:///srv/d\xc3\xa9p\xc3\xb4t l'essai/x;touch pirate;$(touch pirate2).git"
#define PREFIX_PIEGE "/opt/mon prefix'"

static const gchar *const terminaux[] = {"xterm", "x-terminal-emulator", "gnome-terminal", "xfce4-terminal", "konsole", "lxterminal", "qterminal", "urxvt", "kitty"};

static void
test_terminal_argv(void)
{
	for(gsize t = 0; t < G_N_ELEMENTS(terminaux); t++)
	{
		gchar **argv = gt_update_terminal_argv(terminaux[t], "/usr/share/gtkterm/gtkterm-update.sh", URL_PIEGE, PREFIX_PIEGE, 4242, "v2.1.0");
		guint n;

		g_assert_nonnull(argv);
		n = g_strv_length(argv);
		g_assert_cmpuint(n, >=, 9);
		g_assert_cmpstr(argv[0], ==, terminaux[t]);
		g_assert_cmpstr(argv[n - 8], ==, "sh");
		g_assert_cmpstr(argv[n - 7], ==, "-c");
		g_assert_cmpstr(argv[n - 5], ==, "/usr/share/gtkterm/gtkterm-update.sh");
		g_assert_cmpstr(argv[n - 4], ==, URL_PIEGE);
		g_assert_cmpstr(argv[n - 3], ==, PREFIX_PIEGE);
		g_assert_cmpstr(argv[n - 2], ==, "4242");
		g_assert_cmpstr(argv[n - 1], ==, "v2.1.0");
		/* Aucune valeur recopiée ailleurs : pas de commande construite par chaîne (M34) */
		for(guint i = 0; i < n - 4; i++)
		{
			g_assert_null(strstr(argv[i], "essai"));
			g_assert_null(strstr(argv[i], "mon prefix"));
		}
		g_strfreev(argv);
	}
}

static void
test_terminal_argv_drapeaux(void)
{
	gchar **argv;
	guint n;

	argv = gt_update_terminal_argv("xfce4-terminal", "/s.sh", "u", "/p", 1, "v1.0.0");
	n = g_strv_length(argv);
	g_assert_cmpstr(argv[n - 9], ==, "-x");
	g_strfreev(argv);
	argv = gt_update_terminal_argv("gnome-terminal", "/s.sh", "u", "/p", 1, "v1.0.0");
	n = g_strv_length(argv);
	g_assert_cmpstr(argv[n - 9], ==, "--");
	g_strfreev(argv);
	argv = gt_update_terminal_argv("kitty", "/s.sh", "u", "/p", 1, "v1.0.0");
	n = g_strv_length(argv);
	g_assert_cmpstr(argv[n - 9], ==, "font_size=14");
	g_strfreev(argv);
	/* Chemin complet : reconnu par son nom de base, argv[0] gardé tel quel */
	argv = gt_update_terminal_argv("/usr/bin/konsole", "/s.sh", "u", "/p", 1, "v1.0.0");
	g_assert_nonnull(argv);
	g_assert_cmpstr(argv[0], ==, "/usr/bin/konsole");
	g_assert_cmpstr(argv[1], ==, "-e");
	g_strfreev(argv);
	g_assert_null(gt_update_terminal_argv("terminal-inconnu", "/s.sh", "u", "/p", 1, "v1.0.0"));
	g_assert_null(gt_update_terminal_argv("xterm", "/s.sh", NULL, "/p", 1, "v1.0.0"));
	g_assert_null(gt_update_terminal_argv(NULL, "/s.sh", "u", "/p", 1, "v1.0.0"));
	g_assert_null(gt_update_terminal_argv("xterm", "/s.sh", "u", "/p", 1, NULL));
	g_assert_null(gt_update_terminal_argv("xterm", "/s.sh", "u", "/p", 1, "v1.0;rm -rf x"));
}

/* Supprime un arbre de fichiers */
static void
supprimer_arbre(const gchar *path)
{
	GDir *d = NULL;

	if(!g_file_test(path, G_FILE_TEST_IS_SYMLINK) && g_file_test(path, G_FILE_TEST_IS_DIR))
		d = g_dir_open(path, 0, NULL);
	if(d != NULL)
	{
		const gchar *name;

		while((name = g_dir_read_name(d)) != NULL)
		{
			gchar *child = g_build_filename(path, name, NULL);

			supprimer_arbre(child);
			g_free(child);
		}
		g_dir_close(d);
		g_rmdir(path);
	}
	else
		g_unlink(path);
}

static void
test_terminal_argv_sans_shell(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-update-XXXXXX", NULL);
	gchar *script = g_build_filename(dir, "faux-script.sh", NULL);
	gchar *sortie = g_build_filename(dir, "args.txt", NULL);
	gchar *pirate = g_build_filename(dir, "pirate", NULL);
	gchar *pirate2 = g_build_filename(dir, "pirate2", NULL);
	gchar *attendu = g_strdup_printf("%s\n%s\n4242\nv2.1.0\n", URL_PIEGE, PREFIX_PIEGE);
	gchar *lu = NULL;
	gchar **argv;
	guint n;
	gint status = 0;
	GError *error = NULL;

	g_assert_true(g_file_set_contents(script, "printf '%s\\n' \"$@\" > \"$(dirname \"$0\")/args.txt\"\n", -1, NULL));
	argv = gt_update_terminal_argv("xterm", script, URL_PIEGE, PREFIX_PIEGE, 4242, "v2.1.0");
	n = g_strv_length(argv);
	/* La commande que le terminal lancerait (« sh -c … » et ses arguments), sans terminal ; entrée /dev/null : « read _ » rend la main aussitôt */
	g_assert_true(g_spawn_sync(dir, argv + n - 8, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
	                           NULL, NULL, NULL, NULL, &status, &error));
	g_assert_no_error(error);
	g_assert_true(g_file_get_contents(sortie, &lu, NULL, NULL));
	g_assert_cmpstr(lu, ==, attendu);
	g_assert_false(g_file_test(pirate, G_FILE_TEST_EXISTS));
	g_assert_false(g_file_test(pirate2, G_FILE_TEST_EXISTS));
	g_strfreev(argv);
	g_free(lu);
	g_free(attendu);
	g_free(pirate);
	g_free(pirate2);
	g_free(sortie);
	g_free(script);
	supprimer_arbre(dir);
	g_free(dir);
}

static void
ecrire_executable(const gchar *dir, const gchar *name)
{
	gchar *path = g_build_filename(dir, name, NULL);

	g_assert_true(g_file_set_contents(path, "#!/bin/sh\nexit 0\n", -1, NULL));
	g_assert_cmpint(g_chmod(path, 0755), ==, 0);
	g_free(path);
}

static void
test_find_terminal(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-path-XXXXXX", NULL);
	gchar *ancien = g_strdup(g_getenv("PATH"));
	gchar *t;

	g_setenv("PATH", dir, TRUE);
	t = gt_update_find_terminal();
	g_assert_null(t);
	ecrire_executable(dir, "kitty");
	ecrire_executable(dir, "konsole");
	t = gt_update_find_terminal();
	/* Ordre de la table : konsole avant kitty */
	g_assert_cmpstr(t, ==, "konsole");
	g_free(t);
	if(ancien != NULL)
		g_setenv("PATH", ancien, TRUE);
	else
		g_unsetenv("PATH");
	g_free(ancien);
	supprimer_arbre(dir);
	g_free(dir);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/update-check/terminal-argv/valeurs-piegees", test_terminal_argv);
	g_test_add_func("/update-check/terminal-argv/drapeaux", test_terminal_argv_drapeaux);
	g_test_add_func("/update-check/terminal-argv/sans-shell", test_terminal_argv_sans_shell);
	g_test_add_func("/update-check/find-terminal", test_find_terminal);
	return g_test_run();
}
