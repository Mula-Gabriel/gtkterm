/***********************************************************************/
/* macros_format.h                                                     */
/* ---------------                                                     */
/*           GTKTerm Software                                          */
/*                                                                     */
/*   Purpose                                                           */
/*      Macro action string parser: printf specifiers, escape        */
/*      sequences, argument info extraction, value validation.       */
/*                                                                     */
/***********************************************************************/

#ifndef MACROS_FORMAT_H_
#define MACROS_FORMAT_H_

#include <glib.h>

typedef struct
{
  gchar *type;
  gchar *list_name;
  gchar *label;
} macro_arg_info_t;

macro_arg_info_t *macro_get_arg_infos    (const gchar *action, gint *count_out);
void              macro_arg_infos_free   (macro_arg_info_t *infos, gint count);
gboolean          macro_type_is_float    (const gchar *type);
gboolean          macro_type_value_valid (const gchar *type, const gchar *value);

gchar    *macro_get_format_type  (const gchar *action);
gboolean  macro_has_format_arg   (const gchar *action);
gint      macro_count_format_args(const gchar *action);
gchar   **macro_get_format_types (const gchar *action, gint *count_out);

GByteArray *parse_macro_string      (const gchar *string);
gchar       *format_action_with_args(const gchar *action,
                                     const gchar **args, gint n_args);

#endif
