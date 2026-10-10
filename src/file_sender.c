/***********************************************************************/
/* file_sender.c                                                       */
/*                                                                     */
/*   Purpose                                                           */
/*      Envoi d'un fichier sur la liaison, sans interface              */
/*      (logique de l'ancien files.c, sous-projet 6a)                  */
/*                                                                     */
/***********************************************************************/

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <config.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include "file_sender.h"

#define BLOCK_SIZE 4096
#define RETRY_MS 10		/* écriture qui a rendu 0 : nouvel essai */

enum
{
	SIGNAL_PROGRESS,
	SIGNAL_DONE,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

struct _GtFileSender
{
	GObject parent_instance;
	GtFileSenderWrite write;
	gpointer user_data;
	gint line_delay_ms;
	gint wait_char;
	gboolean running;	/* entre start et "done" */
	gint fd;		/* -1 hors envoi */
	guint source_id;	/* prochaine étape programmée (idle ou minuterie), 0 si aucune */
	gboolean waiting_char;	/* LF écrit, caractère attendu */
	guint8 buf[BLOCK_SIZE];
	gsize buf_len;		/* octets lus dans buf */
	gsize buf_pos;		/* octets de buf déjà écrits */
	guint64 sent;
	guint64 total;
};

G_DEFINE_FINAL_TYPE(GtFileSender, gt_file_sender, G_TYPE_OBJECT)

static gboolean step_cb(gpointer data);

/* Retire la source programmée et ferme le fichier ; aucun signal */
static void release(GtFileSender *s)
{
	if(s->source_id != 0)
	{
		g_source_remove(s->source_id);
		s->source_id = 0;
	}
	if(s->fd >= 0)
	{
		close(s->fd);
		s->fd = -1;
	}
	s->waiting_char = FALSE;
	s->running = FALSE;
}

/* Fin de l'envoi : "done" une seule fois. L'appelant ne touche plus à s après l'appel (le gestionnaire a pu le libérer).
 * Prend possession de error. */
static void finish(GtFileSender *s, GError *error)
{
	if(!s->running)
	{
		g_clear_error(&error);
		return;
	}
	release(s);
	g_object_ref(s);
	g_signal_emit(s, signals[SIGNAL_DONE], 0, error);
	g_object_unref(s);
	g_clear_error(&error);
}

static void schedule(GtFileSender *s, guint delay_ms)
{
	g_assert(s->source_id == 0);
	if(delay_ms == 0)
		s->source_id = g_idle_add(step_cb, s);
	else
		s->source_id = g_timeout_add(delay_ms, step_cb, s);
}

static gboolean paced(GtFileSender *s)
{
	return s->line_delay_ms > 0 || s->wait_char >= 0;
}

/* Une étape : au plus une écriture, puis la suivante est programmée (ou l'attente du caractère commence) */
static gboolean step_cb(gpointer data)
{
	GtFileSender *s = data;
	gsize chunk;
	gboolean ends_with_lf = FALSE;
	gssize written;

	s->source_id = 0;
	if(s->sent >= s->total)
	{
		finish(s, NULL);
		return G_SOURCE_REMOVE;
	}
	if(s->buf_pos == s->buf_len)
	{
		gssize n;

		do
			n = read(s->fd, s->buf, sizeof(s->buf));
		while(n < 0 && errno == EINTR);
		if(n <= 0)
		{
			/* 0 : fichier raccourci pendant l'envoi, fin normale ; -1 : erreur de lecture */
			gint err = errno;

			finish(s, n == 0 ? NULL : g_error_new(G_IO_ERROR, g_io_error_from_errno(err), _("Error sending file: %s"), g_strerror(err)));
			return G_SOURCE_REMOVE;
		}
		s->buf_len = (gsize)n;
		s->buf_pos = 0;
	}

	chunk = s->buf_len - s->buf_pos;
	if(paced(s))
	{
		/* Jusqu'au prochain LF inclus : la ligne part avec sa fin */
		const guint8 *lf = memchr(s->buf + s->buf_pos, '\n', chunk);

		if(lf != NULL)
		{
			chunk = (gsize)(lf - (s->buf + s->buf_pos)) + 1;
			ends_with_lf = TRUE;
		}
	}

	errno = 0;
	written = s->write(s->buf + s->buf_pos, chunk, s->user_data);
	if(written < 0)
	{
		gint err = errno != 0 ? errno : EIO;

		finish(s, g_error_new(G_IO_ERROR, g_io_error_from_errno(err), _("Error sending file: %s"), g_strerror(err)));
		return G_SOURCE_REMOVE;
	}
	if(written == 0)
	{
		schedule(s, RETRY_MS);
		return G_SOURCE_REMOVE;
	}
	if((gsize)written > chunk)
		written = (gssize)chunk;
	s->buf_pos += (gsize)written;
	s->sent += (guint64)written;

	/* Référence gardée jusqu'à la fin de l'étape : un gestionnaire de « progress » peut annuler l'envoi, voire lâcher sa référence
	 * (l'objet est alors libéré par le g_object_unref final, et dispose retire ce qui vient d'être programmé) */
	g_object_ref(s);
	g_signal_emit(s, signals[SIGNAL_PROGRESS], 0, s->sent, s->total);
	if(s->running && s->source_id == 0)
	{
		if(ends_with_lf && (gsize)written == chunk)
		{
			if(s->line_delay_ms > 0)
				schedule(s, (guint)s->line_delay_ms);
			else
				s->waiting_char = TRUE;
		}
		else
			schedule(s, 0);
	}
	g_object_unref(s);
	return G_SOURCE_REMOVE;
}

static void gt_file_sender_dispose(GObject *object)
{
	/* Libéré pendant un envoi : arrêt silencieux (pas de "done" depuis un objet qui disparaît) */
	release(GT_FILE_SENDER(object));
	G_OBJECT_CLASS(gt_file_sender_parent_class)->dispose(object);
}

static void gt_file_sender_class_init(GtFileSenderClass *klass)
{
	GObjectClass *oc = G_OBJECT_CLASS(klass);

	oc->dispose = gt_file_sender_dispose;
	signals[SIGNAL_PROGRESS] = g_signal_new("progress", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
	                                        G_TYPE_NONE, 2, G_TYPE_UINT64, G_TYPE_UINT64);
	signals[SIGNAL_DONE] = g_signal_new("done", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
	                                    G_TYPE_NONE, 1, G_TYPE_ERROR);
}

static void gt_file_sender_init(GtFileSender *s)
{
	s->fd = -1;
	s->wait_char = -1;
}

GtFileSender *gt_file_sender_new(GtFileSenderWrite write, gpointer user_data, gint line_delay_ms, gint wait_char)
{
	GtFileSender *s;

	g_return_val_if_fail(write != NULL, NULL);
	s = g_object_new(GT_TYPE_FILE_SENDER, NULL);
	s->write = write;
	s->user_data = user_data;
	s->line_delay_ms = MAX(line_delay_ms, 0);
	s->wait_char = (wait_char >= 0 && wait_char <= 255) ? wait_char : -1;
	return s;
}

gboolean gt_file_sender_start(GtFileSender *s, const gchar *path, GError **error)
{
	struct stat st;
	gint fd;

	g_return_val_if_fail(GT_IS_FILE_SENDER(s), FALSE);
	g_return_val_if_fail(path != NULL, FALSE);

	if(s->running)
	{
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PENDING, _("A file is already being sent"));
		return FALSE;
	}
	/* O_NONBLOCK : une FIFO sans écrivain ne bloque pas l'ouverture (l'interface gèlerait) ; O_NOCTTY : un tty choisi par erreur ne devient
	 * pas terminal de contrôle. Le fichier est ensuite refusé s'il n'est pas ordinaire ; sur un fichier ordinaire, O_NONBLOCK est sans effet. */
	fd = g_open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOCTTY, 0);
	if(fd < 0)
	{
		gint err = errno;

		g_set_error(error, G_IO_ERROR, g_io_error_from_errno(err), _("Cannot read file %s: %s"), path, g_strerror(err));
		return FALSE;
	}
	if(fstat(fd, &st) != 0 || !S_ISREG(st.st_mode))
	{
		close(fd);
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_REGULAR_FILE, _("Cannot read file %s: not a regular file"), path);
		return FALSE;
	}
	s->fd = fd;
	s->total = (guint64)st.st_size;
	s->sent = 0;
	s->buf_len = 0;
	s->buf_pos = 0;
	s->waiting_char = FALSE;
	s->running = TRUE;
	/* Fichier vide compris : la première étape constate sent >= total et émet "done" depuis la boucle */
	schedule(s, 0);
	return TRUE;
}

void gt_file_sender_feed_rx(GtFileSender *s, const guint8 *data, gsize len)
{
	g_return_if_fail(GT_IS_FILE_SENDER(s));

	if(!s->running || !s->waiting_char || len == 0)
		return;
	if(memchr(data, s->wait_char, len) != NULL)
	{
		s->waiting_char = FALSE;
		schedule(s, 0);
	}
}

void gt_file_sender_link_lost(GtFileSender *s)
{
	g_return_if_fail(GT_IS_FILE_SENDER(s));

	if(s->running)
		finish(s, g_error_new_literal(G_IO_ERROR, G_IO_ERROR_BROKEN_PIPE, _("Error sending file")));
}

void gt_file_sender_cancel(GtFileSender *s)
{
	g_return_if_fail(GT_IS_FILE_SENDER(s));

	if(s->running)
		finish(s, g_error_new_literal(G_IO_ERROR, G_IO_ERROR_CANCELLED, _("File transfer cancelled")));
}

guint64 gt_file_sender_get_sent(GtFileSender *s)
{
	g_return_val_if_fail(GT_IS_FILE_SENDER(s), 0);

	return s->sent;
}

guint64 gt_file_sender_get_total(GtFileSender *s)
{
	g_return_val_if_fail(GT_IS_FILE_SENDER(s), 0);

	return s->total;
}
