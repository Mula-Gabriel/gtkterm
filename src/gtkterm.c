/***********************************************************************/
/* gtkterm.c                                                           */
/* ---------                                                           */
/*           GTKTerm Software                                          */
/*                      (c) Julien Schmitt                             */
/*                                                                     */
/* ------------------------------------------------------------------- */
/*                                                                     */
/*   Purpose                                                           */
/*      Main program file                                              */
/*                                                                     */
/*   ChangeLog                                                         */
/*      - 0.99.2 : Internationalization                                */
/*      - 0.99.0 : added call to add_shortcuts()                       */
/*      - 0.98 : all GUI functions moved to widgets.c                  */
/*                                                                     */
/***********************************************************************/

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <stdlib.h>
#include <signal.h>

#include "interface.h"
#include "update.h"
#include "term_config.h"
#include "cmdline.h"
#include "parsecfg.h"
#include "link.h"
#include "macros.h"
#include "user_signals.h"
#include "script_panel.h"

#include <config.h>
#include <glib/gi18n.h>

extern GtkWidget *Fenetre;

static gboolean startup_update_check(gpointer data)
{
	(void)data;
	update_check(GTK_WINDOW(Fenetre), FALSE);
	return G_SOURCE_REMOVE;
}

int main(int argc, char *argv[])
{
	GtkTermLink *link;

	/* Une écriture vers un pair TCP fermé (port principal ou port Lua) ne doit jamais tuer l'application (C2). */
	signal(SIGPIPE, SIG_IGN);
	config_file_init();
	bindtextdomain(PACKAGE, LOCALEDIR);
	bind_textdomain_codeset(PACKAGE, "UTF-8");
	textdomain(PACKAGE);

	gtk_init(&argc, &argv);

	link = gt_link_new();
	gt_link_set_default(link);

	create_main_window();
	interface_connect_link(link);

	if(read_command_line(argc, argv) < 0)
	{
		g_object_unref(link);
		exit(1);
	}

	ConfigFlags();
	script_panel_refresh_colors();

	add_shortcuts();

	set_view(ASCII_VIEW);

	/* Une seule ouverture au démarrage, après la configuration et la ligne de commande (M4) ; échec : une boîte de dialogue */
	interface_open_port();

	user_signals_catch();

	g_idle_add(startup_update_check, NULL);

	gtk_main();

	/* La fenêtre peut être détruite (bouton « Fermer » de la barre de titre) : plus aucun gestionnaire de l'interface sur la liaison */
	interface_disconnect_link(link);
	gt_link_close(link);
	g_object_unref(link);

	return 0;
}
