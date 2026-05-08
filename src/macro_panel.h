/***********************************************************************/
/* macro_panel.h                                                       */
/* ------------                                                        */
/*           GTKTerm Software                                          */
/*                                                                     */
/*   Purpose                                                           */
/*      Sidebar macro button panel, polling system, entry validation */
/*                                                                     */
/***********************************************************************/

#ifndef MACRO_PANEL_H_
#define MACRO_PANEL_H_

#include <gtk/gtk.h>

extern GtkWidget *macro_panel;
extern GtkWidget *macro_notebook;

void create_macro_panel     (void);
void rebuild_macro_buttons  (void);

#endif
