/* État de fenêtre et de page (6a, partagé par le projet en 6c) : taille et maximisation de la fenêtre, séparateurs et visibilités d'une page. */
#ifndef WINDOW_STATE_H_
#define WINDOW_STATE_H_

#include <glib.h>

typedef struct
{
	gint width, height;	/* ≤ 0 : taille par défaut */
	gboolean maximized;
	gint h_paned, v_paned;	/* < 0 : position par défaut */
	gboolean macro_panel_visible, script_panel_visible, hex_bar_visible;
} GtWindowState;

#endif
