/***********************************************************************/
/* logging.h                                                           */
/* ---------                                                           */
/*                           GTKTerm Software                          */
/*                                 (c)                                 */
/*                                                                     */
/* ------------------------------------------------------------------- */
/*                                                                     */
/*   Purpose                                                           */
/*      Log all data that GTKTerm sees to a file                       */
/*                                                                     */
/*   ChangeLog                                                         */
/*       0.99.7 - Logging added (Thanks to Brian Beattie)              */
/*                                                                     */
/***********************************************************************/

#include <gtk/gtk.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <glib.h>

#include "interface.h"
#include "link.h"
#include "logging.h"
#include "rx_transform.h"

#include <config.h>
#include <glib/gi18n.h>

#define MAX_WRITE_ATTEMPTS 5

static gboolean	  Logging;
static gchar     *LoggingFileName;
static FILE      *LoggingFile;
static gchar     *logfile_default = NULL;
static GtRxTransform *log_transform = NULL;	/* état de ligne propre au journal */

extern gboolean crlfauto_on;
extern gboolean timestamp_on;

static gint OpenLogFile(gchar *filename)
{
	gchar *str;

	// open file and start logging
	if(!filename || (strcmp(filename, "") == 0))
	{
		str = g_strdup_printf(_("Filename error\n"));
		show_message(str, MSG_ERR);
		g_free(str);
		g_free(filename);
		return FALSE;
	}

	if(LoggingFile != NULL)
	{
		fclose(LoggingFile);
		LoggingFile = NULL;
		Logging = FALSE;
	}

	LoggingFileName = filename;

	/* Le sélecteur a fait confirmer le remplacement d'un fichier existant (set_do_overwrite_confirmation) : écraser, pas ajouter (M39) */
	LoggingFile = fopen(LoggingFileName, "w");
	if(LoggingFile == NULL)
	{
		str = g_strdup_printf(_("Cannot open file %s: %s\n"), LoggingFileName, strerror(errno));

		show_message(str, MSG_ERR);
		g_free(str);
		g_free(LoggingFileName);
		LoggingFileName = NULL;
	}
	else
	{
		logfile_default = g_strdup(LoggingFileName);
		Logging = TRUE;
		if(log_transform != NULL)
			gt_rx_transform_reset(log_transform);
	}

	return FALSE;
}

void logging_start(GtkAction *action, gpointer data)
{
	GtkWidget *file_select;
	gint retval;

	file_select = gtk_file_chooser_dialog_new(_("Log file selection"), GTK_WINDOW(Fenetre),
	              GTK_FILE_CHOOSER_ACTION_SAVE,
		"_Cancel", GTK_RESPONSE_CANCEL,
	              "_OK", GTK_RESPONSE_OK, NULL);
	gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(file_select), TRUE);

	if(logfile_default != NULL)
	{
		gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(file_select), logfile_default);
	}

	retval = gtk_dialog_run(GTK_DIALOG(file_select));
	if(retval == GTK_RESPONSE_OK)
	{
		OpenLogFile(gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(file_select)));
	}

	gtk_widget_destroy(file_select);

	toggle_logging_sensitivity(Logging);
	toggle_logging_pause_resume(Logging);
}

void logging_clear(void)
{
	if(LoggingFile == NULL)
	{
		return;
	}

	//Reopening with "w" will truncate the file
	LoggingFile = freopen(LoggingFileName, "w", LoggingFile);

	if (LoggingFile == NULL)
	{
		gchar *str = g_strdup_printf(_("Cannot open file %s: %s\n"), LoggingFileName, strerror(errno));
		show_message(str, MSG_ERR);
		g_free(str);
		g_free(LoggingFileName);
		LoggingFileName = NULL;
		Logging = FALSE;
		/* Plus de journal ouvert : menus remis dans leur état initial, sinon on ne pourrait plus en relancer un (M39) */
		toggle_logging_sensitivity(Logging);
		toggle_logging_pause_resume(Logging);
	}
	else if(log_transform != NULL)
		gt_rx_transform_reset(log_transform);
}

void logging_pause_resume(void)
{
	if(LoggingFile == NULL)
	{
		return;
	}
	if(Logging == TRUE)
	{
		Logging = FALSE;
	}
	else
	{
		Logging = TRUE;
	}
	toggle_logging_pause_resume(Logging);
}

void logging_stop(void)
{
	if(LoggingFile == NULL)
	{
		return;
	}

	fclose(LoggingFile);
	LoggingFile = NULL;
	Logging = FALSE;
	g_free(LoggingFileName);
	LoggingFileName = NULL;

	toggle_logging_sensitivity(Logging);
	toggle_logging_pause_resume(Logging);
}

/* Message d'échec d'écriture du journal, affiché depuis la boucle principale */
static gboolean log_failed_idle_cb(gpointer data)
{
	show_message(_("Failed to log data\n"), MSG_ERR);
	return G_SOURCE_REMOVE;
}

/* Échec d'écriture : le journal s'arrête tout de suite et le message est différé. log_write s'exécute sous le signal
   « received » de la liaison (surveillance du descripteur du transport) : la boucle imbriquée d'une boîte de dialogue
   pourrait y fermer le transport en cours de lecture ; et un journal laissé actif rouvrirait une boîte à chaque réception. */
static void log_write_failed(void)
{
	logging_stop();
	g_idle_add(log_failed_idle_cb, NULL);
}

static void log_write(const gchar *chars, guint size)
{
	guint writeAttempts = 0;
	guint bytesWritten = 0;

	/* if we are not logging exit */
	if(LoggingFile == NULL || Logging == FALSE)
	{
		return;
	}

	while (bytesWritten < size)
	{
		if (writeAttempts < MAX_WRITE_ATTEMPTS)
		{
			bytesWritten += fwrite(&chars[bytesWritten], 1,
			                       size-bytesWritten, LoggingFile);
			writeAttempts++;
		}
		else
		{
			log_write_failed();
			return;
		}
	}

	/* Disque plein : souvent signalé au vidage seulement, le flux étant tamponné */
	if(fflush(LoggingFile) != 0)
		log_write_failed();
}

void logging_on_received(GtkTermLink *link, GBytes *data, gint64 wall_time_us, gpointer user_data)
{
	gsize len;
	const guint8 *p = g_bytes_get_data(data, &len);
	GString *text;

	if(LoggingFile == NULL || Logging == FALSE)
		return;
	if(log_transform == NULL)
		log_transform = gt_rx_transform_new(crlfauto_on, timestamp_on);
	gt_rx_transform_set_options(log_transform, crlfauto_on, timestamp_on);
	text = g_string_new(NULL);
	gt_rx_transform_feed(log_transform, p, len, wall_time_us, text);
	log_write(text->str, (guint)text->len);
	g_string_free(text, TRUE);
}
