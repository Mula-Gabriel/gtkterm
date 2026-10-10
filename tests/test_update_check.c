/* Tests de update_check.c (cœur, sous-projet 6a) : comparaison de révisions (M33), révision ignorée (M34), commande du script en liste
 * d'arguments (M34), « git ls-remote » asynchrone sur un dépôt local, révision locale plus récente d'après le dépôt cache. */

#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdarg.h>
#include <string.h>

#include "update_check.h"

#define A40 "1111111111111111111111111111111111111111"
#define B40 "2222222222222222222222222222222222222222"
#define C40_MIN "abcdefabcdefabcdefabcdefabcdefabcdefabcd"
#define C40_MAJ "ABCDEFABCDEFABCDEFABCDEFABCDEFABCDEFABCD"
/* Valeurs piégées : apostrophe, espaces, non-ASCII, séparateur de commandes et substitution ; un shell qui les interpréterait créerait « pirate* » */
#define URL_PIEGE "file:///srv/d\xc3\xa9p\xc3\xb4t l'essai/x;touch pirate;$(touch pirate2).git"
#define PREFIX_PIEGE "/opt/mon prefix'"

typedef struct
{
	const gchar *nom;
	const gchar *local;
	const gchar *remote;
	const gchar *skipped;
	gboolean newer;
	GtUpdateVerdict attendu;
} CasCompare;

static const CasCompare cas_compare[] = {
	{"egales", A40, A40, NULL, FALSE, GT_UPDATE_UP_TO_DATE},
	{"egales sans casse", C40_MIN, C40_MAJ, NULL, FALSE, GT_UPDATE_UP_TO_DATE},
	{"differentes", A40, B40, NULL, FALSE, GT_UPDATE_AVAILABLE},
	{"locale plus recente (M33)", A40, B40, NULL, TRUE, GT_UPDATE_UP_TO_DATE},
	{"distante ignoree (M34)", A40, B40, B40, FALSE, GT_UPDATE_SKIPPED},
	{"ignoree sans casse", A40, C40_MIN, C40_MAJ, FALSE, GT_UPDATE_SKIPPED},
	{"autre revision ignoree", A40, B40, A40, FALSE, GT_UPDATE_AVAILABLE},
	{"ignoree vide", A40, B40, "", FALSE, GT_UPDATE_AVAILABLE},
	{"plus recente avant ignoree", A40, B40, B40, TRUE, GT_UPDATE_UP_TO_DATE},
	{"locale unknown", "unknown", B40, NULL, FALSE, GT_UPDATE_LOCAL_UNKNOWN},
	{"unknown prime sur tout", "unknown", B40, B40, TRUE, GT_UPDATE_LOCAL_UNKNOWN},
	{"locale NULL", NULL, B40, NULL, FALSE, GT_UPDATE_LOCAL_UNKNOWN},
	{"locale vide", "", B40, NULL, FALSE, GT_UPDATE_LOCAL_UNKNOWN},
	{"locale courte valide", "1111111", B40, NULL, FALSE, GT_UPDATE_AVAILABLE},
	{"distante absente", A40, NULL, NULL, FALSE, GT_UPDATE_UP_TO_DATE},
	{"distante invalide", A40, "pas-un-sha", NULL, FALSE, GT_UPDATE_UP_TO_DATE},
};

static void
test_compare(void)
{
	for(gsize i = 0; i < G_N_ELEMENTS(cas_compare); i++)
	{
		const CasCompare *c = &cas_compare[i];
		GtUpdateVerdict v = gt_update_compare(c->local, c->remote, c->skipped, c->newer);

		if(v != c->attendu)
			g_test_fail_printf("%s : verdict %d au lieu de %d", c->nom, v, c->attendu);
	}
}

static const gchar *const terminaux[] = {"xterm", "x-terminal-emulator", "gnome-terminal", "xfce4-terminal", "konsole", "lxterminal", "qterminal", "urxvt", "kitty"};

static void
test_terminal_argv(void)
{
	for(gsize t = 0; t < G_N_ELEMENTS(terminaux); t++)
	{
		gchar **argv = gt_update_terminal_argv(terminaux[t], "/usr/share/gtkterm/gtkterm-update.sh", URL_PIEGE, PREFIX_PIEGE, 4242);
		guint n;

		g_assert_nonnull(argv);
		n = g_strv_length(argv);
		g_assert_cmpuint(n, >=, 8);
		g_assert_cmpstr(argv[0], ==, terminaux[t]);
		g_assert_cmpstr(argv[n - 7], ==, "sh");
		g_assert_cmpstr(argv[n - 6], ==, "-c");
		g_assert_cmpstr(argv[n - 4], ==, "/usr/share/gtkterm/gtkterm-update.sh");
		g_assert_cmpstr(argv[n - 3], ==, URL_PIEGE);
		g_assert_cmpstr(argv[n - 2], ==, PREFIX_PIEGE);
		g_assert_cmpstr(argv[n - 1], ==, "4242");
		/* Aucune valeur recopiée ailleurs : pas de commande construite par chaîne (M34) */
		for(guint i = 0; i < n - 3; i++)
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

	argv = gt_update_terminal_argv("xfce4-terminal", "/s.sh", "u", "/p", 1);
	n = g_strv_length(argv);
	g_assert_cmpstr(argv[n - 8], ==, "-x");
	g_strfreev(argv);
	argv = gt_update_terminal_argv("gnome-terminal", "/s.sh", "u", "/p", 1);
	n = g_strv_length(argv);
	g_assert_cmpstr(argv[n - 8], ==, "--");
	g_strfreev(argv);
	argv = gt_update_terminal_argv("kitty", "/s.sh", "u", "/p", 1);
	n = g_strv_length(argv);
	g_assert_cmpstr(argv[n - 8], ==, "font_size=14");
	g_strfreev(argv);
	/* Chemin complet : reconnu par son nom de base, argv[0] gardé tel quel */
	argv = gt_update_terminal_argv("/usr/bin/konsole", "/s.sh", "u", "/p", 1);
	g_assert_nonnull(argv);
	g_assert_cmpstr(argv[0], ==, "/usr/bin/konsole");
	g_assert_cmpstr(argv[1], ==, "-e");
	g_strfreev(argv);
	g_assert_null(gt_update_terminal_argv("terminal-inconnu", "/s.sh", "u", "/p", 1));
	g_assert_null(gt_update_terminal_argv("xterm", "/s.sh", NULL, "/p", 1));
	g_assert_null(gt_update_terminal_argv(NULL, "/s.sh", "u", "/p", 1));
}

/* Supprime un arbre de fichiers (dépôt git de test) */
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
	gchar *attendu = g_strdup_printf("%s\n%s\n4242\n", URL_PIEGE, PREFIX_PIEGE);
	gchar *lu = NULL;
	gchar **argv;
	guint n;
	gint status = 0;
	GError *error = NULL;

	g_assert_true(g_file_set_contents(script, "printf '%s\\n' \"$@\" > \"$(dirname \"$0\")/args.txt\"\n", -1, NULL));
	argv = gt_update_terminal_argv("xterm", script, URL_PIEGE, PREFIX_PIEGE, 4242);
	n = g_strv_length(argv);
	/* La commande que le terminal lancerait (« sh -c … » et ses arguments), sans terminal ; entrée /dev/null : « read _ » rend la main aussitôt */
	g_assert_true(g_spawn_sync(dir, argv + n - 7, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
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

/* git -C dir <arguments…> ; échec du test si git échoue ; renvoie la sortie sans blancs de tête ni de fin (g_free) */
static gchar *git(const gchar *dir, ...) G_GNUC_NULL_TERMINATED;

static gchar *
git(const gchar *dir, ...)
{
	GPtrArray *argv = g_ptr_array_new();
	gchar *out = NULL;
	gint status = 0;
	GError *error = NULL;
	const gchar *arg;
	va_list ap;

	g_ptr_array_add(argv, (gpointer)"git");
	g_ptr_array_add(argv, (gpointer)"-C");
	g_ptr_array_add(argv, (gpointer)dir);
	va_start(ap, dir);
	while((arg = va_arg(ap, const gchar *)) != NULL)
		g_ptr_array_add(argv, (gpointer)arg);
	va_end(ap);
	g_ptr_array_add(argv, NULL);
	g_assert_true(g_spawn_sync(NULL, (gchar **)argv->pdata, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &out, NULL, &status, &error));
	g_assert_no_error(error);
	g_assert_true(g_spawn_check_wait_status(status, NULL));
	g_ptr_array_free(argv, TRUE);
	return g_strstrip(out);
}

typedef struct
{
	gchar *dir;
	gchar *c1;	/* premier commit */
	gchar *c2;	/* second commit (HEAD), descendant de c1 */
} Depot;

static void
depot_commit(const Depot *d, const gchar *contenu, const gchar *message)
{
	gchar *f = g_build_filename(d->dir, "a.txt", NULL);

	g_assert_true(g_file_set_contents(f, contenu, -1, NULL));
	g_free(f);
	g_free(git(d->dir, "add", "a.txt", NULL));
	g_free(git(d->dir, "-c", "user.name=Test", "-c", "user.email=test@example.invalid", "-c", "commit.gpgsign=false", "commit", "-q", "-m", message, NULL));
}

/* Petit dépôt à deux commits dans un dossier temporaire ; FALSE si git est absent */
static gboolean
depot_creer(Depot *d)
{
	gchar *prog = g_find_program_in_path("git");

	if(prog == NULL)
		return FALSE;
	g_free(prog);
	d->dir = g_dir_make_tmp("gtkterm-depot-XXXXXX", NULL);
	g_assert_nonnull(d->dir);
	g_free(git(d->dir, "init", "-q", NULL));
	depot_commit(d, "1\n", "un");
	d->c1 = git(d->dir, "rev-parse", "HEAD", NULL);
	depot_commit(d, "2\n", "deux");
	d->c2 = git(d->dir, "rev-parse", "HEAD", NULL);
	return TRUE;
}

static void
depot_liberer(Depot *d)
{
	supprimer_arbre(d->dir);
	g_free(d->dir);
	g_free(d->c1);
	g_free(d->c2);
}

static void
test_local_is_newer(void)
{
	Depot d;
	gchar *pas_depot;

	if(!depot_creer(&d))
	{
		g_test_skip("git absent");
		return;
	}
	/* Distante = ancêtre de la locale : la locale est plus récente (M33) */
	g_assert_true(gt_update_local_is_newer(d.dir, d.c2, d.c1));
	g_assert_true(gt_update_local_is_newer(d.dir, d.c2, d.c2));
	g_assert_false(gt_update_local_is_newer(d.dir, d.c1, d.c2));
	g_assert_false(gt_update_local_is_newer(d.dir, "unknown", d.c1));
	g_assert_false(gt_update_local_is_newer(d.dir, d.c2, "0123456789abcdef0123456789abcdef01234567"));
	g_assert_false(gt_update_local_is_newer(d.dir, d.c2, "--help"));
	g_assert_false(gt_update_local_is_newer(NULL, d.c2, d.c1));
	pas_depot = g_dir_make_tmp("gtkterm-pas-depot-XXXXXX", NULL);
	g_assert_false(gt_update_local_is_newer(pas_depot, d.c2, d.c1));
	supprimer_arbre(pas_depot);
	g_free(pas_depot);
	depot_liberer(&d);
}

typedef struct
{
	GMainLoop *loop;
	gchar *rev;
	GError *error;
	gboolean appele;
} Attente;

static void
on_fetch(GObject *source, GAsyncResult *res, gpointer user_data)
{
	Attente *a = user_data;

	a->rev = gt_update_fetch_remote_finish(res, &a->error);
	a->appele = TRUE;
	g_main_loop_quit(a->loop);
}

/* Lance la récupération, annule aussitôt si demandé, attend le rappel ; *error reçoit l'erreur, le résultat est renvoyé */
static gchar *
recuperer(const gchar *url, gboolean annuler, GError **error)
{
	Attente a = {g_main_loop_new(NULL, FALSE), NULL, NULL, FALSE};
	GCancellable *c = g_cancellable_new();

	gt_update_fetch_remote_async(url, c, on_fetch, &a);
	g_assert_false(a.appele);	/* jamais de rappel pendant l'appel _async */
	if(annuler)
		g_cancellable_cancel(c);
	g_main_loop_run(a.loop);
	g_main_loop_unref(a.loop);
	g_object_unref(c);
	if(a.error != NULL)
		g_propagate_error(error, a.error);
	return a.rev;
}

static void
test_fetch_local(void)
{
	Depot d;
	gchar *url, *rev;
	GError *error = NULL;

	if(!depot_creer(&d))
	{
		g_test_skip("git absent");
		return;
	}
	url = g_strconcat("file://", d.dir, NULL);
	rev = recuperer(url, FALSE, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(rev, ==, d.c2);
	g_free(rev);
	g_free(url);
	depot_liberer(&d);
}

static void
test_fetch_erreurs(void)
{
	gchar *dir, *pirate, *url, *rev;
	GError *error = NULL;
	gchar *prog = g_find_program_in_path("git");

	if(prog == NULL)
	{
		g_test_skip("git absent");
		return;
	}
	g_free(prog);
	rev = recuperer("file:///nonexistent/gtkterm-aucun-depot.git", FALSE, &error);
	g_assert_null(rev);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_FAILED);
	g_assert_cmpuint(strlen(error->message), >, 0);
	g_clear_error(&error);

	/* URL qui ressemble à une option : « -- » la garde comme dépôt, aucune commande n'est lancée (M34) */
	dir = g_dir_make_tmp("gtkterm-option-XXXXXX", NULL);
	pirate = g_build_filename(dir, "pirate", NULL);
	url = g_strdup_printf("--upload-pack=touch %s", pirate);
	rev = recuperer(url, FALSE, &error);
	g_assert_null(rev);
	g_assert_nonnull(error);
	g_clear_error(&error);
	g_assert_false(g_file_test(pirate, G_FILE_TEST_EXISTS));
	g_free(url);
	g_free(pirate);
	supprimer_arbre(dir);
	g_free(dir);

	rev = recuperer("", FALSE, &error);
	g_assert_null(rev);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_clear_error(&error);
}

static void
test_fetch_annule(void)
{
	Depot d;
	gchar *url, *rev;
	GError *error = NULL;

	if(!depot_creer(&d))
	{
		g_test_skip("git absent");
		return;
	}
	url = g_strconcat("file://", d.dir, NULL);
	rev = recuperer(url, TRUE, &error);
	g_assert_null(rev);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
	g_clear_error(&error);
	g_free(url);
	depot_liberer(&d);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	/* git des tests : ni la configuration globale ni celle du système (signature, crochets, branche par défaut) */
	g_setenv("GIT_CONFIG_GLOBAL", "/dev/null", TRUE);
	g_setenv("GIT_CONFIG_NOSYSTEM", "1", TRUE);
	g_test_add_func("/update-check/compare", test_compare);
	g_test_add_func("/update-check/terminal-argv/valeurs-piegees", test_terminal_argv);
	g_test_add_func("/update-check/terminal-argv/drapeaux", test_terminal_argv_drapeaux);
	g_test_add_func("/update-check/terminal-argv/sans-shell", test_terminal_argv_sans_shell);
	g_test_add_func("/update-check/find-terminal", test_find_terminal);
	g_test_add_func("/update-check/local-is-newer", test_local_is_newer);
	g_test_add_func("/update-check/fetch/depot-local", test_fetch_local);
	g_test_add_func("/update-check/fetch/erreurs", test_fetch_erreurs);
	g_test_add_func("/update-check/fetch/annulation", test_fetch_annule);
	return g_test_run();
}
