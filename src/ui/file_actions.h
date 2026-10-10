/* Actions de fichiers et de journal de l'interface GTK 4 (spec §6.2, §6.7) : app.send-file, app.save-raw, app.save-ascii, app.log-start,
 * app.log-paused (booléenne), app.log-stop, app.log-clear. */
#ifndef GT_UI_FILE_ACTIONS_H
#define GT_UI_FILE_ACTIONS_H

#include <gtk/gtk.h>

#include "link.h"
#include "term_options.h"

G_BEGIN_DECLS

/* Une fois, dans startup, après la création de la liaison par défaut et de GtTermOptions. initial_send_file : option -f (copiée), NULL sinon. */
void gt_file_actions_install(GtkApplication *app, GtkTermLink *link, GtTermOptions *opts, const gchar *initial_send_file);
/* Arrêt (en tête de app_stop_services : close-request, sinon shutdown ; avant l'arrêt de la liaison) : annule un envoi sans message, arrête le journal,
 * débranche tout. Idempotente. */
void gt_file_actions_shutdown(void);

G_END_DECLS

#endif
