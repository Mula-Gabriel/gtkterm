/* Vue du terminal GTK 4 : voir terminal_view.h. Reprise de terminal_display.c : feed(), terminal_display_redraw(),
 * terminal_display_on_received/on_sent/on_history_cleared(), Got_Input() et clear_display() ; de term_config.c :
 * config_apply_to_terminal() et clear_scrollback(). Les anciennes variables globales (affichage, écho…) sont remplacées par l'état de la vue et GtTermOptions.
 * Rappels de la liaison sous la surveillance du transport (link.h) : ils ne font qu'alimenter VTE, jamais d'ouverture ni de boucle. */
#include <config.h>
#include <glib/gi18n.h>
#include <errno.h>

#include "app_ui.h"
#include "terminal_view.h"
#include "view_render.h"

#define TERMINAL_VIEW_KEY "gt-terminal-view"

/* RIS (remise à zéro complète, écran effacé) puis effacement du défilement (extension VTE) : injectés dans le même flux que les données,
 * donc exécutés avant le rejeu qui suit. 6 octets : sans l'octet NUL que clear_display() envoyait en trop. */
static const char clear_sequence[] = "\033c\033[3J";

typedef struct
{
	GtkTermLink *link;	/* non possédée : liaison par défaut, vit plus longtemps que la fenêtre */
	GtTermOptions *options;	/* référence */
	GtSession *session;	/* pointeur faible : destinataire des messages (crochet status_flash) */
	VteTerminal *vte;
	GtViewRender *render;
	gint scrollback;	/* dernière valeur appliquée (clear_scrollback la rétablit) */
	gulong received_id, sent_id, cleared_id, changed_id;
} TermView;

static TermView *term_view_get(GtkWidget *view)
{
	return g_object_get_data(G_OBJECT(view), TERMINAL_VIEW_KEY);
}

static void feed(TermView *tv, GString *s)
{
	if(s->len > 0)
		vte_terminal_feed(tv->vte, s->str, (gssize)s->len);
}

static void current_options(TermView *tv, GtViewOptions *vo)
{
	gt_term_options_get_view(tv->options, vo);
	gt_term_options_get_rx(tv->options, &vo->crlf_auto, &vo->timestamp);
}

typedef struct
{
	TermView *tv;
	GString *out;
} Replay;

static void replay_record(const GtRecord *rec, gpointer user_data)
{
	Replay *r = user_data;

	gt_view_render_record(r->tv->render, rec, r->out);
}

void gt_terminal_view_redraw(GtkWidget *view)
{
	TermView *tv = term_view_get(view);
	Replay r = { tv, g_string_new(NULL) };
	GtViewOptions vo;

	current_options(tv, &vo);
	gt_view_render_reset(tv->render, &vo);
	vte_terminal_feed(tv->vte, clear_sequence, sizeof(clear_sequence) - 1);
	gt_history_foreach(gt_link_get_history(tv->link), replay_record, &r);
	feed(tv, r.out);
	g_string_free(r.out, TRUE);
}

static void on_received(GtkTermLink *link, GBytes *data, gint64 wall_time_us, gpointer user_data)
{
	TermView *tv = term_view_get(user_data);
	GString *out = g_string_new(NULL);
	gsize len;
	const guint8 *p = g_bytes_get_data(data, &len);

	gt_view_render_rx(tv->render, p, len, wall_time_us, out);
	feed(tv, out);
	g_string_free(out, TRUE);
}

static void on_sent(GtkTermLink *link, GBytes *data, gint origin, gpointer user_data)
{
	TermView *tv = term_view_get(user_data);
	GString *out = g_string_new(NULL);
	gsize len;
	const guint8 *p = g_bytes_get_data(data, &len);

	gt_view_render_tx(tv->render, p, len, (GtSendOrigin)origin, out);
	feed(tv, out);
	g_string_free(out, TRUE);
}

/* Historique vidé (Échap avec « ESC clear screen », Clear screen) et options changées : un seul redessin chacun */
static void on_redraw_needed(gpointer instance, gpointer user_data)
{
	gt_terminal_view_redraw(GTK_WIDGET(user_data));
}

/* Frappe et collage (signal « commit », connecté après le gestionnaire de VTE). « ESC clear screen » : seule la touche Échap tapée
 * dans le terminal efface l'écran local, sans rien envoyer (M32) ; macros, Lua, saisie hexadécimale et fichiers envoient 0x1B. */
static void on_commit(VteTerminal *vte, gchar *text, guint length, gpointer user_data)
{
	TermView *tv = term_view_get(user_data);

	if(gt_term_options_get_esc_clear_screen(tv->options) && length == 1 && (guchar)text[0] == 0x1b)
	{
		gt_link_clear_history(tv->link);
		return;
	}
	/* Tout accepté ou rien : au-delà de la file d'émission (collage géant, port qui n'avance plus), rien ne part et on le dit */
	if(gt_link_send(tv->link, (const guint8 *)text, length, GT_SEND_KEYBOARD) < 0 && errno == ENOBUFS && tv->session != NULL)
		gt_app_status_flash(tv->session, 5000, _("Not sent: %u bytes do not fit in the send queue (use File › Send RAW file… for large data)"), length);
}

/* « destroy » (dispose) : plus aucun rappel de la liaison ni des options vers un VTE détruit */
static void on_view_destroy(GtkWidget *view, gpointer user_data)
{
	TermView *tv = term_view_get(view);

	g_clear_signal_handler(&tv->received_id, tv->link);
	g_clear_signal_handler(&tv->sent_id, tv->link);
	g_clear_signal_handler(&tv->cleared_id, tv->link);
	g_clear_signal_handler(&tv->changed_id, tv->options);
	if(tv->session != NULL)
	{
		g_object_remove_weak_pointer(G_OBJECT(tv->session), (gpointer *)&tv->session);
		tv->session = NULL;
	}
}

static void term_view_free(gpointer data)
{
	TermView *tv = data;

	/* Normalement déjà fait par « destroy » ; par sûreté, aucun pointeur faible ne survit à tv */
	if(tv->session != NULL)
		g_object_remove_weak_pointer(G_OBJECT(tv->session), (gpointer *)&tv->session);
	gt_view_render_free(tv->render);
	g_object_unref(tv->options);
	g_free(tv);
}

GtkWidget *gt_terminal_view_new(GtkTermLink *link, GtTermOptions *opts, GtSession *session)
{
	TermView *tv = g_new0(TermView, 1);
	GtkWidget *view = gtk_scrolled_window_new();
	GtViewOptions vo;

	tv->link = link;
	tv->options = g_object_ref(opts);
	tv->session = session;
	if(session != NULL)
		g_object_add_weak_pointer(G_OBJECT(session), (gpointer *)&tv->session);
	tv->scrollback = -1;
	tv->vte = VTE_TERMINAL(vte_terminal_new());
	current_options(tv, &vo);
	tv->render = gt_view_render_new(&vo);
	g_object_set_data_full(G_OBJECT(view), TERMINAL_VIEW_KEY, tv, term_view_free);

	gtk_widget_set_name(GTK_WIDGET(tv->vte), "terminal");
	gtk_accessible_update_property(GTK_ACCESSIBLE(tv->vte), GTK_ACCESSIBLE_PROPERTY_LABEL, _("Terminal"), -1);
	gtk_widget_set_hexpand(GTK_WIDGET(tv->vte), TRUE);
	gtk_widget_set_vexpand(GTK_WIDGET(tv->vte), TRUE);
	/* Réglages de create_main_window (interface.c GTK 3) */
	vte_terminal_set_scroll_on_output(tv->vte, FALSE);
	vte_terminal_set_scroll_on_keystroke(tv->vte, TRUE);
	vte_terminal_set_mouse_autohide(tv->vte, TRUE);
	vte_terminal_set_backspace_binding(tv->vte, VTE_ERASE_ASCII_BACKSPACE);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(view), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(view), GTK_WIDGET(tv->vte));

	g_signal_connect_after(tv->vte, "commit", G_CALLBACK(on_commit), view);
	tv->received_id = g_signal_connect(link, "received", G_CALLBACK(on_received), view);
	tv->sent_id = g_signal_connect(link, "sent", G_CALLBACK(on_sent), view);
	tv->cleared_id = g_signal_connect(link, "history-cleared", G_CALLBACK(on_redraw_needed), view);
	tv->changed_id = g_signal_connect(opts, "changed", G_CALLBACK(on_redraw_needed), view);
	g_signal_connect(view, "destroy", G_CALLBACK(on_view_destroy), NULL);
	gt_terminal_view_redraw(view);	/* écran vide et rejeu de l'historique déjà présent */
	return view;
}

VteTerminal *gt_terminal_view_get_vte(GtkWidget *view)
{
	return term_view_get(view)->vte;
}

void gt_terminal_view_apply_profile(GtkWidget *view, const GtProfile *p)
{
	TermView *tv = term_view_get(view);
	PangoFontDescription *font = pango_font_description_from_string(p->font != NULL ? p->font : GT_PROFILE_DEFAULT_FONT);
	GdkRGBA fg, bg;

	vte_terminal_set_font(tv->vte, font);
	pango_font_description_free(font);
	vte_terminal_set_size(tv->vte, p->columns, p->rows);	/* profil validé (Verify_configuration) : jamais 0 */
	vte_terminal_set_scrollback_lines(tv->vte, p->scrollback);
	tv->scrollback = p->scrollback;
	gt_color_to_rgba(&p->foreground, &fg);
	gt_color_to_rgba(&p->background, &bg);
	vte_terminal_set_color_foreground(tv->vte, &fg);
	g_object_set_data_full(G_OBJECT(view), "gt-applied-foreground", gdk_rgba_copy(&fg), (GDestroyNotify)gdk_rgba_free);
	vte_terminal_set_color_background(tv->vte, &bg);
	vte_terminal_set_cursor_shape(tv->vte, p->block_cursor ? VTE_CURSOR_SHAPE_BLOCK : VTE_CURSOR_SHAPE_IBEAM);
}

void gt_terminal_view_clear_scrollback(GtkWidget *view)
{
	TermView *tv = term_view_get(view);

	vte_terminal_set_scrollback_lines(tv->vte, 0);
	vte_terminal_set_scrollback_lines(tv->vte, tv->scrollback);
}

gboolean gt_terminal_view_get_foreground(GtkWidget *view, GdkRGBA *out)
{
	const GdkRGBA *fg = g_object_get_data(G_OBJECT(view), "gt-applied-foreground");

	if(fg == NULL)
		return FALSE;
	*out = *fg;
	return TRUE;
}
