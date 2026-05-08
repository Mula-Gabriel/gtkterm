/***********************************************************************/
/* terminal_display.c                                                  */
/* ------------------                                                  */
/*           GTKTerm Software                                          */
/*                                                                     */
/*   Purpose                                                           */
/*      Terminal text/hex display rendering and serial send           */
/*                                                                     */
/***********************************************************************/

#include <string.h>
#include <stdio.h>
#include <vte/vte.h>

#include "buffer.h"
#include "logging.h"
#include "serial.h"
#include "terminal_display.h"

#include <glib/gi18n.h>

/* Display globals (defined in interface.c) */
extern GtkWidget *display;
extern gboolean echo_on;
extern gboolean crlfauto_on;
extern gboolean esc_clear_screen_on;

/* --- Hexadecimal display state --- */
static gint     bytes_per_line = 16;
static gchar    blank_data[128];
static guint    total_bytes;
static gboolean show_index = FALSE;
guint           virt_col_pos = 0;

/* ─── Hexadecimal display ──────────────────────────────────────────── */

void
initialize_hexadecimal_display (void)
{
  total_bytes = 0;
  memset (blank_data, ' ', 128);
  blank_data[bytes_per_line * 3 + 5] = 0;
}

void
put_hexadecimal (const gchar *string, guint size)
{
  static gchar data[128];
  static gchar data_byte[16];
  gint i = 0;

  if (size == 0)
    return;

  while (i < size)
    {
      while (gtk_events_pending ())
        gtk_main_iteration ();

      data[0] = 0;

      while (virt_col_pos < bytes_per_line && i < size)
        {
          gint avance = 0;
          gchar ascii[1];

          if (show_index)
            {
              if (virt_col_pos == 0)
                {
                  sprintf (data, "%6d: ", total_bytes);
                  vte_terminal_feed (VTE_TERMINAL (display), data, strlen (data));
                }
            }

          sprintf (data_byte, "%02X ", (guchar) string[i]);
          log_chars (data_byte, 3);
          vte_terminal_feed (VTE_TERMINAL (display), data_byte, 3);

          avance = (bytes_per_line - virt_col_pos) * 3 + virt_col_pos + 2;
          sprintf (data_byte, "%c[%dC", 27, avance);
          vte_terminal_feed (VTE_TERMINAL (display), data_byte, strlen (data_byte));

          ascii[0] = (string[i] > 0x1F) ? string[i] : '.';
          vte_terminal_feed (VTE_TERMINAL (display), ascii, 1);

          sprintf (data_byte, "%c[%dD", 27, avance + 1);
          vte_terminal_feed (VTE_TERMINAL (display), data_byte, strlen (data_byte));

          if (virt_col_pos == bytes_per_line / 2 - 1)
            vte_terminal_feed (VTE_TERMINAL (display), "- ", strlen ("- "));

          virt_col_pos++;
          i++;

          if (virt_col_pos == bytes_per_line)
            {
              vte_terminal_feed (VTE_TERMINAL (display), "\r\n", 2);
              total_bytes += virt_col_pos;
              virt_col_pos = 0;
            }
        }
    }
}

void
put_text (const gchar *string, guint size)
{
  log_chars (string, size);
  vte_terminal_feed (VTE_TERMINAL (display), string, size);
}

gint
send_serial (gchar *string, gint len)
{
  gint bytes_written;

  bytes_written = Send_chars (string, len);
  if (bytes_written > 0)
    {
      if (echo_on)
        put_chars (string, bytes_written, crlfauto_on, esc_clear_screen_on);
    }

  return bytes_written;
}

void
Got_Input (VteTerminal *widget, gchar *text, guint length, gpointer ptr)
{
  send_serial (text, length);
}

gboolean
Envoie_car (GtkWidget *widget, GdkEventKey *event, gpointer pointer)
{
  if (g_utf8_validate (event->string, 1, NULL))
    send_serial (event->string, 1);

  return FALSE;
}

void
clear_display (void)
{
  initialize_hexadecimal_display ();
  if (display)
    vte_terminal_reset (VTE_TERMINAL (display), TRUE, TRUE);
}

/* ─── Display state setters ────────────────────────────────────────── */

void
set_hex_bytes_per_line (gint bpl)
{
  bytes_per_line = bpl;
}

void
set_hex_show_index (gboolean show)
{
  show_index = show;
}
