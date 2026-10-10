/* Barre d'envoi hexadécimal (sous-projet 6a, §6.3) : saisie analysée par hex_input.c, historique borné */
#ifndef GT_UI_HEX_SEND_BAR_H_
#define GT_UI_HEX_SEND_BAR_H_

#include <gtk/gtk.h>

#include "link.h"
#include "session.h"

G_BEGIN_DECLS

#define GT_HEX_SEND_HISTORY_MAX 50

/* GtkBox « hex-bar », cachée ; envoi sur link, messages dans la barre d'état de la page de session (pointeur faible) */
GtkWidget *gt_hex_send_bar_new(GtkTermLink *link, GtSession *session);
/* show-hex-bar (GPropertyAction sur visible) dans le groupe de la session (relayée en app.show-hex-bar) ; elle garde la barre en vie : à retirer à l'arrêt */
void       gt_hex_send_bar_install_action(GActionMap *tab_actions, GtkWidget *bar);

G_END_DECLS

#endif
