/***********************************************************************/
/* macros_list.h                                                       */
/* ------------                                                        */
/*           GTKTerm Software                                          */
/*                                                                     */
/*   Purpose                                                           */
/*      Named value lists used by %#ListName in macros (one instance  */
/*      per macro set, no global state)                               */
/*                                                                     */
/***********************************************************************/

#ifndef MACROS_LIST_H_
#define MACROS_LIST_H_

#include <glib.h>

typedef struct _GtMacroLists GtMacroLists;

GtMacroLists *gt_macro_lists_new           (void);
void          gt_macro_lists_free          (GtMacroLists *lists);
GtMacroLists *gt_macro_lists_copy          (const GtMacroLists *lists);
gint          gt_macro_lists_find          (const GtMacroLists *lists, const gchar *name);
void          gt_macro_lists_add           (GtMacroLists *lists, const gchar *name, const gchar *display, const gchar *value);
gint          gt_macro_lists_count         (const GtMacroLists *lists);
const gchar  *gt_macro_lists_name          (const GtMacroLists *lists, gint list_idx);
gint          gt_macro_lists_entry_count   (const GtMacroLists *lists, gint list_idx);
const gchar  *gt_macro_lists_entry_display (const GtMacroLists *lists, gint list_idx, gint entry_idx);
const gchar  *gt_macro_lists_entry_value   (const GtMacroLists *lists, gint list_idx, gint entry_idx);

#endif
