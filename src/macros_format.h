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
#include "macros_list.h"

/* Valeur de arg_optional d'un drapeau : facultatif sans valeur, présent (valeur ni vide ni "0") il n'envoie que son préfixe */
#define GT_MACRO_ARG_FLAG "flag"

typedef struct
{
  gchar *type;
  gchar *list_name;
  gchar *label;
  gchar *spec;       /* spécificateur tel qu'écrit dans l'action, sans libellé : « %.1f », « %#Mode » */
} macro_arg_info_t;

macro_arg_info_t *macro_get_arg_infos    (const GtMacroLists *lists, const gchar *action, gint *count_out);
void              macro_arg_infos_free   (macro_arg_info_t *infos, gint count);
/* Bornes (octets) du k-ième spécificateur : start = « % », end = dernier caractère (inclus), label_start = « [ » de son libellé ou -1.
   FALSE si k n'existe pas ; pointeurs de sortie nullables. */
gboolean          macro_arg_spec_bounds  (const GtMacroLists *lists, const gchar *action, gint k, gint *start, gint *end, gint *label_start);
gboolean          macro_type_is_float    (const gchar *type);
gboolean          macro_type_value_valid (const gchar *type, const gchar *value);

gint      macro_count_format_args(const GtMacroLists *lists, const gchar *action);

GByteArray *parse_macro_string      (const gchar *string);
gchar       *format_action_with_args(const GtMacroLists *lists, const gchar *action,
                                     const gchar **args, gint n_args);

/* ─── Usage du cœur (macro_set.c) : variantes tracées ─────────────────
   Les fonctions publiques ci-dessus en sont les appels sans traçage : leurs octets ne changent pas. */

/* Plage de caractères (octets) dans un texte. */
typedef struct
{
  guint start;
  guint len;
} GtMacroRange;

/* format_action_with_args, plus : arg_ranges (nullable, GArray de GtMacroRange) reçoit, pour chaque argument mis en forme (rang 0..),
   sa plage dans le texte rendu, après le doublement des antislashs ; charsets (nullable, parallèle à args, entrées NULL permises) :
   un %s en « latin1 » voit sa valeur convertie en ISO-8859-1 avant la mise en forme (gardée telle quelle si la conversion échoue) ;
   optional et prefixes (nullables, parallèles aux spécificateurs) : un argument facultatif ("1") vide ou non fourni est omis (plage vide
   notée), sinon son préfixe, copié brut, précède sa valeur (la plage couvre préfixe et valeur). */
gchar      *format_action_traced     (const GtMacroLists *lists, const gchar *action, const gchar **args, gint n_args,
                                      const gchar *const *charsets, const gchar *const *optional,
                                      const gchar *const *prefixes, GArray *arg_ranges);
/* parse_macro_string, plus : origins (nullable, GArray de GtMacroRange) reçoit, pour chaque octet produit, la plage des caractères
   de string dont il est issu. */
GByteArray *parse_macro_string_traced(const gchar *string, GArray *origins);

#endif
