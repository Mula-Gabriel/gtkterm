/***********************************************************************/
/* macro_panel.c                                                       */
/* ------------                                                        */
/*           GTKTerm Software                                          */
/*                                                                     */
/*   Purpose                                                           */
/*      Sidebar macro button panel, polling system, entry validation */
/*                                                                     */
/***********************************************************************/

#include <string.h>
#include <vte/vte.h>

#include "interface.h"
#include "macro_panel.h"
#include "macros.h"
#include "term_config.h"

#include <config.h>
#include <glib/gi18n.h>

GtkWidget *macro_panel;
GtkWidget *macro_notebook;
static GtkWidget *macro_tab_flowbox;
static GtkWidget *macro_stack;
static GHashTable *hidden_macro_tabs;
static GtkCssProvider *polling_css_provider;



typedef struct {
	gint       macro_index;
	guint      period_ms;
	guint64    last_fire_us;
	gboolean   enabled;
	gboolean   running;
	GtkWidget *button;
	gint       n_args;
	gchar    **args;} macro_polling_t;

static GHashTable *macro_polling_table;
static GHashTable *macro_button_table;
static gboolean blink_state = FALSE;

static void free_polling_args(macro_polling_t *ps)
{
	if (ps->args)
	{
		for (gint k = 0; k < ps->n_args; k++)
			g_free(ps->args[k]);
		g_free(ps->args);
	}
	g_free(ps);
}

/* Forward declarations */
static macro_polling_t *get_polling_state(gint macro_index);
static void toggle_polling_run(gint macro_index);
static void on_polling_mode_toggled(GtkCheckMenuItem *check_item, gpointer user_data);
static void on_polling_period_changed(GtkWidget *entry, gpointer user_data);
static void on_macro_tab_clicked(GtkToggleButton *btn, gpointer user_data);
static void send_macro_by_index(gint macro_index);
static void update_entry_width(GtkEntry *entry);
static void apply_polling_css(GtkWidget *button);
static void on_list_action_button_clicked(GtkWidget *widget, gpointer data);
static void on_macro_arg_button_clicked(GtkWidget *widget, gpointer data);
static void on_macro_button_clicked_with_polling(GtkWidget *widget, gpointer data);
static gboolean on_macro_button_right_click(GtkWidget *button, GdkEventButton *event, gpointer user_data);
static gboolean on_macro_notebook_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data);


typedef struct {
	gint       macro_index;
	GtkWidget **entries;
	gint       n_entries;
} MacroArgData;

static void macro_arg_data_free(gpointer data)
{
	MacroArgData *d = (MacroArgData *)data;
	g_free(d->entries);
	g_free(d);
}

static gchar *
get_arg_value_from_widget(GtkWidget *widget)
{
	if (GTK_IS_ENTRY(widget))
		return g_strdup(gtk_entry_get_text(GTK_ENTRY(widget)));
	if (GTK_IS_COMBO_BOX(widget))
	{
		GtkTreeIter iter;
		GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(widget));
		if (gtk_combo_box_get_active_iter(GTK_COMBO_BOX(widget), &iter))
		{
			gchar *value = NULL;
			gtk_tree_model_get(model, &iter, 1, &value, -1);
			return value;
		}
	}
	return g_strdup("");
}

static void
save_arg_from_widget(GtkWidget *widget)
{
	gint macro_index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "macro-index"));
	gint arg_index   = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "arg-index"));
	gchar *val = get_arg_value_from_widget(widget);
	macro_set_arg(macro_index, arg_index, val);
	g_free(val);
	macros_file_save(NULL);
	save_config_silent();
}

	static void on_list_action_button_clicked(GtkWidget *widget, gpointer data)
	{
		gint macro_index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "macro-index"));
		gchar *list_value = (gchar *)g_object_get_data(G_OBJECT(widget), "list-value");
		if (list_value == NULL) return;

		const gchar **args = g_new(const gchar *, 1);
		args[0] = list_value;
		send_macro_with_args(macro_index, args, 1);
		g_free(args);
	}

static void on_macro_arg_button_clicked(GtkWidget *widget, gpointer data)
{
	MacroArgData *d = (MacroArgData *)g_object_get_data(G_OBJECT(widget), "macro-data");
	if (d == NULL) return;
	gint macro_index = d->macro_index;

	const gchar **args = g_new(const gchar *, d->n_entries);
	for (gint k = 0; k < d->n_entries; k++)
	{
		gchar *val = get_arg_value_from_widget(d->entries[k]);
		args[k] = val;
	}

	macro_polling_t *ps = get_polling_state(macro_index);
	if (ps && ps->enabled)
	{
		/* Store args copy for polling */
		if (ps->args)
		{
			for (gint k = 0; k < ps->n_args; k++)
				g_free(ps->args[k]);
			g_free(ps->args);
		}
		ps->n_args = d->n_entries;
		ps->args = g_new(gchar *, d->n_entries);
		for (gint k = 0; k < d->n_entries; k++)
			ps->args[k] = g_strdup(args[k]);

		toggle_polling_run(macro_index);

		if (ps->running)
		{
			ps->last_fire_us = g_get_monotonic_time();
			send_macro_with_args(macro_index, args, d->n_entries);
		}
	}
	else
	{
		send_macro_with_args(macro_index, args, d->n_entries);
	}

	for (gint k = 0; k < d->n_entries; k++)
		g_free((gchar *)args[k]);
	g_free(args);
}

/* --- Polling system --- */

static void send_macro_by_index(gint macro_index)
{
	gint nb_macros = 0;
	macro_t *macros = get_shortcuts(&nb_macros);
	if (macros != NULL && macro_index < nb_macros && macros[macro_index].action != NULL)
		shortcut_callback((gpointer)(long)macro_index);
}

static macro_polling_t *get_polling_state(gint macro_index)
{
	return (macro_polling_t *)g_hash_table_lookup(macro_polling_table, GINT_TO_POINTER(macro_index));
}

static void set_polling_state(gint macro_index, guint period_ms, gboolean enabled, gboolean running, GtkWidget *button)
{
	macro_polling_t *ps = g_new0(macro_polling_t, 1);
	ps->macro_index = macro_index;
	ps->period_ms = period_ms;
	ps->enabled = enabled;
	ps->running = running;
	ps->button = button;
	g_hash_table_insert(macro_polling_table, GINT_TO_POINTER(macro_index), ps);
}

static GtkCssProvider *polling_css_provider = NULL;

static void apply_polling_css(GtkWidget *button)
{
	if (button == NULL || polling_css_provider == NULL) return;
	GtkStyleContext *ctx = gtk_widget_get_style_context(button);
	gtk_style_context_add_provider(ctx, GTK_STYLE_PROVIDER(polling_css_provider),
	                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static void update_button_label(macro_polling_t *ps)
{
	if (ps == NULL || ps->button == NULL)
		return;

	gint nb_macros = 0;
	macro_t *macros = get_shortcuts(&nb_macros);
	if (ps->macro_index < 0 || ps->macro_index >= nb_macros || macros[ps->macro_index].label == NULL)
		return;

	const gchar *base = macros[ps->macro_index].label;
	if (ps->enabled)
		gtk_button_set_label(GTK_BUTTON(ps->button), g_strdup_printf("⏱ %s", base));
	else
		gtk_button_set_label(GTK_BUTTON(ps->button), base);
}

static void update_button_appearance(macro_polling_t *ps)
{
	if (ps == NULL || ps->button == NULL)
		return;

	GtkStyleContext *ctx = gtk_widget_get_style_context(ps->button);
	gtk_style_context_remove_class(ctx, "polling-blink");
	gtk_widget_queue_draw(ps->button);

	update_button_label(ps);
}
static void toggle_polling_run(gint macro_index)
{
	macro_polling_t *ps = get_polling_state(macro_index);
	if (ps == NULL || !ps->enabled)
		return;

	ps->running = !ps->running;
	update_button_appearance(ps);
}

static void restore_macro_polling(gint macro_index, gboolean enabled, guint period_ms)
{
	GtkWidget *btn = g_hash_table_lookup(macro_button_table, GINT_TO_POINTER(macro_index));
	if (btn == NULL)
		return;

	macro_polling_t *ps = get_polling_state(macro_index);
	if (ps == NULL)
	{
		set_polling_state(macro_index, period_ms, enabled, FALSE, btn);
		ps = get_polling_state(macro_index);
	}
	else
	{
		ps->period_ms = period_ms;
		ps->enabled = enabled;
		ps->running = FALSE;
		ps->button = btn;
	}

	if (enabled)
	{
		update_button_label(ps);
		apply_polling_css(btn);
	}
	else
	{
		update_button_appearance(ps);
	}

	macro_set_polling(macro_index, enabled, period_ms);
}

static gboolean polling_blink_callback(gpointer user_data)
{
	blink_state = !blink_state;
	GList *values = g_hash_table_get_values(macro_polling_table);
	for (GList *l = values; l != NULL; l = l->next)
	{
		macro_polling_t *ps = (macro_polling_t *)l->data;
		if (ps->enabled && ps->running && ps->button != NULL)
		{
			GtkStyleContext *ctx = gtk_widget_get_style_context(ps->button);
			gtk_style_context_remove_class(ctx, "polling-blink");
			if (blink_state)
				gtk_style_context_add_class(ctx, "polling-blink");
			gtk_widget_queue_draw(ps->button);
			//g_print("[BLINK] macro=%d state=%d\n", ps->macro_index, blink_state);
		}
	}
	g_list_free(values);
	return G_SOURCE_CONTINUE;
}

static gboolean polling_timer_callback(gpointer user_data)
{
	guint64 now = g_get_monotonic_time();
	GList *values = g_hash_table_get_values(macro_polling_table);

	for (GList *l = values; l != NULL; l = l->next)
	{
		macro_polling_t *ps = (macro_polling_t *)l->data;
		if (ps->enabled && ps->running)
		{
			guint64 elapsed_us = now - ps->last_fire_us;
			if (elapsed_us >= ps->period_ms * 1000)
			{
				if (ps->n_args > 0 && ps->args)
					send_macro_with_args(ps->macro_index, (const gchar **)ps->args, ps->n_args);
				else
					send_macro_by_index(ps->macro_index);
				ps->last_fire_us = now;
			}
		}
	}
	g_list_free(values);
	return G_SOURCE_CONTINUE;
}

static gboolean on_macro_button_right_click(GtkWidget *button, GdkEventButton *event, gpointer user_data)
{
	if (event->button != 3)
		return FALSE;

	if (g_object_get_data(G_OBJECT(button), "list-value") != NULL)
		return FALSE;

	gint macro_index = GPOINTER_TO_INT(user_data);
	macro_polling_t *ps = get_polling_state(macro_index);

	GtkWidget *menu = gtk_menu_new();

	/* Polling mode toggle */
	GtkWidget *polling_toggle = gtk_check_menu_item_new_with_label(_("Polling Mode"));
	if (ps)
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(polling_toggle), ps->enabled);
	g_object_set_data(G_OBJECT(polling_toggle), "macro-index", GINT_TO_POINTER(macro_index));
	g_signal_connect(polling_toggle, "toggled",
	                 G_CALLBACK(on_polling_mode_toggled), NULL);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), polling_toggle);

	/* Period entry */
	GtkWidget *period_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_container_set_border_width(GTK_CONTAINER(period_vbox), 8);

	GtkWidget *period_label = gtk_label_new(_("Period (ms)"));
	gtk_box_pack_start(GTK_BOX(period_vbox), period_label, FALSE, FALSE, 0);

	GtkWidget *period_entry = gtk_entry_new();
	gtk_entry_set_width_chars(GTK_ENTRY(period_entry), 8);
	gtk_entry_set_input_purpose(GTK_ENTRY(period_entry), GTK_INPUT_PURPOSE_DIGITS);
	if (ps)
	{
		gchar buf[16];
		g_snprintf(buf, sizeof(buf), "%u", ps->period_ms);
		gtk_entry_set_text(GTK_ENTRY(period_entry), buf);
	}
	else
		gtk_entry_set_text(GTK_ENTRY(period_entry), "1000");

	g_object_set_data(G_OBJECT(period_entry), "macro-index", GINT_TO_POINTER(macro_index));
	g_signal_connect(period_entry, "changed",
	                 G_CALLBACK(on_polling_period_changed), NULL);
	gtk_box_pack_start(GTK_BOX(period_vbox), period_entry, FALSE, FALSE, 0);

	GtkWidget *period_menu_item = gtk_menu_item_new();
	gtk_container_add(GTK_CONTAINER(period_menu_item), period_vbox);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), period_menu_item);

	gtk_widget_show_all(menu);
	gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
	gtk_widget_grab_focus(period_entry);
	return TRUE;
}

static void on_polling_mode_toggled(GtkCheckMenuItem *check_item, gpointer user_data)
{
	gint macro_index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(check_item), "macro-index"));
	gboolean active = gtk_check_menu_item_get_active(check_item);
	macro_polling_t *ps = get_polling_state(macro_index);

	if (active)
	{
		GtkWidget *btn = g_hash_table_lookup(macro_button_table, GINT_TO_POINTER(macro_index));
		if (ps == NULL)
		{
			set_polling_state(macro_index, 1000, TRUE, FALSE, btn);
			ps = get_polling_state(macro_index);
		}
		else
		{
			ps->button = btn;
			ps->enabled = TRUE;
			ps->running = FALSE;
		}
		update_button_appearance(ps);
		macro_set_polling(macro_index, TRUE, ps->period_ms);
		macros_file_save(NULL);
	}
	else if (ps)
	{
		ps->enabled = FALSE;
		ps->running = FALSE;
		update_button_appearance(ps);
		macro_set_polling(macro_index, FALSE, ps->period_ms);
		macros_file_save(NULL);
	}
}

static void on_polling_period_changed(GtkWidget *entry, gpointer user_data)
{
	gint macro_index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "macro-index"));
	guint period_ms = (guint)strtoul(gtk_entry_get_text(GTK_ENTRY(entry)), NULL, 10);
	if (period_ms == 0)
		period_ms = 1000;

	GtkWidget *btn = g_hash_table_lookup(macro_button_table, GINT_TO_POINTER(macro_index));
	macro_polling_t *ps = get_polling_state(macro_index);
	if (ps)
	{
		ps->period_ms = period_ms;
		ps->button = btn;
		macro_set_polling(macro_index, ps->enabled, period_ms);
		macros_file_save(NULL);
	}
	else
	{
		set_polling_state(macro_index, period_ms, FALSE, FALSE, btn);
	}
}

/* Override click handler for polling */
static void on_macro_button_clicked_with_polling(GtkWidget *widget, gpointer data)
{
	gint macro_index = GPOINTER_TO_INT(data);
	macro_polling_t *ps = get_polling_state(macro_index);



	if (ps && ps->enabled)
	{
		toggle_polling_run(macro_index);

		if (ps->running)
		{
			ps->last_fire_us = g_get_monotonic_time();
			send_macro_by_index(macro_index);
		}
	}
	else
	{

		send_macro_by_index(macro_index);
	}
}

static void save_entry_arg(GtkWidget *entry)
{
	const gchar *type = (const gchar *)g_object_get_data(G_OBJECT(entry), "arg-type");
	const gchar *text = gtk_entry_get_text(GTK_ENTRY(entry));
	GtkStyleContext *ctx = gtk_widget_get_style_context(entry);

	if (!macro_type_value_valid(type, text))
		gtk_style_context_add_class(ctx, "error");
	else
		gtk_style_context_remove_class(ctx, "error");

	save_arg_from_widget(entry);
}

static void apply_entry_validation(GtkWidget *entry)
{
	const gchar *type = (const gchar *)g_object_get_data(G_OBJECT(entry), "arg-type");
	const gchar *text = gtk_entry_get_text(GTK_ENTRY(entry));
	GtkStyleContext *ctx = gtk_widget_get_style_context(entry);

	if (!macro_type_value_valid(type, text))
		gtk_style_context_add_class(ctx, "error");
	else
		gtk_style_context_remove_class(ctx, "error");
}

static void update_entry_width(GtkEntry *entry)
{
	const gchar *text = gtk_entry_get_text(entry);
	gint len = (gint) g_utf8_strlen(text, -1);
	gtk_entry_set_width_chars(entry, MAX(4, len));
}

static gboolean on_macro_arg_entry_focus_out(GtkWidget *entry, GdkEvent *event, gpointer data)
{
	save_entry_arg(entry);
	return FALSE;
}

static void on_macro_arg_entry_activate(GtkWidget *entry, gpointer data)
{
	save_entry_arg(entry);
}

static void on_combo_arg_changed(GtkComboBox *combo, gpointer data)
{
	save_arg_from_widget(GTK_WIDGET(combo));
}
void rebuild_macro_buttons(void)
{
	gint nb_macros = 0;
	macro_t *macros = get_shortcuts(&nb_macros);

	if (macro_tab_flowbox == NULL || macro_notebook == NULL)
		return;

	/* Supprimer tous les boutons-onglets et pages existants */
	GList *old_tabs = gtk_container_get_children(GTK_CONTAINER(macro_tab_flowbox));
	for (GList *c = old_tabs; c != NULL; c = c->next)
		gtk_container_remove(GTK_CONTAINER(macro_tab_flowbox), GTK_WIDGET(c->data));
	g_list_free(old_tabs);

	while (gtk_notebook_get_n_pages(GTK_NOTEBOOK(macro_notebook)) > 0)
		gtk_notebook_remove_page(GTK_NOTEBOOK(macro_notebook), 0);

	/* Collecter les noms d'onglets uniques (non masqués) dans l'ordre d'apparition */
	GList *tab_names = NULL;
	for (gint i = 0; i < nb_macros; i++)
	{
		if (macros[i].label == NULL || strlen(macros[i].label) == 0)
			continue;
		if (macros[i].action == NULL || strlen(macros[i].action) == 0)
			continue;

		const gchar *tab = (macros[i].tab != NULL && strlen(macros[i].tab) > 0)
		                   ? macros[i].tab : _("General");

		if (g_hash_table_lookup(hidden_macro_tabs, tab) != NULL)
			continue;

		gboolean found = FALSE;
		for (GList *l = tab_names; l != NULL; l = l->next)
		{
			if (g_strcmp0((gchar *)l->data, tab) == 0)
			{
				found = TRUE;
				break;
			}
		}
		if (!found)
			tab_names = g_list_append(tab_names, (gpointer)tab);
	}

	if (tab_names == NULL)
	{
		gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(macro_tab_flowbox), 1);

		GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
		gtk_container_set_border_width(GTK_CONTAINER(vbox), 5);
		GtkWidget *lbl = gtk_label_new(_("No macros defined\nwith labels"));
		gtk_label_set_justify(GTK_LABEL(lbl), GTK_JUSTIFY_CENTER);
		gtk_widget_set_sensitive(lbl, FALSE);
		gtk_box_pack_start(GTK_BOX(vbox), lbl, FALSE, FALSE, 10);
		gtk_widget_show_all(vbox);
		gtk_notebook_append_page(GTK_NOTEBOOK(macro_notebook), vbox, NULL);

		GtkWidget *tab_btn = gtk_toggle_button_new_with_label(_("General"));
		gtk_button_set_relief(GTK_BUTTON(tab_btn), GTK_RELIEF_NONE);
		gtk_style_context_add_class(gtk_widget_get_style_context(tab_btn), "macro-tab");
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tab_btn), TRUE);
		gtk_widget_show(tab_btn);
		gtk_container_add(GTK_CONTAINER(macro_tab_flowbox), tab_btn);
		gtk_widget_show_all(macro_tab_flowbox);
		return;
	}

	gint n_tabs = g_list_length(tab_names);
	gint min_per_line = (n_tabs >= 3) ? 3 : n_tabs;
	gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(macro_tab_flowbox), min_per_line);

	/* Créer un onglet par nom unique */
	for (GList *l = tab_names; l != NULL; l = l->next)
	{
		const gchar *tab_name = (gchar *)l->data;

		GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
		                               GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
		GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
		gtk_container_set_border_width(GTK_CONTAINER(vbox), 5);
		gtk_container_add(GTK_CONTAINER(scrolled), vbox);

		for (gint i = 0; i < nb_macros; i++)
		{
			if (macros[i].label == NULL || strlen(macros[i].label) == 0)
				continue;
			if (macros[i].action == NULL || strlen(macros[i].action) == 0)
				continue;

			const gchar *macro_tab = (macros[i].tab != NULL && strlen(macros[i].tab) > 0)
			                         ? macros[i].tab : _("General");

			if (g_strcmp0(macro_tab, tab_name) != 0)
				continue;

			gchar tooltip[256];
			g_snprintf(tooltip, sizeof(tooltip), _("Shortcut: %s\nAction: %s"),
			           macros[i].shortcut ? macros[i].shortcut : "",
			           macros[i].action);

			gint n_args = macro_count_format_args(macros[i].action);
			if (n_args > 0)
			{
				macro_arg_info_t *arg_infos = macro_get_arg_infos(macros[i].action, NULL);

				gboolean is_two_button_list = FALSE;
				if (n_args == 1 && g_strcmp0 (arg_infos[0].type, "l") == 0 && arg_infos[0].list_name != NULL)
				{
					gint list_idx = macro_list_find(arg_infos[0].list_name);
					if (list_idx >= 0 && macro_list_entry_count(list_idx) == 2)
						is_two_button_list = TRUE;
				}

				if (is_two_button_list)
				{
					gint list_idx = macro_list_find(arg_infos[0].list_name);
					GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
					for (gint ei = 0; ei < 2; ei++)
					{
						gchar *label = g_strdup_printf("%s %s",
						                             macros[i].label,
						                             macro_list_entry_display(list_idx, ei));
					GtkWidget *button = gtk_button_new_with_label(label);
					g_free(label);
					gtk_style_context_add_class(gtk_widget_get_style_context(button), "macro-button");
					g_object_set_data(G_OBJECT(button), "macro-index", GINT_TO_POINTER(i));

					/* Store button reference for polling */
					g_hash_table_insert(macro_button_table, GINT_TO_POINTER(i), button);
					apply_polling_css(button);
						g_object_set_data(G_OBJECT(button), "list-value",
						                 (gpointer)macro_list_entry_value(list_idx, ei));
						gtk_widget_set_tooltip_text(button, tooltip);
						g_signal_connect(button, "clicked",
						                 G_CALLBACK(on_list_action_button_clicked), NULL);
						g_signal_connect(button, "button-press-event",
						                 G_CALLBACK(on_macro_button_right_click),
						                 GINT_TO_POINTER(i));
						gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);
					}
					gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 2);
					macro_arg_infos_free(arg_infos, n_args);
					continue;
				}

			GtkWidget *hbox   = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
			GtkWidget *button = gtk_button_new_with_label(macros[i].label);
			gtk_style_context_add_class(gtk_widget_get_style_context(button), "macro-button");

			/* Store button reference for polling */
			g_hash_table_insert(macro_button_table, GINT_TO_POINTER(i), button);
			apply_polling_css(button);

			MacroArgData *d = g_new(MacroArgData, 1);
			d->macro_index = i;
			d->n_entries   = n_args;
			d->entries     = g_new(GtkWidget *, n_args);

			g_object_set_data_full(G_OBJECT(button), "macro-data", d, macro_arg_data_free);
			g_signal_connect(button, "clicked",
			                 G_CALLBACK(on_macro_arg_button_clicked), NULL);
			g_signal_connect(button, "button-press-event",
			                 G_CALLBACK(on_macro_button_right_click),
			                 GINT_TO_POINTER(i));
			gtk_widget_set_tooltip_text(button, tooltip);
			gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);

				for (gint k = 0; k < n_args; k++)
				{
					GtkWidget *widget;

					if (g_strcmp0 (arg_infos[k].type, "l") == 0 && arg_infos[k].list_name != NULL)
					{
						/* Argument de liste : GtkComboBox avec GtkListStore */
						GtkListStore *store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
						gint list_idx = macro_list_find(arg_infos[k].list_name);
						if (list_idx >= 0)
						{
							gint n_entries = macro_list_entry_count(list_idx);
							for (gint ei = 0; ei < n_entries; ei++)
							{
								GtkTreeIter iter;
								gtk_list_store_append(store, &iter);
								gtk_list_store_set(store, &iter,
								                   0, macro_list_entry_display(list_idx, ei),
								                   1, macro_list_entry_value(list_idx, ei),
								                   -1);
							}
						}
						widget = gtk_combo_box_new_with_model(GTK_TREE_MODEL(store));
						g_object_unref(store);
						GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
						gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(widget), renderer, TRUE);
						gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(widget), renderer,
						                              "text", 0, NULL);

						/* Pré-sélectionner la valeur sauvegardée si disponible */
						gint active_idx = 0;
						if (macros[i].args != NULL && k < (gint)g_strv_length(macros[i].args)
						    && macros[i].args[k] != NULL)
						{
							GtkTreeModel *m = gtk_combo_box_get_model(GTK_COMBO_BOX(widget));
							GtkTreeIter it;
							if (gtk_tree_model_get_iter_first(m, &it))
							{
								gint idx = 0;
								do {
									gchar *val;
									gtk_tree_model_get(m, &it, 1, &val, -1);
									if (g_strcmp0(val, macros[i].args[k]) == 0)
									{
										active_idx = idx;
										g_free(val);
										break;
									}
									g_free(val);
									idx++;
								} while (gtk_tree_model_iter_next(m, &it));
							}
						}
						gtk_combo_box_set_active(GTK_COMBO_BOX(widget), active_idx);
						gtk_widget_set_size_request(widget, 50, -1);
					}
					else
					{
						/* Argument classique : GtkEntry */
						widget = gtk_entry_new();

					const gchar *placeholder =
					    (g_strcmp0 (arg_infos[k].type, "s") == 0)           ? "text" :
					    (macro_type_is_float (arg_infos[k].type))            ? "0.0"  : "0";
						gtk_entry_set_placeholder_text(GTK_ENTRY(widget), placeholder);
						gtk_entry_set_width_chars(GTK_ENTRY(widget), 4);

						if (macros[i].args != NULL && k < (gint)g_strv_length(macros[i].args))
						{
							gtk_entry_set_text(GTK_ENTRY(widget), macros[i].args[k]);
							update_entry_width(GTK_ENTRY(widget));
						}
					}

					d->entries[k] = widget;

			g_object_set_data(G_OBJECT(widget), "macro-index", GINT_TO_POINTER(i));
			g_object_set_data(G_OBJECT(widget), "arg-index",   GINT_TO_POINTER(k));
			g_object_set_data_full(G_OBJECT(widget), "arg-type", g_strdup(arg_infos[k].type), g_free);

			if (GTK_IS_ENTRY(widget))
				apply_entry_validation(widget);

					if (GTK_IS_COMBO_BOX(widget))
						g_signal_connect(widget, "changed",
						                 G_CALLBACK(on_combo_arg_changed), NULL);
					else
						g_signal_connect(widget, "focus-out-event",
						                 G_CALLBACK(on_macro_arg_entry_focus_out), NULL);
					if (GTK_IS_ENTRY(widget))
					{
						g_signal_connect(widget, "changed",
						                 G_CALLBACK(update_entry_width), NULL);
						g_signal_connect(widget, "activate",
						                 G_CALLBACK(on_macro_arg_entry_activate), button);
					}

					GtkWidget *arg_cell = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
					if (arg_infos[k].label != NULL)
					{
						GtkWidget *lbl = gtk_label_new(arg_infos[k].label);
						gtk_label_set_xalign(GTK_LABEL(lbl), 0.5);
						gtk_style_context_add_class(gtk_widget_get_style_context(lbl), "dim-label");
						gtk_box_pack_start(GTK_BOX(arg_cell), lbl, FALSE, FALSE, 0);
						gtk_box_pack_start(GTK_BOX(arg_cell), widget, FALSE, FALSE, 0);
					}
					else
					{
						gtk_box_pack_start(GTK_BOX(arg_cell), widget, TRUE, TRUE, 0);
					}
					gtk_box_pack_start(GTK_BOX(hbox), arg_cell, TRUE, TRUE, 0);
				}
				macro_arg_infos_free(arg_infos, n_args);
				gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 2);
			}
			else
			{
				GtkWidget *button = gtk_button_new_with_label(macros[i].label);
				gtk_style_context_add_class(gtk_widget_get_style_context(button), "macro-button");

				/* Store button reference for polling */
				g_hash_table_insert(macro_button_table, GINT_TO_POINTER(i), button);
				apply_polling_css(button);

				g_signal_connect(button, "clicked",
				                 G_CALLBACK(on_macro_button_clicked_with_polling),
				                 GINT_TO_POINTER(i));
				g_signal_connect(button, "button-press-event",
				                 G_CALLBACK(on_macro_button_right_click),
				                 GINT_TO_POINTER(i));
				gtk_widget_set_tooltip_text(button, tooltip);
				GtkWidget *hbox_simple = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
				gtk_box_pack_start(GTK_BOX(hbox_simple), button, TRUE, TRUE, 0);
				gtk_box_pack_start(GTK_BOX(vbox), hbox_simple, FALSE, FALSE, 2);
			}
		}

		gtk_widget_show_all(scrolled);
		gtk_notebook_append_page(GTK_NOTEBOOK(macro_notebook), scrolled, NULL);

		GtkWidget *tab_btn = gtk_toggle_button_new_with_label(tab_name);
		gtk_button_set_relief(GTK_BUTTON(tab_btn), GTK_RELIEF_NONE);
		gtk_style_context_add_class(gtk_widget_get_style_context(tab_btn), "macro-tab");
		g_signal_connect(tab_btn, "clicked", G_CALLBACK(on_macro_tab_clicked), NULL);
		gtk_widget_show(tab_btn);
		gtk_container_add(GTK_CONTAINER(macro_tab_flowbox), tab_btn);
	}

	/* Activer le premier onglet */
	gtk_notebook_set_current_page(GTK_NOTEBOOK(macro_notebook), 0);
	GList *fb_children = gtk_container_get_children(GTK_CONTAINER(macro_tab_flowbox));
	if (fb_children != NULL)
	{
		GtkWidget *first_child = gtk_bin_get_child(GTK_BIN(fb_children->data));
		if (GTK_IS_TOGGLE_BUTTON(first_child))
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(first_child), TRUE);
	}
	g_list_free(fb_children);
	gtk_widget_show_all(macro_tab_flowbox);

	g_list_free(tab_names);

	/* Restore polling state from macro_t */
	for (gint i = 0; i < nb_macros; i++)
	{
		if (macros[i].polling_enabled)
			restore_macro_polling(i, TRUE, macros[i].polling_period_ms);
	}
}

static void on_macro_tab_clicked(GtkToggleButton *btn, gpointer user_data)
{
	/* Trouver l'index de ce bouton dans le FlowBox */
	GList *fb_children = gtk_container_get_children(GTK_CONTAINER(macro_tab_flowbox));
	gint page_index = 0;
	gint idx = 0;
	for (GList *c = fb_children; c != NULL; c = c->next, idx++)
	{
		GtkWidget *child = gtk_bin_get_child(GTK_BIN(c->data));
		if (child == GTK_WIDGET(btn))
		{
			page_index = idx;
		}
		else if (GTK_IS_TOGGLE_BUTTON(child))
		{
			/* Bloquer le signal pour éviter la récursion : set_active émet "clicked" */
			g_signal_handlers_block_by_func(child, on_macro_tab_clicked, NULL);
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(child), FALSE);
			g_signal_handlers_unblock_by_func(child, on_macro_tab_clicked, NULL);
		}
	}
	g_list_free(fb_children);

	/* Afficher la page correspondante dans le notebook */
	gtk_notebook_set_current_page(GTK_NOTEBOOK(macro_notebook), page_index);

	/* Empêcher de désactiver le bouton actif en recliquant dessus */
	if (!gtk_toggle_button_get_active(btn))
	{
		g_signal_handlers_block_by_func(btn, on_macro_tab_clicked, NULL);
		gtk_toggle_button_set_active(btn, TRUE);
		g_signal_handlers_unblock_by_func(btn, on_macro_tab_clicked, NULL);
	}
}

static void on_macro_tab_visibility_toggled(GtkCheckMenuItem *check_item, gpointer user_data)
{
	const gchar *tab_name = (const gchar *)user_data;
	gboolean active = gtk_check_menu_item_get_active(check_item);

	if (active)
		g_hash_table_remove(hidden_macro_tabs, tab_name);
	else
		g_hash_table_insert(hidden_macro_tabs, g_strdup(tab_name), GINT_TO_POINTER(1));

	rebuild_macro_buttons();
}

static gboolean on_macro_notebook_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	if (event->button != 3)
		return FALSE;

	GtkWidget *target = gtk_get_event_widget((GdkEvent *)event);
	GtkWidget *btn = gtk_widget_get_ancestor(target, GTK_TYPE_BUTTON);
	/* Ne pas bloquer le menu si c'est un bouton-onglet (macro-tab) */
	if (btn != NULL && !gtk_style_context_has_class(gtk_widget_get_style_context(btn), "macro-tab"))
		return FALSE;

	gint nb_macros = 0;
	macro_t *macros = get_shortcuts(&nb_macros);

	GList *tab_names = NULL;
	for (gint i = 0; i < nb_macros; i++)
	{
		if (macros[i].label == NULL || strlen(macros[i].label) == 0)
			continue;
		if (macros[i].action == NULL || strlen(macros[i].action) == 0)
			continue;

		const gchar *tab = (macros[i].tab != NULL && strlen(macros[i].tab) > 0)
		                   ? macros[i].tab : _("General");

		gboolean found = FALSE;
		for (GList *l = tab_names; l != NULL; l = l->next)
		{
			if (g_strcmp0((gchar *)l->data, tab) == 0)
			{
				found = TRUE;
				break;
			}
		}
		if (!found)
			tab_names = g_list_append(tab_names, (gpointer)tab);
	}

	GtkWidget *menu = gtk_menu_new();
	for (GList *l = tab_names; l != NULL; l = l->next)
	{
		const gchar *tab_name = (gchar *)l->data;
		gboolean is_hidden = g_hash_table_lookup(hidden_macro_tabs, tab_name) != NULL;

		GtkWidget *check = gtk_check_menu_item_new_with_label(tab_name);
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(check), !is_hidden);
		g_signal_connect(check, "toggled",
		                 G_CALLBACK(on_macro_tab_visibility_toggled),
		                 (gpointer)tab_name);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), check);
	}

	gtk_widget_show_all(menu);
	gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
	g_list_free(tab_names);
	return GDK_EVENT_STOP;
}

void create_macro_panel(void)
{
	hidden_macro_tabs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	macro_polling_table = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)free_polling_args);
	macro_button_table = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);

	/* Conteneur principal du panneau latéral */
	GtkWidget *panel_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	/* Barre d'onglets wrappable */
	macro_tab_flowbox = gtk_flow_box_new();
	gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(macro_tab_flowbox), 1);
	gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(macro_tab_flowbox), 100);
	gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(macro_tab_flowbox), GTK_SELECTION_NONE);
	gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(macro_tab_flowbox), TRUE);
	gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(macro_tab_flowbox), 0);
	gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(macro_tab_flowbox), 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(macro_tab_flowbox), "macro-tab-bar");
	g_signal_connect(macro_tab_flowbox, "button-press-event",
	                 G_CALLBACK(on_macro_notebook_button_press), NULL);

	/* Notebook natif avec onglets masqués — donne le look natif à la zone de contenu */
	macro_notebook = gtk_notebook_new();
	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(macro_notebook), FALSE);
	gtk_notebook_set_show_border(GTK_NOTEBOOK(macro_notebook), TRUE);
	macro_stack = NULL;

	gtk_box_pack_start(GTK_BOX(panel_vbox), macro_tab_flowbox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(panel_vbox), macro_notebook, TRUE, TRUE, 0);

	macro_panel = panel_vbox;

	/* CSS : polling blink + apparence onglets */
	polling_css_provider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(polling_css_provider,
	    /* Polling blink */
	    "button.polling-blink { background-color: #abf573; background-image: none;"
	    "  color: black; border-color: #00cc00; }\n"
	    /* Barre d'onglets */
	    ".macro-tab-bar { background-color: #f6f5f4;"
	    "  border-bottom: 2px solid #888888;"
	    "  padding: 3px 3px 0px 3px; }\n"
	    ".macro-tab-bar flowboxchild { padding: 0; margin: 0; }\n"
	    /* Onglet inactif */
	    "button.macro-tab { border-top-width: 1px; border-right-width: 1px;"
	    "  border-bottom-width: 0px; border-left-width: 1px;"
	    "  border-style: solid; border-color: #888888;"
	    "  border-radius: 4px 4px 0 0;"
	    "  padding: 3px 6px; min-width: 0;"
	    "  background-color: #bbbbbb; background-image: none;"
	    "  box-shadow: none; margin: 0; color: #444444; }\n"
	    /* Onglet actif */
	    "button.macro-tab:checked { background-color: #eeeeee; background-image: none;"
	    "  color: #000000; border-color: #888888;"
	    "  margin-top: -1px; padding-top: 4px; }\n"
	    "button.macro-tab:hover:not(:checked) { background-color: #cccccc;"
	    "  color: #000000; }\n"
	    "button.macro-button { min-width: 0; }\n"
	    "entry.error { color: #cc0000; font-weight: bold; caret-color: #cc0000;"
	    "  border-color: #cc0000; border-width: 2px; }\n",
	    -1, NULL);
	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
	                                          GTK_STYLE_PROVIDER(polling_css_provider),
	                                          GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	g_timeout_add(1, polling_timer_callback, NULL);
	g_timeout_add(500, polling_blink_callback, NULL);

	rebuild_macro_buttons();
}
