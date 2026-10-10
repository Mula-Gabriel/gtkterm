/* macro_panel.c — panneau des boutons de macros (interface GTK 4, spec 6a §6.5) : une vue du jeu courant (macros_get_set) et de
 * l'ordonnanceur (macros_get_poller). Règles d'affichage du cœur (macro_view.c) ; tout envoi passe par macros_send ; le panneau ne fait
 * qu'afficher l'état du polling. Les deux popovers (polling, onglets) sont créés une fois, enfants de la boîte du panneau, et réutilisés.
 * Fil principal seulement. */

#include <config.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "app.h"
#include "app_config.h"
#include "macro_panel.h"
#include "macro_view.h"
#include "macros.h"
#include "macros_format.h"
#include "main_window.h"

#define BLINK_MS 500
#define ARG_WIDTH_MAX 24	/* largeur d'un champ d'argument, en caractères : le texte reste entier dans le champ */

/* Seules couleurs fixes : celles du clignotement ; barre d'onglets et « entry.error » suivent le thème */
static const char panel_css[] =
	"button.polling-blink { background: #abf573; color: black; border-color: #00cc00; }\n"
	".macro-tab-bar flowboxchild { padding: 0; margin: 0; }\n"
	".macro-tab-bar button.macro-tab { border-radius: 4px 4px 0 0; padding: 3px 6px; min-width: 0; margin: 0; }\n"
	"button.macro-button { min-width: 0; }\n";

typedef struct
{
	GtkWidget      *box;	/* « macro-panel » : barre d'onglets puis pages ; parent des deux popovers */
	GtkWidget      *tab_bar;	/* GtkFlowBox de boutons bascule groupés « macro-tab-<onglet> » */
	GtkWidget      *stack;	/* une page par onglet (« page-<n> ») */
	GHashTable     *buttons;	/* identifiant → bouton principal (non possédé), vidée à chaque reconstruction */
	GHashTable     *hidden_tabs;	/* noms d'onglets masqués (possédés) ; jamais enregistrés (§3.2-12) */
	GtkWidget      *polling_popover;	/* « polling-popover » */
	GtkWidget      *polling_mode;	/* « polling-mode » */
	GtkWidget      *polling_period;	/* « polling-period » */
	GtkWidget      *polling_error;	/* « polling-error » */
	guint           polling_id;	/* macro du popover ouvert, 0 sinon */
	GtkWidget      *tabs_popover;	/* « tabs-popover » */
	GtkWidget      *tabs_box;	/* cases du popover des onglets, refaites à chaque ouverture */
	guint           blink_source;
	gboolean        blink_on;
	gboolean        rebuilding;	/* reconstruction : les champs détruits (perte du focus) ne touchent pas au modèle */
	GtkCssProvider *css;
} MacroPanel;

static MacroPanel *panel = NULL;

static void on_button_right_click(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer data);

static const gchar *default_tab(void)
{
	return _("General");
}

static void save_macros(void)
{
	macros_save_file(NULL, TRUE);
	config_remember_macros_file();
}

static void focus_terminal(void)
{
	GtkWindow *win = gt_app_get_main_window();

	if(win != NULL)
		gt_main_window_focus_terminal(GTK_WIDGET(win));
}

static guint widget_id(GtkWidget *w)
{
	return GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(w), "macro-id"));
}

/* --- Polling : affichage seulement (l'ordonnanceur envoie) --- */

/* « ⏱ Libellé » en mode polling ; plus de clignotement quand la macro ne tourne pas */
static void update_button_appearance(guint id)
{
	GtkWidget *button = g_hash_table_lookup(panel->buttons, GUINT_TO_POINTER(id));
	const GtMacro *m = gt_macro_set_find(macros_get_set(), id);

	if(button == NULL || m == NULL)
		return;
	if(!gt_macro_poller_is_running(macros_get_poller(), id))
		gtk_widget_remove_css_class(button, "polling-blink");
	if(m->polling)
	{
		gchar *label = g_strdup_printf("⏱ %s", m->label);

		gtk_button_set_label(GTK_BUTTON(button), label);
		g_free(label);
	}
	else
		gtk_button_set_label(GTK_BUTTON(button), m->label);
}

static gboolean blink_cb(gpointer data)
{
	GHashTableIter it;
	gpointer key, value;

	panel->blink_on = !panel->blink_on;
	g_hash_table_iter_init(&it, panel->buttons);
	while(g_hash_table_iter_next(&it, &key, &value))
	{
		if(!gt_macro_poller_is_running(macros_get_poller(), GPOINTER_TO_UINT(key)))
			continue;
		if(panel->blink_on)
			gtk_widget_add_css_class(value, "polling-blink");
		else
			gtk_widget_remove_css_class(value, "polling-blink");
	}
	return G_SOURCE_CONTINUE;
}

/* Minuterie de clignotement : n'existe que tant qu'au moins une macro tourne */
static void update_blink_timer(void)
{
	guint running = gt_macro_poller_running_count(macros_get_poller());

	if(running > 0 && panel->blink_source == 0)
		panel->blink_source = g_timeout_add(BLINK_MS, blink_cb, NULL);
	else if(running == 0 && panel->blink_source != 0)
	{
		g_source_remove(panel->blink_source);
		panel->blink_source = 0;
	}
}

void gt_macro_panel_polling_changed(guint id, gboolean running)
{
	if(panel == NULL || macros_get_poller() == NULL)
		return;
	update_button_appearance(id);
	update_blink_timer();
}

/* Clic sur une macro : bascule son polling si le mode est activé, sinon un envoi (arguments enregistrés) */
static void activate_macro(guint id)
{
	const GtMacro *m = gt_macro_set_find(macros_get_set(), id);

	if(m == NULL)
		return;
	if(m->polling)
		gt_macro_poller_toggle(macros_get_poller(), id);
	else
		macros_send(id, NULL, -1);
}

/* --- Arguments --- */

/* Texte de l'entrée, ou valeur de l'élément choisi du GtkDropDown ("" sans choix) */
static gchar *arg_value(GtkWidget *w)
{
	if(GTK_IS_DROP_DOWN(w))
	{
		gchar **values = g_object_get_data(G_OBJECT(w), "values");
		guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(w));

		if(values != NULL && sel != GTK_INVALID_LIST_POSITION && sel < g_strv_length(values))
			return g_strdup(values[sel]);
		return g_strdup("");
	}
	return g_strdup(gtk_editable_get_text(GTK_EDITABLE(w)));
}

/* Valeur du champ écrite dans le modèle si elle diffère de l'argument mémorisé ; TRUE si le modèle a changé (rien n'est enregistré ici) */
static gboolean commit_arg(GtkWidget *w)
{
	gint k = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "arg-index"));
	const GtMacro *m;
	const gchar *saved;
	gchar *value;
	gboolean changed = FALSE;

	if(panel == NULL || panel->rebuilding)
		return FALSE;
	m = gt_macro_set_find(macros_get_set(), widget_id(w));
	if(m == NULL)
		return FALSE;
	saved = (m->args != NULL && k < (gint)g_strv_length(m->args)) ? m->args[k] : "";
	value = arg_value(w);
	if(g_strcmp0(value, saved) != 0)
		changed = gt_macro_set_set_arg(macros_get_set(), m->id, k, value);
	g_free(value);
	return changed;
}

static void validate_entry(GtkWidget *entry)
{
	const gchar *type = g_object_get_data(G_OBJECT(entry), "arg-type");

	if(macro_type_value_valid(type, gtk_editable_get_text(GTK_EDITABLE(entry))))
		gtk_widget_remove_css_class(entry, "error");
	else
		gtk_widget_add_css_class(entry, "error");
}

static void on_entry_changed(GtkEditable *editable, gpointer data)
{
	gint len = (gint)g_utf8_strlen(gtk_editable_get_text(editable), -1);

	gtk_editable_set_width_chars(editable, CLAMP(len, 4, ARG_WIDTH_MAX));
	validate_entry(GTK_WIDGET(editable));
}

/* Entrée : enregistre si la valeur a changé ; n'envoie pas la macro (comme l'interface GTK 3) */
static void on_entry_activate(GtkEntry *entry, gpointer data)
{
	if(commit_arg(GTK_WIDGET(entry)))
		save_macros();
}

static void on_entry_focus_leave(GtkEventControllerFocus *controller, gpointer data)
{
	if(commit_arg(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller))))
		save_macros();
}

static void on_dropdown_selected(GObject *dropdown, GParamSpec *pspec, gpointer data)
{
	if(commit_arg(GTK_WIDGET(dropdown)))
		save_macros();
}

static GtkWidget *make_arg_widget(const GtMacroSet *set, const GtMacro *m, const macro_arg_info_t *info, gint k)
{
	const GtMacroLists *lists = gt_macro_set_get_lists(set);
	const gchar *saved = (m->args != NULL && k < (gint)g_strv_length(m->args)) ? m->args[k] : NULL;
	gchar *name = g_strdup_printf("macro-%u-arg-%d", m->id, k);
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
			if(!found && saved != NULL && g_strcmp0(values[e], saved) == 0)
			{
				selected = (guint)e;
				found = TRUE;
			}
		}
		w = gtk_drop_down_new_from_strings((const char *const *)displays);
		g_strfreev(displays);
		if(n > 0)
			gtk_drop_down_set_selected(GTK_DROP_DOWN(w), selected);
		g_object_set_data_full(G_OBJECT(w), "values", values, (GDestroyNotify)g_strfreev);	/* copies : le jeu peut être remplacé */
		g_signal_connect(w, "notify::selected", G_CALLBACK(on_dropdown_selected), NULL);
	}
	else
	{
		GtkEventController *focus = gtk_event_controller_focus_new();

		w = gtk_entry_new();
		gtk_entry_set_placeholder_text(GTK_ENTRY(w), g_strcmp0(info->type, "s") == 0 ? "text" : macro_type_is_float(info->type) ? "0.0" : "0");
		if(saved != NULL)
			gtk_editable_set_text(GTK_EDITABLE(w), saved);
		g_object_set_data_full(G_OBJECT(w), "arg-type", g_strdup(info->type), g_free);
		on_entry_changed(GTK_EDITABLE(w), NULL);	/* largeur et validation initiales */
		g_signal_connect(w, "changed", G_CALLBACK(on_entry_changed), NULL);
		g_signal_connect(w, "activate", G_CALLBACK(on_entry_activate), NULL);
		g_signal_connect(focus, "leave", G_CALLBACK(on_entry_focus_leave), NULL);
		gtk_widget_add_controller(w, focus);
	}
	gtk_widget_set_name(w, name);
	g_object_set_data(G_OBJECT(w), "macro-id", GUINT_TO_POINTER(m->id));
	g_object_set_data(G_OBJECT(w), "arg-index", GINT_TO_POINTER(k));
	g_free(name);
	return w;
}

/* --- Lignes du panneau --- */

static gchar *tooltip_for(const GtMacro *m)
{
	return g_strdup_printf(_("Shortcut: %s\nAction: %s"), m->shortcut[0] != '\0' ? m->shortcut : _("None"), m->action);
}

static GtkWidget *make_separator(const gchar *title)
{
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	GtkWidget *left = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);

	gtk_widget_set_hexpand(left, TRUE);
	gtk_widget_set_valign(left, GTK_ALIGN_CENTER);
	gtk_box_append(GTK_BOX(box), left);
	if(title != NULL && title[0] != '\0')
	{
		GtkWidget *label = gtk_label_new(title);
		GtkWidget *right = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);

		gtk_widget_add_css_class(label, "dim-label");
		gtk_box_append(GTK_BOX(box), label);
		gtk_widget_set_hexpand(right, TRUE);
		gtk_widget_set_valign(right, GTK_ALIGN_CENTER);
		gtk_box_append(GTK_BOX(box), right);
	}
	gtk_widget_set_sensitive(box, FALSE);
	gtk_widget_set_margin_top(box, 4);
	gtk_widget_set_margin_bottom(box, 4);
	return box;
}

/* Bouton principal « macro-<id> » : inscrit par identifiant (libellé ⏱ et clignotement), clic droit = popover de polling */
static GtkWidget *make_main_button(const GtMacro *m)
{
	GtkWidget *button = gtk_button_new_with_label(m->label);
	GtkGesture *right = gtk_gesture_click_new();
	gchar *name = g_strdup_printf("macro-%u", m->id);
	gchar *tip = tooltip_for(m);

	gtk_widget_set_name(button, name);
	gtk_widget_add_css_class(button, "macro-button");
	gtk_widget_set_focus_on_click(button, FALSE);	/* la frappe suivante reste au terminal (Review Focus 4) */
	gtk_widget_set_hexpand(button, TRUE);
	gtk_widget_set_tooltip_text(button, tip);
	g_object_set_data(G_OBJECT(button), "macro-id", GUINT_TO_POINTER(m->id));
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right), GDK_BUTTON_SECONDARY);
	g_signal_connect(right, "pressed", G_CALLBACK(on_button_right_click), GUINT_TO_POINTER(m->id));
	gtk_widget_add_controller(button, GTK_EVENT_CONTROLLER(right));
	g_hash_table_insert(panel->buttons, GUINT_TO_POINTER(m->id), button);
	g_free(name);
	g_free(tip);
	return button;
}

static void on_simple_button_clicked(GtkButton *button, gpointer data)
{
	activate_macro(widget_id(GTK_WIDGET(button)));
}

/* Bouton à arguments : champs écrits dans le modèle, un seul enregistrement si l'un a changé, puis clic */
static void on_args_button_clicked(GtkButton *button, gpointer data)
{
	GPtrArray *fields = g_object_get_data(G_OBJECT(button), "arg-widgets");
	guint id = widget_id(GTK_WIDGET(button));
	gboolean changed = FALSE;

	for(guint k = 0; fields != NULL && k < fields->len; k++)
		changed |= commit_arg(g_ptr_array_index(fields, k));
	if(changed)
		save_macros();
	activate_macro(id);
}

/* Bouton d'une liste à deux valeurs : un envoi avec la valeur du bouton */
static void on_value_button_clicked(GtkButton *button, gpointer data)
{
	const gchar *args[] = { g_object_get_data(G_OBJECT(button), "list-value") };

	if(args[0] != NULL)
		macros_send(widget_id(GTK_WIDGET(button)), args, 1);
}

static GtkWidget *make_two_values(const GtMacroSet *set, const GtMacro *m)
{
	const GtMacroLists *lists = gt_macro_set_get_lists(set);
	gint n_args = 0;
	macro_arg_info_t *infos = macro_get_arg_infos(lists, m->action, &n_args);
	gint li = gt_macro_lists_find(lists, infos[0].list_name);
	GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	gchar *tip = tooltip_for(m);

	for(gint e = 0; e < 2; e++)
	{
		gchar *label = g_strdup_printf("%s %s", m->label, gt_macro_lists_entry_display(lists, li, e));
		gchar *name = g_strdup_printf("macro-%u-value-%d", m->id, e);
		GtkWidget *b = gtk_button_new_with_label(label);

		gtk_widget_set_name(b, name);
		gtk_widget_add_css_class(b, "macro-button");
		gtk_widget_set_focus_on_click(b, FALSE);
		gtk_widget_set_hexpand(b, TRUE);
		gtk_widget_set_tooltip_text(b, tip);
		g_object_set_data(G_OBJECT(b), "macro-id", GUINT_TO_POINTER(m->id));
		g_object_set_data_full(G_OBJECT(b), "list-value", g_strdup(gt_macro_lists_entry_value(lists, li, e)), g_free);
		g_signal_connect(b, "clicked", G_CALLBACK(on_value_button_clicked), NULL);
		gtk_box_append(GTK_BOX(hbox), b);
		g_free(label);
		g_free(name);
	}
	macro_arg_infos_free(infos, n_args);
	g_free(tip);
	return hbox;
}

static GtkWidget *make_args_row(const GtMacroSet *set, const GtMacro *m)
{
	gint n_args = 0;
	macro_arg_info_t *infos = macro_get_arg_infos(gt_macro_set_get_lists(set), m->action, &n_args);
	GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	GtkWidget *button = make_main_button(m);
	GPtrArray *fields = g_ptr_array_new();	/* champs non possédés */

	g_signal_connect(button, "clicked", G_CALLBACK(on_args_button_clicked), NULL);
	gtk_box_append(GTK_BOX(hbox), button);
	for(gint k = 0; k < n_args; k++)
	{
		GtkWidget *cell = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
		GtkWidget *w = make_arg_widget(set, m, &infos[k], k);

		if(infos[k].label != NULL)
		{
			GtkWidget *label = gtk_label_new(infos[k].label);

			gtk_widget_add_css_class(label, "dim-label");
			gtk_box_append(GTK_BOX(cell), label);
		}
		gtk_widget_set_hexpand(cell, TRUE);
		gtk_box_append(GTK_BOX(cell), w);
		gtk_box_append(GTK_BOX(hbox), cell);
		g_ptr_array_add(fields, w);
	}
	g_object_set_data_full(G_OBJECT(button), "arg-widgets", fields, (GDestroyNotify)g_ptr_array_unref);
	update_button_appearance(m->id);
	macro_arg_infos_free(infos, n_args);
	return hbox;
}

static GtkWidget *make_page(const GtMacroSet *set, const gchar *tab)
{
	GtkWidget *scrolled = gtk_scrolled_window_new();
	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);

	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	/* Largeur naturelle = celle des boutons et des champs : position initiale du séparateur (main_window.c) ; la largeur minimale reste petite */
	gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scrolled), TRUE);
	gtk_widget_set_margin_start(vbox, 5);
	gtk_widget_set_margin_end(vbox, 5);
	gtk_widget_set_margin_top(vbox, 5);
	gtk_widget_set_margin_bottom(vbox, 5);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), vbox);
	for(guint i = 0; i < gt_macro_set_count(set); i++)
	{
		const GtMacro *m = gt_macro_set_get(set, i);
		GtMacroViewKind kind = gt_macro_view_kind(set, m);
		gchar *title = NULL;
		GtkWidget *b;

		if(kind == GT_MACRO_VIEW_HIDDEN || g_strcmp0(gt_macro_view_tab(m, default_tab()), tab) != 0)
			continue;
		switch(kind)
		{
		case GT_MACRO_VIEW_SEPARATOR:
			gt_macro_action_is_separator(m->action, &title);
			gtk_box_append(GTK_BOX(vbox), make_separator(title));
			g_free(title);
			break;
		case GT_MACRO_VIEW_BUTTON:
			b = make_main_button(m);
			g_signal_connect(b, "clicked", G_CALLBACK(on_simple_button_clicked), NULL);
			gtk_box_append(GTK_BOX(vbox), b);
			update_button_appearance(m->id);
			break;
		case GT_MACRO_VIEW_TWO_VALUES:
			gtk_box_append(GTK_BOX(vbox), make_two_values(set, m));
			break;
		case GT_MACRO_VIEW_ARGS:
			gtk_box_append(GTK_BOX(vbox), make_args_row(set, m));
			break;
		default:
			break;
		}
	}
	return scrolled;
}

static void on_tab_toggled(GtkToggleButton *toggle, gpointer data)
{
	if(panel == NULL)
		return;
	if(gtk_toggle_button_get_active(toggle))
		gtk_stack_set_visible_child_name(GTK_STACK(panel->stack), g_object_get_data(G_OBJECT(toggle), "page"));
}

void gt_macro_panel_rebuild(void)
{
	GtMacroSet *set = macros_get_set();
	GPtrArray *tabs;
	GtkWidget *child, *first = NULL;
	gboolean empty;

	if(panel == NULL || set == NULL)
		return;
	panel->rebuilding = TRUE;
	if(panel->polling_id != 0)
		gtk_popover_popdown(GTK_POPOVER(panel->polling_popover));	/* « closed » applique la période valide, par identifiant */
	/* Les boutons vont être détruits : le clignotement ne doit plus les toucher. Le polling, par identifiant, survit à la reconstruction. */
	g_hash_table_remove_all(panel->buttons);
	gtk_flow_box_remove_all(GTK_FLOW_BOX(panel->tab_bar));
	while((child = gtk_widget_get_first_child(panel->stack)) != NULL)
		gtk_stack_remove(GTK_STACK(panel->stack), child);
	tabs = gt_macro_view_tabs(set, default_tab(), panel->hidden_tabs);
	empty = tabs->len == 0;
	if(empty)
	{
		GtkWidget *label = gtk_label_new(_("No macros defined\nwith labels"));

		gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
		gtk_widget_set_sensitive(label, FALSE);
		gtk_widget_set_valign(label, GTK_ALIGN_START);
		gtk_widget_set_margin_top(label, 10);
		gtk_stack_add_named(GTK_STACK(panel->stack), label, "page-0");
		g_ptr_array_add(tabs, g_strdup(default_tab()));
	}
	gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(panel->tab_bar), MIN(tabs->len, 3));
	/* Largeur naturelle de la barre = celle de ses onglets (GtkFlowBox la calcule sur max-children-per-line) : position initiale du séparateur */
	gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(panel->tab_bar), tabs->len);
	for(guint t = 0; t < tabs->len; t++)
	{
		const gchar *tab = g_ptr_array_index(tabs, t);
		gchar *page = g_strdup_printf("page-%u", t);
		gchar *name = g_strdup_printf("macro-tab-%s", tab);
		GtkWidget *toggle = gtk_toggle_button_new_with_label(tab);

		if(!empty)
			gtk_stack_add_named(GTK_STACK(panel->stack), make_page(set, tab), page);
		gtk_widget_set_name(toggle, name);
		gtk_widget_add_css_class(toggle, "macro-tab");
		gtk_button_set_has_frame(GTK_BUTTON(toggle), FALSE);
		gtk_widget_set_focus_on_click(toggle, FALSE);
		g_object_set_data_full(G_OBJECT(toggle), "page", page, g_free);
		if(first == NULL)
			first = toggle;
		else
			gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(toggle), GTK_TOGGLE_BUTTON(first));
		g_signal_connect(toggle, "toggled", G_CALLBACK(on_tab_toggled), NULL);
		gtk_flow_box_append(GTK_FLOW_BOX(panel->tab_bar), toggle);
		g_free(name);
	}
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(first), TRUE);	/* premier onglet affiché, comme l'interface GTK 3 */
	g_ptr_array_unref(tabs);
	panel->rebuilding = FALSE;
}

/* --- Popover de polling --- */

/* Période du champ appliquée si elle est valide (1 à 86 400 000 ms) ; sinon ancienne période gardée et, si show_error, champ en erreur
 * et message (M22). Enregistre seulement si la période change. TRUE si valide (ou plus de macro visée). */
static gboolean apply_period(gboolean show_error)
{
	guint id = panel->polling_id;
	const GtMacro *m = gt_macro_set_find(macros_get_set(), id);
	guint period;

	if(m == NULL)
		return TRUE;
	if(!gt_macro_period_parse(gtk_editable_get_text(GTK_EDITABLE(panel->polling_period)), &period))
	{
		if(show_error)
		{
			gtk_widget_add_css_class(panel->polling_period, "error");
			gtk_label_set_text(GTK_LABEL(panel->polling_error), _("Period must be a whole number of milliseconds from 1 to 86400000."));
			gtk_widget_set_visible(panel->polling_error, TRUE);
		}
		return FALSE;
	}
	gtk_widget_remove_css_class(panel->polling_period, "error");
	gtk_widget_set_visible(panel->polling_error, FALSE);
	if(period != m->polling_period_ms)
	{
		gt_macro_set_set_polling(macros_get_set(), id, m->polling, period);
		gt_macro_poller_period_changed(macros_get_poller(), id);
		save_macros();
	}
	return TRUE;
}

static void on_polling_mode_toggled(GtkCheckButton *check, gpointer data)
{
	guint id;
	const GtMacro *m;
	gboolean active = gtk_check_button_get_active(check);

	if(panel == NULL)
		return;
	id = panel->polling_id;
	m = gt_macro_set_find(macros_get_set(), id);
	if(m == NULL)
		return;
	gt_macro_set_set_polling(macros_get_set(), id, active, m->polling_period_ms);
	if(!active)
		gt_macro_poller_stop(macros_get_poller(), id);
	update_button_appearance(id);
	save_macros();
}

static void on_polling_period_activate(GtkEntry *entry, gpointer data)
{
	if(panel == NULL)
		return;
	if(apply_period(TRUE))
		gtk_popover_popdown(GTK_POPOVER(panel->polling_popover));
}

/* Fermeture (Entrée, Échap, clic ailleurs, reconstruction) : période valide appliquée, focus rendu au terminal */
static void on_polling_closed(GtkPopover *popover, gpointer data)
{
	if(panel == NULL)
		return;
	apply_period(FALSE);
	panel->polling_id = 0;
	focus_terminal();
}

static void on_button_right_click(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer data)
{
	GtkWidget *button = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
	guint id = GPOINTER_TO_UINT(data);
	const GtMacro *m = gt_macro_set_find(macros_get_set(), id);
	graphene_rect_t bounds;
	GdkRectangle rect;
	gchar buf[16];

	gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
	if(panel == NULL || m == NULL || !gtk_widget_compute_bounds(button, panel->box, &bounds))
		return;
	panel->polling_id = id;
	g_signal_handlers_block_by_func(panel->polling_mode, on_polling_mode_toggled, NULL);
	gtk_check_button_set_active(GTK_CHECK_BUTTON(panel->polling_mode), m->polling);
	g_signal_handlers_unblock_by_func(panel->polling_mode, on_polling_mode_toggled, NULL);
	g_snprintf(buf, sizeof(buf), "%u", m->polling_period_ms);
	gtk_editable_set_text(GTK_EDITABLE(panel->polling_period), buf);
	gtk_widget_remove_css_class(panel->polling_period, "error");
	gtk_widget_set_visible(panel->polling_error, FALSE);
	rect = (GdkRectangle){ (int)bounds.origin.x, (int)bounds.origin.y, (int)bounds.size.width, (int)bounds.size.height };
	gtk_popover_set_pointing_to(GTK_POPOVER(panel->polling_popover), &rect);
	gtk_popover_popup(GTK_POPOVER(panel->polling_popover));
	gtk_widget_grab_focus(panel->polling_period);
}

/* --- Popover des onglets --- */

/* Décocher masque l'onglet et arrête les pollings de toutes ses macros ; cocher le réaffiche */
static void on_tab_visibility_toggled(GtkCheckButton *check, gpointer data)
{
	const gchar *tab = g_object_get_data(G_OBJECT(check), "tab");

	if(panel == NULL)
		return;
	if(gtk_check_button_get_active(check))
		g_hash_table_remove(panel->hidden_tabs, tab);
	else
	{
		GArray *ids = gt_macro_view_tab_ids(macros_get_set(), tab, default_tab());

		for(guint i = 0; i < ids->len; i++)
			gt_macro_poller_stop(macros_get_poller(), g_array_index(ids, guint, i));
		g_array_unref(ids);
		g_hash_table_add(panel->hidden_tabs, g_strdup(tab));
	}
	gt_macro_panel_rebuild();
}

static void on_tab_bar_right_click(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer data)
{
	GPtrArray *tabs;
	graphene_point_t in = GRAPHENE_POINT_INIT((float)x, (float)y), out;
	GdkRectangle rect;
	GtkWidget *child;

	gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
	if(panel == NULL)
		return;
	tabs = gt_macro_view_tabs(macros_get_set(), default_tab(), NULL);	/* tous les onglets, masqués compris */
	if(tabs->len == 0)
	{
		g_ptr_array_unref(tabs);
		return;
	}
	while((child = gtk_widget_get_first_child(panel->tabs_box)) != NULL)
		gtk_box_remove(GTK_BOX(panel->tabs_box), child);
	for(guint t = 0; t < tabs->len; t++)
	{
		const gchar *tab = g_ptr_array_index(tabs, t);
		GtkWidget *check = gtk_check_button_new_with_label(tab);

		gtk_check_button_set_active(GTK_CHECK_BUTTON(check), !g_hash_table_contains(panel->hidden_tabs, tab));
		g_object_set_data_full(G_OBJECT(check), "tab", g_strdup(tab), g_free);	/* copie : survit au remplacement du jeu */
		g_signal_connect(check, "toggled", G_CALLBACK(on_tab_visibility_toggled), NULL);
		gtk_box_append(GTK_BOX(panel->tabs_box), check);
	}
	g_ptr_array_unref(tabs);
	if(!gtk_widget_compute_point(panel->tab_bar, panel->box, &in, &out))
		out = in;
	rect = (GdkRectangle){ (int)out.x, (int)out.y, 1, 1 };
	gtk_popover_set_pointing_to(GTK_POPOVER(panel->tabs_popover), &rect);
	gtk_popover_popup(GTK_POPOVER(panel->tabs_popover));
}

static void on_tabs_closed(GtkPopover *popover, gpointer data)
{
	if(panel == NULL)
		return;
	focus_terminal();
}

/* --- Construction et fermeture --- */

static void build_polling_popover(void)
{
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	GtkWidget *label = gtk_label_new(_("Period (ms)"));

	panel->polling_popover = gtk_popover_new();
	gtk_widget_set_name(panel->polling_popover, "polling-popover");
	gtk_widget_set_margin_start(box, 8);
	gtk_widget_set_margin_end(box, 8);
	gtk_widget_set_margin_top(box, 8);
	gtk_widget_set_margin_bottom(box, 8);
	panel->polling_mode = gtk_check_button_new_with_label(_("Polling Mode"));
	gtk_widget_set_name(panel->polling_mode, "polling-mode");
	g_signal_connect(panel->polling_mode, "toggled", G_CALLBACK(on_polling_mode_toggled), NULL);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	panel->polling_period = gtk_entry_new();
	gtk_widget_set_name(panel->polling_period, "polling-period");
	gtk_editable_set_width_chars(GTK_EDITABLE(panel->polling_period), 8);
	gtk_entry_set_input_purpose(GTK_ENTRY(panel->polling_period), GTK_INPUT_PURPOSE_DIGITS);
	g_signal_connect(panel->polling_period, "activate", G_CALLBACK(on_polling_period_activate), NULL);
	panel->polling_error = gtk_label_new(NULL);
	gtk_widget_set_name(panel->polling_error, "polling-error");
	gtk_label_set_wrap(GTK_LABEL(panel->polling_error), TRUE);
	gtk_label_set_max_width_chars(GTK_LABEL(panel->polling_error), 30);
	gtk_widget_add_css_class(panel->polling_error, "error");
	gtk_widget_set_visible(panel->polling_error, FALSE);
	gtk_box_append(GTK_BOX(box), panel->polling_mode);
	gtk_box_append(GTK_BOX(box), label);
	gtk_box_append(GTK_BOX(box), panel->polling_period);
	gtk_box_append(GTK_BOX(box), panel->polling_error);
	gtk_popover_set_child(GTK_POPOVER(panel->polling_popover), box);
	gtk_widget_set_parent(panel->polling_popover, panel->box);	/* GtkBox : sa disposition présente le popover (gtk_popover_present) */
	g_signal_connect(panel->polling_popover, "closed", G_CALLBACK(on_polling_closed), NULL);
}

static void build_tabs_popover(void)
{
	panel->tabs_popover = gtk_popover_new();
	gtk_widget_set_name(panel->tabs_popover, "tabs-popover");
	panel->tabs_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	gtk_widget_set_margin_start(panel->tabs_box, 6);
	gtk_widget_set_margin_end(panel->tabs_box, 6);
	gtk_widget_set_margin_top(panel->tabs_box, 6);
	gtk_widget_set_margin_bottom(panel->tabs_box, 6);
	gtk_popover_set_child(GTK_POPOVER(panel->tabs_popover), panel->tabs_box);
	gtk_widget_set_parent(panel->tabs_popover, panel->box);
	g_signal_connect(panel->tabs_popover, "closed", G_CALLBACK(on_tabs_closed), NULL);
}

void gt_macro_panel_install(GtkApplication *app, GtkWidget *main_window)
{
	GtkGesture *right = gtk_gesture_click_new();
	GPropertyAction *action;

	g_return_if_fail(panel == NULL);
	panel = g_new0(MacroPanel, 1);
	panel->buttons = g_hash_table_new(g_direct_hash, g_direct_equal);
	panel->hidden_tabs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	panel->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_name(panel->box, "macro-panel");
	panel->tab_bar = gtk_flow_box_new();
	gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(panel->tab_bar), 1);
	gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(panel->tab_bar), GTK_SELECTION_NONE);
	gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(panel->tab_bar), TRUE);
	gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(panel->tab_bar), 0);
	gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(panel->tab_bar), 0);
	gtk_widget_add_css_class(panel->tab_bar, "macro-tab-bar");
	/* Capture : le clic droit sur un bouton d'onglet ouvre le menu des onglets sans basculer d'onglet */
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right), GDK_BUTTON_SECONDARY);
	gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(right), GTK_PHASE_CAPTURE);
	g_signal_connect(right, "pressed", G_CALLBACK(on_tab_bar_right_click), NULL);
	gtk_widget_add_controller(panel->tab_bar, GTK_EVENT_CONTROLLER(right));
	panel->stack = gtk_stack_new();
	gtk_widget_set_vexpand(panel->stack, TRUE);
	gtk_box_append(GTK_BOX(panel->box), panel->tab_bar);
	gtk_box_append(GTK_BOX(panel->box), panel->stack);
	build_polling_popover();
	build_tabs_popover();
	panel->css = gtk_css_provider_new();
	gtk_css_provider_load_from_string(panel->css, panel_css);
	gtk_style_context_add_provider_for_display(gtk_widget_get_display(main_window), GTK_STYLE_PROVIDER(panel->css),
	                                           GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	gt_main_window_set_slot(main_window, "macro-panel", panel->box);	/* visibilité lue dans [window] par le slot */
	action = g_property_action_new("show-macro-panel", panel->box, "visible");
	g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(action));
	g_object_unref(action);
	gt_macro_panel_rebuild();
}

void gt_macro_panel_shutdown(void)
{
	if(panel == NULL)
		return;
	if(panel->blink_source != 0)
		g_source_remove(panel->blink_source);
	panel->polling_id = 0;
	g_signal_handlers_disconnect_by_func(panel->polling_popover, on_polling_closed, NULL);
	g_signal_handlers_disconnect_by_func(panel->tabs_popover, on_tabs_closed, NULL);
	gtk_widget_unparent(panel->polling_popover);
	gtk_widget_unparent(panel->tabs_popover);
	gtk_style_context_remove_provider_for_display(gtk_widget_get_display(panel->box), GTK_STYLE_PROVIDER(panel->css));
	g_object_unref(panel->css);
	g_hash_table_unref(panel->buttons);
	g_hash_table_unref(panel->hidden_tabs);
	/* Comme app.show-hex-bar : l'action retirée ne garde pas le panneau en vie après la fenêtre (état déjà enregistré par close-request) */
	g_action_map_remove_action(G_ACTION_MAP(g_application_get_default()), "show-macro-panel");
	g_clear_pointer(&panel, g_free);	/* rappels encore branchés sur les widgets : tous vérifient panel != NULL */
}
