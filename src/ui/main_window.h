/* Fenêtre principale GTK 4 (spec §6.1). De haut en bas : barre de menus (menus.ui), emplacement « search-bar », GtkPaned vertical
 * [GtkPaned horizontal (terminal | emplacement « macro-panel ») / emplacement « script-panel »], emplacements « hex-bar » et
 * « status-bar ». app.c (barres d'état, de recherche et d'envoi hexadécimal, panneau de script) et macro_panel.c remplissent les emplacements. */
#ifndef GT_UI_MAIN_WINDOW_H_
#define GT_UI_MAIN_WINDOW_H_

#include <gtk/gtk.h>

#include "app_config.h"

G_BEGIN_DECLS

/* GtkApplicationWindow nommée « main-window » ; taille, maximisation et séparateurs restaurés depuis app_config_load_window_state */
GtkWidget *gt_main_window_new(GtkApplication *app);
GtkWidget *gt_main_window_get_terminal_view(GtkWidget *win);
void       gt_main_window_focus_terminal(GtkWidget *win);	/* focus clavier au VteTerminal (fermeture d'un popover, d'une barre, d'un dialogue) */
/* slots : "macro-panel", "script-panel", "hex-bar", "status-bar", "search-bar" ; child remplace le précédent (NULL : vide).
 * « macro-panel », « script-panel » et « hex-bar » prennent la visibilité enregistrée dans [window]. */
void       gt_main_window_set_slot(GtkWidget *win, const char *slot, GtkWidget *child);
GtkWidget *gt_main_window_get_slot(GtkWidget *win, const char *slot);
/* État à enregistrer ; un emplacement vide garde la valeur lue au démarrage (visibilité, séparateur) */
void       gt_main_window_get_state(GtkWidget *win, GtWindowState *out);

G_END_DECLS

#endif
