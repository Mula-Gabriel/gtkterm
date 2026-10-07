/* Tests de config_file.c : préparation du chemin de .gtktermrc (M13) ; profils et groupes réservés : test_config_store.c. */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config_file.h"
#include "test_util.h"

static gchar *
lire(const gchar *chemin)
{
	gchar *contenu = NULL;

	g_assert_true(g_file_get_contents(chemin, &contenu, NULL, NULL));
	return contenu;
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

/* Ancien $HOME/.gtktermrc qu'on ne peut pas déplacer (répertoire d'origine en lecture seule : rename refusé) : la copie
 * créée au nouvel emplacement est en 0600, quel que soit le umask, comme les écritures du magasin. */
static void
test_preparation_copie_en_0600(void)
{
	gchar *base, *home, *conf, *ancien, *chemin, *contenu;
	struct stat st;
	mode_t ancien_umask;

	if(geteuid() == 0)
	{
		g_test_skip("root ignore les droits du repertoire");
		return;
	}
	base = g_dir_make_tmp("gtkterm-test-XXXXXX", NULL);
	home = g_build_filename(base, "home", NULL);
	conf = g_build_filename(base, "conf", NULL);
	g_assert_cmpint(g_mkdir(home, 0700), ==, 0);
	ancien = test_write_file(home, ".gtktermrc", "[default]\nport\t= /dev/ttyS0\n");
	g_assert_cmpint(g_chmod(home, 0500), ==, 0);
	ancien_umask = umask(022);
	chemin = config_file_prepare(conf, home);
	umask(ancien_umask);
	g_assert_cmpint(g_chmod(home, 0700), ==, 0);

	contenu = lire(chemin);
	g_assert_cmpstr(contenu, ==, "[default]\nport\t= /dev/ttyS0\n");
	g_assert_cmpint(g_stat(chemin, &st), ==, 0);
	g_assert_cmpint(st.st_mode & 0777, ==, 0600);

	test_rmdir(conf);
	test_rmdir(home);
	g_rmdir(base);
	g_free(contenu);
	g_free(chemin);
	g_free(ancien);
	g_free(conf);
	g_free(home);
	g_free(base);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/config_file/preparation-cree-le-repertoire-et-migre", test_preparation_cree_le_repertoire_et_migre);
	g_test_add_func("/config_file/preparation-copie-en-0600", test_preparation_copie_en_0600);
	return g_test_run();
}
