/* macro_set.c — jeu de macros sans GTK (voir macro_set.h). */

#include <string.h>

#include "macro_set.h"
#include "macros_format.h"

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

/* Les arguments enregistrés ne restent que s'ils correspondent encore au nombre de spécificateurs */
static void
check_args(const GtMacroSet *set, GtMacro *m)
{
	if(m->args != NULL && g_strv_length(m->args) != (guint)macro_count_format_args(set->lists, m->action))
		g_clear_pointer(&m->args, g_strfreev);
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
