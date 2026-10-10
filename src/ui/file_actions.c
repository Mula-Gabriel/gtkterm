/* Envoi de fichier, captures RAW/ASCII et journal (sous-projet 6a). La logique est dans le cœur (file_sender.c, capture.c, rx_logger.c) ;
 * ce module ne fait que relier les actions, les sélecteurs de fichiers asynchrones, la liaison et les options. */
#include <config.h>
#include <errno.h>
#include <glib/gi18n.h>

#include "app.h"
#include "app_config.h"
#include "app_ui.h"
#include "capture.h"
#include "dialogs.h"
#include "file_actions.h"
#include "file_send_window.h"
#include "file_sender.h"
#include "rx_logger.h"

typedef enum { CAPTURE_RAW, CAPTURE_ASCII } CaptureKind;

typedef struct
{
	GtkApplication *app;
	GtkTermLink *link;	/* non possédée : vit jusqu'au shutdown de l'application */
	GtTermOptions *opts;	/* non possédée */
	GSimpleAction *send_file, *save_raw, *save_ascii, *log_start, *log_paused, *log_stop, *log_clear;
	GtFileSender *sender;	/* envoi en cours, NULL sinon */
	GtFileSender *finished;	/* envoi terminé, libéré hors de son émission « done » */
	GtkWidget *send_window;	/* pointeur faible */
	gboolean quiet;	/* fermeture : fin d'envoi sans message */
	gboolean shut_down;
	GtRxLogger *logger;
	gchar *initial_send, *last_send, *last_raw, *last_ascii, *last_log;
	gulong received_id, state_id, opts_id;
} FileActions;

static FileActions fa;

/* ---- outils ---- */

static GtkWindow *main_window(void)
{
	return fa.shut_down ? NULL : gt_app_get_main_window();
}

static void error_message(const gchar *text)
{
	GtkWindow *w = main_window();

	if(w != NULL)
		gt_ui_message(w, GT_APP_MSG_ERROR, text);
}

/* Chemin local du fichier choisi ; NULL + message sinon */
static gchar *local_path(GFile *file)
{
	gchar *path = g_file_get_path(file);

	if(path == NULL)
		error_message(_("Only local files are supported"));
	return path;
}

/* Sélecteur d'enregistrement ouvert sur le dossier et le nom de last (rien de proposé si last est NULL) */
static void ask_save(const gchar *title, const gchar *last, GtUiFileCb cb, gpointer user_data)
{
	GtkWindow *parent = main_window();
	GFile *folder = NULL;
	gchar *name = NULL;

	if(parent == NULL)
		return;
	if(last != NULL)
	{
		gchar *dir = g_path_get_dirname(last);

		folder = g_file_new_for_path(dir);
		name = g_path_get_basename(last);
		g_free(dir);
	}
	gt_ui_save_file(parent, title, folder, name, cb, user_data);
	g_clear_object(&folder);
	g_free(name);
}

/* ---- envoi de fichier (règles 1 à 4) ---- */

/* gt_link_send rend 0 sans port ouvert, comme pour un port momentanément plein ; pour GtFileSender, 0 = « réessayer dans 10 ms » : hors GT_LINK_OPEN
 * (port fermé, perdu, en reconnexion, serveur TCP sans client), c'est une erreur, sinon l'envoi réessaierait sans fin. */
static gssize send_write(const guint8 *data, gsize len, gpointer user_data)
{
	GtkTermLink *link = GT_LINK(user_data);
	gssize written;

	if(gt_link_get_state(link) != GT_LINK_OPEN)
	{
		errno = ENOTCONN;
		return -1;
	}
	written = gt_link_send(link, data, len, GT_SEND_FILE);
	/* Port plein (écriture non bloquante, tampon du pilote ou de la pty saturé) : « réessayer plus tard », pas une erreur d'envoi */
	if(written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
		return 0;
	return written;
}

static void on_send_progress(GtFileSender *s, guint64 sent, guint64 total, gpointer user_data)
{
	if(s == fa.sender && fa.send_window != NULL)
		gt_file_send_window_set_progress(fa.send_window, sent, total);
}

static void on_send_done(GtFileSender *s, GError *error, gpointer user_data)
{
	if(s != fa.sender)
		return;
	fa.sender = NULL;
	g_clear_object(&fa.finished);
	fa.finished = s;	/* référence de fa.sender transférée ; libérée au prochain envoi ou à l'arrêt */
	if(fa.send_window != NULL)
		gt_file_send_window_close(fa.send_window);
	g_simple_action_set_enabled(fa.send_file, TRUE);
	/* Message rédigé par le module : « Error sending file » (perte du port) ou « Error sending file: <cause> » (écriture échouée) */
	if(error != NULL && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED) && !fa.quiet)
		error_message(error->message);
}

static void on_send_cancel(gpointer user_data)
{
	if(fa.sender != NULL)
		gt_file_sender_cancel(fa.sender);
}

static void on_send_file_chosen(GFile *file, gpointer user_data)
{
	GError *err = NULL;
	gchar *path, *display;

	if(fa.shut_down || file == NULL || fa.sender != NULL)
		return;
	path = local_path(file);
	if(path == NULL)
		return;
	if(gt_link_get_state(fa.link) != GT_LINK_OPEN)
	{
		gchar *text = g_strdup_printf("%s\n%s", _("Error sending file"), _("The port is not open"));

		error_message(text);
		g_free(text);
		g_free(path);
		return;
	}
	g_clear_object(&fa.finished);
	fa.sender = gt_file_sender_new(send_write, fa.link, config.wait_delay_ms, config.wait_char);
	g_signal_connect(fa.sender, "progress", G_CALLBACK(on_send_progress), NULL);
	g_signal_connect(fa.sender, "done", G_CALLBACK(on_send_done), NULL);
	if(!gt_file_sender_start(fa.sender, path, &err))
	{
		/* Message déjà rédigé par GtFileSender (« Cannot read file <chemin>: … ») : affiché tel quel */
		g_clear_object(&fa.sender);
		error_message(err->message);
		g_error_free(err);
		g_free(path);
		return;
	}
	g_free(fa.last_send);
	fa.last_send = path;
	g_simple_action_set_enabled(fa.send_file, FALSE);
	/* Fichier vide : « done » arrive depuis un idle, après la création de la fenêtre, qui est alors refermée aussitôt */
	display = g_filename_display_basename(path);
	fa.send_window = gt_file_send_window_new(main_window(), display, gt_file_sender_get_total(fa.sender), on_send_cancel, NULL);
	g_object_add_weak_pointer(G_OBJECT(fa.send_window), (gpointer *)&fa.send_window);
	g_free(display);
}

static void on_send_file(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	GtkWindow *parent = main_window();
	const gchar *proposed = fa.last_send != NULL ? fa.last_send : fa.initial_send;
	GFile *initial = proposed != NULL ? g_file_new_for_path(proposed) : NULL;

	if(parent != NULL && fa.sender == NULL)
		gt_ui_open_file(parent, _("Send RAW File"), initial, NULL, on_send_file_chosen, NULL);
	g_clear_object(&initial);
}

/* ---- captures (règle 5) ---- */

static void on_capture_chosen(GFile *file, gpointer user_data)
{
	CaptureKind kind = GPOINTER_TO_INT(user_data);
	GtHistory *history;
	GError *err = NULL;
	gboolean ok;
	gchar *path, **last;

	if(fa.shut_down || file == NULL)
		return;
	path = local_path(file);
	if(path == NULL)
		return;
	history = gt_link_get_history(fa.link);
	if(kind == CAPTURE_RAW)
		ok = gt_capture_save_raw(history, path, &err);
	else
	{
		gboolean crlf_auto, timestamp;

		gt_term_options_get_rx(fa.opts, &crlf_auto, &timestamp);	/* lu au moment de l'enregistrement */
		ok = gt_capture_save_ascii(history, crlf_auto, timestamp, path, &err);
	}
	if(!ok)
	{
		error_message(err->message);	/* message de capture.c, chemin et cause compris : affiché tel quel */
		g_error_free(err);
		g_free(path);
		return;
	}
	last = kind == CAPTURE_RAW ? &fa.last_raw : &fa.last_ascii;
	g_free(*last);
	*last = path;
}

static void on_save_raw(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	ask_save(_("Save RAW File"), fa.last_raw, on_capture_chosen, GINT_TO_POINTER(CAPTURE_RAW));
}

static void on_save_ascii(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	ask_save(_("Save ASCII File"), fa.last_ascii, on_capture_chosen, GINT_TO_POINTER(CAPTURE_ASCII));
}

/* ---- journal (règles 6 à 9) ---- */

static void log_update_actions(void)
{
	gboolean active = fa.logger != NULL && gt_rx_logger_is_active(fa.logger);

	g_simple_action_set_enabled(fa.log_start, active == FALSE);
	g_simple_action_set_enabled(fa.log_paused, active);
	g_simple_action_set_enabled(fa.log_stop, active);
	g_simple_action_set_enabled(fa.log_clear, active);
	g_simple_action_set_state(fa.log_paused, g_variant_new_boolean(active && gt_rx_logger_is_paused(fa.logger)));
}

static void on_log_file_chosen(GFile *file, gpointer user_data)
{
	gboolean crlf_auto, timestamp;
	GError *err = NULL;
	gchar *path;

	if(fa.shut_down || file == NULL)
		return;
	path = local_path(file);
	if(path == NULL)
		return;
	gt_term_options_get_rx(fa.opts, &crlf_auto, &timestamp);
	if(!gt_rx_logger_start(fa.logger, path, crlf_auto, timestamp, &err))
	{
		error_message(err->message);	/* message de rx_logger.c, chemin et cause compris : affiché tel quel */
		g_error_free(err);
		g_free(path);
	}
	else
	{
		g_free(fa.last_log);
		fa.last_log = path;
	}
	log_update_actions();
}

static void on_log_start(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	if(fa.shut_down)
		return;
	if(!gt_rx_logger_is_active(fa.logger))
		ask_save(_("Log file selection"), fa.last_log, on_log_file_chosen, NULL);
}

static void on_log_paused_change_state(GSimpleAction *action, GVariant *value, gpointer user_data)
{
	if(fa.shut_down || !gt_rx_logger_is_active(fa.logger))
		return;
	gt_rx_logger_set_paused(fa.logger, g_variant_get_boolean(value));
	g_simple_action_set_state(action, value);
}

static void on_log_stop(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	if(fa.shut_down)
		return;
	gt_rx_logger_stop(fa.logger);
	log_update_actions();
}

static void on_log_clear(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	GError *err = NULL;

	if(fa.shut_down || !gt_rx_logger_is_active(fa.logger))
		return;
	if(!gt_rx_logger_clear(fa.logger, &err))
	{
		error_message(err->message);	/* « Cannot clear file <chemin>: … » (rx_logger.c) */
		g_error_free(err);
	}
	log_update_actions();
}

static void on_options_changed(GtTermOptions *opts, gpointer user_data)
{
	gboolean crlf_auto, timestamp;

	gt_term_options_get_rx(opts, &crlf_auto, &timestamp);
	gt_rx_logger_set_options(fa.logger, crlf_auto, timestamp);
}

/* ---- liaison ---- */

/* Sous la surveillance du transport : aucune boucle imbriquée (gt_app_message ouvre une boîte non bloquante) */
static void on_received(GtkTermLink *link, GBytes *data, gint64 wall_time_us, gpointer user_data)
{
	gsize len = 0;
	const guint8 *p = g_bytes_get_data(data, &len);

	if(fa.shut_down)
		return;
	if(fa.sender != NULL)
		gt_file_sender_feed_rx(fa.sender, p, len);
	if(gt_rx_logger_is_active(fa.logger))
	{
		GError *err = NULL;

		if(!gt_rx_logger_feed(fa.logger, p, len, wall_time_us, &err))
		{
			log_update_actions();
			/* « Failed to log data to <chemin>: … » (rx_logger.c) : affiché tel quel */
			gt_app_message(GT_APP_MSG_ERROR, "%s", err != NULL ? err->message : _("Failed to log data"));
			g_clear_error(&err);
		}
	}
}

static void on_link_state(GObject *object, GParamSpec *pspec, gpointer user_data)
{
	GtLinkState st = gt_link_get_state(GT_LINK(object));

	if(fa.sender != NULL && (st == GT_LINK_LOST || st == GT_LINK_RECONNECTING || st == GT_LINK_CLOSED))
		gt_file_sender_link_lost(fa.sender);
}

/* ---- installation et arrêt ---- */

static GSimpleAction *add_action(GtkApplication *app, const gchar *name, GCallback activate)
{
	GSimpleAction *a = g_simple_action_new(name, NULL);

	g_signal_connect(a, "activate", activate, NULL);
	g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(a));
	g_object_unref(a);	/* la table d'actions garde sa référence */
	return a;
}

void gt_file_actions_install(GtkApplication *app, GtkTermLink *link, GtTermOptions *opts, const gchar *initial_send_file)
{
	static const gchar *const send_accels[] = { "<Control><Shift>r", NULL };

	g_return_if_fail(fa.app == NULL);
	fa.app = app;
	fa.link = link;
	fa.opts = opts;
	fa.initial_send = g_strdup(initial_send_file);
	fa.logger = gt_rx_logger_new();

	fa.send_file = add_action(app, "send-file", G_CALLBACK(on_send_file));
	fa.save_raw = add_action(app, "save-raw", G_CALLBACK(on_save_raw));
	fa.save_ascii = add_action(app, "save-ascii", G_CALLBACK(on_save_ascii));
	fa.log_start = add_action(app, "log-start", G_CALLBACK(on_log_start));
	fa.log_stop = add_action(app, "log-stop", G_CALLBACK(on_log_stop));
	fa.log_clear = add_action(app, "log-clear", G_CALLBACK(on_log_clear));
	fa.log_paused = g_simple_action_new_stateful("log-paused", NULL, g_variant_new_boolean(FALSE));
	g_signal_connect(fa.log_paused, "change-state", G_CALLBACK(on_log_paused_change_state), NULL);
	g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(fa.log_paused));
	g_object_unref(fa.log_paused);
	gtk_application_set_accels_for_action(app, "app.send-file", send_accels);

	fa.received_id = g_signal_connect(link, "received", G_CALLBACK(on_received), NULL);
	fa.state_id = g_signal_connect(link, "notify::state", G_CALLBACK(on_link_state), NULL);
	fa.opts_id = g_signal_connect(opts, "changed", G_CALLBACK(on_options_changed), NULL);
	log_update_actions();
}

void gt_file_actions_shutdown(void)
{
	if(fa.app == NULL || fa.shut_down)
		return;
	fa.shut_down = TRUE;
	fa.quiet = TRUE;
	g_clear_signal_handler(&fa.received_id, fa.link);
	g_clear_signal_handler(&fa.state_id, fa.link);
	g_clear_signal_handler(&fa.opts_id, fa.opts);
	if(fa.sender != NULL)
	{
		GtFileSender *s = g_object_ref(fa.sender);

		gt_file_sender_cancel(s);	/* « done » synchrone ou non : dans les deux cas, plus rien ne touche l'interface */
		if(fa.sender == s)	/* « done » différé par le module : plus attendu */
		{
			g_signal_handlers_disconnect_by_func(s, G_CALLBACK(on_send_progress), NULL);
			g_signal_handlers_disconnect_by_func(s, G_CALLBACK(on_send_done), NULL);
			g_clear_object(&fa.sender);
		}
		g_object_unref(s);
	}
	g_clear_object(&fa.finished);
	if(fa.send_window != NULL)
		gt_file_send_window_close(fa.send_window);
	if(fa.logger != NULL)
	{
		gt_rx_logger_stop(fa.logger);
		gt_rx_logger_free(fa.logger);
		fa.logger = NULL;
	}
	g_clear_pointer(&fa.initial_send, g_free);
	g_clear_pointer(&fa.last_send, g_free);
	g_clear_pointer(&fa.last_raw, g_free);
	g_clear_pointer(&fa.last_ascii, g_free);
	g_clear_pointer(&fa.last_log, g_free);
}
