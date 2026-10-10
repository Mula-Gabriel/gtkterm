/* Tests d'intégration de app_config.c (couche application, sans interface) : démarrage (premier lancement, profil « default » absent,
 * fichier illisible), profils (chargement, enregistrement, suppression, noms), validation, réglages [general], état de la fenêtre
 * ([window], x et y ignorés puis retirés) ; et la table de crochets GtAppUi (app_ui.c). HOME et XDG privés : jamais le vrai ~/.config.
 * Fichier de macros bouchonné (stub_macros_file.c). */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "app_config.h"
#include "app_ui.h"
#include "stubs.h"
#include "test_util.h"

#define RC_DEUX_PROFILS \
	"[profile:default]\nport=/dev/null\nspeed=9600\n\n" \
	"[profile:atelier]\nport=/dev/null\nspeed=19200\nmacros_file=/nulle-part/atelier.ini\n"

static gchar *chemin_rc;
static GPtrArray *messages;	/* « I|texte », « W|texte », « E|texte » */
static gint profils_appliques;

static void
enr_message(GtAppMessageKind kind, const gchar *text)
{
	static const gchar lettres[] = { 'I', 'W', 'E' };

	g_ptr_array_add(messages, g_strdup_printf("%c|%s", lettres[kind], text));
}

static void
enr_profil_applique(void)
{
	profils_appliques++;
}

static const GtAppUi table_enregistrement = { .message = enr_message, .profile_applied = enr_profil_applique };

static gint
nb_messages(gchar lettre)
{
	gint n = 0;

	for(guint i = 0; i < messages->len; i++)
		if(((const gchar *)g_ptr_array_index(messages, i))[0] == lettre)
			n++;
	return n;
}

static const gchar *
dernier_message(void)
{
	return messages->len > 0 ? g_ptr_array_index(messages, messages->len - 1) : "";
}

/* Fichier de configuration (NULL : absent), puis magasin relu comme au démarrage de GTKTerm */
static void
demarrer(const gchar *contenu)
{
	gchar *copie = g_strconcat(chemin_rc, ".avant-migration", NULL);

	g_unlink(copie);
	g_free(copie);
	if(contenu == NULL)
		g_unlink(chemin_rc);
	else
		g_assert_true(g_file_set_contents(chemin_rc, contenu, -1, NULL));
	gt_app_ui_set(&table_enregistrement);
	g_ptr_array_set_size(messages, 0);
	profils_appliques = 0;
	stub_reset();
	stub_macros_file_set_path(NULL);
	config_file_init();
}

static gchar *
lire_rc(void)
{
	gchar *contenu = NULL;

	g_assert_true(g_file_get_contents(chemin_rc, &contenu, NULL, NULL));
	return contenu;
}

static GKeyFile *
charger_rc(void)
{
	GKeyFile *kf = g_key_file_new();

	g_assert_true(g_key_file_load_from_file(kf, chemin_rc, G_KEY_FILE_NONE, NULL));
	return kf;
}

/* ---- Crochets GtAppUi ----------------------------------------------------- */

static void
test_crochets(void)
{
	GtAppUi copie = table_enregistrement;

	gt_app_ui_set(&copie);
	copie.message = NULL;	/* la table est copiée : l'original peut changer ou disparaître */
	g_assert_true(gt_app_ui_get()->message == enr_message);
	g_ptr_array_set_size(messages, 0);
	gt_app_message(GT_APP_MSG_WARNING, "valeur %d", 42);
	g_assert_cmpstr(dernier_message(), ==, "W|valeur 42");
	gt_app_status_flash(500, "sans crochet %s", "rien");	/* status_flash NULL : aucun effet */
	g_assert_cmpuint(messages->len, ==, 1);
	gt_app_ui_set(NULL);
	g_assert_nonnull(gt_app_ui_get());
	g_assert_null(gt_app_ui_get()->message);
	g_assert_null(gt_app_ui_get()->profile_applied);
	gt_app_ui_set(&table_enregistrement);
}

/* Sans interface (tests, ligne de commande avant la fenêtre) : le message part sur stderr, jamais perdu */
static void
test_message_sans_crochet(void)
{
	if(g_test_subprocess())
	{
		gt_app_ui_set(NULL);
		gt_app_message(GT_APP_MSG_ERROR, "erreur %s", "sur stderr");
		return;
	}
	g_test_trap_subprocess(NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
	g_test_trap_assert_passed();
	g_test_trap_assert_stderr("*erreur sur stderr*");
}

/* ---- Démarrage ------------------------------------------------------------- */

static void
test_premier_lancement(void)
{
	gchar *contenu;

	demarrer(NULL);
	g_assert_cmpint(Check_configuration_file(), ==, 0);
	g_assert_cmpint(nb_messages('I'), ==, 1);
	g_assert_cmpuint(messages->len, ==, 1);
	g_assert_nonnull(strstr(dernier_message(), chemin_rc));
	contenu = lire_rc();
	g_assert_nonnull(strstr(contenu, "[profile:default]"));
	g_free(contenu);
	g_assert_cmpstr(app_config_active_name(), ==, "default");
	g_assert_cmpuint(config.baudrate, ==, 115200);
	g_assert_cmpint(stub_count("macros_load_file"), ==, 0);
}

static void
test_fichier_sans_profil(void)
{
	GKeyFile *kf;

	demarrer("[window]\nwidth=700\n");
	g_assert_cmpint(Check_configuration_file(), ==, 0);
	g_assert_cmpint(nb_messages('I'), ==, 1);
	kf = charger_rc();
	g_assert_true(g_key_file_has_group(kf, "profile:default"));
	g_assert_cmpint(g_key_file_get_integer(kf, "window", "width", NULL), ==, 700);
	g_key_file_free(kf);
}

/* Spec §3.2-14 : d'autres profils mais pas « default » : valeurs par défaut, aucun message, rien n'est écrit */
static void
test_default_absent(void)
{
	gchar *avant, *apres;

	demarrer("[profile:atelier]\nport=/dev/null\nspeed=19200\n");
	avant = lire_rc();
	config.baudrate = 1;
	g_assert_cmpint(Check_configuration_file(), ==, 0);
	g_assert_cmpuint(messages->len, ==, 0);
	g_assert_cmpuint(config.baudrate, ==, 115200);
	g_assert_cmpstr(config.port, ==, GT_PROFILE_DEFAULT_PORT);
	g_assert_cmpstr(app_config_active_name(), ==, "default");
	g_assert_cmpint(stub_count("macros_load_file"), ==, 0);
	apres = lire_rc();
	g_assert_cmpstr(apres, ==, avant);
	g_assert_false(app_config_has_profile("default"));
	g_free(avant);
	g_free(apres);
}

/* ---- Profils ---------------------------------------------------------------- */

static void
test_chargement_profils(void)
{
	GError *err = NULL;

	demarrer(RC_DEUX_PROFILS);
	g_assert_cmpint(Check_configuration_file(), ==, 0);
	g_assert_cmpuint(config.baudrate, ==, 9600);
	g_assert_cmpstr(stub_last("macros_load_file"), ==, "(null)|non-interactif");
	g_assert_cmpint(profils_appliques, ==, 0);	/* démarrage : l'interface applique elle-même le profil */
	config.lock_port = FALSE;	/* --disable-port-lock */
	g_assert_true(app_config_load_profile("ATELIER", &err));
	g_assert_no_error(err);
	g_assert_cmpuint(config.baudrate, ==, 19200);
	g_assert_false(config.lock_port);
	g_assert_cmpstr(app_config_active_name(), ==, "ATELIER");
	g_assert_cmpstr(stub_last("macros_load_file"), ==, "/nulle-part/atelier.ini|non-interactif");
	g_assert_cmpint(profils_appliques, ==, 1);
	g_assert_cmpuint(messages->len, ==, 0);
}

static void
test_profil_inconnu(void)
{
	GError *err = NULL;

	demarrer(RC_DEUX_PROFILS);
	g_assert_cmpint(Check_configuration_file(), ==, 0);
	g_assert_false(app_config_load_profile("absent", &err));
	g_assert_error(err, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_GROUP_NOT_FOUND);
	g_assert_nonnull(strstr(err->message, "absent"));
	g_clear_error(&err);
	g_assert_cmpuint(config.baudrate, ==, 9600);
	g_assert_cmpstr(app_config_active_name(), ==, "default");
	g_assert_cmpint(profils_appliques, ==, 0);
	g_assert_cmpuint(messages->len, ==, 0);	/* l'appelant affiche l'erreur */
	g_assert_cmpint(Load_configuration_from_file("absent"), ==, -1);
	g_assert_cmpint(nb_messages('E'), ==, 1);
	g_assert_nonnull(strstr(dernier_message(), "absent"));
}

static void
test_verification(void)
{
	demarrer("[profile:default]\nport=/dev/null\nbits=3\n\n[profile:abime]\nport=/dev/null\nstopbits=5\n");
	g_assert_cmpint(Check_configuration_file(), ==, 0);
	g_assert_cmpuint(messages->len, ==, 0);	/* le démarrage ne valide pas : la ligne de commande le fait après ses options */
	g_assert_cmpint(config.bits, ==, 3);
	Verify_configuration();
	g_assert_cmpint(nb_messages('E'), ==, 1);
	g_assert_cmpint(config.bits, ==, 8);
	Verify_configuration();
	g_assert_cmpint(nb_messages('E'), ==, 1);
	g_assert_true(app_config_load_profile("abime", NULL));
	g_assert_cmpint(nb_messages('E'), ==, 2);	/* un seul message pour le profil chargé */
	g_assert_cmpint(config.stopbits, ==, 1);
	g_assert_cmpint(profils_appliques, ==, 1);
}

static void
test_enregistrement(void)
{
	GError *err = NULL;
	GKeyFile *kf;
	gchar **noms;
	gchar *valeur;

	demarrer(RC_DEUX_PROFILS);
	g_assert_cmpint(Check_configuration_file(), ==, 0);
	stub_macros_file_set_path("/nulle-part/courant.ini");
	config.baudrate = 57600;
	g_assert_true(app_config_save_profile("  atelier é  ", &err));
	g_assert_no_error(err);
	g_assert_cmpstr(app_config_active_name(), ==, "atelier é");
	kf = charger_rc();
	g_assert_true(g_key_file_has_group(kf, "profile:atelier é"));
	g_assert_cmpint(g_key_file_get_integer(kf, "profile:atelier é", "speed", NULL), ==, 57600);
	valeur = g_key_file_get_string(kf, "profile:atelier é", "macros_file", NULL);
	g_assert_cmpstr(valeur, ==, "/nulle-part/courant.ini");
	g_free(valeur);
	g_key_file_free(kf);
	g_assert_true(app_config_has_profile("ATELIER É"));
	g_assert_true(app_config_has_profile(" atelier é "));
	noms = app_config_list_profiles();
	g_assert_true(g_strv_contains((const gchar * const *)noms, "atelier é"));
	g_assert_true(g_strv_contains((const gchar * const *)noms, "atelier"));
	g_strfreev(noms);
	config.baudrate = 1200;
	g_assert_true(app_config_load_profile("atelier é", NULL));
	g_assert_cmpuint(config.baudrate, ==, 57600);
	g_assert_cmpuint(messages->len, ==, 0);
}

static void
verifier_nom_refuse(const gchar *nom, gint code)
{
	GError *err = NULL;

	g_assert_false(app_config_save_profile(nom, &err));
	g_assert_error(err, GT_PROFILE_ERROR, code);
	g_clear_error(&err);
}

static void
test_enregistrement_nom_invalide(void)
{
	gchar *avant, *apres;

	demarrer(RC_DEUX_PROFILS);
	g_assert_cmpint(Check_configuration_file(), ==, 0);
	avant = lire_rc();
	verifier_nom_refuse("a[b", GT_PROFILE_ERROR_NAME_INVALID_CHAR);
	verifier_nom_refuse("Window", GT_PROFILE_ERROR_NAME_RESERVED);
	verifier_nom_refuse("   ", GT_PROFILE_ERROR_NAME_EMPTY);
	apres = lire_rc();
	g_assert_cmpstr(apres, ==, avant);
	g_assert_cmpstr(app_config_active_name(), ==, "default");
	g_free(avant);
	g_free(apres);
}

/* C6 : « DEFAULT » écrase « default » (même profil sans casse), sans groupe en double */
static void
test_ecrasement_sans_doublon(void)
{
	GKeyFile *kf;

	demarrer(RC_DEUX_PROFILS);
	g_assert_cmpint(Check_configuration_file(), ==, 0);
	g_assert_true(app_config_has_profile("DEFAULT"));
	config.baudrate = 38400;
	g_assert_true(app_config_save_profile("DEFAULT", NULL));
	kf = charger_rc();
	g_assert_true(g_key_file_has_group(kf, "profile:default"));
	g_assert_false(g_key_file_has_group(kf, "profile:DEFAULT"));
	g_assert_cmpint(g_key_file_get_integer(kf, "profile:default", "speed", NULL), ==, 38400);
	g_key_file_free(kf);
}

static void
test_suppression(void)
{
	GError *err = NULL;
	gchar *contenu;

	demarrer(RC_DEUX_PROFILS);
	g_assert_cmpint(Check_configuration_file(), ==, 0);
	g_assert_true(app_config_delete_profile("ATELIER", &err));
	g_assert_no_error(err);
	g_assert_false(app_config_has_profile("atelier"));
	contenu = lire_rc();
	g_assert_null(strstr(contenu, "[profile:atelier]"));
	g_assert_nonnull(strstr(contenu, "[profile:default]"));
	g_free(contenu);
	g_assert_false(app_config_delete_profile("atelier", &err));
	g_assert_error(err, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_GROUP_NOT_FOUND);
	g_clear_error(&err);
}

/* Une autre instance (ou une modification à la main) ajoute un profil : la liste le voit, une écriture ne l'efface pas */
static void
test_relecture_autre_instance(void)
{
	GtWindowState st;
	gchar *contenu, *ajout;
	gchar **noms;
	GKeyFile *kf;

	demarrer(RC_DEUX_PROFILS);
	g_assert_cmpint(Check_configuration_file(), ==, 0);
	contenu = lire_rc();
	ajout = g_strconcat(contenu, "\n[profile:externe]\nport=/dev/null\nspeed=4800\n", NULL);
	g_assert_true(g_file_set_contents(chemin_rc, ajout, -1, NULL));
	noms = app_config_list_profiles();
	g_assert_true(g_strv_contains((const gchar * const *)noms, "externe"));
	g_strfreev(noms);
	app_config_load_window_state(&st);
	app_config_save_window_state(&st);
	kf = charger_rc();
	g_assert_true(g_key_file_has_group(kf, "profile:externe"));
	g_key_file_free(kf);
	g_free(contenu);
	g_free(ajout);
}

static void
test_fichier_de_macros_retenu(void)
{
	GKeyFile *kf;
	gchar *valeur;

	demarrer(RC_DEUX_PROFILS);
	g_assert_cmpint(Check_configuration_file(), ==, 0);
	stub_macros_file_set_path("/nulle-part/autre.ini");
	config_remember_macros_file();
	kf = charger_rc();
	valeur = g_key_file_get_string(kf, "profile:default", "macros_file", NULL);
	g_assert_cmpstr(valeur, ==, "/nulle-part/autre.ini");
	g_free(valeur);
	g_key_file_free(kf);
	/* Profil actif supprimé entre-temps : mémoire seulement, pas de profil fantôme réduit à macros_file */
	g_assert_true(app_config_delete_profile("default", NULL));
	stub_macros_file_set_path("/nulle-part/troisieme.ini");
	config_remember_macros_file();
	g_assert_cmpstr(config.macros_file, ==, "/nulle-part/troisieme.ini");
	kf = charger_rc();
	g_assert_false(g_key_file_has_group(kf, "profile:default"));
	g_key_file_free(kf);
	g_assert_cmpuint(messages->len, ==, 0);
}

/* Fichier illisible : défauts, un message, plus aucune écriture (ni état de fenêtre, ni profil, ni [general]) */
static void
test_lecture_seule(void)
{
	GtWindowState st;
	GError *err = NULL;
	gchar *avant, *apres, *copie;

	demarrer("ceci n'est pas un fichier ini\n[profile:a]\nport=/dev/x\n");
	avant = lire_rc();
	g_assert_cmpint(Check_configuration_file(), ==, -1);
	g_assert_cmpint(nb_messages('E'), ==, 1);
	g_assert_nonnull(strstr(dernier_message(), ".gtktermrc"));
	g_assert_cmpuint(config.baudrate, ==, 115200);
	app_config_load_window_state(&st);
	st.width = 900;
	app_config_save_window_state(&st);
	g_assert_false(app_config_save_profile("atelier", &err));
	g_assert_nonnull(err);
	g_clear_error(&err);
	config_save_skipped_revision("0123abc");
	g_assert_cmpint(nb_messages('E'), ==, 2);	/* écriture demandée par l'utilisateur et refusée : signalée */
	apres = lire_rc();
	g_assert_cmpstr(apres, ==, avant);
	copie = g_strconcat(chemin_rc, ".avant-migration", NULL);
	g_assert_false(g_file_test(copie, G_FILE_TEST_EXISTS));
	g_free(copie);
	g_free(avant);
	g_free(apres);
}

/* M34 : « Skip this version » enregistré dans [general] et relu au démarrage suivant ; NULL l'efface */
static void
test_revision_ignoree(void)
{
	GKeyFile *kf;
	gchar *valeur;

	demarrer(RC_DEUX_PROFILS);
	g_assert_cmpint(Check_configuration_file(), ==, 0);
	g_assert_null(settings.update_skipped_revision);
	config_save_skipped_revision("0123abc");
	g_assert_cmpstr(settings.update_skipped_revision, ==, "0123abc");
	kf = charger_rc();
	valeur = g_key_file_get_string(kf, "general", "update_skipped_revision", NULL);
	g_assert_cmpstr(valeur, ==, "0123abc");
	g_free(valeur);
	g_key_file_free(kf);
	config_file_init();
	g_assert_cmpint(Check_configuration_file(), ==, 0);
	g_assert_cmpstr(settings.update_skipped_revision, ==, "0123abc");
	config_save_skipped_revision(NULL);
	g_assert_null(settings.update_skipped_revision);
	config_file_init();
	g_assert_cmpint(Check_configuration_file(), ==, 0);
	g_assert_null(settings.update_skipped_revision);
	g_assert_cmpuint(messages->len, ==, 0);
}

/* Case « Check for updates at startup » (fenêtre « Updates ») : seule la clé [general] update_check_startup est écrite, après
 * relecture ; valeur inchangée : aucune écriture */
static void
test_verification_au_demarrage(void)
{
	GKeyFile *kf;
	gchar *avant, *apres;

	demarrer(RC_DEUX_PROFILS);
	g_assert_cmpint(Check_configuration_file(), ==, 0);
	g_assert_true(settings.update_check_startup);	/* absente du fichier : TRUE */
	avant = lire_rc();
	config_save_update_check_startup(TRUE);
	apres = lire_rc();
	g_assert_cmpstr(apres, ==, avant);	/* valeur inchangée : rien n'est écrit */
	g_free(apres);
	config_save_update_check_startup(FALSE);
	g_assert_false(settings.update_check_startup);
	kf = charger_rc();
	g_assert_true(g_key_file_has_key(kf, "general", "update_check_startup", NULL));
	g_assert_false(g_key_file_get_boolean(kf, "general", "update_check_startup", NULL));
	g_assert_false(g_key_file_has_key(kf, "general", "update_skipped_revision", NULL));	/* seule la clé modifiée */
	g_assert_cmpuint(g_key_file_get_uint64(kf, "profile:atelier", "speed", NULL), ==, 19200);	/* profils conservés */
	g_key_file_free(kf);
	config_file_init();
	g_assert_cmpint(Check_configuration_file(), ==, 0);
	g_assert_false(settings.update_check_startup);
	g_assert_cmpuint(messages->len, ==, 0);
	g_free(avant);
}

/* ---- État de la fenêtre ------------------------------------------------------ */

static void
test_etat_fenetre_defauts(void)
{
	GtWindowState st;

	demarrer(NULL);
	app_config_load_window_state(&st);
	g_assert_cmpint(st.width, ==, 750);
	g_assert_cmpint(st.height, ==, 550);
	g_assert_false(st.maximized);
	g_assert_cmpint(st.h_paned, ==, -1);
	g_assert_cmpint(st.v_paned, ==, -1);
	g_assert_true(st.macro_panel_visible);
	g_assert_false(st.script_panel_visible);
	g_assert_false(st.hex_bar_visible);
}

static void
test_etat_fenetre_aller_retour(void)
{
	GtWindowState st = { 1024, 700, TRUE, 300, 200, FALSE, TRUE, TRUE };
	GtWindowState lu;
	GKeyFile *kf;

	demarrer(RC_DEUX_PROFILS);
	g_assert_cmpint(Check_configuration_file(), ==, 0);
	app_config_save_window_state(&st);
	kf = charger_rc();
	g_assert_cmpint(g_key_file_get_integer(kf, "window", "width", NULL), ==, 1024);
	g_assert_cmpint(g_key_file_get_integer(kf, "window", "height", NULL), ==, 700);
	g_assert_true(g_key_file_get_boolean(kf, "window", "maximized", NULL));
	g_assert_cmpint(g_key_file_get_integer(kf, "window", "paned_position", NULL), ==, 300);
	g_assert_cmpint(g_key_file_get_integer(kf, "window", "v_paned_position", NULL), ==, 200);
	g_assert_false(g_key_file_get_boolean(kf, "window", "macro_panel_visible", NULL));
	g_assert_true(g_key_file_get_boolean(kf, "window", "script_panel_visible", NULL));
	g_assert_true(g_key_file_get_boolean(kf, "window", "hex_bar_visible", NULL));
	g_assert_true(g_key_file_has_group(kf, "profile:default"));
	g_assert_true(g_key_file_has_group(kf, "profile:atelier"));
	g_key_file_free(kf);
	config_file_init();	/* relu comme au démarrage suivant */
	app_config_load_window_state(&lu);
	g_assert_cmpint(lu.width, ==, 1024);
	g_assert_cmpint(lu.height, ==, 700);
	g_assert_true(lu.maximized);
	g_assert_cmpint(lu.h_paned, ==, 300);
	g_assert_cmpint(lu.v_paned, ==, 200);
	g_assert_false(lu.macro_panel_visible);
	g_assert_true(lu.script_panel_visible);
	g_assert_true(lu.hex_bar_visible);
}

/* Spec §3.2-7 : x et y ne sont plus lus, et disparaissent au premier enregistrement ; les autres groupes restent */
static void
test_etat_fenetre_x_y_retires(void)
{
	GtWindowState st;
	GKeyFile *kf;
	gchar *valeur;

	demarrer("[window]\nx=10\ny=20\nwidth=800\nheight=600\n\n[profile:default]\nport=/dev/null\n\n[colors]\nbg=#000000\n");
	g_assert_cmpint(Check_configuration_file(), ==, 0);
	app_config_load_window_state(&st);
	g_assert_cmpint(st.width, ==, 800);
	g_assert_cmpint(st.height, ==, 600);
	app_config_save_window_state(&st);
	kf = charger_rc();
	g_assert_false(g_key_file_has_key(kf, "window", "x", NULL));
	g_assert_false(g_key_file_has_key(kf, "window", "y", NULL));
	g_assert_cmpint(g_key_file_get_integer(kf, "window", "width", NULL), ==, 800);
	valeur = g_key_file_get_string(kf, "colors", "bg", NULL);
	g_assert_cmpstr(valeur, ==, "#000000");
	g_free(valeur);
	g_key_file_free(kf);
}

/* Spec §8 : tailles ≤ 0 ou > 10 000, positions négatives, valeurs illisibles : ignorées */
static void
test_etat_fenetre_aberrant(void)
{
	GtWindowState st;

	demarrer("[window]\nwidth=-5\nheight=20000\npaned_position=-3\nv_paned_position=abc\nmaximized=peut-etre\nscript_panel_visible=yes\n");
	app_config_load_window_state(&st);
	g_assert_cmpint(st.width, ==, 750);
	g_assert_cmpint(st.height, ==, 550);
	g_assert_cmpint(st.h_paned, ==, -1);
	g_assert_cmpint(st.v_paned, ==, -1);
	g_assert_false(st.maximized);
	g_assert_true(st.script_panel_visible);
	demarrer("[window]\nwidth=900\nheight=0\n");
	app_config_load_window_state(&st);
	g_assert_cmpint(st.width, ==, 750);
	g_assert_cmpint(st.height, ==, 550);
}

int
main(int argc, char **argv)
{
	gchar *racine = test_private_dirs();
	gint ret;

	g_test_init(&argc, &argv, NULL);
	/* Garde-fou : le magasin ne doit jamais viser le vrai ~/.config */
	g_assert_true(g_str_has_prefix(g_get_user_config_dir(), racine));
	chemin_rc = g_build_filename(g_get_user_config_dir(), ".gtktermrc", NULL);
	messages = g_ptr_array_new_with_free_func(g_free);
	gt_app_ui_set(&table_enregistrement);

	g_test_add_func("/app_config/crochets", test_crochets);
	g_test_add_func("/app_config/message-sans-crochet", test_message_sans_crochet);
	g_test_add_func("/app_config/premier-lancement", test_premier_lancement);
	g_test_add_func("/app_config/fichier-sans-profil", test_fichier_sans_profil);
	g_test_add_func("/app_config/default-absent", test_default_absent);
	g_test_add_func("/app_config/chargement-profils", test_chargement_profils);
	g_test_add_func("/app_config/profil-inconnu", test_profil_inconnu);
	g_test_add_func("/app_config/verification", test_verification);
	g_test_add_func("/app_config/enregistrement", test_enregistrement);
	g_test_add_func("/app_config/enregistrement-nom-invalide", test_enregistrement_nom_invalide);
	g_test_add_func("/app_config/ecrasement-sans-doublon", test_ecrasement_sans_doublon);
	g_test_add_func("/app_config/suppression", test_suppression);
	g_test_add_func("/app_config/relecture-autre-instance", test_relecture_autre_instance);
	g_test_add_func("/app_config/fichier-de-macros-retenu", test_fichier_de_macros_retenu);
	g_test_add_func("/app_config/lecture-seule", test_lecture_seule);
	g_test_add_func("/app_config/revision-ignoree", test_revision_ignoree);
	g_test_add_func("/app_config/verification-au-demarrage", test_verification_au_demarrage);
	g_test_add_func("/app_config/etat-fenetre-defauts", test_etat_fenetre_defauts);
	g_test_add_func("/app_config/etat-fenetre-aller-retour", test_etat_fenetre_aller_retour);
	g_test_add_func("/app_config/etat-fenetre-x-y-retires", test_etat_fenetre_x_y_retires);
	g_test_add_func("/app_config/etat-fenetre-aberrant", test_etat_fenetre_aberrant);
	ret = g_test_run();

	g_ptr_array_unref(messages);
	g_free(chemin_rc);
	test_rmdir_recursive(racine);
	g_free(racine);
	return ret;
}
