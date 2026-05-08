/***********************************************************************/
/* terminal_display.h                                                  */
/* ------------------                                                  */
/*           GTKTerm Software                                          */
/*                                                                     */
/*   Purpose                                                           */
/*      Terminal text/hex display rendering and serial send           */
/*                                                                     */
/***********************************************************************/

#ifndef TERMINAL_DISPLAY_H_
#define TERMINAL_DISPLAY_H_

#include <gtk/gtk.h>

void     initialize_hexadecimal_display (void);
void     put_hexadecimal     (const gchar *string, guint size);
void     put_text            (const gchar *string, guint size);
gint     send_serial         (gchar *string, gint len);
void     clear_display       (void);
gboolean Envoie_car          (GtkWidget *widget, GdkEventKey *event,
                              gpointer pointer);

void     set_hex_bytes_per_line (gint bpl);
void     set_hex_show_index    (gboolean show);

void     Got_Input (VteTerminal *widget, gchar *text, guint length,
                     gpointer ptr);

extern guint virt_col_pos;

#endif
