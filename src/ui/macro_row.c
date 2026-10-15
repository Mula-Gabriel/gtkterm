/* macro_row.c — lignes de la fenêtre « Configure Macros » (sans GTK). */

#include <string.h>

#include "macro_row.h"

/* Remplace *field par value (NULL → "") ; TRUE si la valeur a changé */
static gboolean replace_string(gchar **field, const gchar *value)
{
	if(value == NULL)
		value = "";
	if(*field != NULL && strcmp(*field, value) == 0)
		return FALSE;
	g_free(*field);
	*field = g_strdup(value);
	return TRUE;
}

/* ---- GtMacroRow ---- */

struct _GtMacroRow
{
	GObject parent_instance;
	guint   id;
	gchar  *label;
	gchar  *shortcut;
	gchar  *action;
	gchar  *tab;
	gchar **args;	/* valeurs par défaut ; NULL : aucune */
	gchar **arg_min;	/* limites (macro_arg.h) ; NULL : aucune */
	gchar **arg_max;
	gchar **arg_charset;
};

enum
{
	ROW_PROP_0, ROW_PROP_ID, ROW_PROP_LABEL, ROW_PROP_SHORTCUT, ROW_PROP_ACTION, ROW_PROP_TAB, ROW_PROP_ARGS, ROW_PROP_ARG_MIN, ROW_PROP_ARG_MAX,
	ROW_PROP_ARG_CHARSET, ROW_N_PROPS
};
static GParamSpec *row_props[ROW_N_PROPS];

G_DEFINE_FINAL_TYPE(GtMacroRow, gt_macro_row, G_TYPE_OBJECT)

static gchar **row_field(GtMacroRow *row, guint prop_id)
{
	switch(prop_id)
	{
	case ROW_PROP_LABEL:
		return &row->label;
	case ROW_PROP_SHORTCUT:
		return &row->shortcut;
	case ROW_PROP_ACTION:
		return &row->action;
	case ROW_PROP_TAB:
		return &row->tab;
	default:
		return NULL;
	}
}

/* Vecteur de la propriété (args, arg-min, arg-max, arg-charset), NULL pour les autres */
static gchar ***row_strv_field(GtMacroRow *row, guint prop_id)
{
	switch(prop_id)
	{
	case ROW_PROP_ARGS:
		return &row->args;
	case ROW_PROP_ARG_MIN:
		return &row->arg_min;
	case ROW_PROP_ARG_MAX:
		return &row->arg_max;
	case ROW_PROP_ARG_CHARSET:
		return &row->arg_charset;
	default:
		return NULL;
	}
}

/* Remplace *field par une copie de value (NULL permis) et notifie prop_id, seulement si la valeur change */
static void replace_strv(GtMacroRow *row, guint prop_id, const gchar *const *value)
{
	gchar ***field = row_strv_field(row, prop_id);

	if((*field == NULL) == (value == NULL) && (value == NULL || g_strv_equal((const gchar *const *)*field, value)))
		return;
	g_strfreev(*field);
	*field = value != NULL ? g_strdupv((gchar **)value) : NULL;
	g_object_notify_by_pspec(G_OBJECT(row), row_props[prop_id]);
}

static void gt_macro_row_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GtMacroRow *row = GT_MACRO_ROW(object);
	gchar **field;

	if(prop_id == ROW_PROP_ID)
	{
		row->id = g_value_get_uint(value);
		return;
	}
	if(row_strv_field(row, prop_id) != NULL)
	{
		replace_strv(row, prop_id, g_value_get_boxed(value));
		return;
	}
	field = row_field(row, prop_id);
	if(field == NULL)
	{
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		return;
	}
	if(replace_string(field, g_value_get_string(value)))
		g_object_notify_by_pspec(object, pspec);
}

static void gt_macro_row_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GtMacroRow *row = GT_MACRO_ROW(object);
	gchar **field;

	if(prop_id == ROW_PROP_ID)
	{
		g_value_set_uint(value, row->id);
		return;
	}
	if(row_strv_field(row, prop_id) != NULL)
	{
		g_value_set_boxed(value, *row_strv_field(row, prop_id));
		return;
	}
	field = row_field(row, prop_id);
	if(field == NULL)
	{
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		return;
	}
	g_value_set_string(value, *field);
}

static void gt_macro_row_finalize(GObject *object)
{
	GtMacroRow *row = GT_MACRO_ROW(object);

	g_free(row->label);
	g_free(row->shortcut);
	g_free(row->action);
	g_free(row->tab);
	g_strfreev(row->args);
	g_strfreev(row->arg_min);
	g_strfreev(row->arg_max);
	g_strfreev(row->arg_charset);
	G_OBJECT_CLASS(gt_macro_row_parent_class)->finalize(object);
}

static void gt_macro_row_class_init(GtMacroRowClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	const GParamFlags flags = G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS;

	object_class->set_property = gt_macro_row_set_property;
	object_class->get_property = gt_macro_row_get_property;
	object_class->finalize = gt_macro_row_finalize;
	row_props[ROW_PROP_ID] = g_param_spec_uint("id", NULL, NULL, 0, G_MAXUINT, 0, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
	row_props[ROW_PROP_LABEL] = g_param_spec_string("label", NULL, NULL, "", flags);
	row_props[ROW_PROP_SHORTCUT] = g_param_spec_string("shortcut", NULL, NULL, "", flags);
	row_props[ROW_PROP_ACTION] = g_param_spec_string("action", NULL, NULL, "", flags);
	row_props[ROW_PROP_TAB] = g_param_spec_string("tab", NULL, NULL, "", flags);
	row_props[ROW_PROP_ARGS] = g_param_spec_boxed("args", NULL, NULL, G_TYPE_STRV, flags);
	row_props[ROW_PROP_ARG_MIN] = g_param_spec_boxed("arg-min", NULL, NULL, G_TYPE_STRV, flags);
	row_props[ROW_PROP_ARG_MAX] = g_param_spec_boxed("arg-max", NULL, NULL, G_TYPE_STRV, flags);
	row_props[ROW_PROP_ARG_CHARSET] = g_param_spec_boxed("arg-charset", NULL, NULL, G_TYPE_STRV, flags);
	g_object_class_install_properties(object_class, ROW_N_PROPS, row_props);
}

static void gt_macro_row_init(GtMacroRow *row)
{
	row->label = g_strdup("");
	row->shortcut = g_strdup("");
	row->action = g_strdup("");
	row->tab = g_strdup("");
}

GtMacroRow *gt_macro_row_new(guint id, const gchar *label, const gchar *shortcut, const gchar *action, const gchar *tab)
{
	return g_object_new(GT_TYPE_MACRO_ROW, "id", id, "label", label, "shortcut", shortcut, "action", action, "tab", tab, NULL);
}

guint        gt_macro_row_get_id(GtMacroRow *row) { return row->id; }
const gchar *gt_macro_row_get_label(GtMacroRow *row) { return row->label; }
const gchar *gt_macro_row_get_shortcut(GtMacroRow *row) { return row->shortcut; }
const gchar *gt_macro_row_get_action(GtMacroRow *row) { return row->action; }
const gchar *gt_macro_row_get_tab(GtMacroRow *row) { return row->tab; }
const gchar *const *gt_macro_row_get_args(GtMacroRow *row) { return (const gchar *const *)row->args; }
const gchar *const *gt_macro_row_get_arg_min(GtMacroRow *row) { return (const gchar *const *)row->arg_min; }
const gchar *const *gt_macro_row_get_arg_max(GtMacroRow *row) { return (const gchar *const *)row->arg_max; }
const gchar *const *gt_macro_row_get_arg_charset(GtMacroRow *row) { return (const gchar *const *)row->arg_charset; }

void gt_macro_row_set_shortcut(GtMacroRow *row, const gchar *shortcut)
{
	if(replace_string(&row->shortcut, shortcut))
		g_object_notify_by_pspec(G_OBJECT(row), row_props[ROW_PROP_SHORTCUT]);
}

void gt_macro_row_set_args(GtMacroRow *row, const gchar *const *args)
{
	replace_strv(row, ROW_PROP_ARGS, args);
}

void gt_macro_row_set_limits(GtMacroRow *row, const gchar *const *min, const gchar *const *max, const gchar *const *charset)
{
	g_object_freeze_notify(G_OBJECT(row));
	replace_strv(row, ROW_PROP_ARG_MIN, min);
	replace_strv(row, ROW_PROP_ARG_MAX, max);
	replace_strv(row, ROW_PROP_ARG_CHARSET, charset);
	g_object_thaw_notify(G_OBJECT(row));
}

/* ---- GtMacroListRow ---- */

struct _GtMacroListRow
{
	GObject parent_instance;
	gchar  *list;
	gchar  *display;
	gchar  *value;
};

enum { LIST_PROP_0, LIST_PROP_LIST, LIST_PROP_DISPLAY, LIST_PROP_VALUE, LIST_N_PROPS };
static GParamSpec *list_props[LIST_N_PROPS];

G_DEFINE_FINAL_TYPE(GtMacroListRow, gt_macro_list_row, G_TYPE_OBJECT)

static gchar **list_field(GtMacroListRow *row, guint prop_id)
{
	switch(prop_id)
	{
	case LIST_PROP_LIST:
		return &row->list;
	case LIST_PROP_DISPLAY:
		return &row->display;
	case LIST_PROP_VALUE:
		return &row->value;
	default:
		return NULL;
	}
}

static void gt_macro_list_row_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	gchar **field = list_field(GT_MACRO_LIST_ROW(object), prop_id);

	if(field == NULL)
	{
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		return;
	}
	if(replace_string(field, g_value_get_string(value)))
		g_object_notify_by_pspec(object, pspec);
}

static void gt_macro_list_row_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	gchar **field = list_field(GT_MACRO_LIST_ROW(object), prop_id);

	if(field == NULL)
	{
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		return;
	}
	g_value_set_string(value, *field);
}

static void gt_macro_list_row_finalize(GObject *object)
{
	GtMacroListRow *row = GT_MACRO_LIST_ROW(object);

	g_free(row->list);
	g_free(row->display);
	g_free(row->value);
	G_OBJECT_CLASS(gt_macro_list_row_parent_class)->finalize(object);
}

static void gt_macro_list_row_class_init(GtMacroListRowClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	const GParamFlags flags = G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS;

	object_class->set_property = gt_macro_list_row_set_property;
	object_class->get_property = gt_macro_list_row_get_property;
	object_class->finalize = gt_macro_list_row_finalize;
	list_props[LIST_PROP_LIST] = g_param_spec_string("list", NULL, NULL, "", flags);
	list_props[LIST_PROP_DISPLAY] = g_param_spec_string("display", NULL, NULL, "", flags);
	list_props[LIST_PROP_VALUE] = g_param_spec_string("value", NULL, NULL, "", flags);
	g_object_class_install_properties(object_class, LIST_N_PROPS, list_props);
}

static void gt_macro_list_row_init(GtMacroListRow *row)
{
	row->list = g_strdup("");
	row->display = g_strdup("");
	row->value = g_strdup("");
}

GtMacroListRow *gt_macro_list_row_new(const gchar *list, const gchar *display, const gchar *value)
{
	return g_object_new(GT_TYPE_MACRO_LIST_ROW, "list", list, "display", display, "value", value, NULL);
}

const gchar *gt_macro_list_row_get_list(GtMacroListRow *row) { return row->list; }
const gchar *gt_macro_list_row_get_display(GtMacroListRow *row) { return row->display; }
const gchar *gt_macro_list_row_get_value(GtMacroListRow *row) { return row->value; }

/* ---- Opérations sur les modèles ---- */

GListStore *gt_macro_rows_from_set(const GtMacroSet *set)
{
	GListStore *store = g_list_store_new(GT_TYPE_MACRO_ROW);

	for(guint i = 0; set != NULL && i < gt_macro_set_count(set); i++)
	{
		const GtMacro *m = gt_macro_set_get(set, i);
		GtMacroRow *row = gt_macro_row_new(m->id, m->label, m->shortcut, m->action, m->tab);

		g_object_set(row, "args", m->args, "arg-min", m->arg_min, "arg-max", m->arg_max, "arg-charset", m->arg_charset, NULL);
		g_list_store_append(store, row);
		g_object_unref(row);
	}
	return store;
}

GListStore *gt_macro_list_rows_from_lists(const GtMacroLists *lists)
{
	GListStore *store = g_list_store_new(GT_TYPE_MACRO_LIST_ROW);

	for(gint li = 0; lists != NULL && li < gt_macro_lists_count(lists); li++)
		for(gint ei = 0; ei < gt_macro_lists_entry_count(lists, li); ei++)
		{
			GtMacroListRow *row = gt_macro_list_row_new(gt_macro_lists_name(lists, li), gt_macro_lists_entry_display(lists, li, ei),
			                                            gt_macro_lists_entry_value(lists, li, ei));

			g_list_store_append(store, row);
			g_object_unref(row);
		}
	return store;
}

GtMacroEdit *gt_macro_rows_to_edits(GListModel *rows, guint *n_out)
{
	guint n = g_list_model_get_n_items(rows);
	GtMacroEdit *edits = g_new0(GtMacroEdit, MAX(n, 1));

	for(guint i = 0; i < n; i++)
	{
		GtMacroRow *row = g_list_model_get_item(rows, i);

		edits[i].id = row->id;
		edits[i].label = row->label;
		edits[i].shortcut = row->shortcut;
		edits[i].action = row->action;
		edits[i].tab = row->tab;
		edits[i].args = (const gchar *const *)row->args;
		edits[i].arg_min = (const gchar *const *)row->arg_min;
		edits[i].arg_max = (const gchar *const *)row->arg_max;
		edits[i].arg_charset = (const gchar *const *)row->arg_charset;
		g_object_unref(row);	/* le modèle garde sa référence : les chaînes restent valides */
	}
	*n_out = n;
	return edits;
}

GtMacroLists *gt_macro_list_rows_to_lists(GListModel *rows)
{
	GtMacroLists *lists = gt_macro_lists_new();

	for(guint i = 0; i < g_list_model_get_n_items(rows); i++)
	{
		GtMacroListRow *row = g_list_model_get_item(rows, i);

		gt_macro_lists_add(lists, row->list, row->display, row->value);
		g_object_unref(row);
	}
	return lists;
}

guint gt_macro_rows_insert_position(const guint *selected, guint n_selected, guint n_items)
{
	guint last = 0;

	if(n_selected == 0)
		return n_items;
	for(guint k = 0; k < n_selected; k++)
		last = MAX(last, selected[k]);
	return MIN(last + 1, n_items);
}

/* Échange les éléments aux positions a et a + 1 */
static void swap_with_next(GListStore *store, guint a)
{
	gpointer first = g_list_model_get_item(G_LIST_MODEL(store), a);
	gpointer second = g_list_model_get_item(G_LIST_MODEL(store), a + 1);
	gpointer items[2] = { second, first };

	g_list_store_splice(store, a, 2, items, 2);
	g_object_unref(first);
	g_object_unref(second);
}

gboolean gt_macro_rows_move(GListStore *store, guint *positions, guint n, gint delta)
{
	guint n_items = g_list_model_get_n_items(G_LIST_MODEL(store));

	if(n == 0 || (delta != -1 && delta != 1))
		return FALSE;
	if(delta < 0 && positions[0] == 0)
		return FALSE;
	if(delta > 0 && positions[n - 1] + 1 >= n_items)
		return FALSE;
	if(delta < 0)
		for(guint k = 0; k < n; k++)
		{
			swap_with_next(store, positions[k] - 1);
			positions[k]--;
		}
	else
		for(guint k = n; k-- > 0;)
		{
			swap_with_next(store, positions[k]);
			positions[k]++;
		}
	return TRUE;
}

void gt_macro_rows_remove(GListStore *store, const guint *positions, guint n)
{
	for(guint k = n; k-- > 0;)
		g_list_store_remove(store, positions[k]);
}
