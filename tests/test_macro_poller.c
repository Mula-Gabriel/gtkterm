/* Tests de macro_poller.c : une minuterie GLib par macro en cours, envoi immédiat puis périodique, rappels d'état. */

#include <glib.h>

#include "macro_poller.h"

typedef struct
{
	GArray        *envois;		/* guint : identifiants envoyés, dans l'ordre */
	GArray        *etats;		/* guint : id * 2 + (running ? 1 : 0) */
	GtMacroPoller *poller;
	guint          arreter_au;	/* > 0 : la fonction d'envoi arrête la macro au n-ième envoi */
} Suivi;

static gboolean
envoyer(guint id, gpointer data)
{
	Suivi *s = data;

	g_array_append_val(s->envois, id);
	if(s->arreter_au != 0 && s->envois->len == s->arreter_au)
		gt_macro_poller_stop(s->poller, id);
	return TRUE;
}

static void
etat(guint id, gboolean running, gpointer data)
{
	Suivi *s = data;
	guint v = id * 2 + (running ? 1 : 0);

	g_array_append_val(s->etats, v);
}

static gboolean
quitter(gpointer data)
{
	g_main_loop_quit(data);
	return G_SOURCE_REMOVE;
}

static void
attendre(guint ms)
{
	GMainLoop *boucle = g_main_loop_new(NULL, FALSE);

	g_timeout_add(ms, quitter, boucle);
	g_main_loop_run(boucle);
	g_main_loop_unref(boucle);
}

static guint
compter(const Suivi *s, guint id)
{
	guint n = 0;

	for(guint i = 0; i < s->envois->len; i++)
		if(g_array_index(s->envois, guint, i) == id)
			n++;
	return n;
}

static guint
dernier_etat(const Suivi *s)
{
	g_assert_cmpuint(s->etats->len, >, 0);
	return g_array_index(s->etats, guint, s->etats->len - 1);
}

static void
suivi_init(Suivi *s, GtMacroSet *set)
{
	s->envois = g_array_new(FALSE, FALSE, sizeof(guint));
	s->etats = g_array_new(FALSE, FALSE, sizeof(guint));
	s->arreter_au = 0;
	s->poller = gt_macro_poller_new(set, envoyer, etat, s);
}

static void
suivi_fin(Suivi *s)
{
	gt_macro_poller_free(s->poller);
	g_array_unref(s->envois);
	g_array_unref(s->etats);
}

static guint
macro_en_polling(GtMacroSet *set, const gchar *label, const gchar *action, guint period)
{
	guint id = gt_macro_set_append(set, label, "", action, "");

	gt_macro_set_set_polling(set, id, TRUE, period);
	return id;
}

static void
test_envoi_immediat_puis_periodique(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint id = macro_en_polling(set, "A", "a", 30);
	Suivi s;
	guint n;

	suivi_init(&s, set);
	g_assert_true(gt_macro_poller_start(s.poller, id));
	g_assert_cmpuint(compter(&s, id), ==, 1);
	g_assert_true(gt_macro_poller_is_running(s.poller, id));
	g_assert_cmpuint(gt_macro_poller_running_count(s.poller), ==, 1);
	g_assert_cmpuint(dernier_etat(&s), ==, id * 2 + 1);
	attendre(110);
	n = compter(&s, id);
	g_assert_cmpuint(n, >=, 3);
	g_assert_cmpuint(n, <=, 6);
	gt_macro_poller_stop(s.poller, id);
	g_assert_false(gt_macro_poller_is_running(s.poller, id));
	g_assert_cmpuint(dernier_etat(&s), ==, id * 2);
	attendre(80);
	g_assert_cmpuint(compter(&s, id), ==, n);
	g_assert_cmpuint(gt_macro_poller_running_count(s.poller), ==, 0);
	suivi_fin(&s);
	gt_macro_set_free(set);
}

static void
test_demarrage_refuse(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint sans_mode = gt_macro_set_append(set, "S", "", "s", "");
	guint vide = macro_en_polling(set, "V", "", 30);
	guint sep = macro_en_polling(set, "P", "%&", 30);
	Suivi s;

	suivi_init(&s, set);
	g_assert_false(gt_macro_poller_start(s.poller, 9999999));
	g_assert_false(gt_macro_poller_start(s.poller, sans_mode));
	g_assert_false(gt_macro_poller_start(s.poller, vide));
	g_assert_false(gt_macro_poller_start(s.poller, sep));
	g_assert_cmpuint(s.envois->len, ==, 0);
	g_assert_cmpuint(s.etats->len, ==, 0);
	gt_macro_poller_stop(s.poller, sans_mode);		/* arrêter une macro qui ne tourne pas : sans effet */
	g_assert_cmpuint(s.etats->len, ==, 0);
	suivi_fin(&s);
	gt_macro_set_free(set);
}

static void
test_deja_en_cours_et_bascule(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint id = macro_en_polling(set, "A", "a", 1000);
	Suivi s;

	suivi_init(&s, set);
	g_assert_true(gt_macro_poller_start(s.poller, id));
	g_assert_true(gt_macro_poller_start(s.poller, id));
	g_assert_cmpuint(compter(&s, id), ==, 1);
	g_assert_cmpuint(s.etats->len, ==, 1);
	g_assert_false(gt_macro_poller_toggle(s.poller, id));
	g_assert_false(gt_macro_poller_is_running(s.poller, id));
	g_assert_true(gt_macro_poller_toggle(s.poller, id));
	g_assert_cmpuint(compter(&s, id), ==, 2);
	g_assert_cmpuint(s.etats->len, ==, 3);
	suivi_fin(&s);
	gt_macro_set_free(set);
}

static void
test_reset(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint a = macro_en_polling(set, "A", "a", 1000), b = macro_en_polling(set, "B", "b", 1000);
	Suivi s;

	suivi_init(&s, set);
	gt_macro_poller_start(s.poller, a);
	gt_macro_poller_start(s.poller, b);
	gt_macro_poller_reset(s.poller);
	g_assert_cmpuint(gt_macro_poller_running_count(s.poller), ==, 0);
	g_assert_cmpuint(s.etats->len, ==, 4);
	suivi_fin(&s);
	gt_macro_set_free(set);
}

static void
test_sync_suppression_et_mode(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint a = macro_en_polling(set, "A", "a", 1000), b = macro_en_polling(set, "B", "b", 1000), c = macro_en_polling(set, "C", "c", 1000);
	GtMacroEdit rows[] = { { b, "B", "", "b", "" }, { c, "C", "", "c", "" } };
	Suivi s;

	suivi_init(&s, set);
	gt_macro_poller_start(s.poller, a);
	gt_macro_poller_start(s.poller, b);
	gt_macro_poller_start(s.poller, c);
	gt_macro_set_apply_edits(set, rows, G_N_ELEMENTS(rows), NULL);
	gt_macro_set_set_polling(set, c, FALSE, 1000);
	gt_macro_poller_sync(s.poller);
	g_assert_false(gt_macro_poller_is_running(s.poller, a));
	g_assert_true(gt_macro_poller_is_running(s.poller, b));
	g_assert_false(gt_macro_poller_is_running(s.poller, c));
	g_assert_cmpuint(gt_macro_poller_running_count(s.poller), ==, 1);
	suivi_fin(&s);
	gt_macro_set_free(set);
}

static void
test_sync_action_videe(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint a = macro_en_polling(set, "A", "a", 1000);
	GtMacroEdit rows[] = { { a, "A", "", "", "" } };
	Suivi s;

	suivi_init(&s, set);
	gt_macro_poller_start(s.poller, a);
	gt_macro_set_apply_edits(set, rows, 1, NULL);
	gt_macro_poller_sync(s.poller);
	g_assert_false(gt_macro_poller_is_running(s.poller, a));
	suivi_fin(&s);
	gt_macro_set_free(set);
}

static void
test_changement_de_periode(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint a = macro_en_polling(set, "A", "a", 1000), b = macro_en_polling(set, "B", "b", 1000);
	Suivi s;

	suivi_init(&s, set);
	gt_macro_poller_start(s.poller, a);
	gt_macro_poller_start(s.poller, b);
	gt_macro_set_set_polling(set, a, TRUE, 30);
	gt_macro_set_set_polling(set, b, TRUE, 30);
	gt_macro_poller_sync(s.poller);
	gt_macro_poller_period_changed(s.poller, b);		/* déjà relancée par sync : sans effet supplémentaire */
	/* Pas d'envoi immédiat au changement de période */
	g_assert_cmpuint(compter(&s, a), ==, 1);
	g_assert_cmpuint(compter(&s, b), ==, 1);
	attendre(110);
	g_assert_cmpuint(compter(&s, a), >=, 3);
	g_assert_cmpuint(compter(&s, b), >=, 3);
	gt_macro_set_set_polling(set, a, TRUE, 1000);
	gt_macro_poller_period_changed(s.poller, a);
	{
		guint n = compter(&s, a);

		attendre(80);
		g_assert_cmpuint(compter(&s, a), ==, n);
	}
	suivi_fin(&s);
	gt_macro_set_free(set);
}

static void
test_periode_maximale(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint a = macro_en_polling(set, "A", "a", GT_MACRO_PERIOD_MAX_MS);
	Suivi s;

	suivi_init(&s, set);
	g_assert_true(gt_macro_poller_start(s.poller, a));
	attendre(50);
	g_assert_cmpuint(compter(&s, a), ==, 1);
	g_assert_true(gt_macro_poller_is_running(s.poller, a));
	suivi_fin(&s);
	gt_macro_set_free(set);
}

static void
test_arret_depuis_envoi(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint a = macro_en_polling(set, "A", "a", 20);
	Suivi s;

	suivi_init(&s, set);
	s.arreter_au = 2;
	gt_macro_poller_start(s.poller, a);
	attendre(120);
	g_assert_cmpuint(compter(&s, a), ==, 2);
	g_assert_false(gt_macro_poller_is_running(s.poller, a));
	g_assert_cmpuint(s.etats->len, ==, 2);
	suivi_fin(&s);
	gt_macro_set_free(set);
}

static void
test_arret_pendant_envoi_immediat(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint a = macro_en_polling(set, "A", "a", 20);
	Suivi s;

	suivi_init(&s, set);
	s.arreter_au = 1;
	g_assert_false(gt_macro_poller_start(s.poller, a));
	attendre(60);
	g_assert_cmpuint(compter(&s, a), ==, 1);
	g_assert_false(gt_macro_poller_is_running(s.poller, a));
	suivi_fin(&s);
	gt_macro_set_free(set);
}

static void
test_free_sans_rappel(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint a = macro_en_polling(set, "A", "a", 20);
	Suivi s;

	suivi_init(&s, set);
	gt_macro_poller_start(s.poller, a);
	gt_macro_poller_free(s.poller);
	s.poller = NULL;
	g_assert_cmpuint(s.etats->len, ==, 1);
	attendre(60);
	g_assert_cmpuint(compter(&s, a), ==, 1);
	g_array_unref(s.envois);
	g_array_unref(s.etats);
	gt_macro_set_free(set);
	gt_macro_poller_free(NULL);
}

static void
test_nouveau_jeu(void)
{
	GtMacroSet *set = gt_macro_set_new(), *autre = gt_macro_set_new();
	guint a = macro_en_polling(set, "A", "a", 1000);
	Suivi s;

	suivi_init(&s, set);
	gt_macro_poller_start(s.poller, a);
	gt_macro_poller_set_set(s.poller, autre);
	g_assert_cmpuint(gt_macro_poller_running_count(s.poller), ==, 0);
	g_assert_cmpuint(dernier_etat(&s), ==, a * 2);
	g_assert_false(gt_macro_poller_start(s.poller, a));
	suivi_fin(&s);
	gt_macro_set_free(set);
	gt_macro_set_free(autre);
}

/* Après macros_shutdown, l'ordonnanceur est NULL : toutes les fonctions qui le modifient (ou le lisent) le tolèrent */
static void
test_ordonnanceur_nul(void)
{
	GtMacroSet *set = gt_macro_set_new();

	g_assert_false(gt_macro_poller_start(NULL, 1));
	g_assert_false(gt_macro_poller_toggle(NULL, 1));
	gt_macro_poller_stop(NULL, 1);
	gt_macro_poller_reset(NULL);
	gt_macro_poller_sync(NULL);
	gt_macro_poller_period_changed(NULL, 1);
	gt_macro_poller_set_set(NULL, set);
	g_assert_false(gt_macro_poller_is_running(NULL, 1));
	g_assert_cmpuint(gt_macro_poller_running_count(NULL), ==, 0);
	gt_macro_poller_free(NULL);
	gt_macro_set_free(set);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/macro_poller/envoi-immediat-puis-periodique", test_envoi_immediat_puis_periodique);
	g_test_add_func("/macro_poller/demarrage-refuse", test_demarrage_refuse);
	g_test_add_func("/macro_poller/deja-en-cours-et-bascule", test_deja_en_cours_et_bascule);
	g_test_add_func("/macro_poller/reset", test_reset);
	g_test_add_func("/macro_poller/sync-suppression-et-mode", test_sync_suppression_et_mode);
	g_test_add_func("/macro_poller/sync-action-videe", test_sync_action_videe);
	g_test_add_func("/macro_poller/changement-de-periode", test_changement_de_periode);
	g_test_add_func("/macro_poller/periode-maximale", test_periode_maximale);
	g_test_add_func("/macro_poller/arret-depuis-envoi", test_arret_depuis_envoi);
	g_test_add_func("/macro_poller/arret-pendant-envoi-immediat", test_arret_pendant_envoi_immediat);
	g_test_add_func("/macro_poller/free-sans-rappel", test_free_sans_rappel);
	g_test_add_func("/macro_poller/nouveau-jeu", test_nouveau_jeu);
	g_test_add_func("/macro_poller/ordonnanceur-nul", test_ordonnanceur_nul);
	return g_test_run();
}
