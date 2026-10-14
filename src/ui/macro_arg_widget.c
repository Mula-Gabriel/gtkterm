/* macro_arg_widget.c — champ d'un argument de macro, commun au panneau et à l'éditeur. Fil principal seulement. */

#include <config.h>
#include <gtk/gtk.h>

#include "macro_arg_widget.h"

GtkWidget *gt_macro_arg_widget_new(const GtMacroLists *lists, const macro_arg_info_t *info, const gchar *initial)
{
	GtkWidget *w;

	if(g_strcmp0(info->type, "l") == 0 && info->list_name != NULL)
	{
		gint li = gt_macro_lists_find(lists, info->list_name);
		gint n = li >= 0 ? gt_macro_lists_entry_count(lists, li) : 0;
		gchar **displays = g_new0(gchar *, n + 1);
		gchar **values = g_new0(gchar *, n + 1);
		guint selected = 0;
		gboolean found = FALSE;

		for(gint e = 0; e < n; e++)
		{
			displays[e] = g_strdup(gt_macro_lists_entry_display(lists, li, e));
			values[e] = g_strdup(gt_macro_lists_entry_value(lists, li, e));
			if(!found && initial != NULL && g_strcmp0(values[e], initial) == 0)
			{
				selected = (guint)e;
				found = TRUE;
			}
		}
		w = gtk_drop_down_new_from_strings((const char *const *)displays);
		g_strfreev(displays);
		if(n > 0)
			gtk_drop_down_set_selected(GTK_DROP_DOWN(w), selected);
		g_object_set_data_full(G_OBJECT(w), "values", values, (GDestroyNotify)g_strfreev);
	}
	else
	{
		w = gtk_entry_new();
		gtk_entry_set_placeholder_text(GTK_ENTRY(w), g_strcmp0(info->type, "s") == 0 ? "text" : macro_type_is_float(info->type) ? "0.0" : "0");
		if(initial != NULL)
			gtk_editable_set_text(GTK_EDITABLE(w), initial);
		g_object_set_data_full(G_OBJECT(w), "arg-type", g_strdup(info->type), g_free);
		gt_macro_arg_widget_validate(w);
	}
	return w;
}

gchar *gt_macro_arg_widget_get_value(GtkWidget *w)
{
	if(GTK_IS_DROP_DOWN(w))
	{
		gchar **values = g_object_get_data(G_OBJECT(w), "values");
		guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(w));

		return g_strdup(values != NULL && sel != GTK_INVALID_LIST_POSITION && sel < g_strv_length(values) ? values[sel] : "");
	}
	return g_strdup(gtk_editable_get_text(GTK_EDITABLE(w)));
}

void gt_macro_arg_widget_validate(GtkWidget *entry)
{
	if(macro_type_value_valid(g_object_get_data(G_OBJECT(entry), "arg-type"), gtk_editable_get_text(GTK_EDITABLE(entry))))
		gtk_widget_remove_css_class(entry, "error");
	else
		gtk_widget_add_css_class(entry, "error");
}
