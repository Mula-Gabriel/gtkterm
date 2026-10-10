/* Barre d'envoi hexadécimal (sous-projet 6a, §6.3) : saisie analysée par hex_input.c, historique borné */
#ifndef GT_UI_HEX_SEND_BAR_H_
#define GT_UI_HEX_SEND_BAR_H_

#include <gtk/gtk.h>

#include "link.h"

G_BEGIN_DECLS

#define GT_HEX_SEND_HISTORY_MAX 50

GtkWidget *gt_hex_send_bar_new(GtkTermLink *link);	/* GtkBox « hex-bar », cachée */
void       gt_hex_send_bar_install_action(GtkApplication *app, GtkWidget *bar);	/* app.show-hex-bar (GPropertyAction sur visible) */

G_END_DECLS

#endif
