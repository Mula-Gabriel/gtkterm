/* Tests de parsecfg.c (format INI multi-profils de .gtktermrc) : lecture, aller-retour,
 * locale, fichier vide, cas limites. Les fichiers sont écrits dans un répertoire temporaire. */

#include <glib.h>
#include <glib/gstdio.h>
#include <locale.h>
#include <string.h>

#include "parsecfg.h"
#include "test_util.h"

static gchar **port;
static guint *speed;
static gint *bits;
static gint *echo;
static gfloat *red;

static cfgStruct cfg[] =
{
	{"port", CFG_STRING, &port},
	{"speed", CFG_UINT, &speed},
	{"bits", CFG_INT, &bits},
	{"echo", CFG_BOOL, &echo},
	{"red", CFG_FLOAT, &red},
	{NULL, CFG_END, NULL}
};

static const gchar *fichier_deux_profils =
	"[default]\n"
	"port\t= /dev/ttyS0\n"
	"speed\t= 115200\n"
	"bits\t= 8\n"
	"echo\t= True\n"
	"red\t= 0.5\n"
	"\n"
	"[usb]\n"
	"port = \"/dev/my port\"\n"
	"speed = 9600\n"
	"bits = 7\n"
	"echo = False\n"
	"inconnue = 3\n";

static gchar *
nouveau_repertoire(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-test-XXXXXX", NULL);

	g_assert_nonnull(dir);
	return dir;
}

static void
test_lecture(void)
{
	gchar *dir = nouveau_repertoire();
	gchar *chemin = test_write_file(dir, "a.ini", fichier_deux_profils);

	g_assert_cmpint(cfgParse(chemin, cfg, CFG_INI), ==, 2);
	g_assert_cmpstr(cfgSectionNumberToName(0), ==, "default");
	g_assert_cmpstr(cfgSectionNumberToName(1), ==, "usb");
	g_assert_cmpstr(port[0], ==, "/dev/ttyS0");
	g_assert_cmpstr(port[1], ==, "/dev/my port");
	g_assert_cmpuint(speed[0], ==, 115200);
	g_assert_cmpuint(speed[1], ==, 9600);
	g_assert_cmpint(bits[0], ==, 8);
	g_assert_cmpint(bits[1], ==, 7);
	g_assert_cmpint(echo[0], ==, 1);
	g_assert_cmpint(echo[1], ==, 0);
	g_assert_cmpfloat_with_epsilon(red[0], 0.5, 1e-6);

	g_free(chemin);
	test_rmdir(dir);
	g_free(dir);
}

static void
test_aller_retour(void)
{
	gchar *dir = nouveau_repertoire();
	gchar *source = test_write_file(dir, "a.ini", fichier_deux_profils);
	gchar *copie = g_build_filename(dir, "b.ini", NULL);
	gint idx;

	g_assert_cmpint(cfgParse(source, cfg, CFG_INI), ==, 2);
	idx = cfgAllocForNewSection(cfg, "nouveau") - 1;
	g_assert_cmpint(idx, ==, 2);
	g_assert_cmpint(cfgStoreValue(cfg, "port", "/dev/x y", CFG_INI, idx), ==, CFG_NO_ERROR);
	g_assert_cmpint(cfgStoreValue(cfg, "speed", "57600", CFG_INI, idx), ==, CFG_NO_ERROR);
	g_assert_cmpint(cfgStoreValue(cfg, "bits", "5", CFG_INI, idx), ==, CFG_NO_ERROR);
	g_assert_cmpint(cfgStoreValue(cfg, "echo", "yes", CFG_INI, idx), ==, CFG_NO_ERROR);
	g_assert_cmpint(cfgStoreValue(cfg, "red", "0.25", CFG_INI, idx), ==, CFG_NO_ERROR);
	g_assert_cmpint(cfgDump(copie, cfg, CFG_INI, idx + 1), ==, 0);

	g_assert_cmpint(cfgParse(copie, cfg, CFG_INI), ==, 3);
	g_assert_cmpstr(cfgSectionNumberToName(2), ==, "nouveau");
	g_assert_cmpstr(port[1], ==, "/dev/my port");
	g_assert_cmpint(echo[1], ==, 0);
	g_assert_cmpstr(port[2], ==, "/dev/x y");
	g_assert_cmpuint(speed[2], ==, 57600);
	g_assert_cmpint(bits[2], ==, 5);
	g_assert_cmpint(echo[2], ==, 1);
	g_assert_cmpfloat_with_epsilon(red[2], 0.25, 1e-6);

	g_free(copie);
	g_free(source);
	test_rmdir(dir);
	g_free(dir);
}

static void
test_booleen_non_renseigne(void)
{
	/* Comportement actuel, à décider : un booléen jamais renseigné vaut -1 et s'écrit « True ». */
	gchar *dir = nouveau_repertoire();
	gchar *source = test_write_file(dir, "a.ini", fichier_deux_profils);
	gchar *copie = g_build_filename(dir, "b.ini", NULL);
	gint idx;

	g_assert_cmpint(cfgParse(source, cfg, CFG_INI), ==, 2);
	idx = cfgAllocForNewSection(cfg, "nouveau") - 1;
	g_assert_cmpint(cfgStoreValue(cfg, "port", "/dev/ttyUSB0", CFG_INI, idx), ==, CFG_NO_ERROR);
	g_assert_cmpint(cfgDump(copie, cfg, CFG_INI, idx + 1), ==, 0);
	g_assert_cmpint(cfgParse(copie, cfg, CFG_INI), ==, 3);
	g_assert_cmpint(echo[idx], ==, 1);
	g_assert_cmpuint(speed[idx], ==, 0);

	g_free(copie);
	g_free(source);
	test_rmdir(dir);
	g_free(dir);
}

static void
test_locale_preservee(void)
{
	gchar *dir;
	gchar *source;
	gchar *copie;
	const gchar *apres;
	gchar *detail;

	if(setlocale(LC_ALL, "C.UTF-8") == NULL)
	{
		g_test_skip("locale C.UTF-8 indisponible");
		return;
	}
	dir = nouveau_repertoire();
	source = test_write_file(dir, "a.ini", fichier_deux_profils);
	copie = g_build_filename(dir, "b.ini", NULL);
	g_assert_cmpint(cfgParse(source, cfg, CFG_INI), ==, 2);
	g_assert_cmpint(cfgDump(copie, cfg, CFG_INI, 2), ==, 0);

	apres = setlocale(LC_ALL, NULL);
	detail = g_strdup_printf("locale apres cfgParse/cfgDump : attendu C.UTF-8, obtenu %s", apres);
	test_conclude(g_strcmp0(apres, "C.UTF-8") == 0, "M9", detail);

	g_free(detail);
	setlocale(LC_ALL, "C");
	g_free(copie);
	g_free(source);
	test_rmdir(dir);
	g_free(dir);
}

#define VAR_FICHIER_VIDE "GTKTERM_TEST_FICHIER_VIDE"

static void
test_fichier_vide_puis_ecriture(void)
{
	gchar *dir;
	gchar *chemin;

	if(g_test_subprocess())
	{
		/* Processus neuf : les tableaux de cfg[] sont encore NULL, comme au premier démarrage de GTKTerm. */
		g_assert_cmpint(cfgParse(g_getenv(VAR_FICHIER_VIDE), cfg, CFG_INI), ==, 0);
		cfgStoreValue(cfg, "port", "/dev/ttyS0", CFG_INI, 0);
		return;
	}
	dir = nouveau_repertoire();
	chemin = test_write_file(dir, "vide.ini", "");
	g_setenv(VAR_FICHIER_VIDE, chemin, TRUE);
	g_test_trap_subprocess(NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
	test_conclude(g_test_trap_has_passed(), "C7", "cfgStoreValue apres la lecture d'un fichier vide ne doit pas planter");
	g_unsetenv(VAR_FICHIER_VIDE);
	g_free(chemin);
	test_rmdir(dir);
	g_free(dir);
}

static void
test_sections_differant_par_la_casse(void)
{
	/* Comportement actuel conservé : parsecfg refuse deux sections qui ne diffèrent que par la casse.
	 * Le défaut C6 vient de term_config.c (comparaison par strcmp) ; son test arrive au sous-projet 1. */
	gchar *dir = nouveau_repertoire();
	gchar *chemin = test_write_file(dir, "c.ini", "[default]\nport = /dev/a\n[Default]\nport = /dev/b\n");

	g_assert_cmpint(cfgParse(chemin, cfg, CFG_INI), ==, -1);
	g_free(chemin);
	test_rmdir(dir);
	g_free(dir);
}

static void
test_espace_sans_guillemets(void)
{
	/* Comportement actuel conservé : une valeur non entre guillemets contenant une espace est refusée.
	 * C'est la cause de C5 : GKeyFile écrit [session] script_path sans guillemets, et term_config.c
	 * oublie de retirer ce groupe avant certains appels à cfgParse. Test de C5 : sous-projet 1. */
	gchar *dir = nouveau_repertoire();
	gchar *chemin = test_write_file(dir, "d.ini", "[session]\nscript_path=/home/x/my script.lua\n");

	g_assert_cmpint(cfgParse(chemin, cfg, CFG_INI), ==, -1);
	g_free(chemin);
	test_rmdir(dir);
	g_free(dir);
}

static void
test_fichier_absent(void)
{
	gchar *dir = nouveau_repertoire();
	gchar *chemin = g_build_filename(dir, "absent.ini", NULL);

	g_assert_cmpint(cfgParse(chemin, cfg, CFG_INI), ==, -1);
	g_free(chemin);
	test_rmdir(dir);
	g_free(dir);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/parsecfg/lecture", test_lecture);
	g_test_add_func("/parsecfg/aller-retour", test_aller_retour);
	g_test_add_func("/parsecfg/booleen-non-renseigne", test_booleen_non_renseigne);
	g_test_add_func("/parsecfg/locale-preservee", test_locale_preservee);
	g_test_add_func("/parsecfg/fichier-vide-puis-ecriture", test_fichier_vide_puis_ecriture);
	g_test_add_func("/parsecfg/sections-differant-par-la-casse", test_sections_differant_par_la_casse);
	g_test_add_func("/parsecfg/espace-sans-guillemets", test_espace_sans_guillemets);
	g_test_add_func("/parsecfg/fichier-absent", test_fichier_absent);
	return g_test_run();
}
