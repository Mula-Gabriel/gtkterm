/* Tests de cmdline.c (couche application, sans GTK) : analyse pure (gt_cmdline_parse, gt_cmdline_apply) et read_command_line
 * (--help et option invalide avant toute configuration, -c puis options, -f). HOME et XDG privés : jamais le vrai ~/.config.
 * Vraie couche application (gtkterm_app) : le profil lu est celui d'une session de test (une par cas). */

#include <glib.h>
#include <glib/gstdio.h>
#include <stdarg.h>
#include <string.h>

#include "app_config.h"
#include "app_ui.h"
#include "cmdline.h"
#include "session.h"
#include "test_util.h"

static gchar *chemin_rc;
static gint nb_messages;

static void
enr_message(GtAppMessageKind kind, const gchar *text)
{
	nb_messages++;
}

static const GtAppUi table_enregistrement = { .message = enr_message };

/* argv de « gtkterm » suivi des arguments (NULL final) ; tableau modifiable, getopt_long permute ses pointeurs. g_strfreev. */
static gchar **
arguments(const gchar *premier, ...)
{
	GPtrArray *a = g_ptr_array_new();
	va_list ap;

	g_ptr_array_add(a, g_strdup("gtkterm"));
	va_start(ap, premier);
	for(const gchar *s = premier; s != NULL; s = va_arg(ap, const gchar *))
		g_ptr_array_add(a, g_strdup(s));
	va_end(ap);
	g_ptr_array_add(a, NULL);
	return (gchar **)g_ptr_array_free(a, FALSE);
}

/* Fichier de configuration (NULL : absent), magasin relu, session neuve (profil par défaut, à libérer) : comme main() avant read_command_line */
static GtSession *
demarrer(const gchar *contenu)
{
	if(contenu == NULL)
		g_unlink(chemin_rc);
	else
		g_assert_true(g_file_set_contents(chemin_rc, contenu, -1, NULL));
	gt_app_ui_set(&table_enregistrement);
	nb_messages = 0;
	config_file_init();
	return gt_session_new(NULL, NULL);
}

static void
analyser(gchar **av, GtCmdlineOptions *o)
{
	GError *err = NULL;

	g_assert_true(gt_cmdline_parse((int)g_strv_length(av), av, o, &err));
	g_assert_no_error(err);
}

static void
refuser(gchar **av, GtCmdlineError code)
{
	GtCmdlineOptions o;
	GError *err = NULL;

	g_assert_false(gt_cmdline_parse((int)g_strv_length(av), av, &o, &err));
	g_assert_error(err, GT_CMDLINE_ERROR, (gint)code);
	g_assert_cmpstr(err->message, !=, "");
	g_clear_error(&err);
	gt_cmdline_options_clear(&o);
	g_strfreev(av);
}

/* ---- Analyse pure ------------------------------------------------------------ */

static void
test_analyse_valeurs(void)
{
	gchar **av = arguments("-p", "/dev/pty-test", "-s", "9600", "-b", "7", "-t", "2", "-a", "even", "-w", "Xon", "-d", "10", "-r", "A",
	                       "-e", "-L", "-x", "5", "-y", "6", "-T", "tcp-client", "-H", "hote", "-P", "2000", "-f", "/tmp/envoi.bin",
	                       "-c", "atelier", NULL);
	GtCmdlineOptions o;
	GtProfile p;

	analyser(av, &o);
	g_assert_false(o.help);
	g_assert_cmpstr(o.config_name, ==, "atelier");
	g_assert_cmpstr(o.send_file, ==, "/tmp/envoi.bin");
	gt_profile_init_defaults(&p);
	gt_cmdline_apply(&o, &p);
	g_assert_cmpstr(p.port, ==, "/dev/pty-test");
	g_assert_cmpuint(p.baudrate, ==, 9600);
	g_assert_cmpint(p.bits, ==, 7);
	g_assert_cmpint(p.stopbits, ==, 2);
	g_assert_cmpint(p.parity, ==, GT_PARITY_EVEN);
	g_assert_cmpint(p.flow, ==, GT_FLOW_XONXOFF);
	g_assert_cmpint(p.wait_delay_ms, ==, 10);
	g_assert_cmpint(p.wait_char, ==, 'A');
	g_assert_true(p.echo);
	g_assert_false(p.lock_port);
	g_assert_cmpint(p.rs485_before_ms, ==, 5);
	g_assert_cmpint(p.rs485_after_ms, ==, 6);
	g_assert_cmpint(p.transport, ==, TRANSPORT_TCP_CLIENT);
	g_assert_cmpstr(p.host, ==, "hote");
	g_assert_cmpstr(p.tcp_port, ==, "2000");
	gt_profile_clear(&p);
	gt_cmdline_options_clear(&o);
	g_assert_null(o.config_name);
	g_assert_null(o.values.port);
	g_strfreev(av);
}

/* Sans option : le profil n'est pas touché */
static void
test_analyse_sans_option(void)
{
	gchar **av = arguments(NULL);
	GtCmdlineOptions o;
	GtProfile p;

	analyser(av, &o);
	g_assert_cmpuint(o.set, ==, 0);
	g_assert_null(o.config_name);
	g_assert_null(o.send_file);
	gt_profile_init_defaults(&p);
	p.parity = GT_PARITY_ODD;
	p.baudrate = 4800;
	gt_cmdline_apply(&o, &p);
	g_assert_cmpint(p.parity, ==, GT_PARITY_ODD);
	g_assert_cmpuint(p.baudrate, ==, 4800);
	g_assert_true(p.lock_port);
	gt_profile_clear(&p);
	gt_cmdline_options_clear(&o);
	g_strfreev(av);
}

/* Spec annexe A : -T serial, -a none, -w none étaient ignorés (pas de retour depuis un profil TCP, à parité ou à contrôle de flux) */
static void
test_analyse_none_et_serie(void)
{
	gchar **av = arguments("-a", "none", "-w", "none", "-T", "serial", NULL);
	gchar **longues = arguments("--parity=odd", "--flow", "RS485", "--transport", "tcp-server", "--sp", "1200", NULL);
	GtCmdlineOptions o;
	GtProfile p;

	analyser(av, &o);
	gt_profile_init_defaults(&p);
	p.parity = GT_PARITY_ODD;
	p.flow = GT_FLOW_RTSCTS;
	p.transport = TRANSPORT_TCP_CLIENT;
	gt_cmdline_apply(&o, &p);
	g_assert_cmpint(p.parity, ==, GT_PARITY_NONE);
	g_assert_cmpint(p.flow, ==, GT_FLOW_NONE);
	g_assert_cmpint(p.transport, ==, TRANSPORT_SERIAL);
	gt_cmdline_options_clear(&o);
	analyser(longues, &o);	/* deuxième analyse dans le même processus : getopt réinitialisé */
	gt_cmdline_apply(&o, &p);
	g_assert_cmpint(p.parity, ==, GT_PARITY_ODD);
	g_assert_cmpint(p.flow, ==, GT_FLOW_RS485);
	g_assert_cmpint(p.transport, ==, TRANSPORT_TCP_SERVER);
	g_assert_cmpuint(p.baudrate, ==, 1200);	/* abréviation sans ambiguïté d'une option longue */
	gt_cmdline_options_clear(&o);
	gt_profile_clear(&p);
	g_strfreev(av);
	g_strfreev(longues);
}

/* --help arrête l'analyse : une option invalide qui le suit n'est pas lue */
static void
test_analyse_aide(void)
{
	gchar **av = arguments("-s", "9600", "--help", "-Z", "-s", "abc", NULL);
	GtCmdlineOptions o;

	analyser(av, &o);
	g_assert_true(o.help);
	gt_cmdline_options_clear(&o);
	g_strfreev(av);
}

/* atoi remplacé : texte, reste, signe, hors bornes refusés (avant : « abc » donnait 0) */
static void
test_valeurs_invalides(void)
{
	static const gchar * const invalides[][2] = {
		{ "-s", "abc" }, { "-s", "" }, { "-s", "0" }, { "-s", "9600x" }, { "-s", "-1" }, { "-s", " 9600" }, { "--speed", "99999999999" },
		{ "-b", "4" }, { "-b", "9" }, { "-t", "0" }, { "-t", "3" }, { "-d", "-1" }, { "-d", "501" }, { "-x", "abc" }, { "-y", "501" },
		{ "-a", "mark" }, { "-w", "cts" }, { "-T", "udp" }, { "-r", "" }, { "-r", "ab" }, { "-p", "" },
	};

	for(gsize i = 0; i < G_N_ELEMENTS(invalides); i++)
		refuser(arguments(invalides[i][0], invalides[i][1], NULL), GT_CMDLINE_ERROR_INVALID_VALUE);
}

static void
test_options_inconnues(void)
{
	refuser(arguments("--nimporte", NULL), GT_CMDLINE_ERROR_UNKNOWN_OPTION);
	refuser(arguments("-Z", NULL), GT_CMDLINE_ERROR_UNKNOWN_OPTION);
	refuser(arguments("--echo=1", NULL), GT_CMDLINE_ERROR_UNKNOWN_OPTION);
	refuser(arguments("--speed", NULL), GT_CMDLINE_ERROR_MISSING_VALUE);
	refuser(arguments("-e", "-s", NULL), GT_CMDLINE_ERROR_MISSING_VALUE);
}

/* ---- read_command_line ---------------------------------------------------------- */

/* --help : code 0, aide sur la sortie standard, avant toute lecture de la configuration (aucun fichier créé, aucun message) */
static void
test_aide_sans_configuration(void)
{
	if(g_test_subprocess())
	{
		GtSession *s;
		gchar **av = arguments("--help", NULL);
		GtCmdline cmd;

		s = demarrer(NULL);
		read_command_line(s, (int)g_strv_length(av), av, &cmd);
		g_assert_true(cmd.exit_now);
		g_assert_cmpint(cmd.exit_code, ==, 0);
		g_assert_null(cmd.send_file);
		g_assert_false(g_file_test(chemin_rc, G_FILE_TEST_EXISTS));
		g_assert_cmpint(nb_messages, ==, 0);
		gt_cmdline_clear(&cmd);
		g_object_unref(s);
		g_strfreev(av);
		return;
	}
	g_test_trap_subprocess(NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
	g_test_trap_assert_passed();
	g_test_trap_assert_stdout("*--help or -h*");
}

/* Option inconnue ou valeur invalide : code 1, message sur stderr, avant toute lecture de la configuration */
static void
test_option_invalide_code_1(void)
{
	if(g_test_subprocess())
	{
		GtSession *s;
		gchar **av = arguments("-e", "--nimporte", NULL);
		gchar **av2 = arguments("-s", "abc", NULL);
		GtCmdline cmd;

		s = demarrer(NULL);
		read_command_line(s, (int)g_strv_length(av), av, &cmd);
		g_assert_true(cmd.exit_now);
		g_assert_cmpint(cmd.exit_code, ==, 1);
		gt_cmdline_clear(&cmd);
		read_command_line(s, (int)g_strv_length(av2), av2, &cmd);
		g_assert_true(cmd.exit_now);
		g_assert_cmpint(cmd.exit_code, ==, 1);
		g_assert_false(g_file_test(chemin_rc, G_FILE_TEST_EXISTS));
		g_assert_cmpint(nb_messages, ==, 0);
		gt_cmdline_clear(&cmd);
		g_object_unref(s);
		g_strfreev(av);
		g_strfreev(av2);
		return;
	}
	g_test_trap_subprocess(NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
	g_test_trap_assert_passed();
	g_test_trap_assert_stderr("*--nimporte*--speed*abc*");
}

/* -c puis les autres options, quel que soit leur ordre ; -f rendu à l'appelant (plus d'écriture de fic_defaut) */
static void
test_profil_et_fichier(void)
{
	GtSession *s;
	gchar **av = arguments("-s", "4800", "-c", "atelier", "-f", "/tmp/envoi.bin", NULL);
	GtCmdline cmd;

	s = demarrer("[profile:default]\nport=/dev/null\nspeed=9600\n\n[profile:atelier]\nport=/dev/null\nspeed=19200\nbits=7\n");
	read_command_line(s, (int)g_strv_length(av), av, &cmd);
	g_assert_false(cmd.exit_now);
	g_assert_cmpint(cmd.exit_code, ==, 0);
	g_assert_cmpstr(cmd.send_file, ==, "/tmp/envoi.bin");
	g_assert_cmpuint(gt_session_get_profile(s)->baudrate, ==, 4800);
	g_assert_cmpint(gt_session_get_profile(s)->bits, ==, 7);
	g_assert_cmpstr(gt_session_get_profile_name(s), ==, "atelier");
	g_assert_cmpint(nb_messages, ==, 0);
	gt_cmdline_clear(&cmd);
	g_assert_null(cmd.send_file);
	g_strfreev(av);
	g_object_unref(s);
}

static void
test_retour_au_serie(void)
{
	GtSession *s;
	gchar **av = arguments("-T", "serial", "-a", "none", "-w", "none", NULL);
	GtCmdline cmd;

	s = demarrer("[profile:default]\ntransport=tcp-client\nsocket_host=localhost\nsocket_port=2323\nport=/dev/null\nparity=odd\nflow=rts\n");
	read_command_line(s, (int)g_strv_length(av), av, &cmd);
	g_assert_false(cmd.exit_now);
	g_assert_cmpint(gt_session_get_profile(s)->transport, ==, TRANSPORT_SERIAL);
	g_assert_cmpint(gt_session_get_profile(s)->parity, ==, GT_PARITY_NONE);
	g_assert_cmpint(gt_session_get_profile(s)->flow, ==, GT_FLOW_NONE);
	g_assert_cmpstr(gt_session_get_profile_name(s), ==, "default");
	g_assert_cmpint(nb_messages, ==, 0);
	gt_cmdline_clear(&cmd);
	g_strfreev(av);
	g_object_unref(s);
}

/* Profil -c inconnu : message, démarrage sur « default » ; débit non standard : avertissement de la validation, valeur gardée */
static void
test_profil_inconnu_et_validation(void)
{
	GtSession *s;
	gchar **av = arguments("-c", "inconnu", "-s", "12345", NULL);
	GtCmdline cmd;

	s = demarrer("[profile:default]\nport=/dev/null\nspeed=9600\n");
	read_command_line(s, (int)g_strv_length(av), av, &cmd);
	g_assert_false(cmd.exit_now);
	g_assert_cmpint(nb_messages, ==, 2);
	g_assert_cmpuint(gt_session_get_profile(s)->baudrate, ==, 12345);
	g_assert_cmpstr(gt_session_get_profile_name(s), ==, "default");
	gt_cmdline_clear(&cmd);
	g_strfreev(av);
	g_object_unref(s);
}

static void
test_project(void)
{
	gchar **av = arguments("--project", "rel.gtkterm", NULL);
	GtCmdlineOptions o;
	GtCmdline cmd;
	GtSession *s;
	gchar *attendu = g_canonicalize_filename("rel.gtkterm", NULL);

	analyser(av, &o);
	g_assert_cmpstr(o.project, ==, "rel.gtkterm");
	gt_cmdline_options_clear(&o);
	s = demarrer(NULL);
	read_command_line(s, (int)g_strv_length(av), av, &cmd);
	g_assert_false(cmd.exit_now);
	g_assert_cmpstr(cmd.project, ==, attendu);
	g_assert_false(cmd.profile_given);
	gt_cmdline_clear(&cmd);
	g_assert_null(cmd.project);
	g_free(attendu);
	g_strfreev(av);
	g_object_unref(s);
}

static void
refuser_projet(gchar **av)
{
	GtCmdlineOptions o;
	GError *err = NULL;

	g_assert_false(gt_cmdline_parse((int)g_strv_length(av), av, &o, &err));
	g_assert_error(err, GT_CMDLINE_ERROR, GT_CMDLINE_ERROR_INVALID_VALUE);
	g_assert_nonnull(strstr(err->message, "--project"));
	g_clear_error(&err);
	gt_cmdline_options_clear(&o);
	g_strfreev(av);
}

static void
test_project_refuse(void)
{
	refuser_projet(arguments("--project", "x", "-p", "/dev/pts/1", NULL));
	refuser_projet(arguments("--project", "x", "-c", "a", NULL));
}

static void
test_profile_given(void)
{
	const struct { const gchar *a; const gchar *b; gboolean attendu; } cas[] = { { "-s", "9600", TRUE }, { "-L", NULL, FALSE }, { "-c", "a", TRUE } };

	for(guint i = 0; i < G_N_ELEMENTS(cas); i++)
	{
		gchar **av = arguments(cas[i].a, cas[i].b, NULL);
		GtCmdline cmd;
		GtSession *s = demarrer("[profile:default]\nport=/dev/null\n\n[profile:a]\nport=/dev/null\n");

		read_command_line(s, (int)g_strv_length(av), av, &cmd);
		g_assert_false(cmd.exit_now);
		g_assert_cmpint(cmd.profile_given, ==, cas[i].attendu);
		gt_cmdline_clear(&cmd);
		g_strfreev(av);
		g_object_unref(s);
	}
}

int
main(int argc, char **argv)
{
	gchar *racine = test_private_dirs();
	gint ret;

	g_test_init(&argc, &argv, NULL);
	/* Garde-fou : jamais le vrai ~/.config */
	g_assert_true(g_str_has_prefix(g_get_user_config_dir(), racine));
	chemin_rc = g_build_filename(g_get_user_config_dir(), ".gtktermrc", NULL);

	g_test_add_func("/cmdline/analyse-valeurs", test_analyse_valeurs);
	g_test_add_func("/cmdline/analyse-sans-option", test_analyse_sans_option);
	g_test_add_func("/cmdline/analyse-none-et-serie", test_analyse_none_et_serie);
	g_test_add_func("/cmdline/analyse-aide", test_analyse_aide);
	g_test_add_func("/cmdline/valeurs-invalides", test_valeurs_invalides);
	g_test_add_func("/cmdline/options-inconnues", test_options_inconnues);
	g_test_add_func("/cmdline/aide-sans-configuration", test_aide_sans_configuration);
	g_test_add_func("/cmdline/option-invalide-code-1", test_option_invalide_code_1);
	g_test_add_func("/cmdline/profil-et-fichier", test_profil_et_fichier);
	g_test_add_func("/cmdline/retour-au-serie", test_retour_au_serie);
	g_test_add_func("/cmdline/profil-inconnu-et-validation", test_profil_inconnu_et_validation);
	g_test_add_func("/cmdline/project", test_project);
	g_test_add_func("/cmdline/project-refuse", test_project_refuse);
	g_test_add_func("/cmdline/profile-given", test_profile_given);
	ret = g_test_run();

	g_free(chemin_rc);
	test_rmdir_recursive(racine);
	g_free(racine);
	return ret;
}
