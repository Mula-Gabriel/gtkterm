/* Fenêtre de progression d'un envoi de fichier (sous-projet 6a). */
#include <config.h>
#include <glib/gi18n.h>

#include "file_send_window.h"

typedef struct
{
	GtFileSendCancelCb cancel;
	gpointer user_data;
	GtkWidget *bar;
	gboolean closing;	/* fermeture demandée par gt_file_send_window_close : ne pas rappeler cancel */
} SendWindow;

/* Fraction affichée : jamais de division par zéro (fichier vide), jamais au-delà de 1 */
static gdouble send_fraction(guint64 sent, guint64 total)
{
	if(total == 0 || sent >= total)
		return 1.0;
	return (gdouble)sent / (gdouble)total;
}

static void on_cancel_clicked(GtkButton *button, gpointer user_data)
{
	SendWindow *sw = user_data;

	if(!sw->closing && sw->cancel != NULL)
		sw->cancel(sw->user_data);
}

static gboolean on_close_request(GtkWindow *win, gpointer user_data)
{
	SendWindow *sw = user_data;

	if(sw->closing)
		return FALSE;
	if(sw->cancel != NULL)
		sw->cancel(sw->user_data);
	return TRUE;	/* la fin de l'envoi (signal done) ferme la fenêtre */
}

GtkWidget *gt_file_send_window_new(GtkWindow *parent, const gchar *display_name, guint64 total, GtFileSendCancelCb cancel, gpointer user_data)
{
	SendWindow *sw = g_new0(SendWindow, 1);
	GtkWidget *win = gtk_window_new();
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	GtkWidget *button = gtk_button_new_with_mnemonic(_("_Cancel"));
	gchar *title = g_strdup_printf(_("%s: transfer in progress..."), display_name);

	sw->cancel = cancel;
	sw->user_data = user_data;
	gtk_widget_set_name(win, "file-send-window");
	gtk_window_set_title(GTK_WINDOW(win), title);
	g_free(title);
	gtk_window_set_transient_for(GTK_WINDOW(win), parent);
	gtk_window_set_modal(GTK_WINDOW(win), TRUE);
	gtk_window_set_destroy_with_parent(GTK_WINDOW(win), TRUE);
	gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
	gtk_window_set_default_size(GTK_WINDOW(win), 320, -1);
	gtk_widget_set_margin_top(box, 12);
	gtk_widget_set_margin_bottom(box, 12);
	gtk_widget_set_margin_start(box, 12);
	gtk_widget_set_margin_end(box, 12);

	sw->bar = gtk_progress_bar_new();
	gtk_widget_set_name(sw->bar, "file-send-progress");
	gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(sw->bar), TRUE);
	gtk_box_append(GTK_BOX(box), sw->bar);

	gtk_widget_set_name(button, "file-send-cancel");
	gtk_widget_set_halign(button, GTK_ALIGN_END);
	g_signal_connect(button, "clicked", G_CALLBACK(on_cancel_clicked), sw);
	gtk_box_append(GTK_BOX(box), button);

	gtk_window_set_child(GTK_WINDOW(win), box);
	g_signal_connect(win, "close-request", G_CALLBACK(on_close_request), sw);
	g_object_set_data_full(G_OBJECT(win), "gt-send-window", sw, g_free);
	gt_file_send_window_set_progress(win, 0, total);	/* « 0 / total », barre vide jusqu'au premier « progress » */
	gtk_window_present(GTK_WINDOW(win));
	return win;
}

void gt_file_send_window_set_progress(GtkWidget *win, guint64 sent, guint64 total)
{
	SendWindow *sw = g_object_get_data(G_OBJECT(win), "gt-send-window");
	gchar *a = g_format_size(sent);
	gchar *b = g_format_size(total);
	gchar *text = g_strdup_printf("%s / %s", a, b);

	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(sw->bar), send_fraction(sent, total));
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(sw->bar), text);
	g_free(text);
	g_free(b);
	g_free(a);
}

void gt_file_send_window_close(GtkWidget *win)
{
	SendWindow *sw = g_object_get_data(G_OBJECT(win), "gt-send-window");

	sw->closing = TRUE;
	gtk_window_destroy(GTK_WINDOW(win));
}
