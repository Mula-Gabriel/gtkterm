/***********************************************************************/
/* macros_list.h                                                       */
/* ------------                                                        */
/*           GTKTerm Software                                          */
/*                                                                     */
/*   Purpose                                                           */
/*      Named value-list data model used by %#ListName in macros     */
/*                                                                     */
/***********************************************************************/

#ifndef MACROS_LIST_H_
#define MACROS_LIST_H_

#include <glib.h>

typedef struct
{
  gchar *display;
  gchar *value;
} list_entry_t;

typedef struct
{
  gchar *name;
  GPtrArray *entries;
} macro_list_t;

void         macro_lists_init        (void);
void         macro_lists_free        (void);
gint         macro_list_find         (const gchar *name);
void         macro_list_add          (const gchar *name,
                                      const gchar *display,
                                      const gchar *value);
void         macro_list_remove_entry (gint list_idx, gint entry_idx);
gint         macro_list_entry_count  (gint list_idx);
const gchar *macro_list_entry_display(gint list_idx, gint entry_idx);
const gchar *macro_list_entry_value  (gint list_idx, gint entry_idx);
gint         macro_list_count        (void);
const gchar *macro_list_name         (gint list_idx);

#endif
