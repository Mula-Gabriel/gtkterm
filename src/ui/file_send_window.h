/* Fenêtre de progression d'un envoi de fichier (spec §6.7) : transitoire et modale sur la fenêtre principale, barre de progression, Cancel. */
#ifndef GT_UI_FILE_SEND_WINDOW_H
#define GT_UI_FILE_SEND_WINDOW_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef void (*GtFileSendCancelCb)(gpointer user_data);

/* Fenêtre nommée « file-send-window », titre « <display_name>: transfer in progress... », barre à « 0 / total » (taille du fichier). Cancel ou
 * fermeture par la barre de titre appellent cancel (la fenêtre reste ouverte : c'est la fin de l'envoi qui la ferme). */
GtkWidget *gt_file_send_window_new(GtkWindow *parent, const gchar *display_name, guint64 total, GtFileSendCancelCb cancel, gpointer user_data);
void       gt_file_send_window_set_progress(GtkWidget *win, guint64 sent, guint64 total);
/* Détruit la fenêtre sans appeler cancel. */
void       gt_file_send_window_close(GtkWidget *win);

G_END_DECLS

#endif
