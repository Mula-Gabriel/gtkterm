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

#include "interface.h"
#include "update.h"
#include "serial.h"
#include "term_config.h"
#include "cmdline.h"
#include "parsecfg.h"
#include "buffer.h"
#include "macros.h"
#include "auto_config.h"
#include "device_monitor.h"
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
	gchar *message;

	config_file_init();
	bindtextdomain(PACKAGE, LOCALEDIR);
	bind_textdomain_codeset(PACKAGE, "UTF-8");
	textdomain(PACKAGE);

	gtk_init(&argc, &argv);

	create_buffer();

	create_main_window();

	if(read_command_line(argc, argv) < 0)
	{
		delete_buffer();
		exit(1);
	}

	Config_port();
	ConfigFlags();
	script_panel_refresh_colors();

	message = get_port_string();
	Set_window_title(message);
	Set_status_message(message);
	g_free(message);

	add_shortcuts();

	set_view(ASCII_VIEW);

	device_monitor_start();

	user_signals_catch();

	g_idle_add(startup_update_check, NULL);

	gtk_main();

	delete_buffer();

	Close_port();

	return 0;
}
