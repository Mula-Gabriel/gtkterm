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

/* --- Gestion des listes globales --- */
static GPtrArray *macro_lists = NULL;

static void
list_entry_free (gpointer data)
{
  list_entry_t *e = (list_entry_t *) data;
  if (e)
    {
      g_free (e->display);
      g_free (e->value);
      g_free (e);
    }
}

static void
macro_list_free (gpointer data)
{
  macro_list_t *ml = (macro_list_t *) data;
  if (ml)
    {
      g_free (ml->name);
      g_ptr_array_unref (ml->entries);
      g_free (ml);
    }
}

void
macro_lists_init (void)
{
  if (!macro_lists)
    macro_lists = g_ptr_array_new_with_free_func (macro_list_free);
}

void
macro_lists_free (void)
{
  if (macro_lists)
    {
      g_ptr_array_unref (macro_lists);
      macro_lists = NULL;
    }
}

gint
macro_list_find (const gchar *name)
{
  if (!macro_lists || !name)
    return -1;
  for (guint i = 0; i < macro_lists->len; i++)
    {
      macro_list_t *ml = g_ptr_array_index (macro_lists, i);
      if (g_strcmp0 (ml->name, name) == 0)
        return (gint) i;
    }
  return -1;
}

void
macro_list_add (const gchar *name, const gchar *display, const gchar *value)
{
  if (!name || !display)
    return;

  macro_lists_init ();

  gint idx = macro_list_find (name);
  macro_list_t *ml;

  if (idx < 0)
    {
      ml = g_new0 (macro_list_t, 1);
      ml->name = g_strdup (name);
      ml->entries = g_ptr_array_new_with_free_func (list_entry_free);
      g_ptr_array_add (macro_lists, ml);
    }
  else
    {
      ml = g_ptr_array_index (macro_lists, idx);
    }

  list_entry_t *entry = g_new0 (list_entry_t, 1);
  entry->display = g_strdup (display);
  entry->value = g_strdup (value ? value : display);
  g_ptr_array_add (ml->entries, entry);
}

void
macro_list_remove_entry (gint list_idx, gint entry_idx)
{
  if (!macro_lists || list_idx < 0 || (guint) list_idx >= macro_lists->len)
    return;
  macro_list_t *ml = g_ptr_array_index (macro_lists, list_idx);
  if (entry_idx < 0 || (guint) entry_idx >= ml->entries->len)
    return;
  g_ptr_array_remove_index (ml->entries, entry_idx);
}

gint
macro_list_entry_count (gint list_idx)
{
  if (!macro_lists || list_idx < 0 || (guint) list_idx >= macro_lists->len)
    return 0;
  macro_list_t *ml = (macro_list_t *) g_ptr_array_index (macro_lists, list_idx);
  return (gint) ml->entries->len;
}

const gchar *
macro_list_entry_display (gint list_idx, gint entry_idx)
{
  if (!macro_lists || list_idx < 0 || (guint) list_idx >= macro_lists->len)
    return NULL;
  macro_list_t *ml = (macro_list_t *) g_ptr_array_index (macro_lists, list_idx);
  if (entry_idx < 0 || (guint) entry_idx >= ml->entries->len)
    return NULL;
  return ((list_entry_t *) g_ptr_array_index (ml->entries, entry_idx))->display;
}

const gchar *
macro_list_entry_value (gint list_idx, gint entry_idx)
{
  if (!macro_lists || list_idx < 0 || (guint) list_idx >= macro_lists->len)
    return NULL;
  macro_list_t *ml = (macro_list_t *) g_ptr_array_index (macro_lists, list_idx);
  if (entry_idx < 0 || (guint) entry_idx >= ml->entries->len)
    return NULL;
  return ((list_entry_t *) g_ptr_array_index (ml->entries, entry_idx))->value;
}

gint
macro_list_count (void)
{
  return macro_lists ? (gint) macro_lists->len : 0;
}

const gchar *
macro_list_name (gint list_idx)
{
  if (!macro_lists || list_idx < 0 || (guint) list_idx >= macro_lists->len)
    return NULL;
  macro_list_t *ml = (macro_list_t *) g_ptr_array_index (macro_lists, list_idx);
  return ml->name;
}
