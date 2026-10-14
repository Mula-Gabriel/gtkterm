/* macro_set.c — jeu de macros sans GTK (voir macro_set.h). */

#include <string.h>

#include "macro_set.h"
#include "macros_format.h"
#include "macro_arg.h"

struct _GtMacroSet
{
	GPtrArray    *macros;	/* GtMacro *, possédées, dans l'ordre d'affichage */
	GtMacroLists *lists;	/* jamais NULL */
};

/* Compteur du processus : un identifiant n'est jamais réutilisé, même par un autre jeu (fil principal seulement) */
static guint next_id = 1;

static void
macro_free(gpointer data)
{
	GtMacro *m = data;

	/* apply_edits laisse des trous (NULL) dans l'ancien tableau pour les macros reprises */
	if(m == NULL)
		return;
	g_free(m->label);
	g_free(m->shortcut);
	g_free(m->action);
	g_free(m->tab);
	g_strfreev(m->args);
	g_strfreev(m->arg_min);
	g_strfreev(m->arg_max);
	g_strfreev(m->arg_charset);
	g_free(m);
}

static gchar *
normalized(const gchar *s)
{
	return g_strdup(s != NULL ? s : "");
}

/* « None » était la valeur « pas de raccourci » de l'ancienne fenêtre d'édition */
static gchar *
normalized_shortcut(const gchar *s)
{
	if(s == NULL || strcmp(s, "None") == 0)
		return g_strdup("");
	return g_strdup(s);
}

static guint
clamp_period(guint period_ms)
{
	if(period_ms == 0)
		return GT_MACRO_PERIOD_DEFAULT_MS;
	return CLAMP(period_ms, GT_MACRO_PERIOD_MIN_MS, GT_MACRO_PERIOD_MAX_MS);
}

static void
set_fields(GtMacro *m, const gchar *label, const gchar *shortcut, const gchar *action, const gchar *tab)
{
	g_free(m->label);
	g_free(m->shortcut);
	g_free(m->action);
	g_free(m->tab);
	m->label = normalized(label);
	m->shortcut = normalized_shortcut(shortcut);
	m->action = normalized(action);
	m->tab = normalized(tab);
}

static GtMacro *
macro_new(const gchar *label, const gchar *shortcut, const gchar *action, const gchar *tab)
{
	GtMacro *m = g_new0(GtMacro, 1);

	m->id = next_id++;
	set_fields(m, label, shortcut, action, tab);
	m->polling_period_ms = GT_MACRO_PERIOD_DEFAULT_MS;
	return m;
}

/* Valide en place les trois vecteurs de limites (chacun NULL ou de n éléments, ""-complétés) pour les types des arguments ;
 * rend le nombre d'entrées remises à "". Une paire min > max est remise à "" des deux côtés. */
static gint
sanitize_limits(const macro_arg_info_t *infos, gint n, gchar **min, gchar **max, gchar **charset)
{
	gint replaced = 0;

	for(gint k = 0; k < n; k++)
	{
		const gchar *type = infos[k].type;
		gchar **entry[3] = { min, max, charset };

		for(gint v = 0; v < 3; v++)
		{
			gboolean ok;

			if(entry[v] == NULL || entry[v][k][0] == '\0')
				continue;
			if(v == 2)
				ok = gt_macro_arg_limits_valid(type, "", "", entry[v][k]);
			else
				ok = gt_macro_arg_limit_valid(type, entry[v][k]);
			if(!ok)
			{
				g_free(entry[v][k]);
				entry[v][k] = g_strdup("");
				replaced++;
			}
		}
		if(min != NULL && max != NULL && min[k][0] != '\0' && max[k][0] != '\0'
		   && !gt_macro_arg_limits_valid(type, min[k], max[k], ""))
		{
			g_free(min[k]);
			g_free(max[k]);
			min[k] = g_strdup("");
			max[k] = g_strdup("");
			replaced += 2;
		}
	}
	return replaced;
}

static gboolean
vector_all_empty(gchar **v)
{
	for(guint k = 0; v[k] != NULL; k++)
		if(v[k][0] != '\0')
			return FALSE;
	return TRUE;
}

/* Un vecteur dont la longueur ne correspond plus au nombre d'arguments est abandonné */
static void
drop_if_length(gchar ***v, gint n)
{
	if(*v != NULL && g_strv_length(*v) != (guint)n)
		g_clear_pointer(v, g_strfreev);
}

/* Les arguments enregistrés et leurs limites ne restent que s'ils correspondent encore au nombre de spécificateurs ;
 * chaque limite est revalidée pour le type courant de son argument (sans avertissement) */
static void
check_args(const GtMacroSet *set, GtMacro *m)
{
	gint n = 0;
	macro_arg_info_t *infos;
	gchar ***vecs[3] = { &m->arg_min, &m->arg_max, &m->arg_charset };

	if(m->args == NULL && m->arg_min == NULL && m->arg_max == NULL && m->arg_charset == NULL)
		return;
	infos = macro_get_arg_infos(set->lists, m->action, &n);
	drop_if_length(&m->args, n);
	for(gint v = 0; v < 3; v++)
		drop_if_length(vecs[v], n);
	sanitize_limits(infos, n, m->arg_min, m->arg_max, m->arg_charset);
	for(gint v = 0; v < 3; v++)
		if(*vecs[v] != NULL && vector_all_empty(*vecs[v]))
			g_clear_pointer(vecs[v], g_strfreev);
	macro_arg_infos_free(infos, n);
}

static GtMacro *
find_mutable(const GtMacroSet *set, guint id)
{
	if(set == NULL || id == 0)
		return NULL;
	for(guint i = 0; i < set->macros->len; i++)
	{
		GtMacro *m = g_ptr_array_index(set->macros, i);

		if(m->id == id)
			return m;
	}
	return NULL;
}

GtMacroSet *
gt_macro_set_new(void)
{
	GtMacroSet *set = g_new0(GtMacroSet, 1);

	set->macros = g_ptr_array_new_with_free_func(macro_free);
	set->lists = gt_macro_lists_new();
	return set;
}

void
gt_macro_set_free(GtMacroSet *set)
{
	if(set == NULL)
		return;
	g_ptr_array_unref(set->macros);
	gt_macro_lists_free(set->lists);
	g_free(set);
}

guint
gt_macro_set_count(const GtMacroSet *set)
{
	return set != NULL ? set->macros->len : 0;
}

const GtMacro *
gt_macro_set_get(const GtMacroSet *set, guint index)
{
	if(set == NULL || index >= set->macros->len)
		return NULL;
	return g_ptr_array_index(set->macros, index);
}

const GtMacro *
gt_macro_set_find(const GtMacroSet *set, guint id)
{
	return find_mutable(set, id);
}

gint
gt_macro_set_index_of(const GtMacroSet *set, guint id)
{
	if(set == NULL || id == 0)
		return -1;
	for(guint i = 0; i < set->macros->len; i++)
		if(((GtMacro *)g_ptr_array_index(set->macros, i))->id == id)
			return (gint)i;
	return -1;
}

const GtMacro *
gt_macro_set_find_label(const GtMacroSet *set, const gchar *label)
{
	if(set == NULL || label == NULL)
		return NULL;
	for(guint i = 0; i < set->macros->len; i++)
	{
		GtMacro *m = g_ptr_array_index(set->macros, i);

		if(strcmp(m->label, label) == 0)
			return m;
	}
	return NULL;
}

guint
gt_macro_set_append(GtMacroSet *set, const gchar *label, const gchar *shortcut, const gchar *action, const gchar *tab)
{
	GtMacro *m = macro_new(label, shortcut, action, tab);

	g_ptr_array_add(set->macros, m);
	return m->id;
}

const GtMacroLists *
gt_macro_set_get_lists(const GtMacroSet *set)
{
	return set != NULL ? set->lists : NULL;
}

void
gt_macro_set_take_lists(GtMacroSet *set, GtMacroLists *lists)
{
	/* Les listes déjà en place : rien à libérer (elles seraient libérées puis gardées) */
	if(lists != set->lists)
	{
		gt_macro_lists_free(set->lists);
		set->lists = lists != NULL ? lists : gt_macro_lists_new();
	}
	for(guint i = 0; i < set->macros->len; i++)
		check_args(set, g_ptr_array_index(set->macros, i));
}

gboolean
gt_macro_set_set_arg(GtMacroSet *set, guint id, gint arg_index, const gchar *value)
{
	GtMacro *m = find_mutable(set, id);
	gint n;

	if(m == NULL)
		return FALSE;
	n = macro_count_format_args(set->lists, m->action);
	if(arg_index < 0 || arg_index >= n)
		return FALSE;
	if(m->args == NULL)
	{
		m->args = g_new0(gchar *, n + 1);
		for(gint k = 0; k < n; k++)
			m->args[k] = g_strdup("");
	}
	g_free(m->args[arg_index]);
	m->args[arg_index] = g_strdup(value != NULL ? value : "");
	return TRUE;
}

gboolean
gt_macro_set_set_args(GtMacroSet *set, guint id, const gchar *const *args, gint n_args)
{
	GtMacro *m = find_mutable(set, id);
	gint n;

	if(m == NULL)
		return FALSE;
	n = macro_count_format_args(set->lists, m->action);
	if(n_args != n)
		return FALSE;
	g_clear_pointer(&m->args, g_strfreev);
	if(n == 0)
		return TRUE;
	m->args = g_new0(gchar *, n + 1);
	for(gint k = 0; k < n; k++)
		m->args[k] = g_strdup(args != NULL && args[k] != NULL ? args[k] : "");
	return TRUE;
}

/* Copie un vecteur de n éléments (NULL entrées → "") ; NULL si le vecteur est NULL ; *ok FALSE si sa longueur n'est pas n */
static gchar **
copy_vector(const gchar *const *v, gint n, gboolean *ok)
{
	gchar **c;

	if(v == NULL)
		return NULL;
	if(g_strv_length((gchar **)v) != (guint)n)
	{
		*ok = FALSE;
		return NULL;
	}
	c = g_new0(gchar *, n + 1);
	for(gint k = 0; k < n; k++)
		c[k] = g_strdup(v[k]);
	return c;
}

gint
gt_macro_set_set_limits(GtMacroSet *set, guint id, const gchar *const *min, const gchar *const *max,
                        const gchar *const *charset, gint n_args)
{
	GtMacro *m = find_mutable(set, id);
	macro_arg_info_t *infos;
	gchar **v[3];
	const gchar *const *src[3] = { min, max, charset };
	gboolean ok = TRUE;
	gint n = 0, replaced = 0;

	if(m == NULL)
		return -1;
	infos = macro_get_arg_infos(set->lists, m->action, &n);
	if(n_args != n)
		ok = FALSE;
	for(gint i = 0; i < 3; i++)
		v[i] = ok ? copy_vector(src[i], n, &ok) : NULL;
	if(!ok)
	{
		for(gint i = 0; i < 3; i++)
			g_strfreev(v[i]);
		macro_arg_infos_free(infos, n);
		return -1;
	}
	replaced = sanitize_limits(infos, n, v[0], v[1], v[2]);
	macro_arg_infos_free(infos, n);
	g_strfreev(m->arg_min);
	g_strfreev(m->arg_max);
	g_strfreev(m->arg_charset);
	m->arg_min = v[0] != NULL && !vector_all_empty(v[0]) ? v[0] : (g_strfreev(v[0]), NULL);
	m->arg_max = v[1] != NULL && !vector_all_empty(v[1]) ? v[1] : (g_strfreev(v[1]), NULL);
	m->arg_charset = v[2] != NULL && !vector_all_empty(v[2]) ? v[2] : (g_strfreev(v[2]), NULL);
	return replaced;
}

gboolean
gt_macro_set_set_polling(GtMacroSet *set, guint id, gboolean polling, guint period_ms)
{
	GtMacro *m = find_mutable(set, id);

	if(m == NULL)
		return FALSE;
	m->polling = polling;
	m->polling_period_ms = clamp_period(period_ms);
	return TRUE;
}

GtMacroSet *
gt_macro_set_copy(const GtMacroSet *set)
{
	GtMacroSet *copy = gt_macro_set_new();

	/* Les listes d'abord : « %#Nom » n'est un argument que si la liste existe */
	gt_macro_set_take_lists(copy, gt_macro_lists_copy(set->lists));
	for(guint i = 0; i < set->macros->len; i++)
	{
		const GtMacro *m = g_ptr_array_index(set->macros, i);
		guint id = gt_macro_set_append(copy, m->label, m->shortcut, m->action, m->tab);

		if(m->args != NULL)
			gt_macro_set_set_args(copy, id, (const gchar *const *)m->args, (gint)g_strv_length(m->args));
		if(m->arg_min != NULL || m->arg_max != NULL || m->arg_charset != NULL)
		{
			gint n = (gint)g_strv_length(m->arg_min != NULL ? m->arg_min : m->arg_max != NULL ? m->arg_max : m->arg_charset);

			gt_macro_set_set_limits(copy, id, (const gchar *const *)m->arg_min, (const gchar *const *)m->arg_max,
			                        (const gchar *const *)m->arg_charset, n);
		}
		gt_macro_set_set_polling(copy, id, m->polling, m->polling_period_ms);
	}
	return copy;
}

void
gt_macro_set_apply_edits(GtMacroSet *set, const GtMacroEdit *rows, guint n_rows, GtMacroLists *lists)
{
	GPtrArray *old = set->macros;
	GPtrArray *fresh = g_ptr_array_new_with_free_func(macro_free);

	/* Les listes d'abord : « %#Nom » n'est un argument que si la liste existe */
	if(lists != NULL && lists != set->lists)
	{
		gt_macro_lists_free(set->lists);
		set->lists = lists;
	}
	for(guint i = 0; i < n_rows; i++)
	{
		GtMacro *m = NULL;

		/* Identifiant connu et pas encore repris : la macro garde son identité, ses arguments et son polling (M16) */
		for(guint k = 0; rows[i].id != 0 && k < old->len; k++)
		{
			GtMacro *o = g_ptr_array_index(old, k);

			if(o != NULL && o->id == rows[i].id)
			{
				m = o;
				old->pdata[k] = NULL;
				break;
			}
		}
		if(m == NULL)
			m = macro_new(rows[i].label, rows[i].shortcut, rows[i].action, rows[i].tab);
		else
			set_fields(m, rows[i].label, rows[i].shortcut, rows[i].action, rows[i].tab);
		/* Les valeurs par défaut sont celles de la ligne (vérifiées après, avec les listes définitives) */
		g_clear_pointer(&m->args, g_strfreev);
		if(rows[i].args != NULL && rows[i].args[0] != NULL)
			m->args = g_strdupv((gchar **)rows[i].args);
		g_clear_pointer(&m->arg_min, g_strfreev);
		g_clear_pointer(&m->arg_max, g_strfreev);
		g_clear_pointer(&m->arg_charset, g_strfreev);
		if(rows[i].arg_min != NULL && rows[i].arg_min[0] != NULL)
			m->arg_min = g_strdupv((gchar **)rows[i].arg_min);
		if(rows[i].arg_max != NULL && rows[i].arg_max[0] != NULL)
			m->arg_max = g_strdupv((gchar **)rows[i].arg_max);
		if(rows[i].arg_charset != NULL && rows[i].arg_charset[0] != NULL)
			m->arg_charset = g_strdupv((gchar **)rows[i].arg_charset);
		g_ptr_array_add(fresh, m);
	}
	set->macros = fresh;
	g_ptr_array_unref(old);
	for(guint i = 0; i < set->macros->len; i++)
		check_args(set, g_ptr_array_index(set->macros, i));
}

gboolean
gt_macro_action_is_separator(const gchar *action, gchar **label_out)
{
	const gchar *end, *rest;

	if(label_out != NULL)
		*label_out = NULL;
	if(action == NULL)
		return FALSE;
	if(action[0] != '[')
		return strcmp(action, "%&") == 0;
	end = strchr(action + 1, ']');
	if(end == NULL)
		return FALSE;
	for(rest = end + 1; *rest == ' '; rest++)
		;
	if(strcmp(rest, "%&") != 0)
		return FALSE;
	if(label_out != NULL)
		*label_out = g_strndup(action + 1, end - action - 1);
	return TRUE;
}

gboolean
gt_macro_is_sendable(const GtMacro *macro)
{
	return macro != NULL && macro->action[0] != '\0' && !gt_macro_action_is_separator(macro->action, NULL);
}

GBytes *
gt_macro_expand(const GtMacroSet *set, const GtMacro *macro, const gchar *const *args, gint n_args)
{
	const gchar *const *use = args;
	gint n = n_args;
	gchar *formatted;
	GByteArray *bytes;

	if(macro == NULL)
		return NULL;
	if(n_args < 0)
	{
		use = (const gchar *const *)macro->args;
		n = macro->args != NULL ? (gint)g_strv_length(macro->args) : 0;
	}
	else if(args == NULL)
		n = 0;
	formatted = format_action_with_args(gt_macro_set_get_lists(set), macro->action, (const gchar **)use, n);
	bytes = parse_macro_string(formatted);
	g_free(formatted);
	return g_byte_array_free_to_bytes(bytes);
}

gboolean
gt_macro_period_parse(const gchar *text, guint *period_ms)
{
	guint64 value;

	/* g_ascii_string_to_unsigned refuse signe, blancs, texte en trop et dépassement */
	if(text == NULL || !g_ascii_string_to_unsigned(text, 10, GT_MACRO_PERIOD_MIN_MS, GT_MACRO_PERIOD_MAX_MS, &value, NULL))
		return FALSE;
	*period_ms = (guint)value;
	return TRUE;
}
