/* Tests de macro_file.c : fichier de macros v2 (valeurs échappées), lecture de l'ancien format, copie avant migration. */

#include <glib.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "macro_file.h"
#include "test_util.h"

#define GENERAL "G\303\251n\303\251ral"	/* « Général » en UTF-8 */

static GtMacroSet *
charger(const gchar *path)
{
	GtMacroSet *set = NULL;
	GError *error = NULL;

	g_assert_true(gt_macro_file_load(path, &set, &error));
	g_assert_no_error(error);
	g_assert_nonnull(set);
	return set;
}

static gchar *
lire(const gchar *path)
{
	gchar *texte = NULL;

	g_assert_true(g_file_get_contents(path, &texte, NULL, NULL));
	return texte;
}

static void
comparer_vecteur(gchar **a, gchar **b)
{
	g_assert_cmpint(a == NULL, ==, b == NULL);
	if(a != NULL)
	{
		g_assert_cmpuint(g_strv_length(a), ==, g_strv_length(b));
		for(guint k = 0; a[k] != NULL; k++)
			g_assert_cmpstr(a[k], ==, b[k]);
	}
}

static void
comparer_macro(const GtMacro *a, const GtMacro *b)
{
	g_assert_cmpstr(a->label, ==, b->label);
	g_assert_cmpstr(a->shortcut, ==, b->shortcut);
	g_assert_cmpstr(a->action, ==, b->action);
	g_assert_cmpstr(a->tab, ==, b->tab);
	g_assert_cmpint(a->polling, ==, b->polling);
	g_assert_cmpuint(a->polling_period_ms, ==, b->polling_period_ms);
	g_assert_cmpint(a->args == NULL, ==, b->args == NULL);
	if(a->args != NULL)
	{
		g_assert_cmpuint(g_strv_length(a->args), ==, g_strv_length(b->args));
		for(guint k = 0; a->args[k] != NULL; k++)
			g_assert_cmpstr(a->args[k], ==, b->args[k]);
	}
	comparer_vecteur(a->arg_min, b->arg_min);
	comparer_vecteur(a->arg_max, b->arg_max);
	comparer_vecteur(a->arg_charset, b->arg_charset);
}

static void
test_aller_retour(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "m.ini", NULL);
	GtMacroSet *set = gt_macro_set_new(), *relu;
	GtMacroLists *lists = gt_macro_lists_new();
	const GtMacroLists *l2;
	const gchar *deux[] = { "", "a;b\\c" };
	GError *error = NULL;
	guint id1, id2, id3;
	gchar *texte;

	gt_macro_lists_add(lists, "Vitesses", "Lent", "0");
	gt_macro_lists_add(lists, "Vitesses", "Rapide", "1");
	gt_macro_lists_add(lists, "S\303\251p;ar\303\251e", "a;b", "x\\y");
	gt_macro_set_take_lists(set, lists);
	id1 = gt_macro_set_append(set, "  Lecture", "<Control>F5", "R%d::x|y;z=w\\r", GENERAL);
	gt_macro_set_set_arg(set, id1, 0, "42");
	gt_macro_set_set_polling(set, id1, TRUE, 250);
	id2 = gt_macro_set_append(set, "Vide", "", "V%s %s", "");
	g_assert_true(gt_macro_set_set_args(set, id2, deux, 2));
	id3 = gt_macro_set_append(set, "Liste", "", "S %#Vitesses", "Onglet 2");
	gt_macro_set_set_arg(set, id3, 0, "1");
	gt_macro_set_append(set, "", "", "%&", "");
	/* Le jeu lu est regroupé par onglet (§2.2) : le jeu de départ l'est aussi, pour comparer rang par rang */
	g_assert_true(gt_macro_set_group_by_tab(set));

	g_assert_true(gt_macro_file_save(set, path, &error));
	g_assert_no_error(error);
	texte = lire(path);
	g_assert_nonnull(strstr(texte, "[gtkterm-macros]\nversion=2\n"));
	g_assert_nonnull(strstr(texte, "[macro:1]"));
	g_assert_nonnull(strstr(texte, "[list:1]"));
	g_free(texte);

	relu = charger(path);
	g_assert_cmpuint(gt_macro_set_count(relu), ==, 4);
	for(guint i = 0; i < 4; i++)
		comparer_macro(gt_macro_set_get(set, i), gt_macro_set_get(relu, i));
	l2 = gt_macro_set_get_lists(relu);
	g_assert_cmpint(gt_macro_lists_count(l2), ==, 2);
	g_assert_cmpstr(gt_macro_lists_name(l2, 1), ==, "S\303\251p;ar\303\251e");
	g_assert_cmpstr(gt_macro_lists_entry_display(l2, 1, 0), ==, "a;b");
	g_assert_cmpstr(gt_macro_lists_entry_value(l2, 1, 0), ==, "x\\y");
	g_assert_cmpstr(gt_macro_lists_entry_value(l2, 0, 1), ==, "1");
	gt_macro_set_free(relu);
	gt_macro_set_free(set);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

static void
test_limites_aller_retour(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "m.ini", NULL);
	GtMacroSet *set = gt_macro_set_new(), *relu;
	const gchar *const mn[] = { "0", "" , NULL };
	const gchar *const mx[] = { "100", "8", NULL };
	const gchar *const cs[] = { "", "class:A-Z;0-9\\-", NULL };
	GError *error = NULL;
	guint id;
	gchar *texte;

	id = gt_macro_set_append(set, "L", "", "L%d %s", "");
	g_assert_cmpint(gt_macro_set_set_limits(set, id, mn, mx, cs, 2), ==, 0);
	gt_macro_set_append(set, "Sans", "", "S%d", "");
	g_assert_true(gt_macro_file_save(set, path, &error));
	g_assert_no_error(error);
	texte = lire(path);
	g_assert_nonnull(strstr(texte, "arg_min="));
	g_assert_nonnull(strstr(texte, "arg_max="));
	g_assert_nonnull(strstr(texte, "arg_charset="));
	/* Le second groupe n'écrit aucune de ces clés */
	g_assert_null(strstr(strstr(texte, "[macro:2]"), "arg_"));
	g_free(texte);

	relu = charger(path);
	g_assert_cmpuint(gt_macro_set_count(relu), ==, 2);
	comparer_macro(gt_macro_set_get(set, 0), gt_macro_set_get(relu, 0));
	comparer_macro(gt_macro_set_get(set, 1), gt_macro_set_get(relu, 1));
	g_assert_cmpstr(gt_macro_set_get(relu, 0)->arg_charset[1], ==, "class:A-Z;0-9\\-");
	g_assert_null(gt_macro_set_get(relu, 1)->arg_min);
	gt_macro_set_free(relu);
	gt_macro_set_free(set);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

static void
test_limites_absentes_ou_invalides(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);
	gchar *path = test_write_file(dir, "m.ini",
	                              "[gtkterm-macros]\nversion=2\n\n[macro:1]\nlabel=A\naction=A%d\nargs=5;\n\n"
	                              "[macro:2]\nlabel=B\naction=B%d %s\narg_min=abc;1;\narg_max=10;5;\narg_charset=;ascii;\n\n"
	                              "[macro:3]\nlabel=C\naction=C%d\narg_min=1;2;\n");
	GtMacroSet *set;
	const GtMacro *m;

	g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*invalid argument limits replaced by none*");
	g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*invalid argument limits replaced by none*");
	set = charger(path);
	g_test_assert_expected_messages();
	g_assert_cmpuint(gt_macro_set_count(set), ==, 3);
	m = gt_macro_set_get(set, 0);
	g_assert_null(m->arg_min);
	g_assert_null(m->arg_max);
	g_assert_null(m->arg_charset);
	m = gt_macro_set_get(set, 1);
	g_assert_cmpstr(m->arg_min[0], ==, "");
	g_assert_cmpstr(m->arg_min[1], ==, "1");
	g_assert_cmpstr(m->arg_max[0], ==, "10");
	g_assert_cmpstr(m->arg_max[1], ==, "5");
	g_assert_cmpstr(m->arg_charset[1], ==, "ascii");
	m = gt_macro_set_get(set, 2);		/* mauvaise longueur : tout refusé */
	g_assert_null(m->arg_min);
	gt_macro_set_free(set);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

static void
test_ordre_conserve(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "m.ini", NULL);
	GtMacroSet *set = gt_macro_set_new(), *relu;

	for(guint i = 0; i < 12; i++)
	{
		gchar *label = g_strdup_printf("M%u", i);
		gt_macro_set_append(set, label, "", label, "");
		g_free(label);
	}
	g_assert_true(gt_macro_file_save(set, path, NULL));
	relu = charger(path);
	g_assert_cmpuint(gt_macro_set_count(relu), ==, 12);
	for(guint i = 0; i < 12; i++)
		g_assert_cmpstr(gt_macro_set_get(relu, i)->label, ==, gt_macro_set_get(set, i)->label);
	gt_macro_set_free(relu);
	gt_macro_set_free(set);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

static void
test_version_refusee(void)
{
	const gchar *contenus[] = {
		"[gtkterm-macros]\nversion=3\n[macro:1]\naction=x\n",
		"[gtkterm-macros]\n[macro:1]\naction=x\n",
		"[gtkterm-macros]\nversion=abc\n",
		"[gtkterm-macros]\nversion=1\n",
	};
	gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);

	for(guint i = 0; i < G_N_ELEMENTS(contenus); i++)
	{
		gchar *path = test_write_file(dir, "m.ini", contenus[i]);
		GtMacroSet *set = NULL;
		GError *error = NULL;

		g_assert_false(gt_macro_file_load(path, &set, &error));
		g_assert_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE);
		g_assert_nonnull(strstr(error->message, path));
		g_assert_null(set);
		g_clear_error(&error);
		g_free(path);
	}
	test_rmdir(dir);
	g_free(dir);
}

static void
test_cles_manquantes_ou_invalides(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);
	gchar *path = test_write_file(dir, "m.ini",
		"[gtkterm-macros]\nversion=2\n"
		"[macro:1]\naction=A%d\nargs=1;2;\npolling=peut-etre\npolling_period_ms=-5\n"
		"[macro:2]\nlabel=B\npolling=true\npolling_period_ms=99999999999\n"
		"[macro:3]\nlabel=C\npolling=true\npolling_period_ms=0\n"
		"[list:1]\ndisplays=x;\n"
		"[list:2]\nname=L\ndisplays=a;b;c;\nvalues=1;\n"
		"[list:3]\nname=L\ndisplays=d;\nvalues=4;5;\n"
		"[autre]\ncle=valeur\n");
	GtMacroSet *set = charger(path);
	const GtMacro *m1 = gt_macro_set_get(set, 0), *m2 = gt_macro_set_get(set, 1), *m3 = gt_macro_set_get(set, 2);
	const GtMacroLists *l = gt_macro_set_get_lists(set);

	g_assert_cmpuint(gt_macro_set_count(set), ==, 3);
	g_assert_cmpstr(m1->label, ==, "");
	g_assert_cmpstr(m1->shortcut, ==, "");
	g_assert_cmpstr(m1->tab, ==, "");
	g_assert_cmpstr(m1->action, ==, "A%d");
	g_assert_null(m1->args);
	g_assert_false(m1->polling);
	g_assert_cmpuint(m1->polling_period_ms, ==, GT_MACRO_PERIOD_DEFAULT_MS);
	g_assert_cmpstr(m2->label, ==, "B");
	g_assert_cmpstr(m2->action, ==, "");
	g_assert_true(m2->polling);
	g_assert_cmpuint(m2->polling_period_ms, ==, GT_MACRO_PERIOD_MAX_MS);
	g_assert_cmpuint(m3->polling_period_ms, ==, GT_MACRO_PERIOD_DEFAULT_MS);
	g_assert_cmpint(gt_macro_lists_count(l), ==, 1);
	g_assert_cmpstr(gt_macro_lists_name(l, 0), ==, "L");
	g_assert_cmpint(gt_macro_lists_entry_count(l, 0), ==, 4);
	g_assert_cmpstr(gt_macro_lists_entry_value(l, 0, 0), ==, "1");
	g_assert_cmpstr(gt_macro_lists_entry_value(l, 0, 1), ==, "b");
	g_assert_cmpstr(gt_macro_lists_entry_value(l, 0, 2), ==, "c");
	g_assert_cmpstr(gt_macro_lists_entry_display(l, 0, 3), ==, "d");
	g_assert_cmpstr(gt_macro_lists_entry_value(l, 0, 3), ==, "4");
	gt_macro_set_free(set);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

#define ANCIEN \
	"[macros]\n" \
	"macro_0=Lecture::<Control>F5::R%d\\\\r::" GENERAL "::42::1::250\n" \
	"macro_1=\n" \
	"macro_2=SansTab::None::X\n" \
	"macro_3=Deux::None\n" \
	"macro_4=Liste::None::S %#Vitesses::::1::0\n" \
	"macro_5=Args::None::A%d %d::T::1|2\n" \
	"macro_6=Pol::None::P::T::::1\n" \
	"macro_7=Faux::None::F%d::::1|2::0::1000\n" \
	"[lists]\n" \
	"list_0=Vitesses:Lent=0|Rapide=1\n" \
	"list_1=Sans:a|b\n" \
	"list_2=pasdedeuxpoints\n"

static void
test_ancien_format(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);
	gchar *path = test_write_file(dir, "m.ini", ANCIEN);
	GtMacroSet *set = charger(path);
	const GtMacroLists *l = gt_macro_set_get_lists(set);
	const GtMacro *m;

	/* macro_1 (vide) et macro_3 (moins de deux « :: ») ignorées */
	g_assert_cmpuint(gt_macro_set_count(set), ==, 6);
	m = gt_macro_set_get(set, 0);
	g_assert_cmpstr(m->label, ==, "Lecture");
	g_assert_cmpstr(m->shortcut, ==, "<Control>F5");
	g_assert_cmpstr(m->action, ==, "R%d\\r");
	g_assert_cmpstr(m->tab, ==, GENERAL);
	g_assert_cmpstr(m->args[0], ==, "42");
	g_assert_true(m->polling);
	g_assert_cmpuint(m->polling_period_ms, ==, 250);
	m = gt_macro_set_get(set, 1);
	g_assert_cmpstr(m->label, ==, "SansTab");
	g_assert_cmpstr(m->shortcut, ==, "");
	g_assert_cmpstr(m->action, ==, "X");
	g_assert_cmpstr(m->tab, ==, "");
	m = gt_macro_set_get(set, 2);
	g_assert_cmpstr(m->args[0], ==, "1");
	g_assert_false(m->polling);
	g_assert_cmpuint(m->polling_period_ms, ==, GT_MACRO_PERIOD_DEFAULT_MS);
	/* Fichier : G, "", "", T, T, "" : regroupé à la lecture, « Faux » (onglet "") passe avant « Args » et « Pol » (T) */
	m = gt_macro_set_get(set, 3);
	g_assert_cmpstr(m->label, ==, "Faux");
	g_assert_cmpstr(m->tab, ==, "");
	g_assert_null(m->args);			/* 2 arguments enregistrés pour 1 spécificateur */
	m = gt_macro_set_get(set, 4);
	g_assert_cmpstr(m->tab, ==, "T");
	g_assert_cmpstr(m->args[0], ==, "1");
	g_assert_cmpstr(m->args[1], ==, "2");
	m = gt_macro_set_get(set, 5);
	g_assert_cmpstr(m->label, ==, "Pol");
	g_assert_null(m->args);
	g_assert_true(m->polling);
	g_assert_cmpint(gt_macro_lists_count(l), ==, 2);
	g_assert_cmpstr(gt_macro_lists_entry_value(l, 0, 1), ==, "1");
	g_assert_cmpstr(gt_macro_lists_name(l, 1), ==, "Sans");
	g_assert_cmpstr(gt_macro_lists_entry_value(l, 1, 1), ==, "b");
	gt_macro_set_free(set);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

static void
test_ancien_sans_macros(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);
	gchar *path = test_write_file(dir, "m.ini", "[lists]\nlist_0=L:a=1\n");
	GtMacroSet *set = charger(path);

	g_assert_cmpuint(gt_macro_set_count(set), ==, 0);
	g_assert_cmpint(gt_macro_lists_count(gt_macro_set_get_lists(set)), ==, 1);
	gt_macro_set_free(set);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

/* Fichier vide ou sans aucun groupe : jeu vide ; fichier avec des groupes mais aucun groupe connu : refuse (I3) */
static void
test_fichier_vide_ou_inconnu(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);
	gchar *vide = test_write_file(dir, "vide.ini", "");
	gchar *commentaire = test_write_file(dir, "commentaire.ini", "# rien\n\n");
	gchar *inconnu = test_write_file(dir, "inconnu.ini", "[autre]\nx=1\n");
	GtMacroSet *set = charger(vide);
	GError *error = NULL;

	g_assert_cmpuint(gt_macro_set_count(set), ==, 0);
	gt_macro_set_free(set);
	set = charger(commentaire);
	g_assert_cmpuint(gt_macro_set_count(set), ==, 0);
	gt_macro_set_free(set);
	set = NULL;
	g_assert_false(gt_macro_file_load(inconnu, &set, &error));
	g_assert_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_GROUP_NOT_FOUND);
	g_assert_nonnull(strstr(error->message, inconnu));
	g_assert_null(set);
	g_clear_error(&error);
	test_rmdir(dir);
	g_free(vide);
	g_free(commentaire);
	g_free(inconnu);
	g_free(dir);
}

/* I2 : antislash invalide tape a la main dans un fichier v2 : texte brut garde (syntaxe des macros), aller-retour sans perte */
static void
test_antislash_invalide(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);
	gchar *path = test_write_file(dir, "m.ini",
		"[gtkterm-macros]\nversion=2\n"
		"[macro:1]\nlabel=L\\q\nshortcut=<Control>\\k\naction=AT\\x0D\ntab=T\\z\n"
		"[list:1]\nname=N\\y\ndisplays=a;\nvalues=1;\n");
	GtMacroSet *set = charger(path), *relu;
	const GtMacro *m = gt_macro_set_get(set, 0);
	gchar *texte;

	g_assert_cmpuint(gt_macro_set_count(set), ==, 1);
	g_assert_cmpstr(m->label, ==, "L\\q");
	g_assert_cmpstr(m->shortcut, ==, "<Control>\\k");
	g_assert_cmpstr(m->action, ==, "AT\\x0D");
	g_assert_cmpstr(m->tab, ==, "T\\z");
	g_assert_cmpstr(gt_macro_lists_name(gt_macro_set_get_lists(set), 0), ==, "N\\y");
	g_assert_true(gt_macro_file_save(set, path, NULL));
	texte = lire(path);
	g_assert_nonnull(strstr(texte, "action=AT\\\\x0D\n"));
	g_free(texte);
	relu = charger(path);
	comparer_macro(gt_macro_set_get(set, 0), gt_macro_set_get(relu, 0));
	g_assert_cmpstr(gt_macro_lists_name(gt_macro_set_get_lists(relu), 0), ==, "N\\y");
	gt_macro_set_free(relu);
	gt_macro_set_free(set);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

static void
test_fichier_absent_ou_corrompu(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);
	gchar *absent = g_build_filename(dir, "absent.ini", NULL);
	gchar *corrompu = test_write_file(dir, "corrompu.ini", "pas un fichier ini\n[[[\n");
	GtMacroSet *set = NULL;
	GError *error = NULL;

	g_assert_false(gt_macro_file_load(absent, &set, &error));
	g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_NOENT);
	g_assert_null(set);
	g_clear_error(&error);
	g_assert_false(gt_macro_file_load(corrompu, &set, &error));
	g_assert_nonnull(error);
	g_assert_nonnull(strstr(error->message, "corrompu.ini"));
	g_assert_null(set);
	g_clear_error(&error);
	test_rmdir(dir);
	g_free(absent);
	g_free(corrompu);
	g_free(dir);
}

static void
test_copie_avant_migration(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);
	gchar *path = test_write_file(dir, "m.ini", ANCIEN);
	gchar *copie = g_strconcat(path, ".avant-migration", NULL);
	GtMacroSet *set = charger(path);
	gchar *texte;

	g_assert_false(g_file_test(copie, G_FILE_TEST_EXISTS));
	g_assert_true(gt_macro_file_save(set, path, NULL));
	texte = lire(copie);
	g_assert_cmpstr(texte, ==, ANCIEN);
	g_free(texte);
	texte = lire(path);
	g_assert_nonnull(strstr(texte, "[gtkterm-macros]"));
	g_free(texte);
	/* Second enregistrement : le fichier est v2, la copie ne bouge pas */
	gt_macro_set_append(set, "Nouvelle", "", "N", "");
	g_assert_true(gt_macro_file_save(set, path, NULL));
	texte = lire(copie);
	g_assert_cmpstr(texte, ==, ANCIEN);
	g_free(texte);
	/* Un autre ancien fichier au même endroit : la copie existante n'est jamais écrasée, l'enregistrement est refusé (I1) */
	g_free(test_write_file(dir, "m.ini", "[macros]\nmacro_0=Autre::None::O\n"));
	g_assert_false(gt_macro_file_save(set, path, NULL));
	texte = lire(copie);
	g_assert_cmpstr(texte, ==, ANCIEN);
	g_free(texte);
	texte = lire(path);
	g_assert_cmpstr(texte, ==, "[macros]\nmacro_0=Autre::None::O\n");
	g_free(texte);
	gt_macro_set_free(set);
	test_rmdir(dir);
	g_free(copie);
	g_free(path);
	g_free(dir);
}

static void
test_pas_de_copie_pour_v2(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "m.ini", NULL);
	gchar *copie = g_strconcat(path, ".avant-migration", NULL);
	GtMacroSet *set = gt_macro_set_new();

	gt_macro_set_append(set, "A", "", "a", "");
	g_assert_true(gt_macro_file_save(set, path, NULL));
	g_assert_true(gt_macro_file_save(set, path, NULL));
	g_assert_false(g_file_test(copie, G_FILE_TEST_EXISTS));
	gt_macro_set_free(set);
	test_rmdir(dir);
	g_free(copie);
	g_free(path);
	g_free(dir);
}

/* I1 : tout fichier present, non vide, qui n'est pas un v2 lisible est copie une fois avant d'etre ecrase */
static void
test_copie_avant_ecrasement(void)
{
	const gchar *contenus[] = {
		"[gtkterm-macros]\nversion=3\n[macro:1]\naction=x\n",	/* version inconnue */
		"[gtkterm-macros]\nversion=2\n; commentaire\n",	/* syntaxe invalide (« ; » n'est pas un commentaire GKeyFile) */
		"[autre]\nx=1\n",					/* fichier etranger */
	};

	for(guint i = 0; i < G_N_ELEMENTS(contenus); i++)
	{
		gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);
		gchar *path = test_write_file(dir, "m.ini", contenus[i]);
		gchar *copie = g_strconcat(path, ".avant-migration", NULL);
		GtMacroSet *set = gt_macro_set_new();
		GError *error = NULL;
		gchar *texte;

		gt_macro_set_append(set, "A", "", "a", "");
		g_assert_true(gt_macro_file_save(set, path, &error));
		g_assert_no_error(error);
		texte = lire(copie);
		g_assert_cmpstr(texte, ==, contenus[i]);
		g_free(texte);
		texte = lire(path);
		g_assert_nonnull(strstr(texte, "[gtkterm-macros]\nversion=2\n"));
		g_free(texte);
		/* Le fichier est maintenant un v2 lisible : la copie ne bouge plus */
		g_assert_true(gt_macro_file_save(set, path, NULL));
		texte = lire(copie);
		g_assert_cmpstr(texte, ==, contenus[i]);
		g_free(texte);
		gt_macro_set_free(set);
		test_rmdir(dir);
		g_free(copie);
		g_free(path);
		g_free(dir);
	}
}

/* I1 : une copie existante au contenu different n'est jamais ecrasee : l'enregistrement est refuse ; identique, il passe */
static void
test_refus_si_copie_differente(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);
	gchar *path = test_write_file(dir, "m.ini", "[[[\n");
	gchar *copie = test_write_file(dir, "m.ini.avant-migration", "autre contenu\n");
	GtMacroSet *set = gt_macro_set_new();
	GError *error = NULL;
	gchar *texte;

	gt_macro_set_append(set, "A", "", "a", "");
	g_assert_false(gt_macro_file_save(set, path, &error));
	g_assert_nonnull(error);
	g_assert_nonnull(strstr(error->message, path));
	g_assert_nonnull(strstr(error->message, copie));
	g_clear_error(&error);
	texte = lire(path);
	g_assert_cmpstr(texte, ==, "[[[\n");
	g_free(texte);
	texte = lire(copie);
	g_assert_cmpstr(texte, ==, "autre contenu\n");
	g_free(texte);
	g_free(test_write_file(dir, "m.ini.avant-migration", "[[[\n"));
	g_assert_true(gt_macro_file_save(set, path, &error));
	g_assert_no_error(error);
	texte = lire(copie);
	g_assert_cmpstr(texte, ==, "[[[\n");
	g_free(texte);
	gt_macro_set_free(set);
	test_rmdir(dir);
	g_free(copie);
	g_free(path);
	g_free(dir);
}

/* Fichier vide ou absent : rien a copier */
static void
test_pas_de_copie_pour_vide(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);
	gchar *path = test_write_file(dir, "m.ini", "");
	gchar *copie = g_strconcat(path, ".avant-migration", NULL);
	GtMacroSet *set = gt_macro_set_new();

	g_assert_true(gt_macro_file_save(set, path, NULL));
	g_assert_false(g_file_test(copie, G_FILE_TEST_EXISTS));
	gt_macro_set_free(set);
	test_rmdir(dir);
	g_free(copie);
	g_free(path);
	g_free(dir);
}

/* I1 : la copie ne peut pas etre ecrite (repertoire en lecture seule) : rien n'est ecrase */
static void
test_echec_ecriture_copie(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);
	gchar *path = test_write_file(dir, "m.ini", "[autre]\nx=1\n");
	gchar *copie = g_strconcat(path, ".avant-migration", NULL);
	gchar *sonde = g_build_filename(dir, "sonde", NULL);
	GtMacroSet *set = gt_macro_set_new();
	GError *error = NULL;
	gchar *texte;

	g_assert_cmpint(g_chmod(dir, 0500), ==, 0);
	/* En root, le repertoire reste inscriptible : cas sans objet */
	if(g_file_set_contents(sonde, "x", 1, NULL))
	{
		g_test_skip("repertoire toujours inscriptible (root ?)");
	}
	else
	{
		g_assert_false(gt_macro_file_save(set, path, &error));
		g_assert_nonnull(error);
		g_clear_error(&error);
		g_assert_false(g_file_test(copie, G_FILE_TEST_EXISTS));
		texte = lire(path);
		g_assert_cmpstr(texte, ==, "[autre]\nx=1\n");
		g_free(texte);
	}
	g_assert_cmpint(g_chmod(dir, 0700), ==, 0);
	gt_macro_set_free(set);
	test_rmdir(dir);
	g_free(sonde);
	g_free(copie);
	g_free(path);
	g_free(dir);
}

static void
test_echec_ecriture(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "absent", "m.ini", NULL);
	GtMacroSet *set = gt_macro_set_new();
	GError *error = NULL;

	g_assert_false(gt_macro_file_save(set, path, &error));
	g_assert_nonnull(error);
	g_assert_nonnull(strstr(error->message, "absent"));
	g_clear_error(&error);
	gt_macro_set_free(set);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

/* Texte produit par le code 6b (avant l'extraction de gt_macro_set_to_key_file) pour le jeu de référence de test_aller_retour : le format
 * du fichier .ini ne change pas */
static GtMacroSet *
jeu_reference(void)
{
	GtMacroSet *set = gt_macro_set_new();
	GtMacroLists *lists = gt_macro_lists_new();
	const gchar *deux[] = { "", "a;b\\c" };
	guint id1, id2, id3;

	gt_macro_lists_add(lists, "Vitesses", "Lent", "0");
	gt_macro_lists_add(lists, "Vitesses", "Rapide", "1");
	gt_macro_lists_add(lists, "S\303\251p;ar\303\251e", "a;b", "x\\y");
	gt_macro_set_take_lists(set, lists);
	id1 = gt_macro_set_append(set, "  Lecture", "<Control>F5", "R%d::x|y;z=w\\r", GENERAL);
	gt_macro_set_set_arg(set, id1, 0, "42");
	gt_macro_set_set_polling(set, id1, TRUE, 250);
	id2 = gt_macro_set_append(set, "Vide", "", "V%s %s", "");
	g_assert_true(gt_macro_set_set_args(set, id2, deux, 2));
	id3 = gt_macro_set_append(set, "Liste", "", "S %#Vitesses", "Onglet 2");
	gt_macro_set_set_arg(set, id3, 0, "1");
	gt_macro_set_append(set, "", "", "%&", "");
	return set;
}

static void
test_identique_6b(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "m.ini", NULL);
	GtMacroSet *set = jeu_reference();
	gchar *texte;
	const gchar *attendu =
		"[gtkterm-macros]\n"
		"version=2\n"
		"\n"
		"[macro:1]\n"
		"label=\\s\\sLecture\n"
		"shortcut=<Control>F5\n"
		"action=R%d::x|y;z=w\\\\r\n"
		"tab=G\303\251n\303\251ral\n"
		"args=42;\n"
		"polling=true\n"
		"polling_period_ms=250\n"
		"\n"
		"[macro:2]\n"
		"label=Vide\n"
		"shortcut=\n"
		"action=V%s %s\n"
		"tab=\n"
		"args=;a\\;b\\\\c;\n"
		"polling=false\n"
		"polling_period_ms=1000\n"
		"\n"
		"[macro:3]\n"
		"label=Liste\n"
		"shortcut=\n"
		"action=S %#Vitesses\n"
		"tab=Onglet 2\n"
		"args=1;\n"
		"polling=false\n"
		"polling_period_ms=1000\n"
		"\n"
		"[macro:4]\n"
		"label=\n"
		"shortcut=\n"
		"action=%&\n"
		"tab=\n"
		"polling=false\n"
		"polling_period_ms=1000\n"
		"\n"
		"[list:1]\n"
		"name=Vitesses\n"
		"displays=Lent;Rapide;\n"
		"values=0;1;\n"
		"\n"
		"[list:2]\n"
		"name=S\303\251p;ar\303\251e\n"
		"displays=a\\;b;\n"
		"values=x\\\\y;\n";

	g_assert_true(gt_macro_file_save(set, path, NULL));
	texte = lire(path);
	g_assert_cmpstr(texte, ==, attendu);
	g_free(texte);
	gt_macro_set_free(set);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

static void
test_prefixe(void)
{
	GtMacroSet *set = jeu_reference(), *relu, *autre;
	GKeyFile *kf = g_key_file_new();

	gt_macro_set_group_by_tab(set);	/* jeu lu regroupé par onglet (§2.2) : le jeu de départ l'est aussi, pour comparer rang par rang */
	gt_macro_set_to_key_file(set, kf, "tab:3:");
	g_assert_true(g_key_file_has_group(kf, "tab:3:macro:1"));
	g_assert_true(g_key_file_has_group(kf, "tab:3:macro:4"));
	g_assert_true(g_key_file_has_group(kf, "tab:3:list:1"));
	g_assert_true(g_key_file_has_group(kf, "tab:3:list:2"));
	g_assert_false(g_key_file_has_group(kf, "gtkterm-macros"));
	g_key_file_set_string(kf, "tab:30:macro:1", "label", "intrus");
	relu = gt_macro_set_from_key_file(kf, "tab:3:", "x");
	g_assert_cmpuint(gt_macro_set_count(relu), ==, 4);
	for(guint i = 0; i < 4; i++)
		comparer_macro(gt_macro_set_get(set, i), gt_macro_set_get(relu, i));
	g_assert_cmpint(gt_macro_lists_count(gt_macro_set_get_lists(relu)), ==, 2);
	autre = gt_macro_set_from_key_file(kf, "tab:1:", "x");
	g_assert_nonnull(autre);
	g_assert_cmpuint(gt_macro_set_count(autre), ==, 0);
	g_assert_cmpint(gt_macro_lists_count(gt_macro_set_get_lists(autre)), ==, 0);
	gt_macro_set_free(autre);
	gt_macro_set_free(relu);
	gt_macro_set_free(set);
	g_key_file_free(kf);
}

static void
test_echappement_invalide_garde(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);
	gchar *path = test_write_file(dir, "m.ini",
	                              "[gtkterm-macros]\nversion=2\n\n[macro:1]\nlabel=M\naction=A%s%s\nargs=a\\q;b\n\n[list:1]\nname=L\ndisplays=x\\q;y\nvalues=1;2\n");
	GtMacroSet *set;
	const GtMacroLists *l;

	g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*invalid escape*");
	g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*invalid escape*");
	set = charger(path);
	g_test_assert_expected_messages();
	g_assert_cmpuint(gt_macro_set_count(set), ==, 1);
	g_assert_nonnull(gt_macro_set_get(set, 0)->args);
	g_assert_cmpstr(gt_macro_set_get(set, 0)->args[0], ==, "a\\q");
	g_assert_cmpstr(gt_macro_set_get(set, 0)->args[1], ==, "b");
	l = gt_macro_set_get_lists(set);
	g_assert_cmpint(gt_macro_lists_entry_count(l, 0), ==, 2);
	g_assert_cmpstr(gt_macro_lists_entry_display(l, 0, 0), ==, "x\\q");
	g_assert_cmpstr(gt_macro_lists_entry_display(l, 0, 1), ==, "y");
	gt_macro_set_free(set);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

static void
test_liste_vide_relue(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);
	gchar *path = test_write_file(dir, "m.ini", "[gtkterm-macros]\nversion=2\n\n[list:1]\nname=Vide\ndisplays=\nvalues=\n");
	GtMacroSet *set = charger(path), *relu;
	GError *error = NULL;
	gchar *texte;

	g_assert_cmpint(gt_macro_lists_find(gt_macro_set_get_lists(set), "Vide"), >=, 0);
	g_assert_cmpint(gt_macro_lists_entry_count(gt_macro_set_get_lists(set), 0), ==, 0);
	g_assert_true(gt_macro_file_save(set, path, &error));
	g_assert_no_error(error);
	texte = lire(path);
	g_assert_nonnull(strstr(texte, "[list:1]"));
	g_free(texte);
	relu = charger(path);
	g_assert_cmpint(gt_macro_lists_find(gt_macro_set_get_lists(relu), "Vide"), >=, 0);
	gt_macro_set_free(relu);
	gt_macro_set_free(set);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

static void
test_illisible_cause(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);
	gchar *path = test_write_file(dir, "m.ini", "[gtkterm-macros]\nversion=2\n");
	GtMacroSet *set = NULL;
	GError *error = NULL;

	if(geteuid() == 0)
		g_test_skip("root lit tout");
	else
	{
		g_assert_cmpint(g_chmod(path, 0), ==, 0);
		g_assert_false(gt_macro_file_load(path, &set, &error));
		g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_ACCES);
		g_assert_nonnull(strstr(error->message, path));
		g_assert_nonnull(strstr(error->message, g_strerror(EACCES)));
		{
			gchar *attendu = g_strdup_printf("%s: %s", path, g_strerror(EACCES));

			g_assert_cmpstr(error->message, ==, attendu);
			g_free(attendu);
		}
		g_assert_null(set);
		g_clear_error(&error);
	}
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

static void
test_refus_copie_remede(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);
	gchar *path = test_write_file(dir, "m.ini", "[[[\n");
	gchar *copie = test_write_file(dir, "m.ini.avant-migration", "autre contenu\n");
	GtMacroSet *set = gt_macro_set_new();
	GError *error = NULL;

	g_assert_false(gt_macro_file_save(set, path, &error));
	g_assert_nonnull(error);
	g_assert_nonnull(strstr(error->message, ".avant-migration"));
	g_assert_nonnull(strstr(error->message, "ename or delete"));
	g_clear_error(&error);
	gt_macro_set_free(set);
	test_rmdir(dir);
	g_free(copie);
	g_free(path);
	g_free(dir);
}

/* Un fichier aux onglets entrelacés est lu regroupé ; le fichier n'est pas réécrit */
static void
test_regroupe_lecture(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-mf-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "m.ini", NULL);
	const gchar *texte = "[gtkterm-macros]\nversion=2\n\n"
	                     "[macro:1]\nlabel=a\naction=A\ntab=T1\n\n"
	                     "[macro:2]\nlabel=b\naction=B\ntab=\n\n"
	                     "[macro:3]\nlabel=c\naction=C\ntab=T1\n";
	GtMacroSet *set;
	gchar *relu;

	g_assert_true(g_file_set_contents(path, texte, -1, NULL));
	set = charger(path);
	g_assert_cmpstr(gt_macro_set_get(set, 0)->label, ==, "a");
	g_assert_cmpstr(gt_macro_set_get(set, 1)->label, ==, "c");
	g_assert_cmpstr(gt_macro_set_get(set, 2)->label, ==, "b");
	relu = lire(path);
	g_assert_cmpstr(relu, ==, texte);	/* jamais réécrit par la lecture */
	g_free(relu);
	gt_macro_set_free(set);
	g_remove(path);
	g_rmdir(dir);
	g_free(path);
	g_free(dir);
}

/* Même regroupement pour l'ancien format ([macros]) */
static void
test_regroupe_ancien_format(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-mf-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "m.ini", NULL);
	const gchar *texte = "[macros]\nmacro_0=a::None::A::T1::::0::1000\n"
	                     "macro_1=b::None::B::::::0::1000\n"
	                     "macro_2=c::None::C::T1::::0::1000\n";
	GtMacroSet *set;
	gchar *relu;

	g_assert_true(g_file_set_contents(path, texte, -1, NULL));
	set = charger(path);
	g_assert_cmpstr(gt_macro_set_get(set, 0)->label, ==, "a");
	g_assert_cmpstr(gt_macro_set_get(set, 1)->label, ==, "c");
	g_assert_cmpstr(gt_macro_set_get(set, 2)->label, ==, "b");
	relu = lire(path);
	g_assert_cmpstr(relu, ==, texte);
	g_free(relu);
	gt_macro_set_free(set);
	g_remove(path);
	g_rmdir(dir);
	g_free(path);
	g_free(dir);
}

static void
test_facultatif_aller_retour(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-mf-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "m.ini", NULL);
	GtMacroSet *set = gt_macro_set_new(), *relu;
	guint id = gt_macro_set_append(set, "co", "", "connect %s%s", "");
	const gchar *opt[] = { "", "1", NULL }, *pre[] = { "", " -password=", NULL };
	GError *error = NULL;
	gchar *texte;

	g_assert_true(gt_macro_set_set_optional(set, id, opt, pre, 2));
	g_assert_true(gt_macro_file_save(set, path, &error));
	g_assert_no_error(error);
	texte = lire(path);
	g_assert_nonnull(strstr(texte, "arg_optional=;1;"));
	g_assert_nonnull(strstr(texte, "arg_prefix=;\\s-password=;"));	/* espace de tête échappé par GKeyFile */
	g_free(texte);
	relu = charger(path);
	g_assert_cmpstr(gt_macro_arg_prefix(gt_macro_set_get(relu, 0), 1), ==, " -password=");
	g_assert_true(gt_macro_arg_is_optional(gt_macro_set_get(relu, 0), 1));
	g_assert_true(gt_macro_set_same_structure(set, relu));
	gt_macro_set_free(relu);
	/* Lecture tolérante : longueur fausse ajustée, valeur inconnue -> "" (avertissements attendus) */
	g_assert_true(g_file_set_contents(path, "[gtkterm-macros]\nversion=2\n\n[macro:1]\nlabel=x\naction=X%s%s\n"
	                                  "arg_optional=1;oui;1;\narg_prefix=a;\n", -1, NULL));
	g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*arg_optional*");
	g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*arg_prefix*");
	relu = charger(path);
	g_test_assert_expected_messages();
	g_assert_true(gt_macro_arg_is_optional(gt_macro_set_get(relu, 0), 0));
	g_assert_false(gt_macro_arg_is_optional(gt_macro_set_get(relu, 0), 1));
	g_assert_cmpstr(gt_macro_arg_prefix(gt_macro_set_get(relu, 0), 0), ==, "a");
	g_assert_cmpstr(gt_macro_arg_prefix(gt_macro_set_get(relu, 0), 1), ==, "");
	gt_macro_set_free(relu);
	gt_macro_set_free(set);
	g_remove(path);
	g_rmdir(dir);
	g_free(path);
	g_free(dir);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/macro_file/aller-retour", test_aller_retour);
	g_test_add_func("/macro_file/limites-aller-retour", test_limites_aller_retour);
	g_test_add_func("/macro_file/limites-absentes-ou-invalides", test_limites_absentes_ou_invalides);
	g_test_add_func("/macro_file/ordre-conserve", test_ordre_conserve);
	g_test_add_func("/macro_file/version-refusee", test_version_refusee);
	g_test_add_func("/macro_file/cles-manquantes-ou-invalides", test_cles_manquantes_ou_invalides);
	g_test_add_func("/macro_file/ancien-format", test_ancien_format);
	g_test_add_func("/macro_file/ancien-sans-macros", test_ancien_sans_macros);
	g_test_add_func("/macro_file/fichier-vide-ou-inconnu", test_fichier_vide_ou_inconnu);
	g_test_add_func("/macro_file/fichier-absent-ou-corrompu", test_fichier_absent_ou_corrompu);
	g_test_add_func("/macro_file/copie-avant-migration", test_copie_avant_migration);
	g_test_add_func("/macro_file/pas-de-copie-pour-v2", test_pas_de_copie_pour_v2);
	g_test_add_func("/macro_file/echec-ecriture", test_echec_ecriture);
	g_test_add_func("/macro_file/antislash-invalide", test_antislash_invalide);
	g_test_add_func("/macro_file/copie-avant-ecrasement", test_copie_avant_ecrasement);
	g_test_add_func("/macro_file/refus-si-copie-differente", test_refus_si_copie_differente);
	g_test_add_func("/macro_file/pas-de-copie-pour-vide", test_pas_de_copie_pour_vide);
	g_test_add_func("/macro_file/echec-ecriture-copie", test_echec_ecriture_copie);
	g_test_add_func("/macro_file/identique-6b", test_identique_6b);
	g_test_add_func("/macro_file/prefixe", test_prefixe);
	g_test_add_func("/macro_file/echappement-invalide-garde", test_echappement_invalide_garde);
	g_test_add_func("/macro_file/liste-vide-relue", test_liste_vide_relue);
	g_test_add_func("/macro_file/illisible-cause", test_illisible_cause);
	g_test_add_func("/macro_file/refus-copie-remede", test_refus_copie_remede);
	g_test_add_func("/macro_file/regroupe-lecture", test_regroupe_lecture);
	g_test_add_func("/macro_file/facultatif-aller-retour", test_facultatif_aller_retour);
	g_test_add_func("/macro_file/regroupe-ancien-format", test_regroupe_ancien_format);
	return g_test_run();
}
