/* Panneau de script Lua de l'interface GTK 4 (spec §6.6) : éditeur et console GtkSourceView 5, Open/Save, Run/Stop, couleurs, disposition. */
#ifndef GT_UI_SCRIPT_PANEL_H
#define GT_UI_SCRIPT_PANEL_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* Une seule instance (activate) : lit [colors], [layout], [session], installe app.script-layout, branche le journal et la fin du moteur Lua. */
GtkWidget *gt_script_panel_new(GtkApplication *app);
/* Couleurs non choisies ← couleurs du terminal du profil en cours (config) ; schéma réappliqué. Sans panneau : rien. */
void       gt_script_panel_refresh_colors(void);
/* [colors] (couleurs choisies seulement), [layout], [session] ; relit le magasin avant ; rien d'écrit en lecture seule. Sans panneau : rien. */
void       gt_script_panel_save_state(void);
/* Mêmes groupes écrits dans kf, sans relire ni enregistrer (close-request : app_config_save_window_state fait la seule écriture). Sans panneau : rien. */
void       gt_script_panel_store_state(GKeyFile *kf);

G_END_DECLS

#endif
