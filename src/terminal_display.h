/***********************************************************************/
/* terminal_display.h                                                  */
/* ------------------                                                  */
/*           GTKTerm Software                                          */
/*                                                                     */
/*   Purpose                                                           */
/*      Alimentation du terminal VTE : rendu des vues depuis la        */
/*      liaison et son historique (sous-projet 2)                      */
/*                                                                     */
/***********************************************************************/

#ifndef TERMINAL_DISPLAY_H_
#define TERMINAL_DISPLAY_H_

#include <gtk/gtk.h>
#include <vte/vte.h>

#include "link.h"
#include "view_render.h"

/* Vide la vue et la redessine depuis l'historique avec les options courantes (vue, écho, CR/LF, horodatage, hexadécimal) */
void     terminal_display_redraw (void);
void     terminal_display_set_mode (GtViewMode mode);
void     set_hex_bytes_per_line (gint bpl);
void     set_hex_show_index (gboolean show);
void     clear_display (void);

/* Gestionnaires des signaux de la liaison */
void     terminal_display_on_received (GtkTermLink *link, GBytes *data, gint64 wall_time_us, gpointer user_data);
void     terminal_display_on_sent (GtkTermLink *link, GBytes *data, gint origin, gpointer user_data);
void     terminal_display_on_history_cleared (GtkTermLink *link, gpointer user_data);

void     Got_Input (VteTerminal *widget, gchar *text, guint length, gpointer ptr);

#endif
