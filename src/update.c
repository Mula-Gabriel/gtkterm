#include "config.h"

#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <string.h>

#include "update.h"
#include "term_config.h"

extern struct configuration_port config;

/* Remembered "skip this version" sha for the current session. */
static gchar skipped_revision[64] = "";

/* ---- progress dialog: streams the update script's output ------------- */

typedef struct
{
	GtkWidget *dialog;
	GtkTextBuffer *buffer;
	GtkWidget *close_button;
} ProgressUI;

static void append_output(ProgressUI *ui, const gchar *text)
{
	GtkTextIter end;
	gtk_text_buffer_get_end_iter(ui->buffer, &end);
	gtk_text_buffer_insert(ui->buffer, &end, text, -1);
}

static gboolean on_script_output(GIOChannel *source, GIOCondition condition, gpointer data)
{
	ProgressUI *ui = (ProgressUI *)data;
	gchar *line = NULL;
	gsize len = 0;
	GIOStatus status;

	if(condition & (G_IO_IN | G_IO_PRI))
	{
		while((status = g_io_channel_read_line(source, &line, &len, NULL, NULL)) == G_IO_STATUS_NORMAL)
		{
			append_output(ui, line);
			g_free(line);
			line = NULL;
		}
	}

	if(condition & (G_IO_HUP | G_IO_ERR))
		return FALSE;

	return TRUE;
}

static void on_child_exit(GPid pid, gint status, gpointer data)
{
	ProgressUI *ui = (ProgressUI *)data;
	gboolean ok = g_spawn_check_wait_status(status, NULL);

	if(ok)
	{
		append_output(ui, _("\n>>> Update finished successfully.\n"));
		GtkWidget *q = gtk_message_dialog_new(GTK_WINDOW(ui->dialog),
				GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
				_("Update complete. Restart GTKTerm now?"));
		if(gtk_dialog_run(GTK_DIALOG(q)) == GTK_RESPONSE_YES)
		{
			gchar *argv[] = { (gchar *)"gtkterm", NULL };
			g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
			gtk_main_quit();
		}
		gtk_widget_destroy(q);
	}
	else
	{
		append_output(ui, _("\n!!! Update failed. See the log above.\n"));
	}

	gtk_widget_set_sensitive(ui->close_button, TRUE);
	g_spawn_close_pid(pid);
}

static void run_update_script(GtkWindow *parent)
{
	const gchar *script = PACKAGE_DATA_DIR "/gtkterm-update.sh";
	gchar *argv[] = {
		(gchar *)"sh", (gchar *)script,
		config.update_url, config.update_prefix, NULL
	};
	GError *error = NULL;
	GPid pid;
	gint out_fd = -1, err_fd = -1;

	ProgressUI *ui = g_new0(ProgressUI, 1);
	ui->dialog = gtk_dialog_new_with_buttons(_("Updating GTKTerm"),
			parent, GTK_DIALOG_DESTROY_WITH_PARENT,
			_("_Close"), GTK_RESPONSE_CLOSE, NULL);
	gtk_window_set_default_size(GTK_WINDOW(ui->dialog), 640, 400);
	ui->close_button = gtk_dialog_get_widget_for_response(GTK_DIALOG(ui->dialog), GTK_RESPONSE_CLOSE);
	gtk_widget_set_sensitive(ui->close_button, FALSE);

	GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
	GtkWidget *view = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
	ui->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
	gtk_container_add(GTK_CONTAINER(scroll), view);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(ui->dialog))), scroll, TRUE, TRUE, 0);
	gtk_widget_show_all(ui->dialog);

	if(!g_spawn_async_with_pipes(NULL, argv, NULL,
			G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
			NULL, NULL, &pid, NULL, &out_fd, &err_fd, &error))
	{
		append_output(ui, error ? error->message : _("Failed to start update script."));
		gtk_widget_set_sensitive(ui->close_button, TRUE);
		g_clear_error(&error);
		g_signal_connect_swapped(ui->dialog, "response", G_CALLBACK(gtk_widget_destroy), ui->dialog);
		return;
	}

	GIOChannel *out_ch = g_io_channel_unix_new(out_fd);
	GIOChannel *err_ch = g_io_channel_unix_new(err_fd);
	g_io_channel_set_flags(out_ch, G_IO_FLAG_NONBLOCK, NULL);
	g_io_channel_set_flags(err_ch, G_IO_FLAG_NONBLOCK, NULL);
	g_io_add_watch(out_ch, G_IO_IN | G_IO_HUP | G_IO_ERR, on_script_output, ui);
	g_io_add_watch(err_ch, G_IO_IN | G_IO_HUP | G_IO_ERR, on_script_output, ui);
	g_io_channel_unref(out_ch);
	g_io_channel_unref(err_ch);
	g_child_watch_add(pid, on_child_exit, ui);

	g_signal_connect_swapped(ui->dialog, "response", G_CALLBACK(gtk_widget_destroy), ui->dialog);
}

/* ---- the remote version check --------------------------------------- */

typedef struct
{
	GtkWindow *parent;
	gboolean manual;
} CheckCtx;

static void offer_update(GtkWindow *parent, const gchar *remote_sha)
{
	GtkWidget *d = gtk_dialog_new_with_buttons(_("Update available"),
			parent, GTK_DIALOG_DESTROY_WITH_PARENT,
			_("_Update now"), GTK_RESPONSE_ACCEPT,
			_("_Later"), GTK_RESPONSE_REJECT,
			_("_Skip this version"), GTK_RESPONSE_NO, NULL);
	GtkWidget *label = gtk_label_new(_("A new version of GTKTerm is available.\nUpdate now?"));
	gtk_widget_set_margin_top(label, 12);
	gtk_widget_set_margin_bottom(label, 12);
	gtk_widget_set_margin_start(label, 12);
	gtk_widget_set_margin_end(label, 12);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(d))), label, TRUE, TRUE, 0);
	gtk_widget_show_all(d);

	gint resp = gtk_dialog_run(GTK_DIALOG(d));
	gtk_widget_destroy(d);

	if(resp == GTK_RESPONSE_ACCEPT)
		run_update_script(parent);
	else if(resp == GTK_RESPONSE_NO)
		g_strlcpy(skipped_revision, remote_sha, sizeof(skipped_revision));
}

static void on_ls_remote_done(GObject *source, GAsyncResult *res, gpointer data)
{
	CheckCtx *ctx = (CheckCtx *)data;
	GError *error = NULL;
	gchar *stdout_buf = NULL;
	GSubprocess *proc = G_SUBPROCESS(source);

	gboolean ok = g_subprocess_communicate_utf8_finish(proc, res, &stdout_buf, NULL, &error);

	if(!ok || stdout_buf == NULL || stdout_buf[0] == '\0')
	{
		if(ctx->manual)
		{
			GtkWidget *m = gtk_message_dialog_new(ctx->parent,
					GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
					_("Could not check for updates:\n%s"),
					error ? error->message : _("no response from remote"));
			gtk_dialog_run(GTK_DIALOG(m));
			gtk_widget_destroy(m);
		}
		g_clear_error(&error);
		g_free(stdout_buf);
		g_free(ctx);
		return;
	}

	/* `git ls-remote` output: "<sha>\tHEAD\n" -> take the leading sha. */
	gchar remote_sha[64] = "";
	sscanf(stdout_buf, "%63s", remote_sha);
	g_free(stdout_buf);

	gboolean up_to_date = (g_strcmp0(remote_sha, GIT_REVISION) == 0);
	gboolean already_skipped = (g_strcmp0(remote_sha, skipped_revision) == 0);

	if(up_to_date)
	{
		if(ctx->manual)
		{
			GtkWidget *m = gtk_message_dialog_new(ctx->parent,
					GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
					_("GTKTerm is up to date."));
			gtk_dialog_run(GTK_DIALOG(m));
			gtk_widget_destroy(m);
		}
	}
	else if(ctx->manual || !already_skipped)
	{
		offer_update(ctx->parent, remote_sha);
	}

	g_free(ctx);
}

void update_check(GtkWindow *parent, gboolean manual)
{
	/* Startup check obeys the toggle; the manual button ignores it. */
	if(!manual && !config.update_check_startup)
		return;

	/* If we don't know our own commit, only the manual path proceeds. */
	if(g_strcmp0(GIT_REVISION, "unknown") == 0 && !manual)
		return;

	GError *error = NULL;
	GSubprocess *proc = g_subprocess_new(
			G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
			&error,
			"git", "ls-remote", config.update_url, "HEAD", NULL);

	if(proc == NULL)
	{
		if(manual)
		{
			GtkWidget *m = gtk_message_dialog_new(parent,
					GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
					_("Could not run git: %s"), error ? error->message : "");
			gtk_dialog_run(GTK_DIALOG(m));
			gtk_widget_destroy(m);
		}
		g_clear_error(&error);
		return;
	}

	CheckCtx *ctx = g_new0(CheckCtx, 1);
	ctx->parent = parent;
	ctx->manual = manual;
	g_subprocess_communicate_utf8_async(proc, NULL, NULL, on_ls_remote_done, ctx);
	g_object_unref(proc);
}
