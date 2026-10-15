/* macro_arg_widget.c — champ d'un argument de macro, commun au panneau et à l'éditeur. Fil principal seulement. */

#include <config.h>
#include <gtk/gtk.h>

#include "macro_arg.h"
#include "macro_arg_widget.h"

/* Données de l'objet : "arg-type", "arg-list" (nom de la liste), "values" (liste), "arg-min", "arg-max", "arg-charset", "arg-default",
 * "arg-described" (set_limits appelé), "empty-is-default" */

GtkWidget *gt_macro_arg_widget_new(const GtMacroLists *lists, const macro_arg_info_t *info, const gchar *initial, const gchar *placeholder)
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
		g_object_set_data_full(G_OBJECT(w), "arg-list", g_strdup(info->list_name), g_free);
	}
	else
	{
		w = gtk_entry_new();
		if(placeholder == NULL || placeholder[0] == '\0')
			placeholder = g_strcmp0(info->type, "s") == 0 ? "text" : macro_type_is_float(info->type) ? "0.0" : "0";
		gtk_entry_set_placeholder_text(GTK_ENTRY(w), placeholder);
		if(initial != NULL)
			gtk_editable_set_text(GTK_EDITABLE(w), initial);
	}
	g_object_set_data_full(G_OBJECT(w), "arg-type", g_strdup(info->type), g_free);
	gt_macro_arg_widget_validate(w);
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

static void set_string(GtkWidget *w, const gchar *key, const gchar *value)
{
	g_object_set_data_full(G_OBJECT(w), key, g_strdup(value != NULL ? value : ""), g_free);
}

void gt_macro_arg_widget_set_limits(GtkWidget *w, const gchar *min, const gchar *max, const gchar *charset, const gchar *def)
{
	set_string(w, "arg-min", min);
	set_string(w, "arg-max", max);
	set_string(w, "arg-charset", charset);
	set_string(w, "arg-default", def);
	g_object_set_data(G_OBJECT(w), "arg-described", GINT_TO_POINTER(1));
	gt_macro_arg_widget_validate(w);
}

void gt_macro_arg_widget_set_empty_is_default(GtkWidget *w, gboolean empty_is_default)
{
	g_object_set_data(G_OBJECT(w), "empty-is-default", GINT_TO_POINTER(empty_is_default ? 1 : 0));
	gt_macro_arg_widget_validate(w);
}

gboolean gt_macro_arg_widget_check(GtkWidget *w, gchar **reason)
{
	GObject *o = G_OBJECT(w);
	const gchar *value;
	GtMacroArgVerdict verdict;

	if(reason != NULL)
		*reason = NULL;
	if(!GTK_IS_EDITABLE(w))
		return TRUE;
	value = gtk_editable_get_text(GTK_EDITABLE(w));
	if(value[0] == '\0' && g_object_get_data(o, "empty-is-default") != NULL && g_object_get_data(o, "arg-default") != NULL)
		value = g_object_get_data(o, "arg-default");
	verdict = gt_macro_arg_check(g_object_get_data(o, "arg-type"), value, g_object_get_data(o, "arg-min"), g_object_get_data(o, "arg-max"),
	                             g_object_get_data(o, "arg-charset"), reason);
	return verdict == GT_MACRO_ARG_OK;
}

void gt_macro_arg_widget_validate(GtkWidget *w)
{
	GObject *o = G_OBJECT(w);
	gchar *reason = NULL;
	gboolean valid = gt_macro_arg_widget_check(w, &reason);

	if(GTK_IS_EDITABLE(w))
	{
		if(valid)
			gtk_widget_remove_css_class(w, "error");
		else
			gtk_widget_add_css_class(w, "error");
	}
	if(g_object_get_data(o, "arg-described") != NULL)
	{
		gchar *text = gt_macro_arg_describe(g_object_get_data(o, "arg-type"), g_object_get_data(o, "arg-list"), g_object_get_data(o, "arg-min"),
		                                    g_object_get_data(o, "arg-max"), g_object_get_data(o, "arg-charset"),
		                                    g_object_get_data(o, "arg-default"));

		if(reason != NULL)
		{
			gchar *full = g_strconcat(text, "\n", reason, NULL);

			g_free(text);
			text = full;
		}
		gtk_widget_set_tooltip_text(w, text);
		g_free(text);
	}
	g_free(reason);
}
