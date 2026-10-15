/* macro_arg.h — limites et jeux de caractères des arguments de macros (cœur, sans GTK).
 * Voir docs/superpowers/specs/2026-10-14-arguments-macros-design.md. */

#ifndef MACRO_ARG_H_
#define MACRO_ARG_H_

#include <glib.h>

typedef enum
{
	GT_MACRO_CHARSET_UTF8,
	GT_MACRO_CHARSET_PRINTABLE,
	GT_MACRO_CHARSET_ASCII,
	GT_MACRO_CHARSET_LATIN1,
	GT_MACRO_CHARSET_CLASS
} GtMacroCharset;

typedef enum
{
	GT_MACRO_ARG_OK,
	GT_MACRO_ARG_BAD_VALUE,
	GT_MACRO_ARG_BELOW_MIN,
	GT_MACRO_ARG_ABOVE_MAX,
	GT_MACRO_ARG_TOO_SHORT,
	GT_MACRO_ARG_TOO_LONG,
	GT_MACRO_ARG_BAD_CHAR
} GtMacroArgVerdict;

/* NULL ou "" → UTF8 ; "printable", "ascii", "latin1" ; "class:<classe>" (classe valide, *class_out = copie de la classe,
 * à libérer ; NULL dans les autres cas) ; sinon FALSE. class_out peut être NULL. */
gboolean          gt_macro_charset_parse(const gchar *text, GtMacroCharset *kind, gchar **class_out);
/* Classe : non vide, caractères isolés et plages a-b (a ≤ b), « - » littéral au bord, « \ » protège le suivant, 256 caractères au plus. */
gboolean          gt_macro_class_valid(const gchar *cls);
gboolean          gt_macro_class_contains(const gchar *cls, gunichar c);	/* cls supposée valide */
/* Entiers, décimaux et « s » ont des limites ; « c » et les listes non. */
gboolean          gt_macro_arg_type_has_limits(const gchar *type);
gboolean          gt_macro_arg_limit_valid(const gchar *type, const gchar *limit);	/* "" ou NULL : TRUE */
gboolean          gt_macro_arg_limits_valid(const gchar *type, const gchar *min, const gchar *max, const gchar *charset);
/* Octets envoyés pour la valeur d'un %s : latin1 → ISO-8859-1 (NULL si un caractère dépasse U+00FF) ; sinon copie.
 * *len_out = longueur (peut être NULL). À libérer avec g_free. */
gchar            *gt_macro_arg_encode(const gchar *charset, const gchar *value, gsize *len_out);
/* Validité du type (macro_type_value_valid), puis limites. reason (peut être NULL) : texte traduit, NULL si OK ; g_free. */
GtMacroArgVerdict gt_macro_arg_check(const gchar *type, const gchar *value, const gchar *min, const gchar *max,
                                     const gchar *charset, gchar **reason);
const gchar      *gt_macro_arg_type_name(const gchar *type);	/* traduit : integer, unsigned, hexadecimal, octal, decimal, text, character, list */
/* Bulle du panneau : « integer, 0 – 100 (default 50) ». À libérer avec g_free. */
gchar            *gt_macro_arg_describe(const gchar *type, const gchar *list_name, const gchar *min, const gchar *max,
                                        const gchar *charset, const gchar *def);
/* « optional » ou « optional, prefix "…" » (bulle, éditeur). À libérer avec g_free. */
gchar            *gt_macro_arg_describe_optional(const gchar *prefix);

#endif
