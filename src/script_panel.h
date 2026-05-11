#ifndef SCRIPT_PANEL_H_
#define SCRIPT_PANEL_H_

#include <gtk/gtk.h>

extern GtkWidget *script_panel;

void create_script_panel(void);
void script_panel_refresh_colors(void);
void script_panel_save_state(void);

#endif
