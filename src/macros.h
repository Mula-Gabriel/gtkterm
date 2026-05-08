/***********************************************************************/
/* macros.h                                                            */
/* --------                                                            */
/*           GTKTerm Software                                          */
/*                      (c) Julien Schmitt                             */
/*                                                                     */
/* ------------------------------------------------------------------- */
/*                                                                     */
/*   Purpose                                                           */
/*      Functions for the management of the macros                     */
/*      - Header file -                                                */
/*                                                                     */
/***********************************************************************/

#ifndef MACROS_H_
#define MACROS_H_

typedef struct
{
  gchar *label;
  gchar *shortcut;
  gchar *action;
  gchar *tab;
  gchar **args;    /* valeurs des arguments (%d, %f, %s…), tableau NULL-terminé */
  GClosure *closure;
  gboolean polling_enabled;
  guint    polling_period_ms;
} macro_t;

/* --- Listes de valeurs (%#NomListe) --- */

typedef struct
{
  gchar *display;  /* texte affiché dans le combo */
  gchar *value;    /* valeur envoyée sur le port série */
} list_entry_t;

typedef struct
{
  gchar *name;              /* nom de la liste (ex: "Commands") */
  GPtrArray *entries;       /* GPtrArray de list_entry_t* */
} macro_list_t;

/* Gestion des listes globales */
void     macro_lists_init (void);
void     macro_lists_free (void);
gint     macro_list_find  (const gchar *name);
void     macro_list_add   (const gchar *name, const gchar *display, const gchar *value);
void     macro_list_remove_entry (gint list_idx, gint entry_idx);
gint     macro_list_entry_count (gint list_idx);
const gchar *macro_list_entry_display (gint list_idx, gint entry_idx);
const gchar *macro_list_entry_value   (gint list_idx, gint entry_idx);
gint     macro_list_count (void);
const gchar *macro_list_name (gint list_idx);

/* --- Info sur les arguments d'une macro (inclut les listes) --- */

typedef struct
{
  gchar *type;           /* "d","ld","f","lf","s","l"... ou "l" pour liste */
  gchar *list_name;      /* nom de la liste si type=='l', sinon NULL */
  gchar *label;          /* texte du label (ex: "[addr]%s" → "addr"), sinon NULL */
} macro_arg_info_t;

macro_arg_info_t *macro_get_arg_infos (const gchar *action, gint *count_out);
void              macro_arg_infos_free (macro_arg_info_t *infos, gint count);
gboolean          macro_type_is_float  (const gchar *type);
gboolean          macro_type_value_valid (const gchar *type, const gchar *value);

void Config_macros (GtkAction *action, gpointer data);
void remove_shortcuts (void);
void add_shortcuts (void);
void create_shortcuts (macro_t *, gint);
void shortcut_callback(gpointer number);
macro_t *get_shortcuts (gint *);
gchar   *macro_get_format_type  (const gchar *action);
gboolean macro_has_format_arg   (const gchar *action);
gint     macro_count_format_args(const gchar *action);
gchar  **macro_get_format_types (const gchar *action, gint *count_out);
void     send_macro_with_arg    (gint macro_index, const gchar *arg_str);
void     send_macro_with_args   (gint macro_index, const gchar **args, gint n_args);
void     macro_set_arg          (gint macro_index, gint arg_index, const gchar *value);
void     macro_set_polling      (gint macro_index, gboolean enabled, guint period_ms);

/* --- Fichier macros séparé --- */
void     macros_file_load (const gchar *path);
void     macros_file_save (const gchar *path);
const gchar *macros_file_get_default_path (void);
void     macros_file_set_path (const gchar *path);
const gchar *macros_file_get_path (void);

#endif

