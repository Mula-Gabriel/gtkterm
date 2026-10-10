/* macro_view.h — règles d'affichage des macros dans le panneau (sans GTK) : onglets, sorte de ligne de chaque macro, macros d'un onglet.
 * Utilisé par le panneau GTK 4 (src/ui/macro_panel.c). Fil principal seulement. */

#ifndef MACRO_VIEW_H_
#define MACRO_VIEW_H_

#include <glib.h>

#include "macro_set.h"

typedef enum
{
	GT_MACRO_VIEW_HIDDEN,	/* sans libellé ou sans action : absente du panneau (son raccourci reste actif) */
	GT_MACRO_VIEW_SEPARATOR,	/* « %& » ou « [Titre] %& » */
	GT_MACRO_VIEW_BUTTON,	/* sans argument : un bouton */
	GT_MACRO_VIEW_TWO_VALUES,	/* un seul argument « %#Liste », liste d'exactement deux entrées : deux boutons */
	GT_MACRO_VIEW_ARGS	/* un bouton et un champ par argument */
} GtMacroViewKind;

gboolean        gt_macro_view_is_displayed(const GtMacro *m);	/* libellé et action non vides */
const gchar    *gt_macro_view_tab(const GtMacro *m, const gchar *default_tab);	/* m->tab, ou default_tab si vide */
/* Noms d'onglets distincts des macros affichées, dans l'ordre d'apparition, hors ceux de hidden (clés chaînes ; NULL : aucun).
 * Chaînes copiées ; g_ptr_array_unref. */
GPtrArray      *gt_macro_view_tabs(const GtMacroSet *set, const gchar *default_tab, GHashTable *hidden);
GtMacroViewKind gt_macro_view_kind(const GtMacroSet *set, const GtMacro *m);
/* Identifiants (guint) de toutes les macros de l'onglet, affichées ou non, dans l'ordre du jeu ; g_array_unref. */
GArray         *gt_macro_view_tab_ids(const GtMacroSet *set, const gchar *tab, const gchar *default_tab);

#endif
