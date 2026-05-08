/***********************************************************************/
/* macros_format.c                                                     */
/* ---------------                                                     */
/*           GTKTerm Software                                          */
/*                                                                     */
/*   Purpose                                                           */
/*      Macro action string parser: printf specifiers, escape        */
/*      sequences, argument info extraction, value validation.       */
/*                                                                     */
/***********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include "macros_format.h"
#include "macros_list.h"

/* ─── Escape sequence parsing ─────────────────────────────────────── */

static guchar
parse_hex_escape (const gchar *string, gint *index)
{
  gint i = *index;
  gint hex_start = 0;
  gint isTwodigits = 0;

  if (string[i + 1] == '0' && string[i + 2] != '\0'
      && g_unichar_isxdigit ((gunichar) string[i + 2]))
    {
      hex_start = i + 2;
      isTwodigits = (string[i + 3] != '\0'
                     && g_unichar_isxdigit ((gunichar) string[i + 3])) ? 1 : 0;
    }
  else if (g_unichar_isxdigit ((gunichar) string[i + 1]))
    {
      hex_start = i + 1;
      isTwodigits = (string[i + 2] != '\0'
                     && g_unichar_isxdigit ((gunichar) string[i + 2])) ? 1 : 0;
    }
  else
    {
      return '\\';
    }

  guint val_read;
  if (sscanf (&string[hex_start], "%02X", &val_read) == 1)
    {
      *index = hex_start + isTwodigits;
      return (guchar) val_read;
    }

  return '\\';
}

static guchar
parse_standard_escape (gchar escape_char, gint *index)
{
  guchar result;

  switch (escape_char)
    {
    case 'a': result = '\a'; break;
    case 'b': result = '\b'; break;
    case 't': result = '\t'; break;
    case 'n': result = '\n'; break;
    case 'v': result = '\v'; break;
    case 'f': result = '\f'; break;
    case 'r': result = '\r'; break;
    case '\\': result = '\\'; break;
    default:   return '\\';
    }

  (*index)++;
  return result;
}

GByteArray *
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
            byte = parse_hex_escape (string, &i);
          else
            byte = parse_standard_escape (string[i + 1], &i);
        }
      else
        {
          byte = (guchar) string[i];
        }

      g_byte_array_append (buffer, &byte, 1);
    }

  return buffer;
}

/* ─── Printf-format specifier parsing ──────────────────────────────── */

static gchar *
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
  const gchar *modifier = "";
  if (action[j] == 'h' && action[j + 1] == 'h')
    { modifier = "hh"; j += 2; }
  else if (action[j] == 'h')
    { modifier = "h";  j++; }
  else if (action[j] == 'l' && action[j + 1] == 'l')
    { modifier = "ll"; j += 2; }
  else if (action[j] == 'l')
    { modifier = "l";  j++; }
  else if (action[j] == 'L')
    { modifier = "L";  j++; }
  else if (action[j] == 'z' || action[j] == 'j' || action[j] == 't')
    { j++; }
  if (action[j] != '\0' && strchr ("diouxXeEfFgGaAcs", action[j]))
    {
      if (end_out)
        *end_out = j;
      return g_strdup_printf ("%s%c", modifier, action[j]);
    }
  return NULL;
}

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

static gboolean
try_parse_list_spec (const gchar *action, gint start,
                     gchar **name_out, gint *end_out)
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

static gchar *
apply_spec (const gchar *spec, const gchar *fmt_type, const gchar *arg_str)
{
  if (!fmt_type || !fmt_type[0] || !arg_str)
    return g_strdup ("");

  gchar conv = fmt_type[strlen (fmt_type) - 1];

  if (conv == 'd' || conv == 'i')
    {
      if (g_strcmp0 (fmt_type, "ld") == 0 || g_strcmp0 (fmt_type, "li") == 0)
        return g_strdup_printf (spec, (long) strtol (arg_str, NULL, 10));
      if (g_strcmp0 (fmt_type, "lld") == 0 || g_strcmp0 (fmt_type, "lli") == 0)
        return g_strdup_printf (spec, (long long) strtoll (arg_str, NULL, 10));
      if (g_strcmp0 (fmt_type, "hd") == 0 || g_strcmp0 (fmt_type, "hi") == 0)
        return g_strdup_printf (spec, (short) strtol (arg_str, NULL, 10));
      if (g_strcmp0 (fmt_type, "hhd") == 0 || g_strcmp0 (fmt_type, "hhi") == 0)
        return g_strdup_printf (spec, (signed char) strtol (arg_str, NULL, 10));
      return g_strdup_printf (spec, (int) strtol (arg_str, NULL, 10));
    }

  if (conv == 'u' || conv == 'o')
    {
      if (g_strcmp0 (fmt_type, "lu") == 0 || g_strcmp0 (fmt_type, "lo") == 0)
        return g_strdup_printf (spec, (unsigned long) strtoul (arg_str, NULL, 10));
      if (g_strcmp0 (fmt_type, "llu") == 0 || g_strcmp0 (fmt_type, "llo") == 0)
        return g_strdup_printf (spec, (unsigned long long) strtoull (arg_str, NULL, 10));
      if (g_strcmp0 (fmt_type, "hu") == 0 || g_strcmp0 (fmt_type, "ho") == 0)
        return g_strdup_printf (spec, (unsigned short) strtoul (arg_str, NULL, 10));
      if (g_strcmp0 (fmt_type, "hhu") == 0 || g_strcmp0 (fmt_type, "hho") == 0)
        return g_strdup_printf (spec, (unsigned char) strtoul (arg_str, NULL, 10));
      return g_strdup_printf (spec, (unsigned int) strtoul (arg_str, NULL, 10));
    }

  if (conv == 'x' || conv == 'X')
    {
      if (g_strcmp0 (fmt_type, "lx") == 0 || g_strcmp0 (fmt_type, "lX") == 0)
        return g_strdup_printf (spec, (unsigned long) strtoul (arg_str, NULL, 0));
      if (g_strcmp0 (fmt_type, "llx") == 0 || g_strcmp0 (fmt_type, "llX") == 0)
        return g_strdup_printf (spec, (unsigned long long) strtoull (arg_str, NULL, 0));
      if (g_strcmp0 (fmt_type, "hx") == 0 || g_strcmp0 (fmt_type, "hX") == 0)
        return g_strdup_printf (spec, (unsigned short) strtoul (arg_str, NULL, 0));
      if (g_strcmp0 (fmt_type, "hhx") == 0 || g_strcmp0 (fmt_type, "hhX") == 0)
        return g_strdup_printf (spec, (unsigned char) strtoul (arg_str, NULL, 0));
      return g_strdup_printf (spec, (unsigned int) strtoul (arg_str, NULL, 0));
    }

  if (strchr ("fFeEgGaA", conv))
    {
      if (fmt_type[0] == 'L')
        {
          long double val = strtold (arg_str, NULL);
          return g_strdup_printf ("%.19Lg", val);
        }
      double val = strtod (arg_str, NULL);
      if (fmt_type[0] == 'l')
        return g_strdup_printf ("%.16g", val);
      return g_strdup_printf ("%.9g", val);
    }

  if (conv == 's')
    return g_strdup_printf (spec, arg_str);

  if (conv == 'c')
    return g_strdup_printf (spec, (int) (arg_str[0] ? arg_str[0] : ' '));

  return g_strdup ("");
}

/* ─── Public format-type queries ───────────────────────────────────── */

gchar *
macro_get_format_type (const gchar *action)
{
  if (action == NULL)
    return NULL;
  for (gint i = 0; action[i] != '\0'; i++)
    {
      if (action[i] != '%')
        continue;
      if (action[i + 1] == '%')
        { i++; continue; }
      gchar *t = parse_one_spec (action, i, NULL);
      if (t != NULL)
        return t;
    }
  return NULL;
}

gboolean
macro_has_format_arg (const gchar *action)
{
  gchar *t = macro_get_format_type (action);
  gboolean has = (t != NULL);
  g_free (t);
  return has;
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
        { i++; continue; }
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
      gchar *t = parse_one_spec (action, i, &end);
      if (t != NULL)
        {
          count++;
          i = end;
        }
      g_free (t);
    }
  return count;
}

gchar **
macro_get_format_types (const gchar *action, gint *count_out)
{
  gint n = macro_count_format_args (action);
  if (count_out)
    *count_out = n;
  if (n == 0)
    return NULL;
  gchar **types = g_new0 (gchar *, n + 1);
  gint idx = 0;
  for (gint i = 0; action[i] != '\0' && idx < n; i++)
    {
      if (action[i] != '%')
        continue;
      if (action[i + 1] == '%')
        { i++; continue; }
      {
        gchar *list_name = NULL;
        gint end;
        if (try_parse_list_spec (action, i, &list_name, &end))
          {
            types[idx++] = g_strdup ("l");
            i = end;
            g_free (list_name);
            continue;
          }
      }
      gint end;
      gchar *t = parse_one_spec (action, i, &end);
      if (t != NULL)
        { types[idx++] = t; i = end; }
    }
  types[n] = NULL;
  return types;
}

/* ─── Argument info extraction ─────────────────────────────────────── */

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
        { i++; continue; }

      (void) try_parse_label (action, i, &infos[idx].label);

      {
        gchar *list_name = NULL;
        gint end;
        if (try_parse_list_spec (action, i, &list_name, &end))
          {
            infos[idx].type = g_strdup ("l");
            infos[idx].list_name = list_name;
            idx++;
            i = end;
            continue;
          }
      }

      gint end;
      gchar *t = parse_one_spec (action, i, &end);
      if (t != NULL)
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
      g_free (infos[i].type);
    }
  g_free (infos);
}

/* ─── Type query helpers ───────────────────────────────────────────── */

gboolean
macro_type_is_float (const gchar *type)
{
  if (!type || !type[0])
    return FALSE;
  gchar last = type[strlen (type) - 1];
  return strchr ("fFeEgGaA", last) != NULL;
}

gboolean
macro_type_value_valid (const gchar *type, const gchar *value)
{
  if (!type || !type[0] || !value || !value[0])
    return TRUE;

  gchar conv = type[strlen (type) - 1];

  if (conv == 's' || conv == 'c' || g_strcmp0 (type, "l") == 0)
    return TRUE;

  if (strchr ("fFeEgGaA", conv))
    {
      gchar *end = NULL;
      errno = 0;
      strtod (value, &end);
      if (errno == ERANGE || end == value || *end != '\0')
        return FALSE;

      gint limit;
      if (type[0] == 'L')
        limit = 19;
      else if (type[0] == 'l')
        limit = 16;
      else
        limit = 9;
      gint digits = 0;
      for (const gchar *p = value; *p != '\0' && *p != 'e' && *p != 'E'; p++)
        if (g_ascii_isdigit (*p))
          digits++;
      return digits <= limit;
    }

  {
    gchar *end = NULL;
    errno = 0;

    if (conv == 'd' || conv == 'i')
      {
        gint64 v = g_ascii_strtoll (value, &end, 10);
        if (errno == ERANGE || end == value || *end != '\0')
          return FALSE;
        if (g_strcmp0 (type, "hd") == 0 || g_strcmp0 (type, "hi") == 0)
          return v >= SHRT_MIN && v <= SHRT_MAX;
        if (g_strcmp0 (type, "hhd") == 0 || g_strcmp0 (type, "hhi") == 0)
          return v >= SCHAR_MIN && v <= SCHAR_MAX;
        if (g_strcmp0 (type, "ld") == 0 || g_strcmp0 (type, "li") == 0)
          return v >= LONG_MIN && v <= LONG_MAX;
        if (g_strcmp0 (type, "lld") == 0 || g_strcmp0 (type, "lli") == 0)
          return TRUE;
        return v >= INT_MIN && v <= INT_MAX;
      }

    if (conv == 'u' || conv == 'o' || conv == 'x' || conv == 'X')
      {
        gint base = (conv == 'x' || conv == 'X') ? 0 : 10;
        guint64 v = g_ascii_strtoull (value, &end, base);
        if (errno == ERANGE || end == value || *end != '\0')
          return FALSE;
        if (g_strcmp0 (type, "hu") == 0 || g_strcmp0 (type, "ho") == 0 ||
            g_strcmp0 (type, "hx") == 0 || g_strcmp0 (type, "hX") == 0)
          return v <= USHRT_MAX;
        if (g_strcmp0 (type, "hhu") == 0 || g_strcmp0 (type, "hho") == 0 ||
            g_strcmp0 (type, "hhx") == 0 || g_strcmp0 (type, "hhX") == 0)
          return v <= UCHAR_MAX;
        if (g_strcmp0 (type, "lu") == 0 || g_strcmp0 (type, "lo") == 0 ||
            g_strcmp0 (type, "lx") == 0 || g_strcmp0 (type, "lX") == 0)
          return v <= ULONG_MAX;
        if (g_strcmp0 (type, "llu") == 0 || g_strcmp0 (type, "llo") == 0 ||
            g_strcmp0 (type, "llx") == 0 || g_strcmp0 (type, "llX") == 0)
          return TRUE;
        return v <= UINT_MAX;
      }
  }

  return TRUE;
}

/* ─── Format action with arguments ─────────────────────────────────── */

gchar *
format_action_with_args (const gchar *action, const gchar **args,
                         gint n_args)
{
  GString *result = g_string_new ("");
  gint arg_idx = 0;

  for (gint i = 0; action[i] != '\0'; i++)
    {
      if (action[i] != '%')
        { g_string_append_c (result, action[i]); continue; }
      if (action[i + 1] == '%')
        { g_string_append_c (result, '%'); i++; continue; }

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
      gchar *fmt_type = parse_one_spec (action, i, &spec_end);

      if (fmt_type == NULL)
        { g_string_append_c (result, action[i]); continue; }

      if (arg_idx < n_args)
        {
          gchar *spec = g_strndup (&action[i], spec_end - i + 1);
          gchar *formatted = apply_spec (spec, fmt_type,
                                         args[arg_idx] ? args[arg_idx] : "");
          g_string_append (result, formatted);
          g_free (formatted);
          g_free (spec);
          arg_idx++;
        }
      else
        {
          g_string_append_len (result, &action[i], spec_end - i + 1);
        }
      g_free (fmt_type);
      i = spec_end;
    }
  return g_string_free (result, FALSE);
}
