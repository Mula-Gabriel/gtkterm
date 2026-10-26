/* Tests d'intégration de macros.c (couche application, sans GTK) : envoi unique par identifiant, fichier, ordonnanceur ; effets d'interface relevés par la
 * table GtAppUi du test (raccourcis et fenêtre d'édition : suite graphique, test_raccourcis_macros.py et test_macros_edition.py). */

#include <glib.h>
#include <errno.h>
#include <string.h>

#include "app_ui.h"
#include "macros.h"
#include "stubs.h"
#include "test_util.h"

/* Crochets GtAppUi : chaque appel est noté dans le journal des bouchons (stub_count, stub_last) */
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

static GtMacros *macros;
static guint id_vitesse, id_sans_libelle, id_pourcent, id_separateur;

static void
installer_macros(void)
{
	GtMacroSet *set = gt_macro_set_new();

	id_vitesse = gt_macro_set_append(set, "Vitesse", "<Control>F5", "V%d\\r", "");
	g_assert_true(gt_macro_set_set_arg(set, id_vitesse, 0, "42"));
	id_sans_libelle = gt_macro_set_append(set, NULL, "<Control>F6", "SANS_LIBELLE", "");	/* C8 */
	id_pourcent = gt_macro_set_append(set, "Pourcent", "None", "100%%", "");
	id_separateur = gt_macro_set_append(set, "Sep", "<Control>F7", "%&", "");
	gt_macros_replace_set(macros, set);
}

static void
desinstaller(void)
{
	gt_macros_replace_set(macros, gt_macro_set_new());
}

static gchar *
envoye(void)
{
	GByteArray *octets = stub_sent();

	return g_strndup((const gchar *)octets->data, octets->len);
}

static void
test_arguments_enregistres(void)
{
	gchar *s;

	installer_macros();
	stub_reset();
	g_assert_true(gt_macros_send(macros, id_vitesse, NULL, -1));
	s = envoye();
	g_assert_cmpstr(s, ==, "V42\r");
	g_assert_cmpint(stub_last_origin(), ==, GT_SEND_MACRO);
	g_free(s);
	desinstaller();
}

static void
test_arguments_fournis(void)
{
	const gchar *sept[] = { "7" };
	gchar *s;

	installer_macros();
	stub_reset();
	g_assert_true(gt_macros_send(macros, id_vitesse, sept, 1));
	s = envoye();
	g_assert_cmpstr(s, ==, "V7\r");
	g_free(s);
	desinstaller();
}

static void
test_double_pourcent(void)
{
	gchar *s;

	installer_macros();
	stub_reset();
	gt_macros_send(macros, id_pourcent, NULL, -1);
	s = envoye();
	g_assert_cmpstr(s, ==, "100%");
	g_free(s);
	desinstaller();
}

static void
test_sans_libelle(void)
{
	gchar *s;

	installer_macros();
	stub_reset();
	gt_macros_send(macros, id_sans_libelle, NULL, -1);
	s = envoye();
	g_assert_cmpstr(s, ==, "SANS_LIBELLE");
	g_assert_cmpint(stub_count("status_flash"), ==, 1);
	g_assert_nonnull(strstr(stub_last("status_flash"), "<Control>F6"));
	g_free(s);
	desinstaller();
}

static void
test_inconnue_ou_separateur(void)
{
	installer_macros();
	stub_reset();
	g_assert_false(gt_macros_send(macros, 9999999, NULL, -1));
	g_assert_false(gt_macros_send(macros, id_separateur, NULL, -1));
	g_assert_cmpuint(stub_sent()->len, ==, 0);
	g_assert_cmpint(stub_count("status_flash"), ==, 0);
	desinstaller();
}

/* Port ferme ou perdu : rien n'est ecrit, macros_send le signale par FALSE et n'affiche aucun message */
static void
test_ecriture_en_echec(void)
{
	installer_macros();
	stub_reset();
	stub_link_set_write_fails(TRUE);
	g_assert_false(gt_macros_send(macros, id_vitesse, NULL, -1));
	g_assert_cmpuint(stub_sent()->len, ==, 0);
	g_assert_cmpint(stub_count("gt_link_send"), ==, 1);
	/* M3 : aucun octet ecrit, pas de message « envoyee » (le « - lost » de la barre d'etat reste visible) */
	g_assert_cmpint(stub_count("status_flash"), ==, 0);
	stub_link_set_write_fails(FALSE);
	desinstaller();
}

/* File d'émission pleine (gt_link_send : -1 / ENOBUFS) : pas de « sent! », l'échec est signalé, au plus une fois toutes les 3 s par macro ;
 * gt_macros_send_quiet (Lua) ne dit rien */
static void
test_file_pleine(void)
{
	installer_macros();
	stub_reset();
	stub_link_set_refuse(ENOBUFS);
	g_assert_false(gt_macros_send(macros, id_vitesse, NULL, -1));
	g_assert_cmpuint(stub_sent()->len, ==, 0);
	g_assert_cmpint(stub_count("status_flash"), ==, 1);
	g_assert_null(strstr(stub_last("status_flash"), "sent!"));
	g_assert_nonnull(strstr(stub_last("status_flash"), "Vitesse"));
	g_assert_nonnull(strstr(stub_last("status_flash"), "send queue full"));
	g_assert_false(gt_macros_send(macros, id_vitesse, NULL, -1));
	g_assert_cmpint(stub_count("status_flash"), ==, 1);
	g_assert_false(gt_macros_send_quiet(macros, id_sans_libelle, NULL, -1));
	g_assert_cmpint(stub_count("status_flash"), ==, 1);
	stub_link_set_refuse(0);
	g_assert_true(gt_macros_send(macros, id_vitesse, NULL, -1));
	g_assert_cmpint(stub_count("status_flash"), ==, 2);
	g_assert_nonnull(strstr(stub_last("status_flash"), "sent!"));
	desinstaller();
}

/* Scrutation : un tir est sauté tant que la file d'émission du port n'est pas vide (pas de retard accumulé) */
static void
test_polling_saute_si_file(void)
{
	GMainContext *ctx = g_main_context_default();
	gint64 fin;

	installer_macros();
	stub_reset();
	stub_link_set_queued(100);
	g_assert_true(gt_macro_set_set_polling(gt_macros_get_set(macros), id_vitesse, TRUE, 5));
	g_assert_true(gt_macro_poller_start(gt_macros_get_poller(macros), id_vitesse));
	fin = g_get_monotonic_time() + 60 * G_TIME_SPAN_MILLISECOND;
	while(g_get_monotonic_time() < fin)
		g_main_context_iteration(ctx, FALSE);
	g_assert_cmpuint(stub_sent()->len, ==, 0);
	g_assert_cmpint(stub_count("gt_link_send"), ==, 0);
	g_assert_true(gt_macro_poller_is_running(gt_macros_get_poller(macros), id_vitesse));
	/* Envoi direct (clic, raccourci) : jamais sauté, il passe derrière la file */
	g_assert_true(gt_macros_send(macros, id_vitesse, NULL, -1));
	g_assert_cmpuint(stub_sent()->len, ==, 4);
	stub_link_set_queued(0);
	fin = g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;
	while(stub_sent()->len < 8 && g_get_monotonic_time() < fin)
		g_main_context_iteration(ctx, FALSE);
	g_assert_cmpuint(stub_sent()->len, >=, 8);
	desinstaller();
}

/* Spec Lua §1 : macros_send annonce l'envoi (status_flash), macros_send_quiet (moteur Lua) n'annonce rien ; même envoi */
static void
test_envoi_silencieux(void)
{
	gchar *s;

	installer_macros();
	stub_reset();
	g_assert_true(gt_macros_send_quiet(macros, id_vitesse, NULL, -1));
	s = envoye();
	g_assert_cmpstr(s, ==, "V42\r");
	g_free(s);
	g_assert_cmpint(stub_last_origin(), ==, GT_SEND_MACRO);
	g_assert_cmpint(stub_count("status_flash"), ==, 0);
	g_assert_false(gt_macros_send_quiet(macros, id_separateur, NULL, -1));
	g_assert_false(gt_macros_send_quiet(macros, 9999999, NULL, -1));
	g_assert_true(gt_macros_send(macros, id_vitesse, NULL, -1));
	g_assert_cmpint(stub_count("status_flash"), ==, 1);
	g_assert_nonnull(strstr(stub_last("status_flash"), "Vitesse"));
	desinstaller();
}

/* F1 : %#Nom n'est une liste que si elle existe ; ancien format lu avec ses listes avant ses macros. */
static void
test_chargement_ancien_format_listes(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macros-XXXXXX", NULL);
	gchar *path, *s;
	GtMacroSet *set;
	const gchar *contenu =
		"[macros]\n"
		"macro_0=SPD::<Control>F5::SPD %#Speed\\\\r::::115200::0::1000\n"
		"macro_1=M::None::M %#Speed %d\\\\r::::9600|5::0::1000\n"
		"[lists]\n"
		"list_0=Speed:9600=9600|115200=115200\n";

	path = test_write_file(dir, "macros.ini", contenu);
	stub_reset();
	g_assert_true(gt_macros_load_file(macros, path, FALSE));
	set = gt_macros_get_set(macros);
	g_assert_cmpuint(gt_macro_set_count(set), ==, 2);
	g_assert_cmpstr(gt_macro_set_get(set, 0)->args[0], ==, "115200");
	g_assert_cmpstr(gt_macro_set_get(set, 1)->args[0], ==, "9600");
	g_assert_cmpstr(gt_macro_set_get(set, 1)->args[1], ==, "5");
	g_assert_cmpstr(gt_macros_get_path(macros), ==, path);
	g_assert_cmpint(stub_count("macros_changed"), >=, 1);
	gt_macros_send(macros, gt_macro_set_get(set, 0)->id, NULL, -1);
	s = envoye();
	g_assert_cmpstr(s, ==, "SPD 115200\r");
	g_free(s);
	desinstaller();
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

static void
test_chargement_fichier_absent(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macros-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "absent.ini", NULL);

	installer_macros();
	stub_reset();
	g_assert_true(gt_macros_load_file(macros, path, TRUE));
	g_assert_cmpuint(gt_macro_set_count(gt_macros_get_set(macros)), ==, 0);
	g_assert_cmpstr(gt_macros_get_path(macros), ==, path);
	g_assert_cmpint(stub_count("message"), ==, 0);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

static void
test_chargement_corrompu_garde_le_jeu(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macros-XXXXXX", NULL);
	gchar *bon = g_build_filename(dir, "absent.ini", NULL);
	gchar *corrompu = test_write_file(dir, "corrompu.ini", "[[[\n");

	g_assert_true(gt_macros_load_file(macros, bon, FALSE));
	installer_macros();
	stub_reset();
	g_assert_false(gt_macros_load_file(macros, corrompu, TRUE));
	g_assert_cmpuint(gt_macro_set_count(gt_macros_get_set(macros)), ==, 4);
	g_assert_cmpint(stub_count("message"), ==, 1);
	g_assert_cmpstr(gt_macros_get_path(macros), ==, bon);
	desinstaller();
	test_rmdir(dir);
	g_free(bon);
	g_free(corrompu);
	g_free(dir);
}

static void
test_enregistrement_echec_une_seule_boite(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macros-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "absent", "m.ini", NULL);

	installer_macros();
	stub_reset();
	g_assert_cmpint(gt_macros_save_file(macros, path, TRUE, FALSE), ==, GT_MACROS_FAILED);
	g_assert_cmpint(gt_macros_save_file(macros, path, TRUE, FALSE), ==, GT_MACROS_FAILED);
	g_assert_cmpint(stub_count("message"), ==, 1);
	desinstaller();
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

/* M1 : un echec deja signale n'ouvre plus de boite, sauf apres macros_forget_save_error (enregistrement explicite du menu) */
static void
test_enregistrement_explicite_reaffiche(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macros-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "absent", "m.ini", NULL);

	installer_macros();
	stub_reset();
	g_assert_cmpint(gt_macros_save_file(macros, path, TRUE, FALSE), ==, GT_MACROS_FAILED);
	g_assert_cmpint(gt_macros_save_file(macros, path, TRUE, FALSE), ==, GT_MACROS_FAILED);
	g_assert_cmpint(stub_count("message"), ==, 1);
	gt_macros_forget_save_error(macros);
	g_assert_cmpint(gt_macros_save_file(macros, path, TRUE, FALSE), ==, GT_MACROS_FAILED);
	g_assert_cmpint(stub_count("message"), ==, 2);
	desinstaller();
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

static void
iterer_boucle(void)
{
	while(g_main_context_iteration(NULL, FALSE))
		;
}

/* I1 : chargement non interactif (profil) d'un fichier illisible : jeu vide, chemin courant = ce fichier, boite differee ; le
 * premier enregistrement vise ce fichier (jamais le fichier par defaut) et en garde une copie identique a l'original. */
static void
test_chargement_profil_abime(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macros-XXXXXX", NULL);
	const gchar *original = "[gtkterm-macros]\nversion=3\n[macro:1]\naction=x\n";
	gchar *path = test_write_file(dir, "m.ini", original);
	gchar *copie = g_strconcat(path, ".avant-migration", NULL);
	gchar *texte = NULL;

	installer_macros();
	stub_reset();
	g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*m.ini*");
	g_assert_false(gt_macros_load_file(macros, path, FALSE));
	g_test_assert_expected_messages();
	g_assert_cmpuint(gt_macro_set_count(gt_macros_get_set(macros)), ==, 0);
	g_assert_cmpstr(gt_macros_get_path(macros), ==, path);
	g_assert_cmpint(stub_count("message"), ==, 0);	/* jamais de boucle imbriquee pendant le chargement du profil */
	iterer_boucle();
	g_assert_cmpint(stub_count("message"), ==, 1);
	g_assert_nonnull(strstr(stub_last("message"), "m.ini"));
	g_assert_cmpint(gt_macros_save_file(macros, NULL, TRUE, FALSE), ==, GT_MACROS_SAVED);
	g_assert_cmpstr(gt_macros_get_path(macros), ==, path);
	g_assert_true(g_file_get_contents(copie, &texte, NULL, NULL));
	g_assert_cmpstr(texte, ==, original);
	g_free(texte);
	g_assert_true(g_file_get_contents(path, &texte, NULL, NULL));
	g_assert_nonnull(strstr(texte, "version=2"));
	g_free(texte);
	desinstaller();
	test_rmdir(dir);
	g_free(copie);
	g_free(path);
	g_free(dir);
}

static void
test_enregistrement_puis_relecture(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macros-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "m.ini", NULL);

	installer_macros();
	g_assert_cmpint(gt_macros_save_file(macros, path, FALSE, FALSE), ==, GT_MACROS_SAVED);
	g_assert_cmpstr(gt_macros_get_path(macros), ==, path);
	desinstaller();
	g_assert_true(gt_macros_load_file(macros, path, FALSE));
	g_assert_cmpuint(gt_macro_set_count(gt_macros_get_set(macros)), ==, 4);
	g_assert_cmpstr(gt_macro_set_get(gt_macros_get_set(macros), 0)->args[0], ==, "42");
	desinstaller();
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

/* Charger un autre jeu arrête tout polling ; les rappels d'état atteignent le panneau. */
static void
test_polling_arrete_au_chargement(void)
{
	installer_macros();
	stub_reset();
	g_assert_true(gt_macro_set_set_polling(gt_macros_get_set(macros), id_vitesse, TRUE, 1000));
	g_assert_true(gt_macro_poller_start(gt_macros_get_poller(macros), id_vitesse));
	g_assert_cmpuint(stub_sent()->len, ==, 4);			/* envoi immédiat « V42\r » */
	desinstaller();
	g_assert_cmpuint(gt_macro_poller_running_count(gt_macros_get_poller(macros)), ==, 0);
	g_assert_cmpint(stub_count("macro_polling_changed"), ==, 2);
}

/* Deux instances sur deux liaisons : chaque envoi et chaque polling part sur la liaison de son instance (spec §3.1-2) */
static void
test_deux_instances(void)
{
	GtkTermLink *l1 = gt_link_new(), *l2 = gt_link_new();
	GtMacros *m1 = gt_macros_new(l1, NULL), *m2 = gt_macros_new(l2, NULL);
	GtMacroSet *s1 = gt_macro_set_new(), *s2 = gt_macro_set_new();
	guint id1 = gt_macro_set_append(s1, "Un", NULL, "UN", NULL);
	guint id2 = gt_macro_set_append(s2, "Deux", NULL, "DEUX", NULL);
	GMainContext *ctx = g_main_context_default();
	gint64 fin;

	gt_macros_replace_set(m1, s1);
	gt_macros_replace_set(m2, s2);
	g_assert_true(gt_macros_send(m1, id1, NULL, -1));
	g_assert_true(gt_macros_send(m2, id2, NULL, -1));
	g_assert_cmpmem(stub_link_sent(l1)->data, stub_link_sent(l1)->len, "UN", 2);
	g_assert_cmpmem(stub_link_sent(l2)->data, stub_link_sent(l2)->len, "DEUX", 4);
	/* L'identifiant de m1 n'existe pas dans m2 : rien n'est envoyé sur l2 */
	g_assert_false(gt_macros_send(m2, id1 + 1000, NULL, -1));
	/* Polling de m1 toutes les 10 ms : seul l1 reçoit */
	g_byte_array_set_size(stub_link_sent(l1), 0);
	g_byte_array_set_size(stub_link_sent(l2), 0);
	g_assert_true(gt_macro_set_set_polling(gt_macros_get_set(m1), id1, TRUE, 10));
	gt_macro_poller_start(gt_macros_get_poller(m1), id1);
	fin = g_get_monotonic_time() + 100 * G_TIME_SPAN_MILLISECOND;
	while(g_get_monotonic_time() < fin)
		g_main_context_iteration(ctx, FALSE);
	g_assert_cmpuint(stub_link_sent(l1)->len, >=, 4);
	g_assert_cmpuint(stub_link_sent(l2)->len, ==, 0);
	/* Après shutdown : plus rien n'est envoyé */
	gt_macros_shutdown(m1);
	g_assert_false(gt_macros_send(m1, id1, NULL, -1));
	gt_macros_free(m1);
	gt_macros_free(m2);
	g_object_unref(l1);
	g_object_unref(l2);
}

static gint compteur_edited;

static void
rappel_edited(gpointer data)
{
	compteur_edited++;
}

/* Macro « N%d; » avec la valeur par défaut « 5 » */
static guint
installer_macro_n(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint id = gt_macro_set_append(set, "N", NULL, "N%d;", "");

	g_assert_true(gt_macro_set_set_arg(set, id, 0, "5"));
	gt_macros_replace_set(macros, set);
	return id;
}

static void
verifier_envoi(guint id, const gchar *attendu)
{
	gchar *s;

	stub_reset();
	g_assert_true(gt_macros_send(macros, id, NULL, -1));
	s = envoye();
	g_assert_cmpstr(s, ==, attendu);
	g_free(s);
}

static void
test_usage_prioritaire(void)
{
	guint id = installer_macro_n();

	verifier_envoi(id, "N5;");
	g_assert_false(gt_macros_has_usage(macros, id));
	gt_macros_set_usage_arg(macros, id, 0, "9");
	g_assert_true(gt_macros_has_usage(macros, id));
	verifier_envoi(id, "N9;");
	g_assert_cmpstr(gt_macro_set_find(gt_macros_get_set(macros), id)->args[0], ==, "5");
	gt_macros_reset_usage(macros, id);
	verifier_envoi(id, "N5;");
	/* Indice hors limites : rien */
	gt_macros_set_usage_arg(macros, id, 3, "x");
	g_assert_false(gt_macros_has_usage(macros, id));
	desinstaller();
}

static void
test_usage_jamais_enregistre(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macros-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "m.ini", NULL);
	gchar *texte = NULL;
	guint id = installer_macro_n();

	g_assert_cmpint(gt_macros_save_file(macros, path, FALSE, FALSE), ==, GT_MACROS_SAVED);
	g_assert_true(gt_macros_load_file(macros, path, FALSE));
	id = gt_macro_set_get(gt_macros_get_set(macros), 0)->id;
	gt_macros_set_usage_arg(macros, id, 0, "9");
	g_assert_true(gt_macros_has_usage(macros, id));
	g_assert_cmpint(gt_macros_save_file(macros, NULL, FALSE, FALSE), ==, GT_MACROS_SAVED);
	g_assert_true(g_file_get_contents(path, &texte, NULL, NULL));
	g_assert_nonnull(strstr(texte, "args=5;"));
	g_assert_null(strstr(texte, "args=9"));
	g_free(texte);
	desinstaller();
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

static void
test_usage_efface_au_remplacement(void)
{
	guint id = installer_macro_n();

	gt_macros_set_usage_arg(macros, id, 0, "9");
	g_assert_true(gt_macros_has_usage(macros, id));
	gt_macros_replace_set(macros, gt_macro_set_copy(gt_macros_get_set(macros)));
	g_assert_false(gt_macros_has_usage(macros, id));
	g_assert_false(gt_macros_has_usage(macros, gt_macro_set_get(gt_macros_get_set(macros), 0)->id));
	desinstaller();
}

static void
test_usage_nombre_change(void)
{
	guint id = installer_macro_n();
	GtMacroEdit ligne = { .id = id, .label = "N", .shortcut = "", .action = "N%d %d", .tab = "", .args = NULL };
	const gchar *const *args;

	gt_macros_set_usage_arg(macros, id, 0, "9");
	args = gt_macros_get_args(macros, id);
	g_assert_nonnull(args);
	g_assert_cmpstr(args[0], ==, "9");
	gt_macro_set_apply_edits(gt_macros_get_set(macros), &ligne, 1, NULL);
	g_assert_null(gt_macros_get_args(macros, id));
	desinstaller();
}

/* §2.7 : champ d'usage vide = valeur par défaut ; vecteur résolu champ par champ */
static void
test_champ_vide_defaut(void)
{
	guint id = installer_macro_n();
	const gchar *const *args, *const *usage;

	gt_macros_set_usage_arg(macros, id, 0, "");
	usage = gt_macros_get_usage(macros, id);
	g_assert_nonnull(usage);
	g_assert_cmpstr(usage[0], ==, "");
	args = gt_macros_get_args(macros, id);
	g_assert_nonnull(args);
	g_assert_cmpstr(args[0], ==, "5");
	verifier_envoi(id, "N5;");
	gt_macros_set_usage_arg(macros, id, 0, "8");
	verifier_envoi(id, "N8;");
	g_assert_cmpstr(gt_macros_get_args(macros, id)[0], ==, "8");
	gt_macros_reset_usage(macros, id);
	g_assert_null(gt_macros_get_usage(macros, id));
	g_assert_cmpstr(gt_macros_get_args(macros, id)[0], ==, "5");
	desinstaller();
}

/* Résolution par argument : usage non vide, sinon défaut, sinon "" */
static void
test_resolution_par_argument(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint id = gt_macro_set_append(set, "D", NULL, "D%d,%s,%d;", "");
	const gchar *const defauts[] = { "1", "", "3", NULL };
	const gchar *const *args;

	g_assert_true(gt_macro_set_set_args(set, id, defauts, 3));
	gt_macros_replace_set(macros, set);
	gt_macros_set_usage_arg(macros, id, 1, "x");
	args = gt_macros_get_args(macros, id);
	g_assert_cmpstr(args[0], ==, "1");
	g_assert_cmpstr(args[1], ==, "x");
	g_assert_cmpstr(args[2], ==, "3");
	g_assert_null(args[3]);
	verifier_envoi(id, "D1,x,3;");
	desinstaller();
}

/* Sans défaut ni valeur d'usage : NULL, l'action part avec ses spécificateurs */
static void
test_sans_defaut_ni_usage(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint id = gt_macro_set_append(set, "W", NULL, "W%d;", "");

	gt_macros_replace_set(macros, set);
	g_assert_null(gt_macros_get_args(macros, id));
	g_assert_null(gt_macros_get_usage(macros, id));
	verifier_envoi(id, "W%d;");
	/* Champ vidé : toujours aucune valeur, comme sans usage */
	gt_macros_set_usage_arg(macros, id, 0, "");
	g_assert_null(gt_macros_get_args(macros, id));
	verifier_envoi(id, "W%d;");
	gt_macros_set_usage_arg(macros, id, 0, "7");
	verifier_envoi(id, "W7;");
	desinstaller();
}

/* Vecteur d'usage de mauvaise longueur (action changée sans gt_macros_apply_edits) : ignoré en entier */
static void
test_usage_mauvaise_longueur(void)
{
	guint id = installer_macro_n();
	const gchar *const deux[] = { "1", "2", NULL };
	GtMacroEdit ligne = { .id = id, .label = "N", .shortcut = "", .action = "N%d %d;", .tab = "", .args = deux };
	const gchar *const *args;

	gt_macros_set_usage_arg(macros, id, 0, "9");
	gt_macro_set_apply_edits(gt_macros_get_set(macros), &ligne, 1, NULL);
	g_assert_true(gt_macros_has_usage(macros, id));
	g_assert_null(gt_macros_get_usage(macros, id));
	args = gt_macros_get_args(macros, id);
	g_assert_cmpstr(args[0], ==, "1");
	g_assert_cmpstr(args[1], ==, "2");
	verifier_envoi(id, "N1 2;");
	desinstaller();
}

/* §2.8 : hors limites, rien n'est écrit ; un seul message par macro en 3 s ; send_quiet sans message */
static void
test_refus_hors_limites(void)
{
	guint id = installer_macro_n();
	const gchar *const vide[] = { "", NULL }, *const dix[] = { "10", NULL };
	const gchar *const cinquante[] = { "50", NULL };
	gchar *raison = NULL;

	g_assert_cmpint(gt_macro_set_set_limits(gt_macros_get_set(macros), id, vide, dix, vide, 1), ==, 0);
	g_assert_cmpint(gt_macros_check_args(macros, id, NULL, -1, &raison), ==, -1);
	g_assert_null(raison);
	gt_macros_set_usage_arg(macros, id, 0, "50");
	g_assert_cmpint(gt_macros_check_args(macros, id, NULL, -1, &raison), ==, 0);
	g_assert_cmpstr(raison, ==, "above maximum 10");
	g_free(raison);
	g_assert_cmpint(gt_macros_check_args(macros, 9999, NULL, -1, NULL), ==, -1);
	g_assert_cmpint(gt_macros_check_args(macros, id, dix, 1, NULL), ==, -1);

	stub_reset();
	g_assert_false(gt_macros_send(macros, id, NULL, -1));
	g_assert_cmpuint(stub_sent()->len, ==, 0);
	g_assert_cmpint(stub_count("status_flash"), ==, 1);
	g_assert_cmpstr(stub_last("status_flash"), ==, "Macro \"N\" not sent: argument 1: above maximum 10");
	/* Polling rapide : pas de second message dans les 3 s */
	g_assert_false(gt_macros_send(macros, id, NULL, -1));
	g_assert_false(gt_macros_send(macros, id, cinquante, 1));
	g_assert_cmpint(stub_count("status_flash"), ==, 1);
	g_assert_false(gt_macros_send_quiet(macros, id, cinquante, 1));
	g_assert_cmpuint(stub_sent()->len, ==, 0);
	g_assert_cmpint(stub_count("status_flash"), ==, 1);
	/* Valeur d'usage changée : le délai repart de zéro pour cette macro */
	gt_macros_set_usage_arg(macros, id, 0, "60");
	g_assert_false(gt_macros_send(macros, id, NULL, -1));
	g_assert_cmpint(stub_count("status_flash"), ==, 2);
	/* Champ vidé : défaut 5, envoyé */
	gt_macros_set_usage_arg(macros, id, 0, "");
	stub_reset();
	g_assert_true(gt_macros_send(macros, id, NULL, -1));
	g_assert_cmpuint(stub_sent()->len, ==, 3);
	desinstaller();
}

/* Application des modifications de l'éditeur (OK de Configure Macros) : une macro dont les valeurs par défaut changent perd ses valeurs d'usage
 * (sinon elles masqueraient la nouvelle valeur par défaut) ; une macro dont les valeurs par défaut ne changent pas les garde. */
static void
test_editeur_efface_usage(void)
{
	guint id = installer_macro_n();
	guint id2 = gt_macro_set_append(gt_macros_get_set(macros), "M", NULL, "M%d;", "");
	const gchar *const sept[] = { "7", NULL }, *const un[] = { "1", NULL };
	GtMacroEdit lignes[] = {
		{ .id = id, .label = "N", .shortcut = "", .action = "N%d;", .tab = "", .args = sept },
		{ .id = id2, .label = "M", .shortcut = "", .action = "M%d;", .tab = "", .args = un },
	};

	g_assert_true(gt_macro_set_set_arg(gt_macros_get_set(macros), id2, 0, "1"));
	gt_macros_set_usage_arg(macros, id, 0, "9");
	gt_macros_set_usage_arg(macros, id2, 0, "4");
	gt_macros_apply_edits(macros, lignes, G_N_ELEMENTS(lignes), NULL);
	g_assert_false(gt_macros_has_usage(macros, id));
	verifier_envoi(id, "N7;");
	g_assert_true(gt_macros_has_usage(macros, id2));
	verifier_envoi(id2, "M4;");
	desinstaller();
}

static void
test_integre_aucune_ecriture(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macros-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "m.ini", NULL);
	gchar *autre = g_build_filename(dir, "autre.ini", NULL);

	installer_macro_n();
	g_assert_cmpint(gt_macros_save_file(macros, path, FALSE, FALSE), ==, GT_MACROS_SAVED);
	g_assert_true(gt_macros_load_file(macros, path, FALSE));
	g_assert_nonnull(gt_macros_get_path(macros));
	gt_macros_set_embedded(macros, TRUE);
	g_assert_true(gt_macros_is_embedded(macros));
	g_assert_null(gt_macros_get_path(macros));
	g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*no macros file*");
	g_assert_cmpint(gt_macros_save_file(macros, NULL, FALSE, FALSE), ==, GT_MACROS_FAILED);
	g_test_assert_expected_messages();
	g_assert_cmpint(gt_macros_save_file(macros, autre, FALSE, FALSE), ==, GT_MACROS_SAVED);
	g_assert_true(g_file_test(autre, G_FILE_TEST_EXISTS));
	g_assert_null(gt_macros_get_path(macros));
	desinstaller();
	g_assert_true(gt_macros_load_file(macros, autre, FALSE));
	g_assert_cmpuint(gt_macro_set_count(gt_macros_get_set(macros)), ==, 1);
	g_assert_null(gt_macros_get_path(macros));
	gt_macros_set_embedded(macros, FALSE);
	desinstaller();
	test_rmdir(dir);
	g_free(path);
	g_free(autre);
	g_free(dir);
}

static void
test_conflit(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macros-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "m.ini", NULL);
	gchar *texte = NULL;

	installer_macro_n();
	g_assert_cmpint(gt_macros_save_file(macros, path, FALSE, FALSE), ==, GT_MACROS_SAVED);
	g_assert_cmpint(gt_macros_save_file(macros, NULL, FALSE, FALSE), ==, GT_MACROS_SAVED);	/* identité reprise après l'écriture */
	g_assert_true(g_file_set_contents(path, "[gtkterm-macros]\nversion=2\n# tiers\n", -1, NULL));
	g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*changed on disk*");
	g_assert_cmpint(gt_macros_save_file(macros, NULL, FALSE, FALSE), ==, GT_MACROS_CONFLICT);
	g_test_assert_expected_messages();
	g_assert_true(g_file_get_contents(path, &texte, NULL, NULL));
	g_assert_nonnull(strstr(texte, "# tiers"));
	g_free(texte);
	g_assert_cmpint(gt_macros_save_file(macros, NULL, FALSE, TRUE), ==, GT_MACROS_SAVED);
	g_assert_true(g_file_get_contents(path, &texte, NULL, NULL));
	g_assert_null(strstr(texte, "# tiers"));
	g_free(texte);
	/* Nouveau tiers, puis relecture : l'identité est reprise */
	g_assert_true(g_file_set_contents(path, "[gtkterm-macros]\nversion=2\n[macro:1]\naction=ZZ\n", -1, NULL));
	g_assert_true(gt_macros_reload(macros));
	g_assert_cmpuint(gt_macro_set_count(gt_macros_get_set(macros)), ==, 1);
	g_assert_cmpstr(gt_macro_set_get(gt_macros_get_set(macros), 0)->action, ==, "ZZ");
	g_assert_cmpint(gt_macros_save_file(macros, NULL, FALSE, FALSE), ==, GT_MACROS_SAVED);
	desinstaller();
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

/* Reload d'un fichier courant disparu (conflit Overwrite / Reload / Cancel) : refusé avec un message nommant le fichier, jeu et chemin inchangés */
static void
test_reload_fichier_disparu(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macros-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "m.ini", NULL);
	guint avant;

	installer_macro_n();
	g_assert_cmpint(gt_macros_save_file(macros, path, FALSE, FALSE), ==, GT_MACROS_SAVED);
	avant = gt_macro_set_count(gt_macros_get_set(macros));
	g_assert_cmpuint(avant, >, 0);
	g_assert_cmpint(g_unlink(path), ==, 0);
	stub_reset();
	g_assert_false(gt_macros_reload(macros));
	g_assert_cmpint(stub_count("message"), ==, 1);
	g_assert_nonnull(strstr(stub_last("message"), "no longer exists"));
	g_assert_nonnull(strstr(stub_last("message"), path));
	g_assert_cmpuint(gt_macro_set_count(gt_macros_get_set(macros)), ==, avant);
	g_assert_cmpstr(gt_macros_get_path(macros), ==, path);
	/* Le chargement (démarrage, profil) garde son comportement : fichier absent = jeu vide, créé au premier enregistrement */
	g_assert_true(gt_macros_load_file(macros, path, TRUE));
	g_assert_cmpuint(gt_macro_set_count(gt_macros_get_set(macros)), ==, 0);
	desinstaller();
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

/* Aucun fichier courant (onglet dont le profil n'a pas chargé de macros) : la cible est le fichier par défaut ; présent et non vide, il n'est jamais
 * réécrit en silence (conflit, comme un fichier changé sur le disque) ; Reload le lit et en fait le fichier courant ; forcé (Overwrite), il est écrit ;
 * absent ou vide, il est écrit sans question. */
static void
test_defaut_existant_conflit(void)
{
	GtMacros *m = gt_macros_new(stub_link_default(), NULL);
	gchar *defaut = macros_file_default_path();
	gchar *dossier = g_path_get_dirname(defaut);
	const gchar *tiers = "[gtkterm-macros]\nversion=2\n\n[macro:1]\nlabel=A\naction=A\n";
	gchar *texte = NULL;

	g_assert_cmpint(g_mkdir_with_parents(dossier, 0700), ==, 0);
	g_assert_true(g_file_set_contents(defaut, tiers, -1, NULL));
	gt_macro_set_append(gt_macros_get_set(m), "B", NULL, "B", "");
	g_assert_null(gt_macros_get_path(m));
	g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*not overwritten*");
	g_assert_cmpint(gt_macros_save_file(m, NULL, FALSE, FALSE), ==, GT_MACROS_CONFLICT);
	g_test_assert_expected_messages();
	g_assert_true(g_file_get_contents(defaut, &texte, NULL, NULL));
	g_assert_cmpstr(texte, ==, tiers);
	g_clear_pointer(&texte, g_free);
	g_assert_null(gt_macros_get_path(m));
	/* Reload : le fichier par défaut est lu et devient le fichier courant */
	g_assert_true(gt_macros_reload(m));
	g_assert_cmpstr(gt_macros_get_path(m), ==, defaut);
	g_assert_cmpuint(gt_macro_set_count(gt_macros_get_set(m)), ==, 1);
	g_assert_cmpstr(gt_macro_set_get(gt_macros_get_set(m), 0)->label, ==, "A");
	gt_macros_free(m);
	/* Overwrite (forcé) : écrit, devient le fichier courant */
	m = gt_macros_new(stub_link_default(), NULL);
	gt_macro_set_append(gt_macros_get_set(m), "B", NULL, "B", "");
	g_assert_cmpint(gt_macros_save_file(m, NULL, TRUE, TRUE), ==, GT_MACROS_SAVED);
	g_assert_cmpstr(gt_macros_get_path(m), ==, defaut);
	g_assert_true(g_file_get_contents(defaut, &texte, NULL, NULL));
	g_assert_nonnull(strstr(texte, "label=B"));
	g_assert_null(strstr(texte, "label=A"));
	g_clear_pointer(&texte, g_free);
	gt_macros_free(m);
	/* Fichier par défaut vide, puis absent : écrit sans question */
	g_assert_true(g_file_set_contents(defaut, "", -1, NULL));
	m = gt_macros_new(stub_link_default(), NULL);
	gt_macro_set_append(gt_macros_get_set(m), "C", NULL, "C", "");
	g_assert_cmpint(gt_macros_save_file(m, NULL, FALSE, FALSE), ==, GT_MACROS_SAVED);
	gt_macros_free(m);
	g_assert_cmpint(g_unlink(defaut), ==, 0);
	m = gt_macros_new(stub_link_default(), NULL);
	gt_macro_set_append(gt_macros_get_set(m), "D", NULL, "D", "");
	g_assert_cmpint(gt_macros_save_file(m, NULL, FALSE, FALSE), ==, GT_MACROS_SAVED);
	g_assert_true(g_file_get_contents(defaut, &texte, NULL, NULL));
	g_assert_nonnull(strstr(texte, "label=D"));
	g_free(texte);
	gt_macros_free(m);
	g_unlink(defaut);
	g_free(dossier);
	g_free(defaut);
}

static void
test_edited(void)
{
	compteur_edited = 0;
	gt_macros_set_edited_func(macros, rappel_edited, NULL);
	gt_macros_changed(macros);
	g_assert_cmpint(compteur_edited, ==, 1);
	gt_macros_edited(macros);
	g_assert_cmpint(compteur_edited, ==, 2);
	gt_macros_replace_set(macros, gt_macro_set_new());
	g_assert_cmpint(compteur_edited, ==, 3);
	gt_macros_set_edited_func(macros, NULL, NULL);
	gt_macros_edited(macros);
	g_assert_cmpint(compteur_edited, ==, 3);
}

int
main(int argc, char **argv)
{
	gchar *racine;
	gint ret;

	g_test_init(&argc, &argv, NULL);
	racine = test_private_dirs();	/* HOME et XDG privés : le fichier de macros par défaut n'est jamais le vrai */
	/* Garde-fou : le fichier de macros par défaut (écrit puis supprimé par un cas) ne doit jamais viser le vrai ~/.config */
	g_assert_true(g_str_has_prefix(g_get_user_config_dir(), racine));
	gt_app_ui_set(&table_enregistrement);
	macros = gt_macros_new(stub_link_default(), NULL);
	g_test_add_func("/macros_send/arguments-enregistres", test_arguments_enregistres);
	g_test_add_func("/macros_send/arguments-fournis", test_arguments_fournis);
	g_test_add_func("/macros_send/double-pourcent", test_double_pourcent);
	g_test_add_func("/macros_send/sans-libelle", test_sans_libelle);
	g_test_add_func("/macros_send/inconnue-ou-separateur", test_inconnue_ou_separateur);
	g_test_add_func("/macros_send/ecriture-en-echec", test_ecriture_en_echec);
	g_test_add_func("/macros_send/file-pleine", test_file_pleine);
	g_test_add_func("/macros_send/polling-saute-si-file", test_polling_saute_si_file);
	g_test_add_func("/macros_send/envoi-silencieux", test_envoi_silencieux);
	g_test_add_func("/macros_send/chargement-ancien-format-listes", test_chargement_ancien_format_listes);
	g_test_add_func("/macros_send/chargement-fichier-absent", test_chargement_fichier_absent);
	g_test_add_func("/macros_send/chargement-corrompu-garde-le-jeu", test_chargement_corrompu_garde_le_jeu);
	g_test_add_func("/macros_send/enregistrement-echec-une-seule-boite", test_enregistrement_echec_une_seule_boite);
	g_test_add_func("/macros_send/enregistrement-explicite-reaffiche", test_enregistrement_explicite_reaffiche);
	g_test_add_func("/macros_send/chargement-profil-abime", test_chargement_profil_abime);
	g_test_add_func("/macros_send/enregistrement-puis-relecture", test_enregistrement_puis_relecture);
	g_test_add_func("/macros_send/polling-arrete-au-chargement", test_polling_arrete_au_chargement);
	g_test_add_func("/macros_send/deux-instances", test_deux_instances);
	g_test_add_func("/macros_send/usage-prioritaire", test_usage_prioritaire);
	g_test_add_func("/macros_send/usage-jamais-enregistre", test_usage_jamais_enregistre);
	g_test_add_func("/macros_send/usage-efface-au-remplacement", test_usage_efface_au_remplacement);
	g_test_add_func("/macros_send/usage-nombre-change", test_usage_nombre_change);
	g_test_add_func("/macros_send/editeur-efface-usage", test_editeur_efface_usage);
	g_test_add_func("/macros_send/champ-vide-defaut", test_champ_vide_defaut);
	g_test_add_func("/macros_send/resolution-par-argument", test_resolution_par_argument);
	g_test_add_func("/macros_send/sans-defaut-ni-usage", test_sans_defaut_ni_usage);
	g_test_add_func("/macros_send/usage-mauvaise-longueur", test_usage_mauvaise_longueur);
	g_test_add_func("/macros_send/refus-hors-limites", test_refus_hors_limites);
	g_test_add_func("/macros_send/integre-aucune-ecriture", test_integre_aucune_ecriture);
	g_test_add_func("/macros_send/conflit", test_conflit);
	g_test_add_func("/macros_send/defaut-existant-conflit", test_defaut_existant_conflit);
	g_test_add_func("/macros_send/reload-fichier-disparu", test_reload_fichier_disparu);
	g_test_add_func("/macros_send/edited", test_edited);
	ret = g_test_run();
	gt_macros_free(macros);
	test_rmdir_recursive(racine);
	g_free(racine);
	return ret;
}
