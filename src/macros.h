/***********************************************************************/
/* macros.h                                                            */
/* --------                                                            */
/*           GTKTerm Software                                          */
/*                      (c) Julien Schmitt                             */
/*                                                                     */
/* ------------------------------------------------------------------- */
/*                                                                     */
/*   Purpose                                                           */
/*      Umbrella header - includes all macro sub-modules              */
/*                                                                     */
/***********************************************************************/

#ifndef MACROS_H_
#define MACROS_H_

#include <gtk/gtk.h>
#include "macros_list.h"
#include "macros_format.h"

typedef struct
{
  gchar *label;
  gchar *shortcut;
  gchar *action;
  gchar *tab;
  gchar **args;
  GClosure *closure;
  gboolean polling_enabled;
  guint    polling_period_ms;
} macro_t;

void Config_macros (GtkAction *action, gpointer data);
void remove_shortcuts  (void);
void add_shortcuts     (void);
void create_shortcuts  (macro_t *, gint);
void shortcut_callback (gpointer number);
macro_t *get_shortcuts (gint *);
void     send_macro_with_arg  (gint macro_index, const gchar *arg_str);
void     send_macro_with_args (gint macro_index, const gchar **args, gint n_args);
void     macro_set_arg        (gint macro_index, gint arg_index, const gchar *value);
void     macro_set_polling    (gint macro_index, gboolean enabled, guint period_ms);

void         macros_file_load              (const gchar *path);
void         macros_file_save              (const gchar *path);
const gchar *macros_file_get_default_path  (void);
void         macros_file_set_path          (const gchar *path);
const gchar *macros_file_get_path          (void);

#endif
