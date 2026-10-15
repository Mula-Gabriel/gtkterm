/* macro_editor.h — fenêtre « Configure Macros » (interface GTK 4) : une par session (sous-projet 6b, spec §3.2-12). */

#ifndef GT_UI_MACRO_EDITOR_H_
#define GT_UI_MACRO_EDITOR_H_

#include <gtk/gtk.h>

#include "session.h"

void gt_macro_editor_install_action(GtkApplication *app);	/* action app.macros : éditeur de la session active */
/* Éditeur des macros de s (session gardée par référence faible ; rangé sur la session : une seconde demande présente la fenêtre ouverte), titre
 * « Configure Macros — <nom court> ». Session arrêtée : rien. La fenêtre se ferme sans rien appliquer quand s émet « closed ». */
void gt_macro_editor_open(GtkWindow *parent, GtSession *s);
void gt_macro_editor_macros_changed(GtSession *s);	/* crochet macros_changed : jeu de s remplacé pendant l'édition → fermeture sans enregistrer */

/* Fenêtre d'édition de s fermée sans rien appliquer (sans close-request) : modèle relu en dehors d'elle (onglet frère, spec modèles §2.8) */
void gt_macro_editor_close(GtSession *s);

#endif
