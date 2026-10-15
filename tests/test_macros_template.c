/* Tests d'intégration de macros.c, onglet lié à un modèle de macros (spec 2026-10-15-modeles-macros-design.md, §2.1, §2.5 à §2.8) :
 * lier, détacher, classer des modifications, garder dans le projet, enregistrer dans le modèle, conflit, rechargement, rafraîchissement,
 * ouverture d'un projet. Sans GTK : effets d'interface relevés par la table GtAppUi du test, liaison du bouchon. */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>

#include "app_ui.h"
#include "file_stamp.h"
#include "macro_file.h"
#include "macro_override.h"
#include "macros.h"
#include "stubs.h"
#include "test_util.h"

static void
enr_message(GtAppMessageKind kind, const gchar *text)
{
	stub_record("message", text);
}

static void
enr_status_flash(GtSession *session, const gchar *text, guint ms)
{
	stub_record("status_flash", text);
}

static void
enr_macros_changed(GtSession *session)
{
	stub_record("macros_changed", NULL);
}

static void
enr_polling(GtSession *session, guint id, gboolean running)
{
	stub_record("macro_polling_changed", running ? "1" : "0");
}

static const GtAppUi table_enregistrement =
{
	.message = enr_message,
	.status_flash = enr_status_flash,
	.macros_changed = enr_macros_changed,
	.macro_polling_changed = enr_polling,
};

static gint compteur_edited;

static void
rappel_edited(gpointer data)
{
	compteur_edited++;
}

/* Modèle de référence : Vitesse « V%d\r » (défaut 10, max 100), Mode « M%d; » (défaut 1), Ping « AT\r » (sans argument), Choix « C%#Liste »
 * avec la liste Liste (a=1, b=2). */
static GtMacroSet *
jeu_modele(const gchar *defaut_vitesse)
{
	GtMacroSet *set = gt_macro_set_new();
	GtMacroLists *lists = gt_macro_lists_new();
	const gchar *mx[] = { "100", NULL };
	guint id;

	gt_macro_lists_add(lists, "Liste", "a", "1");
	gt_macro_lists_add(lists, "Liste", "b", "2");
	gt_macro_set_take_lists(set, lists);
	id = gt_macro_set_append(set, "Vitesse", "", "V%d\\r", "");
	g_assert_true(gt_macro_set_set_arg(set, id, 0, defaut_vitesse));
	g_assert_cmpint(gt_macro_set_set_limits(set, id, NULL, mx, NULL, 1), ==, 0);
	id = gt_macro_set_append(set, "Mode", "", "M%d;", "");
	g_assert_true(gt_macro_set_set_arg(set, id, 0, "1"));
	gt_macro_set_append(set, "Ping", "", "AT\\r", "");
	id = gt_macro_set_append(set, "Choix", "", "C%#Liste", "");
	g_assert_true(gt_macro_set_set_arg(set, id, 0, "1"));
	return set;
}

static gchar *
ecrire_modele(const gchar *dir, const gchar *nom, const gchar *defaut_vitesse)
{
	gchar *path = g_build_filename(dir, nom, NULL);
	GtMacroSet *set = jeu_modele(defaut_vitesse);

	g_assert_true(gt_macro_file_save(set, path, NULL));
	gt_macro_set_free(set);
	return path;
}

/* Écrit set (pris) dans dir/nom ; renvoie le chemin */
static gchar *
ecrire_jeu(const gchar *dir, const gchar *nom, GtMacroSet *set)
{
	gchar *path = g_build_filename(dir, nom, NULL);

	g_assert_true(gt_macro_file_save(set, path, NULL));
	gt_macro_set_free(set);
	return path;
}

static gchar *
lire_texte(const gchar *path)
{
	gchar *texte = NULL;

	g_assert_true(g_file_get_contents(path, &texte, NULL, NULL));
	return texte;
}

static const GtMacro *
macro_nommee(GtMacros *m, const gchar *label)
{
	const GtMacro *macro = gt_macro_set_find_label(gt_macros_get_set(m), label);

	g_assert_nonnull(macro);
	return macro;
}

static const gchar *
defaut(const GtMacro *macro, guint k)
{
	return macro->args != NULL ? macro->args[k] : "";
}

/* Lignes de la fenêtre d'édition : un clone du jeu courant (identifiants gardés) dont les chaînes vivent tant que lui ; jamais des chaînes
 * du jeu courant, que l'application libère. */
typedef struct
{
	GtMacroSet *clone;
	GtMacroEdit *rows;
	guint n;
	gchar **args[16];	/* valeurs par défaut remplacées, possédées */
	gchar **max[16];
} Lignes;

static Lignes *
lignes_de(GtMacros *m)
{
	Lignes *l = g_new0(Lignes, 1);

	l->clone = gt_macro_set_clone(gt_macros_get_set(m));
	l->n = gt_macro_set_count(l->clone);
	g_assert_cmpuint(l->n, <, 15);
	l->rows = g_new0(GtMacroEdit, l->n + 1);
	for(guint i = 0; i < l->n; i++)
	{
		const GtMacro *macro = gt_macro_set_get(l->clone, i);

		l->rows[i].id = macro->id;
		l->rows[i].label = macro->label;
		l->rows[i].shortcut = macro->shortcut;
		l->rows[i].action = macro->action;
		l->rows[i].tab = macro->tab;
		l->rows[i].args = (const gchar *const *)macro->args;
		l->rows[i].arg_min = (const gchar *const *)macro->arg_min;
		l->rows[i].arg_max = (const gchar *const *)macro->arg_max;
		l->rows[i].arg_charset = (const gchar *const *)macro->arg_charset;
	}
	return l;
}

static guint
rang(Lignes *l, const gchar *label)
{
	for(guint i = 0; i < l->n; i++)
		if(g_strcmp0(l->rows[i].label, label) == 0)
			return i;
	g_assert_not_reached();
	return 0;
}

/* Valeur par défaut d'un argument unique remplacée dans la ligne */
static void
lignes_defaut(Lignes *l, const gchar *label, const gchar *valeur)
{
	guint i = rang(l, label);

	g_strfreev(l->args[i]);
	l->args[i] = g_new0(gchar *, 2);
	l->args[i][0] = g_strdup(valeur);
	l->rows[i].args = (const gchar *const *)l->args[i];
}

static void
lignes_max(Lignes *l, const gchar *label, const gchar *valeur)
{
	guint i = rang(l, label);

	g_strfreev(l->max[i]);
	l->max[i] = g_new0(gchar *, 2);
	l->max[i][0] = g_strdup(valeur);
	l->rows[i].arg_max = (const gchar *const *)l->max[i];
}

static void
lignes_free(Lignes *l)
{
	for(guint i = 0; i < G_N_ELEMENTS(l->args); i++)
	{
		g_strfreev(l->args[i]);
		g_strfreev(l->max[i]);
	}
	g_free(l->rows);
	gt_macro_set_free(l->clone);
	g_free(l);
}

static GtMacros *
nouvelles_macros(void)
{
	GtMacros *m = gt_macros_new(stub_link_default(), NULL);

	gt_macros_set_embedded(m, TRUE);
	return m;
}

/* Lier : modèle lu, chemin rendu absolu, jeu effectif = copie de la base, aucune valeur propre ; usage et polling remis à zéro ; edited. */
static void
test_lier(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-modele-XXXXXX", NULL);
	gchar *path = ecrire_modele(dir, "modele.ini", "10");
	gchar *ancien = g_get_current_dir();
	GtMacros *m = nouvelles_macros();
	GtMacroOverrides *o;
	guint64 g0;
	guint id;

	gt_macros_set_edited_func(m, rappel_edited, NULL);
	gt_macro_set_append(gt_macros_get_set(m), "Autre", "", "X%s", "");
	id = gt_macro_set_get(gt_macros_get_set(m), 0)->id;
	gt_macros_set_usage_arg(m, id, 0, "z");
	g_assert_true(gt_macros_has_usage(m, 0));
	g_assert_null(gt_macros_get_template(m));
	g_assert_null(gt_macros_get_template_set(m));
	g_assert_null(gt_macros_get_overrides(m));
	g0 = gt_macros_get_generation(m);
	compteur_edited = 0;
	stub_reset();
	/* Chemin relatif : rendu absolu */
	g_assert_cmpint(g_chdir(dir), ==, 0);
	g_assert_true(gt_macros_link_template(m, "modele.ini"));
	g_assert_cmpint(g_chdir(ancien), ==, 0);
	g_assert_cmpstr(gt_macros_get_template(m), ==, path);
	g_assert_nonnull(gt_macros_get_template_set(m));
	g_assert_true(gt_macro_set_same_structure(gt_macros_get_template_set(m), gt_macros_get_set(m)));
	g_assert_cmpuint(gt_macro_set_count(gt_macros_get_set(m)), ==, 4);
	g_assert_cmpstr(defaut(macro_nommee(m, "Vitesse"), 0), ==, "10");
	o = gt_macros_get_overrides(m);
	g_assert_nonnull(o);
	g_assert_cmpuint(gt_macro_overrides_count(o), ==, 0);
	gt_macro_overrides_free(o);
	g_assert_false(gt_macros_has_usage(m, 0));
	g_assert_cmpint(compteur_edited, >=, 1);
	g_assert_cmpint(stub_count("message"), ==, 0);
	g_assert_cmpuint(gt_macros_get_generation(m), >, g0);
	/* Ni chemin de .ini ni écriture : le lien est à part */
	g_assert_null(gt_macros_get_path(m));
	gt_macros_free(m);
	test_rmdir(dir);
	g_free(ancien);
	g_free(path);
	g_free(dir);
}

/* Lien sur un fichier absent ou illisible : message, rien ne change */
static void
test_lier_absent_refuse(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-modele-XXXXXX", NULL);
	gchar *absent = g_build_filename(dir, "absent.ini", NULL);
	gchar *corrompu = test_write_file(dir, "corrompu.ini", "[[[\n");
	GtMacros *m = nouvelles_macros();
	guint64 g0;

	gt_macro_set_append(gt_macros_get_set(m), "Autre", "", "X", "");
	g0 = gt_macros_get_generation(m);
	stub_reset();
	g_assert_false(gt_macros_link_template(m, absent));
	g_assert_cmpint(stub_count("message"), ==, 1);
	g_assert_nonnull(strstr(stub_last("message"), "Cannot load macros file"));
	g_assert_false(gt_macros_link_template(m, corrompu));
	g_assert_cmpint(stub_count("message"), ==, 2);
	g_assert_null(gt_macros_get_template(m));
	g_assert_cmpuint(gt_macro_set_count(gt_macros_get_set(m)), ==, 1);
	g_assert_cmpuint(gt_macros_get_generation(m), ==, g0);
	g_assert_cmpint(stub_count("macros_changed"), ==, 0);
	gt_macros_free(m);
	test_rmdir(dir);
	g_free(absent);
	g_free(corrompu);
	g_free(dir);
}

/* Classer : rien, valeurs par défaut seulement, structure ; le jeu effectif n'est pas touché ; les listes passées ne sont pas consommées */
static void
test_classer(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-modele-XXXXXX", NULL);
	gchar *path = ecrire_modele(dir, "modele.ini", "10");
	GtMacros *m = nouvelles_macros();
	GtMacroLists *listes;
	Lignes *l;
	guint64 g0;

	g_assert_true(gt_macros_link_template(m, path));
	g0 = gt_macros_get_generation(m);
	listes = gt_macro_lists_copy(gt_macro_set_get_lists(gt_macros_get_set(m)));
	l = lignes_de(m);
	g_assert_cmpint(gt_macros_classify_edits(m, l->rows, l->n, listes), ==, GT_MACROS_EDIT_NONE);
	g_assert_cmpint(gt_macros_classify_edits(m, l->rows, l->n, NULL), ==, GT_MACROS_EDIT_NONE);
	lignes_defaut(l, "Vitesse", "20");
	g_assert_cmpint(gt_macros_classify_edits(m, l->rows, l->n, listes), ==, GT_MACROS_EDIT_DEFAULTS);
	/* Valeur d'une liste */
	lignes_defaut(l, "Vitesse", "10");
	lignes_defaut(l, "Choix", "2");
	g_assert_cmpint(gt_macros_classify_edits(m, l->rows, l->n, listes), ==, GT_MACROS_EDIT_DEFAULTS);
	/* Rien appliqué */
	g_assert_cmpstr(defaut(macro_nommee(m, "Vitesse"), 0), ==, "10");
	g_assert_cmpstr(defaut(macro_nommee(m, "Choix"), 0), ==, "1");
	g_assert_cmpuint(gt_macros_get_generation(m), ==, g0);
	lignes_free(l);
	/* Libellé, limites, suppression, ordre, listes : structure */
	l = lignes_de(m);
	l->rows[0].label = "Speed";
	g_assert_cmpint(gt_macros_classify_edits(m, l->rows, l->n, listes), ==, GT_MACROS_EDIT_STRUCTURE);
	lignes_free(l);
	l = lignes_de(m);
	lignes_max(l, "Vitesse", "50");
	g_assert_cmpint(gt_macros_classify_edits(m, l->rows, l->n, listes), ==, GT_MACROS_EDIT_STRUCTURE);
	lignes_free(l);
	l = lignes_de(m);
	g_assert_cmpint(gt_macros_classify_edits(m, l->rows, l->n - 1, listes), ==, GT_MACROS_EDIT_STRUCTURE);
	{
		GtMacroEdit t = l->rows[0];

		l->rows[0] = l->rows[1];
		l->rows[1] = t;
	}
	g_assert_cmpint(gt_macros_classify_edits(m, l->rows, l->n, listes), ==, GT_MACROS_EDIT_STRUCTURE);
	lignes_free(l);
	l = lignes_de(m);
	gt_macro_lists_add(listes, "Liste", "c", "3");
	g_assert_cmpint(gt_macros_classify_edits(m, l->rows, l->n, listes), ==, GT_MACROS_EDIT_STRUCTURE);
	/* Listes toujours à l'appelant (valgrind : ni double libération ni fuite) */
	g_assert_cmpint(gt_macro_lists_entry_count(listes, gt_macro_lists_find(listes, "Liste")), ==, 3);
	g_assert_cmpint(gt_macro_lists_entry_count(gt_macro_set_get_lists(gt_macros_get_set(m)), 0), ==, 2);
	gt_macro_lists_free(listes);
	lignes_free(l);
	gt_macros_free(m);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

/* Garder dans le projet : le jeu effectif change, le modèle non ; valeur propre ; usage de la macro changée effacé, pas celui des autres */
static void
test_garder_dans_le_projet(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-modele-XXXXXX", NULL);
	gchar *path = ecrire_modele(dir, "modele.ini", "10");
	gchar *avant = lire_texte(path), *apres;
	GtMacros *m = nouvelles_macros();
	GtMacroLists *listes;
	GtMacroOverrides *o;
	const GtMacroOverride *e;
	Lignes *l;
	guint id_v, id_m;
	guint64 g0;

	g_assert_true(gt_macros_link_template(m, path));
	id_v = macro_nommee(m, "Vitesse")->id;
	id_m = macro_nommee(m, "Mode")->id;
	gt_macros_set_usage_arg(m, id_v, 0, "33");
	gt_macros_set_usage_arg(m, id_m, 0, "4");
	listes = gt_macro_lists_copy(gt_macro_set_get_lists(gt_macros_get_set(m)));
	l = lignes_de(m);
	lignes_defaut(l, "Vitesse", "20");
	g0 = gt_macros_get_generation(m);
	stub_reset();
	gt_macros_commit_edits(m, l->rows, l->n, listes);
	g_assert_cmpint(gt_macro_lists_count(listes), ==, 1);	/* copiée, pas prise */
	gt_macro_lists_free(listes);
	lignes_free(l);
	g_assert_cmpint(stub_count("macros_changed"), ==, 1);
	g_assert_cmpuint(gt_macros_get_generation(m), >, g0);
	g_assert_cmpuint(macro_nommee(m, "Vitesse")->id, ==, id_v);
	g_assert_cmpstr(defaut(macro_nommee(m, "Vitesse"), 0), ==, "20");
	g_assert_false(gt_macros_has_usage(m, id_v));
	g_assert_true(gt_macros_has_usage(m, id_m));
	g_assert_cmpstr(defaut(gt_macro_set_find_label(gt_macros_get_template_set(m), "Vitesse"), 0), ==, "10");
	apres = lire_texte(path);
	g_assert_cmpstr(apres, ==, avant);
	o = gt_macros_get_overrides(m);
	g_assert_cmpuint(gt_macro_overrides_count(o), ==, 1);
	e = gt_macro_overrides_get(o, 0);
	g_assert_cmpstr(e->label, ==, "Vitesse");
	g_assert_cmpstr(g_ptr_array_index(e->args, 0), ==, "20");
	gt_macro_overrides_free(o);
	gt_macros_free(m);
	test_rmdir(dir);
	g_free(avant);
	g_free(apres);
	g_free(path);
	g_free(dir);
}

/* Enregistrer dans le modèle : la valeur changée part au modèle, la valeur propre d'un autre rang n'y fuit pas et reste valeur propre */
static void
test_enregistrer_dans_le_modele(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-modele-XXXXXX", NULL);
	gchar *path = ecrire_modele(dir, "modele.ini", "10");
	GtMacros *m = nouvelles_macros();
	GtMacroSet *relu = NULL;
	GtMacroOverrides *o;
	Lignes *l;

	g_assert_true(gt_macros_link_template(m, path));
	/* Valeur propre : Vitesse 20, gardée dans le projet */
	l = lignes_de(m);
	lignes_defaut(l, "Vitesse", "20");
	gt_macros_commit_edits(m, l->rows, l->n, NULL);
	lignes_free(l);
	/* Puis Mode 5, enregistré dans le modèle */
	l = lignes_de(m);
	lignes_defaut(l, "Mode", "5");
	stub_reset();
	g_assert_cmpint(gt_macros_save_template_edits(m, l->rows, l->n, NULL, FALSE), ==, GT_MACROS_SAVED);
	lignes_free(l);
	g_assert_cmpint(stub_count("status_flash"), ==, 1);
	g_assert_nonnull(strstr(stub_last("status_flash"), "modele.ini"));
	g_assert_nonnull(strstr(stub_last("status_flash"), "saved"));
	g_assert_cmpint(stub_count("message"), ==, 0);
	g_assert_true(gt_macro_file_load(path, &relu, NULL));
	g_assert_cmpstr(defaut(gt_macro_set_find_label(relu, "Mode"), 0), ==, "5");
	g_assert_cmpstr(defaut(gt_macro_set_find_label(relu, "Vitesse"), 0), ==, "10");	/* non fuitée */
	g_assert_true(gt_macro_set_same_structure(relu, gt_macros_get_template_set(m)));
	gt_macro_set_free(relu);
	g_assert_cmpstr(defaut(macro_nommee(m, "Mode"), 0), ==, "5");
	g_assert_cmpstr(defaut(macro_nommee(m, "Vitesse"), 0), ==, "20");
	g_assert_cmpstr(defaut(gt_macro_set_find_label(gt_macros_get_template_set(m), "Mode"), 0), ==, "5");
	o = gt_macros_get_overrides(m);
	g_assert_cmpuint(gt_macro_overrides_count(o), ==, 1);
	g_assert_cmpstr(gt_macro_overrides_get(o, 0)->label, ==, "Vitesse");
	gt_macro_overrides_free(o);
	/* Changement de structure enregistré : libellé nouveau dans le modèle, invariant tenu */
	l = lignes_de(m);
	l->rows[rang(l, "Ping")].label = "Ping2";
	g_assert_cmpint(gt_macros_save_template_edits(m, l->rows, l->n, NULL, FALSE), ==, GT_MACROS_SAVED);
	lignes_free(l);
	g_assert_true(gt_macro_file_load(path, &relu, NULL));
	g_assert_nonnull(gt_macro_set_find_label(relu, "Ping2"));
	gt_macro_set_free(relu);
	g_assert_true(gt_macro_set_same_structure(gt_macros_get_template_set(m), gt_macros_get_set(m)));
	g_assert_cmpstr(defaut(macro_nommee(m, "Vitesse"), 0), ==, "20");
	gt_macros_free(m);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

/* Valeur du modèle reprise, mais refusée par les nouvelles limites : la valeur de l'édition est gardée dans le modèle */
static void
test_enregistrer_limite_durcie(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-modele-XXXXXX", NULL);
	gchar *path = ecrire_modele(dir, "modele.ini", "50");
	GtMacros *m = nouvelles_macros();
	GtMacroSet *relu = NULL;
	Lignes *l;

	g_assert_true(gt_macros_link_template(m, path));
	l = lignes_de(m);
	lignes_defaut(l, "Vitesse", "20");
	gt_macros_commit_edits(m, l->rows, l->n, NULL);
	lignes_free(l);
	/* Défaut inchangé (20), maximum 30 : le 50 du modèle serait refusé */
	l = lignes_de(m);
	lignes_max(l, "Vitesse", "30");
	g_assert_cmpint(gt_macros_classify_edits(m, l->rows, l->n, NULL), ==, GT_MACROS_EDIT_STRUCTURE);
	g_assert_cmpint(gt_macros_save_template_edits(m, l->rows, l->n, NULL, FALSE), ==, GT_MACROS_SAVED);
	lignes_free(l);
	g_assert_true(gt_macro_file_load(path, &relu, NULL));
	g_assert_cmpstr(defaut(gt_macro_set_find_label(relu, "Vitesse"), 0), ==, "20");
	g_assert_cmpstr(gt_macro_set_find_label(relu, "Vitesse")->arg_max[0], ==, "30");
	gt_macro_set_free(relu);
	gt_macros_free(m);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

/* Échec d'écriture : message, rien appliqué (jeu effectif, base et fichier inchangés) */
static void
test_enregistrer_echec(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-modele-XXXXXX", NULL);
	gchar *path = ecrire_modele(dir, "modele.ini", "10");
	gchar *avant = lire_texte(path), *apres;
	GtMacros *m = nouvelles_macros();
	Lignes *l;
	guint64 g0;

	if(geteuid() == 0)
	{
		g_test_skip("root ignore les droits du dossier");
		g_free(avant);
		gt_macros_free(m);
		test_rmdir(dir);
		g_free(path);
		g_free(dir);
		return;
	}
	g_assert_true(gt_macros_link_template(m, path));
	g_assert_cmpint(g_chmod(dir, 0500), ==, 0);
	l = lignes_de(m);
	lignes_defaut(l, "Vitesse", "20");
	g0 = gt_macros_get_generation(m);
	stub_reset();
	g_assert_cmpint(gt_macros_save_template_edits(m, l->rows, l->n, NULL, FALSE), ==, GT_MACROS_FAILED);
	lignes_free(l);
	g_assert_cmpint(g_chmod(dir, 0700), ==, 0);
	g_assert_cmpint(stub_count("message"), ==, 1);
	g_assert_nonnull(strstr(stub_last("message"), "Cannot save macros template"));
	g_assert_cmpint(stub_count("status_flash"), ==, 0);
	g_assert_cmpint(stub_count("macros_changed"), ==, 0);
	g_assert_cmpuint(gt_macros_get_generation(m), ==, g0);
	g_assert_cmpstr(defaut(macro_nommee(m, "Vitesse"), 0), ==, "10");
	g_assert_cmpstr(defaut(gt_macro_set_find_label(gt_macros_get_template_set(m), "Vitesse"), 0), ==, "10");
	apres = lire_texte(path);
	g_assert_cmpstr(apres, ==, avant);
	/* Droits rendus : l'identité n'a pas bougé, l'enregistrement passe (pas de conflit) */
	l = lignes_de(m);
	lignes_defaut(l, "Vitesse", "20");
	g_assert_cmpint(gt_macros_save_template_edits(m, l->rows, l->n, NULL, FALSE), ==, GT_MACROS_SAVED);
	lignes_free(l);
	g_assert_cmpstr(defaut(macro_nommee(m, "Vitesse"), 0), ==, "20");
	gt_macros_free(m);
	test_rmdir(dir);
	g_free(avant);
	g_free(apres);
	g_free(path);
	g_free(dir);
}

/* Modèle réécrit par un tiers : CONFLICT, rien écrit ni appliqué ; forcé : écrit */
static void
test_conflit_puis_force(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-modele-XXXXXX", NULL);
	gchar *path = ecrire_modele(dir, "modele.ini", "10");
	gchar *tiers = NULL, *texte;
	GtMacros *m = nouvelles_macros();
	GtMacroSet *relu = NULL;
	Lignes *l;

	g_assert_true(gt_macros_link_template(m, path));
	g_free(ecrire_modele(dir, "modele.ini", "77"));
	tiers = lire_texte(path);
	l = lignes_de(m);
	lignes_defaut(l, "Mode", "5");
	stub_reset();
	g_assert_cmpint(gt_macros_save_template_edits(m, l->rows, l->n, NULL, FALSE), ==, GT_MACROS_CONFLICT);
	texte = lire_texte(path);
	g_assert_cmpstr(texte, ==, tiers);
	g_free(texte);
	g_assert_cmpstr(defaut(macro_nommee(m, "Mode"), 0), ==, "1");
	g_assert_cmpint(stub_count("message"), ==, 0);
	g_assert_cmpint(stub_count("macros_changed"), ==, 0);
	g_assert_cmpint(gt_macros_save_template_edits(m, l->rows, l->n, NULL, TRUE), ==, GT_MACROS_SAVED);
	lignes_free(l);
	g_assert_true(gt_macro_file_load(path, &relu, NULL));
	g_assert_cmpstr(defaut(gt_macro_set_find_label(relu, "Mode"), 0), ==, "5");
	g_assert_cmpstr(defaut(gt_macro_set_find_label(relu, "Vitesse"), 0), ==, "10");
	gt_macro_set_free(relu);
	g_assert_cmpstr(defaut(macro_nommee(m, "Mode"), 0), ==, "5");
	/* Identité reprise après l'écriture : un nouvel enregistrement passe sans conflit */
	l = lignes_de(m);
	lignes_defaut(l, "Mode", "6");
	g_assert_cmpint(gt_macros_save_template_edits(m, l->rows, l->n, NULL, FALSE), ==, GT_MACROS_SAVED);
	lignes_free(l);
	gt_macros_free(m);
	test_rmdir(dir);
	g_free(tiers);
	g_free(path);
	g_free(dir);
}

/* Rechargement (Reload du conflit) : modèle relu, valeurs propres actuelles réappliquées ; lecture impossible : message, rien ne change */
static void
test_rechargement(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-modele-XXXXXX", NULL);
	gchar *path = ecrire_modele(dir, "modele.ini", "10");
	GtMacros *m = nouvelles_macros();
	GtMacroSet *set;
	Lignes *l;
	guint64 g0;

	g_assert_true(gt_macros_link_template(m, path));
	l = lignes_de(m);
	lignes_defaut(l, "Mode", "9");
	gt_macros_commit_edits(m, l->rows, l->n, NULL);
	lignes_free(l);
	g_free(ecrire_modele(dir, "modele.ini", "77"));
	g0 = gt_macros_get_generation(m);
	stub_reset();
	g_assert_true(gt_macros_reload_template(m));
	g_assert_cmpuint(gt_macros_get_generation(m), >, g0);
	g_assert_cmpstr(defaut(macro_nommee(m, "Vitesse"), 0), ==, "77");
	g_assert_cmpstr(defaut(macro_nommee(m, "Mode"), 0), ==, "9");
	g_assert_cmpstr(defaut(gt_macro_set_find_label(gt_macros_get_template_set(m), "Vitesse"), 0), ==, "77");
	/* Identité reprise : enregistrer ensuite ne fait pas conflit */
	l = lignes_de(m);
	lignes_defaut(l, "Mode", "8");
	g_assert_cmpint(gt_macros_save_template_edits(m, l->rows, l->n, NULL, FALSE), ==, GT_MACROS_SAVED);
	lignes_free(l);
	/* Modèle illisible : message, rien ne change */
	g_assert_true(g_file_set_contents(path, "[[[\n", -1, NULL));
	set = gt_macros_get_set(m);
	g0 = gt_macros_get_generation(m);
	stub_reset();
	g_assert_false(gt_macros_reload_template(m));
	g_assert_cmpint(stub_count("message"), ==, 1);
	g_assert_true(gt_macros_get_set(m) == set);
	g_assert_cmpuint(gt_macros_get_generation(m), ==, g0);
	g_assert_cmpstr(gt_macros_get_template(m), ==, path);
	/* Non interactif : avertissement seulement */
	g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*Cannot load macros file*");
	g_assert_false(gt_macros_refresh_template(m, FALSE));
	g_test_assert_expected_messages();
	g_assert_cmpint(stub_count("message"), ==, 1);
	gt_macros_free(m);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

/* Onglets frères : l'un enregistre une valeur dans le modèle, l'autre est rafraîchi en place ; son polling en marche continue, ses valeurs propres
 * restent ; l'usage n'est effacé que pour les macros dont le défaut change */
static void
test_rafraichir_en_place(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-modele-XXXXXX", NULL);
	gchar *path = ecrire_modele(dir, "modele.ini", "10");
	GtMacros *a = nouvelles_macros(), *b = nouvelles_macros();
	GtMacroSet *set_b;
	GtMacroOverrides *o;
	Lignes *l;
	guint ping, id_m, id_c;
	guint64 g0;

	g_assert_true(gt_macros_link_template(a, path));
	g_assert_true(gt_macros_link_template(b, path));
	/* b : valeur propre Vitesse 20, polling de Ping (valeur propre aussi), usages */
	l = lignes_de(b);
	lignes_defaut(l, "Vitesse", "20");
	gt_macros_commit_edits(b, l->rows, l->n, NULL);
	lignes_free(l);
	ping = macro_nommee(b, "Ping")->id;
	g_assert_true(gt_macro_set_set_polling(gt_macros_get_set(b), ping, TRUE, 500));
	gt_macros_edited(b);
	stub_reset();
	g_assert_true(gt_macro_poller_start(gt_macros_get_poller(b), ping));
	id_m = macro_nommee(b, "Mode")->id;
	id_c = macro_nommee(b, "Choix")->id;
	gt_macros_set_usage_arg(b, id_m, 0, "3");
	gt_macros_set_usage_arg(b, id_c, 0, "2");
	set_b = gt_macros_get_set(b);
	/* a : Mode 5 enregistré dans le modèle */
	l = lignes_de(a);
	lignes_defaut(l, "Mode", "5");
	g_assert_cmpint(gt_macros_save_template_edits(a, l->rows, l->n, NULL, FALSE), ==, GT_MACROS_SAVED);
	lignes_free(l);
	g0 = gt_macros_get_generation(b);
	stub_reset();
	g_assert_true(gt_macros_refresh_template(b, FALSE));
	g_assert_true(gt_macros_get_set(b) == set_b);
	g_assert_true(gt_macro_poller_is_running(gt_macros_get_poller(b), ping));
	g_assert_cmpint(stub_count("macro_polling_changed"), ==, 0);
	g_assert_cmpint(stub_count("macros_changed"), ==, 1);
	g_assert_cmpuint(gt_macros_get_generation(b), >, g0);
	g_assert_cmpstr(defaut(macro_nommee(b, "Mode"), 0), ==, "5");
	g_assert_cmpstr(defaut(macro_nommee(b, "Vitesse"), 0), ==, "20");
	g_assert_true(macro_nommee(b, "Ping")->polling);
	g_assert_cmpuint(macro_nommee(b, "Ping")->polling_period_ms, ==, 500);
	g_assert_false(gt_macros_has_usage(b, id_m));
	g_assert_true(gt_macros_has_usage(b, id_c));
	g_assert_cmpstr(defaut(gt_macro_set_find_label(gt_macros_get_template_set(b), "Mode"), 0), ==, "5");
	o = gt_macros_get_overrides(b);
	g_assert_cmpuint(gt_macro_overrides_count(o), ==, 2);
	gt_macro_overrides_free(o);
	/* Identité reprise : b enregistre ensuite sans conflit */
	l = lignes_de(b);
	lignes_defaut(l, "Mode", "6");
	g_assert_cmpint(gt_macros_save_template_edits(b, l->rows, l->n, NULL, FALSE), ==, GT_MACROS_SAVED);
	lignes_free(l);
	gt_macros_free(a);
	gt_macros_free(b);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

/* Changement de structure enregistré par un frère : l'autre est rafraîchi par remplacement (polling arrêté) */
static void
test_rafraichir_par_remplacement(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-modele-XXXXXX", NULL);
	gchar *path = ecrire_modele(dir, "modele.ini", "10");
	GtMacros *a = nouvelles_macros(), *b = nouvelles_macros();
	GtMacroSet *set_b;
	Lignes *l;
	guint ping;

	g_assert_true(gt_macros_link_template(a, path));
	g_assert_true(gt_macros_link_template(b, path));
	l = lignes_de(b);
	lignes_defaut(l, "Vitesse", "20");
	gt_macros_commit_edits(b, l->rows, l->n, NULL);
	lignes_free(l);
	ping = macro_nommee(b, "Ping")->id;
	g_assert_true(gt_macro_set_set_polling(gt_macros_get_set(b), ping, TRUE, 500));
	g_assert_true(gt_macro_poller_start(gt_macros_get_poller(b), ping));
	set_b = gt_macros_get_set(b);
	l = lignes_de(a);
	l->rows[rang(l, "Mode")].label = "Mode2";
	g_assert_cmpint(gt_macros_save_template_edits(a, l->rows, l->n, NULL, FALSE), ==, GT_MACROS_SAVED);
	lignes_free(l);
	stub_reset();
	g_assert_true(gt_macros_refresh_template(b, FALSE));
	g_assert_true(gt_macros_get_set(b) != set_b);
	g_assert_cmpuint(gt_macro_poller_running_count(gt_macros_get_poller(b)), ==, 0);
	g_assert_nonnull(gt_macro_set_find_label(gt_macros_get_set(b), "Mode2"));
	g_assert_cmpstr(defaut(macro_nommee(b, "Vitesse"), 0), ==, "20");
	g_assert_true(macro_nommee(b, "Ping")->polling);	/* réglage gardé, marche arrêtée */
	g_assert_true(gt_macro_set_same_structure(gt_macros_get_template_set(b), gt_macros_get_set(b)));
	gt_macros_free(a);
	gt_macros_free(b);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

/* Détacher : modèle, base et identité oubliés ; jeu effectif gardé tel quel ; flash ; edited ; non lié : rien */
static void
test_detacher(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-modele-XXXXXX", NULL);
	gchar *path = ecrire_modele(dir, "modele.ini", "10");
	GtMacros *m = nouvelles_macros();
	GtMacroSet *set;
	Lignes *l;
	guint64 g0;
	guint id_m, ping;

	g_assert_true(gt_macros_link_template(m, path));
	l = lignes_de(m);
	lignes_defaut(l, "Vitesse", "20");
	gt_macros_commit_edits(m, l->rows, l->n, NULL);
	lignes_free(l);
	id_m = macro_nommee(m, "Mode")->id;
	gt_macros_set_usage_arg(m, id_m, 0, "3");
	ping = macro_nommee(m, "Ping")->id;
	g_assert_true(gt_macro_set_set_polling(gt_macros_get_set(m), ping, TRUE, 500));
	g_assert_true(gt_macro_poller_start(gt_macros_get_poller(m), ping));
	set = gt_macros_get_set(m);
	gt_macros_set_edited_func(m, rappel_edited, NULL);
	compteur_edited = 0;
	g0 = gt_macros_get_generation(m);
	stub_reset();
	gt_macros_detach_template(m);
	g_assert_true(gt_macro_poller_is_running(gt_macros_get_poller(m), ping));
	g_assert_cmpint(stub_count("macro_polling_changed"), ==, 0);
	g_assert_null(gt_macros_get_template(m));
	g_assert_null(gt_macros_get_template_set(m));
	g_assert_null(gt_macros_get_overrides(m));
	g_assert_true(gt_macros_get_set(m) == set);
	g_assert_cmpstr(defaut(macro_nommee(m, "Vitesse"), 0), ==, "20");
	g_assert_true(gt_macros_has_usage(m, id_m));
	g_assert_cmpint(compteur_edited, ==, 1);
	g_assert_cmpuint(gt_macros_get_generation(m), >, g0);
	g_assert_cmpint(stub_count("status_flash"), ==, 1);
	g_assert_nonnull(strstr(stub_last("status_flash"), "Detached from macros template modele.ini"));
	/* Non lié : rien */
	g0 = gt_macros_get_generation(m);
	gt_macros_detach_template(m);
	g_assert_cmpint(compteur_edited, ==, 1);
	g_assert_cmpint(stub_count("status_flash"), ==, 1);
	g_assert_cmpuint(gt_macros_get_generation(m), ==, g0);
	/* Enregistrer dans le modèle sans modèle : échec */
	l = lignes_de(m);
	g_assert_cmpint(gt_macros_save_template_edits(m, l->rows, l->n, NULL, FALSE), ==, GT_MACROS_FAILED);
	g_assert_false(gt_macros_refresh_template(m, FALSE));
	lignes_free(l);
	gt_macros_free(m);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

/* Génération : relevée par chaque changement du jeu effectif ou du lien (§2.1) */
static void
test_generation(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-modele-XXXXXX", NULL);
	gchar *path = ecrire_modele(dir, "modele.ini", "10");
	GtMacros *m = nouvelles_macros();
	Lignes *l;
	guint64 g;

	g = gt_macros_get_generation(m);
	gt_macros_replace_set(m, gt_macro_set_new());
	g_assert_cmpuint(gt_macros_get_generation(m), >, g);
	g = gt_macros_get_generation(m);
	g_assert_true(gt_macros_link_template(m, path));
	g_assert_cmpuint(gt_macros_get_generation(m), >, g);
	g = gt_macros_get_generation(m);
	gt_macros_edited(m);
	g_assert_cmpuint(gt_macros_get_generation(m), >, g);
	l = lignes_de(m);
	lignes_defaut(l, "Vitesse", "20");
	g = gt_macros_get_generation(m);
	gt_macros_apply_edits(m, l->rows, l->n, NULL);
	g_assert_cmpuint(gt_macros_get_generation(m), >, g);
	g = gt_macros_get_generation(m);
	gt_macros_commit_edits(m, l->rows, l->n, NULL);
	g_assert_cmpuint(gt_macros_get_generation(m), >, g);
	lignes_free(l);
	g = gt_macros_get_generation(m);
	g_assert_true(gt_macros_refresh_template(m, FALSE));
	g_assert_cmpuint(gt_macros_get_generation(m), >, g);
	g = gt_macros_get_generation(m);
	gt_macros_detach_template(m);
	g_assert_cmpuint(gt_macros_get_generation(m), >, g);
	/* Lecture seule : inchangée */
	g = gt_macros_get_generation(m);
	l = lignes_de(m);
	gt_macros_classify_edits(m, l->rows, l->n, NULL);
	lignes_free(l);
	gt_macros_get_args(m, macro_nommee(m, "Vitesse")->id);
	g_assert_cmpuint(gt_macros_get_generation(m), ==, g);
	gt_macros_free(m);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

/* Ouverture d'un projet : modèle relu identique à la copie du projet (UNCHANGED), différent (UPDATED), absent (MISSING : copie du projet, lien
 * gardé) ; valeurs propres réappliquées, abandons comptés */
static void
test_lien_depuis_projet(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-modele-XXXXXX", NULL);
	gchar *path = ecrire_modele(dir, "modele.ini", "10");
	gchar *absent = g_build_filename(dir, "absent.ini", NULL);
	GtMacroSet *copie = jeu_modele("10");
	GtMacroOverrides *o = gt_macro_overrides_new(), *diff;
	GtMacros *m = nouvelles_macros();
	gchar *erreur = NULL;
	guint dropped = 99;
	guint ping;

	gt_macro_overrides_set_arg(o, "Mode", 0, 0, "7");
	gt_macro_overrides_set_polling(o, "Ping", 0, TRUE, 250);
	/* Polling en marche et usage posés avant : remis à zéro par l'ouverture */
	{
		guint p = gt_macro_set_append(gt_macros_get_set(m), "P", "", "P%d", "");

		g_assert_true(gt_macro_set_set_polling(gt_macros_get_set(m), p, TRUE, 500));
		g_assert_true(gt_macro_poller_start(gt_macros_get_poller(m), p));
		gt_macros_set_usage_arg(m, p, 0, "4");
		g_assert_true(gt_macros_has_usage(m, 0));
	}
	/* UNCHANGED, rien d'abandonné */
	g_assert_cmpint(gt_macros_link_from_project(m, path, copie, o, &dropped, &erreur), ==, GT_MACROS_TEMPLATE_UNCHANGED);
	g_assert_cmpuint(dropped, ==, 0);
	g_assert_null(erreur);
	g_assert_false(gt_macros_has_usage(m, 0));
	g_assert_cmpstr(gt_macros_get_template(m), ==, path);
	g_assert_cmpstr(defaut(macro_nommee(m, "Mode"), 0), ==, "7");
	ping = macro_nommee(m, "Ping")->id;
	g_assert_true(macro_nommee(m, "Ping")->polling);
	g_assert_cmpuint(gt_macro_poller_running_count(gt_macros_get_poller(m)), ==, 0);
	g_assert_false(gt_macro_poller_is_running(gt_macros_get_poller(m), ping));
	diff = gt_macros_get_overrides(m);
	g_assert_cmpuint(gt_macro_overrides_count(diff), ==, 2);
	gt_macro_overrides_free(diff);
	/* UPDATED : le modèle a changé depuis le dernier enregistrement du projet ; valeur propre abandonnée (macro disparue, limite) */
	g_free(ecrire_modele(dir, "modele.ini", "55"));
	gt_macro_overrides_set_arg(o, "Disparue", 0, 0, "1");
	gt_macro_overrides_set_arg(o, "Vitesse", 0, 0, "500");	/* au-dessus du maximum 100 */
	g_assert_cmpint(gt_macros_link_from_project(m, path, copie, o, &dropped, &erreur), ==, GT_MACROS_TEMPLATE_UPDATED);
	g_assert_cmpuint(dropped, ==, 2);
	g_assert_null(erreur);
	g_assert_cmpstr(defaut(macro_nommee(m, "Vitesse"), 0), ==, "55");
	g_assert_cmpstr(defaut(macro_nommee(m, "Mode"), 0), ==, "7");
	g_assert_cmpstr(defaut(gt_macro_set_find_label(gt_macros_get_template_set(m), "Vitesse"), 0), ==, "55");
	/* L'identité est celle du fichier lu : enregistrer passe */
	{
		Lignes *l = lignes_de(m);

		g_assert_cmpint(gt_macros_save_template_edits(m, l->rows, l->n, NULL, FALSE), ==, GT_MACROS_SAVED);
		lignes_free(l);
	}
	/* MISSING : copie du projet, lien gardé (chemin), identité « absent » ; le fichier réapparu est un conflit */
	g_assert_cmpint(gt_macros_link_from_project(m, absent, copie, o, &dropped, &erreur), ==, GT_MACROS_TEMPLATE_MISSING);
	g_assert_nonnull(erreur);
	g_assert_cmpuint(dropped, ==, 2);
	g_assert_cmpstr(gt_macros_get_template(m), ==, absent);
	g_assert_cmpstr(defaut(macro_nommee(m, "Vitesse"), 0), ==, "10");
	g_assert_cmpstr(defaut(macro_nommee(m, "Mode"), 0), ==, "7");
	g_assert_true(gt_macro_set_same_structure(gt_macros_get_template_set(m), copie));
	g_clear_pointer(&erreur, g_free);
	g_free(ecrire_modele(dir, "absent.ini", "10"));
	{
		Lignes *l = lignes_de(m);

		g_assert_cmpint(gt_macros_save_template_edits(m, l->rows, l->n, NULL, FALSE), ==, GT_MACROS_CONFLICT);
		lignes_free(l);
	}
	/* Paramètres de sortie facultatifs */
	g_assert_cmpint(gt_macros_link_from_project(m, path, copie, NULL, NULL, NULL), ==, GT_MACROS_TEMPLATE_UPDATED);
	g_assert_cmpstr(defaut(macro_nommee(m, "Mode"), 0), ==, "1");
	gt_macros_free(m);
	gt_macro_overrides_free(o);
	gt_macro_set_free(copie);
	test_rmdir(dir);
	g_free(absent);
	g_free(path);
	g_free(dir);
}

/* Nouvel onglet d'un projet : jeu et lien copiés avant le passage en mode intégré, que set_embedded(TRUE) ne touche pas */
static void
test_copy_from(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-modele-XXXXXX", NULL);
	gchar *path = ecrire_modele(dir, "modele.ini", "10");
	GtMacros *src = nouvelles_macros();
	GtMacros *dst = gt_macros_new(stub_link_default(), NULL);
	GtMacros *libre = gt_macros_new(stub_link_default(), NULL);
	GtMacroOverrides *o;
	Lignes *l;

	g_assert_true(gt_macros_link_template(src, path));
	l = lignes_de(src);
	lignes_defaut(l, "Vitesse", "20");
	gt_macros_commit_edits(src, l->rows, l->n, NULL);
	lignes_free(l);
	gt_macros_copy_from(dst, src);
	gt_macros_set_embedded(dst, TRUE);
	g_assert_cmpstr(gt_macros_get_template(dst), ==, path);
	g_assert_true(gt_macros_get_template_set(dst) != gt_macros_get_template_set(src));
	g_assert_true(gt_macros_get_set(dst) != gt_macros_get_set(src));
	g_assert_cmpstr(defaut(macro_nommee(dst, "Vitesse"), 0), ==, "20");
	o = gt_macros_get_overrides(dst);
	g_assert_cmpuint(gt_macro_overrides_count(o), ==, 1);
	gt_macro_overrides_free(o);
	/* Même identité : enregistrer depuis la copie passe */
	l = lignes_de(dst);
	lignes_defaut(l, "Mode", "5");
	g_assert_cmpint(gt_macros_save_template_edits(dst, l->rows, l->n, NULL, FALSE), ==, GT_MACROS_SAVED);
	lignes_free(l);
	/* Source non liée : la destination ne l'est plus */
	gt_macros_copy_from(dst, libre);
	g_assert_null(gt_macros_get_template(dst));
	g_assert_cmpuint(gt_macro_set_count(gt_macros_get_set(dst)), ==, 0);
	gt_macros_free(src);
	gt_macros_free(dst);
	gt_macros_free(libre);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

/* Sortie du projet (set_embedded(FALSE)) : détache ; Load macros file… en mode intégré : détache s'il réussit, garde le lien s'il échoue */
static void
test_mode_integre(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-modele-XXXXXX", NULL);
	gchar *path = ecrire_modele(dir, "modele.ini", "10");
	gchar *autre = ecrire_modele(dir, "autre.ini", "30");
	gchar *corrompu = test_write_file(dir, "corrompu.ini", "[[[\n");
	GtMacros *m = nouvelles_macros();

	g_assert_true(gt_macros_link_template(m, path));
	gt_macros_set_embedded(m, TRUE);
	g_assert_cmpstr(gt_macros_get_template(m), ==, path);
	gt_macros_set_embedded(m, FALSE);
	g_assert_null(gt_macros_get_template(m));
	g_assert_null(gt_macros_get_template_set(m));
	gt_macros_set_embedded(m, TRUE);
	g_assert_true(gt_macros_link_template(m, path));
	stub_reset();
	g_assert_false(gt_macros_load_file(m, corrompu, TRUE));
	g_assert_cmpint(stub_count("message"), ==, 1);
	g_assert_cmpstr(gt_macros_get_template(m), ==, path);
	g_assert_true(gt_macros_load_file(m, autre, TRUE));
	g_assert_null(gt_macros_get_template(m));
	g_assert_null(gt_macros_get_path(m));
	g_assert_cmpstr(defaut(macro_nommee(m, "Vitesse"), 0), ==, "30");
	/* reload et save_file ignorent le modèle */
	g_assert_true(gt_macros_link_template(m, path));
	g_assert_false(gt_macros_reload(m));
	gt_macros_free(m);
	test_rmdir(dir);
	g_free(corrompu);
	g_free(autre);
	g_free(path);
	g_free(dir);
}

/* Tout chargement qui remplace le jeu oublie le lien : non interactif en échec (jeu vide), fichier absent, onglet lié non intégré ; set_embedded(FALSE)
 * appelle edited ; save_file n'écrit jamais le modèle */
static void
test_chargement_oublie_lien(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-modele-XXXXXX", NULL);
	gchar *path = ecrire_modele(dir, "modele.ini", "10");
	gchar *autre = ecrire_modele(dir, "autre.ini", "30");
	gchar *export = g_build_filename(dir, "export.ini", NULL);
	gchar *absent = g_build_filename(dir, "absent.ini", NULL);
	gchar *corrompu = test_write_file(dir, "corrompu.ini", "[[[\n");
	gchar *avant = lire_texte(path), *apres;
	GtMacros *m = nouvelles_macros(), *libre;
	guint64 g;

	/* Intégré, non interactif, fichier corrompu : jeu vide installé, lien oublié */
	g_assert_true(gt_macros_link_template(m, path));
	g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*Cannot load macros file*");
	g_assert_false(gt_macros_load_file(m, corrompu, FALSE));
	g_test_assert_expected_messages();
	g_assert_null(gt_macros_get_template(m));
	g_assert_cmpuint(gt_macro_set_count(gt_macros_get_set(m)), ==, 0);
	/* Intégré, fichier absent (NOENT) : jeu vide, lien oublié */
	g_assert_true(gt_macros_link_template(m, path));
	g_assert_true(gt_macros_load_file(m, absent, TRUE));
	g_assert_null(gt_macros_get_template(m));
	/* save_file (export) n'écrit pas le modèle */
	g_assert_true(gt_macros_link_template(m, path));
	g_assert_true(gt_macro_set_set_arg(gt_macros_get_set(m), macro_nommee(m, "Mode")->id, 0, "9"));
	g_assert_cmpint(gt_macros_save_file(m, export, FALSE, FALSE), ==, GT_MACROS_SAVED);
	apres = lire_texte(path);
	g_assert_cmpstr(apres, ==, avant);
	g_free(apres);
	/* set_embedded(FALSE) : edited et génération */
	gt_macros_set_edited_func(m, rappel_edited, NULL);
	compteur_edited = 0;
	g = gt_macros_get_generation(m);
	gt_macros_set_embedded(m, FALSE);
	g_assert_null(gt_macros_get_template(m));
	g_assert_cmpint(compteur_edited, ==, 1);
	g_assert_cmpuint(gt_macros_get_generation(m), >, g);
	/* Déjà non lié : set_embedded(FALSE) n'appelle rien */
	gt_macros_set_embedded(m, FALSE);
	g_assert_cmpint(compteur_edited, ==, 1);
	gt_macros_free(m);
	/* GtMacros lié non intégré (avant passage en projet) : un chargement oublie le lien et retient le fichier */
	libre = gt_macros_new(stub_link_default(), NULL);
	g_assert_true(gt_macros_link_template(libre, path));
	g_assert_true(gt_macros_load_file(libre, autre, TRUE));
	g_assert_null(gt_macros_get_template(libre));
	g_assert_cmpstr(gt_macros_get_path(libre), ==, autre);
	gt_macros_free(libre);
	test_rmdir(dir);
	g_free(avant);
	g_free(corrompu);
	g_free(absent);
	g_free(export);
	g_free(autre);
	g_free(path);
	g_free(dir);
}

/* Enregistrer dans le modèle avec des listes : entrée ajoutée (STRUCTURE), écrite ; base et effectif de même structure ; listes de l'appelant intactes */
static void
test_enregistrer_listes(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-modele-XXXXXX", NULL);
	gchar *path = ecrire_modele(dir, "modele.ini", "10");
	GtMacros *m = nouvelles_macros();
	GtMacroSet *relu = NULL;
	GtMacroLists *listes;
	const GtMacroLists *lr;
	Lignes *l;

	g_assert_true(gt_macros_link_template(m, path));
	listes = gt_macro_lists_copy(gt_macro_set_get_lists(gt_macros_get_set(m)));
	gt_macro_lists_add(listes, "Liste", "c", "3");
	l = lignes_de(m);
	g_assert_cmpint(gt_macros_classify_edits(m, l->rows, l->n, listes), ==, GT_MACROS_EDIT_STRUCTURE);
	g_assert_cmpint(gt_macros_save_template_edits(m, l->rows, l->n, listes, FALSE), ==, GT_MACROS_SAVED);
	lignes_free(l);
	g_assert_true(gt_macro_file_load(path, &relu, NULL));
	lr = gt_macro_set_get_lists(relu);
	g_assert_cmpint(gt_macro_lists_entry_count(lr, gt_macro_lists_find(lr, "Liste")), ==, 3);
	g_assert_cmpstr(gt_macro_lists_entry_value(lr, gt_macro_lists_find(lr, "Liste"), 2), ==, "3");
	gt_macro_set_free(relu);
	g_assert_true(gt_macro_set_same_structure(gt_macros_get_template_set(m), gt_macros_get_set(m)));
	g_assert_cmpint(gt_macro_lists_entry_count(gt_macro_set_get_lists(gt_macros_get_set(m)), 0), ==, 3);
	g_assert_cmpint(gt_macro_lists_entry_count(listes, gt_macro_lists_find(listes, "Liste")), ==, 3);
	gt_macro_lists_free(listes);
	gt_macros_free(m);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

/* Garde-fou : un jeu d'une autre structure installé sur un onglet lié oublie le lien (avertissement) ; de même structure, le lien reste */
static void
test_remplacement_garde_fou(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-modele-XXXXXX", NULL);
	gchar *path = ecrire_modele(dir, "modele.ini", "10");
	GtMacros *m = nouvelles_macros();

	g_assert_true(gt_macros_link_template(m, path));
	gt_macros_replace_set(m, jeu_modele("42"));
	g_assert_cmpstr(gt_macros_get_template(m), ==, path);
	g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*template forgotten*");
	gt_macros_replace_set(m, gt_macro_set_new());
	g_test_assert_expected_messages();
	g_assert_null(gt_macros_get_template(m));
	g_assert_null(gt_macros_get_template_set(m));
	gt_macros_free(m);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

/* Rafraîchissement : période changée par le modèle appliquée au polling en marche, polling coupé par le modèle (arrêt signalé) ; valeur propre devenue
 * invalide (limite durcie, donc changement de structure et remplacement) : valeur du modèle reprise, usage effacé */
static void
test_rafraichir_polling_et_invalide(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-modele-XXXXXX", NULL);
	GtMacroSet *set = jeu_modele("10");
	gchar *path;
	GtMacros *m = nouvelles_macros();
	const gchar *mx[] = { "50", NULL };
	Lignes *l;
	guint ping, id_v;

	g_assert_true(gt_macro_set_set_polling(set, gt_macro_set_find_label(set, "Ping")->id, TRUE, 500));
	path = ecrire_jeu(dir, "modele.ini", set);
	g_assert_true(gt_macros_link_template(m, path));
	ping = macro_nommee(m, "Ping")->id;
	g_assert_true(macro_nommee(m, "Ping")->polling);
	g_assert_true(gt_macro_poller_start(gt_macros_get_poller(m), ping));
	/* Période 200 dans le modèle : en place, le polling continue à la nouvelle période */
	set = jeu_modele("10");
	g_assert_true(gt_macro_set_set_polling(set, gt_macro_set_find_label(set, "Ping")->id, TRUE, 200));
	g_free(ecrire_jeu(dir, "modele.ini", set));
	stub_reset();
	g_assert_true(gt_macros_refresh_template(m, FALSE));
	g_assert_cmpuint(macro_nommee(m, "Ping")->id, ==, ping);
	g_assert_cmpuint(macro_nommee(m, "Ping")->polling_period_ms, ==, 200);
	g_assert_true(gt_macro_poller_is_running(gt_macros_get_poller(m), ping));
	g_assert_cmpint(stub_count("macro_polling_changed"), ==, 0);
	/* Polling coupé dans le modèle : arrêté, crochet d'état */
	g_free(ecrire_jeu(dir, "modele.ini", jeu_modele("10")));
	stub_reset();
	g_assert_true(gt_macros_refresh_template(m, FALSE));
	g_assert_cmpuint(macro_nommee(m, "Ping")->id, ==, ping);
	g_assert_false(macro_nommee(m, "Ping")->polling);
	g_assert_false(gt_macro_poller_is_running(gt_macros_get_poller(m), ping));
	g_assert_cmpint(stub_count("macro_polling_changed"), ==, 1);
	g_assert_cmpstr(stub_last("macro_polling_changed"), ==, "0");
	/* Valeur propre 80, puis maximum 50 dans le modèle : abandonnée, valeur du modèle reprise, usage effacé */
	l = lignes_de(m);
	lignes_defaut(l, "Vitesse", "80");
	gt_macros_commit_edits(m, l->rows, l->n, NULL);
	lignes_free(l);
	id_v = macro_nommee(m, "Vitesse")->id;
	gt_macros_set_usage_arg(m, id_v, 0, "33");
	set = jeu_modele("10");
	g_assert_cmpint(gt_macro_set_set_limits(set, gt_macro_set_find_label(set, "Vitesse")->id, NULL, mx, NULL, 1), ==, 0);
	g_free(ecrire_jeu(dir, "modele.ini", set));
	g_assert_true(gt_macros_refresh_template(m, FALSE));
	id_v = macro_nommee(m, "Vitesse")->id;
	g_assert_cmpstr(defaut(macro_nommee(m, "Vitesse"), 0), ==, "10");
	g_assert_false(gt_macros_has_usage(m, id_v));
	g_assert_true(gt_macro_set_same_structure(gt_macros_get_template_set(m), gt_macros_get_set(m)));
	gt_macros_free(m);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

int
main(int argc, char **argv)
{
	gchar *racine;
	gint ret;

	g_test_init(&argc, &argv, NULL);
	racine = test_private_dirs();
	g_assert_true(g_str_has_prefix(g_get_user_config_dir(), racine));
	gt_app_ui_set(&table_enregistrement);
	g_test_add_func("/macros_template/lier", test_lier);
	g_test_add_func("/macros_template/lier-absent-refuse", test_lier_absent_refuse);
	g_test_add_func("/macros_template/classer", test_classer);
	g_test_add_func("/macros_template/garder-dans-le-projet", test_garder_dans_le_projet);
	g_test_add_func("/macros_template/enregistrer-dans-le-modele", test_enregistrer_dans_le_modele);
	g_test_add_func("/macros_template/enregistrer-limite-durcie", test_enregistrer_limite_durcie);
	g_test_add_func("/macros_template/enregistrer-echec", test_enregistrer_echec);
	g_test_add_func("/macros_template/conflit-puis-force", test_conflit_puis_force);
	g_test_add_func("/macros_template/rechargement", test_rechargement);
	g_test_add_func("/macros_template/rafraichir-en-place", test_rafraichir_en_place);
	g_test_add_func("/macros_template/rafraichir-par-remplacement", test_rafraichir_par_remplacement);
	g_test_add_func("/macros_template/detacher", test_detacher);
	g_test_add_func("/macros_template/generation", test_generation);
	g_test_add_func("/macros_template/lien-depuis-projet", test_lien_depuis_projet);
	g_test_add_func("/macros_template/copy-from", test_copy_from);
	g_test_add_func("/macros_template/mode-integre", test_mode_integre);
	g_test_add_func("/macros_template/chargement-oublie-lien", test_chargement_oublie_lien);
	g_test_add_func("/macros_template/enregistrer-listes", test_enregistrer_listes);
	g_test_add_func("/macros_template/remplacement-garde-fou", test_remplacement_garde_fou);
	g_test_add_func("/macros_template/rafraichir-polling-et-invalide", test_rafraichir_polling_et_invalide);
	ret = g_test_run();
	test_rmdir_recursive(racine);
	g_free(racine);
	return ret;
}
