/* macro_arg.c — limites et jeux de caractères des arguments de macros (voir macro_arg.h). */

#include <config.h>
#include <errno.h>
#include <glib/gi18n.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "macro_arg.h"
#include "macros_format.h"

#define CLASS_MAX_CHARS 256
#define LENGTH_LIMIT_MAX 65535

/* Même règle que int_arg_base de macros_format.c : hexadécimal avec 0x/0X, décimal sinon, jamais d'octal. */
static gint
int_arg_base(const gchar *s)
{
	while(*s == ' ' || *s == '\t')
		s++;
	if(*s == '+' || *s == '-')
		s++;
	return (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) ? 16 : 10;
}

static gboolean
is_empty(const gchar *s)
{
	return s == NULL || s[0] == '\0';
}

/* ─── Classes ─────────────────────────────────────────────────────── */

typedef struct
{
	gunichar lo;
	gunichar hi;
} ClassRange;

/* Analyse une classe en plages ; NULL si invalide. */
static GArray *
class_parse(const gchar *cls)
{
	if(cls == NULL || cls[0] == '\0' || !g_utf8_validate(cls, -1, NULL))
		return NULL;

	GArray *ranges = g_array_new(FALSE, FALSE, sizeof(ClassRange));
	const gchar *p = cls;
	gboolean first = TRUE;
	guint64 total = 0;

	while(*p != '\0')
	{
		gunichar c = g_utf8_get_char(p);
		gboolean escaped = FALSE;

		p = g_utf8_next_char(p);
		if(c == '\\')
		{
			if(*p == '\0')
				goto invalid;
			c = g_utf8_get_char(p);
			p = g_utf8_next_char(p);
			escaped = TRUE;
		}
		else if(c == '-' && !first && *p != '\0')
			goto invalid;	/* « - » au milieu sans caractère de début */

		ClassRange r = { c, c };

		/* Plage : c '-' d, avec au moins un caractère après le tiret */
		if(*p == '-' && p[1] != '\0')
		{
			const gchar *q = g_utf8_next_char(p);
			gunichar d = g_utf8_get_char(q);

			q = g_utf8_next_char(q);
			if(d == '\\')
			{
				if(*q == '\0')
					goto invalid;
				d = g_utf8_get_char(q);
				q = g_utf8_next_char(q);
			}
			if(d < c)
				goto invalid;
			r.hi = d;
			p = q;
		}
		(void)escaped;
		total += (guint64)(r.hi - r.lo) + 1;
		if(total > CLASS_MAX_CHARS)
			goto invalid;
		g_array_append_val(ranges, r);
		first = FALSE;
	}
	return ranges;

invalid:
	g_array_free(ranges, TRUE);
	return NULL;
}

gboolean
gt_macro_class_valid(const gchar *cls)
{
	GArray *r = class_parse(cls);

	if(r == NULL)
		return FALSE;
	g_array_free(r, TRUE);
	return TRUE;
}

gboolean
gt_macro_class_contains(const gchar *cls, gunichar c)
{
	GArray *r = class_parse(cls);
	gboolean found = FALSE;

	if(r == NULL)
		return FALSE;
	for(guint i = 0; i < r->len && !found; i++)
	{
		const ClassRange *x = &g_array_index(r, ClassRange, i);

		found = c >= x->lo && c <= x->hi;
	}
	g_array_free(r, TRUE);
	return found;
}

/* ─── Jeux de caractères ──────────────────────────────────────────── */

gboolean
gt_macro_charset_parse(const gchar *text, GtMacroCharset *kind, gchar **class_out)
{
	GtMacroCharset k;

	if(class_out != NULL)
		*class_out = NULL;
	if(is_empty(text))
		k = GT_MACRO_CHARSET_UTF8;
	else if(strcmp(text, "printable") == 0)
		k = GT_MACRO_CHARSET_PRINTABLE;
	else if(strcmp(text, "ascii") == 0)
		k = GT_MACRO_CHARSET_ASCII;
	else if(strcmp(text, "latin1") == 0)
		k = GT_MACRO_CHARSET_LATIN1;
	else if(g_str_has_prefix(text, "class:"))
	{
		if(!gt_macro_class_valid(text + 6))
			return FALSE;
		k = GT_MACRO_CHARSET_CLASS;
		if(class_out != NULL)
			*class_out = g_strdup(text + 6);
	}
	else
		return FALSE;
	if(kind != NULL)
		*kind = k;
	return TRUE;
}

gchar *
gt_macro_arg_encode(const gchar *charset, const gchar *value, gsize *len_out)
{
	if(value == NULL)
		value = "";
	if(g_strcmp0(charset, "latin1") == 0)
	{
		if(!g_utf8_validate(value, -1, NULL))
			return NULL;
		GString *out = g_string_new(NULL);

		for(const gchar *p = value; *p != '\0'; p = g_utf8_next_char(p))
		{
			gunichar c = g_utf8_get_char(p);

			if(c > 0xFF)
			{
				g_string_free(out, TRUE);
				return NULL;
			}
			g_string_append_c(out, (gchar)c);
		}
		if(len_out != NULL)
			*len_out = out->len;
		return g_string_free(out, FALSE);
	}
	if(len_out != NULL)
		*len_out = strlen(value);
	return g_strdup(value);
}

/* ─── Types ───────────────────────────────────────────────────────── */

typedef enum
{
	KIND_NONE,
	KIND_SIGNED,
	KIND_UNSIGNED,
	KIND_FLOAT,
	KIND_TEXT,
	KIND_CHAR,
	KIND_LIST
} TypeKind;

static TypeKind
type_kind(const gchar *type)
{
	if(is_empty(type))
		return KIND_NONE;
	if(strcmp(type, "l") == 0)
		return KIND_LIST;

	gchar conv = type[strlen(type) - 1];

	if(conv == 'd' || conv == 'i')
		return KIND_SIGNED;
	if(conv == 'u' || conv == 'o' || conv == 'x' || conv == 'X')
		return KIND_UNSIGNED;
	if(strchr("fFeEgGaA", conv) != NULL)
		return KIND_FLOAT;
	if(conv == 's')
		return KIND_TEXT;
	if(conv == 'c')
		return KIND_CHAR;
	return KIND_NONE;
}

gboolean
gt_macro_arg_type_has_limits(const gchar *type)
{
	TypeKind k = type_kind(type);

	return k == KIND_SIGNED || k == KIND_UNSIGNED || k == KIND_FLOAT || k == KIND_TEXT;
}

const gchar *
gt_macro_arg_type_name(const gchar *type)
{
	if(is_empty(type))
		return _("text");
	if(strcmp(type, "l") == 0)
		return _("list");

	gchar conv = type[strlen(type) - 1];

	switch(conv)
	{
		case 'd':
		case 'i':
			return _("integer");
		case 'u':
			return _("unsigned");
		case 'x':
		case 'X':
			return _("hexadecimal");
		case 'o':
			return _("octal");
		case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A':
			return _("decimal");
		case 'c':
			return _("character");
		default:
			return _("text");
	}
}

/* ─── Limites ─────────────────────────────────────────────────────── */

/* Longueur d'un %s : entier décimal 0..65535, sans signe ni hexa. */
static gboolean
length_limit_parse(const gchar *s, guint *out)
{
	if(is_empty(s) || strlen(s) > 5)
		return FALSE;
	for(const gchar *p = s; *p != '\0'; p++)
		if(!g_ascii_isdigit(*p))
			return FALSE;

	guint64 v = g_ascii_strtoull(s, NULL, 10);

	if(v > LENGTH_LIMIT_MAX)
		return FALSE;
	if(out != NULL)
		*out = (guint)v;
	return TRUE;
}

typedef struct
{
	gint64 i;
	guint64 u;
	gdouble d;
} Number;

/* Lit une valeur (vide = 0) dans son domaine ; FALSE si illisible ou non finie. */
static gboolean
number_read(TypeKind kind, const gchar *s, Number *n)
{
	if(is_empty(s))
		s = "0";

	gchar *end = NULL;

	errno = 0;
	if(kind == KIND_SIGNED)
		n->i = g_ascii_strtoll(s, &end, int_arg_base(s));
	else if(kind == KIND_UNSIGNED)
		n->u = g_ascii_strtoull(s, &end, int_arg_base(s));
	else
	{
		n->d = g_ascii_strtod(s, &end);
		if(!isfinite(n->d))
			return FALSE;
	}
	return errno != ERANGE && end != s && *end == '\0';
}

static gint
number_cmp(TypeKind kind, const Number *a, const Number *b)
{
	if(kind == KIND_SIGNED)
		return (a->i > b->i) - (a->i < b->i);
	if(kind == KIND_UNSIGNED)
		return (a->u > b->u) - (a->u < b->u);
	return (a->d > b->d) - (a->d < b->d);
}

gboolean
gt_macro_arg_limit_valid(const gchar *type, const gchar *limit)
{
	if(is_empty(limit))
		return TRUE;

	TypeKind kind = type_kind(type);

	if(kind == KIND_TEXT)
		return length_limit_parse(limit, NULL);
	if(kind != KIND_SIGNED && kind != KIND_UNSIGNED && kind != KIND_FLOAT)
		return FALSE;
	if(!macro_type_value_valid(type, limit))
		return FALSE;

	Number n;

	return number_read(kind, limit, &n);
}

gboolean
gt_macro_arg_limits_valid(const gchar *type, const gchar *min, const gchar *max, const gchar *charset)
{
	if(!gt_macro_arg_limit_valid(type, min) || !gt_macro_arg_limit_valid(type, max))
		return FALSE;

	TypeKind kind = type_kind(type);

	if(!is_empty(charset) && (kind != KIND_TEXT || !gt_macro_charset_parse(charset, NULL, NULL)))
		return FALSE;
	if(!is_empty(min) && !is_empty(max))
	{
		Number a, b;

		if(kind == KIND_TEXT)
		{
			guint x = 0, y = 0;

			length_limit_parse(min, &x);
			length_limit_parse(max, &y);
			return x <= y;
		}
		if(!number_read(kind, min, &a) || !number_read(kind, max, &b))
			return FALSE;
		return number_cmp(kind, &a, &b) <= 0;
	}
	return TRUE;
}

/* ─── Vérification ────────────────────────────────────────────────── */

static GtMacroArgVerdict
verdict(GtMacroArgVerdict v, gchar **reason, gchar *text)
{
	if(reason != NULL)
		*reason = text;
	else
		g_free(text);
	return v;
}

static GtMacroArgVerdict
check_text(const gchar *type, const gchar *value, const gchar *min, const gchar *max, const gchar *charset, gchar **reason)
{
	GtMacroCharset kind = GT_MACRO_CHARSET_UTF8;
	gchar *cls = NULL;

	if(value == NULL)
		value = "";
	if(!g_utf8_validate(value, -1, NULL))
		return verdict(GT_MACRO_ARG_BAD_VALUE, reason, g_strdup_printf(_("not a valid %s"), gt_macro_arg_type_name(type)));
	if(!gt_macro_charset_parse(charset, &kind, &cls))
		kind = GT_MACRO_CHARSET_UTF8;

	for(const gchar *p = value; *p != '\0'; p = g_utf8_next_char(p))
	{
		gunichar c = g_utf8_get_char(p);
		gboolean ok = TRUE;

		switch(kind)
		{
			case GT_MACRO_CHARSET_PRINTABLE:
				ok = c >= 0x20 && c <= 0x7E;
				break;
			case GT_MACRO_CHARSET_ASCII:
				ok = c <= 0x7F;
				break;
			case GT_MACRO_CHARSET_LATIN1:
				ok = c <= 0xFF;
				break;
			case GT_MACRO_CHARSET_CLASS:
				ok = gt_macro_class_contains(cls, c);
				break;
			default:
				break;
		}
		if(!ok)
		{
			gchar buf[8] = { 0 };

			g_unichar_to_utf8(c, buf);
			g_free(cls);
			return verdict(GT_MACRO_ARG_BAD_CHAR, reason, g_strdup_printf(_("character \"%s\" not allowed"), buf));
		}
	}
	g_free(cls);

	gsize len = 0;
	gchar *enc = gt_macro_arg_encode(kind == GT_MACRO_CHARSET_LATIN1 ? "latin1" : NULL, value, &len);

	g_free(enc);

	guint lim;

	if(length_limit_parse(min, &lim) && len < lim)
		return verdict(GT_MACRO_ARG_TOO_SHORT, reason, g_strdup_printf(_("%u bytes, at least %s"), (guint)len, min));
	if(length_limit_parse(max, &lim) && len > lim)
		return verdict(GT_MACRO_ARG_TOO_LONG, reason, g_strdup_printf(_("%u bytes, at most %s"), (guint)len, max));
	return verdict(GT_MACRO_ARG_OK, reason, NULL);
}

GtMacroArgVerdict
gt_macro_arg_check(const gchar *type, const gchar *value, const gchar *min, const gchar *max,
                   const gchar *charset, gchar **reason)
{
	if(reason != NULL)
		*reason = NULL;

	TypeKind kind = type_kind(type);

	if(kind == KIND_TEXT)
		return check_text(type, value, min, max, charset, reason);
	if(!macro_type_value_valid(type, value))
		return verdict(GT_MACRO_ARG_BAD_VALUE, reason, g_strdup_printf(_("not a valid %s"), gt_macro_arg_type_name(type)));
	if(kind != KIND_SIGNED && kind != KIND_UNSIGNED && kind != KIND_FLOAT)
		return verdict(GT_MACRO_ARG_OK, reason, NULL);

	gboolean has_min = gt_macro_arg_limit_valid(type, min) && !is_empty(min);
	gboolean has_max = gt_macro_arg_limit_valid(type, max) && !is_empty(max);

	if(!has_min && !has_max)
		return verdict(GT_MACRO_ARG_OK, reason, NULL);

	Number v, lim;

	if(!number_read(kind, value, &v))	/* nan, inf : refusés dès qu'une limite existe */
		return verdict(GT_MACRO_ARG_BAD_VALUE, reason, g_strdup_printf(_("not a valid %s"), gt_macro_arg_type_name(type)));
	if(has_min && number_read(kind, min, &lim) && number_cmp(kind, &v, &lim) < 0)
		return verdict(GT_MACRO_ARG_BELOW_MIN, reason, g_strdup_printf(_("below minimum %s"), min));
	if(has_max && number_read(kind, max, &lim) && number_cmp(kind, &v, &lim) > 0)
		return verdict(GT_MACRO_ARG_ABOVE_MAX, reason, g_strdup_printf(_("above maximum %s"), max));
	return verdict(GT_MACRO_ARG_OK, reason, NULL);
}

/* ─── Description ─────────────────────────────────────────────────── */

gchar *
gt_macro_arg_describe(const gchar *type, const gchar *list_name, const gchar *min, const gchar *max,
                      const gchar *charset, const gchar *def)
{
	TypeKind kind = type_kind(type);
	GString *s = g_string_new(NULL);

	if(kind == KIND_LIST)
		g_string_append_printf(s, _("list %s"), list_name != NULL ? list_name : "");
	else
		g_string_append(s, gt_macro_arg_type_name(type));

	if(gt_macro_arg_type_has_limits(type))
	{
		gboolean has_min = !is_empty(min), has_max = !is_empty(max);

		if(has_min && has_max)
			g_string_append_printf(s, _(", %s – %s"), min, max);
		else if(has_min)
			g_string_append_printf(s, _(", ≥ %s"), min);
		else if(has_max)
			g_string_append_printf(s, _(", ≤ %s"), max);
		if((has_min || has_max) && kind == KIND_TEXT)
			g_string_append(s, _(" bytes"));
	}
	if(kind == KIND_TEXT && !is_empty(charset))
	{
		GtMacroCharset cs;
		gchar *cls = NULL;

		if(gt_macro_charset_parse(charset, &cs, &cls))
		{
			switch(cs)
			{
				case GT_MACRO_CHARSET_PRINTABLE:
					g_string_append(s, _(", printable ASCII"));
					break;
				case GT_MACRO_CHARSET_ASCII:
					g_string_append(s, _(", ASCII"));
					break;
				case GT_MACRO_CHARSET_LATIN1:
					g_string_append(s, _(", 8 bits (Latin-1)"));
					break;
				case GT_MACRO_CHARSET_CLASS:
					g_string_append_printf(s, _(", characters %s"), cls);
					break;
				default:
					break;
			}
		}
		g_free(cls);
	}
	if(!is_empty(def))
		g_string_append_printf(s, _(" (default %s)"), def);
	return g_string_free(s, FALSE);
}

gchar *
gt_macro_arg_describe_optional(const gchar *prefix)
{
	if(prefix == NULL || prefix[0] == '\0')
		return g_strdup(_("optional"));
	return g_strdup_printf(_("optional, prefix \"%s\""), prefix);
}
