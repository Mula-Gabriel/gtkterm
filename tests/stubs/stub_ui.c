/* Bouchons de l'interface : configuration globale et fonctions d'affichage, sans fenêtre. */

#include <gtk/gtk.h>

#include "interface.h"
#include "term_config.h"
#include "files.h"
#include "macro_panel.h"
#include "stubs.h"

GtProfile config;
GtkTermSettings settings;
GtkAccelGroup *shortcuts = NULL;

void
show_message(gchar *message, gint type_msg)
{
	stub_record("show_message", message);
}

void
Set_status_message(gchar *msg)
{
	stub_record("Set_status_message", msg);
}

void
Set_window_title(gchar *msg)
{
	stub_record("Set_window_title", msg);
}

void
Set_local_echo(gboolean echo)
{
	stub_record("Set_local_echo", echo ? "1" : "0");
}

void
Put_temp_message(const gchar *msg, gint duree)
{
	stub_record("Put_temp_message", msg);
}

void
rebuild_macro_buttons(void)
{
	stub_record("rebuild_macro_buttons", NULL);
}

void
macro_panel_polling_changed(guint id, gboolean running)
{
	stub_record("macro_panel_polling_changed", running ? "1" : "0");
}

void
config_remember_macros_file(void)
{
	stub_record("config_remember_macros_file", NULL);
}
