/***********************************************************************/
/* fichier.c                                                           */
/* ---------                                                           */
/*           GTKTerm Software                                          */
/*                      (c) Julien Schmitt                             */
/*                                                                     */
/* ------------------------------------------------------------------- */
/*                                                                     */
/*   Purpose                                                           */
/*      Raw / text file transfer management                            */
/*                                                                     */
/*   ChangeLog                                                         */
/*   (All changes by Julien Schmitt except when explicitly written)    */
/*                                                                     */
/*      - 0.99.5 : changed all calls to strerror() by strerror_utf8()  */
/*      - 0.99.4 : added auto CR LF function by Sebastien              */
/*                 modified ecriture() to use send_serial()            */
/*      - 0.99.2 : Internationalization                                */
/*      - 0.98.4 : modified to use new buffer                          */
/*      - 0.98 : file transfer completely rewritten / optimized        */
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

#include "term_config.h"
#include "interface.h"
#include "link.h"
#include "rx_transform.h"

#define BUFFER_EMISSION 4096
#define LINE_FEED 0x0A

#include <config.h>
#include <glib/gi18n.h>

/* Global variables */
gint nb_car;
gint car_written;
gint current_buffer_position;
gint bytes_read;
GtkAdjustment *adj;
GtkWidget *ProgressBar;
gint Fichier;
guint callback_handler;
gchar *fic_defaut = NULL;
GtkWidget *Window;
static gboolean waiting_for_char = FALSE;
gboolean waiting_for_timer = FALSE;
gboolean input_running = FALSE;
gchar *str = NULL;
FILE *Fic;
static guint transfer_msg_id = 0;	/* message « transfert en cours » de la barre d'état, 0 si aucun */

/* Local functions prototype */
gint Envoie_fichier(GtkFileChooser *FS);
gint Sauve_fichier(GtkFileChooser *FS);
gint close_all(void);
gboolean ecriture(GIOChannel *source, GIOCondition condition, gpointer data);
gboolean timer(gpointer pointer);
gboolean idle(gpointer pointer);
void remove_input(void);
void add_input(void);

extern struct configuration_port config;
extern gboolean crlfauto_on;
extern gboolean timestamp_on;


void send_raw_file(GtkAction *action, gpointer data)
{
	GtkWidget *file_select;

	file_select = gtk_file_chooser_dialog_new(_("Send RAW File"),
	              GTK_WINDOW(Fenetre),
	              GTK_FILE_CHOOSER_ACTION_OPEN,
	              _("Cancel"), GTK_RESPONSE_CANCEL,
	              _("OK"), GTK_RESPONSE_ACCEPT,
	              NULL);

	if(fic_defaut != NULL)
		gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(file_select), fic_defaut);

	if(gtk_dialog_run(GTK_DIALOG(file_select)) == GTK_RESPONSE_ACCEPT)
	{
		gchar *fileName;
		gchar *msg;

		fileName = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(file_select));
		if (!fileName || strcmp(fileName, "") == 0)
		{
			msg = g_strdup_printf(_("File error\n"));
			show_message(msg, MSG_ERR);
			g_free(msg);
			g_free(fileName);
			gtk_widget_destroy(file_select);
			return;
		}

		if(!g_file_test(fileName, G_FILE_TEST_IS_REGULAR))
		{
			msg = g_strdup_printf(_("Error opening file\n"));
			show_message(msg, MSG_ERR);
			g_free(msg);
			g_free(fileName);
			gtk_widget_destroy(file_select);
			return;
		}

		Fichier = open(fileName, O_RDONLY);
		if(Fichier != -1)
		{
			GtkWidget *Bouton_annuler, *Box;

			fic_defaut = g_strdup(fileName);
			msg = g_strdup_printf(_("%s: transfer in progress..."), fileName);

			transfer_msg_id = gtk_statusbar_push(GTK_STATUSBAR(StatusBar), id, msg);
			car_written = 0;
			current_buffer_position = 0;
			bytes_read = 0;
			nb_car = lseek(Fichier, 0L, SEEK_END);
			if (nb_car == (gint)-1)
				nb_car = 0;
			lseek(Fichier, 0L, SEEK_SET);

			Window = gtk_dialog_new();
			gtk_window_set_title(GTK_WINDOW(Window), msg);
			g_free(msg);
			Box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
			gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(Window))), Box);
			ProgressBar = gtk_progress_bar_new();

			gtk_box_pack_start(GTK_BOX(Box), ProgressBar, FALSE, FALSE, 5);

			Bouton_annuler = gtk_button_new_with_label(_("Cancel"));
			g_signal_connect(GTK_WIDGET(Bouton_annuler), "clicked", G_CALLBACK(close_all), NULL);

			gtk_dialog_add_action_widget(GTK_DIALOG(Window), Bouton_annuler, GTK_RESPONSE_CANCEL);

			g_signal_connect(GTK_WIDGET(Window), "delete_event", G_CALLBACK(close_all), NULL);

			gtk_window_set_default_size(GTK_WINDOW(Window), 250, 100);
			gtk_window_set_modal(GTK_WINDOW(Window), TRUE);
			gtk_widget_show_all(Window);

			add_input();
		}
		else
		{
			msg = g_strdup_printf(_("Cannot read file %s: %s\n"), fileName, strerror(errno));
			show_message(msg, MSG_ERR);
			g_free(msg);
		}
		g_free(fileName);
	}
	gtk_widget_destroy(file_select);
}

/* Rappel GIOFunc de la surveillance G_IO_OUT (M35) : TRUE pour continuer, FALSE quand le transfert est terminé,
   interrompu ou mis en attente (la source est alors déjà retirée par close_all ou remove_input). */
gboolean ecriture(GIOChannel *source, GIOCondition condition, gpointer data)
{
	static gchar buffer[BUFFER_EMISSION];
	static gchar *current_buffer;
	static gint bytes_to_write;
	gint bytes_written;
	gchar *car;
	gboolean lf_found = FALSE;

	/* Perte de liaison pendant l'envoi : le descripteur est fermé, GLib signale ERR/HUP/NVAL en boucle et gt_link_send renvoie 0 (rien d'ouvert),
	   donc aucun chemin d'erreur ne serait pris. Fermer d'abord : show_message fait tourner une boucle imbriquée. */
	if((condition & (G_IO_ERR | G_IO_HUP | G_IO_NVAL)) || gt_link_get_fd(gt_link_get_default()) < 0)
	{
		close_all();
		show_message(_("Error sending file\n"), MSG_ERR);
		return FALSE;
	}

	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ProgressBar), (gfloat)car_written/(gfloat)nb_car );

	if(car_written < nb_car)
	{
		/* Read the file only if buffer totally sent or if buffer empty */
		if(current_buffer_position == bytes_read)
		{
			bytes_read = read(Fichier, buffer, BUFFER_EMISSION);

			if(bytes_read <= 0)
			{
				/* 0 : fin du fichier avant la taille annoncée (fichier raccourci) : fin normale du transfert.
				   -1 : erreur de lecture : transfert interrompu avec un message. Fermer d'abord : show_message fait tourner
				   une boucle imbriquée pendant laquelle la surveillance rappellerait ce rappel. */
				gint read_errno = errno;

				close_all();
				if(bytes_read < 0)
				{
					g_free(str);
					str = g_strdup_printf(_("Error sending file: %s\n"), strerror(read_errno));
					show_message(str, MSG_ERR);
				}
				return FALSE;
			}

			current_buffer_position = 0;
			current_buffer = buffer;
			bytes_to_write = bytes_read;
		}

		if(current_buffer == NULL)
		{
			/* something went wrong... */
			g_free(str);
			str = g_strdup_printf(_("Error sending file\n"));
			show_message(str, MSG_ERR);
			close_all();
			return FALSE;
		}

		car = current_buffer;

		if(config.delai != 0 || config.car != -1)
		{
			/* search for next LF, sans lire au-delà des données (borne testée avant *car) */
			bytes_to_write = current_buffer_position;
			while(bytes_to_write < bytes_read && *car != LINE_FEED)
			{
				car++;
				bytes_to_write++;
			}
			if(bytes_to_write < bytes_read)
			{
				/* *car est le LF : il part avec sa ligne */
				lf_found = TRUE;
				bytes_to_write++;
			}
		}

		/* write to serial port */
		bytes_written = (gint)gt_link_send(gt_link_get_default(), (const guint8 *)current_buffer, (gsize)(bytes_to_write - current_buffer_position),
		                                   GT_SEND_FILE);

		if(bytes_written == -1)
		{
			/* Problem while writing, stop file transfer. Fermer d'abord (la fenêtre de progression ne doit pas rester derrière la boîte
			   d'erreur, et la surveillance ne doit pas rappeler ce rappel pendant la boucle imbriquée de show_message) */
			gint write_errno = errno;

			close_all();
			g_free(str);
			str = g_strdup_printf(_("Error sending file: %s\n"), strerror(write_errno));
			show_message(str, MSG_ERR);
			return FALSE;
		}

		car_written += bytes_written;
		current_buffer_position += bytes_written;
		current_buffer += bytes_written;

		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ProgressBar), (gfloat)car_written/(gfloat)nb_car );

		if(config.delai != 0 && lf_found)
		{
			remove_input();
			g_timeout_add(config.delai, (GSourceFunc)timer, NULL);
			waiting_for_timer = TRUE;
			return FALSE;
		}
		else if(config.car != -1 && lf_found)
		{
			remove_input();
			waiting_for_char = TRUE;
			return FALSE;
		}
	}
	else
	{
		close_all();
		return FALSE;
	}
	return TRUE;
}

gboolean timer(gpointer pointer)
{
	if(waiting_for_timer == TRUE)
	{
		add_input();
		waiting_for_timer = FALSE;
	}
	return FALSE;
}

void add_input(void)
{
	GIOChannel *channel;
	gint fd;

	if(input_running == FALSE)
	{
		fd = gt_link_get_fd(gt_link_get_default());
		if(fd < 0)
		{
			/* Plus de port ouvert (perte de liaison) : le transfert s'arrête au lieu d'attendre indéfiniment */
			close_all();
			show_message(_("Error sending file\n"), MSG_ERR);
			return;
		}
		input_running = TRUE;
		channel = g_io_channel_unix_new(fd);
		callback_handler = g_io_add_watch_full(channel,
		                                       10,
		                                       G_IO_OUT,
		                                       ecriture,
		                                       NULL, NULL);
		/* La source garde sa propre référence sur le canal (M35) ; g_io_channel_unix_new ne ferme pas le descripteur à la libération */
		g_io_channel_unref(channel);
	}
}

/* La comparaison avec config.car a quitté le transport (sous-projet 2) : faite ici, sur les octets reçus par la liaison */
void files_on_received(GtkTermLink *link, GBytes *data, gint64 wall_time_us, gpointer user_data)
{
	gsize len;
	const guint8 *p = g_bytes_get_data(data, &len);

	if(!waiting_for_char || config.car == -1)
		return;
	for(gsize i = 0; i < len; i++)
	{
		if(p[i] == (guint8)config.car)
		{
			waiting_for_char = FALSE;
			add_input();
			return;
		}
	}
}

static gboolean files_lost_message_idle(gpointer data)
{
	show_message(_("Error sending file\n"), MSG_ERR);
	return G_SOURCE_REMOVE;
}

/* Pendant l'attente du caractère ou du délai, la surveillance G_IO_OUT est retirée : seul l'état de la liaison signale la perte.
   Ce gestionnaire tourne sous la surveillance du transport, sous udev ou dans une boucle imbriquée : message différé (pas de show_message ici). */
void files_on_link_state(GObject *object, GParamSpec *pspec, gpointer user_data)
{
	GtLinkState state = gt_link_get_state(GT_LINK(object));

	if(state != GT_LINK_LOST && state != GT_LINK_RECONNECTING && state != GT_LINK_CLOSED)
		return;
	if(!input_running && !waiting_for_char && !waiting_for_timer)
		return;
	close_all();
	g_idle_add(files_lost_message_idle, NULL);
}

void remove_input(void)
{
	if(input_running == TRUE)
	{
		g_source_remove(callback_handler);
		input_running = FALSE;
	}
}

gint close_all(void)
{
	remove_input();
	waiting_for_char = FALSE;
	waiting_for_timer = FALSE;
	/* Retirer le message du transfert lui-même : un message temporaire (macro en polling) peut être au sommet de la pile */
	if(transfer_msg_id != 0)
	{
		gtk_statusbar_remove(GTK_STATUSBAR(StatusBar), id, transfer_msg_id);
		transfer_msg_id = 0;
	}
	close(Fichier);
	gtk_widget_destroy(Window);

	return FALSE;
}

typedef struct
{
	GtRxTransform *transform;
	GString *text;
} AsciiSave;

static void save_ascii_record(const GtRecord *rec, gpointer user_data)
{
	AsciiSave *s = user_data;

	if(rec->kind == GT_RECORD_RX)
		gt_rx_transform_feed(s->transform, rec->data, rec->len, rec->wall_time_us, s->text);
}

/* Octets reçus bruts : ni écho ni horodatage (spec §5) */
static void write_raw_history(FILE *f)
{
	GBytes *rx = gt_history_copy_rx(gt_link_get_history(gt_link_get_default()));
	gsize n;
	const guint8 *p = g_bytes_get_data(rx, &n);

	if(n > 0)
		fwrite(p, 1, n, f);
	g_bytes_unref(rx);
}

/* Texte reçu comme la vue ASCII le montre, sans caractères de contrôle autres que CR et LF (octets non signés : l'UTF-8 reste) */
static void write_ascii_history(FILE *f)
{
	AsciiSave s;

	s.transform = gt_rx_transform_new(crlfauto_on, timestamp_on);
	s.text = g_string_new(NULL);
	gt_history_foreach(gt_link_get_history(gt_link_get_default()), save_ascii_record, &s);
	for(gsize i = 0; i < s.text->len; i++)
	{
		guchar c = (guchar)s.text->str[i];

		if(c > 0x1F || c == 0x0A || c == 0x0D)
			fputc(c, f);
	}
	g_string_free(s.text, TRUE);
	gt_rx_transform_free(s.transform);
}

void save_raw_file(GtkAction *action, gpointer data)
{
	GtkWidget *file_select;

	file_select = gtk_file_chooser_dialog_new(_("Save RAW File"),
	              GTK_WINDOW(Fenetre),
	              GTK_FILE_CHOOSER_ACTION_SAVE,
	              _("Cancel"), GTK_RESPONSE_CANCEL,
	              _("Save"), GTK_RESPONSE_ACCEPT,
	              NULL);
	gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(file_select), TRUE);

	if(fic_defaut != NULL)
		gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(file_select), fic_defaut);

	if(gtk_dialog_run(GTK_DIALOG(file_select)) == GTK_RESPONSE_ACCEPT)
	{
		gchar *fileName;
		gchar *msg;

		fileName = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(file_select));
		if (!fileName || strcmp(fileName, "") == 0)
		{
			msg = g_strdup_printf(_("File error\n"));
			show_message(msg, MSG_ERR);
			g_free(msg);
			g_free(fileName);
			gtk_widget_destroy(file_select);
			return;
		}

		Fic = fopen(fileName, "w");
		if(Fic == NULL)
		{
			msg = g_strdup_printf(_("Cannot open file %s: %s\n"), fileName, strerror(errno));
			show_message(msg, MSG_ERR);
			g_free(msg);
		}
		else
		{
			fic_defaut = g_strdup(fileName);

			write_raw_history(Fic);

			fclose(Fic);
		}
		g_free(fileName);
	}
	gtk_widget_destroy(file_select);
}

void save_ascii_file(GtkAction *action, gpointer data)
{
	GtkWidget *file_select;

	file_select = gtk_file_chooser_dialog_new(_("Save ASCII File"),
	              GTK_WINDOW(Fenetre),
	              GTK_FILE_CHOOSER_ACTION_SAVE,
	              _("Cancel"), GTK_RESPONSE_CANCEL,
	              _("Save"), GTK_RESPONSE_ACCEPT,
	              NULL);
	gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(file_select), TRUE);

	if(fic_defaut != NULL)
		gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(file_select), fic_defaut);

	if(gtk_dialog_run(GTK_DIALOG(file_select)) == GTK_RESPONSE_ACCEPT)
	{
		gchar *fileName;
		gchar *msg;

		fileName = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(file_select));
		if (!fileName || strcmp(fileName, "") == 0)
		{
			msg = g_strdup_printf(_("File error\n"));
			show_message(msg, MSG_ERR);
			g_free(msg);
			g_free(fileName);
			gtk_widget_destroy(file_select);
			return;
		}

		Fic = fopen(fileName, "w");
		if(Fic == NULL)
		{
			msg = g_strdup_printf(_("Cannot open file %s: %s\n"), fileName, strerror(errno));
			show_message(msg, MSG_ERR);
			g_free(msg);
		}
		else
		{
			fic_defaut = g_strdup(fileName);

			write_ascii_history(Fic);

			fclose(Fic);
		}
		g_free(fileName);
	}
	gtk_widget_destroy(file_select);
}

