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

/* ---- lancer le script dans un terminal visible -------------------- */

typedef struct
{
	const gchar *name;
	const gchar *flag;    /* "-e", "--", or NULL for kitty-style */
	const gchar *extra;   /* additional flags (font/zoom) applied before -e/-- */
} TerminalDef;

static const TerminalDef terminals[] =
{
	{"xterm",             "-e", "-fa Monospace -fs 12"},
	{"x-terminal-emulator", "-e", NULL},
	{"gnome-terminal",    "--", "--zoom=1.5"},
	{"xfce4-terminal",    "-e", "--font 'Monospace 12'"},
	{"konsole",           "-e", NULL},
	{"lxterminal",        "-e", NULL},
	{"qterminal",         "-e", NULL},
	{"urxvt",             "-e", "-fn 'xft:Monospace:size=12'"},
	{"kitty",             NULL, "-o font_size=14"},
	{NULL, NULL, NULL}
};

static gchar *find_terminal(void)
{
	for(gint i = 0; terminals[i].name != NULL; i++)
	{
		gchar *path = g_find_program_in_path(terminals[i].name);
		if(path != NULL)
		{
			g_free(path);
			return g_strdup(terminals[i].name);
		}
	}
	return NULL;
}

static const TerminalDef *get_terminal_def(const gchar *name)
{
	for(gint i = 0; terminals[i].name != NULL; i++)
	{
		if(g_strcmp0(terminals[i].name, name) == 0)
			return &terminals[i];
	}
	return NULL;
}

static void run_update_script(GtkWindow *parent)
{
	const gchar *script = PACKAGE_DATA_DIR "/gtkterm-update.sh";

	gchar *term_name = find_terminal();
	if(term_name == NULL)
	{
		GtkWidget *m = gtk_message_dialog_new(parent,
				GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
				_("No terminal emulator found. Please install xterm or similar."));
		gtk_dialog_run(GTK_DIALOG(m));
		gtk_widget_destroy(m);
		return;
	}

	/*
	 * Build a single command string that launches the script in the
	 * terminal.  The inner sh -c wrapper ensures the terminal stays open
	 * after the script finishes so the user can read the output.
	 */
	const TerminalDef *def = get_terminal_def(term_name);
	gchar *inner_cmd = g_strdup_printf("sh '%s' '%s' '%s'; echo; echo '>>> Appuyez sur Entrée pour fermer...'; read",
			script, config.update_url, config.update_prefix);
	gchar *inner = g_shell_quote(inner_cmd);
	g_free(inner_cmd);
	gchar *cmd;

	if(def->flag == NULL)
		cmd = g_strdup_printf("%s %s sh -c %s",
			term_name,
			def->extra ? def->extra : "",
			inner);
	else
		cmd = g_strdup_printf("%s %s %s sh -c %s",
			term_name,
			def->extra ? def->extra : "",
			def->flag,
			inner);

	g_free(inner);
	g_free(term_name);

	GError *error = NULL;
	if(!g_spawn_command_line_async(cmd, &error))
	{
		GtkWidget *m = gtk_message_dialog_new(parent,
				GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
				_("Failed to launch update terminal:\n%s"), error->message);
		gtk_dialog_run(GTK_DIALOG(m));
		gtk_widget_destroy(m);
		g_clear_error(&error);
	}
	else
	{
		GtkWidget *m = gtk_message_dialog_new(parent,
				GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
				_("The update is running in a terminal window.\nFollow the instructions there and restart GTKTerm when done."));
		gtk_dialog_run(GTK_DIALOG(m));
		gtk_widget_destroy(m);
	}

	g_free(cmd);
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
