/***********************************************************************/
/* rx_logger.c                                                         */
/*                                                                     */
/*   Purpose                                                           */
/*      Journal des octets reçus, sans interface                       */
/*      (logique de l'ancien logging.c, sous-projet 6a)                */
/*                                                                     */
/***********************************************************************/

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include <config.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include "rx_logger.h"
#include "rx_transform.h"

struct _GtRxLogger
{
	FILE *file;			/* NULL : inactif */
	gchar *path;
	gboolean paused;
	GtRxTransform *transform;	/* état de ligne propre au journal */
	GString *text;			/* tampon de transformation réutilisé */
};

GtRxLogger *gt_rx_logger_new(void)
{
	GtRxLogger *l = g_new0(GtRxLogger, 1);

	l->transform = gt_rx_transform_new(FALSE, FALSE);
	l->text = g_string_new(NULL);
	return l;
}

void gt_rx_logger_free(GtRxLogger *l)
{
	if(l == NULL)
		return;
	gt_rx_logger_stop(l);
	gt_rx_transform_free(l->transform);
	g_string_free(l->text, TRUE);
	g_free(l);
}

/* Erreur d'écriture (ou de troncature) : message construit avec le chemin, puis journal arrêté */
static gboolean fail(GtRxLogger *l, gint err, gboolean clearing, GError **error)
{
	if(clearing)
		g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(err), _("Cannot clear file %s: %s"), l->path, g_strerror(err));
	else
		g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(err), _("Failed to log data to %s: %s"), l->path, g_strerror(err));
	gt_rx_logger_stop(l);
	return FALSE;
}

gboolean gt_rx_logger_start(GtRxLogger *l, const gchar *path, gboolean crlf_auto, gboolean timestamp, GError **error)
{
	FILE *f;

	g_return_val_if_fail(l != NULL && path != NULL, FALSE);
	gt_rx_logger_stop(l);
	/* Le sélecteur a fait confirmer le remplacement : écraser, pas ajouter (M39) */
	f = g_fopen(path, "w");
	if(f == NULL)
	{
		gint err = errno;

		g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(err), _("Cannot open file %s: %s"), path, g_strerror(err));
		return FALSE;
	}
	l->file = f;
	l->path = g_strdup(path);
	l->paused = FALSE;
	gt_rx_transform_set_options(l->transform, crlf_auto, timestamp);
	gt_rx_transform_reset(l->transform);
	return TRUE;
}

void gt_rx_logger_set_options(GtRxLogger *l, gboolean crlf_auto, gboolean timestamp)
{
	g_return_if_fail(l != NULL);

	gt_rx_transform_set_options(l->transform, crlf_auto, timestamp);
}

void gt_rx_logger_set_paused(GtRxLogger *l, gboolean paused)
{
	g_return_if_fail(l != NULL);

	if(l->file != NULL)
		l->paused = paused ? TRUE : FALSE;
}

gboolean gt_rx_logger_is_active(GtRxLogger *l)
{
	g_return_val_if_fail(l != NULL, FALSE);

	return l->file != NULL;
}

gboolean gt_rx_logger_is_paused(GtRxLogger *l)
{
	g_return_val_if_fail(l != NULL, FALSE);

	return l->file != NULL && l->paused;
}

gboolean gt_rx_logger_feed(GtRxLogger *l, const guint8 *data, gsize len, gint64 wall_time_us, GError **error)
{
	g_return_val_if_fail(l != NULL, FALSE);

	if(l->file == NULL || len == 0)
		return TRUE;
	g_string_truncate(l->text, 0);
	/* Nourri même en pause : la ligne en cours et l'horodatage suivent le flux reçu */
	gt_rx_transform_feed(l->transform, data, len, wall_time_us, l->text);
	if(l->paused || l->text->len == 0)
		return TRUE;
	errno = 0;
	if(fwrite(l->text->str, 1, l->text->len, l->file) != l->text->len)
		return fail(l, errno != 0 ? errno : EIO, FALSE, error);
	/* Disque plein : souvent signalé au vidage seulement, le flux étant tamponné */
	if(fflush(l->file) != 0)
		return fail(l, errno != 0 ? errno : EIO, FALSE, error);
	return TRUE;
}

gboolean gt_rx_logger_clear(GtRxLogger *l, GError **error)
{
	g_return_val_if_fail(l != NULL, FALSE);

	if(l->file == NULL)
		return TRUE;
	/* Vidé puis tronqué ; repositionné au début (le fichier n'est pas en ajout : sans fseek, la prochaine écriture laisserait un trou) */
	errno = 0;
	if(fflush(l->file) != 0 || ftruncate(fileno(l->file), 0) != 0 || fseek(l->file, 0, SEEK_SET) != 0)
		return fail(l, errno != 0 ? errno : EIO, TRUE, error);
	gt_rx_transform_reset(l->transform);
	return TRUE;
}

void gt_rx_logger_stop(GtRxLogger *l)
{
	g_return_if_fail(l != NULL);

	if(l->file == NULL)
		return;
	/* Chaque écriture a déjà été vidée et vérifiée : la fermeture n'a plus rien à signaler */
	fclose(l->file);
	l->file = NULL;
	g_clear_pointer(&l->path, g_free);
	l->paused = FALSE;
}

const gchar *gt_rx_logger_get_path(GtRxLogger *l)
{
	g_return_val_if_fail(l != NULL, NULL);

	return l->path;
}
