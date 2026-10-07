/***********************************************************************/
/* macro_panel.c                                                       */
/* ------------                                                        */
/*           GTKTerm Software                                          */
/*                                                                     */
/*   Purpose                                                           */
/*      Sidebar macro button panel (view of the macro set)            */
/*                                                                     */
/***********************************************************************/

#include <stdlib.h>
#include <string.h>
#include <vte/vte.h>

#include "interface.h"
#include "macro_panel.h"
#include "macros.h"
#include "macros_format.h"
#include "term_config.h"

#include <config.h>
#include <glib/gi18n.h>

GtkWidget *macro_panel;
GtkWidget *macro_notebook;
static GtkWidget *macro_tab_flowbox;
static GHashTable *hidden_macro_tabs;
static GtkCssProvider *polling_css_provider = NULL;
static GHashTable *macro_button_table;		/* identifiant de macro → bouton principal (non possédé), vidée à chaque reconstruction */
static gboolean blink_state = FALSE;

/* Forward declarations */
static void on_polling_mode_toggled(GtkCheckMenuItem *check_item, gpointer user_data);
static void on_polling_period_activate(GtkEntry *entry, gpointer menu);
static void on_polling_menu_hide(GtkWidget *menu, gpointer entry);
static gboolean on_polling_menu_key_press(GtkWidget *menu, GdkEventKey *event, gpointer entry);
static void on_macro_tab_clicked(GtkToggleButton *btn, gpointer user_data);
static void update_entry_width(GtkEntry *entry);
static void apply_polling_css(GtkWidget *button);
static void on_list_action_button_clicked(GtkWidget *widget, gpointer data);
static void on_macro_arg_button_clicked(GtkWidget *widget, gpointer data);
static void on_macro_button_clicked_with_polling(GtkWidget *widget, gpointer data);
static gboolean on_macro_button_right_click(GtkWidget *button, GdkEventButton *event, gpointer user_data);
static gboolean on_macro_notebook_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data);


typedef struct {
	guint       macro_id;
	GtkWidget **entries;
	gint        n_entries;
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

static guint widget_macro_id(GtkWidget *widget)
{
	return GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget), "macro-id"));
}

static void save_macros(void)
{
	macros_save_file(NULL, TRUE);
	config_remember_macros_file();
}

/* Valeur d'un champ d'argument écrite dans le modèle (le fichier n'est pas enregistré ici) */
static void commit_arg_widget(GtkWidget *widget)
{
	gint arg_index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "arg-index"));
	gchar *val = get_arg_value_from_widget(widget);

	gt_macro_set_set_arg(macros_get_set(), widget_macro_id(widget), arg_index, val);
	g_free(val);
}

static void save_arg_from_widget(GtkWidget *widget)
{
	commit_arg_widget(widget);
	save_macros();
}

/* Bouton d'une liste à deux valeurs : un envoi avec la valeur du bouton */
static void on_list_action_button_clicked(GtkWidget *widget, gpointer data)
{
	const gchar *list_value = g_object_get_data(G_OBJECT(widget), "list-value");

	if (list_value == NULL)
		return;
	const gchar *args[] = { list_value };
	macros_send(widget_macro_id(widget), args, 1);
}

static void apply_polling_css(GtkWidget *button)
{
	if (button == NULL || polling_css_provider == NULL) return;
	GtkStyleContext *ctx = gtk_widget_get_style_context(button);
	gtk_style_context_add_provider(ctx, GTK_STYLE_PROVIDER(polling_css_provider),
	                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

/* Libellé « ⏱ Libellé » en mode polling ; plus de clignotement quand la macro ne tourne pas */
static void update_button_appearance(guint id)
{
	GtkWidget *button = g_hash_table_lookup(macro_button_table, GUINT_TO_POINTER(id));
	const GtMacro *m = gt_macro_set_find(macros_get_set(), id);

	if (button == NULL || m == NULL)
		return;
	if (!gt_macro_poller_is_running(macros_get_poller(), id))
	{
		gtk_style_context_remove_class(gtk_widget_get_style_context(button), "polling-blink");
		gtk_widget_queue_draw(button);
	}
	if (m->polling)
	{
		gchar *label = g_strdup_printf("⏱ %s", m->label);

		gtk_button_set_label(GTK_BUTTON(button), label);
		g_free(label);
	}
	else
		gtk_button_set_label(GTK_BUTTON(button), m->label);
}

static guint blink_source = 0;
static gboolean polling_blink_callback(gpointer user_data);

/* Minuterie de clignotement : n'existe que tant qu'au moins une macro tourne */
static void update_blink_timer(void)
{
	guint running = gt_macro_poller_running_count(macros_get_poller());

	if (running > 0 && blink_source == 0)
		blink_source = g_timeout_add(500, polling_blink_callback, NULL);
	else if (running == 0 && blink_source != 0)
	{
		g_source_remove(blink_source);
		blink_source = 0;
	}
}

/* Fermeture de GTKTerm : les boutons sont peut-être déjà détruits (fenêtre fermée par la barre de titre) ; plus de clignotement ni de
   pointeur vers eux */
void macro_panel_shutdown(void)
{
	if (blink_source != 0)
	{
		g_source_remove(blink_source);
		blink_source = 0;
	}
	if (macro_button_table != NULL)
		g_hash_table_remove_all(macro_button_table);
}

void macro_panel_polling_changed(guint id, gboolean running)
{
	if (macro_button_table == NULL)
		return;
	update_button_appearance(id);
	update_blink_timer();
}

/* Clic sur une macro : bascule son polling si le mode est activé, sinon un envoi (arguments enregistrés) */
static void activate_macro(guint id)
{
	const GtMacro *m = gt_macro_set_find(macros_get_set(), id);

	if (m == NULL)
		return;
	if (m->polling)
		gt_macro_poller_toggle(macros_get_poller(), id);
	else
		macros_send(id, NULL, -1);
}

static void on_macro_arg_button_clicked(GtkWidget *widget, gpointer data)
{
	MacroArgData *d = (MacroArgData *)g_object_get_data(G_OBJECT(widget), "macro-data");
	guint id;

	if (d == NULL)
		return;
	/* Identifiant pris avant l'enregistrement : une boîte d'erreur fait tourner une boucle imbriquée pendant laquelle le panneau
	   peut être reconstruit (d détruit avec son bouton) */
	id = d->macro_id;
	for (gint k = 0; k < d->n_entries; k++)
		commit_arg_widget(d->entries[k]);
	save_macros();
	activate_macro(id);
}

/* --- Polling : l'ordonnanceur (macros_get_poller) envoie ; le panneau ne fait qu'afficher l'état --- */

/* Clignotement des boutons dont la macro tourne */
static gboolean polling_blink_callback(gpointer user_data)
{
	GHashTableIter it;
	gpointer key, value;

	blink_state = !blink_state;
	if (macro_button_table == NULL)
		return G_SOURCE_CONTINUE;
	g_hash_table_iter_init(&it, macro_button_table);
	while (g_hash_table_iter_next(&it, &key, &value))
	{
		GtkWidget *button = value;
		GtkStyleContext *ctx;

		if (!gt_macro_poller_is_running(macros_get_poller(), GPOINTER_TO_UINT(key)))
			continue;
		ctx = gtk_widget_get_style_context(button);
		gtk_style_context_remove_class(ctx, "polling-blink");
		if (blink_state)
			gtk_style_context_add_class(ctx, "polling-blink");
		gtk_widget_queue_draw(button);
	}
	return G_SOURCE_CONTINUE;
}

static gboolean on_macro_button_right_click(GtkWidget *button, GdkEventButton *event, gpointer user_data)
{
	if (event->button != 3)
		return FALSE;

	if (g_object_get_data(G_OBJECT(button), "list-value") != NULL)
		return FALSE;

	guint id = GPOINTER_TO_UINT(user_data);
	const GtMacro *m = gt_macro_set_find(macros_get_set(), id);

	if (m == NULL)
		return FALSE;

	GtkWidget *menu = gtk_menu_new();

	/* Polling mode toggle */
	GtkWidget *polling_toggle = gtk_check_menu_item_new_with_label(_("Polling Mode"));
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(polling_toggle), m->polling);
	g_object_set_data(G_OBJECT(polling_toggle), "macro-id", GUINT_TO_POINTER(id));
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
	{
		gchar buf[16];
		g_snprintf(buf, sizeof(buf), "%u", m->polling_period_ms);
		gtk_entry_set_text(GTK_ENTRY(period_entry), buf);
	}

	g_object_set_data(G_OBJECT(period_entry), "macro-id", GUINT_TO_POINTER(id));
	g_signal_connect(period_entry, "activate", G_CALLBACK(on_polling_period_activate), menu);
	g_signal_connect(menu, "deactivate", G_CALLBACK(on_polling_menu_hide), period_entry);
	g_signal_connect(menu, "key-press-event", G_CALLBACK(on_polling_menu_key_press), period_entry);
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
	guint id = widget_macro_id(GTK_WIDGET(check_item));
	const GtMacro *m = gt_macro_set_find(macros_get_set(), id);
	gboolean active = gtk_check_menu_item_get_active(check_item);

	if (m == NULL)
		return;
	gt_macro_set_set_polling(macros_get_set(), id, active, m->polling_period_ms);
	if (!active)
		gt_macro_poller_stop(macros_get_poller(), id);
	update_button_appearance(id);
	save_macros();
}

/* Période saisie appliquée si elle est valide (entier de 1 à 86 400 000 ms) ; sinon champ en erreur, ancienne période gardée (M22) */
static gboolean apply_period_entry(GtkWidget *entry)
{
	guint id = widget_macro_id(entry);
	const GtMacro *m = gt_macro_set_find(macros_get_set(), id);
	GtkStyleContext *ctx = gtk_widget_get_style_context(entry);
	guint period;

	if (m == NULL)
		return FALSE;
	if (!gt_macro_period_parse(gtk_entry_get_text(GTK_ENTRY(entry)), &period))
	{
		gtk_style_context_add_class(ctx, "error");
		return FALSE;
	}
	gtk_style_context_remove_class(ctx, "error");
	if (period != m->polling_period_ms)
	{
		gt_macro_set_set_polling(macros_get_set(), id, m->polling, period);
		gt_macro_poller_period_changed(macros_get_poller(), id);
		save_macros();
	}
	return TRUE;
}

static void on_polling_period_activate(GtkEntry *entry, gpointer menu)
{
	if (apply_period_entry(GTK_WIDGET(entry)))
		gtk_menu_shell_deactivate(GTK_MENU_SHELL(menu));
}

static gboolean destroy_widget_idle(gpointer widget)
{
	gtk_widget_destroy(GTK_WIDGET(widget));
	return G_SOURCE_REMOVE;
}

/* Entrée dans le menu : le menu consomme la touche (activation de l'élément sélectionné, aucun ici) avant le champ, qui ne reçoit donc jamais
   « activate » ; la validation est faite ici. Période invalide : le menu reste ouvert, champ en erreur */
static gboolean on_polling_menu_key_press(GtkWidget *menu, GdkEventKey *event, gpointer entry)
{
	if (event->keyval != GDK_KEY_Return && event->keyval != GDK_KEY_KP_Enter && event->keyval != GDK_KEY_ISO_Enter)
		return GDK_EVENT_PROPAGATE;
	if (apply_period_entry(GTK_WIDGET(entry)))
		gtk_menu_shell_deactivate(GTK_MENU_SHELL(menu));
	return GDK_EVENT_STOP;
}

/* Fermeture du menu (Entrée, clic ailleurs, Échap ; signal « deactivate » : « hide » n'est pas émis sur le menu, c'est sa fenêtre qui est masquée) :
   la période valide est appliquée, puis le menu est détruit (il fuyait) */
static void on_polling_menu_hide(GtkWidget *menu, gpointer entry)
{
	apply_period_entry(GTK_WIDGET(entry));
	g_idle_add(destroy_widget_idle, menu);
}

/* Clic sur une macro sans argument */
static void on_macro_button_clicked_with_polling(GtkWidget *widget, gpointer data)
{
	activate_macro(GPOINTER_TO_UINT(data));
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

/* Crée un widget séparateur horizontal avec label centré, non-interactif. */
static GtkWidget *make_separator_widget(const gchar *label)
{
	GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

	GtkWidget *sep_left = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_widget_set_valign(sep_left, GTK_ALIGN_CENTER);
	gtk_box_pack_start(GTK_BOX(hbox), sep_left, TRUE, TRUE, 0);

	if (label && *label)
	{
		GtkWidget *lbl = gtk_label_new(label);
		gtk_style_context_add_class(gtk_widget_get_style_context(lbl), "dim-label");
		gtk_box_pack_start(GTK_BOX(hbox), lbl, FALSE, FALSE, 2);

		GtkWidget *sep_right = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
		gtk_widget_set_valign(sep_right, GTK_ALIGN_CENTER);
		gtk_box_pack_start(GTK_BOX(hbox), sep_right, TRUE, TRUE, 0);
	}

	gtk_widget_set_sensitive(hbox, FALSE);
	gtk_widget_show_all(hbox);
	return hbox;
}

void rebuild_macro_buttons(void)
{
	GtMacroSet *set = macros_get_set();
	const GtMacroLists *lists = gt_macro_set_get_lists(set);
	guint nb_macros = gt_macro_set_count(set);

	if (macro_tab_flowbox == NULL || macro_notebook == NULL)
		return;

	/* Les boutons vont être détruits : oublier leurs références (le clignotement ne doit pas toucher un bouton libéré). Le polling
	   n'est pas arrêté ici : il appartient à l'ordonnanceur, par identifiant, et survit à la reconstruction. */
	g_hash_table_remove_all(macro_button_table);

	/* Supprimer tous les boutons-onglets et pages existants */
	GList *old_tabs = gtk_container_get_children(GTK_CONTAINER(macro_tab_flowbox));
	for (GList *c = old_tabs; c != NULL; c = c->next)
		gtk_container_remove(GTK_CONTAINER(macro_tab_flowbox), GTK_WIDGET(c->data));
	g_list_free(old_tabs);

	while (gtk_notebook_get_n_pages(GTK_NOTEBOOK(macro_notebook)) > 0)
		gtk_notebook_remove_page(GTK_NOTEBOOK(macro_notebook), 0);

	/* Collecter les noms d'onglets uniques (non masqués) dans l'ordre d'apparition */
	GList *tab_names = NULL;
	for (guint i = 0; i < nb_macros; i++)
	{
		const GtMacro *m = gt_macro_set_get(set, i);

		if (m->label[0] == '\0' || m->action[0] == '\0')
			continue;

		const gchar *tab = m->tab[0] != '\0' ? m->tab : _("General");

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

		for (guint i = 0; i < nb_macros; i++)
		{
			const GtMacro *m = gt_macro_set_get(set, i);

			if (m->label[0] == '\0' || m->action[0] == '\0')
				continue;

			const gchar *macro_tab = m->tab[0] != '\0' ? m->tab : _("General");

			if (g_strcmp0(macro_tab, tab_name) != 0)
				continue;

			{
				gchar *sep_label = NULL;
				if (gt_macro_action_is_separator(m->action, &sep_label))
				{
					GtkWidget *sep = make_separator_widget(sep_label);
					g_free(sep_label);
					gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 4);
					continue;
				}
			}

			gchar *tooltip = g_strdup_printf(_("Shortcut: %s\nAction: %s"), m->shortcut[0] != '\0' ? m->shortcut : _("None"), m->action);

			gint n_args = macro_count_format_args(lists, m->action);
			if (n_args > 0)
			{
				macro_arg_info_t *arg_infos = macro_get_arg_infos(lists, m->action, NULL);

				gboolean is_two_button_list = FALSE;
				if (n_args == 1 && g_strcmp0 (arg_infos[0].type, "l") == 0 && arg_infos[0].list_name != NULL)
				{
					gint list_idx = gt_macro_lists_find(lists, arg_infos[0].list_name);
					if (list_idx >= 0 && gt_macro_lists_entry_count(lists, list_idx) == 2)
						is_two_button_list = TRUE;
				}

				if (is_two_button_list)
				{
					gint list_idx = gt_macro_lists_find(lists, arg_infos[0].list_name);
					GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
					for (gint ei = 0; ei < 2; ei++)
					{
						gchar *label = g_strdup_printf("%s %s",
						                             m->label,
						                             gt_macro_lists_entry_display(lists, list_idx, ei));
					GtkWidget *button = gtk_button_new_with_label(label);
					g_free(label);
					gtk_style_context_add_class(gtk_widget_get_style_context(button), "macro-button");
					/* Bouton de liste : pas de polling, non inscrit dans macro_button_table */
					g_object_set_data(G_OBJECT(button), "macro-id", GUINT_TO_POINTER(m->id));
						g_object_set_data(G_OBJECT(button), "list-value",
						                 (gpointer)gt_macro_lists_entry_value(lists, list_idx, ei));
						gtk_widget_set_tooltip_text(button, tooltip);
						g_signal_connect(button, "clicked",
						                 G_CALLBACK(on_list_action_button_clicked), NULL);
						g_signal_connect(button, "button-press-event",
						                 G_CALLBACK(on_macro_button_right_click),
						                 GUINT_TO_POINTER(m->id));
						gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);
					}
					gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 2);
					macro_arg_infos_free(arg_infos, n_args);
					g_free(tooltip);
					continue;
				}

			GtkWidget *hbox   = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
			GtkWidget *button = gtk_button_new_with_label(m->label);
			gtk_style_context_add_class(gtk_widget_get_style_context(button), "macro-button");
			g_object_set_data(G_OBJECT(button), "macro-id", GUINT_TO_POINTER(m->id));

			/* Bouton principal inscrit par identifiant (polling : libellé et clignotement) */
			g_hash_table_insert(macro_button_table, GUINT_TO_POINTER(m->id), button);
			apply_polling_css(button);
			update_button_appearance(m->id);

			MacroArgData *d = g_new(MacroArgData, 1);
			d->macro_id    = m->id;
			d->n_entries   = n_args;
			d->entries     = g_new(GtkWidget *, n_args);

			g_object_set_data_full(G_OBJECT(button), "macro-data", d, macro_arg_data_free);
			g_signal_connect(button, "clicked",
			                 G_CALLBACK(on_macro_arg_button_clicked), NULL);
			g_signal_connect(button, "button-press-event",
			                 G_CALLBACK(on_macro_button_right_click),
			                 GUINT_TO_POINTER(m->id));
			gtk_widget_set_tooltip_text(button, tooltip);
			gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);

				for (gint k = 0; k < n_args; k++)
				{
					GtkWidget *widget;

					if (g_strcmp0 (arg_infos[k].type, "l") == 0 && arg_infos[k].list_name != NULL)
					{
						/* Argument de liste : GtkComboBox avec GtkListStore */
						GtkListStore *store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
						gint list_idx = gt_macro_lists_find(lists, arg_infos[k].list_name);
						if (list_idx >= 0)
						{
							gint n_entries = gt_macro_lists_entry_count(lists, list_idx);
							for (gint ei = 0; ei < n_entries; ei++)
							{
								GtkTreeIter iter;
								gtk_list_store_append(store, &iter);
								gtk_list_store_set(store, &iter,
								                   0, gt_macro_lists_entry_display(lists, list_idx, ei),
								                   1, gt_macro_lists_entry_value(lists, list_idx, ei),
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
						if (m->args != NULL && k < (gint)g_strv_length(m->args))
						{
							GtkTreeModel *cm = gtk_combo_box_get_model(GTK_COMBO_BOX(widget));
							GtkTreeIter it;
							if (gtk_tree_model_get_iter_first(cm, &it))
							{
								gint idx = 0;
								do {
									gchar *val;
									gtk_tree_model_get(cm, &it, 1, &val, -1);
									if (g_strcmp0(val, m->args[k]) == 0)
									{
										active_idx = idx;
										g_free(val);
										break;
									}
									g_free(val);
									idx++;
								} while (gtk_tree_model_iter_next(cm, &it));
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

						if (m->args != NULL && k < (gint)g_strv_length(m->args))
						{
							gtk_entry_set_text(GTK_ENTRY(widget), m->args[k]);
							update_entry_width(GTK_ENTRY(widget));
						}
					}

					d->entries[k] = widget;

			g_object_set_data(G_OBJECT(widget), "macro-id",    GUINT_TO_POINTER(m->id));
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
				GtkWidget *button = gtk_button_new_with_label(m->label);
				gtk_style_context_add_class(gtk_widget_get_style_context(button), "macro-button");
				g_object_set_data(G_OBJECT(button), "macro-id", GUINT_TO_POINTER(m->id));

				/* Bouton principal inscrit par identifiant (polling : libellé et clignotement) */
				g_hash_table_insert(macro_button_table, GUINT_TO_POINTER(m->id), button);
				apply_polling_css(button);
				update_button_appearance(m->id);

				g_signal_connect(button, "clicked",
				                 G_CALLBACK(on_macro_button_clicked_with_polling),
				                 GUINT_TO_POINTER(m->id));
				g_signal_connect(button, "button-press-event",
				                 G_CALLBACK(on_macro_button_right_click),
				                 GUINT_TO_POINTER(m->id));
				gtk_widget_set_tooltip_text(button, tooltip);
				GtkWidget *hbox_simple = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
				gtk_box_pack_start(GTK_BOX(hbox_simple), button, TRUE, TRUE, 0);
				gtk_box_pack_start(GTK_BOX(vbox), hbox_simple, FALSE, FALSE, 2);
			}
			g_free(tooltip);
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

/* Les pollings des macros d'un onglet masqué s'arrêtent (rien ne tournerait sans bouton visible) ; les autres continuent */
static void stop_tab_pollings(const gchar *tab_name)
{
	GtMacroSet *set = macros_get_set();

	for (guint i = 0; i < gt_macro_set_count(set); i++)
	{
		const GtMacro *m = gt_macro_set_get(set, i);
		const gchar *tab = m->tab[0] != '\0' ? m->tab : _("General");

		if (g_strcmp0(tab, tab_name) == 0)
			gt_macro_poller_stop(macros_get_poller(), m->id);
	}
}

static gboolean destroy_widget_idle(gpointer widget);

/* Fermeture du menu des onglets : destruction différée (le menu fuyait) */
static void on_tab_menu_hide(GtkWidget *menu, gpointer user_data)
{
	g_idle_add(destroy_widget_idle, menu);
}

static void on_macro_tab_visibility_toggled(GtkCheckMenuItem *check_item, gpointer user_data)
{
	const gchar *tab_name = (const gchar *)user_data;
	gboolean active = gtk_check_menu_item_get_active(check_item);

	if (active)
		g_hash_table_remove(hidden_macro_tabs, tab_name);
	else
	{
		stop_tab_pollings(tab_name);
		g_hash_table_insert(hidden_macro_tabs, g_strdup(tab_name), GINT_TO_POINTER(1));
	}

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

	GtMacroSet *set = macros_get_set();

	GList *tab_names = NULL;
	for (guint i = 0; i < gt_macro_set_count(set); i++)
	{
		const GtMacro *m = gt_macro_set_get(set, i);

		if (m->label[0] == '\0' || m->action[0] == '\0')
			continue;

		const gchar *tab = m->tab[0] != '\0' ? m->tab : _("General");

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

	g_signal_connect(menu, "deactivate", G_CALLBACK(on_tab_menu_hide), NULL);
	gtk_widget_show_all(menu);
	gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
	g_list_free(tab_names);
	return GDK_EVENT_STOP;
}

void create_macro_panel(void)
{
	hidden_macro_tabs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
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

	rebuild_macro_buttons();
}
