/* macro_view.c — règles d'affichage des macros dans le panneau (sans GTK). */

#include "macro_view.h"
#include "macros_format.h"

gboolean gt_macro_view_is_displayed(const GtMacro *m)
{
	return m != NULL && m->label[0] != '\0' && m->action[0] != '\0';
}

const gchar *gt_macro_view_tab(const GtMacro *m, const gchar *default_tab)
{
	return m->tab[0] != '\0' ? m->tab : default_tab;
}

GPtrArray *gt_macro_view_tabs(const GtMacroSet *set, const gchar *default_tab, GHashTable *hidden)
{
	GPtrArray *tabs = g_ptr_array_new_with_free_func(g_free);

	for(guint i = 0; set != NULL && i < gt_macro_set_count(set); i++)
	{
		const GtMacro *m = gt_macro_set_get(set, i);
		const gchar *tab;

		if(!gt_macro_view_is_displayed(m))
			continue;
		tab = gt_macro_view_tab(m, default_tab);
		if(hidden != NULL && g_hash_table_contains(hidden, tab))
			continue;
		if(!g_ptr_array_find_with_equal_func(tabs, tab, g_str_equal, NULL))
			g_ptr_array_add(tabs, g_strdup(tab));
	}
	return tabs;
}

GtMacroViewKind gt_macro_view_kind(const GtMacroSet *set, const GtMacro *m)
{
	const GtMacroLists *lists;
	GtMacroViewKind kind = GT_MACRO_VIEW_ARGS;
	gint n_args;

	if(!gt_macro_view_is_displayed(m))
		return GT_MACRO_VIEW_HIDDEN;
	if(gt_macro_action_is_separator(m->action, NULL))
		return GT_MACRO_VIEW_SEPARATOR;
	lists = gt_macro_set_get_lists(set);
	n_args = macro_count_format_args(lists, m->action);
	if(n_args <= 0)
		return GT_MACRO_VIEW_BUTTON;
	if(n_args == 1)
	{
		macro_arg_info_t *infos = macro_get_arg_infos(lists, m->action, NULL);

		if(g_strcmp0(infos[0].type, "l") == 0 && infos[0].list_name != NULL)
		{
			gint list_idx = gt_macro_lists_find(lists, infos[0].list_name);

			/* une liste facultative garde la liste déroulante : seule elle offre l'entrée « (none) » */
			if(list_idx >= 0 && gt_macro_lists_entry_count(lists, list_idx) == 2 && !gt_macro_arg_is_optional(m, 0))
				kind = GT_MACRO_VIEW_TWO_VALUES;
		}
		macro_arg_infos_free(infos, n_args);
	}
	return kind;
}

GArray *gt_macro_view_tab_ids(const GtMacroSet *set, const gchar *tab, const gchar *default_tab)
{
	GArray *ids = g_array_new(FALSE, FALSE, sizeof(guint));

	for(guint i = 0; set != NULL && i < gt_macro_set_count(set); i++)
	{
		const GtMacro *m = gt_macro_set_get(set, i);

		if(g_strcmp0(gt_macro_view_tab(m, default_tab), tab) == 0)
			g_array_append_val(ids, m->id);
	}
	return ids;
}
