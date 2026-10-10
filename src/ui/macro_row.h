/* macro_row.h — lignes de la fenêtre « Configure Macros » (interface GTK 4) : objets de GListStore, sans GTK.
 * GtMacroRow : une macro (identifiant stable, 0 = nouvelle) ; GtMacroListRow : une entrée de liste nommée. Chaînes jamais NULL. */

#ifndef GT_UI_MACRO_ROW_H_
#define GT_UI_MACRO_ROW_H_

#include <gio/gio.h>

#include "macro_set.h"

G_BEGIN_DECLS

#define GT_TYPE_MACRO_ROW (gt_macro_row_get_type())
G_DECLARE_FINAL_TYPE(GtMacroRow, gt_macro_row, GT, MACRO_ROW, GObject)

/* propriétés : "id" (guint, construction seulement), "label", "shortcut", "action", "tab" (chaînes, NULL → "", notify seulement si changé) */
GtMacroRow  *gt_macro_row_new(guint id, const gchar *label, const gchar *shortcut, const gchar *action, const gchar *tab);
guint        gt_macro_row_get_id(GtMacroRow *row);
const gchar *gt_macro_row_get_label(GtMacroRow *row);
const gchar *gt_macro_row_get_shortcut(GtMacroRow *row);
const gchar *gt_macro_row_get_action(GtMacroRow *row);
const gchar *gt_macro_row_get_tab(GtMacroRow *row);
void         gt_macro_row_set_shortcut(GtMacroRow *row, const gchar *shortcut);

#define GT_TYPE_MACRO_LIST_ROW (gt_macro_list_row_get_type())
G_DECLARE_FINAL_TYPE(GtMacroListRow, gt_macro_list_row, GT, MACRO_LIST_ROW, GObject)

/* propriétés : "list", "display", "value" (chaînes, NULL → "", notify seulement si changé) */
GtMacroListRow *gt_macro_list_row_new(const gchar *list, const gchar *display, const gchar *value);
const gchar    *gt_macro_list_row_get_list(GtMacroListRow *row);
const gchar    *gt_macro_list_row_get_display(GtMacroListRow *row);
const gchar    *gt_macro_list_row_get_value(GtMacroListRow *row);

GListStore   *gt_macro_rows_from_set(const GtMacroSet *set);	/* GtMacroRow dans l'ordre du jeu ; NULL : vide */
GListStore   *gt_macro_list_rows_from_lists(const GtMacroLists *lists);	/* GtMacroListRow, une par entrée, listes dans l'ordre ; NULL : vide */
/* Lignes → GtMacroEdit dans l'ordre affiché ; chaînes empruntées aux lignes (valides tant que le modèle les garde) ; g_free du tableau */
GtMacroEdit  *gt_macro_rows_to_edits(GListModel *rows, guint *n_out);
GtMacroLists *gt_macro_list_rows_to_lists(GListModel *rows);	/* regroupées par nom (gt_macro_lists_add) ; jamais NULL */
guint         gt_macro_rows_insert_position(const guint *selected, guint n_selected, guint n_items);	/* après la dernière sélectionnée, sinon à la fin */
/* Déplace d'un rang (delta -1 ou +1) chaque élément aux positions données (croissantes, distinctes) et met les positions à jour.
 * FALSE, rien ne bouge : aucune position, delta invalide, ou une position au bord dans le sens demandé. Tout type d'objet. */
gboolean      gt_macro_rows_move(GListStore *store, guint *positions, guint n, gint delta);
void          gt_macro_rows_remove(GListStore *store, const guint *positions, guint n);	/* positions croissantes */

G_END_DECLS

#endif
