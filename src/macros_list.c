/***********************************************************************/
/* macros_list.c                                                       */
/* ------------                                                        */
/*           GTKTerm Software                                          */
/*                                                                     */
/*   Purpose                                                           */
/*      Named value-list data model (%#ListName in macro actions)    */
/*                                                                     */
/***********************************************************************/

#include <glib.h>

#include "macros_list.h"

typedef struct
{
  gchar *display;
  gchar *value;
} ListEntry;

typedef struct
{
  gchar *name;
  GPtrArray *entries;   /* ListEntry * */
} NamedList;

struct _GtMacroLists
{
  GPtrArray *lists;     /* NamedList *, dans l'ordre d'apparition */
};

static void
list_entry_free (gpointer data)
{
  ListEntry *e = data;

  g_free (e->display);
  g_free (e->value);
  g_free (e);
}

static void
named_list_free (gpointer data)
{
  NamedList *nl = data;

  g_free (nl->name);
  g_ptr_array_unref (nl->entries);
  g_free (nl);
}

static NamedList *
list_at (const GtMacroLists *lists, gint list_idx)
{
  if (lists == NULL || list_idx < 0 || (guint) list_idx >= lists->lists->len)
    return NULL;
  return g_ptr_array_index (lists->lists, list_idx);
}

static ListEntry *
entry_at (const GtMacroLists *lists, gint list_idx, gint entry_idx)
{
  NamedList *nl = list_at (lists, list_idx);

  if (nl == NULL || entry_idx < 0 || (guint) entry_idx >= nl->entries->len)
    return NULL;
  return g_ptr_array_index (nl->entries, entry_idx);
}

GtMacroLists *
gt_macro_lists_new (void)
{
  GtMacroLists *lists = g_new0 (GtMacroLists, 1);

  lists->lists = g_ptr_array_new_with_free_func (named_list_free);
  return lists;
}

void
gt_macro_lists_free (GtMacroLists *lists)
{
  if (lists == NULL)
    return;
  g_ptr_array_unref (lists->lists);
  g_free (lists);
}

GtMacroLists *
gt_macro_lists_copy (const GtMacroLists *lists)
{
  GtMacroLists *copy = gt_macro_lists_new ();

  for (gint li = 0; li < gt_macro_lists_count (lists); li++)
    {
      /* Une liste vide existe aussi dans la copie */
      gt_macro_lists_add_list (copy, gt_macro_lists_name (lists, li));
      for (gint ei = 0; ei < gt_macro_lists_entry_count (lists, li); ei++)
      gt_macro_lists_add (copy, gt_macro_lists_name (lists, li), gt_macro_lists_entry_display (lists, li, ei), gt_macro_lists_entry_value (lists, li, ei));
    }
  return copy;
}

gint
gt_macro_lists_find (const GtMacroLists *lists, const gchar *name)
{
  if (lists == NULL || name == NULL)
    return -1;
  for (guint i = 0; i < lists->lists->len; i++)
    {
      NamedList *nl = g_ptr_array_index (lists->lists, i);
      if (g_strcmp0 (nl->name, name) == 0)
        return (gint) i;
    }
  return -1;
}

gint
gt_macro_lists_add_list (GtMacroLists *lists, const gchar *name)
{
  gint idx;
  NamedList *nl;

  if (lists == NULL || name == NULL)
    return -1;
  idx = gt_macro_lists_find (lists, name);
  if (idx >= 0)
    return idx;
  nl = g_new0 (NamedList, 1);
  nl->name = g_strdup (name);
  nl->entries = g_ptr_array_new_with_free_func (list_entry_free);
  g_ptr_array_add (lists->lists, nl);
  return (gint) lists->lists->len - 1;
}

void
gt_macro_lists_add (GtMacroLists *lists, const gchar *name, const gchar *display, const gchar *value)
{
  gint idx;
  NamedList *nl;
  ListEntry *entry;

  if (lists == NULL || name == NULL || display == NULL)
    return;
  idx = gt_macro_lists_add_list (lists, name);
  nl = g_ptr_array_index (lists->lists, idx);

  entry = g_new0 (ListEntry, 1);
  entry->display = g_strdup (display);
  entry->value = g_strdup (value != NULL ? value : display);
  g_ptr_array_add (nl->entries, entry);
}

gint
gt_macro_lists_count (const GtMacroLists *lists)
{
  return lists != NULL ? (gint) lists->lists->len : 0;
}

const gchar *
gt_macro_lists_name (const GtMacroLists *lists, gint list_idx)
{
  NamedList *nl = list_at (lists, list_idx);

  return nl != NULL ? nl->name : NULL;
}

gint
gt_macro_lists_entry_count (const GtMacroLists *lists, gint list_idx)
{
  NamedList *nl = list_at (lists, list_idx);

  return nl != NULL ? (gint) nl->entries->len : 0;
}

const gchar *
gt_macro_lists_entry_display (const GtMacroLists *lists, gint list_idx, gint entry_idx)
{
  ListEntry *e = entry_at (lists, list_idx, entry_idx);

  return e != NULL ? e->display : NULL;
}

const gchar *
gt_macro_lists_entry_value (const GtMacroLists *lists, gint list_idx, gint entry_idx)
{
  ListEntry *e = entry_at (lists, list_idx, entry_idx);

  return e != NULL ? e->value : NULL;
}
