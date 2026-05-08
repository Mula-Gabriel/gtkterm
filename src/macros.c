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
#include "macros.h"
#include "term_config.h"

#include <config.h>
#include <glib/gi18n.h>

enum
{
  COLUMN_SHORTCUT,
  COLUMN_LABEL,
  COLUMN_ACTION,
  COLUMN_TAB,
  NUM_COLUMNS
};

enum
{
  LIST_COLUMN_NAME,
  LIST_COLUMN_DISPLAY,
  LIST_COLUMN_VALUE,
  LIST_NUM_COLUMNS
};

macro_t *macros = NULL;
static GtkWidget *window = NULL;
static GtkTreeModel *lists_model = NULL;

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

macro_t *
get_shortcuts (gint *size)
{
  gint i = 0;

  if (macros != NULL)
    {
      while (macros[i].shortcut != NULL || macros[i].label != NULL)
        i++;
    }
  *size = i;
  return macros;
}

/* Parse une séquence hexadécimale après un backslash (\XX ou \0XX) */
static guchar
parse_hex_escape (const gchar *string, gint *index)
{
  gint i = *index;
  gint hex_start = 0;
  gint isTwodigits = 0;

  /* Déterminer où commence la partie hexa et combien de digits on a */
  if (string[i + 1] == '0' && string[i + 2] != '\0' && g_unichar_isxdigit ((gunichar) string[i + 2]))
    {
      // Format \0XX ou \0X
      hex_start = i + 2;
      isTwodigits = (string[i + 3] != '\0' && g_unichar_isxdigit ((gunichar) string[i + 3])) ? 1 : 0;
    }
  else if (g_unichar_isxdigit ((gunichar) string[i + 1]))
    {
      // Format \XX ou \X
      hex_start = i + 1;
      isTwodigits = (string[i + 2] != '\0' && g_unichar_isxdigit ((gunichar) string[i + 2])) ? 1 : 0;
    }
  else
    { // finalement, mauvais format, on sort les caractaire ascii tel quel
      return '\\';
    }

  /* Parser les digits hexa */
  guint val_read;
  if (sscanf (&string[hex_start], "%02X", &val_read) == 1)
    {
      /* Mettre l'index sur le dernier caractère utiliser */
      *index = hex_start + isTwodigits;
      return (guchar) val_read;
    }

  return '\\';
}
/* Parse une séquence d'échappement C standard (\n, \t, etc.) */
static guchar
parse_standard_escape (gchar escape_char, gint *index)
{
  guchar result;

  switch (escape_char)
    {
    case 'a':
      result = '\a';
      break;
    case 'b':
      result = '\b';
      break;
    case 't':
      result = '\t';
      break;
    case 'n':
      result = '\n';
      break;
    case 'v':
      result = '\v';
      break;
    case 'f':
      result = '\f';
      break;
    case 'r':
      result = '\r';
      break;
    case '\\':
      result = '\\';
      break;
    default:
      return '\\'; // séquence inconue, retourne \ et n'avance pas
    }

  (*index)++;
  return result;
}

/* Parse une chaîne de macro et construit le buffer à envoyer */
static GByteArray *
parse_macro_string (const gchar *string)
{
  guchar byte;
  GByteArray *buffer = g_byte_array_new ();
  gint length = strlen (string);

  for (gint i = 0; i < length; i++)
    {
      if (string[i] == '\\' && string[i + 1] != '\0')
        {
          if (g_unichar_isdigit ((gunichar) string[i + 1]))
            {
              byte = parse_hex_escape (string, &i);
            }
          else
            {
              byte = parse_standard_escape (string[i + 1], &i);
            }
        }
      else
        {
          byte = (guchar) string[i];
        }

      g_byte_array_append (buffer, &byte, 1);
    }

  return buffer;
}

/* Parse le spécificateur de format à action[start] (où action[start]=='%' et action[start+1]!='%').
   Retourne le type char ('d','f','s'...) et écrit la position du type dans *end_out. */
static gchar
parse_one_spec (const gchar *action, gint start, gint *end_out)
{
  gint j = start + 1;
  while (action[j] == '-' || action[j] == '+' || action[j] == ' ' ||
         action[j] == '#' || action[j] == '0')
    j++;
  while (g_ascii_isdigit (action[j]))
    j++;
  if (action[j] == '.')
    {
      j++;
      while (g_ascii_isdigit (action[j]))
        j++;
    }
  while (action[j] == 'h' || action[j] == 'l' || action[j] == 'L' ||
         action[j] == 'z' || action[j] == 'j' || action[j] == 't')
    j++;
  if (action[j] != '\0' && strchr ("diouxXeEfFgGaAcs", action[j]))
    {
      if (end_out)
        *end_out = j;
      return action[j];
    }
  return '\0';
}

/* Tente de parser [label] juste avant action[start] (où action[start]=='%').
   Retourne l'indice de '[' trouvé, ou -1 s'il n'y a pas de label.
   Écrit le texte du label dans *label_out (à libérer avec g_free). */
static gint
try_parse_label (const gchar *action, gint start, gchar **label_out)
{
  if (label_out)
    *label_out = NULL;
  if (start <= 0 || action[start] != '%')
    return -1;

  gint j = start - 1;
  while (j >= 0 && action[j] == ' ')
    j--;
  if (j < 0 || action[j] != ']')
    return -1;

  gint bracket_end = j;
  while (j >= 0 && action[j] != '[')
    j--;
  if (j < 0)
    return -1;

  gchar *label = g_strndup (&action[j + 1], bracket_end - j - 1);
  if (label_out)
    *label_out = label;
  return j;
}

/* Parse un spécificateur de liste %#NomListe.
   Retourne TRUE si c'est une liste, écrit le nom dans *name_out (à libérer) et la fin dans *end_out. */
static gboolean
try_parse_list_spec (const gchar *action, gint start, gchar **name_out, gint *end_out)
{
  if (action[start] != '%' || action[start + 1] != '#')
    return FALSE;

  gint j = start + 2;
  gint name_start = j;

  while (g_ascii_isalnum (action[j]) || action[j] == '_' || action[j] == '-')
    j++;

  if (j == name_start)
    return FALSE;

  gchar *name = g_strndup (&action[name_start], j - name_start);
  if (name_out)
    *name_out = name;
  if (end_out)
    *end_out = j - 1;
  return TRUE;
}

/* Applique un spécificateur isolé sur une valeur string et retourne le résultat alloué. */
static gchar *
apply_spec (const gchar *spec, gchar fmt_type, const gchar *arg_str)
{
  switch (fmt_type)
    {
    case 'd':
    case 'i':
      return g_strdup_printf (spec, (int) strtol (arg_str, NULL, 10));
    case 'u':
    case 'o':
      return g_strdup_printf (spec, (unsigned int) strtoul (arg_str, NULL, 10));
    case 'x':
    case 'X':
      return g_strdup_printf (spec, (unsigned int) strtoul (arg_str, NULL, 0));
    case 'e':
    case 'E':
    case 'f':
    case 'F':
    case 'g':
    case 'G':
    case 'a':
    case 'A':
      return g_strdup_printf (spec, strtod (arg_str, NULL));
    case 's':
      return g_strdup_printf (spec, arg_str);
    case 'c':
      return g_strdup_printf (spec, (int) (arg_str[0] ? arg_str[0] : ' '));
    default:
      return g_strdup ("");
    }
}

gchar
macro_get_format_type (const gchar *action)
{
  if (action == NULL)
    return '\0';
  for (gint i = 0; action[i] != '\0'; i++)
    {
      if (action[i] != '%')
        continue;
      if (action[i + 1] == '%')
        {
          i++;
          continue;
        }
      gchar t = parse_one_spec (action, i, NULL);
      if (t != '\0')
        return t;
    }
  return '\0';
}

gboolean
macro_has_format_arg (const gchar *action)
{
  return macro_get_format_type (action) != '\0';
}

gint
macro_count_format_args (const gchar *action)
{
  if (action == NULL)
    return 0;
  gint count = 0;
  for (gint i = 0; action[i] != '\0'; i++)
    {
      if (action[i] != '%')
        continue;
      if (action[i + 1] == '%')
        {
          i++;
          continue;
        }
      {
        gchar *list_name = NULL;
        gint end;
        if (try_parse_list_spec (action, i, &list_name, &end))
          {
            count++;
            i = end;
            g_free (list_name);
            continue;
          }
      }
      gint end;
      if (parse_one_spec (action, i, &end) != '\0')
        {
          count++;
          i = end;
        }
    }
  return count;
}

/* Retourne un tableau de types (ex: "dfs") à libérer avec g_free, ou NULL si aucun arg. */
gchar *
macro_get_format_types (const gchar *action, gint *count_out)
{
  gint n = macro_count_format_args (action);
  if (count_out)
    *count_out = n;
  if (n == 0)
    return NULL;
  gchar *types = g_new (gchar, n + 1);
  gint idx = 0;
  for (gint i = 0; action[i] != '\0' && idx < n; i++)
    {
      if (action[i] != '%')
        continue;
      if (action[i + 1] == '%')
        {
          i++;
          continue;
        }
      {
        gchar *list_name = NULL;
        gint end;
        if (try_parse_list_spec (action, i, &list_name, &end))
          {
            types[idx++] = 'l';
            i = end;
            g_free (list_name);
            continue;
          }
      }
      gint end;
      gchar t = parse_one_spec (action, i, &end);
      if (t != '\0')
        {
          types[idx++] = t;
          i = end;
        }
    }
  types[n] = '\0';
  return types;
}

/* Retourne un tableau d'infos sur les arguments (type + nom de liste si applicable).
   À libérer avec macro_arg_infos_free(). */
macro_arg_info_t *
macro_get_arg_infos (const gchar *action, gint *count_out)
{
  gint n = macro_count_format_args (action);
  if (count_out)
    *count_out = n;
  if (n == 0)
    return NULL;

  macro_arg_info_t *infos = g_new0 (macro_arg_info_t, n);
  gint idx = 0;

  for (gint i = 0; action[i] != '\0' && idx < n; i++)
    {
      if (action[i] != '%')
        continue;
      if (action[i + 1] == '%')
        {
          i++;
          continue;
        }

      /* Parser le label [texte] avant le spécificateur */
      (void) try_parse_label (action, i, &infos[idx].label);

      {
        gchar *list_name = NULL;
        gint end;
        if (try_parse_list_spec (action, i, &list_name, &end))
          {
            infos[idx].type = 'l';
            infos[idx].list_name = list_name;
            idx++;
            i = end;
            continue;
          }
      }

      gint end;
      gchar t = parse_one_spec (action, i, &end);
      if (t != '\0')
        {
          infos[idx].type = t;
          infos[idx].list_name = NULL;
          idx++;
          i = end;
        }
    }

  return infos;
}

void
macro_arg_infos_free (macro_arg_info_t *infos, gint count)
{
  if (!infos)
    return;
  for (gint i = 0; i < count; i++)
    {
      g_free (infos[i].list_name);
      g_free (infos[i].label);
    }
  g_free (infos);
}

/* Construit la chaîne d'action en substituant chaque spécificateur par l'argument correspondant. */
static gchar *
format_action_with_args (const gchar *action, const gchar **args, gint n_args)
{
  GString *result = g_string_new ("");
  gint arg_idx = 0;

  for (gint i = 0; action[i] != '\0'; i++)
    {
      if (action[i] != '%')
        {
          g_string_append_c (result, action[i]);
          continue;
        }
      if (action[i + 1] == '%')
        {
          g_string_append_c (result, '%');
          i++;
          continue;
        }

      /* Si un label [texte] précède ce spécificateur, le retirer du résultat */
      {
        gint label_start = try_parse_label (action, i, NULL);
        if (label_start >= 0)
          g_string_truncate (result, result->len - (i - label_start));
      }

      {
        gchar *list_name = NULL;
        gint list_end;
        if (try_parse_list_spec (action, i, &list_name, &list_end))
          {
            if (arg_idx < n_args)
              g_string_append (result, args[arg_idx] ? args[arg_idx] : "");
            else
              g_string_append_len (result, &action[i], list_end - i + 1);
            arg_idx++;
            i = list_end;
            g_free (list_name);
            continue;
          }
      }

      gint spec_end;
      gchar fmt_type = parse_one_spec (action, i, &spec_end);

      if (fmt_type == '\0')
        {
          g_string_append_c (result, action[i]);
          continue;
        }

      if (arg_idx < n_args)
        {
          gchar *spec = g_strndup (&action[i], spec_end - i + 1);
          gchar *formatted = apply_spec (spec, fmt_type, args[arg_idx] ? args[arg_idx] : "");
          g_string_append (result, formatted);
          g_free (formatted);
          g_free (spec);
          arg_idx++;
        }
      else
        {
          g_string_append_len (result, &action[i], spec_end - i + 1);
        }
      i = spec_end;
    }
  return g_string_free (result, FALSE);
}

void
send_macro_with_args (gint macro_index, const gchar **args, gint n_args)
{
  gint nb_macros = 0;
  get_shortcuts (&nb_macros);
  if (macro_index >= nb_macros || macros[macro_index].action == NULL)
    return;

  gchar *formatted = format_action_with_args (macros[macro_index].action, args, n_args);
  GByteArray *buffer = parse_macro_string (formatted);
  g_free (formatted);

  if (buffer->len > 0)
    send_serial ((gchar *) buffer->data, buffer->len);
  g_byte_array_free (buffer, TRUE);

  gchar *message = g_strdup_printf (_ ("Macro \"%s\" sent!"),
                                    strlen (macros[macro_index].label) > 0 ? macros[macro_index].label : macros[macro_index].shortcut);
  Put_temp_message (message, 800);
  g_free (message);
}

void
send_macro_with_arg (gint macro_index, const gchar *arg_str)
{
  const gchar *args[] = { arg_str };
  send_macro_with_args (macro_index, args, 1);
}

/* Fonction principale de callback pour l'exécution d'une macro */
void
shortcut_callback (gpointer number)
{
  gint macro_index = GPOINTER_TO_INT (number);

  if (macros == NULL || macros[macro_index].action == NULL)
    return;

  gchar *macro_string = macros[macro_index].action;

  /* Parser et construire le buffer */
  GByteArray *buffer = parse_macro_string (macro_string);

  /* Envoyer tout le buffer d'un seul coup */
  if (buffer->len > 0)
    send_serial ((gchar *) buffer->data, buffer->len);

  /* Libérer le buffer */
  g_byte_array_free (buffer, TRUE);

  /* Afficher le message de confirmation */
  gchar *message = g_strdup_printf (_ ("Macro \"%s\" sent!"), strlen (macros[macro_index].label) > 0 ? macros[macro_index].label : macros[macro_index].shortcut);
  Put_temp_message (message, 800);
  g_free (message);
}

void
create_shortcuts (macro_t *macro, gint size)
{
  macros = g_malloc ((size + 1) * sizeof (macro_t));
  if (macros != NULL)
    {
      memcpy (macros, macro, size * sizeof (macro_t));
      macros[size].label = NULL;
      macros[size].shortcut = NULL;
      macros[size].action = NULL;
      macros[size].tab = NULL;
    }
  else
    perror ("malloc");
}

void
add_shortcuts (void)
{
  long i = 0;
  guint acc_key;
  GdkModifierType mod;

  if (macros == NULL)
    return;

  while (macros[i].shortcut != NULL)
    {
      macros[i].closure = g_cclosure_new_swap (G_CALLBACK (shortcut_callback), (gpointer) i, NULL);
      gtk_accelerator_parse (macros[i].shortcut, &acc_key, &mod);
      if (acc_key != 0)
        gtk_accel_group_connect (shortcuts, acc_key, mod, GTK_ACCEL_MASK, macros[i].closure);
      i++;
    }
}

static void
macros_destroy (void)
{
  gint i = 0;

  if (macros == NULL)
    return;

  while (macros[i].shortcut != NULL)
    {
      g_free (macros[i].shortcut);
      g_free (macros[i].action);
      g_free (macros[i].label);
      g_free (macros[i].tab);
      g_strfreev (macros[i].args);
      /*
      g_closure_unref(macros[i].closure);
      */
      i++;
    }
  g_free (macros);
  macros = NULL;
}

void
remove_shortcuts (void)
{
  gint i = 0;

  if (macros == NULL)
    return;

  while (macros[i].shortcut != NULL)
    {
      gtk_accel_group_disconnect (shortcuts, macros[i].closure);
      i++;
    }

  macros_destroy ();
}

static GtkTreeModel *
create_model (void)
{
  gint i = 0;
  GtkListStore *store;
  GtkTreeIter iter;

  /* create list store */
  store = gtk_list_store_new (NUM_COLUMNS,
                              G_TYPE_STRING,
                              G_TYPE_STRING,
                              G_TYPE_STRING,
                              G_TYPE_STRING);

  /* add data to the list store */
  if (macros != NULL)
    {
      while (1)
        {
          if (macros[i].shortcut == NULL && macros[i].action == NULL && macros[i].label == NULL)
            break;

          gtk_list_store_append (store, &iter);
          gtk_list_store_set (store, &iter,
                              COLUMN_LABEL, macros[i].label,
                              COLUMN_SHORTCUT, macros[i].shortcut,
                              COLUMN_ACTION, macros[i].action,
                              COLUMN_TAB, macros[i].tab ? macros[i].tab : "",
                              -1);
          i++;
        }
    }

  return GTK_TREE_MODEL (store);
}

static gboolean
action_edited (GtkCellRendererText *cell,
               const gchar *path_string,
               const gchar *new_text,
               gpointer data)
{
  GtkTreeModel *model = (GtkTreeModel *) data;
  GtkTreePath *path = gtk_tree_path_new_from_string (path_string);
  GtkTreeIter iter;

  gtk_tree_model_get_iter (model, &iter, path);

  gtk_list_store_set (GTK_LIST_STORE (model), &iter, COLUMN_ACTION, new_text, -1);
  gtk_tree_path_free (path);

  return TRUE;
}

static gboolean
label_edited (GtkCellRendererText *cell,
              const gchar *path_string,
              const gchar *new_text,
              gpointer data)
{
  GtkTreeModel *model = (GtkTreeModel *) data;
  GtkTreePath *path = gtk_tree_path_new_from_string (path_string);
  GtkTreeIter iter;

  gtk_tree_model_get_iter (model, &iter, path);

  gtk_list_store_set (GTK_LIST_STORE (model), &iter, COLUMN_LABEL, new_text, -1);
  gtk_tree_path_free (path);

  return TRUE;
}

static gboolean
tab_edited (GtkCellRendererText *cell,
            const gchar *path_string,
            const gchar *new_text,
            gpointer data)
{
  GtkTreeModel *model = (GtkTreeModel *) data;
  GtkTreePath *path = gtk_tree_path_new_from_string (path_string);
  GtkTreeIter iter;

  gtk_tree_model_get_iter (model, &iter, path);

  gtk_list_store_set (GTK_LIST_STORE (model), &iter, COLUMN_TAB, new_text, -1);
  gtk_tree_path_free (path);

  return TRUE;
}

static void
add_columns (GtkTreeView *treeview)
{
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;
  GtkTreeModel *model = gtk_tree_view_get_model (treeview);

  renderer = gtk_cell_renderer_text_new ();
  g_signal_connect (renderer, "edited", G_CALLBACK (label_edited), model);
  g_object_set (G_OBJECT (renderer), "editable", TRUE, NULL);
  column = gtk_tree_view_column_new_with_attributes (_ ("Label"), renderer, "text", COLUMN_LABEL, NULL);
  gtk_tree_view_column_set_sort_column_id (column, COLUMN_LABEL);
  gtk_tree_view_column_set_resizable (column, TRUE);
  gtk_tree_view_column_set_min_width (column, 80);
  gtk_tree_view_append_column (treeview, column);

  renderer = gtk_cell_renderer_text_new ();
  column = gtk_tree_view_column_new_with_attributes (_ ("Shortcut"), renderer, "text", COLUMN_SHORTCUT, NULL);
  gtk_tree_view_column_set_sort_column_id (column, COLUMN_SHORTCUT);
  gtk_tree_view_column_set_resizable (column, TRUE);
  gtk_tree_view_column_set_min_width (column, 80);
  gtk_tree_view_append_column (treeview, column);

  renderer = gtk_cell_renderer_text_new ();
  g_signal_connect (renderer, "edited", G_CALLBACK (action_edited), model);
  column = gtk_tree_view_column_new_with_attributes (_ ("Action"), renderer, "text", COLUMN_ACTION, NULL);
  g_object_set (G_OBJECT (renderer), "editable", TRUE, NULL);
  gtk_tree_view_column_set_sort_column_id (column, COLUMN_ACTION);
  gtk_tree_view_column_set_resizable (column, TRUE);
  gtk_tree_view_column_set_min_width (column, 120);
  gtk_tree_view_append_column (treeview, column);

  renderer = gtk_cell_renderer_text_new ();
  g_signal_connect (renderer, "edited", G_CALLBACK (tab_edited), model);
  g_object_set (G_OBJECT (renderer), "editable", TRUE, NULL);
  column = gtk_tree_view_column_new_with_attributes (_ ("Tab"), renderer, "text", COLUMN_TAB, NULL);
  gtk_tree_view_column_set_sort_column_id (column, COLUMN_TAB);
  gtk_tree_view_column_set_resizable (column, TRUE);
  gtk_tree_view_column_set_min_width (column, 60);
  gtk_tree_view_append_column (treeview, column);
}

static gint
Add_shortcut (GtkWidget *button, gpointer pointer)
{
  GtkTreeIter iter, new_iter;
  GtkTreeView *treeview = (GtkTreeView *) pointer;
  GtkTreeModel *model = gtk_tree_view_get_model (treeview);
  GtkTreeSelection *selection = gtk_tree_view_get_selection (treeview);

  if (gtk_tree_selection_get_selected (selection, NULL, &iter))
    {
      gtk_list_store_insert_after (GTK_LIST_STORE (model), &new_iter, &iter);
    }
  else
    {
      gtk_list_store_append (GTK_LIST_STORE (model), &new_iter);
    }

  gtk_list_store_set (GTK_LIST_STORE (model), &new_iter,
                      COLUMN_SHORTCUT, "None",
                      COLUMN_TAB, _ ("General"),
                      -1);

  return FALSE;
}

static gboolean
Delete_macro (GtkWidget *button, gpointer pointer)
{
  GtkTreeIter iter;
  GtkTreeView *treeview = (GtkTreeView *) pointer;
  GtkTreeModel *model = gtk_tree_view_get_model (treeview);
  GtkTreeSelection *selection = gtk_tree_view_get_selection (treeview);

  if (gtk_tree_selection_get_selected (selection, NULL, &iter))
    {
      GtkTreePath *path = gtk_tree_model_get_path (model, &iter);
      gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
      gtk_tree_path_free (path);
    }

  return FALSE;
}

static gboolean
Delete_shortcut (GtkWidget *button, gpointer pointer)
{
  GtkTreeIter iter;
  GtkTreeView *treeview = (GtkTreeView *) pointer;
  GtkTreeModel *model = gtk_tree_view_get_model (treeview);
  GtkTreeSelection *selection = gtk_tree_view_get_selection (treeview);

  if (gtk_tree_selection_get_selected (selection, NULL, &iter))
    {
      gtk_list_store_set (GTK_LIST_STORE (model), &iter, COLUMN_SHORTCUT, "None", -1);
    }

  return FALSE;
}

static gboolean
Move_up (GtkWidget *button, gpointer pointer)
{
  GtkTreeIter iter;
  GtkTreeView *treeview = (GtkTreeView *) pointer;
  GtkTreeModel *model = gtk_tree_view_get_model (treeview);
  GtkTreeSelection *selection = gtk_tree_view_get_selection (treeview);

  if (!gtk_tree_selection_get_selected (selection, NULL, &iter))
    return FALSE;

  GtkTreePath *path = gtk_tree_model_get_path (model, &iter);
  gint row = gtk_tree_path_get_indices (path)[0];
  gtk_tree_path_free (path);

  if (row > 0)
    {
      GtkTreePath *target = gtk_tree_path_new_from_indices (row - 1, -1);
      GtkTreeIter target_iter;
      gtk_tree_model_get_iter (model, &target_iter, target);
      gtk_list_store_move_before (GTK_LIST_STORE (model), &iter, &target_iter);
      gtk_tree_path_free (target);
      target = gtk_tree_path_new_from_indices (row - 1, -1);
      gtk_tree_model_get_iter (model, &iter, target);
      gtk_tree_selection_select_iter (selection, &iter);
      gtk_tree_path_free (target);
    }

  return FALSE;
}

static gboolean
Move_down (GtkWidget *button, gpointer pointer)
{
  GtkTreeIter iter;
  GtkTreeView *treeview = (GtkTreeView *) pointer;
  GtkTreeModel *model = gtk_tree_view_get_model (treeview);
  GtkTreeSelection *selection = gtk_tree_view_get_selection (treeview);

  if (!gtk_tree_selection_get_selected (selection, NULL, &iter))
    return FALSE;

  GtkTreePath *path = gtk_tree_model_get_path (model, &iter);
  gint row = gtk_tree_path_get_indices (path)[0];
  gtk_tree_path_free (path);

  gint n_rows = gtk_tree_model_iter_n_children (model, NULL);
  if (row < n_rows - 1)
    {
      GtkTreePath *target = gtk_tree_path_new_from_indices (row + 1, -1);
      GtkTreeIter target_iter;
      gtk_tree_model_get_iter (model, &target_iter, target);
      gtk_list_store_move_after (GTK_LIST_STORE (model), &iter, &target_iter);
      gtk_tree_path_free (target);
      target = gtk_tree_path_new_from_indices (row + 1, -1);
      gtk_tree_model_get_iter (model, &iter, target);
      gtk_tree_selection_select_iter (selection, &iter);
      gtk_tree_path_free (target);
    }

  return FALSE;
}

static void save_lists_from_model (GtkTreeModel *model);

static gboolean
Save_shortcuts (GtkWidget *button, gpointer pointer)
{
  GtkTreeIter iter;
  GtkTreeView *treeview = (GtkTreeView *) pointer;
  GtkTreeModel *model = gtk_tree_view_get_model (treeview);
  gint i = 0;

  /* Préserver args et état polling avant de détruire les macros */
  gint old_count = 0;
  macro_t *old_macros = get_shortcuts (&old_count);
  gchar    ***saved_args           = g_new0 (gchar **,  old_count);
  gboolean  *saved_polling_enabled = g_new0 (gboolean,  old_count);
  guint     *saved_polling_period  = g_new0 (guint,     old_count);
  for (gint k = 0; k < old_count; k++)
    {
      if (old_macros[k].args != NULL)
        saved_args[k] = g_strdupv (old_macros[k].args);
      saved_polling_enabled[k] = old_macros[k].polling_enabled;
      saved_polling_period[k]  = old_macros[k].polling_period_ms;
    }

  remove_shortcuts ();

  if (gtk_tree_model_get_iter_first (model, &iter))
    {
      do
        {
          i++;
        }
      while (gtk_tree_model_iter_next (model, &iter));

      gtk_tree_model_get_iter_first (model, &iter);
      macros = g_malloc0 ((i + 1) * sizeof (macro_t));
      i = 0;
      if (macros != NULL)
        {
          do
            {
              gtk_tree_model_get (model, &iter,
                                  COLUMN_LABEL, &(macros[i].label),
                                  COLUMN_SHORTCUT, &(macros[i].shortcut),
                                  COLUMN_ACTION, &(macros[i].action),
                                  COLUMN_TAB, &(macros[i].tab),
                                  -1);

              /* Restaurer les args si le nombre de spécificateurs est identique */
              if (i < old_count && saved_args[i] != NULL &&
                  g_strv_length (saved_args[i]) == (guint) macro_count_format_args (macros[i].action))
                {
                  macros[i].args = saved_args[i];
                  saved_args[i] = NULL;
                }
              else
                {
                  macros[i].args = NULL;
                }

              /* Restaurer l'état polling */
              if (i < old_count)
                {
                  macros[i].polling_enabled    = saved_polling_enabled[i];
                  macros[i].polling_period_ms  = saved_polling_period[i]
                                                 ? saved_polling_period[i] : 1000;
                }
              else
                {
                  macros[i].polling_enabled   = FALSE;
                  macros[i].polling_period_ms = 1000;
                }
              i++;
            }
          while (gtk_tree_model_iter_next (model, &iter));

          macros[i].label = NULL;
          macros[i].shortcut = NULL;
          macros[i].action = NULL;
          macros[i].tab = NULL;
          macros[i].args = NULL;
        }
    }

  for (gint k = 0; k < old_count; k++)
    g_strfreev (saved_args[k]);
  g_free (saved_args);
  g_free (saved_polling_enabled);
  g_free (saved_polling_period);

  add_shortcuts ();

  /* Sauvegarder les listes depuis le modèle */
  if (lists_model)
    save_lists_from_model (lists_model);

  rebuild_macro_buttons ();
  macros_file_save (NULL);
  return FALSE;
}

void
macro_set_arg (gint macro_index, gint arg_index, const gchar *value)
{
  gint nb_macros = 0;
  get_shortcuts (&nb_macros);
  if (macro_index >= nb_macros || macros[macro_index].action == NULL)
    return;

  gint n_args = macro_count_format_args (macros[macro_index].action);
  if (arg_index >= n_args)
    return;

  if (macros[macro_index].args == NULL ||
      (gint) g_strv_length (macros[macro_index].args) < n_args)
    {
      gchar **new_args = g_new0 (gchar *, n_args + 1);
      for (gint k = 0; k < n_args; k++)
        {
          if (macros[macro_index].args != NULL &&
              k < (gint) g_strv_length (macros[macro_index].args) &&
              macros[macro_index].args[k] != NULL)
            new_args[k] = g_strdup (macros[macro_index].args[k]);
          else
            new_args[k] = g_strdup ("");
        }
      g_strfreev (macros[macro_index].args);
      macros[macro_index].args = new_args;
    }

  g_free (macros[macro_index].args[arg_index]);
  macros[macro_index].args[arg_index] = g_strdup (value ? value : "");
}

void
macro_set_polling (gint macro_index, gboolean enabled, guint period_ms)
{
  gint nb_macros = 0;
  get_shortcuts (&nb_macros);
  if (macro_index >= nb_macros)
    return;
  macros[macro_index].polling_enabled = enabled;
  macros[macro_index].polling_period_ms = period_ms;
}

static gboolean
key_pressed (GtkWidget *window, GdkEventKey *key, gpointer pointer)
{
  GtkTreeIter iter;
  GtkTreeView *treeview = (GtkTreeView *) pointer;
  GtkTreeModel *model = gtk_tree_view_get_model (treeview);
  GtkTreeSelection *selection = gtk_tree_view_get_selection (treeview);
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

  if (gtk_tree_selection_get_selected (selection, NULL, &iter))
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
                                   _ ("The \"action\" field of a macro is the data to be sent on the port. Text can be entered, but also special chars, like \\n, \\t, \\r, etc. You can also enter hexadecimal data preceded by a '\\'. The hexadecimal data should not begin with a letter (eg. use \\0FF and not \\FF)\nExamples:\n\t\"Hello\\n\" sends \"Hello\" followed by a Line Feed\n\t\"Hello\\0A\" does the same thing but the LF is entered in hexadecimal"));

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

  gint n_lists = macro_list_count ();
  for (gint li = 0; li < n_lists; li++)
    {
      gint n_entries = macro_list_entry_count (li);
      for (gint ei = 0; ei < n_entries; ei++)
        {
          GtkTreeIter iter;
          gtk_list_store_append (store, &iter);
          gtk_list_store_set (store, &iter,
                              LIST_COLUMN_NAME, macro_list_name (li),
                              LIST_COLUMN_DISPLAY, macro_list_entry_display (li, ei),
                              LIST_COLUMN_VALUE, macro_list_entry_value (li, ei),
                              -1);
        }
    }

  return GTK_TREE_MODEL (store);
}

static gboolean
list_name_edited (GtkCellRendererText *cell,
                  const gchar *path_string,
                  const gchar *new_text,
                  gpointer data)
{
  GtkTreeModel *model = (GtkTreeModel *) data;
  GtkTreeIter iter;
  GtkTreePath *path = gtk_tree_path_new_from_string (path_string);

  if (gtk_tree_model_get_iter (model, &iter, path))
    gtk_list_store_set (GTK_LIST_STORE (model), &iter, LIST_COLUMN_NAME, new_text, -1);

  gtk_tree_path_free (path);
  return TRUE;
}

static gboolean
list_display_edited (GtkCellRendererText *cell,
                     const gchar *path_string,
                     const gchar *new_text,
                     gpointer data)
{
  GtkTreeModel *model = (GtkTreeModel *) data;
  GtkTreeIter iter;
  GtkTreePath *path = gtk_tree_path_new_from_string (path_string);

  if (gtk_tree_model_get_iter (model, &iter, path))
    gtk_list_store_set (GTK_LIST_STORE (model), &iter, LIST_COLUMN_DISPLAY, new_text, -1);

  gtk_tree_path_free (path);
  return TRUE;
}

static gboolean
list_value_edited (GtkCellRendererText *cell,
                   const gchar *path_string,
                   const gchar *new_text,
                   gpointer data)
{
  GtkTreeModel *model = (GtkTreeModel *) data;
  GtkTreeIter iter;
  GtkTreePath *path = gtk_tree_path_new_from_string (path_string);

  if (gtk_tree_model_get_iter (model, &iter, path))
    gtk_list_store_set (GTK_LIST_STORE (model), &iter, LIST_COLUMN_VALUE, new_text, -1);

  gtk_tree_path_free (path);
  return TRUE;
}

static void
add_list_columns (GtkTreeView *treeview)
{
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;
  GtkTreeModel *model = gtk_tree_view_get_model (treeview);

  renderer = gtk_cell_renderer_text_new ();
  g_signal_connect (renderer, "edited", G_CALLBACK (list_name_edited), model);
  g_object_set (G_OBJECT (renderer), "editable", TRUE, NULL);
  column = gtk_tree_view_column_new_with_attributes (_ ("List"), renderer, "text", LIST_COLUMN_NAME, NULL);
  gtk_tree_view_column_set_resizable (column, TRUE);
  gtk_tree_view_column_set_min_width (column, 80);
  gtk_tree_view_append_column (treeview, column);

  renderer = gtk_cell_renderer_text_new ();
  g_signal_connect (renderer, "edited", G_CALLBACK (list_display_edited), model);
  g_object_set (G_OBJECT (renderer), "editable", TRUE, NULL);
  column = gtk_tree_view_column_new_with_attributes (_ ("Display"), renderer, "text", LIST_COLUMN_DISPLAY, NULL);
  gtk_tree_view_column_set_resizable (column, TRUE);
  gtk_tree_view_column_set_min_width (column, 80);
  gtk_tree_view_append_column (treeview, column);

  renderer = gtk_cell_renderer_text_new ();
  g_signal_connect (renderer, "edited", G_CALLBACK (list_value_edited), model);
  g_object_set (G_OBJECT (renderer), "editable", TRUE, NULL);
  column = gtk_tree_view_column_new_with_attributes (_ ("Value"), renderer, "text", LIST_COLUMN_VALUE, NULL);
  gtk_tree_view_column_set_resizable (column, TRUE);
  gtk_tree_view_column_set_min_width (column, 120);
  gtk_tree_view_append_column (treeview, column);
}

static gboolean
Add_list_entry (GtkWidget *button, gpointer pointer)
{
  GtkTreeIter iter, new_iter;
  GtkTreeView *treeview = (GtkTreeView *) pointer;
  GtkTreeSelection *selection = gtk_tree_view_get_selection (treeview);

  if (gtk_tree_selection_get_selected (selection, NULL, &iter))
    {
      gtk_list_store_insert_after (GTK_LIST_STORE (lists_model), &new_iter, &iter);
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
  GtkTreeIter iter;
  GtkTreeView *treeview = (GtkTreeView *) pointer;
  GtkTreeSelection *selection = gtk_tree_view_get_selection (treeview);

  if (gtk_tree_selection_get_selected (selection, NULL, &iter))
    {
      gtk_list_store_remove (GTK_LIST_STORE (gtk_tree_view_get_model (treeview)), &iter);
    }

  return FALSE;
}

static void
save_lists_from_model (GtkTreeModel *model)
{
  macro_lists_free ();
  macro_lists_init ();

  GtkTreeIter iter;
  if (gtk_tree_model_get_iter_first (model, &iter))
    {
      do
        {
          gchar *name = NULL, *display = NULL, *value = NULL;
          gtk_tree_model_get (model, &iter,
                              LIST_COLUMN_NAME, &name,
                              LIST_COLUMN_DISPLAY, &display,
                              LIST_COLUMN_VALUE, &value,
                              -1);
          if (name && display)
            macro_list_add (name, display, value ? value : display);
          g_free (name);
          g_free (display);
          g_free (value);
        }
      while (gtk_tree_model_iter_next (model, &iter));
    }
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

void
Config_macros (GtkAction *action, gpointer data)
{
  GtkWidget *vbox, *hbox;
  GtkWidget *sw;
  GtkTreeModel *model;
  GtkWidget *treeview;
  GtkWidget *button;
  GtkWidget *separator;
  g_print ("Config_macros called\n");

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title (GTK_WINDOW (window), _ ("Configure Macros"));

  g_signal_connect (window, "destroy",
                    G_CALLBACK (gtk_widget_destroyed), &window);
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

/* --- Fichier macros séparé --- */

static gchar *macros_file_path = NULL;

const gchar *
macros_file_get_default_path (void)
{
  return g_build_filename (g_get_user_config_dir (), "gtkterm_macros.ini", NULL);
}

void
macros_file_set_path (const gchar *path)
{
  g_free (macros_file_path);
  macros_file_path = g_strdup (path);
}

const gchar *
macros_file_get_path (void)
{
  return macros_file_path;
}

static gchar *
get_macros_file_path (void)
{
  if (macros_file_path)
    return macros_file_path;
  static gchar *default_path = NULL;
  if (!default_path)
    default_path = (gchar *) macros_file_get_default_path ();
  return default_path;
}

static void
write_macros_to_file (const gchar *path, macro_t *m, gint size)
{
  GError *error = NULL;
  GKeyFile *kf = g_key_file_new ();

  for (gint i = 0; i < size; i++)
    {
      gchar *args_str = m[i].args ? g_strjoinv ("|", m[i].args) : g_strdup ("");
      gchar *key = g_strdup_printf ("macro_%d", i);
      gchar *val = g_strdup_printf ("%s::%s::%s::%s::%s::%d::%u",
                                    m[i].label    ? m[i].label    : "",
                                    m[i].shortcut ? m[i].shortcut : "",
                                    m[i].action   ? m[i].action   : "",
                                    m[i].tab      ? m[i].tab      : "",
                                    args_str,
                                    m[i].polling_enabled ? 1 : 0,
                                    m[i].polling_period_ms);
      g_key_file_set_string (kf, "macros", key, val);
      g_free (key);
      g_free (val);
      g_free (args_str);
    }

  gint n_lists = macro_list_count ();
  for (gint li = 0; li < n_lists; li++)
    {
      GString *list_str = g_string_new (macro_list_name (li));
      g_string_append_c (list_str, ':');
      gint n_entries = macro_list_entry_count (li);
      for (gint ei = 0; ei < n_entries; ei++)
        {
          if (ei > 0)
            g_string_append_c (list_str, '|');
          g_string_append (list_str, macro_list_entry_display (li, ei));
          g_string_append_c (list_str, '=');
          g_string_append (list_str, macro_list_entry_value (li, ei));
        }
      gchar *key = g_strdup_printf ("list_%d", li);
      g_key_file_set_string (kf, "lists", key, list_str->str);
      g_free (key);
      g_string_free (list_str, TRUE);
    }

  gchar *data = g_key_file_to_data (kf, NULL, &error);
  if (data)
    {
      g_file_set_contents (path, data, -1, &error);
      g_free (data);
    }

  g_key_file_free (kf);

  if (error)
    {
      g_printerr ("Error writing macros file: %s\n", error->message);
      g_error_free (error);
    }
}

void
macros_file_save (const gchar *path)
{
  gint size = 0;
  macro_t *m = get_shortcuts (&size);
  gchar *save_path = path ? g_strdup (path) : g_strdup (get_macros_file_path ());

  g_free (macros_file_path);
  macros_file_path = g_strdup (save_path);

  write_macros_to_file (save_path, m, size);
  g_free (save_path);
}

void
macros_file_load (const gchar *path)
{
  GError *error = NULL;
  GKeyFile *kf = g_key_file_new ();
  gchar *load_path = path ? g_strdup (path) : g_strdup (get_macros_file_path ());

  if (!g_file_test (load_path, G_FILE_TEST_EXISTS))
    {
      g_free (load_path);
      g_key_file_free (kf);
      return;
    }

  if (!g_key_file_load_from_file (kf, load_path, G_KEY_FILE_NONE, &error))
    {
      g_printerr ("Error reading macros file: %s\n", error->message);
      g_error_free (error);
      g_free (load_path);
      g_key_file_free (kf);
      return;
    }

  g_free (macros_file_path);
  macros_file_path = g_strdup (load_path);
  g_free (load_path);

  /* Charger les macros */
  gchar **macro_keys = g_key_file_get_keys (kf, "macros", NULL, &error);
  if (macro_keys)
    {
      gint count = g_strv_length (macro_keys);
      macro_t *new_macros = g_malloc0 ((count + 1) * sizeof (macro_t));

      for (gint i = 0; i < count; i++)
        {
          gchar *val = g_key_file_get_string (kf, "macros", macro_keys[i], NULL);
          if (val)
            {
              gchar *sep1 = strstr (val, "::");
              gchar *sep2 = sep1 ? strstr (sep1 + 2, "::") : NULL;

              if (sep1 && sep2)
                {
                  new_macros[i].label = g_strndup (val, sep1 - val);
                  new_macros[i].shortcut = g_strndup (sep1 + 2, sep2 - (sep1 + 2));

                  gchar *sep3 = strstr (sep2 + 2, "::");
                  if (sep3)
                    {
                      gchar *sep4 = strstr (sep3 + 2, "::");
                      if (sep4)
                        {
                          new_macros[i].action = g_strndup (sep2 + 2, sep3 - (sep2 + 2));
                          new_macros[i].tab = g_strndup (sep3 + 2, sep4 - (sep3 + 2));

                          gchar *sep5 = strstr (sep4 + 2, "::");
                          if (sep5)
                            {
                              gchar *args_str = g_strndup (sep4 + 2, sep5 - (sep4 + 2));
                              gchar **args_arr = g_strsplit (args_str, "|", -1);
                              g_free (args_str);
                              gint n_fmt = macro_count_format_args (new_macros[i].action);
                              if (n_fmt > 0 && (gint) g_strv_length (args_arr) == n_fmt)
                                new_macros[i].args = args_arr;
                              else
                                {
                                  g_strfreev (args_arr);
                                  new_macros[i].args = NULL;
                                }

                              gchar *sep6 = strstr (sep5 + 2, "::");
                              if (sep6)
                                {
                                  gchar *en_str = g_strndup (sep5 + 2, sep6 - (sep5 + 2));
                                  new_macros[i].polling_enabled = (g_strcmp0 (en_str, "1") == 0);
                                  g_free (en_str);
                                  new_macros[i].polling_period_ms = (guint)strtoul (sep6 + 2, NULL, 10);
                                }
                              else
                                {
                                  gchar *en_str = g_strdup (sep5 + 2);
                                  new_macros[i].polling_enabled = (g_strcmp0 (en_str, "1") == 0);
                                  g_free (en_str);
                                  new_macros[i].polling_period_ms = 1000;
                                }
                            }
                          else
                            {
                              gchar **args_arr = g_strsplit (sep4 + 2, "|", -1);
                              gint n_fmt = macro_count_format_args (new_macros[i].action);
                              if (n_fmt > 0 && (gint) g_strv_length (args_arr) == n_fmt)
                                new_macros[i].args = args_arr;
                              else
                                {
                                  g_strfreev (args_arr);
                                  new_macros[i].args = NULL;
                                }
                              new_macros[i].polling_enabled = FALSE;
                              new_macros[i].polling_period_ms = 1000;
                            }
                        }
                      else
                        {
                          new_macros[i].action = g_strdup (sep2 + 2);
                          new_macros[i].tab = g_strdup (sep3 + 2);
                          new_macros[i].args = NULL;
                          new_macros[i].polling_enabled = FALSE;
                          new_macros[i].polling_period_ms = 1000;
                        }
                    }
                  else
                    {
                      new_macros[i].action = g_strdup (sep2 + 2);
                      new_macros[i].tab = g_strdup ("");
                      new_macros[i].args = NULL;
                      new_macros[i].polling_enabled = FALSE;
                      new_macros[i].polling_period_ms = 1000;
                    }

                  if (!new_macros[i].label) new_macros[i].label = g_strdup ("");
                  if (!new_macros[i].shortcut) new_macros[i].shortcut = g_strdup ("None");
                }
              else
                {
                  new_macros[i].label = g_strdup ("");
                  new_macros[i].shortcut = g_strdup ("None");
                  new_macros[i].action = g_strdup ("");
                  new_macros[i].tab = g_strdup ("");
                  new_macros[i].args = NULL;
                  new_macros[i].polling_enabled = FALSE;
                  new_macros[i].polling_period_ms = 1000;
                }

              g_free (val);
            }
        }

      new_macros[count].label = NULL;
      new_macros[count].shortcut = NULL;
      new_macros[count].action = NULL;
      new_macros[count].tab = NULL;
      new_macros[count].args = NULL;

      remove_shortcuts ();
      create_shortcuts (new_macros, count);

      g_free (new_macros);
    }

  /* Charger les listes */
  macro_lists_free ();
  macro_lists_init ();

  gchar **list_keys = g_key_file_get_keys (kf, "lists", NULL, &error);
  if (list_keys)
    {
      for (gint i = 0; list_keys[i] != NULL; i++)
        {
          gchar *val = g_key_file_get_string (kf, "lists", list_keys[i], NULL);
          if (val)
            {
              gchar *colon = strchr (val, ':');
              if (colon)
                {
                  gchar *list_name = g_strndup (val, colon - val);
                  gchar *entries_str = colon + 1;
                  gchar **entries = g_strsplit (entries_str, "|", -1);
                  for (gint ei = 0; entries[ei] != NULL; ei++)
                    {
                      gchar *eq = strchr (entries[ei], '=');
                      if (eq)
                        {
                          gchar *display = g_strndup (entries[ei], eq - entries[ei]);
                          gchar *value = g_strdup (eq + 1);
                          macro_list_add (list_name, display, value);
                          g_free (display);
                          g_free (value);
                        }
                      else
                        {
                          macro_list_add (list_name, entries[ei], entries[ei]);
                        }
                    }
                  g_strfreev (entries);
                  g_free (list_name);
                }
              g_free (val);
            }
        }
      g_strfreev (list_keys);
    }

  g_key_file_free (kf);
}