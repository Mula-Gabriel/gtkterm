/* Panneau de script Lua de l'interface GTK 4 (spec §6.6) : éditeur et console GtkSourceView 5, Open/Save, Run/Stop, couleurs, disposition.
 * Un par page d'onglet (sous-projet 6b) : chacun sert le moteur Lua de sa session. */
#ifndef GT_UI_SCRIPT_PANEL_H
#define GT_UI_SCRIPT_PANEL_H

#include <gtk/gtk.h>

#include "session.h"

G_BEGIN_DECLS

/* Panneau « script-panel » sur le moteur de s (gt_session_get_script_engine) : lit [colors] ; layout_from NULL (première page) : [layout] et
 * [session] lus ; sinon disposition copiée du panneau layout_from (page active), éditeur au modèle par défaut. Ajoute script-layout et
 * show-script-panel (GPropertyAction sur visible) au groupe d'actions de la session (tab_actions, relayées en app.*), branche le journal et la
 * fin du moteur. */
GtkWidget *gt_script_panel_new(GtkApplication *app, GtSession *s, GActionMap *tab_actions, GtkWidget *layout_from);
/* Arrêt de la page (après gt_script_panel_store_state à la fermeture) : journal et fin du moteur débranchés, fenêtre des couleurs fermée,
 * script-layout et show-script-panel retirées du groupe. Le moteur lui-même est arrêté par gt_session_shutdown. Idempotent ; NULL accepté. */
void       gt_script_panel_shutdown(GtkWidget *panel);
/* Couleurs non choisies ← couleurs du terminal du profil de sa session ; schéma réappliqué. Arrêté ou NULL : rien. « Colors… » → Apply d'un
 * panneau : [colors] enregistré, puis les couleurs choisies passent aux panneaux des autres pages (gt_app_foreach_view), rafraîchis ainsi. */
void       gt_script_panel_refresh_colors(GtkWidget *panel);
/* [colors] (couleurs choisies seulement), [layout], [session] de ce panneau écrits dans kf, sans relire ni enregistrer (close-request : panneau
 * de la page active ; app_config_save_window_state fait la seule écriture). [session] script_path : écrit si le panneau a un chemin, jamais
 * retiré sinon (un nouvel onglet n'efface pas le script de la première page). NULL : rien. */
void       gt_script_panel_store_state(GtkWidget *panel, GKeyFile *kf);
/* Disposition « topbottom » | « leftright » (comme app.script-layout) ; get : « topbottom » sans panneau */
void        gt_script_panel_set_layout(GtkWidget *panel, const gchar *layout);
const gchar *gt_script_panel_get_layout(GtkWidget *panel);

G_END_DECLS

#endif
