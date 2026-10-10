/* Actions app.* d'options, de vue, de liaison et d'édition (sous-projet 6a, §6.2) et leurs raccourcis.
 * Les actions à état sont des GPropertyAction sur GtTermOptions et sur la liaison : aucune copie d'état. */
#ifndef GT_UI_APP_ACTIONS_H_
#define GT_UI_APP_ACTIONS_H_

#include <gtk/gtk.h>

#include "link.h"
#include "term_options.h"

G_BEGIN_DECLS

/* startup, après la création de la liaison par défaut et des options : actions, raccourcis, suivi de la liaison */
void gt_app_actions_install(GtkApplication *app, GtkTermLink *link, GtTermOptions *opts);
/* activate, après la fenêtre principale : Copy/Paste/Select All/Clear scrollback visent ce terminal ; menu contextuel « terminal-context » */
void gt_app_actions_attach_terminal(GtkApplication *app, GtkWidget *terminal_view);
/* close-request : plus aucune action ne vise le terminal */
void gt_app_actions_detach_terminal(void);
/* shutdown : déconnecte la liaison, rend les références */
void gt_app_actions_shutdown(void);

G_END_DECLS

#endif
