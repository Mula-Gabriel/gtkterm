/* Barre de recherche dans le terminal (sous-projet 6a, §6.3) */
#ifndef GT_UI_SEARCH_BAR_H_
#define GT_UI_SEARCH_BAR_H_

#include <gtk/gtk.h>
#include <vte/vte.h>

G_BEGIN_DECLS

GtkWidget *gt_search_bar_new(VteTerminal *vte);	/* GtkSearchBar « search-bar », fermée */
void       gt_search_bar_toggle(GtkWidget *bar);	/* ouvre ou ferme selon search-mode-enabled */
/* action find (bascule de cette barre) dans le groupe de la session, relayée en app.find ; retirée par gt_session_view_shutdown */
void       gt_search_bar_install_action(GActionMap *tab_actions, GtkWidget *bar);

G_END_DECLS

#endif
