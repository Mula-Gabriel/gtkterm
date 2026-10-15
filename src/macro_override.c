/* macro_override.c — valeurs propres d'un onglet lié à un modèle de macros (voir macro_override.h). */

#include <config.h>
#include <glib/gi18n.h>
#include <string.h>

#include "macro_override.h"
#include "macro_arg.h"
#include "macros_format.h"

/* Plus grand rang d'argument accepté à la lecture (« arg_1000000000 » n'alloue pas un tableau géant) */
#define MAX_ARG_KEY 1024u

struct _GtMacroOverrides
{
	GPtrArray *items;	/* GtMacroOverride *, possédées, dans l'ordre */
};

static void
override_free(gpointer data)
{
	GtMacroOverride *e = data;

	g_free(e->label);
	g_ptr_array_unref(e->args);
	g_free(e);
}

static GtMacroOverride *
override_new(const gchar *label, guint occurrence)
{
	GtMacroOverride *e = g_new0(GtMacroOverride, 1);

	e->label = g_strdup(label != NULL ? label : "");
	e->occurrence = occurrence;
	e->args = g_ptr_array_new_with_free_func(g_free);
	return e;
}

static GtMacroOverride *
override_copy(const GtMacroOverride *e)
{
	GtMacroOverride *c = override_new(e->label, e->occurrence);

	for(guint k = 0; k < e->args->len; k++)
		g_ptr_array_add(c->args, g_strdup(g_ptr_array_index(e->args, k)));
	c->has_polling = e->has_polling;
	c->polling = e->polling;
	c->polling_period_ms = e->polling_period_ms;
	return c;
}

static void
override_set_arg(GtMacroOverride *e, guint rank, const gchar *value)
{
	while(e->args->len <= rank)
		g_ptr_array_add(e->args, NULL);
	g_free(e->args->pdata[rank]);
	e->args->pdata[rank] = g_strdup(value != NULL ? value : "");
}

static gint
find_entry(const GtMacroOverrides *o, const gchar *label, guint occurrence)
{
	for(guint i = 0; i < o->items->len; i++)
	{
		const GtMacroOverride *e = g_ptr_array_index(o->items, i);

		if(e->occurrence == occurrence && strcmp(e->label, label != NULL ? label : "") == 0)
			return (gint)i;
	}
	return -1;
}

static GtMacroOverride *
entry_for(GtMacroOverrides *o, const gchar *label, guint occurrence)
{
	gint i = find_entry(o, label, occurrence);
	GtMacroOverride *e;

	if(i >= 0)
		return g_ptr_array_index(o->items, i);
	e = override_new(label, occurrence);
	g_ptr_array_add(o->items, e);
	return e;
}

GtMacroOverrides *
gt_macro_overrides_new(void)
{
	GtMacroOverrides *o = g_new0(GtMacroOverrides, 1);

	o->items = g_ptr_array_new_with_free_func(override_free);
	return o;
}

void
gt_macro_overrides_free(GtMacroOverrides *o)
{
	if(o == NULL)
		return;
	g_ptr_array_unref(o->items);
	g_free(o);
}

guint
gt_macro_overrides_count(const GtMacroOverrides *o)
{
	return o != NULL ? o->items->len : 0;
}

const GtMacroOverride *
gt_macro_overrides_get(const GtMacroOverrides *o, guint i)
{
	if(o == NULL || i >= o->items->len)
		return NULL;
	return g_ptr_array_index(o->items, i);
}

GtMacroOverrides *
gt_macro_overrides_copy(const GtMacroOverrides *o)
{
	GtMacroOverrides *c = gt_macro_overrides_new();

	for(guint i = 0; i < gt_macro_overrides_count(o); i++)
		g_ptr_array_add(c->items, override_copy(g_ptr_array_index(o->items, i)));
	return c;
}

void
gt_macro_overrides_set_arg(GtMacroOverrides *o, const gchar *label, guint occurrence, guint rank, const gchar *value)
{
	override_set_arg(entry_for(o, label, occurrence), rank, value);
}

void
gt_macro_overrides_set_polling(GtMacroOverrides *o, const gchar *label, guint occurrence, gboolean polling, guint period_ms)
{
	GtMacroOverride *e = entry_for(o, label, occurrence);

	e->has_polling = TRUE;
	e->polling = polling;
	e->polling_period_ms = period_ms;
}

/* Valeur par défaut de rang k, NULL ≡ "" */
static const gchar *
default_at(const GtMacro *m, guint k)
{
	if(m->args == NULL || k >= g_strv_length(m->args))
		return "";
	return m->args[k];
}

/* Rang de la macro d'indice i parmi celles de même libellé */
static guint
occurrence_of(const GtMacroSet *set, guint i)
{
	const gchar *label = gt_macro_set_get(set, i)->label;
	guint occ = 0;

	for(guint j = 0; j < i; j++)
		if(strcmp(gt_macro_set_get(set, j)->label, label) == 0)
			occ++;
	return occ;
}

/* Indice de la macro (libellé, occurrence), -1 si absente */
static gint
find_macro(const GtMacroSet *set, const gchar *label, guint occurrence)
{
	guint seen = 0;

	for(guint i = 0; i < gt_macro_set_count(set); i++)
		if(strcmp(gt_macro_set_get(set, i)->label, label) == 0 && seen++ == occurrence)
			return (gint)i;
	return -1;
}

GtMacroOverrides *
gt_macro_overrides_diff(const GtMacroSet *base, const GtMacroSet *effective)
{
	GtMacroOverrides *o;

	if(!gt_macro_set_same_structure(base, effective))
		return NULL;
	o = gt_macro_overrides_new();
	for(guint i = 0; i < gt_macro_set_count(base); i++)
	{
		const GtMacro *b = gt_macro_set_get(base, i), *e = gt_macro_set_get(effective, i);
		guint n = MAX(b->args != NULL ? g_strv_length(b->args) : 0, e->args != NULL ? g_strv_length(e->args) : 0);
		guint occ = occurrence_of(effective, i);

		for(guint k = 0; k < n; k++)
			if(strcmp(default_at(b, k), default_at(e, k)) != 0)
				gt_macro_overrides_set_arg(o, e->label, occ, k, default_at(e, k));
		if(b->polling != e->polling || b->polling_period_ms != e->polling_period_ms)
			gt_macro_overrides_set_polling(o, e->label, occ, e->polling, e->polling_period_ms);
	}
	return o;
}

static const gchar *
limit_at(gchar **v, guint k)
{
	return v != NULL ? v[k] : NULL;
}

/* Valeur acceptable pour l'argument k de la macro (type et limites ; une liste n'est pas vérifiée) */
static gboolean
value_accepted(const GtMacro *m, const macro_arg_info_t *info, guint k, const gchar *value)
{
	if(info->list_name != NULL)
		return TRUE;
	return gt_macro_arg_check(info->type, value, limit_at(m->arg_min, k), limit_at(m->arg_max, k), limit_at(m->arg_charset, k),
	                          NULL) == GT_MACRO_ARG_OK;
}

GtMacroSet *
gt_macro_overrides_apply(const GtMacroOverrides *o, const GtMacroSet *base, guint *dropped)
{
	GtMacroSet *res = gt_macro_set_copy(base);
	guint lost = 0;

	for(guint i = 0; i < gt_macro_overrides_count(o); i++)
	{
		const GtMacroOverride *e = g_ptr_array_index(o->items, i);
		gint idx = find_macro(res, e->label, e->occurrence);
		const GtMacro *m;
		macro_arg_info_t *infos;
		gint n = 0;

		if(idx < 0)
		{
			/* Macro disparue du modèle : chaque élément est perdu */
			for(guint k = 0; k < e->args->len; k++)
				if(g_ptr_array_index(e->args, k) != NULL)
					lost++;
			if(e->has_polling)
				lost++;
			continue;
		}
		m = gt_macro_set_get(res, (guint)idx);
		infos = macro_get_arg_infos(gt_macro_set_get_lists(res), m->action, &n);
		for(guint k = 0; k < e->args->len; k++)
		{
			const gchar *value = g_ptr_array_index(e->args, k);

			if(value == NULL)
				continue;
			if(k >= (guint)n || !value_accepted(m, &infos[k], k, value))
				lost++;
			else
				gt_macro_set_set_arg(res, m->id, (gint)k, value);
		}
		macro_arg_infos_free(infos, n);
		if(e->has_polling)
		{
			/* La période n'est jamais ramenée dans les bornes : la valeur propre est abandonnée */
			if(!gt_macro_is_sendable(m) || e->polling_period_ms < GT_MACRO_PERIOD_MIN_MS || e->polling_period_ms > GT_MACRO_PERIOD_MAX_MS)
				lost++;
			else
				gt_macro_set_set_polling(res, m->id, e->polling, e->polling_period_ms);
		}
	}
	if(dropped != NULL)
		*dropped = lost;
	return res;
}

GtMacroSet *
gt_macro_overrides_merge_template(const GtMacroSet *base, const GtMacroSet *before, const GtMacroSet *after)
{
	GtMacroSet *res = gt_macro_set_copy(after);

	for(guint r = 0; r < gt_macro_set_count(after); r++)
	{
		const GtMacro *a = gt_macro_set_get(after, r);
		gint i = gt_macro_set_index_of(before, a->id);
		const GtMacro *bf, *bs;
		guint rid = gt_macro_set_get(res, r)->id;
		macro_arg_info_t *ia, *ib;
		gint na = 0, nb = 0;

		/* Macro nouvelle : telle quelle */
		if(i < 0 || (guint)i >= gt_macro_set_count(base))
			continue;
		bf = gt_macro_set_get(before, (guint)i);
		bs = gt_macro_set_get(base, (guint)i);
		ia = macro_get_arg_infos(gt_macro_set_get_lists(after), a->action, &na);
		ib = macro_get_arg_infos(gt_macro_set_get_lists(before), bf->action, &nb);
		for(gint k = 0; k < MIN(na, nb); k++)
		{
			const gchar *candidate = default_at(bs, (guint)k);

			/* Rang de type changé, ou valeur changée par l'édition : la valeur de after va au modèle */
			if(g_strcmp0(ia[k].type, ib[k].type) != 0 || g_strcmp0(ia[k].list_name, ib[k].list_name) != 0
			   || strcmp(default_at(bf, (guint)k), default_at(a, (guint)k)) != 0)
				continue;
			/* Inchangée : la valeur propre du projet ne fuit pas dans le modèle, sauf si les nouvelles limites refusent celle du modèle */
			if(value_accepted(a, &ia[k], (guint)k, candidate))
				gt_macro_set_set_arg(res, rid, k, candidate);
		}
		macro_arg_infos_free(ia, na);
		macro_arg_infos_free(ib, nb);
		/* L'éditeur ne change pas le polling : celui du modèle reste */
		gt_macro_set_set_polling(res, rid, bs->polling, bs->polling_period_ms);
	}
	return res;
}

/* Le groupe « <prefix>override:… » ? */
static gboolean
is_override_group(const gchar *group, const gchar *prefix)
{
	return g_str_has_prefix(group, prefix) && g_str_has_prefix(group + strlen(prefix), "override:");
}

void
gt_macro_overrides_to_key_file(const GtMacroOverrides *o, GKeyFile *kf, const gchar *prefix)
{
	gchar **groups = g_key_file_get_groups(kf, NULL);

	for(guint g = 0; groups[g] != NULL; g++)
		if(is_override_group(groups[g], prefix))
			g_key_file_remove_group(kf, groups[g], NULL);
	g_strfreev(groups);
	for(guint i = 0; i < gt_macro_overrides_count(o); i++)
	{
		const GtMacroOverride *e = g_ptr_array_index(o->items, i);
		gchar *group = g_strdup_printf("%soverride:%u", prefix, i + 1);

		g_key_file_set_string(kf, group, "label", e->label);
		if(e->occurrence != 0)
			g_key_file_set_uint64(kf, group, "occurrence", e->occurrence);
		for(guint k = 0; k < e->args->len; k++)
		{
			const gchar *value = g_ptr_array_index(e->args, k);
			gchar *key;

			if(value == NULL)
				continue;
			key = g_strdup_printf("arg_%u", k + 1);
			g_key_file_set_string(kf, group, key, value);
			g_free(key);
		}
		if(e->has_polling)
		{
			g_key_file_set_boolean(kf, group, "polling", e->polling);
			g_key_file_set_uint64(kf, group, "polling_period_ms", e->polling_period_ms);
		}
		g_free(group);
	}
}

static void
warn_invalid(const gchar *origin, const gchar *group, const gchar *key)
{
	gchar *msg = g_strdup_printf(_("%s: [%s] %s: invalid value ignored"), origin, group, key);

	g_warning("%s", msg);
	g_free(msg);
}

/* Entier non signé écrit en décimal, dans [min, max] ; FALSE si illisible */
static gboolean
parse_uint(const gchar *text, guint64 min, guint64 max, guint64 *out)
{
	gchar *s;
	gboolean ok;

	if(text == NULL)
		return FALSE;
	/* g_ascii_string_to_unsigned refuse signe, texte en trop et dépassement ; les blancs autour sont retirés d'abord */
	s = g_strstrip(g_strdup(text));
	ok = g_ascii_string_to_unsigned(s, 10, min, max, out, NULL);
	g_free(s);
	return ok;
}

/* Lit un groupe ; NULL si le groupe entier est ignoré (libellé absent ou illisible, occurrence invalide) */
static GtMacroOverride *
read_group(GKeyFile *kf, const gchar *group, const gchar *origin)
{
	gchar *label = g_key_file_get_string(kf, group, "label", NULL);
	gchar *text;
	gchar **keys;
	guint64 occ = 0;
	GtMacroOverride *e;

	if(label == NULL)
	{
		warn_invalid(origin, group, "label");
		return NULL;
	}
	/* Une occurrence invalide désignerait une autre macro : tout le groupe est ignoré */
	text = g_key_file_get_value(kf, group, "occurrence", NULL);
	if(text != NULL && !parse_uint(text, 0, G_MAXUINT, &occ))
	{
		warn_invalid(origin, group, "occurrence");
		g_free(text);
		g_free(label);
		return NULL;
	}
	g_free(text);
	e = override_new(label, (guint)occ);
	g_free(label);
	keys = g_key_file_get_keys(kf, group, NULL, NULL);
	for(guint i = 0; keys != NULL && keys[i] != NULL; i++)
	{
		guint64 k;
		gchar *value;

		if(!g_str_has_prefix(keys[i], "arg_"))
			continue;
		if(!g_ascii_string_to_unsigned(keys[i] + 4, 10, 1, MAX_ARG_KEY, &k, NULL))
		{
			warn_invalid(origin, group, keys[i]);
			continue;
		}
		value = g_key_file_get_string(kf, group, keys[i], NULL);
		if(value == NULL)
			warn_invalid(origin, group, keys[i]);
		else
			override_set_arg(e, (guint)k - 1, value);
		g_free(value);
	}
	g_strfreev(keys);
	/* polling et polling_period_ms vont ensemble ; une période hors bornes est gardée (abandonnée par apply) */
	{
		gboolean has_mode = g_key_file_has_key(kf, group, "polling", NULL);
		gboolean has_period = g_key_file_has_key(kf, group, "polling_period_ms", NULL);
		GError *local = NULL;
		gboolean mode = FALSE;
		guint64 period = 0;

		if(has_mode)
		{
			mode = g_key_file_get_boolean(kf, group, "polling", &local);
			if(local != NULL)
			{
				warn_invalid(origin, group, "polling");
				g_clear_error(&local);
				has_mode = has_period = FALSE;
			}
		}
		if(has_period)
		{
			text = g_key_file_get_value(kf, group, "polling_period_ms", NULL);
			if(!parse_uint(text, 0, G_MAXUINT, &period))
			{
				warn_invalid(origin, group, "polling_period_ms");
				has_mode = has_period = FALSE;
			}
			g_free(text);
		}
		if(has_mode != has_period)
			warn_invalid(origin, group, has_mode ? "polling" : "polling_period_ms");
		else if(has_mode)
		{
			e->has_polling = TRUE;
			e->polling = mode;
			e->polling_period_ms = (guint)period;
		}
	}
	return e;
}

GtMacroOverrides *
gt_macro_overrides_from_key_file(GKeyFile *kf, const gchar *prefix, const gchar *origin)
{
	GtMacroOverrides *o = gt_macro_overrides_new();
	GPtrArray *sources = g_ptr_array_new();	/* groupe d'origine de chaque entrée (pour l'avertissement d'un doublon) */
	gchar **groups = g_key_file_get_groups(kf, NULL);

	for(guint g = 0; groups[g] != NULL; g++)
	{
		GtMacroOverride *e;
		gint dup;

		if(!is_override_group(groups[g], prefix))
			continue;
		e = read_group(kf, groups[g], origin);
		if(e == NULL)
			continue;
		dup = find_entry(o, e->label, e->occurrence);
		if(dup < 0)
		{
			g_ptr_array_add(o->items, e);
			g_ptr_array_add(sources, groups[g]);
			continue;
		}
		/* Doublon : la dernière lecture remplace la première, à sa place */
		warn_invalid(origin, g_ptr_array_index(sources, dup), "label");
		override_free(o->items->pdata[dup]);
		o->items->pdata[dup] = e;
		sources->pdata[dup] = groups[g];
	}
	g_ptr_array_unref(sources);
	g_strfreev(groups);
	return o;
}
