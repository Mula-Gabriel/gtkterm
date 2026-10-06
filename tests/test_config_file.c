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
