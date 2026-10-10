/* Application GTK 4 (sous-projet 6a) : GtkApplication, actions app.*, démarrage et fermeture uniques (spec §4.4). */
#ifndef GT_UI_APP_H_
#define GT_UI_APP_H_

#include <gtk/gtk.h>

#include "cmdline.h"
#include "link.h"
#include "term_options.h"

G_BEGIN_DECLS

#define GT_APP_ID "eu.mulagabriel.gtkterm"

/* Identifiant GT_APP_ID et G_APPLICATION_NON_UNIQUE ; avec le crochet de test actif (GTKTERM_TEST_HOOKS=1) :
 * « eu.mulagabriel.gtkterm.test.p<PID> », application unique sur le bus de session (le harnais la joint par org.gtk.Actions).
 * cmd reste à l'appelant et doit vivre jusqu'au retour de g_application_run. */
GtkApplication *gt_app_new(const GtCmdline *cmd);
GtkTermLink    *gt_app_get_link(void);		/* liaison par défaut (gt_link_get_default) */
GtTermOptions  *gt_app_get_options(void);	/* créé dans « startup » ; NULL avant */
GtkWindow      *gt_app_get_main_window(void);	/* NULL avant « activate » et après la fermeture */
void            gt_app_open_port(void);		/* port du profil en cours ; échec → signal open-failed de la liaison → gt_ui_message */
void            gt_app_close_port(void);
void            gt_app_apply_profile(void);	/* options ← config, terminal, auto-reconnect ; ne rouvre pas le port */

G_END_DECLS

#endif
