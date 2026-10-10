/* macro_shortcuts.h — raccourcis clavier des macros (interface GTK 4) : un contrôleur par page d'onglet (sous-projet 6b, plan, décision 13). */

#ifndef GT_UI_MACRO_SHORTCUTS_H_
#define GT_UI_MACRO_SHORTCUTS_H_

#include <gtk/gtk.h>

#include "session.h"

/* Contrôleur de portée locale en phase de capture sur la page view, rempli depuis le jeu de s ; une seule fois par page */
void gt_macro_shortcuts_attach(GtkWidget *view, GtSession *s);
void gt_macro_shortcuts_rebuild(GtkWidget *view);	/* crochet macros_changed de la session ; sans attach ou après detach : rien */
void gt_macro_shortcuts_detach(GtkWidget *view);	/* arrêt de la page : plus aucune entrée (le contrôleur part avec la page) ; idempotent */

#endif
