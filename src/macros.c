/***********************************************************************/
/* macros.c                                                            */
/* --------                                                            */
/*           GTKTerm Software                                          */
/*                      (c) Julien Schmitt                             */
/*                                                                     */
/* ------------------------------------------------------------------- */
/*                                                                     */
/*   Purpose                                                           */
/*      Functions for the management of the macros                     */
/*                                                                     */
/*   ChangeLog                                                         */
/*      - 0.99.2 : Internationalization                                */
/*      - 0.99.0 : file creation by Julien                             */
/*                                                                     */
/***********************************************************************/

#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interface.h"
#include "link.h"
#include "macros.h"
#include "macros_list.h"
#include "macros_format.h"
#include "macro_file.h"
#include "macro_panel.h"
#include "term_config.h"

#include <config.h>
#include <glib/gi18n.h>

enum
{
  COLUMN_SHORTCUT,
  COLUMN_LABEL,
  COLUMN_ACTION,
  COLUMN_TAB,
  COLUMN_ID,
  NUM_COLUMNS
};

enum
{
  LIST_COLUMN_NAME,
  LIST_COLUMN_DISPLAY,
  LIST_COLUMN_VALUE,
  LIST_NUM_COLUMNS
};

static GtkWidget *window = NULL;
static GtkTreeModel *lists_model = NULL;

static GtMacroSet *current_set = NULL;
static GtMacroPoller *poller = NULL;
static gchar *current_path = NULL;
static GPtrArray *closures = NULL;		/* GClosure * connectées à connected_group */
static GtkAccelGroup *connected_group = NULL;
static gchar *last_save_error = NULL;		/* dernier échec d'enregistrement signalé (chemin, domaine, code) : une seule boîte par échec */

static gboolean
poller_send (guint id, gpointer user_data)
{
  return macros_send (id, NULL, -1);
}

static void
poller_state (guint id, gboolean running, gpointer user_data)
{
  macro_panel_polling_changed (id, running);
}

void
macros_init (void)
{
  if (current_set != NULL)
    return;
  current_set = gt_macro_set_new ();
  poller = gt_macro_poller_new (current_set, poller_send, poller_state, NULL);
}

GtMacroSet *
macros_get_set (void)
{
  return current_set;
}

GtMacroPoller *
macros_get_poller (void)
{
  return poller;
}

gboolean
macros_send (guint id, const gchar *const *args, gint n_args)
{
  const GtMacro *m = gt_macro_set_find (current_set, id);
  GBytes *bytes;
  const guint8 *data;
  gsize len = 0;
  gssize written = 0;
  gchar *message;

  if (!gt_macro_is_sendable (m))
    return FALSE;
  /* Nom affiché : le libellé, à défaut le raccourci (C8) ; pris avant l'envoi, dont les gestionnaires ne doivent pas toucher au jeu */
  message = g_strdup_printf (_ ("Macro \"%s\" sent!"), m->label[0] != '\0' ? m->label : m->shortcut);
  bytes = gt_macro_expand (current_set, m, args, n_args);
  data = g_bytes_get_data (bytes, &len);
  if (len > 0)
    written = gt_link_send (gt_link_get_default (), data, len, GT_SEND_MACRO);
  g_bytes_unref (bytes);
  /* Rien d'écrit (port fermé ou perdu) : pas de « envoyée », le « — lost » de la barre d'état reste visible */
  if (written > 0)
    Put_temp_message (message, 800);
  g_free (message);
  return len == 0 || (written >= 0 && (gsize) written == len);
}

/* Rappel d'un raccourci (g_cclosure_new_swap : la donnée arrive en premier). TRUE : la touche est traitée et n'atteint pas
   le terminal (M43) ; macro disparue ou non envoyable : FALSE, la touche suit son cours. Le résultat de l'écriture n'y change
   rien (§6.2) : port fermé ou perdu, la touche d'une macro envoyable reste consommée, elle ne part pas dans le terminal. */
static gboolean
shortcut_cb (gpointer data)
{
  guint id = GPOINTER_TO_UINT (data);
  gboolean sendable = gt_macro_is_sendable (gt_macro_set_find (current_set, id));

  if (sendable)
    macros_send (id, NULL, -1);
  return sendable;
}

static void
shortcuts_disconnect (void)
{
  if (closures == NULL || connected_group == NULL)
    return;
  for (guint i = 0; i < closures->len; i++)
    gtk_accel_group_disconnect (connected_group, g_ptr_array_index (closures, i));
  g_ptr_array_set_size (closures, 0);
  connected_group = NULL;
}

/* Une seule connexion par macro (M17), par identifiant : un raccourci suit sa macro quand l'ordre change */
static void
shortcuts_connect (void)
{
  shortcuts_disconnect ();
  if (shortcuts == NULL || current_set == NULL)
    return;
  if (closures == NULL)
    closures = g_ptr_array_new ();
  connected_group = shortcuts;
  for (guint i = 0; i < gt_macro_set_count (current_set); i++)
    {
      const GtMacro *m = gt_macro_set_get (current_set, i);
      guint key = 0;
      GdkModifierType mods = 0;
      GClosure *closure;

      if (m->shortcut[0] == '\0')
        continue;
      gtk_accelerator_parse (m->shortcut, &key, &mods);
      if (key == 0)
        continue;
      closure = g_cclosure_new_swap (G_CALLBACK (shortcut_cb), GUINT_TO_POINTER (m->id), NULL);
      gtk_accel_group_connect (connected_group, key, mods, GTK_ACCEL_MASK, closure);
      g_ptr_array_add (closures, closure);
    }
}

void
macros_changed (void)
{
  shortcuts_connect ();
  rebuild_macro_buttons ();
}

void
macros_replace_set (GtMacroSet *set)
{
  GtMacroSet *old;

  macros_init ();
  /* Fenêtre d'édition ouverte : ses lignes et ses listes décrivent l'ancien jeu. La valider ou la fermer ensuite appliquerait ces
     lignes au nouveau jeu (identifiants inconnus : toutes nouvelles, arguments et polling perdus) et les enregistrerait dans le
     nouveau fichier. Elle est donc fermée sans enregistrer : gtk_widget_destroy n'émet pas delete-event (pas de Save_shortcuts),
     et on_window_destroy remet window et lists_model à NULL. */
  if (window != NULL)
    gtk_widget_destroy (window);
  /* L'ordonnanceur s'arrête pendant que l'ancien jeu est encore le jeu courant : les rappels d'état voient des boutons cohérents */
  gt_macro_poller_set_set (poller, set);
  old = current_set;
  current_set = set;
  gt_macro_set_free (old);
  macros_changed ();
}

const gchar *
macros_file_get_path (void)
{
  return current_path;
}

gchar *
macros_file_default_path (void)
{
  return g_build_filename (g_get_user_config_dir (), "gtkterm_macros.ini", NULL);
}

static gchar *
resolve_path (const gchar *path)
{
  if (path != NULL)
    return g_strdup (path);
  if (current_path != NULL)
    return g_strdup (current_path);
  return macros_file_default_path ();
}

static void
report_error (gboolean interactive, const gchar *text)
{
  if (interactive)
    show_message ((gchar *) text, MSG_ERR);
  else
    g_warning ("%s", text);
}

static gboolean
load_error_idle (gpointer data)
{
  show_message (data, MSG_ERR);
  return G_SOURCE_REMOVE;
}

gboolean
macros_load_file (const gchar *path, gboolean interactive)
{
  gchar *load_path = resolve_path (path);
  GtMacroSet *set = NULL;
  GError *error = NULL;
  gboolean ok = TRUE;

  if (!gt_macro_file_load (load_path, &set, &error))
    {
      gboolean missing = g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT);

      if (!missing && interactive)
        {
          gchar *text = g_strdup_printf (_ ("Cannot load macros file: %s"), error->message);

          /* Choix dans le menu : jeu et chemin courants inchangés, rien n'est perdu */
          show_message (text, MSG_ERR);
          g_free (text);
          g_error_free (error);
          g_free (load_path);
          return FALSE;
        }
      if (!missing)
        {
          /* Profil (non interactif) : jeu vide, mais le chemin courant devient ce fichier. Les enregistrements suivants le visent
             (gt_macro_file_save en garde d'abord une copie <fichier>.avant-migration), jamais le fichier par défaut, et le profil
             n'est pas redirigé. Avertissement, plus une boîte différée : pas de boucle imbriquée pendant le chargement du profil,
             et l'utilisateur sait que ses macros n'ont pas été lues. */
          gchar *text = g_strdup_printf (_ ("Cannot load macros file: %s"), error->message);

          g_warning ("%s", text);
          g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, load_error_idle, text, g_free);
          ok = FALSE;
        }
      /* Fichier absent (premier lancement, profil qui pointe un fichier pas encore créé) : jeu vide, créé au premier enregistrement */
      g_clear_error (&error);
      set = gt_macro_set_new ();
    }
  g_free (current_path);
  current_path = load_path;
  g_clear_pointer (&last_save_error, g_free);
  macros_replace_set (set);
  return ok;
}

void
macros_forget_save_error (void)
{
  g_clear_pointer (&last_save_error, g_free);
}

gboolean
macros_save_file (const gchar *path, gboolean interactive)
{
  gchar *save_path = resolve_path (path);
  GError *error = NULL;

  if (!gt_macro_file_save (current_set, save_path, &error))
    {
      /* Chaque frappe dans un argument enregistre : un même échec n'ouvre qu'une boîte. L'échec est reconnu à son chemin et à son
         erreur, pas au texte : celui de g_file_set_contents cite un fichier temporaire au nom aléatoire, différent à chaque essai. */
      gchar *key = g_strdup_printf ("%s\n%s:%d", save_path, g_quark_to_string (error->domain), error->code);

      if (g_strcmp0 (key, last_save_error) != 0)
        {
          gchar *text = g_strdup_printf (_ ("Cannot save macros file: %s"), error->message);

          report_error (interactive, text);
          g_free (text);
        }
      g_free (last_save_error);
      last_save_error = key;
      g_error_free (error);
      g_free (save_path);
      return FALSE;
    }
  g_clear_pointer (&last_save_error, g_free);
  g_free (current_path);
  current_path = save_path;
  return TRUE;
}

static GtkTreeModel *
create_model (void)
{
  GtkListStore *store = gtk_list_store_new (NUM_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT);
  GtkTreeIter iter;

  for (guint i = 0; i < gt_macro_set_count (current_set); i++)
    {
      const GtMacro *m = gt_macro_set_get (current_set, i);

      gtk_list_store_append (store, &iter);
      gtk_list_store_set (store, &iter,
                          COLUMN_LABEL, m->label,
                          COLUMN_SHORTCUT, m->shortcut[0] != '\0' ? m->shortcut : _ ("None"),
                          COLUMN_ACTION, m->action,
                          COLUMN_TAB, m->tab,
                          COLUMN_ID, m->id,
                          -1);
    }
  return GTK_TREE_MODEL (store);
}

static gboolean
cell_focus_out (GtkWidget *entry, GdkEventFocus *event, gpointer data)
{
  gtk_widget_activate (entry);
  return FALSE;
}

static void
cell_editing_started (GtkCellRenderer *renderer, GtkCellEditable *editable,
                      const gchar *path, gpointer data)
{
  if (GTK_IS_ENTRY (editable))
    g_signal_connect (editable, "focus-out-event",
                      G_CALLBACK (cell_focus_out), NULL);
}

static void
cell_edited (GtkCellRendererText *cell, const gchar *path_string, const gchar *new_text, gpointer data)
{
  GtkTreeModel *model = data;
  gint column = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (cell), "column"));
  GtkTreePath *path = gtk_tree_path_new_from_string (path_string);
  GtkTreeIter iter;

  if (gtk_tree_model_get_iter (model, &iter, path))
    gtk_list_store_set (GTK_LIST_STORE (model), &iter, column, new_text, -1);
  gtk_tree_path_free (path);
}

/* Colonne texte ; editable : rendu relié à cell_edited sur le modèle de la vue */
static void
append_text_column (GtkTreeView *treeview, const gchar *title, gint column, gboolean editable, gboolean sortable, gint min_width)
{
  GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();
  GtkTreeViewColumn *col;

  if (editable)
    {
      g_object_set_data (G_OBJECT (renderer), "column", GINT_TO_POINTER (column));
      g_object_set (G_OBJECT (renderer), "editable", TRUE, NULL);
      g_signal_connect (renderer, "edited", G_CALLBACK (cell_edited), gtk_tree_view_get_model (treeview));
      g_signal_connect (renderer, "editing-started", G_CALLBACK (cell_editing_started), NULL);
    }
  col = gtk_tree_view_column_new_with_attributes (title, renderer, "text", column, NULL);
  if (sortable)
    gtk_tree_view_column_set_sort_column_id (col, column);
  gtk_tree_view_column_set_resizable (col, TRUE);
  gtk_tree_view_column_set_min_width (col, min_width);
  gtk_tree_view_append_column (treeview, col);
}

static void
add_columns (GtkTreeView *treeview)
{
  append_text_column (treeview, _ ("Label"), COLUMN_LABEL, TRUE, TRUE, 80);
  append_text_column (treeview, _ ("Shortcut"), COLUMN_SHORTCUT, FALSE, TRUE, 80);
  append_text_column (treeview, _ ("Action"), COLUMN_ACTION, TRUE, TRUE, 120);
  append_text_column (treeview, _ ("Tab"), COLUMN_TAB, TRUE, TRUE, 60);
}

static gint
Add_shortcut (GtkWidget *button, gpointer pointer)
{
  GtkTreeIter new_iter;
  GtkTreeView *treeview = (GtkTreeView *) pointer;
  GtkTreeModel *model = gtk_tree_view_get_model (treeview);
  GtkTreeSelection *selection = gtk_tree_view_get_selection (treeview);

  GList *list = gtk_tree_selection_get_selected_rows (selection, &model);
  gint max_idx = -1;
  for (GList *l = list; l; l = l->next)
    {
      GtkTreePath *path = (GtkTreePath *) l->data;
      gint row = gtk_tree_path_get_indices (path)[0];
      if (row > max_idx)
        max_idx = row;
      gtk_tree_path_free (path);
    }
  g_list_free (list);

  if (max_idx >= 0)
    {
      GtkTreePath *path = gtk_tree_path_new_from_indices (max_idx, -1);
      GtkTreeIter iter;
      gtk_tree_model_get_iter (model, &iter, path);
      gtk_list_store_insert_after (GTK_LIST_STORE (model), &new_iter, &iter);
      gtk_tree_path_free (path);
    }
  else
    {
      gtk_list_store_append (GTK_LIST_STORE (model), &new_iter);
    }

  gtk_list_store_set (GTK_LIST_STORE (model), &new_iter,
                      COLUMN_SHORTCUT, _ ("None"),
                      COLUMN_TAB, "",
                      COLUMN_ID, 0u,
                      -1);

  return FALSE;
}

static void
sort_indices_asc (gint *indices, gint count)
{
  for (gint i = 0; i < count - 1; i++)
    for (gint j = i + 1; j < count; j++)
      if (indices[i] > indices[j])
        { gint tmp = indices[i]; indices[i] = indices[j]; indices[j] = tmp; }
}

static gboolean
Delete_macro (GtkWidget *button, gpointer pointer)
{
  GtkTreeView *treeview = (GtkTreeView *) pointer;
  GtkTreeModel *model = gtk_tree_view_get_model (treeview);
  GtkTreeSelection *selection = gtk_tree_view_get_selection (treeview);

  GList *list = gtk_tree_selection_get_selected_rows (selection, &model);
  if (list == NULL)
    return FALSE;

  gint n = g_list_length (list);
  gint *indices = g_new (gint, n);
  gint idx_count = 0;
  for (GList *l = list; l; l = l->next)
    {
      GtkTreePath *path = (GtkTreePath *) l->data;
      indices[idx_count++] = gtk_tree_path_get_indices (path)[0];
      gtk_tree_path_free (path);
    }
  g_list_free (list);

  sort_indices_asc (indices, idx_count);

  for (gint k = idx_count - 1; k >= 0; k--)
    {
      GtkTreePath *path = gtk_tree_path_new_from_indices (indices[k], -1);
      GtkTreeIter iter;
      if (gtk_tree_model_get_iter (model, &iter, path))
        gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
      gtk_tree_path_free (path);
    }

  g_free (indices);
  return FALSE;
}

/* La liste est en sélection multiple : gtk_tree_selection_get_selected n'y fonctionne pas (M44) */
static gboolean
first_selected (GtkTreeView *treeview, GtkTreeIter *iter)
{
  GtkTreeModel *model = NULL;
  GList *rows = gtk_tree_selection_get_selected_rows (gtk_tree_view_get_selection (treeview), &model);
  gboolean found = rows != NULL && gtk_tree_model_get_iter (model, iter, rows->data);

  g_list_free_full (rows, (GDestroyNotify) gtk_tree_path_free);
  return found;
}

static gboolean
Delete_shortcut (GtkWidget *button, gpointer pointer)
{
  GtkTreeIter iter;
  GtkTreeView *treeview = (GtkTreeView *) pointer;
  GtkTreeModel *model = gtk_tree_view_get_model (treeview);

  if (first_selected (treeview, &iter))
    {
      gtk_list_store_set (GTK_LIST_STORE (model), &iter, COLUMN_SHORTCUT, _ ("None"), -1);
    }

  return FALSE;
}

static gboolean
Move_up (GtkWidget *button, gpointer pointer)
{
  GtkTreeView *treeview = (GtkTreeView *) pointer;
  GtkTreeModel *model = gtk_tree_view_get_model (treeview);
  GtkTreeSelection *selection = gtk_tree_view_get_selection (treeview);

  GList *list = gtk_tree_selection_get_selected_rows (selection, &model);
  if (list == NULL)
    return FALSE;

  gint n = g_list_length (list);
  gint *indices = g_new (gint, n);
  gint idx_count = 0;
  for (GList *l = list; l; l = l->next)
    {
      GtkTreePath *path = (GtkTreePath *) l->data;
      indices[idx_count++] = gtk_tree_path_get_indices (path)[0];
      gtk_tree_path_free (path);
    }
  g_list_free (list);

  sort_indices_asc (indices, idx_count);

  if (indices[0] <= 0)
    { g_free (indices); return FALSE; }

  gtk_tree_selection_unselect_all (selection);

  for (gint k = 0; k < idx_count; k++)
    {
      gint cur = indices[k];
      GtkTreePath *src_path = gtk_tree_path_new_from_indices (cur, -1);
      GtkTreeIter src_iter;
      gtk_tree_model_get_iter (model, &src_iter, src_path);
      gtk_tree_path_free (src_path);

      GtkTreePath *tgt_path = gtk_tree_path_new_from_indices (cur - 1, -1);
      GtkTreeIter tgt_iter;
      gtk_tree_model_get_iter (model, &tgt_iter, tgt_path);
      gtk_tree_path_free (tgt_path);

      gtk_list_store_move_before (GTK_LIST_STORE (model), &src_iter, &tgt_iter);
    }

  for (gint k = 0; k < idx_count; k++)
    {
      GtkTreePath *sel_path = gtk_tree_path_new_from_indices (indices[k] - 1, -1);
      GtkTreeIter sel_iter;
      gtk_tree_model_get_iter (model, &sel_iter, sel_path);
      gtk_tree_selection_select_iter (selection, &sel_iter);
      gtk_tree_path_free (sel_path);
    }

  g_free (indices);
  return FALSE;
}

static gboolean
Move_down (GtkWidget *button, gpointer pointer)
{
  GtkTreeView *treeview = (GtkTreeView *) pointer;
  GtkTreeModel *model = gtk_tree_view_get_model (treeview);
  GtkTreeSelection *selection = gtk_tree_view_get_selection (treeview);

  GList *list = gtk_tree_selection_get_selected_rows (selection, &model);
  if (list == NULL)
    return FALSE;

  gint n = g_list_length (list);
  gint *indices = g_new (gint, n);
  gint idx_count = 0;
  for (GList *l = list; l; l = l->next)
    {
      GtkTreePath *path = (GtkTreePath *) l->data;
      indices[idx_count++] = gtk_tree_path_get_indices (path)[0];
      gtk_tree_path_free (path);
    }
  g_list_free (list);

  sort_indices_asc (indices, idx_count);

  gint n_rows = gtk_tree_model_iter_n_children (model, NULL);
  if (indices[idx_count - 1] >= n_rows - 1)
    { g_free (indices); return FALSE; }

  gtk_tree_selection_unselect_all (selection);

  for (gint k = idx_count - 1; k >= 0; k--)
    {
      GtkTreePath *src_path = gtk_tree_path_new_from_indices (indices[k], -1);
      GtkTreeIter src_iter;
      gtk_tree_model_get_iter (model, &src_iter, src_path);
      gtk_tree_path_free (src_path);

      GtkTreePath *tgt_path = gtk_tree_path_new_from_indices (indices[k] + 1, -1);
      GtkTreeIter tgt_iter;
      gtk_tree_model_get_iter (model, &tgt_iter, tgt_path);
      gtk_tree_path_free (tgt_path);

      gtk_list_store_move_after (GTK_LIST_STORE (model), &src_iter, &tgt_iter);
    }

  for (gint k = 0; k < idx_count; k++)
    {
      GtkTreePath *sel_path = gtk_tree_path_new_from_indices (indices[k] + 1, -1);
      GtkTreeIter sel_iter;
      gtk_tree_model_get_iter (model, &sel_iter, sel_path);
      gtk_tree_selection_select_iter (selection, &sel_iter);
      gtk_tree_path_free (sel_path);
    }

  g_free (indices);
  return FALSE;
}

static GtMacroLists *
lists_from_model (GtkTreeModel *model)
{
  GtMacroLists *lists = gt_macro_lists_new ();
  GtkTreeIter iter;
  gboolean valid;

  for (valid = gtk_tree_model_get_iter_first (model, &iter); valid; valid = gtk_tree_model_iter_next (model, &iter))
    {
      gchar *name = NULL, *display = NULL, *value = NULL;

      gtk_tree_model_get (model, &iter, LIST_COLUMN_NAME, &name, LIST_COLUMN_DISPLAY, &display, LIST_COLUMN_VALUE, &value, -1);
      gt_macro_lists_add (lists, name, display, value);
      g_free (name);
      g_free (display);
      g_free (value);
    }
  return lists;
}

/* Validation de la fenêtre : les lignes, dans l'ordre affiché, avec leur identifiant ; arguments et polling suivent
   l'identifiant, jamais la position (M16). */
static gboolean
Save_shortcuts (GtkWidget *button, gpointer pointer)
{
  GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (pointer));
  GArray *rows = g_array_new (FALSE, TRUE, sizeof (GtMacroEdit));
  GPtrArray *strings = g_ptr_array_new_with_free_func (g_free);
  GtkTreeIter iter;
  gboolean valid;

  for (valid = gtk_tree_model_get_iter_first (model, &iter); valid; valid = gtk_tree_model_iter_next (model, &iter))
    {
      GtMacroEdit row = { 0 };
      gchar *label = NULL, *shortcut = NULL, *action = NULL, *tab = NULL;

      gtk_tree_model_get (model, &iter, COLUMN_ID, &row.id, COLUMN_LABEL, &label, COLUMN_SHORTCUT, &shortcut,
                          COLUMN_ACTION, &action, COLUMN_TAB, &tab, -1);
      /* « None » affiché (traduit) = pas de raccourci ; « None » non traduit est normalisé par le modèle */
      if (g_strcmp0 (shortcut, _ ("None")) == 0)
        {
          g_free (shortcut);
          shortcut = g_strdup ("");
        }
      row.label = label;
      row.shortcut = shortcut;
      row.action = action;
      row.tab = tab;
      g_ptr_array_add (strings, label);
      g_ptr_array_add (strings, shortcut);
      g_ptr_array_add (strings, action);
      g_ptr_array_add (strings, tab);
      g_array_append_val (rows, row);
    }
  gt_macro_set_apply_edits (current_set, (const GtMacroEdit *) rows->data, rows->len, lists_model != NULL ? lists_from_model (lists_model) : NULL);
  gt_macro_poller_sync (poller);
  macros_changed ();
  macros_save_file (NULL, TRUE);
  config_remember_macros_file ();
  g_ptr_array_unref (strings);
  g_array_unref (rows);
  return FALSE;
}

static gboolean
key_pressed (GtkWidget *window, GdkEventKey *key, gpointer pointer)
{
  GtkTreeIter iter;
  GtkTreeView *treeview = (GtkTreeView *) pointer;
  GtkTreeModel *model = gtk_tree_view_get_model (treeview);
  gchar *str = NULL;

  switch (key->keyval)
    {
    case GDK_KEY_Shift_L:
    case GDK_KEY_Shift_R:
    case GDK_KEY_Control_L:
    case GDK_KEY_Control_R:
    case GDK_KEY_Caps_Lock:
    case GDK_KEY_Shift_Lock:
    case GDK_KEY_Meta_L:
    case GDK_KEY_Meta_R:
    case GDK_KEY_Alt_L:
    case GDK_KEY_Alt_R:
    case GDK_KEY_Super_L:
    case GDK_KEY_Super_R:
    case GDK_KEY_Hyper_L:
    case GDK_KEY_Hyper_R:
    case GDK_KEY_Mode_switch:
      return FALSE;
    default:
      break;
    }

  if (first_selected (treeview, &iter))
    {
      GtkTreePath *path = gtk_tree_model_get_path (model, &iter);
      str = gtk_accelerator_name (key->keyval, key->state & ~GDK_MOD2_MASK);
      gtk_list_store_set (GTK_LIST_STORE (model), &iter, COLUMN_SHORTCUT, str, -1);

      gtk_tree_path_free (path);
      g_free (str);

      g_signal_handlers_disconnect_by_func (window, G_CALLBACK (key_pressed), pointer);
    }
  return FALSE;
}

static gboolean
Capture_shortcut (GtkWidget *button, gpointer pointer)
{
  g_signal_connect_after (window, "key_press_event", G_CALLBACK (key_pressed), pointer);

  return FALSE;
}

static gboolean
on_window_close (GtkWidget *widget, GdkEvent *event, gpointer pointer)
{
  Save_shortcuts (NULL, pointer);
  return FALSE;
}

static gboolean
Help_screen (GtkWidget *button, gpointer pointer)
{
  GtkWidget *Dialog;

  Dialog = gtk_message_dialog_new (pointer,
                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                   GTK_MESSAGE_INFO,
                                   GTK_BUTTONS_CLOSE,
                                   _ ("The \"action\" field of a macro is the data to be sent on the port. Text can be entered, but also special chars: \\r (carriage return), \\n (line feed), \\t (tab) and \\\\ (a backslash). You can also enter a byte in hexadecimal with \\xHH (two hexadecimal digits, eg. \\xFF)\nExamples:\n\t\"Hello\\n\" sends \"Hello\" followed by a Line Feed\n\t\"Hello\\x0A\" does the same thing but the LF is entered in hexadecimal"));

  gtk_dialog_run (GTK_DIALOG (Dialog));
  gtk_widget_destroy (Dialog);

  return FALSE;
}

/* --- Onglet Lists --- */

static GtkTreeModel *
create_lists_model (void)
{
  GtkListStore *store = gtk_list_store_new (LIST_NUM_COLUMNS,
                                            G_TYPE_STRING,
                                            G_TYPE_STRING,
                                            G_TYPE_STRING);

  const GtMacroLists *lists = gt_macro_set_get_lists (current_set);
  gint n_lists = gt_macro_lists_count (lists);
  for (gint li = 0; li < n_lists; li++)
    {
      gint n_entries = gt_macro_lists_entry_count (lists, li);
      for (gint ei = 0; ei < n_entries; ei++)
        {
          GtkTreeIter iter;
          gtk_list_store_append (store, &iter);
          gtk_list_store_set (store, &iter,
                              LIST_COLUMN_NAME, gt_macro_lists_name (lists, li),
                              LIST_COLUMN_DISPLAY, gt_macro_lists_entry_display (lists, li, ei),
                              LIST_COLUMN_VALUE, gt_macro_lists_entry_value (lists, li, ei),
                              -1);
        }
    }

  return GTK_TREE_MODEL (store);
}

static void
add_list_columns (GtkTreeView *treeview)
{
  append_text_column (treeview, _ ("List"), LIST_COLUMN_NAME, TRUE, FALSE, 80);
  append_text_column (treeview, _ ("Display"), LIST_COLUMN_DISPLAY, TRUE, FALSE, 80);
  append_text_column (treeview, _ ("Value"), LIST_COLUMN_VALUE, TRUE, FALSE, 120);
}

static gboolean
Add_list_entry (GtkWidget *button, gpointer pointer)
{
  GtkTreeIter new_iter;
  GtkTreeView *treeview = (GtkTreeView *) pointer;
  GtkTreeSelection *selection = gtk_tree_view_get_selection (treeview);
  GtkTreeModel *model = GTK_TREE_MODEL (lists_model);

  GList *list = gtk_tree_selection_get_selected_rows (selection, &model);
  gint max_idx = -1;
  for (GList *l = list; l; l = l->next)
    {
      GtkTreePath *path = (GtkTreePath *) l->data;
      gint row = gtk_tree_path_get_indices (path)[0];
      if (row > max_idx)
        max_idx = row;
      gtk_tree_path_free (path);
    }
  g_list_free (list);

  if (max_idx >= 0)
    {
      GtkTreePath *path = gtk_tree_path_new_from_indices (max_idx, -1);
      GtkTreeIter iter;
      gtk_tree_model_get_iter (model, &iter, path);
      gtk_list_store_insert_after (GTK_LIST_STORE (lists_model), &new_iter, &iter);
      gtk_tree_path_free (path);
    }
  else
    {
      gtk_list_store_append (GTK_LIST_STORE (lists_model), &new_iter);
    }

  gtk_list_store_set (GTK_LIST_STORE (lists_model), &new_iter,
                      LIST_COLUMN_NAME, "NewList",
                      LIST_COLUMN_DISPLAY, "display",
                      LIST_COLUMN_VALUE, "value",
                      -1);

  return FALSE;
}

static gboolean
Delete_list_entry (GtkWidget *button, gpointer pointer)
{
  GtkTreeView *treeview = (GtkTreeView *) pointer;
  GtkTreeModel *model = gtk_tree_view_get_model (treeview);
  GtkTreeSelection *selection = gtk_tree_view_get_selection (treeview);

  GList *list = gtk_tree_selection_get_selected_rows (selection, &model);
  if (list == NULL)
    return FALSE;

  gint n = g_list_length (list);
  gint *indices = g_new (gint, n);
  gint idx_count = 0;
  for (GList *l = list; l; l = l->next)
    {
      GtkTreePath *path = (GtkTreePath *) l->data;
      indices[idx_count++] = gtk_tree_path_get_indices (path)[0];
      gtk_tree_path_free (path);
    }
  g_list_free (list);

  sort_indices_asc (indices, idx_count);

  for (gint k = idx_count - 1; k >= 0; k--)
    {
      GtkTreePath *path = gtk_tree_path_new_from_indices (indices[k], -1);
      GtkTreeIter iter;
      if (gtk_tree_model_get_iter (model, &iter, path))
        gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
      gtk_tree_path_free (path);
    }

  g_free (indices);
  return FALSE;
}

static GtkWidget *
build_lists_page (void)
{
  GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 8);

  GtkWidget *sw = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw), GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_box_pack_start (GTK_BOX (vbox), sw, TRUE, TRUE, 0);

  lists_model = create_lists_model ();
  GtkWidget *treeview = gtk_tree_view_new_with_model (lists_model);
  g_object_unref (lists_model);		/* la vue garde sa référence ; lists_model remis à NULL à la destruction de la fenêtre */
  gtk_tree_selection_set_mode (gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview)), GTK_SELECTION_MULTIPLE);

  add_list_columns (GTK_TREE_VIEW (treeview));
  gtk_container_add (GTK_CONTAINER (sw), treeview);

  GtkWidget *hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_box_set_homogeneous (GTK_BOX (hbox), TRUE);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

  GtkWidget *button = gtk_button_new_with_mnemonic (_ ("_Add Entry"));
  g_signal_connect (button, "clicked", G_CALLBACK (Add_list_entry), (gpointer) treeview);
  gtk_box_pack_start (GTK_BOX (hbox), button, TRUE, TRUE, 0);

  button = gtk_button_new_with_mnemonic (_ ("_Delete Entry"));
  g_signal_connect (button, "clicked", G_CALLBACK (Delete_list_entry), (gpointer) treeview);
  gtk_box_pack_start (GTK_BOX (hbox), button, TRUE, TRUE, 0);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_box_set_homogeneous (GTK_BOX (hbox), TRUE);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

  button = gtk_button_new_with_mnemonic (_ ("Move _Up"));
  g_signal_connect (button, "clicked", G_CALLBACK (Move_up), (gpointer) treeview);
  gtk_box_pack_start (GTK_BOX (hbox), button, TRUE, TRUE, 0);

  button = gtk_button_new_with_mnemonic (_ ("Move _Down"));
  g_signal_connect (button, "clicked", G_CALLBACK (Move_down), (gpointer) treeview);
  gtk_box_pack_start (GTK_BOX (hbox), button, TRUE, TRUE, 0);

  return vbox;
}

static void
on_window_destroy (GtkWidget *widget, gpointer data)
{
  window = NULL;
  lists_model = NULL;		/* libéré avec sa vue */
}

void
Config_macros (GtkAction *action, gpointer data)
{
  GtkWidget *vbox, *hbox;
  GtkWidget *sw;
  GtkTreeModel *model;
  GtkWidget *treeview;
  GtkWidget *button;
  GtkWidget *separator;

  /* Une seule fenêtre d'édition : une seconde ouverture la ramène au premier plan */
  if (window != NULL)
    {
      gtk_window_present (GTK_WINDOW (window));
      return;
    }

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title (GTK_WINDOW (window), _ ("Configure Macros"));

  g_signal_connect (window, "destroy", G_CALLBACK (on_window_destroy), NULL);
  gtk_container_set_border_width (GTK_CONTAINER (window), 8);

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_container_add (GTK_CONTAINER (window), vbox);

  /* --- Notebook avec onglets Macros et Lists --- */
  GtkWidget *notebook = gtk_notebook_new ();

  /* Onglet Macros */
  sw = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw), GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

  model = create_model ();
  treeview = gtk_tree_view_new_with_model (model);
  gtk_tree_selection_set_mode (gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview)), GTK_SELECTION_MULTIPLE);
  gtk_tree_view_set_search_column (GTK_TREE_VIEW (treeview), COLUMN_SHORTCUT);
  g_object_unref (model);

  gtk_container_add (GTK_CONTAINER (sw), treeview);
  add_columns (GTK_TREE_VIEW (treeview));

  GtkWidget *macros_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_container_set_border_width (GTK_CONTAINER (macros_vbox), 8);
  gtk_box_pack_start (GTK_BOX (macros_vbox), sw, TRUE, TRUE, 0);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_box_set_homogeneous (GTK_BOX (hbox), TRUE);
  gtk_box_pack_start (GTK_BOX (macros_vbox), hbox, FALSE, FALSE, 0);

  button = gtk_button_new_with_mnemonic (_ ("_Add"));
  g_signal_connect (button, "clicked", G_CALLBACK (Add_shortcut), (gpointer) treeview);
  gtk_box_pack_start (GTK_BOX (hbox), button, TRUE, TRUE, 0);

  button = gtk_button_new_with_mnemonic (_ ("_Delete"));
  g_signal_connect (button, "clicked", G_CALLBACK (Delete_macro), (gpointer) treeview);
  gtk_box_pack_start (GTK_BOX (hbox), button, TRUE, TRUE, 0);

  button = gtk_button_new_with_mnemonic (_ ("_Capture Shortcut"));
  g_signal_connect (button, "clicked", G_CALLBACK (Capture_shortcut), (gpointer) treeview);
  gtk_box_pack_start (GTK_BOX (hbox), button, TRUE, TRUE, 0);

  button = gtk_button_new_with_mnemonic ("Delete Shortcut");
  g_signal_connect (button, "clicked", G_CALLBACK (Delete_shortcut), (gpointer) treeview);
  gtk_box_pack_start (GTK_BOX (hbox), button, TRUE, TRUE, 0);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_box_set_homogeneous (GTK_BOX (hbox), TRUE);
  gtk_box_pack_start (GTK_BOX (macros_vbox), hbox, FALSE, FALSE, 0);

  button = gtk_button_new_with_mnemonic (_ ("Move _Up"));
  g_signal_connect (button, "clicked", G_CALLBACK (Move_up), (gpointer) treeview);
  gtk_box_pack_start (GTK_BOX (hbox), button, TRUE, TRUE, 0);

  button = gtk_button_new_with_mnemonic (_ ("Move _Down"));
  g_signal_connect (button, "clicked", G_CALLBACK (Move_down), (gpointer) treeview);
  gtk_box_pack_start (GTK_BOX (hbox), button, TRUE, TRUE, 0);

  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), macros_vbox, gtk_label_new (_ ("Macros")));

  /* Onglet Lists */
  GtkWidget *lists_page = build_lists_page ();
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), lists_page, gtk_label_new (_ ("Lists")));

  gtk_box_pack_start (GTK_BOX (vbox), notebook, TRUE, TRUE, 0);

  g_signal_connect (window, "delete-event", G_CALLBACK (on_window_close), (gpointer) treeview);

  separator = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
  gtk_box_pack_start (GTK_BOX (vbox), separator, FALSE, TRUE, 0);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_box_set_homogeneous (GTK_BOX (hbox), TRUE);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

  button = gtk_button_new_with_label (_ ("Help"));
  g_signal_connect (button, "clicked", G_CALLBACK (Help_screen), (gpointer) window);
  gtk_box_pack_start (GTK_BOX (hbox), button, TRUE, TRUE, 0);

  button = gtk_button_new_with_label (_ ("OK"));
  g_signal_connect (button, "clicked", G_CALLBACK (Save_shortcuts), (gpointer) treeview);
  g_signal_connect_swapped (button, "clicked", G_CALLBACK (gtk_widget_destroy), (gpointer) window);
  gtk_box_pack_end (GTK_BOX (hbox), button, TRUE, TRUE, 0);

  button = gtk_button_new_with_label (_ ("Cancel"));
  g_signal_connect_swapped (button, "clicked", G_CALLBACK (gtk_widget_destroy), (gpointer) window);
  gtk_box_pack_end (GTK_BOX (hbox), button, TRUE, TRUE, 0);

  gtk_window_set_default_size (GTK_WINDOW (window), 400, 500);

  gtk_widget_show_all (window);
}
