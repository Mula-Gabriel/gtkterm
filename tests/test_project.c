/* Tests de GtProject (src/project.c, sous-projet 6c) : capture des sessions, instantané, état « modifié » recalculé à l'inactivité, enregistrement
 * avec garde d'identité. Vraie couche application, ports pty (openpty) ; HOME et XDG privés. */
#include <fcntl.h>
#include <pty.h>
#include <unistd.h>

#include "macro_file.h"
#include "project.h"
#include "test_util.h"

static gboolean
iterate_until(gboolean (*pred)(gpointer), gpointer data, guint ms)
{
	gint64 fin = g_get_monotonic_time() + (gint64)ms * 1000;

	while(!pred(data) && g_get_monotonic_time() < fin)
	{
		if(!g_main_context_iteration(NULL, FALSE))
			g_usleep(1000);
	}
	return pred(data);
}

typedef struct
{
	GtSessionManager *mgr;
	GtProject *p;
	GtSession *a, *b;
	gchar *dir;
	gchar *path;
	gint offset_b;	/* ajouté à h_paned : changer la page ne doit rien modifier */
} Fix;

static gboolean
est_modifie(gpointer data)
{
	return gt_project_is_modified(data);
}

/* Itère ≤ 1 s ; rend TRUE si « modified » est devenu TRUE */
static gboolean
devient_modifie(GtProject *p)
{
	return iterate_until(est_modifie, p, 1000);
}

/* Itère 700 ms (plus que le regroupement) ; rend TRUE si « modified » est resté FALSE */
static gboolean
reste_propre(GtProject *p)
{
	gint64 fin = g_get_monotonic_time() + 700 * 1000;

	while(g_get_monotonic_time() < fin)
	{
		if(!g_main_context_iteration(NULL, FALSE))
			g_usleep(1000);
	}
	return !gt_project_is_modified(p);
}

static void
page_func(GtSession *s, GtProjectTab *tab, gpointer user_data)
{
	Fix *f = user_data;

	tab->page.h_paned = 100 + (gint)gt_session_get_id(s) + f->offset_b;
	tab->page.v_paned = 7;
	g_free(tab->script_layout);
	tab->script_layout = g_strdup("leftright");
}

static GtSession *
nouvelle_session(const gchar *nom)
{
	GtProfile prof;
	GtSession *s;

	gt_profile_init_defaults(&prof);
	s = gt_session_new(&prof, nom);
	gt_profile_clear(&prof);
	return s;
}

static void
fix_init(Fix *f)
{
	f->mgr = gt_session_manager_new();
	f->a = nouvelle_session("banc");
	f->b = nouvelle_session(NULL);
	gt_session_manager_add(f->mgr, f->a, -1);
	gt_session_manager_add(f->mgr, f->b, -1);
	f->p = gt_project_new(f->mgr);
	f->offset_b = 0;
	gt_project_set_page_func(f->p, page_func, f);
	f->dir = g_dir_make_tmp("gtkterm-project-XXXXXX", NULL);
	f->path = g_build_filename(f->dir, "banc.gtkterm", NULL);
}

static void
fix_clear(Fix *f)
{
	g_object_unref(f->p);
	gt_session_shutdown(f->a);
	gt_session_shutdown(f->b);
	g_object_unref(f->mgr);
	g_object_unref(f->a);
	g_object_unref(f->b);
	g_remove(f->path);
	g_rmdir(f->dir);
	g_free(f->path);
	g_free(f->dir);
}

static void
compter(GObject *o, GParamSpec *ps, gpointer user_data)
{
	(*(guint *)user_data)++;
}

static void
test_ferme(void)
{
	GtSessionManager *mgr = gt_session_manager_new();
	GtProject *p = gt_project_new(mgr);
	gchar *nom = gt_project_get_name(p);

	g_assert_false(gt_project_is_open(p));
	g_assert_cmpstr(nom, ==, "");
	g_assert_false(gt_project_is_modified(p));
	g_assert_null(gt_project_get_path(p));
	g_free(nom);
	g_object_unref(p);
	g_object_unref(mgr);
}

static void
test_capture(void)
{
	Fix f;
	GtProjectData *d;
	GtProjectTab *t;
	GtMacroSet *set;
	guint id;
	const gchar *args[] = { "5", NULL };

	fix_init(&f);
	g_object_set(gt_session_get_options(f.a), "echo", TRUE, NULL);
	set = gt_macros_get_set(gt_session_get_macros(f.a));
	id = gt_macro_set_append(set, "M", "", "x%s", "");
	gt_macro_set_set_args(set, id, args, 1);
	gt_macros_set_usage_arg(gt_session_get_macros(f.a), id, 0, "9");
	gt_session_set_script_text(f.a, "x");
	gt_session_manager_set_active(f.mgr, f.b);

	d = gt_project_capture(f.p);
	g_assert_cmpuint(d->tabs->len, ==, 2);
	g_assert_cmpuint(d->active, ==, 1);
	t = g_ptr_array_index(d->tabs, 0);
	g_assert_true(t->profile.echo);
	g_assert_cmpstr(t->profile_name, ==, "banc");
	g_assert_cmpstr(t->script, ==, "x");
	g_assert_cmpint(t->page.h_paned, ==, 100 + (gint)gt_session_get_id(f.a));
	g_assert_null(t->profile.macros_file);
	g_assert_cmpuint(gt_macro_set_count(t->macros), ==, 1);
	g_assert_nonnull(gt_macro_set_get(t->macros, 0)->args);
	g_assert_cmpstr(gt_macro_set_get(t->macros, 0)->args[0], ==, "5");
	g_assert_null(gt_macro_set_get(t->macros, 0)->args[1]);
	t = g_ptr_array_index(d->tabs, 1);
	g_assert_cmpint(t->page.h_paned, ==, 100 + (gint)gt_session_get_id(f.b));
	gt_project_data_free(d);
	fix_clear(&f);
}

static void
test_begin_non_modifie(void)
{
	Fix f;
	gchar *nom;

	fix_init(&f);
	gt_project_begin(f.p, NULL, NULL, NULL);
	g_assert_true(gt_project_is_open(f.p));
	g_assert_null(gt_project_get_path(f.p));
	nom = gt_project_get_name(f.p);
	g_assert_cmpstr(nom, ==, "Untitled project");
	g_free(nom);
	g_assert_false(gt_project_is_modified(f.p));
	g_assert_true(gt_session_get_in_project(f.a));
	g_assert_true(gt_session_get_in_project(f.b));
	fix_clear(&f);
}

static void
enregistrer_propre(Fix *f)
{
	GError *err = NULL;

	g_assert_cmpint(gt_project_save(f->p, f->path, FALSE, &err), ==, GT_PROJECT_SAVED);
	g_assert_no_error(err);
	g_assert_false(gt_project_is_modified(f->p));
}

static void
test_modifie(void)
{
	Fix f;
	GtSession *c;
	GtMacroSet *set;
	guint id;

	fix_init(&f);
	set = gt_macros_get_set(gt_session_get_macros(f.a));
	id = gt_macro_set_append(set, "M", "", "x%s", "");
	gt_project_begin(f.p, NULL, NULL, NULL);
	g_assert_false(gt_project_is_modified(f.p));

	g_object_set(gt_session_get_options(f.a), "echo", TRUE, NULL);
	g_assert_true(devient_modifie(f.p));
	enregistrer_propre(&f);

	gt_macro_set_set_polling(set, id, TRUE, 500);
	gt_macros_changed(gt_session_get_macros(f.a));
	g_assert_true(devient_modifie(f.p));
	enregistrer_propre(&f);

	gt_session_set_script_text(f.b, "print(1)");
	g_assert_true(devient_modifie(f.p));
	enregistrer_propre(&f);

	c = nouvelle_session(NULL);
	gt_session_manager_add(f.mgr, c, -1);
	g_assert_true(gt_session_get_in_project(c));
	g_assert_true(devient_modifie(f.p));
	enregistrer_propre(&f);

	gt_session_manager_move(f.mgr, c, 0);
	g_assert_true(devient_modifie(f.p));
	enregistrer_propre(&f);

	/* Retrait : modifié aussi, et la session partie n'est plus suivie */
	gt_session_manager_remove(f.mgr, c);
	g_assert_true(devient_modifie(f.p));
	enregistrer_propre(&f);
	gt_session_set_script_text(c, "ignore");
	g_assert_true(reste_propre(f.p));
	gt_session_shutdown(c);
	g_object_unref(c);

	/* Ne rendent pas modifié : état de page, session active, valeur d'usage */
	f.offset_b = 50;
	gt_project_check_now(f.p);
	g_assert_false(gt_project_is_modified(f.p));
	gt_session_manager_set_active(f.mgr, f.b);
	gt_macros_set_usage_arg(gt_session_get_macros(f.a), id, 0, "42");
	g_assert_true(reste_propre(f.p));
	fix_clear(&f);
}

static void
test_enregistrer(void)
{
	Fix f;
	GError *err = NULL;
	GtProjectData *d = NULL;
	gchar *contenu;

	fix_init(&f);
	gt_project_begin(f.p, NULL, NULL, NULL);
	/* Sans fichier : échec avec message */
	g_assert_cmpint(gt_project_save(f.p, NULL, FALSE, &err), ==, GT_PROJECT_FAILED);
	g_assert_nonnull(err);
	g_clear_error(&err);

	g_assert_cmpint(gt_project_save(f.p, f.path, FALSE, &err), ==, GT_PROJECT_SAVED);
	g_assert_no_error(err);
	g_assert_cmpstr(gt_project_get_path(f.p), ==, f.path);
	g_assert_true(gt_project_data_load(f.path, &d, NULL, &err));
	g_assert_cmpuint(d->tabs->len, ==, 2);
	gt_project_data_free(d);

	/* Tiers : réécriture du fichier */
	g_assert_true(g_file_set_contents(f.path, "[gtkterm-project]\nversion=1\n# tiers\n", -1, NULL));
	g_assert_cmpint(gt_project_save(f.p, NULL, FALSE, &err), ==, GT_PROJECT_CONFLICT);
	g_assert_no_error(err);
	g_assert_true(g_file_get_contents(f.path, &contenu, NULL, NULL));
	g_assert_nonnull(strstr(contenu, "# tiers"));
	g_free(contenu);
	g_assert_cmpint(gt_project_save(f.p, NULL, TRUE, &err), ==, GT_PROJECT_SAVED);
	g_assert_no_error(err);
	g_assert_true(gt_project_data_load(f.path, &d, NULL, &err));
	gt_project_data_free(d);

	/* Dossier non inscriptible */
	if(geteuid() != 0)
	{
		gchar *ro = g_build_filename(f.dir, "ro", NULL);
		gchar *dans = g_build_filename(ro, "x.gtkterm", NULL);

		g_assert_cmpint(g_mkdir(ro, 0500), ==, 0);
		g_assert_cmpint(gt_project_save(f.p, dans, FALSE, &err), ==, GT_PROJECT_FAILED);
		g_assert_nonnull(err);
		g_clear_error(&err);
		g_assert_cmpstr(gt_project_get_path(f.p), ==, f.path);
		g_chmod(ro, 0700);
		g_rmdir(ro);
		g_free(ro);
		g_free(dans);
	}
	fix_clear(&f);
}

static void
test_save_ferme(void)
{
	Fix f;
	GError *err = NULL;

	fix_init(&f);
	g_assert_cmpint(gt_project_save(f.p, f.path, FALSE, &err), ==, GT_PROJECT_FAILED);
	g_assert_nonnull(err);
	g_clear_error(&err);
	g_assert_false(g_file_test(f.path, G_FILE_TEST_EXISTS));
	g_assert_false(gt_project_is_open(f.p));
	g_assert_null(gt_project_get_path(f.p));
	g_assert_false(gt_session_get_in_project(f.a));
	fix_clear(&f);
}

static void
test_identite_inconnue(void)
{
	Fix f;
	GError *err = NULL;

	fix_init(&f);
	g_assert_true(g_file_set_contents(f.path, "[gtkterm-project]\nversion=1\n", -1, NULL));
	gt_project_begin(f.p, f.path, NULL, NULL);
	g_assert_cmpint(gt_project_save(f.p, NULL, FALSE, &err), ==, GT_PROJECT_SAVED);
	g_assert_no_error(err);
	/* Identité désormais connue : une réécriture tierce est un conflit */
	g_assert_true(g_file_set_contents(f.path, "[gtkterm-project]\nversion=1\n# tiers\n", -1, NULL));
	g_assert_cmpint(gt_project_save(f.p, NULL, FALSE, &err), ==, GT_PROJECT_CONFLICT);
	fix_clear(&f);
}

static void
test_begin_avec_donnees(void)
{
	Fix f;
	GError *err = NULL;
	GtProjectData *saved, *lu = NULL;
	GtFileStamp stamp;
	GtProjectTab *t;

	fix_init(&f);
	saved = gt_project_capture(f.p);
	t = g_ptr_array_index(saved->tabs, 0);
	t->profile.baudrate = 1200;
	gt_project_begin(f.p, f.path, NULL, saved);
	g_assert_true(gt_project_is_modified(f.p));
	g_object_unref(f.p);

	f.p = gt_project_new(f.mgr);
	gt_project_set_page_func(f.p, page_func, &f);
	gt_project_data_free(saved);
	saved = gt_project_capture(f.p);
	g_assert_true(gt_project_data_save(saved, f.path, &stamp, &err));
	g_assert_true(gt_project_data_load(f.path, &lu, &stamp, &err));
	gt_project_begin(f.p, f.path, &stamp, lu);
	g_assert_false(gt_project_is_modified(f.p));
	/* L'identité fournie est retenue : pas de conflit */
	g_assert_cmpint(gt_project_save(f.p, NULL, FALSE, &err), ==, GT_PROJECT_SAVED);
	gt_project_data_free(saved);
	gt_project_data_free(lu);
	fix_clear(&f);
}

static void
test_end(void)
{
	Fix f;
	guint n_open = 0, n_path = 0, n_mod = 0;

	fix_init(&f);
	g_signal_connect(f.p, "notify::open", G_CALLBACK(compter), &n_open);
	g_signal_connect(f.p, "notify::path", G_CALLBACK(compter), &n_path);
	g_signal_connect(f.p, "notify::modified", G_CALLBACK(compter), &n_mod);
	gt_project_begin(f.p, f.path, NULL, NULL);
	g_assert_cmpuint(n_open, ==, 1);
	g_assert_cmpuint(n_path, ==, 1);
	g_assert_cmpuint(n_mod, ==, 0);
	g_object_set(gt_session_get_options(f.a), "echo", TRUE, NULL);
	g_assert_true(devient_modifie(f.p));
	g_assert_cmpuint(n_mod, ==, 1);
	gt_project_end(f.p);
	g_assert_false(gt_project_is_open(f.p));
	g_assert_null(gt_project_get_path(f.p));
	g_assert_false(gt_project_is_modified(f.p));
	g_assert_cmpuint(n_open, ==, 2);
	g_assert_cmpuint(n_path, ==, 2);
	g_assert_cmpuint(n_mod, ==, 2);
	gt_project_end(f.p);	/* déjà fermé : aucune notification */
	g_assert_cmpuint(n_open, ==, 2);
	g_assert_cmpuint(n_path, ==, 2);
	g_assert_cmpuint(n_mod, ==, 2);
	/* Fermé : un changement ne rend plus rien modifié */
	g_object_set(gt_session_get_options(f.a), "echo", FALSE, NULL);
	g_assert_true(reste_propre(f.p));
	g_assert_true(gt_session_get_in_project(f.a));	/* les sessions restent comme elles sont */
	fix_clear(&f);
}

/* Modèle de macros écrit dans le dossier du banc : une macro « M » (x%s, défaut 5) et une macro « N » */
static gchar *
ecrire_modele(Fix *f)
{
	GtMacroSet *set = gt_macro_set_new();
	gchar *chemin = g_build_filename(f->dir, "modele.ini", NULL);
	const gchar *args[] = { "5", NULL };
	GError *err = NULL;
	guint id;

	id = gt_macro_set_append(set, "M", "", "x%s", "");
	gt_macro_set_set_args(set, id, args, 1);
	gt_macro_set_append(set, "N", "", "y", "");
	g_assert_true(gt_macro_file_save(set, chemin, &err));
	g_assert_no_error(err);
	gt_macro_set_free(set);
	return chemin;
}

static gboolean
detacher_actif(GtSession *s)
{
	return g_action_group_get_action_enabled(gt_session_get_actions(s), "detach-macros-template");
}

static void
test_capture_lie(void)
{
	Fix f;
	GtProjectData *d;
	GtProjectTab *t;
	GtMacros *m;
	GtMacroSet *set;
	const GtMacroOverride *o;
	gchar *modele;
	const gchar *args[] = { "7", NULL };

	fix_init(&f);
	modele = ecrire_modele(&f);
	gt_project_begin(f.p, NULL, NULL, NULL);
	m = gt_session_get_macros(f.a);
	g_assert_true(gt_macros_link_template(m, modele));
	/* Valeur propre : défaut de « M » changé dans le jeu effectif */
	set = gt_macros_get_set(m);
	gt_macro_set_set_args(set, gt_macro_set_get(set, 0)->id, args, 1);
	gt_macros_changed(m);

	d = gt_project_capture(f.p);
	t = g_ptr_array_index(d->tabs, 0);
	g_assert_cmpstr(t->macros_template, ==, modele);
	/* La base, pas le jeu effectif */
	g_assert_cmpuint(gt_macro_set_count(t->macros), ==, 2);
	g_assert_cmpstr(gt_macro_set_get(t->macros, 0)->args[0], ==, "5");
	g_assert_cmpuint(gt_macro_overrides_count(t->overrides), ==, 1);
	o = gt_macro_overrides_get(t->overrides, 0);
	g_assert_cmpstr(o->label, ==, "M");
	g_assert_cmpuint(o->occurrence, ==, 0);
	g_assert_cmpuint(o->args->len, ==, 1);
	g_assert_cmpstr(g_ptr_array_index(o->args, 0), ==, "7");
	/* Onglet non lié : ni chemin ni valeur propre */
	t = g_ptr_array_index(d->tabs, 1);
	g_assert_null(t->macros_template);
	g_assert_cmpuint(gt_macro_overrides_count(t->overrides), ==, 0);
	gt_project_data_free(d);

	/* Détaché : copie du jeu effectif, plus de lien */
	gt_macros_detach_template(m);
	d = gt_project_capture(f.p);
	t = g_ptr_array_index(d->tabs, 0);
	g_assert_null(t->macros_template);
	g_assert_cmpuint(gt_macro_overrides_count(t->overrides), ==, 0);
	g_assert_cmpstr(gt_macro_set_get(t->macros, 0)->args[0], ==, "7");
	gt_project_data_free(d);
	g_remove(modele);
	g_free(modele);
	fix_clear(&f);
}

static void
test_lie_modifie(void)
{
	Fix f;
	GtMacros *m;
	GtMacroSet *base, *autre;
	GtMacroOverrides *ov;
	gchar *modele;

	fix_init(&f);
	modele = ecrire_modele(&f);
	m = gt_session_get_macros(f.a);
	g_assert_true(gt_macros_link_template(m, modele));
	gt_project_begin(f.p, NULL, NULL, NULL);
	g_assert_false(gt_project_is_modified(f.p));

	/* Même base relue, mêmes valeurs propres (aucune) : pas modifié */
	base = gt_macro_set_copy(gt_macros_get_template_set(m));
	ov = gt_macros_get_overrides(m);
	g_assert_cmpint(gt_macros_link_from_project(m, modele, base, ov, NULL, NULL), ==, GT_MACROS_TEMPLATE_UNCHANGED);
	g_assert_true(reste_propre(f.p));
	/* Base du projet différente du modèle relu (modèle mis à jour) : la base n'est pas dans l'instantané */
	autre = gt_macro_set_new();
	gt_macro_set_append(autre, "Z", "", "z", "");
	g_assert_cmpint(gt_macros_link_from_project(m, modele, autre, ov, NULL, NULL), ==, GT_MACROS_TEMPLATE_UPDATED);
	g_assert_true(reste_propre(f.p));
	gt_macro_set_free(autre);
	gt_macro_set_free(base);
	gt_macro_overrides_free(ov);

	/* Détacher : le lien disparaît de l'instantané */
	gt_macros_detach_template(m);
	g_assert_true(devient_modifie(f.p));
	g_remove(modele);
	g_free(modele);
	fix_clear(&f);
}

static void
test_action_detacher(void)
{
	Fix f;
	GtMacros *m;
	gchar *modele;

	fix_init(&f);
	modele = ecrire_modele(&f);
	m = gt_session_get_macros(f.a);
	gt_project_begin(f.p, NULL, NULL, NULL);
	g_assert_true(g_action_group_has_action(gt_session_get_actions(f.a), "detach-macros-template"));
	g_assert_false(detacher_actif(f.a));

	g_assert_true(gt_macros_link_template(m, modele));
	g_assert_true(detacher_actif(f.a));
	g_assert_false(detacher_actif(f.b));

	/* Activation : détache */
	g_action_group_activate_action(gt_session_get_actions(f.a), "detach-macros-template", NULL);
	g_assert_null(gt_macros_get_template(m));
	g_assert_false(detacher_actif(f.a));

	/* Onglet rendu libre (fermeture du projet) : détache aussi */
	g_assert_true(gt_macros_link_template(m, modele));
	g_assert_true(detacher_actif(f.a));
	gt_project_end(f.p);
	gt_session_set_in_project(f.a, FALSE);
	g_assert_null(gt_macros_get_template(m));
	g_assert_false(detacher_actif(f.a));

	/* Arrêt : désactivée, reste désactivée même si le lien change ensuite */
	g_assert_true(gt_macros_link_template(m, modele));
	gt_session_shutdown(f.a);
	g_assert_false(detacher_actif(f.a));
	g_remove(modele);
	g_free(modele);
	fix_clear(&f);
}

static void
test_nom(void)
{
	Fix f;
	GError *err = NULL;
	guint n = 0;
	gchar *nom;

	fix_init(&f);
	gt_project_begin(f.p, f.path, NULL, NULL);
	g_assert_cmpint(gt_project_save(f.p, NULL, FALSE, &err), ==, GT_PROJECT_SAVED);
	nom = gt_project_get_name(f.p);
	g_assert_cmpstr(nom, ==, "banc");
	g_free(nom);
	g_signal_connect(f.p, "notify::name", G_CALLBACK(compter), &n);
	gt_project_set_name(f.p, " Banc ");
	g_assert_cmpstr(gt_project_get_stored_name(f.p), ==, "Banc");
	nom = gt_project_get_name(f.p);
	g_assert_cmpstr(nom, ==, "Banc");
	g_free(nom);
	g_assert_cmpuint(n, ==, 1);
	gt_project_check_now(f.p);
	g_assert_true(gt_project_is_modified(f.p));
	gt_project_set_name(f.p, "");
	g_assert_null(gt_project_get_stored_name(f.p));
	nom = gt_project_get_name(f.p);
	g_assert_cmpstr(nom, ==, "banc");
	g_free(nom);
	gt_project_set_name(f.p, "X");
	gt_project_end(f.p);
	g_assert_null(gt_project_get_stored_name(f.p));
	fix_clear(&f);
}

static void
test_deplacer(void)
{
	Fix f;
	GError *err = NULL;
	gchar *neuf;

	fix_init(&f);
	gt_project_begin(f.p, f.path, NULL, NULL);
	g_assert_cmpint(gt_project_save(f.p, NULL, FALSE, &err), ==, GT_PROJECT_SAVED);
	neuf = g_build_filename(f.dir, "neuf.gtkterm", NULL);
	g_assert_true(gt_project_move_file(f.p, f.path, &err));	/* même chemin : rien */
	g_assert_true(gt_project_move_file(f.p, neuf, &err));
	g_assert_no_error(err);
	g_assert_false(g_file_test(f.path, G_FILE_TEST_EXISTS));
	g_assert_true(g_file_test(neuf, G_FILE_TEST_EXISTS));
	g_assert_cmpstr(gt_project_get_path(f.p), ==, neuf);
	g_assert_cmpint(gt_project_save(f.p, NULL, FALSE, &err), ==, GT_PROJECT_SAVED);
	g_assert_cmpint(g_rename(neuf, f.path), ==, 0);
	g_free(neuf);
	fix_clear(&f);
}

static void
test_deplacer_refus(void)
{
	Fix f;
	GError *err = NULL;
	gchar *autre, *avant = NULL;

	fix_init(&f);
	gt_project_begin(f.p, f.path, NULL, NULL);
	g_assert_cmpint(gt_project_save(f.p, NULL, FALSE, &err), ==, GT_PROJECT_SAVED);
	autre = g_build_filename(f.dir, "autre.gtkterm", NULL);
	g_assert_true(g_file_set_contents(autre, "x", -1, NULL));
	g_assert_false(gt_project_move_file(f.p, autre, &err));
	g_assert_error(err, G_FILE_ERROR, G_FILE_ERROR_EXIST);
	g_clear_error(&err);
	g_assert_cmpstr(gt_project_get_path(f.p), ==, f.path);
	g_assert_true(g_file_test(f.path, G_FILE_TEST_EXISTS));
	g_assert_true(g_file_get_contents(autre, &avant, NULL, NULL));
	g_assert_cmpstr(avant, ==, "x");
	g_free(avant);
	g_remove(autre);
	g_free(autre);
	fix_clear(&f);
}

/* Revue 1 : un ancien fichier aux onglets de macros entrelacés (T1, "", T1) est regroupé à la lecture ; ouvert, il n'est pas « modifié » */
static void
test_ancien_melange_non_modifie(void)
{
	Fix f;
	GError *err = NULL;
	GtProjectData *d = NULL;
	GtFileStamp stamp;
	GtProjectTab *t;
	const gchar *texte = "[gtkterm-project]\nversion=1\nactive=0\n[tab:0]\ntransport=serial\n"
		"[tab:0:macro:1]\nlabel=A\naction=a\ntab=T1\n[tab:0:macro:2]\nlabel=B\naction=b\ntab=\n[tab:0:macro:3]\nlabel=C\naction=c\ntab=T1\n";

	fix_init(&f);
	gt_session_manager_remove(f.mgr, f.b);
	g_assert_true(g_file_set_contents(f.path, texte, -1, NULL));
	g_assert_true(gt_project_data_load(f.path, &d, &stamp, &err));
	t = g_ptr_array_index(d->tabs, 0);
	gt_macros_replace_set(gt_session_get_macros(f.a), gt_macro_set_copy(t->macros));
	g_free(t->profile_name);
	t->profile_name = g_strdup(gt_session_get_profile_name(f.a));	/* le fichier de la fixture n'en a pas : on aligne celui de la session */
	gt_project_begin(f.p, f.path, &stamp, d);
	g_assert_false(gt_project_is_modified(f.p));
	gt_project_data_free(d);
	fix_clear(&f);
}

int
main(int argc, char *argv[])
{
	gchar *racine = test_private_dirs();
	gint ret;

	g_test_init(&argc, &argv, NULL);
	script_engine_setup();
	g_test_add_func("/project/ferme", test_ferme);
	g_test_add_func("/project/capture", test_capture);
	g_test_add_func("/project/begin-non-modifie", test_begin_non_modifie);
	g_test_add_func("/project/modifie", test_modifie);
	g_test_add_func("/project/enregistrer", test_enregistrer);
	g_test_add_func("/project/save-ferme", test_save_ferme);
	g_test_add_func("/project/identite-inconnue", test_identite_inconnue);
	g_test_add_func("/project/begin-avec-donnees", test_begin_avec_donnees);
	g_test_add_func("/project/end", test_end);
	g_test_add_func("/project/capture-lie", test_capture_lie);
	g_test_add_func("/project/lie-modifie", test_lie_modifie);
	g_test_add_func("/project/action-detacher", test_action_detacher);
	g_test_add_func("/project/nom", test_nom);
	g_test_add_func("/project/deplacer", test_deplacer);
	g_test_add_func("/project/deplacer-refus", test_deplacer_refus);
	g_test_add_func("/project/ancien-melange-non-modifie", test_ancien_melange_non_modifie);
	ret = g_test_run();
	test_rmdir_recursive(racine);
	g_free(racine);
	return ret;
}
