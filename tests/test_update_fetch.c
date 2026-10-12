/* Tests de update_fetch.c : dépôt amont local (file://) avec tags annotés et NEWS ; premier clone filtré, actualisation (tag ajouté, tag
 * supprimé), changement d'URL, NEWS absent, dépôt sans tag, dossier de cache vide ou occupé, erreurs d'URL, annulation. */

#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdarg.h>
#include <string.h>

#include "update_fetch.h"

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

/* git -C dir <arguments…> ; échec du test si git échoue */
static void git(const gchar *dir, ...) G_GNUC_NULL_TERMINATED;

static void
git(const gchar *dir, ...)
{
	GPtrArray *argv = g_ptr_array_new();
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
	g_assert_true(g_spawn_sync(NULL, (gchar **)argv->pdata, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL,
	                           NULL, NULL, &status, &error));
	g_assert_no_error(error);
	g_assert_true(g_spawn_check_wait_status(status, NULL));
	g_ptr_array_free(argv, TRUE);
}

/* Commit (NEWS = news, ou NEWS supprimé si news == NULL) puis tag annoté (tag != NULL) */
static void
version(const gchar *dir, const gchar *news, const gchar *tag)
{
	gchar *f = g_build_filename(dir, "NEWS", NULL);
	gchar *autre = g_build_filename(dir, "fichier.txt", NULL);
	static guint n;
	gchar *contenu = g_strdup_printf("%u\n", ++n);

	if(news != NULL)
		g_assert_true(g_file_set_contents(f, news, -1, NULL));
	else
		g_unlink(f);
	g_assert_true(g_file_set_contents(autre, contenu, -1, NULL));
	git(dir, "add", "-A", NULL);
	git(dir, "-c", "user.name=Test", "-c", "user.email=test@example.invalid", "-c", "commit.gpgsign=false", "commit", "-q", "-m", "version", NULL);
	if(tag != NULL)
		git(dir, "-c", "user.name=Test", "-c", "user.email=test@example.invalid", "-c", "tag.gpgsign=false", "tag", "-a", tag, "-m", tag, NULL);
	g_free(contenu);
	g_free(autre);
	g_free(f);
}

static gchar *
depot_nouveau(const gchar *racine, const gchar *nom)
{
	gchar *dir = g_build_filename(racine, nom, NULL);

	g_assert_cmpint(g_mkdir_with_parents(dir, 0700), ==, 0);
	git(dir, "init", "-q", NULL);
	return dir;
}

typedef struct
{
	GMainLoop *loop;
	GtReleases *r;
	GError *error;
	gboolean appele;
} Attente;

static void
on_fetch(G_GNUC_UNUSED GObject *source, GAsyncResult *res, gpointer user_data)
{
	Attente *a = user_data;

	a->r = gt_releases_fetch_finish(res, &a->error);
	a->appele = TRUE;
	g_main_loop_quit(a->loop);
}

static GtReleases *
recuperer(const gchar *url, const gchar *cache, gboolean annuler, GError **error)
{
	Attente a = {g_main_loop_new(NULL, FALSE), NULL, NULL, FALSE};
	GCancellable *c = g_cancellable_new();

	gt_releases_fetch_async(url, cache, c, on_fetch, &a);
	g_assert_false(a.appele);	/* jamais de rappel pendant l'appel _async */
	if(annuler)
		g_cancellable_cancel(c);
	g_main_loop_run(a.loop);
	g_main_loop_unref(a.loop);
	g_object_unref(c);
	if(a.error != NULL)
		g_propagate_error(error, a.error);
	return a.r;
}

static void
tags_attendus(GtReleases *r, const gchar *const *tags)
{
	guint n = g_strv_length((gchar **)tags);

	g_assert_nonnull(r);
	g_assert_cmpuint(r->tags->len, ==, n);
	for(guint i = 0; i < n; i++)
		g_assert_cmpstr(g_ptr_array_index(r->tags, i), ==, tags[i]);
}

static gboolean
git_present(void)
{
	gchar *prog = g_find_program_in_path("git");

	g_free(prog);
	return prog != NULL;
}

static void
test_actualisation(void)
{
	gchar *racine, *amont, *amont2, *cache, *head, *url, *url2;
	GtReleases *r;
	GError *error = NULL;
	static const gchar *const t1[] = {"v1.1.0", "v1.0.0", NULL};
	static const gchar *const t2[] = {"v1.2.0", "v1.1.0", NULL};
	static const gchar *const t3[] = {"v5.0.0", NULL};

	if(!git_present())
	{
		g_test_skip("git absent");
		return;
	}
	racine = g_dir_make_tmp("gtkterm-fetch-XXXXXX", NULL);
	amont = depot_nouveau(racine, "amont");
	version(amont, "## v1.0.0\n- un\n", "v1.0.0");
	version(amont, "## v1.1.0\n- deux\n## v1.0.0\n- un\n", "v1.1.0");
	git(amont, "tag", "essai", NULL);	/* tag léger qui n'est pas une version */
	cache = g_build_filename(racine, "cache", "gtkterm", "versions.git", NULL);	/* parents absents */
	url = g_strconcat("file://", amont, NULL);

	/* 1. Premier passage : clone filtré, tags de versions seulement, NEWS du plus récent */
	r = recuperer(url, cache, FALSE, &error);
	g_assert_no_error(error);
	tags_attendus(r, t1);
	g_assert_cmpstr(r->news, ==, "## v1.1.0\n- deux\n## v1.0.0\n- un\n");
	gt_releases_free(r);
	head = g_build_filename(cache, "HEAD", NULL);
	g_assert_true(g_file_test(head, G_FILE_TEST_EXISTS));

	/* 2. Actualisation : tag ajouté, tag supprimé en amont */
	version(amont, "## v1.2.0\n! attention\n", "v1.2.0");
	git(amont, "tag", "-d", "v1.0.0", NULL);
	r = recuperer(url, cache, FALSE, &error);
	g_assert_no_error(error);
	tags_attendus(r, t2);
	g_assert_cmpstr(r->news, ==, "## v1.2.0\n! attention\n");
	gt_releases_free(r);

	/* 3. Changement d'URL : seuls les tags de la nouvelle source restent */
	amont2 = depot_nouveau(racine, "amont2");
	version(amont2, "## v5.0.0\n- autre\n", "v5.0.0");
	url2 = g_strconcat("file://", amont2, NULL);
	r = recuperer(url2, cache, FALSE, &error);
	g_assert_no_error(error);
	tags_attendus(r, t3);
	gt_releases_free(r);

	/* 4. Tag le plus récent sans NEWS : pas une erreur */
	version(amont2, NULL, "v6.0.0");
	r = recuperer(url2, cache, FALSE, &error);
	g_assert_no_error(error);
	g_assert_cmpuint(r->tags->len, ==, 2);
	g_assert_null(r->news);
	gt_releases_free(r);

	g_free(url2);
	g_free(amont2);
	g_free(head);
	g_free(url);
	g_free(cache);
	g_free(amont);
	supprimer_arbre(racine);
	g_free(racine);
}

static void
test_cas_limites(void)
{
	gchar *racine, *amont, *vide, *occupe, *bidon, *url, *pirate, *opt;
	GtReleases *r;
	GError *error = NULL;
	static const gchar *const aucun[] = {NULL};
	static const gchar *const un[] = {"v1.0.0", NULL};

	if(!git_present())
	{
		g_test_skip("git absent");
		return;
	}
	racine = g_dir_make_tmp("gtkterm-fetch-XXXXXX", NULL);
	amont = depot_nouveau(racine, "amont");
	version(amont, "## v1.0.0\n- un\n", NULL);	/* aucun tag */
	url = g_strconcat("file://", amont, NULL);

	/* Dépôt sans tag : liste vide, pas de NEWS, pas d'erreur */
	vide = g_build_filename(racine, "cache-vide", NULL);
	g_assert_cmpint(g_mkdir_with_parents(vide, 0700), ==, 0);	/* dossier présent mais vide : le clone y va */
	r = recuperer(url, vide, FALSE, &error);
	g_assert_no_error(error);
	tags_attendus(r, aucun);
	g_assert_null(r->news);
	gt_releases_free(r);
	git(amont, "-c", "user.name=Test", "-c", "user.email=test@example.invalid", "-c", "tag.gpgsign=false", "tag", "-a", "v1.0.0", "-m", "v1.0.0", NULL);
	r = recuperer(url, vide, FALSE, &error);
	g_assert_no_error(error);
	tags_attendus(r, un);
	gt_releases_free(r);

	/* Dossier de cache occupé par autre chose qu'un dépôt : erreur lisible, rien d'écrasé */
	occupe = g_build_filename(racine, "cache-occupe", NULL);
	bidon = g_build_filename(occupe, "a-garder.txt", NULL);
	g_assert_cmpint(g_mkdir_with_parents(occupe, 0700), ==, 0);
	g_assert_true(g_file_set_contents(bidon, "x", -1, NULL));
	r = recuperer(url, occupe, FALSE, &error);
	g_assert_null(r);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_FAILED);
	g_assert_cmpuint(strlen(error->message), >, 0);
	g_clear_error(&error);
	g_assert_true(g_file_test(bidon, G_FILE_TEST_EXISTS));

	/* Source injoignable */
	r = recuperer("file:///nonexistent/gtkterm-aucun-depot.git", vide, FALSE, &error);
	g_assert_null(r);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_FAILED);
	g_clear_error(&error);

	/* URL vide, ou qui ressemble à une option : refusées avant git, aucune commande lancée */
	r = recuperer("", vide, FALSE, &error);
	g_assert_null(r);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_clear_error(&error);
	pirate = g_build_filename(racine, "pirate", NULL);
	opt = g_strdup_printf("--upload-pack=touch %s", pirate);
	r = recuperer(opt, vide, FALSE, &error);
	g_assert_null(r);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_clear_error(&error);
	g_assert_false(g_file_test(pirate, G_FILE_TEST_EXISTS));

	/* Annulation aussitôt après le lancement */
	r = recuperer(url, vide, TRUE, &error);
	g_assert_null(r);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
	g_clear_error(&error);

	g_free(opt);
	g_free(pirate);
	g_free(bidon);
	g_free(occupe);
	g_free(vide);
	g_free(url);
	g_free(amont);
	supprimer_arbre(racine);
	g_free(racine);
}

/* Cache sans dépôt (HEAD vide) dans un arbre git : jamais d'action sur le dépôt parent */
static void
test_cache_non_depot(void)
{
	gchar *racine, *parent, *cache, *head, *url, *amont, *sortie = NULL;
	GtReleases *r;
	GError *error = NULL;
	gchar *argv[] = {(gchar *)"git", (gchar *)"-C", NULL, (gchar *)"remote", NULL};

	if(!git_present())
	{
		g_test_skip("git absent");
		return;
	}
	racine = g_dir_make_tmp("gtkterm-fetch-XXXXXX", NULL);
	amont = depot_nouveau(racine, "amont");
	version(amont, "## v1.0.0\n- un\n", "v1.0.0");
	url = g_strconcat("file://", amont, NULL);
	parent = depot_nouveau(racine, "parent");
	cache = g_build_filename(parent, "cache", NULL);
	g_assert_cmpint(g_mkdir_with_parents(cache, 0700), ==, 0);
	head = g_build_filename(cache, "HEAD", NULL);
	g_assert_true(g_file_set_contents(head, "", -1, NULL));
	r = recuperer(url, cache, FALSE, &error);
	g_assert_null(r);
	g_assert_nonnull(error);
	g_clear_error(&error);
	argv[2] = parent;
	g_assert_true(g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &sortie, NULL, NULL, &error));
	g_assert_no_error(error);
	g_assert_cmpstr(sortie, ==, "");
	g_free(sortie);
	g_free(head);
	g_free(cache);
	g_free(parent);
	g_free(url);
	g_free(amont);
	supprimer_arbre(racine);
	g_free(racine);
}

static void
test_cache_dir(void)
{
	gchar *d = gt_releases_cache_dir();

	g_assert_true(g_str_has_suffix(d, "/gtkterm/versions.git"));
	g_free(d);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	/* git des tests : ni la configuration globale ni celle du système (signature, crochets, branche par défaut) */
	g_setenv("GIT_CONFIG_GLOBAL", "/dev/null", TRUE);
	g_setenv("GIT_CONFIG_NOSYSTEM", "1", TRUE);
	g_test_add_func("/update-fetch/actualisation", test_actualisation);
	g_test_add_func("/update-fetch/cas-limites", test_cas_limites);
	g_test_add_func("/update-fetch/cache-non-depot", test_cache_non_depot);
	g_test_add_func("/update-fetch/cache-dir", test_cache_dir);
	return g_test_run();
}
