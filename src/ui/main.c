/* Point d'entrée de l'interface GTK 4 (gtkterm) : ordre de démarrage de la spec §4.4. */
#include <config.h>
#include <glib/gi18n.h>
#include <signal.h>

#include "app.h"
#include "app_config.h"
#include "cmdline.h"
#include "dialogs.h"
#include "script_engine.h"
#include "session.h"
#include "session_manager.h"

/* Chaque session arrêtée (sans effet si déjà fait), puis rendue */
static void free_sessions(GtSessionManager *sessions)
{
	for(guint i = 0; i < gt_session_manager_get_n(sessions); i++)
		gt_session_shutdown(gt_session_manager_get_nth(sessions, i));
	g_object_unref(sessions);
}

int main(int argc, char *argv[])
{
	GtCmdline cmd = { 0 };
	GtSessionManager *sessions;
	GtSession *first;
	GtkApplication *app;
	int status;

	/* Une écriture vers un pair TCP fermé (port principal ou port Lua) ne doit jamais tuer l'application (C2) */
	signal(SIGPIPE, SIG_IGN);
	config_file_init();
	bindtextdomain(PACKAGE, LOCALEDIR);
	bind_textdomain_codeset(PACKAGE, "UTF-8");
	textdomain(PACKAGE);

	/* Crochets installés avant toute lecture de configuration : un message émis pendant la lecture attend la fenêtre principale */
	gt_ui_install_app_hooks();
	script_engine_setup();

	/* Première session (liaison, options, macros, moteur Lua) avant la ligne de commande : le profil chargé charge son fichier de macros */
	sessions = gt_session_manager_new();
	first = gt_session_new(NULL, NULL);
	gt_session_manager_add(sessions, first, -1);
	g_object_unref(first);	/* le gestionnaire la tient */

	/* Profil, options, validation ; --help (0) et option inconnue (1) sortent ici, avant toute fenêtre */
	read_command_line(first, argc, argv, &cmd);
	if(cmd.exit_now)
	{
		gt_ui_messages_shutdown();	/* messages déjà en file : sur stderr */
		free_sessions(sessions);
		status = cmd.exit_code;
		gt_cmdline_clear(&cmd);
		return status;
	}

	app = gt_app_new(&cmd, sessions);
	status = g_application_run(G_APPLICATION(app), 1, argv);	/* les options sont déjà lues : seul argv[0] */
	/* Sans effet après un arrêt normal (déjà fait) ; si run rend la main sans startup ni shutdown (échec d'enregistrement), les messages
	 * encore en file partent sur stderr au lieu d'être perdus */
	gt_ui_messages_shutdown();
	g_object_unref(app);	/* rend aussi la référence de l'application sur le gestionnaire */

	free_sessions(sessions);	/* sans effet sur une session déjà arrêtée par « close-request » ou « shutdown » */
	gt_cmdline_clear(&cmd);
	return status;
}
