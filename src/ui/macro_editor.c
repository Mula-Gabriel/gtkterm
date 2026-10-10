/* macro_editor.c — fenêtre « Configure Macros » (interface GTK 4, spec 6a §6.5) : non modale, instance unique, pages Macros et Lists.
 * Les lignes sont des copies (GtMacroRow, GtMacroListRow) : rien n'est appliqué avant OK ou la fermeture par la barre de titre ; Cancel
 * jette tout. Un jeu remplacé pendant l'édition ferme la fenêtre sans rien enregistrer (ses lignes décrivent l'ancien jeu). La fenêtre
 * n'est pas rattachée à l'application : ses raccourcis (Ctrl+Maj+Q…) ne s'y appliquent pas, la capture reçoit toutes les touches. */

#include <config.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "app.h"
#include "app_config.h"
#include "dialogs.h"
#include "macro_accel.h"
#include "macro_editor.h"
#include "macro_row.h"
#include "macros.h"

enum { PAGE_MACROS, PAGE_LISTS };

typedef struct
{
	GtkWidget         *window;	/* « macro-editor » */
	GtMacroSet        *set;	/* jeu édité ; jamais déréférencé s'il n'est plus le jeu courant */
	GListStore        *rows;	/* GtMacroRow */
	GListStore        *list_rows;	/* GtMacroListRow */
	GtkSelectionModel *selection;	/* GtkMultiSelection sur rows (possédée par sa vue) */
	GtkSelectionModel *list_selection;	/* GtkMultiSelection sur list_rows */
	GtkWidget         *message;	/* « macro-editor-error » : consigne de capture ou refus */
	GtMacroRow        *capture_row;	/* ligne qui attend son raccourci ; NULL hors capture */
} MacroEditor;

/* Cellule d'une colonne : propriété de la ligne, nom du widget, sélection de la vue */
typedef struct
{
	const gchar       *prop;
	const gchar       *cell_name;
	GtkSelectionModel *selection;
} CellSpec;

static MacroEditor *editor = NULL;

/* xgettext:no-c-format */
static const gchar *help_text = N_(
	"Action: the data sent on the port. Plain text, plus the escapes \\r (carriage return), \\n (line feed), \\t (tab), \\a, \\b, \\f, \\v, "
	"\\\\ (backslash) and \\xHH (one byte in hexadecimal, e.g. \\x0D).\n\n"
	"Arguments: each specifier gets a field in the macro panel; its value is remembered in the macros file. %d %i (integer), %u %o %x %X "
	"(unsigned), %f %e %g %a (decimal number), %c (character), %s (text), with optional flags, width, precision and length (e.g. %02X, %.3f, "
	"%ld); %% sends a percent sign. %#Name picks a value from the list \"Name\" of the Lists page; a macro whose only argument is a list of "
	"exactly two values shows two buttons.\n\n"
	"Separators: the action %& draws a line in the panel, [Title] %& a line with a title.\n\n"
	"Tab: the panel tab of the macro (empty: General). A macro without label or action is not shown in the panel; its shortcut still works.\n\n"
	"Shortcut: select a macro, click \"Capture Shortcut\", then press the keys (Ctrl, Alt or Super with a key, or a function key alone); "
	"Escape cancels. A shortcut already used by the application or by another macro is refused.\n\n"
	"Polling: right-click a macro button in the panel for \"Polling Mode\" and its period (1 to 86400000 ms); a click then starts or stops "
	"the repeated sending. Right-click the tab bar to hide or show tabs; hiding a tab stops its polling.\n\n"
	"Examples: \"Hello\\n\" sends Hello and a line feed; \"Hello\\x0A\" does the same; \"SPEED %d\\r\" asks for a number.");

static void set_message(const gchar *text, gboolean error)
{
	gtk_label_set_text(GTK_LABEL(editor->message), text != NULL ? text : "");
	gtk_widget_set_visible(editor->message, text != NULL && text[0] != '\0');
	if(error)
		gtk_widget_add_css_class(editor->message, "error");
	else
		gtk_widget_remove_css_class(editor->message, "error");
}

static void stop_capture(void)
{
	if(editor->capture_row == NULL)
		return;
	g_clear_object(&editor->capture_row);
	set_message(NULL, FALSE);
}

/* Positions sélectionnées, croissantes ; g_free */
static guint *selected_positions(GtkSelectionModel *model, guint *n_out)
{
	GtkBitset *set = gtk_selection_model_get_selection(model);
	guint *pos = g_new(guint, MAX(gtk_bitset_get_size(set), 1));
	GtkBitsetIter it;
	guint value, k = 0;

	for(gboolean ok = gtk_bitset_iter_init_first(&it, set, &value); ok; ok = gtk_bitset_iter_next(&it, &value))
		pos[k++] = value;
	gtk_bitset_unref(set);
	*n_out = k;
	return pos;
}

static void reselect(GtkSelectionModel *model, const guint *pos, guint n)
{
	gtk_selection_model_unselect_all(model);
	for(guint k = 0; k < n; k++)
		gtk_selection_model_select_item(model, pos[k], FALSE);
}

static GListStore *page_store(gint page)
{
	return page == PAGE_MACROS ? editor->rows : editor->list_rows;
}

static GtkSelectionModel *page_selection(gint page)
{
	return page == PAGE_MACROS ? editor->selection : editor->list_selection;
}

/* --- Cellules --- */

/* Passer une cellule en édition sélectionne sa ligne seule et annule une capture */
static void on_cell_editing(GObject *label, GParamSpec *pspec, gpointer cell)
{
	CellSpec *spec = g_object_get_data(label, "spec");

	if(editor == NULL || !gtk_editable_label_get_editing(GTK_EDITABLE_LABEL(label)))
		return;
	stop_capture();
	gtk_selection_model_select_item(spec->selection, gtk_column_view_cell_get_position(GTK_COLUMN_VIEW_CELL(cell)), TRUE);
}

static void editable_setup(GtkSignalListItemFactory *factory, GObject *cell, gpointer data)
{
	CellSpec *spec = data;
	GtkWidget *label = gtk_editable_label_new("");

	gtk_widget_set_name(label, spec->cell_name);
	g_object_set_data(G_OBJECT(label), "spec", spec);
	g_signal_connect(label, "notify::editing", G_CALLBACK(on_cell_editing), cell);
	gtk_column_view_cell_set_child(GTK_COLUMN_VIEW_CELL(cell), label);
}

/* Liaison dans les deux sens : la ligne suit la frappe, la valeur en cours d'édition est donc prise par OK */
static void editable_bind(GtkSignalListItemFactory *factory, GObject *cell, gpointer data)
{
	CellSpec *spec = data;
	GObject *row = gtk_column_view_cell_get_item(GTK_COLUMN_VIEW_CELL(cell));
	GtkWidget *label = gtk_column_view_cell_get_child(GTK_COLUMN_VIEW_CELL(cell));
	GBinding *binding = g_object_bind_property(row, spec->prop, label, "text", G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);

	g_object_set_data(cell, "binding", binding);
}

static void cell_unbind(GtkSignalListItemFactory *factory, GObject *cell, gpointer data)
{
	GBinding *binding = g_object_get_data(cell, "binding");

	if(binding != NULL)
		g_binding_unbind(binding);
	g_object_set_data(cell, "binding", NULL);
}

static gboolean shortcut_to_text(GBinding *binding, const GValue *from, GValue *to, gpointer data)
{
	const gchar *s = g_value_get_string(from);

	g_value_set_string(to, s != NULL && s[0] != '\0' ? s : _("None"));
	return TRUE;
}

static void shortcut_setup(GtkSignalListItemFactory *factory, GObject *cell, gpointer data)
{
	CellSpec *spec = data;
	GtkWidget *label = gtk_label_new(NULL);

	gtk_widget_set_name(label, spec->cell_name);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_column_view_cell_set_child(GTK_COLUMN_VIEW_CELL(cell), label);
}

static void shortcut_bind(GtkSignalListItemFactory *factory, GObject *cell, gpointer data)
{
	GObject *row = gtk_column_view_cell_get_item(GTK_COLUMN_VIEW_CELL(cell));
	GtkWidget *label = gtk_column_view_cell_get_child(GTK_COLUMN_VIEW_CELL(cell));
	GBinding *binding = g_object_bind_property_full(row, "shortcut", label, "label", G_BINDING_SYNC_CREATE, shortcut_to_text, NULL, NULL, NULL);

	g_object_set_data(cell, "binding", binding);
}

static void add_column(GtkColumnView *view, GtkSelectionModel *selection, const gchar *title, const gchar *prop, const gchar *cell_name, gboolean editable,
           gboolean expand)
{
	GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
	CellSpec *spec = g_new0(CellSpec, 1);
	GtkColumnViewColumn *column;

	spec->prop = prop;
	spec->cell_name = cell_name;
	spec->selection = selection;
	g_object_set_data_full(G_OBJECT(factory), "spec", spec, g_free);
	g_signal_connect(factory, "setup", G_CALLBACK(editable ? editable_setup : shortcut_setup), spec);
	g_signal_connect(factory, "bind", G_CALLBACK(editable ? editable_bind : shortcut_bind), spec);
	g_signal_connect(factory, "unbind", G_CALLBACK(cell_unbind), spec);
	column = gtk_column_view_column_new(title, factory);	/* prend la fabrique */
	gtk_column_view_column_set_resizable(column, TRUE);
	gtk_column_view_column_set_expand(column, expand);
	/* Aucun trieur : un clic sur l'en-tête ne trie pas, l'ordre est celui de l'utilisateur (§6.5) */
	gtk_column_view_append_column(view, column);
	g_object_unref(column);
}

/* --- Boutons des pages --- */

static void on_add(GtkButton *button, gpointer data)
{
	gint page = GPOINTER_TO_INT(data);
	GListStore *store;
	guint n = 0, *pos, at;
	gpointer row;

	if(editor == NULL)
		return;
	store = page_store(page);
	pos = selected_positions(page_selection(page), &n);
	at = gt_macro_rows_insert_position(pos, n, g_list_model_get_n_items(G_LIST_MODEL(store)));
	row = page == PAGE_MACROS ? (gpointer)gt_macro_row_new(0, "", "", "", "") : (gpointer)gt_macro_list_row_new("NewList", "display", "value");
	stop_capture();
	g_list_store_insert(store, at, row);
	g_object_unref(row);
	gtk_selection_model_select_item(page_selection(page), at, TRUE);
	g_free(pos);
}

static void on_delete(GtkButton *button, gpointer data)
{
	gint page = GPOINTER_TO_INT(data);
	guint n = 0, *pos;

	if(editor == NULL)
		return;
	pos = selected_positions(page_selection(page), &n);
	stop_capture();
	gt_macro_rows_remove(page_store(page), pos, n);
	g_free(pos);
}

static void move_selected(gint page, gint delta)
{
	guint n = 0, *pos = selected_positions(page_selection(page), &n);

	stop_capture();
	if(gt_macro_rows_move(page_store(page), pos, n, delta))
		reselect(page_selection(page), pos, n);	/* la sélection suit les lignes déplacées */
	g_free(pos);
}

static void on_up(GtkButton *button, gpointer data)
{
	if(editor == NULL)
		return;
	move_selected(GPOINTER_TO_INT(data), -1);
}

static void on_down(GtkButton *button, gpointer data)
{
	if(editor == NULL)
		return;
	move_selected(GPOINTER_TO_INT(data), +1);
}

static void on_unshortcut(GtkButton *button, gpointer data)
{
	guint n = 0, *pos;

	if(editor == NULL)
		return;
	pos = selected_positions(editor->selection, &n);
	stop_capture();
	if(n > 0)
	{
		GtMacroRow *row = g_list_model_get_item(G_LIST_MODEL(editor->rows), pos[0]);

		gt_macro_row_set_shortcut(row, "");
		g_object_unref(row);
	}
	g_free(pos);
}

static void on_capture(GtkButton *button, gpointer data)
{
	guint n = 0, *pos;

	if(editor == NULL)
		return;
	pos = selected_positions(editor->selection, &n);
	stop_capture();
	if(n == 0)
		set_message(_("Select a macro first."), TRUE);
	else
	{
		gchar *text;

		editor->capture_row = g_list_model_get_item(G_LIST_MODEL(editor->rows), pos[0]);
		text = g_strdup_printf(_("Press the new shortcut for \"%s\" (Escape cancels)."), gt_macro_row_get_label(editor->capture_row));
		set_message(text, FALSE);
		g_free(text);
	}
	g_free(pos);
}

/* Raccourci déjà pris par une action de l'application ou par une autre ligne : message de refus et TRUE */
static gboolean refuse_if_taken(const gchar *accel)
{
	GArray *owners = gt_accel_owners_new();
	GApplication *app = g_application_get_default();
	const GtAccelOwner *owner;
	gchar *text = NULL;
	gboolean taken;

	if(GTK_IS_APPLICATION(app))
		gt_accel_owners_add_app(owners, GTK_APPLICATION(app));
	for(guint i = 0; i < g_list_model_get_n_items(G_LIST_MODEL(editor->rows)); i++)
	{
		GtMacroRow *row = g_list_model_get_item(G_LIST_MODEL(editor->rows), i);
		const gchar *label = gt_macro_row_get_label(row);

		if(row != editor->capture_row)
			gt_accel_owners_add(owners, gt_macro_row_get_shortcut(row), label[0] != '\0' ? label : _("(no label)"), FALSE);
		g_object_unref(row);
	}
	owner = gt_accel_owners_find(owners, accel);
	taken = owner != NULL;
	if(owner != NULL && owner->is_action)
		text = g_strdup_printf(_("%s is already used by the application command \"%s\". Press other keys, or Escape to cancel."), accel, owner->owner);
	else if(owner != NULL)
		text = g_strdup_printf(_("%s is already used by the macro \"%s\". Press other keys, or Escape to cancel."), accel, owner->owner);
	if(text != NULL)
		set_message(text, TRUE);
	g_free(text);
	g_array_unref(owners);	/* owner pointait dans ce tableau */
	return taken;
}

/* Capture : toutes les touches traitées sont consommées (phase de capture de la fenêtre) */
static gboolean on_key_pressed(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer data)
{
	gchar *accel = NULL;

	if(editor == NULL || editor->capture_row == NULL)
		return GDK_EVENT_PROPAGATE;
	switch(gt_macro_accel_from_key(keyval, state, &accel))
	{
	case GT_ACCEL_KEY_MODIFIER:
		return GDK_EVENT_PROPAGATE;
	case GT_ACCEL_KEY_CANCEL:
		stop_capture();
		return GDK_EVENT_STOP;
	case GT_ACCEL_KEY_NEEDS_MODIFIER:
		set_message(_("A shortcut needs Ctrl, Alt or Super, unless it is a function key (F1 to F35). Press other keys, or Escape to cancel."), TRUE);
		return GDK_EVENT_STOP;
	case GT_ACCEL_KEY_ACCEPTED:
		break;
	}
	if(!refuse_if_taken(accel))
	{
		gt_macro_row_set_shortcut(editor->capture_row, accel);
		stop_capture();
	}
	g_free(accel);
	return GDK_EVENT_STOP;
}

static void on_help(GtkButton *button, gpointer data)
{
	if(editor == NULL)
		return;
	stop_capture();	/* règle : tout autre bouton annule la capture en cours */
	gt_ui_message(GTK_WINDOW(editor->window), GT_APP_MSG_INFO, _(help_text));
}

/* OK : les lignes, dans l'ordre affiché, avec leur identifiant (arguments et polling suivent l'identifiant, M16) */
static void apply_edits(void)
{
	GtMacroEdit *edits;
	guint n = 0;

	if(macros_get_set() != editor->set)	/* jeu remplacé : ces lignes ne s'appliquent jamais au nouveau jeu */
		return;
	edits = gt_macro_rows_to_edits(G_LIST_MODEL(editor->rows), &n);
	gt_macro_set_apply_edits(editor->set, edits, n, gt_macro_list_rows_to_lists(G_LIST_MODEL(editor->list_rows)));
	g_free(edits);
	gt_macro_poller_sync(macros_get_poller());
	macros_changed();	/* raccourcis et panneau ; ce module voit le même jeu et ne ferme rien */
	macros_save_file(NULL, TRUE);
	config_remember_macros_file();
}

static void on_ok(GtkButton *button, gpointer data)
{
	if(editor == NULL)
		return;
	apply_edits();
	gtk_window_destroy(GTK_WINDOW(editor->window));	/* sans close-request */
}

static void on_cancel(GtkButton *button, gpointer data)
{
	if(editor == NULL)
		return;
	gtk_window_destroy(GTK_WINDOW(editor->window));
}

/* Fermer par la barre de titre vaut OK (comportement conservé) */
static gboolean on_close_request(GtkWindow *window, gpointer data)
{
	if(editor == NULL)
		return FALSE;
	apply_edits();
	return FALSE;
}

static void on_destroy(GtkWidget *window, gpointer data)
{
	g_clear_object(&editor->capture_row);
	g_clear_object(&editor->rows);
	g_clear_object(&editor->list_rows);
	g_clear_pointer(&editor, g_free);	/* les rappels encore branchés vérifient editor != NULL */
}

/* --- Construction --- */

static GtkWidget *add_button(GtkWidget *box, const gchar *mnemonic, const gchar *name, GCallback callback, gint page)
{
	GtkWidget *button = gtk_button_new_with_mnemonic(mnemonic);

	gtk_widget_set_name(button, name);
	gtk_widget_set_hexpand(button, TRUE);
	g_signal_connect(button, "clicked", callback, GINT_TO_POINTER(page));
	gtk_box_append(GTK_BOX(box), button);
	return button;
}

static GtkWidget *button_row(void)
{
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

	gtk_box_set_homogeneous(GTK_BOX(box), TRUE);
	return box;
}

static GtkWidget *build_page(gint page)
{
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	GtkWidget *scrolled = gtk_scrolled_window_new();
	GtkWidget *row1 = button_row(), *row2 = button_row();
	GtkSelectionModel *selection = GTK_SELECTION_MODEL(gtk_multi_selection_new(G_LIST_MODEL(g_object_ref(page_store(page)))));
	GtkWidget *view = gtk_column_view_new(selection);	/* prend la sélection */

	gtk_column_view_set_reorderable(GTK_COLUMN_VIEW(view), FALSE);
	gtk_column_view_set_show_column_separators(GTK_COLUMN_VIEW(view), TRUE);
	if(page == PAGE_MACROS)
	{
		editor->selection = selection;
		gtk_widget_set_name(view, "macro-list");
		add_column(GTK_COLUMN_VIEW(view), selection, _("Label"), "label", "macro-cell-label", TRUE, TRUE);
		add_column(GTK_COLUMN_VIEW(view), selection, _("Shortcut"), "shortcut", "macro-cell-shortcut", FALSE, FALSE);
		add_column(GTK_COLUMN_VIEW(view), selection, _("Action"), "action", "macro-cell-action", TRUE, TRUE);
		add_column(GTK_COLUMN_VIEW(view), selection, _("Tab"), "tab", "macro-cell-tab", TRUE, FALSE);
		add_button(row1, _("_Add"), "macro-add", G_CALLBACK(on_add), page);
		add_button(row1, _("_Delete"), "macro-delete", G_CALLBACK(on_delete), page);
		add_button(row1, _("_Capture Shortcut"), "macro-capture", G_CALLBACK(on_capture), page);
		add_button(row1, _("Delete Shortcut"), "macro-unshortcut", G_CALLBACK(on_unshortcut), page);
		add_button(row2, _("Move _Up"), "macro-up", G_CALLBACK(on_up), page);
		add_button(row2, _("Move _Down"), "macro-down", G_CALLBACK(on_down), page);
	}
	else
	{
		editor->list_selection = selection;
		gtk_widget_set_name(view, "lists-list");
		add_column(GTK_COLUMN_VIEW(view), selection, _("List"), "list", "list-cell-name", TRUE, FALSE);
		add_column(GTK_COLUMN_VIEW(view), selection, _("Display"), "display", "list-cell-display", TRUE, TRUE);
		add_column(GTK_COLUMN_VIEW(view), selection, _("Value"), "value", "list-cell-value", TRUE, TRUE);
		add_button(row1, _("_Add Entry"), "lists-add", G_CALLBACK(on_add), page);
		add_button(row1, _("_Delete Entry"), "lists-delete", G_CALLBACK(on_delete), page);
		add_button(row2, _("Move _Up"), "lists-up", G_CALLBACK(on_up), page);
		add_button(row2, _("Move _Down"), "lists-down", G_CALLBACK(on_down), page);
	}
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(scrolled), TRUE);
	gtk_widget_set_vexpand(scrolled, TRUE);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), view);
	gtk_box_append(GTK_BOX(box), scrolled);
	gtk_box_append(GTK_BOX(box), row1);
	gtk_box_append(GTK_BOX(box), row2);
	return box;
}

void gt_macro_editor_open(GtkWindow *parent)
{
	GtkWidget *vbox, *stack, *switcher, *bottom;
	GtkEventController *keys;

	if(editor != NULL)
	{
		gtk_window_present(GTK_WINDOW(editor->window));
		return;
	}
	editor = g_new0(MacroEditor, 1);
	editor->set = macros_get_set();
	editor->rows = gt_macro_rows_from_set(editor->set);
	editor->list_rows = gt_macro_list_rows_from_lists(editor->set != NULL ? gt_macro_set_get_lists(editor->set) : NULL);
	editor->window = gtk_window_new();
	gtk_widget_set_name(editor->window, "macro-editor");
	gtk_window_set_title(GTK_WINDOW(editor->window), _("Configure Macros"));
	gtk_window_set_default_size(GTK_WINDOW(editor->window), 520, 500);
	if(parent != NULL)
	{
		gtk_window_set_transient_for(GTK_WINDOW(editor->window), parent);
		gtk_window_set_destroy_with_parent(GTK_WINDOW(editor->window), TRUE);
	}
	g_signal_connect(editor->window, "close-request", G_CALLBACK(on_close_request), NULL);
	g_signal_connect(editor->window, "destroy", G_CALLBACK(on_destroy), NULL);
	keys = gtk_event_controller_key_new();
	gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
	g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key_pressed), NULL);
	gtk_widget_add_controller(editor->window, keys);

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_margin_start(vbox, 8);
	gtk_widget_set_margin_end(vbox, 8);
	gtk_widget_set_margin_top(vbox, 8);
	gtk_widget_set_margin_bottom(vbox, 8);
	stack = gtk_stack_new();
	gtk_widget_set_vexpand(stack, TRUE);
	gtk_stack_add_titled(GTK_STACK(stack), build_page(PAGE_MACROS), "macros", _("Macros"));
	gtk_stack_add_titled(GTK_STACK(stack), build_page(PAGE_LISTS), "lists", _("Lists"));
	switcher = gtk_stack_switcher_new();
	gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher), GTK_STACK(stack));
	gtk_widget_set_halign(switcher, GTK_ALIGN_CENTER);
	editor->message = gtk_label_new(NULL);
	gtk_widget_set_name(editor->message, "macro-editor-error");
	gtk_label_set_wrap(GTK_LABEL(editor->message), TRUE);
	gtk_label_set_xalign(GTK_LABEL(editor->message), 0.0);
	gtk_widget_set_visible(editor->message, FALSE);
	bottom = button_row();
	add_button(bottom, _("Help"), "macro-help", G_CALLBACK(on_help), 0);
	add_button(bottom, _("Cancel"), "macro-cancel", G_CALLBACK(on_cancel), 0);
	add_button(bottom, _("OK"), "macro-ok", G_CALLBACK(on_ok), 0);
	gtk_box_append(GTK_BOX(vbox), switcher);
	gtk_box_append(GTK_BOX(vbox), stack);
	gtk_box_append(GTK_BOX(vbox), editor->message);
	gtk_box_append(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
	gtk_box_append(GTK_BOX(vbox), bottom);
	gtk_window_set_child(GTK_WINDOW(editor->window), vbox);
	gtk_window_present(GTK_WINDOW(editor->window));
}

/* Comparaison de pointeurs sûre : macros_replace_set crée le nouveau jeu avant de libérer l'ancien et appelle ce crochet à chaque
 * remplacement ; editor->set n'est jamais déréférencé une fois différent du jeu courant. */
void gt_macro_editor_macros_changed(void)
{
	if(editor != NULL && macros_get_set() != editor->set)
		gtk_window_destroy(GTK_WINDOW(editor->window));	/* sans close-request : rien n'est appliqué ni enregistré */
}

static void on_macros_action(GSimpleAction *action, GVariant *parameter, gpointer data)
{
	gt_macro_editor_open(gt_app_get_main_window());
}

void gt_macro_editor_install_action(GtkApplication *app)
{
	GSimpleAction *action = g_simple_action_new("macros", NULL);

	g_signal_connect(action, "activate", G_CALLBACK(on_macros_action), NULL);
	g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(action));
	g_object_unref(action);
}
