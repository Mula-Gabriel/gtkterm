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
enr_status_flash(const gchar *text, guint ms)
{
	stub_record("status_flash", text);
}

static void
enr_macros_changed(void)
{
	stub_record("macros_changed", NULL);
}

static void
enr_polling(guint id, gboolean running)
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
	macros_replace_set(set);
}

static void
desinstaller(void)
{
	macros_replace_set(gt_macro_set_new());
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
	g_assert_true(macros_send(id_vitesse, NULL, -1));
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
	g_assert_true(macros_send(id_vitesse, sept, 1));
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
	macros_send(id_pourcent, NULL, -1);
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
	macros_send(id_sans_libelle, NULL, -1);
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
	g_assert_false(macros_send(9999999, NULL, -1));
	g_assert_false(macros_send(id_separateur, NULL, -1));
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
	g_assert_false(macros_send(id_vitesse, NULL, -1));
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
	g_assert_true(macros_send_quiet(id_vitesse, NULL, -1));
	s = envoye();
	g_assert_cmpstr(s, ==, "V42\r");
	g_free(s);
	g_assert_cmpint(stub_last_origin(), ==, GT_SEND_MACRO);
	g_assert_cmpint(stub_count("status_flash"), ==, 0);
	g_assert_false(macros_send_quiet(id_separateur, NULL, -1));
	g_assert_false(macros_send_quiet(9999999, NULL, -1));
	g_assert_true(macros_send(id_vitesse, NULL, -1));
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
	g_assert_true(macros_load_file(path, FALSE));
	set = macros_get_set();
	g_assert_cmpuint(gt_macro_set_count(set), ==, 2);
	g_assert_cmpstr(gt_macro_set_get(set, 0)->args[0], ==, "115200");
	g_assert_cmpstr(gt_macro_set_get(set, 1)->args[0], ==, "9600");
	g_assert_cmpstr(gt_macro_set_get(set, 1)->args[1], ==, "5");
	g_assert_cmpstr(macros_file_get_path(), ==, path);
	g_assert_cmpint(stub_count("macros_changed"), >=, 1);
	macros_send(gt_macro_set_get(set, 0)->id, NULL, -1);
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
	g_assert_true(macros_load_file(path, TRUE));
	g_assert_cmpuint(gt_macro_set_count(macros_get_set()), ==, 0);
	g_assert_cmpstr(macros_file_get_path(), ==, path);
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

	g_assert_true(macros_load_file(bon, FALSE));
	installer_macros();
	stub_reset();
	g_assert_false(macros_load_file(corrompu, TRUE));
	g_assert_cmpuint(gt_macro_set_count(macros_get_set()), ==, 4);
	g_assert_cmpint(stub_count("message"), ==, 1);
	g_assert_cmpstr(macros_file_get_path(), ==, bon);
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
	g_assert_false(macros_save_file(path, TRUE));
	g_assert_false(macros_save_file(path, TRUE));
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
	g_assert_false(macros_save_file(path, TRUE));
	g_assert_false(macros_save_file(path, TRUE));
	g_assert_cmpint(stub_count("message"), ==, 1);
	macros_forget_save_error();
	g_assert_false(macros_save_file(path, TRUE));
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
	g_assert_false(macros_load_file(path, FALSE));
	g_test_assert_expected_messages();
	g_assert_cmpuint(gt_macro_set_count(macros_get_set()), ==, 0);
	g_assert_cmpstr(macros_file_get_path(), ==, path);
	g_assert_cmpint(stub_count("message"), ==, 0);	/* jamais de boucle imbriquee pendant le chargement du profil */
	iterer_boucle();
	g_assert_cmpint(stub_count("message"), ==, 1);
	g_assert_nonnull(strstr(stub_last("message"), "m.ini"));
	g_assert_true(macros_save_file(NULL, TRUE));
	g_assert_cmpstr(macros_file_get_path(), ==, path);
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
	g_assert_true(macros_save_file(path, FALSE));
	g_assert_cmpstr(macros_file_get_path(), ==, path);
	desinstaller();
	g_assert_true(macros_load_file(path, FALSE));
	g_assert_cmpuint(gt_macro_set_count(macros_get_set()), ==, 4);
	g_assert_cmpstr(gt_macro_set_get(macros_get_set(), 0)->args[0], ==, "42");
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
	g_assert_true(gt_macro_set_set_polling(macros_get_set(), id_vitesse, TRUE, 1000));
	g_assert_true(gt_macro_poller_start(macros_get_poller(), id_vitesse));
	g_assert_cmpuint(stub_sent()->len, ==, 4);			/* envoi immédiat « V42\r » */
	desinstaller();
	g_assert_cmpuint(gt_macro_poller_running_count(macros_get_poller()), ==, 0);
	g_assert_cmpint(stub_count("macro_polling_changed"), ==, 2);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	gt_app_ui_set(&table_enregistrement);
	macros_init();
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
	return g_test_run();
}
