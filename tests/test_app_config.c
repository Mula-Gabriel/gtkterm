/* Tests d'intégration de app_config.c (couche application, sans interface) : démarrage (premier lancement, profil « default » absent,
 * fichier illisible), profils (chargement, enregistrement, suppression, noms), validation, réglages [general], état de la fenêtre
 * ([window], x et y ignorés puis retirés) ; et la table de crochets GtAppUi (app_ui.c). HOME et XDG privés : jamais le vrai ~/.config.
 * Vraie couche application (gtkterm_app) : le profil en cours et ses macros sont ceux d'une session de test (une par cas). */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>
#include <utime.h>

#include "app_config.h"
#include "app_ui.h"
#include "session.h"
#include "test_util.h"

#define RC_DEUX_PROFILS \
	"[profile:default]\nport=/dev/null\nspeed=9600\n\n" \
	"[profile:atelier]\nport=/dev/null\nspeed=19200\nmacros_file=/nulle-part/atelier.ini\n"

static gchar *chemin_rc;
static GPtrArray *messages;	/* « I|texte », « W|texte », « E|texte » */
static gint profils_appliques;
static GtSession *dernier_profil_applique;

static void
enr_message(GtAppMessageKind kind, const gchar *text)
{
	static const gchar lettres[] = { 'I', 'W', 'E' };

	g_ptr_array_add(messages, g_strdup_printf("%c|%s", lettres[kind], text));
}

static void
enr_profil_applique(GtSession *session)
{
	profils_appliques++;
	dernier_profil_applique = session;
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

/* Fichier de configuration (NULL : absent), puis magasin relu comme au démarrage de GTKTerm ; rend une session neuve (à libérer) */
static GtSession *
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
	config_file_init();
	return gt_session_new(NULL, NULL);
}

/* Chemin courant des macros de la session (fichier absent : jeu vide, chemin retenu) */
static void
chemin_macros(GtSession *s, const gchar *chemin)
{
	g_assert_true(gt_macros_load_file(gt_session_get_macros(s), chemin, FALSE));
}

static const gchar *
macros_de(GtSession *s)
{
	return gt_macros_get_path(gt_session_get_macros(s));
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
	gt_app_status_flash(NULL, 500, "sans crochet %s", "rien");	/* status_flash NULL : aucun effet */
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
	GtSession *s;
	gchar *contenu;

	s = demarrer(NULL);
	g_assert_cmpint(Check_configuration_file(s), ==, 0);
	g_assert_cmpint(nb_messages('I'), ==, 1);
	g_assert_cmpuint(messages->len, ==, 1);
	g_assert_nonnull(strstr(dernier_message(), chemin_rc));
	contenu = lire_rc();
	g_assert_nonnull(strstr(contenu, "[profile:default]"));
	g_free(contenu);
	g_assert_cmpstr(gt_session_get_profile_name(s), ==, "default");
	g_assert_cmpuint(gt_session_get_profile(s)->baudrate, ==, 115200);
	g_assert_null(macros_de(s));
	g_object_unref(s);
}

static void
test_fichier_sans_profil(void)
{
	GtSession *s;
	GKeyFile *kf;

	s = demarrer("[window]\nwidth=700\n");
	g_assert_cmpint(Check_configuration_file(s), ==, 0);
	g_assert_cmpint(nb_messages('I'), ==, 1);
	kf = charger_rc();
	g_assert_true(g_key_file_has_group(kf, "profile:default"));
	g_assert_cmpint(g_key_file_get_integer(kf, "window", "width", NULL), ==, 700);
	g_key_file_free(kf);
	g_object_unref(s);
}

/* Spec §3.2-14 : d'autres profils mais pas « default » : valeurs par défaut, aucun message, rien n'est écrit */
static void
test_default_absent(void)
{
	GtSession *s;
	gchar *avant, *apres;

	s = demarrer("[profile:atelier]\nport=/dev/null\nspeed=19200\n");
	avant = lire_rc();
	gt_session_get_profile(s)->baudrate = 1;
	g_assert_cmpint(Check_configuration_file(s), ==, 0);
	g_assert_cmpuint(messages->len, ==, 0);
	g_assert_cmpuint(gt_session_get_profile(s)->baudrate, ==, 115200);
	g_assert_cmpstr(gt_session_get_profile(s)->port, ==, GT_PROFILE_DEFAULT_PORT);
	g_assert_cmpstr(gt_session_get_profile_name(s), ==, "default");
	g_assert_null(macros_de(s));
	apres = lire_rc();
	g_assert_cmpstr(apres, ==, avant);
	g_assert_false(app_config_has_profile("default"));
	g_free(avant);
	g_free(apres);
	g_object_unref(s);
}

/* ---- Profils ---------------------------------------------------------------- */

static void
test_chargement_profils(void)
{
	GtSession *s;
	GError *err = NULL;
	gchar *defaut = macros_file_default_path();

	s = demarrer(RC_DEUX_PROFILS);
	g_assert_cmpint(Check_configuration_file(s), ==, 0);
	g_assert_cmpuint(gt_session_get_profile(s)->baudrate, ==, 9600);
	g_assert_cmpstr(macros_de(s), ==, defaut);	/* profil sans fichier : fichier par défaut, non interactif */
	g_assert_cmpint(profils_appliques, ==, 0);	/* démarrage : l'interface applique elle-même le profil */
	gt_session_get_profile(s)->lock_port = FALSE;	/* --disable-port-lock */
	g_assert_true(app_config_load_profile(s, "ATELIER", &err));
	g_assert_no_error(err);
	g_assert_cmpuint(gt_session_get_profile(s)->baudrate, ==, 19200);
	g_assert_false(gt_session_get_profile(s)->lock_port);
	g_assert_cmpstr(gt_session_get_profile_name(s), ==, "ATELIER");
	g_assert_cmpstr(macros_de(s), ==, "/nulle-part/atelier.ini");
	g_assert_cmpint(profils_appliques, ==, 1);
	g_assert_true(dernier_profil_applique == s);	/* crochet appelé pour cette session */
	g_assert_cmpuint(messages->len, ==, 0);
	g_free(defaut);
	g_object_unref(s);
}

static void
test_profil_inconnu(void)
{
	GtSession *s;
	GError *err = NULL;

	s = demarrer(RC_DEUX_PROFILS);
	g_assert_cmpint(Check_configuration_file(s), ==, 0);
	g_assert_false(app_config_load_profile(s, "absent", &err));
	g_assert_error(err, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_GROUP_NOT_FOUND);
	g_assert_nonnull(strstr(err->message, "absent"));
	g_clear_error(&err);
	g_assert_cmpuint(gt_session_get_profile(s)->baudrate, ==, 9600);
	g_assert_cmpstr(gt_session_get_profile_name(s), ==, "default");
	g_assert_cmpint(profils_appliques, ==, 0);
	g_assert_cmpuint(messages->len, ==, 0);	/* l'appelant affiche l'erreur */
	g_assert_cmpint(Load_configuration_from_file(s, "absent"), ==, -1);
	g_assert_cmpint(nb_messages('E'), ==, 1);
	g_assert_nonnull(strstr(dernier_message(), "absent"));
	g_object_unref(s);
}

/* Fichier devenu illisible en cours de session : le chargement d'un profil rend la vraie cause, pas « No section » */
static void
test_profil_cause_reelle(void)
{
	GtSession *s;
	GError *err = NULL;
	struct utimbuf t = { 1000000, 1000000 };

	if(getuid() == 0)
	{
		g_test_skip("les droits ne s'appliquent pas à root");
		return;
	}
	s = demarrer(RC_DEUX_PROFILS);
	g_assert_cmpint(Check_configuration_file(s), ==, 0);
	g_assert_cmpint(g_chmod(chemin_rc, 0), ==, 0);
	g_assert_cmpint(utime(chemin_rc, &t), ==, 0);
	g_assert_false(app_config_load_profile(s, "atelier", &err));
	g_assert_error(err, G_FILE_ERROR, G_FILE_ERROR_PERM);
	g_assert_nonnull(strstr(err->message, "Cannot read the configuration file"));
	g_clear_error(&err);
	g_assert_cmpint(g_chmod(chemin_rc, 0600), ==, 0);
	g_object_unref(s);
}

static void
test_verification(void)
{
	GtSession *s;

	s = demarrer("[profile:default]\nport=/dev/null\nbits=3\n\n[profile:abime]\nport=/dev/null\nstopbits=5\n");
	g_assert_cmpint(Check_configuration_file(s), ==, 0);
	g_assert_cmpuint(messages->len, ==, 0);	/* le démarrage ne valide pas : la ligne de commande le fait après ses options */
	g_assert_cmpint(gt_session_get_profile(s)->bits, ==, 3);
	Verify_configuration(s);
	g_assert_cmpint(nb_messages('E'), ==, 1);
	g_assert_cmpint(gt_session_get_profile(s)->bits, ==, 8);
	Verify_configuration(s);
	g_assert_cmpint(nb_messages('E'), ==, 1);
	g_assert_true(app_config_load_profile(s, "abime", NULL));
	g_assert_cmpint(nb_messages('E'), ==, 2);	/* un seul message pour le profil chargé */
	g_assert_cmpint(gt_session_get_profile(s)->stopbits, ==, 1);
	g_assert_cmpint(profils_appliques, ==, 1);
	g_object_unref(s);
}

static void
test_enregistrement(void)
{
	GtSession *s;
	GError *err = NULL;
	GKeyFile *kf;
	gchar **noms;
	gchar *valeur;

	s = demarrer(RC_DEUX_PROFILS);
	g_assert_cmpint(Check_configuration_file(s), ==, 0);
	chemin_macros(s, "/nulle-part/courant.ini");
	gt_session_get_profile(s)->baudrate = 57600;
	/* État réel de la session recopié dans le profil : options (menus) et auto-reconnexion de la liaison, quel que soit le profil en mémoire */
	gt_session_get_profile(s)->echo = FALSE;
	gt_session_get_profile(s)->auto_reconnect = TRUE;
	g_object_set(gt_session_get_options(s), "echo", TRUE, NULL);
	g_object_set(gt_session_get_link(s), "auto-reconnect", FALSE, NULL);
	g_assert_true(app_config_save_profile(s, "  atelier é  ", &err));
	g_assert_no_error(err);
	g_assert_cmpstr(gt_session_get_profile_name(s), ==, "atelier é");
	kf = charger_rc();
	g_assert_true(g_key_file_has_group(kf, "profile:atelier é"));
	g_assert_cmpint(g_key_file_get_integer(kf, "profile:atelier é", "speed", NULL), ==, 57600);
	g_assert_true(g_key_file_get_boolean(kf, "profile:atelier é", "echo", NULL));
	g_assert_true(g_key_file_has_key(kf, "profile:atelier é", "autoreconnect_enabled", NULL));
	g_assert_false(g_key_file_get_boolean(kf, "profile:atelier é", "autoreconnect_enabled", NULL));
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
	gt_session_get_profile(s)->baudrate = 1200;
	g_assert_true(app_config_load_profile(s, "atelier é", NULL));
	g_assert_cmpuint(gt_session_get_profile(s)->baudrate, ==, 57600);
	g_assert_cmpuint(messages->len, ==, 0);
	g_object_unref(s);
}

static void
verifier_nom_refuse(GtSession *s, const gchar *nom, gint code)
{
	GError *err = NULL;

	g_assert_false(app_config_save_profile(s, nom, &err));
	g_assert_error(err, GT_PROFILE_ERROR, code);
	g_clear_error(&err);
}

static void
test_enregistrement_nom_invalide(void)
{
	GtSession *s;
	gchar *avant, *apres;

	s = demarrer(RC_DEUX_PROFILS);
	g_assert_cmpint(Check_configuration_file(s), ==, 0);
	avant = lire_rc();
	verifier_nom_refuse(s, "a[b", GT_PROFILE_ERROR_NAME_INVALID_CHAR);
	verifier_nom_refuse(s, "Window", GT_PROFILE_ERROR_NAME_RESERVED);
	verifier_nom_refuse(s, "   ", GT_PROFILE_ERROR_NAME_EMPTY);
	apres = lire_rc();
	g_assert_cmpstr(apres, ==, avant);
	g_assert_cmpstr(gt_session_get_profile_name(s), ==, "default");
	g_free(avant);
	g_free(apres);
	g_object_unref(s);
}

/* C6 : « DEFAULT » écrase « default » (même profil sans casse), sans groupe en double */
static void
test_ecrasement_sans_doublon(void)
{
	GtSession *s;
	GKeyFile *kf;

	s = demarrer(RC_DEUX_PROFILS);
	g_assert_cmpint(Check_configuration_file(s), ==, 0);
	g_assert_true(app_config_has_profile("DEFAULT"));
	gt_session_get_profile(s)->baudrate = 38400;
	g_assert_true(app_config_save_profile(s, "DEFAULT", NULL));
	kf = charger_rc();
	g_assert_true(g_key_file_has_group(kf, "profile:default"));
	g_assert_false(g_key_file_has_group(kf, "profile:DEFAULT"));
	g_assert_cmpint(g_key_file_get_integer(kf, "profile:default", "speed", NULL), ==, 38400);
	g_key_file_free(kf);
	g_object_unref(s);
}

static void
test_suppression(void)
{
	GtSession *s;
	GError *err = NULL;
	gchar *contenu;

	s = demarrer(RC_DEUX_PROFILS);
	g_assert_cmpint(Check_configuration_file(s), ==, 0);
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
	g_object_unref(s);
}

/* Une autre instance (ou une modification à la main) ajoute un profil : la liste le voit, une écriture ne l'efface pas */
static void
test_relecture_autre_instance(void)
{
	GtSession *s;
	GtWindowState st;
	gchar *contenu, *ajout;
	gchar **noms;
	GKeyFile *kf;

	s = demarrer(RC_DEUX_PROFILS);
	g_assert_cmpint(Check_configuration_file(s), ==, 0);
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
	g_object_unref(s);
}

static void
test_fichier_de_macros_retenu(void)
{
	GtSession *s;
	GKeyFile *kf;
	gchar *valeur;

	s = demarrer(RC_DEUX_PROFILS);
	g_assert_cmpint(Check_configuration_file(s), ==, 0);
	chemin_macros(s, "/nulle-part/autre.ini");
	config_remember_macros_file(s);
	kf = charger_rc();
	valeur = g_key_file_get_string(kf, "profile:default", "macros_file", NULL);
	g_assert_cmpstr(valeur, ==, "/nulle-part/autre.ini");
	g_free(valeur);
	g_key_file_free(kf);
	/* Profil actif supprimé entre-temps : mémoire seulement, pas de profil fantôme réduit à macros_file */
	g_assert_true(app_config_delete_profile("default", NULL));
	chemin_macros(s, "/nulle-part/troisieme.ini");
	config_remember_macros_file(s);
	g_assert_cmpstr(gt_session_get_profile(s)->macros_file, ==, "/nulle-part/troisieme.ini");
	kf = charger_rc();
	g_assert_false(g_key_file_has_group(kf, "profile:default"));
	g_key_file_free(kf);
	g_assert_cmpuint(messages->len, ==, 0);
	g_object_unref(s);
}

/* Deux sessions, profils « a » et « b » : chacune retient son propre fichier de macros dans son propre profil */
static void
test_macros_file_par_session(void)
{
	GtSession *a, *b;
	GKeyFile *kf;
	gchar *valeur;

	a = demarrer("[profile:default]\nport=/dev/null\n\n[profile:a]\nport=/dev/null\n\n[profile:b]\nport=/dev/null\n");
	b = gt_session_new(NULL, NULL);
	g_assert_cmpint(Check_configuration_file(a), ==, 0);
	g_assert_true(app_config_load_profile(a, "a", NULL));
	g_assert_true(app_config_load_profile(b, "b", NULL));
	g_assert_cmpstr(gt_session_get_profile_name(a), ==, "a");
	g_assert_cmpstr(gt_session_get_profile_name(b), ==, "b");
	chemin_macros(a, "/nulle-part/a.ini");
	chemin_macros(b, "/nulle-part/b.ini");
	config_remember_macros_file(a);
	config_remember_macros_file(b);
	kf = charger_rc();
	valeur = g_key_file_get_string(kf, "profile:a", "macros_file", NULL);
	g_assert_cmpstr(valeur, ==, "/nulle-part/a.ini");
	g_free(valeur);
	valeur = g_key_file_get_string(kf, "profile:b", "macros_file", NULL);
	g_assert_cmpstr(valeur, ==, "/nulle-part/b.ini");
	g_free(valeur);
	g_assert_false(g_key_file_has_key(kf, "profile:default", "macros_file", NULL));
	g_key_file_free(kf);
	g_assert_cmpstr(gt_session_get_profile(a)->macros_file, ==, "/nulle-part/a.ini");
	g_assert_cmpstr(gt_session_get_profile(b)->macros_file, ==, "/nulle-part/b.ini");
	g_assert_cmpuint(messages->len, ==, 0);
	g_object_unref(a);
	g_object_unref(b);
}

/* Fichier illisible : défauts, un message, plus aucune écriture (ni état de fenêtre, ni profil, ni [general]) */
static void
test_lecture_seule(void)
{
	GtSession *s;
	GtWindowState st;
	GError *err = NULL;
	gchar *avant, *apres, *copie;

	s = demarrer("ceci n'est pas un fichier ini\n[profile:a]\nport=/dev/x\n");
	avant = lire_rc();
	g_assert_cmpint(Check_configuration_file(s), ==, -1);
	g_assert_cmpint(nb_messages('E'), ==, 1);
	g_assert_nonnull(strstr(dernier_message(), ".gtktermrc"));
	g_assert_cmpuint(gt_session_get_profile(s)->baudrate, ==, 115200);
	app_config_load_window_state(&st);
	st.width = 900;
	app_config_save_window_state(&st);
	g_assert_false(app_config_save_profile(s, "atelier", &err));
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
	g_object_unref(s);
}

/* M34 : « Skip this version » enregistré dans [general] et relu au démarrage suivant ; NULL l'efface */
static void
test_revision_ignoree(void)
{
	GtSession *s;
	GKeyFile *kf;
	gchar *valeur;

	s = demarrer(RC_DEUX_PROFILS);
	g_assert_cmpint(Check_configuration_file(s), ==, 0);
	g_assert_null(settings.update_skipped_revision);
	config_save_skipped_revision("0123abc");
	g_assert_cmpstr(settings.update_skipped_revision, ==, "0123abc");
	kf = charger_rc();
	valeur = g_key_file_get_string(kf, "general", "update_skipped_revision", NULL);
	g_assert_cmpstr(valeur, ==, "0123abc");
	g_free(valeur);
	g_key_file_free(kf);
	config_file_init();
	g_assert_cmpint(Check_configuration_file(s), ==, 0);
	g_assert_cmpstr(settings.update_skipped_revision, ==, "0123abc");
	config_save_skipped_revision(NULL);
	g_assert_null(settings.update_skipped_revision);
	config_file_init();
	g_assert_cmpint(Check_configuration_file(s), ==, 0);
	g_assert_null(settings.update_skipped_revision);
	g_assert_cmpuint(messages->len, ==, 0);
	g_object_unref(s);
}

/* Case « Check for updates at startup » (fenêtre « Updates ») : seule la clé [general] update_check_startup est écrite, après
 * relecture ; valeur inchangée : aucune écriture */
static void
test_verification_au_demarrage(void)
{
	GtSession *s;
	GKeyFile *kf;
	gchar *avant, *apres;

	s = demarrer(RC_DEUX_PROFILS);
	g_assert_cmpint(Check_configuration_file(s), ==, 0);
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
	g_assert_cmpint(Check_configuration_file(s), ==, 0);
	g_assert_false(settings.update_check_startup);
	g_assert_cmpuint(messages->len, ==, 0);
	g_free(avant);
	g_object_unref(s);
}

/* ---- État de la fenêtre ------------------------------------------------------ */

static void
test_etat_fenetre_defauts(void)
{
	GtSession *s;
	GtWindowState st;

	s = demarrer(NULL);
	app_config_load_window_state(&st);
	g_assert_cmpint(st.width, ==, 1000);
	g_assert_cmpint(st.height, ==, 600);
	g_assert_false(st.maximized);
	g_assert_cmpint(st.h_paned, ==, -1);
	g_assert_cmpint(st.v_paned, ==, -1);
	g_assert_true(st.macro_panel_visible);
	g_assert_false(st.script_panel_visible);
	g_assert_false(st.hex_bar_visible);
	g_object_unref(s);
}

static void
test_etat_fenetre_aller_retour(void)
{
	GtSession *s;
	GtWindowState st = { 1024, 700, TRUE, 300, 200, FALSE, TRUE, TRUE };
	GtWindowState lu;
	GKeyFile *kf;

	s = demarrer(RC_DEUX_PROFILS);
	g_assert_cmpint(Check_configuration_file(s), ==, 0);
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
	g_object_unref(s);
}

/* Spec §3.2-7 : x et y ne sont plus lus, et disparaissent au premier enregistrement ; les autres groupes restent */
static void
test_etat_fenetre_x_y_retires(void)
{
	GtSession *s;
	GtWindowState st;
	GKeyFile *kf;
	gchar *valeur;

	s = demarrer("[window]\nx=10\ny=20\nwidth=800\nheight=600\n\n[profile:default]\nport=/dev/null\n\n[colors]\nbg=#000000\n");
	g_assert_cmpint(Check_configuration_file(s), ==, 0);
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
	g_object_unref(s);
}

/* Spec §8 : tailles ≤ 0 ou > 10 000, positions négatives, valeurs illisibles : ignorées */
static void
test_etat_fenetre_aberrant(void)
{
	GtSession *s;
	GtWindowState st;

	s = demarrer("[window]\nwidth=-5\nheight=20000\npaned_position=-3\nv_paned_position=abc\nmaximized=peut-etre\nscript_panel_visible=yes\n");
	app_config_load_window_state(&st);
	g_assert_cmpint(st.width, ==, 1000);
	g_assert_cmpint(st.height, ==, 600);
	g_assert_cmpint(st.h_paned, ==, -1);
	g_assert_cmpint(st.v_paned, ==, -1);
	g_assert_false(st.maximized);
	g_assert_true(st.script_panel_visible);
	g_object_unref(s);
	s = demarrer("[window]\nwidth=900\nheight=0\n");
	app_config_load_window_state(&st);
	g_assert_cmpint(st.width, ==, 1000);
	g_assert_cmpint(st.height, ==, 600);
	g_object_unref(s);
}

static void
test_last_project(void)
{
	GtSession *s = demarrer(RC_DEUX_PROFILS);
	GKeyFile *kf;
	gchar *v;

	g_assert_null(app_config_get_last_project());
	app_config_set_last_project("/a/b.gtkterm");
	kf = charger_rc();
	v = g_key_file_get_string(kf, "general", "last_project", NULL);
	g_assert_cmpstr(v, ==, "/a/b.gtkterm");
	g_free(v);
	g_key_file_free(kf);
	v = app_config_get_last_project();
	g_assert_cmpstr(v, ==, "/a/b.gtkterm");
	g_free(v);
	app_config_set_last_project(NULL);
	kf = charger_rc();
	g_assert_false(g_key_file_has_key(kf, "general", "last_project", NULL));
	g_key_file_free(kf);
	g_assert_null(app_config_get_last_project());
	g_object_unref(s);
}

static void
test_profil_dans_projet(void)
{
	gchar *dossier = g_path_get_dirname(chemin_rc);
	gchar *m_ini = g_build_filename(dossier, "m.ini", NULL);
	gchar *rc = g_strdup_printf("[profile:default]\nport=/dev/null\nspeed=9600\n\n[profile:p]\nport=/dev/null\nspeed=57600\nmacros_file=%s\n", m_ini);
	GtSession *s;
	GtMacroSet *jeu;
	GKeyFile *kf;
	GError *err = NULL;
	gchar *v;

	/* Fichier de macros du profil existant, avec une macro */
	{
		GtSession *tmp = gt_session_new(NULL, NULL);

		gt_macro_set_append(gt_macros_get_set(gt_session_get_macros(tmp)), "du-fichier", "", "x", "");
		g_assert_cmpint(gt_macros_save_file(gt_session_get_macros(tmp), m_ini, FALSE, TRUE), ==, GT_MACROS_SAVED);
		g_object_unref(tmp);
	}
	s = demarrer(rc);
	gt_session_set_in_project(s, TRUE);
	jeu = gt_macro_set_new();
	gt_macro_set_append(jeu, "du-projet", "", "y", "");
	gt_macros_replace_set(gt_session_get_macros(s), jeu);
	g_assert_true(app_config_load_profile(s, "p", &err));
	g_assert_no_error(err);
	g_assert_cmpuint(gt_session_get_profile(s)->baudrate, ==, 57600);
	g_assert_cmpuint(gt_macro_set_count(gt_macros_get_set(gt_session_get_macros(s))), ==, 1);
	g_assert_cmpstr(gt_macro_set_get(gt_macros_get_set(gt_session_get_macros(s)), 0)->label, ==, "du-projet");
	g_assert_null(gt_macros_get_path(gt_session_get_macros(s)));
	config_remember_macros_file(s);
	kf = charger_rc();
	v = g_key_file_get_string(kf, "profile:p", "macros_file", NULL);
	g_assert_cmpstr(v, ==, m_ini);
	g_free(v);
	g_key_file_free(kf);
	g_assert_true(app_config_save_profile(s, "p", &err));
	g_assert_no_error(err);
	kf = charger_rc();
	v = g_key_file_get_string(kf, "profile:p", "macros_file", NULL);
	g_assert_cmpstr(v, ==, m_ini);
	g_free(v);
	g_key_file_free(kf);
	g_object_unref(s);
	g_free(rc);
	g_free(m_ini);
	g_free(dossier);
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
	g_test_add_func("/app_config/profil-cause-reelle", test_profil_cause_reelle);
	g_test_add_func("/app_config/verification", test_verification);
	g_test_add_func("/app_config/enregistrement", test_enregistrement);
	g_test_add_func("/app_config/enregistrement-nom-invalide", test_enregistrement_nom_invalide);
	g_test_add_func("/app_config/ecrasement-sans-doublon", test_ecrasement_sans_doublon);
	g_test_add_func("/app_config/suppression", test_suppression);
	g_test_add_func("/app_config/relecture-autre-instance", test_relecture_autre_instance);
	g_test_add_func("/app_config/fichier-de-macros-retenu", test_fichier_de_macros_retenu);
	g_test_add_func("/app_config/macros-file-par-session", test_macros_file_par_session);
	g_test_add_func("/app_config/lecture-seule", test_lecture_seule);
	g_test_add_func("/app_config/revision-ignoree", test_revision_ignoree);
	g_test_add_func("/app_config/verification-au-demarrage", test_verification_au_demarrage);
	g_test_add_func("/app_config/etat-fenetre-defauts", test_etat_fenetre_defauts);
	g_test_add_func("/app_config/etat-fenetre-aller-retour", test_etat_fenetre_aller_retour);
	g_test_add_func("/app_config/etat-fenetre-x-y-retires", test_etat_fenetre_x_y_retires);
	g_test_add_func("/app_config/etat-fenetre-aberrant", test_etat_fenetre_aberrant);
	g_test_add_func("/app_config/last-project", test_last_project);
	g_test_add_func("/app_config/profil-dans-projet", test_profil_dans_projet);
	ret = g_test_run();

	g_ptr_array_unref(messages);
	g_free(chemin_rc);
	test_rmdir_recursive(racine);
	g_free(racine);
	return ret;
}
