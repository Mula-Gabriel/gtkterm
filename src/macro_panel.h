/***********************************************************************/
/* macro_panel.h                                                       */
/* ------------                                                        */
/*           GTKTerm Software                                          */
/*                                                                     */
/*   Purpose                                                           */
/*      Sidebar macro button panel (view of the macro set)            */
/*                                                                     */
/***********************************************************************/

#ifndef MACRO_PANEL_H_
#define MACRO_PANEL_H_

#include <gtk/gtk.h>

extern GtkWidget *macro_panel;
extern GtkWidget *macro_notebook;

void create_macro_panel     (void);
void rebuild_macro_buttons  (void);
void macro_panel_polling_changed(guint id, gboolean running);	/* rappel d'état de l'ordonnanceur ; identifiant inconnu ou sans bouton toléré */

#endif
