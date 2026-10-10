/* Tests d'intégration de macros.c (couche application, sans GTK) : envoi unique par identifiant, fichier, ordonnanceur ; effets d'interface relevés par la
 * table GtAppUi du test (raccourcis et fenêtre d'édition : suite graphique, test_raccourcis_macros.py et test_macros_edition.py). */

#include <glib.h>
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
	g_assert_false(gt_macros_save_file(macros, path, TRUE));
	g_assert_false(gt_macros_save_file(macros, path, TRUE));
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
	g_assert_false(gt_macros_save_file(macros, path, TRUE));
	g_assert_false(gt_macros_save_file(macros, path, TRUE));
	g_assert_cmpint(stub_count("message"), ==, 1);
	gt_macros_forget_save_error(macros);
	g_assert_false(gt_macros_save_file(macros, path, TRUE));
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
	g_assert_true(gt_macros_save_file(macros, NULL, TRUE));
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
	g_assert_true(gt_macros_save_file(macros, path, FALSE));
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

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	gt_app_ui_set(&table_enregistrement);
	macros = gt_macros_new(stub_link_default(), NULL);
	g_test_add_func("/macros_send/arguments-enregistres", test_arguments_enregistres);
	g_test_add_func("/macros_send/arguments-fournis", test_arguments_fournis);
	g_test_add_func("/macros_send/double-pourcent", test_double_pourcent);
	g_test_add_func("/macros_send/sans-libelle", test_sans_libelle);
	g_test_add_func("/macros_send/inconnue-ou-separateur", test_inconnue_ou_separateur);
	g_test_add_func("/macros_send/ecriture-en-echec", test_ecriture_en_echec);
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
	return g_test_run();
}
