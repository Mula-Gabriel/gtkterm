/* Point d'entrée de l'interface GTK 4 (gtkterm) : ordre de démarrage de la spec §4.4. */
#include <config.h>
#include <glib/gi18n.h>
#include <signal.h>

#include "app.h"
#include "app_config.h"
#include "cmdline.h"
#include "dialogs.h"
#include "link.h"
#include "macros.h"

int main(int argc, char *argv[])
{
	GtCmdline cmd = { 0 };
	GtkTermLink *link;
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

	link = gt_link_new();
	gt_link_set_default(link);
	gt_profile_init_defaults(&config);
	macros_init();	/* avant la ligne de commande : le profil chargé charge son fichier de macros */

	/* Profil, options, validation ; --help (0) et option inconnue (1) sortent ici, avant toute fenêtre */
	read_command_line(argc, argv, &cmd);
	if(cmd.exit_now)
	{
		gt_ui_messages_shutdown();	/* messages déjà en file : sur stderr */
		macros_shutdown();
		gt_link_set_default(NULL);
		g_object_unref(link);
		status = cmd.exit_code;
		gt_cmdline_clear(&cmd);
		return status;
	}

	app = gt_app_new(&cmd);
	status = g_application_run(G_APPLICATION(app), 1, argv);	/* les options sont déjà lues : seul argv[0] */
	/* Sans effet après un arrêt normal (déjà fait) ; si run rend la main sans startup ni shutdown (échec d'enregistrement), les messages
	 * encore en file partent sur stderr au lieu d'être perdus */
	gt_ui_messages_shutdown();
	g_object_unref(app);

	gt_link_close(link);	/* sans effet si « close-request » ou « shutdown » l'a déjà fermée */
	gt_link_set_default(NULL);	/* aucun pointeur vers la liaison libérée */
	g_object_unref(link);
	gt_cmdline_clear(&cmd);
	return status;
}
