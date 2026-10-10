/* Actions de fichiers et de journal de l'interface GTK 4 (spec §6.2, §6.7 ; par page d'onglet en 6b) : send-file, save-raw, save-ascii, log-start,
 * log-paused (booléenne), log-stop, log-clear, dans le groupe d'actions de la session (relayées en app.*). */
#ifndef GT_UI_FILE_ACTIONS_H
#define GT_UI_FILE_ACTIONS_H

#include <gtk/gtk.h>

#include "session.h"

G_BEGIN_DECLS

/* Une fois par page : état attaché à view, actions ajoutées au groupe d'actions de s, journal et envoi sur la liaison de s ; s tient
 * « file-sending » pendant un envoi (gt_session_set_file_sending). initial_send_file : option -f (copiée), NULL sinon. Raccourci de send-file :
 * gt_app_actions_set_accels. */
void gt_file_actions_new(GtkWidget *view, GtSession *s, const gchar *initial_send_file);
/* Arrêt de la page (gt_session_view_shutdown, avant l'arrêt de la session) : annule un envoi sans message, arrête le journal, débranche tout,
 * retire les actions du groupe de la session. Idempotente ; sans gt_file_actions_new : rien. */
void gt_file_actions_shutdown(GtkWidget *view);

G_END_DECLS

#endif
