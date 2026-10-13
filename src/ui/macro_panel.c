/* macro_panel.c — panneau des boutons de macros (interface GTK 4, spec 6a §6.5 ; un par page en 6b) : une vue du jeu (gt_macros_get_set) et de
 * l'ordonnanceur (gt_macros_get_poller) de sa session. Règles d'affichage du cœur (macro_view.c) ; tout envoi passe par gt_macros_send ; le
 * panneau ne fait qu'afficher l'état du polling. Les deux popovers (polling, onglets) sont créés une fois, enfants de la boîte du panneau, et
 * réutilisés. L'état du panneau est attaché à sa boîte (libéré avec elle) ; chaque rappel le reçoit en paramètre. Fil principal seulement. */

#include <config.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "app_config.h"
#include "macro_arg_widget.h"
#include "macro_panel.h"
#include "macro_view.h"
#include "macros.h"
#include "macros_format.h"
#include "macros_save.h"
#include "session_view.h"

#define BLINK_MS 500
#define ARG_WIDTH_MAX 24	/* largeur d'un champ d'argument, en caractères : le texte reste entier dans le champ */

#define ROW_MARGIN 2	/* marge haute et basse d'une ligne de boutons, en pixels */

#define MACRO_PANEL_KEY "gt-macro-panel"	/* sur la boîte du panneau : MacroPanel, libéré avec elle */
#define MACRO_PANEL_CSS_KEY "gt-macro-panel-css"	/* sur le GdkDisplay : fournisseur CSS commun à tous les panneaux */

/* Seules couleurs fixes : celles du clignotement. Onglets délimités comme l'interface GTK 3 (bordure, onglet affiché plus clair, trait sous la
   barre, cadre autour des pages) mais en teintes de la couleur du texte (currentColor) : ils suivent le thème, clair ou sombre. */
static const char panel_css[] =
	"button.polling-blink { background: #abf573; color: black; border-color: #00cc00; }\n"
	".macro-tab-bar { border-bottom: 2px solid alpha(currentColor, 0.4); padding: 3px 3px 0 3px; }\n"
	".macro-tab-bar flowboxchild { padding: 0; margin: 0; }\n"
	".macro-tab-bar button.macro-tab { border: 1px solid alpha(currentColor, 0.35); border-bottom-width: 0; border-radius: 4px 4px 0 0; padding: 3px 6px;"
	" min-width: 0; margin: 0; background: alpha(currentColor, 0.15); box-shadow: none; }\n"
	".macro-tab-bar button.macro-tab:hover:not(:checked) { background: alpha(currentColor, 0.08); }\n"
	".macro-tab-bar button.macro-tab:checked { background: transparent; font-weight: bold; }\n"
	".macro-pages { border: 1px solid alpha(currentColor, 0.25); border-top-width: 0; }\n"
	"button.macro-button { min-width: 0; }\n";

typedef struct
{
	GtSession      *session;	/* référence, rendue à la libération du panneau */
	GActionMap     *actions;	/* groupe d'actions de la session (référence) : show-macro-panel y est retirée à l'arrêt */
	GtkWidget      *box;	/* « macro-panel » : barre d'onglets puis pages ; parent des deux popovers */
	GtkWidget      *tab_bar;	/* GtkFlowBox de boutons bascule groupés « macro-tab-<onglet> » */
	GtkWidget      *stack;	/* une page par onglet (« page-<n> ») */
	GHashTable     *buttons;	/* identifiant → bouton principal (non possédé), vidée à chaque reconstruction */
	GHashTable     *hidden_tabs;	/* noms d'onglets masqués (possédés) ; jamais enregistrés (§3.2-12) */
	GtkWidget      *polling_popover;	/* « polling-popover », pointeur faible (la boîte le libère à sa destruction) */
	GtkWidget      *polling_mode;	/* « polling-mode » */
	GtkWidget      *polling_period;	/* « polling-period » */
	GtkWidget      *polling_error;	/* « polling-error » */
	GtkWidget      *reset_args;	/* « macro-reset-args » */
	guint           polling_id;	/* macro du popover ouvert, 0 sinon */
	GtkWidget      *tabs_popover;	/* « tabs-popover », pointeur faible */
	GtkWidget      *tabs_box;	/* cases du popover des onglets, refaites à chaque ouverture */
	guint           blink_source;
	gboolean        blink_on;
	gboolean        rebuilding;	/* reconstruction : les champs détruits (perte du focus) ne touchent pas au modèle */
	gboolean        shut_down;	/* gt_macro_panel_shutdown : plus aucun rappel ne touche au modèle */
} MacroPanel;

static void on_button_right_click(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer data);

static MacroPanel *panel_of(GtkWidget *box)
{
	return box != NULL ? g_object_get_data(G_OBJECT(box), MACRO_PANEL_KEY) : NULL;
}

/* Macros de la session du panneau */
static GtMacros *panel_macros(MacroPanel *panel)
{
	return gt_session_get_macros(panel->session);
}

static const gchar *default_tab(void)
{
	return _("General");
}

static void save_macros(MacroPanel *panel)
{
	GtkRoot *root = gtk_widget_get_root(panel->box);

	gt_ui_macros_save(GTK_IS_WINDOW(root) ? GTK_WINDOW(root) : NULL, panel->session, NULL);
}

/* Focus au terminal de la page du panneau (rien si elle est arrêtée) */
static void focus_terminal(MacroPanel *panel)
{
	GtkWidget *view = gt_session_view_lookup(panel->session);

	if(view != NULL)
		gt_session_view_focus_terminal(view);
}

static guint widget_id(GtkWidget *w)
{
	return GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(w), "macro-id"));
}

/* --- Polling : affichage seulement (l'ordonnanceur envoie) --- */

/* « ⏱ Libellé » en mode polling ; plus de clignotement quand la macro ne tourne pas */
static void update_button_appearance(MacroPanel *panel, guint id)
{
	GtkWidget *button = g_hash_table_lookup(panel->buttons, GUINT_TO_POINTER(id));
	const GtMacro *m = gt_macro_set_find(gt_macros_get_set(panel_macros(panel)), id);

	if(button == NULL || m == NULL)
		return;
	if(!gt_macro_poller_is_running(gt_macros_get_poller(panel_macros(panel)), id))
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
	MacroPanel *panel = data;
	GHashTableIter it;
	gpointer key, value;

	panel->blink_on = !panel->blink_on;
	g_hash_table_iter_init(&it, panel->buttons);
	while(g_hash_table_iter_next(&it, &key, &value))
	{
		if(!gt_macro_poller_is_running(gt_macros_get_poller(panel_macros(panel)), GPOINTER_TO_UINT(key)))
			continue;
		if(panel->blink_on)
			gtk_widget_add_css_class(value, "polling-blink");
		else
			gtk_widget_remove_css_class(value, "polling-blink");
	}
	return G_SOURCE_CONTINUE;
}

/* Minuterie de clignotement : n'existe que tant qu'au moins une macro tourne */
static void update_blink_timer(MacroPanel *panel)
{
	guint running = gt_macro_poller_running_count(gt_macros_get_poller(panel_macros(panel)));

	if(running > 0 && panel->blink_source == 0)
		panel->blink_source = g_timeout_add(BLINK_MS, blink_cb, panel);
	else if(running == 0 && panel->blink_source != 0)
	{
		g_source_remove(panel->blink_source);
		panel->blink_source = 0;
	}
}

void gt_macro_panel_polling_changed(GtkWidget *box, guint id, gboolean running)
{
	MacroPanel *panel = panel_of(box);

	if(panel == NULL || panel->shut_down || gt_macros_get_poller(panel_macros(panel)) == NULL)
		return;
	update_button_appearance(panel, id);
	update_blink_timer(panel);
}

/* Clic sur une macro : bascule son polling si le mode est activé, sinon un envoi (arguments enregistrés) */
static void activate_macro(MacroPanel *panel, guint id)
{
	const GtMacro *m = gt_macro_set_find(gt_macros_get_set(panel_macros(panel)), id);

	if(m == NULL)
		return;
	if(m->polling)
		gt_macro_poller_toggle(gt_macros_get_poller(panel_macros(panel)), id);
	else
		gt_macros_send(panel_macros(panel), id, NULL, -1);
}

/* --- Arguments --- */

/* Valeur du champ écrite parmi les valeurs d'usage (jamais enregistrées) si elle diffère de l'argument courant ; TRUE si elle a changé */
static gboolean commit_arg(MacroPanel *panel, GtkWidget *w)
{
	gint k = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "arg-index"));
	GtMacros *macros = panel_macros(panel);
	const gchar *const *current;
	const gchar *saved;
	gchar *value;
	gboolean changed = FALSE;

	if(panel->shut_down || panel->rebuilding)
		return FALSE;
	if(gt_macro_set_find(gt_macros_get_set(macros), widget_id(w)) == NULL)
		return FALSE;
	current = gt_macros_get_args(macros, widget_id(w));
	saved = (current != NULL && k < (gint)g_strv_length((gchar **)current)) ? current[k] : "";
	value = gt_macro_arg_widget_get_value(w);
	if(g_strcmp0(value, saved) != 0)
	{
		gt_macros_set_usage_arg(macros, widget_id(w), k, value);
		changed = TRUE;
	}
	g_free(value);
	return changed;
}

static void on_entry_changed(GtkEditable *editable, gpointer data)
{
	gint len = (gint)g_utf8_strlen(gtk_editable_get_text(editable), -1);

	gtk_editable_set_width_chars(editable, CLAMP(len, 4, ARG_WIDTH_MAX));
	gt_macro_arg_widget_validate(GTK_WIDGET(editable));
}

/* Entrée : prend la valeur comme valeur d'usage (rien n'est enregistré) ; n'envoie pas la macro (comme l'interface GTK 3) */
static void on_entry_activate(GtkEntry *entry, gpointer data)
{
	commit_arg(data, GTK_WIDGET(entry));
}

static void on_entry_focus_leave(GtkEventControllerFocus *controller, gpointer data)
{
	commit_arg(data, gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller)));
}

static void on_dropdown_selected(GObject *dropdown, GParamSpec *pspec, gpointer data)
{
	commit_arg(data, GTK_WIDGET(dropdown));
}

static GtkWidget *make_arg_widget(MacroPanel *panel, const GtMacroSet *set, const GtMacro *m, const macro_arg_info_t *info, gint k)
{
	const GtMacroLists *lists = gt_macro_set_get_lists(set);
	const gchar *const *cur = gt_macros_get_args(panel_macros(panel), m->id);
	const gchar *saved = (cur != NULL && k < (gint)g_strv_length((gchar **)cur)) ? cur[k] : NULL;
	gchar *name = g_strdup_printf("macro-%u-arg-%d", m->id, k);
	GtkWidget *w;

	w = gt_macro_arg_widget_new(lists, info, saved);
	if(GTK_IS_DROP_DOWN(w))
		g_signal_connect(w, "notify::selected", G_CALLBACK(on_dropdown_selected), panel);
	else
	{
		GtkEventController *focus = gtk_event_controller_focus_new();

		on_entry_changed(GTK_EDITABLE(w), NULL);	/* largeur initiale */
		g_signal_connect(w, "changed", G_CALLBACK(on_entry_changed), NULL);
		g_signal_connect(w, "activate", G_CALLBACK(on_entry_activate), panel);
		g_signal_connect(focus, "leave", G_CALLBACK(on_entry_focus_leave), panel);
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
static GtkWidget *make_main_button(MacroPanel *panel, const GtMacro *m)
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
	g_signal_connect(right, "pressed", G_CALLBACK(on_button_right_click), panel);
	gtk_widget_add_controller(button, GTK_EVENT_CONTROLLER(right));
	g_hash_table_insert(panel->buttons, GUINT_TO_POINTER(m->id), button);
	g_free(name);
	g_free(tip);
	return button;
}

static void on_simple_button_clicked(GtkButton *button, gpointer data)
{
	MacroPanel *panel = data;

	if(!panel->shut_down)
		activate_macro(panel, widget_id(GTK_WIDGET(button)));
}

/* Bouton à arguments : champs écrits dans le modèle, un seul enregistrement si l'un a changé, puis clic */
static void on_args_button_clicked(GtkButton *button, gpointer data)
{
	MacroPanel *panel = data;
	GPtrArray *fields = g_object_get_data(G_OBJECT(button), "arg-widgets");
	guint id = widget_id(GTK_WIDGET(button));

	if(panel->shut_down)
		return;
	for(guint k = 0; fields != NULL && k < fields->len; k++)
		commit_arg(panel, g_ptr_array_index(fields, k));
	activate_macro(panel, id);
}

/* Bouton d'une liste à deux valeurs : un envoi avec la valeur du bouton */
static void on_value_button_clicked(GtkButton *button, gpointer data)
{
	MacroPanel *panel = data;
	const gchar *args[] = { g_object_get_data(G_OBJECT(button), "list-value") };

	if(args[0] != NULL && !panel->shut_down)
		gt_macros_send(panel_macros(panel), widget_id(GTK_WIDGET(button)), args, 1);
}

static GtkWidget *make_two_values(MacroPanel *panel, const GtMacroSet *set, const GtMacro *m)
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
		g_signal_connect(b, "clicked", G_CALLBACK(on_value_button_clicked), panel);
		gtk_box_append(GTK_BOX(hbox), b);
		g_free(label);
		g_free(name);
	}
	macro_arg_infos_free(infos, n_args);
	g_free(tip);
	return hbox;
}

static GtkWidget *make_args_row(MacroPanel *panel, const GtMacroSet *set, const GtMacro *m)
{
	gint n_args = 0;
	macro_arg_info_t *infos = macro_get_arg_infos(gt_macro_set_get_lists(set), m->action, &n_args);
	GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	GtkWidget *button = make_main_button(panel, m);
	GPtrArray *fields = g_ptr_array_new();	/* champs non possédés */

	gtk_widget_set_vexpand(hbox, FALSE);	/* explicite : le vexpand d'un champ sans libellé ne remonte pas à la page, la ligne garde sa hauteur naturelle */
	g_signal_connect(button, "clicked", G_CALLBACK(on_args_button_clicked), panel);
	gtk_box_append(GTK_BOX(hbox), button);
	for(gint k = 0; k < n_args; k++)
	{
		GtkWidget *cell = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
		GtkWidget *w = make_arg_widget(panel, set, m, &infos[k], k);

		if(infos[k].label != NULL)
		{
			GtkWidget *label = gtk_label_new(infos[k].label);

			gtk_widget_add_css_class(label, "dim-label");
			gtk_box_append(GTK_BOX(cell), label);
		}
		else
			gtk_widget_set_vexpand(w, TRUE);	/* sans libellé, le champ prend toute la hauteur de la ligne (celle d'un champ à libellé), comme en GTK 3 */
		gtk_widget_set_hexpand(cell, TRUE);
		gtk_box_append(GTK_BOX(cell), w);
		gtk_box_append(GTK_BOX(hbox), cell);
		g_ptr_array_add(fields, w);
	}
	g_object_set_data_full(G_OBJECT(button), "arg-widgets", fields, (GDestroyNotify)g_ptr_array_unref);
	update_button_appearance(panel, m->id);
	macro_arg_infos_free(infos, n_args);
	return hbox;
}

/* Ligne de boutons : 2 px au-dessus et au-dessous en plus de l'espacement de la page (7 px entre deux lignes, comme l'interface GTK 3) */
static void append_row(GtkWidget *vbox, GtkWidget *row)
{
	gtk_widget_set_margin_top(row, ROW_MARGIN);
	gtk_widget_set_margin_bottom(row, ROW_MARGIN);
	gtk_box_append(GTK_BOX(vbox), row);
}

static GtkWidget *make_page(MacroPanel *panel, const GtMacroSet *set, const gchar *tab)
{
	GtkWidget *scrolled = gtk_scrolled_window_new();
	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);

	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	/* Largeur naturelle = celle des boutons et des champs : position initiale du séparateur (session_view.c) ; la largeur minimale reste petite */
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
			b = make_main_button(panel, m);
			g_signal_connect(b, "clicked", G_CALLBACK(on_simple_button_clicked), panel);
			append_row(vbox, b);
			update_button_appearance(panel, m->id);
			break;
		case GT_MACRO_VIEW_TWO_VALUES:
			append_row(vbox, make_two_values(panel, set, m));
			break;
		case GT_MACRO_VIEW_ARGS:
			append_row(vbox, make_args_row(panel, set, m));
			break;
		default:
			break;
		}
	}
	return scrolled;
}

static void on_tab_toggled(GtkToggleButton *toggle, gpointer data)
{
	MacroPanel *panel = data;

	if(panel->shut_down)	/* page arrêtée : plus rien n'est changé */
		return;
	if(gtk_toggle_button_get_active(toggle))
		gtk_stack_set_visible_child_name(GTK_STACK(panel->stack), g_object_get_data(G_OBJECT(toggle), "page"));
}

void gt_macro_panel_rebuild(GtkWidget *box)
{
	MacroPanel *panel = panel_of(box);
	GtMacroSet *set;
	GPtrArray *tabs;
	GtkWidget *child, *first = NULL;
	gboolean empty;

	if(panel == NULL || panel->shut_down)
		return;
	set = gt_macros_get_set(panel_macros(panel));
	if(set == NULL)
		return;
	panel->rebuilding = TRUE;
	if(panel->polling_id != 0 && panel->polling_popover != NULL)
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
			gtk_stack_add_named(GTK_STACK(panel->stack), make_page(panel, set, tab), page);
		gtk_widget_set_name(toggle, name);
		gtk_widget_add_css_class(toggle, "macro-tab");
		gtk_button_set_has_frame(GTK_BUTTON(toggle), FALSE);
		gtk_widget_set_focus_on_click(toggle, FALSE);
		g_object_set_data_full(G_OBJECT(toggle), "page", page, g_free);
		if(first == NULL)
			first = toggle;
		else
			gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(toggle), GTK_TOGGLE_BUTTON(first));
		g_signal_connect(toggle, "toggled", G_CALLBACK(on_tab_toggled), panel);
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
static gboolean apply_period(MacroPanel *panel, gboolean show_error)
{
	guint id = panel->polling_id;
	const GtMacro *m = gt_macro_set_find(gt_macros_get_set(panel_macros(panel)), id);
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
		gt_macro_set_set_polling(gt_macros_get_set(panel_macros(panel)), id, m->polling, period);
		gt_macro_poller_period_changed(gt_macros_get_poller(panel_macros(panel)), id);
		save_macros(panel);
	}
	return TRUE;
}

static void on_polling_mode_toggled(GtkCheckButton *check, gpointer data)
{
	guint id;
	const GtMacro *m;
	MacroPanel *panel = data;
	gboolean active = gtk_check_button_get_active(check);

	if(panel->shut_down)
		return;
	id = panel->polling_id;
	m = gt_macro_set_find(gt_macros_get_set(panel_macros(panel)), id);
	if(m == NULL)
		return;
	gt_macro_set_set_polling(gt_macros_get_set(panel_macros(panel)), id, active, m->polling_period_ms);
	if(!active)
		gt_macro_poller_stop(gt_macros_get_poller(panel_macros(panel)), id);
	update_button_appearance(panel, id);
	save_macros(panel);
}

/* « Reset arguments » : valeurs d'usage de la macro du popover effacées (retour aux valeurs par défaut), panneau reconstruit */
static void on_reset_args_clicked(GtkButton *button, gpointer data)
{
	MacroPanel *panel = data;
	guint id = panel->polling_id;
	GtkWidget *box = panel->box;

	if(panel->shut_down || id == 0)
		return;
	gt_macros_reset_usage(panel_macros(panel), id);
	if(panel->polling_popover != NULL)
		gtk_popover_popdown(GTK_POPOVER(panel->polling_popover));
	gt_macro_panel_rebuild(box);
}

static void on_polling_period_activate(GtkEntry *entry, gpointer data)
{
	MacroPanel *panel = data;

	if(panel->shut_down)
		return;
	if(apply_period(panel, TRUE))
		gtk_popover_popdown(GTK_POPOVER(panel->polling_popover));
}

/* Fermeture (Entrée, Échap, clic ailleurs, reconstruction) : période valide appliquée, focus rendu au terminal */
static void on_polling_closed(GtkPopover *popover, gpointer data)
{
	MacroPanel *panel = data;

	if(panel->shut_down)
		return;
	apply_period(panel, FALSE);
	panel->polling_id = 0;
	focus_terminal(panel);
}

static void on_button_right_click(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer data)
{
	MacroPanel *panel = data;
	GtkWidget *button = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
	guint id = widget_id(button);
	const GtMacro *m;
	graphene_rect_t bounds;
	GdkRectangle rect;
	gchar buf[16];

	gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
	if(panel->shut_down || panel->polling_popover == NULL)
		return;
	m = gt_macro_set_find(gt_macros_get_set(panel_macros(panel)), id);
	if(m == NULL || !gtk_widget_compute_bounds(button, panel->box, &bounds))
		return;
	panel->polling_id = id;
	g_signal_handlers_block_by_func(panel->polling_mode, on_polling_mode_toggled, panel);
	gtk_check_button_set_active(GTK_CHECK_BUTTON(panel->polling_mode), m->polling);
	g_signal_handlers_unblock_by_func(panel->polling_mode, on_polling_mode_toggled, panel);
	g_snprintf(buf, sizeof(buf), "%u", m->polling_period_ms);
	gtk_editable_set_text(GTK_EDITABLE(panel->polling_period), buf);
	gtk_widget_remove_css_class(panel->polling_period, "error");
	gtk_widget_set_visible(panel->polling_error, FALSE);
	gtk_widget_set_sensitive(panel->reset_args, macro_count_format_args(gt_macro_set_get_lists(gt_macros_get_set(panel_macros(panel))), m->action) > 0);
	rect = (GdkRectangle){ (int)bounds.origin.x, (int)bounds.origin.y, (int)bounds.size.width, (int)bounds.size.height };
	gtk_popover_set_pointing_to(GTK_POPOVER(panel->polling_popover), &rect);
	gtk_popover_popup(GTK_POPOVER(panel->polling_popover));
	gtk_widget_grab_focus(panel->polling_period);
}

/* --- Popover des onglets --- */

/* Décocher masque l'onglet et arrête les pollings de toutes ses macros ; cocher le réaffiche */
static void on_tab_visibility_toggled(GtkCheckButton *check, gpointer data)
{
	MacroPanel *panel = data;
	const gchar *tab = g_object_get_data(G_OBJECT(check), "tab");

	if(panel->shut_down)
		return;
	if(gtk_check_button_get_active(check))
		g_hash_table_remove(panel->hidden_tabs, tab);
	else
	{
		GArray *ids = gt_macro_view_tab_ids(gt_macros_get_set(panel_macros(panel)), tab, default_tab());

		for(guint i = 0; i < ids->len; i++)
			gt_macro_poller_stop(gt_macros_get_poller(panel_macros(panel)), g_array_index(ids, guint, i));
		g_array_unref(ids);
		g_hash_table_add(panel->hidden_tabs, g_strdup(tab));
	}
	gt_macro_panel_rebuild(panel->box);
}

static void on_tab_bar_right_click(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer data)
{
	MacroPanel *panel = data;
	GPtrArray *tabs;
	graphene_point_t in = GRAPHENE_POINT_INIT((float)x, (float)y), out;
	GdkRectangle rect;
	GtkWidget *child;

	gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
	if(panel->shut_down || panel->tabs_popover == NULL)
		return;
	tabs = gt_macro_view_tabs(gt_macros_get_set(panel_macros(panel)), default_tab(), NULL);	/* tous les onglets, masqués compris */
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
		g_signal_connect(check, "toggled", G_CALLBACK(on_tab_visibility_toggled), panel);
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
	MacroPanel *panel = data;

	if(!panel->shut_down)
		focus_terminal(panel);
}

/* --- Construction et fermeture --- */

static void build_polling_popover(MacroPanel *panel)
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
	g_signal_connect(panel->polling_mode, "toggled", G_CALLBACK(on_polling_mode_toggled), panel);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	panel->polling_period = gtk_entry_new();
	gtk_widget_set_name(panel->polling_period, "polling-period");
	gtk_editable_set_width_chars(GTK_EDITABLE(panel->polling_period), 8);
	gtk_entry_set_input_purpose(GTK_ENTRY(panel->polling_period), GTK_INPUT_PURPOSE_DIGITS);
	g_signal_connect(panel->polling_period, "activate", G_CALLBACK(on_polling_period_activate), panel);
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
	panel->reset_args = gtk_button_new_with_label(_("Reset arguments"));
	gtk_widget_set_name(panel->reset_args, "macro-reset-args");
	gtk_accessible_update_property(GTK_ACCESSIBLE(panel->reset_args), GTK_ACCESSIBLE_PROPERTY_LABEL, _("Reset arguments"), -1);
	g_signal_connect(panel->reset_args, "clicked", G_CALLBACK(on_reset_args_clicked), panel);
	gtk_box_append(GTK_BOX(box), panel->reset_args);
	gtk_popover_set_child(GTK_POPOVER(panel->polling_popover), box);
	gtk_widget_set_parent(panel->polling_popover, panel->box);	/* GtkBox : sa disposition présente le popover (gtk_popover_present) */
	g_object_add_weak_pointer(G_OBJECT(panel->polling_popover), (gpointer *)&panel->polling_popover);
	g_signal_connect(panel->polling_popover, "closed", G_CALLBACK(on_polling_closed), panel);
}

static void build_tabs_popover(MacroPanel *panel)
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
	g_object_add_weak_pointer(G_OBJECT(panel->tabs_popover), (gpointer *)&panel->tabs_popover);
	g_signal_connect(panel->tabs_popover, "closed", G_CALLBACK(on_tabs_closed), panel);
}

/* Fournisseur CSS des panneaux : un seul par écran, rangé sur le GdkDisplay (aucune statique), jamais retiré */
static void ensure_css(GdkDisplay *display)
{
	GtkCssProvider *css;

	if(display == NULL || g_object_get_data(G_OBJECT(display), MACRO_PANEL_CSS_KEY) != NULL)
		return;
	css = gtk_css_provider_new();
	gtk_css_provider_load_from_string(css, panel_css);
	gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_set_data_full(G_OBJECT(display), MACRO_PANEL_CSS_KEY, css, g_object_unref);
}

/* Libération avec la boîte (finalisation) : minuterie retirée si l'arrêt n'a pas eu lieu, popovers déjà libérés par la boîte */
static void macro_panel_free(gpointer data)
{
	MacroPanel *panel = data;

	if(panel->blink_source != 0)
		g_source_remove(panel->blink_source);
	if(panel->polling_popover != NULL)
		g_object_remove_weak_pointer(G_OBJECT(panel->polling_popover), (gpointer *)&panel->polling_popover);
	if(panel->tabs_popover != NULL)
		g_object_remove_weak_pointer(G_OBJECT(panel->tabs_popover), (gpointer *)&panel->tabs_popover);
	g_hash_table_unref(panel->buttons);
	g_hash_table_unref(panel->hidden_tabs);
	g_clear_object(&panel->actions);
	g_object_unref(panel->session);
	g_free(panel);
}

GtkWidget *gt_macro_panel_new(GtSession *s, GActionMap *tab_actions, gboolean visible)
{
	GtkGesture *right = gtk_gesture_click_new();
	GPropertyAction *action;
	MacroPanel *panel;

	g_return_val_if_fail(GT_IS_SESSION(s), NULL);
	panel = g_new0(MacroPanel, 1);
	panel->session = g_object_ref(s);
	panel->buttons = g_hash_table_new(g_direct_hash, g_direct_equal);
	panel->hidden_tabs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	panel->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_name(panel->box, "macro-panel");
	gtk_widget_set_visible(panel->box, visible);
	g_object_set_data_full(G_OBJECT(panel->box), MACRO_PANEL_KEY, panel, macro_panel_free);
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
	g_signal_connect(right, "pressed", G_CALLBACK(on_tab_bar_right_click), panel);
	gtk_widget_add_controller(panel->tab_bar, GTK_EVENT_CONTROLLER(right));
	panel->stack = gtk_stack_new();
	gtk_widget_set_vexpand(panel->stack, TRUE);
	gtk_widget_add_css_class(panel->stack, "macro-pages");
	gtk_box_append(GTK_BOX(panel->box), panel->tab_bar);
	gtk_box_append(GTK_BOX(panel->box), panel->stack);
	build_polling_popover(panel);
	build_tabs_popover(panel);
	ensure_css(gtk_widget_get_display(panel->box));
	action = g_property_action_new("show-macro-panel", panel->box, "visible");
	panel->actions = g_object_ref(tab_actions);
	g_action_map_add_action(tab_actions, G_ACTION(action));
	g_object_unref(action);
	gt_macro_panel_rebuild(panel->box);
	return panel->box;
}

void gt_macro_panel_shutdown(GtkWidget *box)
{
	MacroPanel *panel = panel_of(box);

	if(panel == NULL || panel->shut_down)
		return;
	panel->shut_down = TRUE;
	g_clear_handle_id(&panel->blink_source, g_source_remove);
	panel->polling_id = 0;
	/* Popovers par pointeurs faibles : déjà libérés si la boîte a été détruite avant l'arrêt */
	if(panel->polling_popover != NULL)
	{
		g_signal_handlers_disconnect_by_func(panel->polling_popover, on_polling_closed, panel);
		gtk_widget_unparent(panel->polling_popover);
	}
	if(panel->tabs_popover != NULL)
	{
		g_signal_handlers_disconnect_by_func(panel->tabs_popover, on_tabs_closed, panel);
		gtk_widget_unparent(panel->tabs_popover);
	}
	/* Comme show-hex-bar : l'action retirée du groupe de la session ne garde plus le panneau en vie (état déjà enregistré par close-request) */
	g_action_map_remove_action(panel->actions, "show-macro-panel");
	g_clear_object(&panel->actions);
}
