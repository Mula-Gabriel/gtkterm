/* Vue du terminal GTK 4 (spec §6.3) : reprend terminal_display.c (GTK 3). Rendu par view_render.c depuis l'historique de la liaison
 * et GtTermOptions ; frappe → gt_link_send(GT_SEND_KEYBOARD). */
#ifndef GT_UI_TERMINAL_VIEW_H_
#define GT_UI_TERMINAL_VIEW_H_

#include <gtk/gtk.h>
#include <vte/vte.h>

#include "link.h"
#include "profile.h"
#include "term_options.h"

G_BEGIN_DECLS

GtkWidget   *gt_terminal_view_new(GtkTermLink *link, GtTermOptions *opts);	/* GtkScrolledWindow contenant le VteTerminal nommé "terminal" */
VteTerminal *gt_terminal_view_get_vte(GtkWidget *view);
void         gt_terminal_view_redraw(GtkWidget *view);	/* vide l'écran et le défilement puis rejoue l'historique */
void         gt_terminal_view_apply_profile(GtkWidget *view, const GtProfile *p);	/* police, couleurs, taille, défilement, curseur */
/* Dernière couleur de texte appliquée par gt_terminal_view_apply_profile (VTE n'a pas d'accesseur) ; FALSE avant la première application */
gboolean     gt_terminal_view_get_foreground(GtkWidget *view, GdkRGBA *out);
void         gt_terminal_view_clear_scrollback(GtkWidget *view);	/* défilement seulement (spec §3.2-11) */

/* Couleur d'un profil → GdkRGBA (vue du terminal, dialogue Terminal, panneau de script : une seule conversion) */
static inline void gt_color_to_rgba(const GtColor *c, GdkRGBA *out)
{
	out->red = (float)c->red;
	out->green = (float)c->green;
	out->blue = (float)c->blue;
	out->alpha = (float)c->alpha;
}

G_END_DECLS

#endif
