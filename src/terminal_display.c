/***********************************************************************/
/* terminal_display.c                                                  */
/* ------------------                                                  */
/*           GTKTerm Software                                          */
/*                                                                     */
/*   Purpose                                                           */
/*      Alimentation du terminal VTE : les vues sont rendues par       */
/*      view_render.c depuis la liaison et son historique              */
/*                                                                     */
/***********************************************************************/

#include <string.h>
#include <vte/vte.h>

#include "link.h"
#include "view_render.h"
#include "terminal_display.h"

/* Display globals (defined in interface.c) */
extern GtkWidget *display;
extern gboolean echo_on;
extern gboolean crlfauto_on;
extern gboolean timestamp_on;
extern gboolean esc_clear_screen_on;

static GtViewOptions view_opts = { GT_VIEW_ASCII, FALSE, FALSE, FALSE, 16, FALSE };
static GtViewRender *view = NULL;

static GtViewRender *
current_view (void)
{
  if (view == NULL)
    view = gt_view_render_new (&view_opts);
  return view;
}

static void
feed (GString *s)
{
  if (display != NULL && s->len > 0)
    vte_terminal_feed (VTE_TERMINAL (display), s->str, (gssize) s->len);
}

static void
replay_record (const GtRecord *rec, gpointer user_data)
{
  gt_view_render_record (current_view (), rec, user_data);
}

void
terminal_display_redraw (void)
{
  GtkTermLink *link = gt_link_get_default ();
  GString *out = g_string_new (NULL);

  view_opts.echo = echo_on;
  view_opts.crlf_auto = crlfauto_on;
  view_opts.timestamp = timestamp_on;
  gt_view_render_reset (current_view (), &view_opts);
  clear_display ();
  if (link != NULL)
    gt_history_foreach (gt_link_get_history (link), replay_record, out);
  feed (out);
  g_string_free (out, TRUE);
}

void
terminal_display_set_mode (GtViewMode mode)
{
  view_opts.mode = mode;
  terminal_display_redraw ();
}

void
set_hex_bytes_per_line (gint bpl)
{
  view_opts.bytes_per_line = (guint) bpl;
}

void
set_hex_show_index (gboolean show)
{
  view_opts.show_index = show;
}

void
terminal_display_on_received (GtkTermLink *link, GBytes *data, gint64 wall_time_us, gpointer user_data)
{
  gsize len;
  const guint8 *p = g_bytes_get_data (data, &len);
  GString *out = g_string_new (NULL);

  gt_view_render_rx (current_view (), p, len, wall_time_us, out);
  feed (out);
  g_string_free (out, TRUE);
}

void
terminal_display_on_sent (GtkTermLink *link, GBytes *data, gint origin, gpointer user_data)
{
  gsize len;
  const guint8 *p = g_bytes_get_data (data, &len);
  GString *out = g_string_new (NULL);

  gt_view_render_tx (current_view (), p, len, (GtSendOrigin) origin, out);
  feed (out);
  g_string_free (out, TRUE);
}

void
terminal_display_on_history_cleared (GtkTermLink *link, gpointer user_data)
{
  terminal_display_redraw ();
}

void
Got_Input (VteTerminal *widget, gchar *text, guint length, gpointer ptr)
{
  /* « ESC efface l'écran » : seule la touche Échap tapée dans le terminal efface l'écran local, sans rien envoyer (M32).
     Les macros, Lua, la saisie hexadécimale et l'envoi de fichier envoient toujours 0x1B. */
  if (esc_clear_screen_on && length == 1 && (guchar) text[0] == '\x1b')
    {
      gt_link_clear_history (gt_link_get_default ());
      return;
    }

  gt_link_send (gt_link_get_default (), (const guint8 *) text, length, GT_SEND_KEYBOARD);
}

void
clear_display (void)
{
  if (display)
    {
      /* Feed clear sequences into the same VTE data stream so the erase
         is guaranteed to execute before any subsequent vte_terminal_feed
         calls (e.g. history replay). vte_terminal_reset() is a direct
         API call whose ordering relative to queued feed data is not
         guaranteed, which leaves stale hex content visible. */
      vte_terminal_feed (VTE_TERMINAL (display),
                         "\033c"    /* RIS – full terminal reset + clear screen */
                         "\033[3J", /* erase scrollback (VTE extension) */
                         7);
    }
}
