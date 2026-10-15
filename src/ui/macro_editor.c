/* macro_editor.c — fenêtre « Configure Macros » (interface GTK 4, spec 6a §6.5) : non modale, une par session (6b, spec §3.2-12), pages
 * Macros et Lists. Les lignes sont des copies (GtMacroRow, GtMacroListRow) : rien n'est appliqué avant OK ou la fermeture par la barre de
 * titre ; Cancel jette tout. Un jeu remplacé pendant l'édition, ou la session fermée, ferme la fenêtre sans rien enregistrer. La fenêtre
 * n'est pas rattachée à l'application : ses raccourcis (Ctrl+Maj+Q…) ne s'y appliquent pas, la capture reçoit toutes les touches.
 * L'état est attaché à la fenêtre ; les rappels le retrouvent par la fenêtre racine de leur widget. */

#include <config.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "app.h"
#include "dialogs.h"
#include "macro_accel.h"
#include "macro_arg.h"
#include "macro_arg_widget.h"
#include "macro_editor.h"
#include "macro_preview.h"
#include "macro_row.h"
#include "macros.h"
#include "macros_format.h"
#include "macros_save.h"

enum { PAGE_MACROS, PAGE_LISTS };

#define EDITOR_KEY "gt-macro-editor"	/* sur la fenêtre : MacroEditor, libéré avec elle */
#define SESSION_EDITOR_KEY "gt-macro-editor-window"	/* sur la session : sa fenêtre ouverte (non possédée), retirée à la destruction */

typedef struct
{
	GtkWidget         *window;	/* « macro-editor » */
	GWeakRef           session;	/* session éditée (spec §3.2-12 : référence faible) */
	GtMacroSet        *set;	/* jeu édité ; jamais déréférencé s'il n'est plus le jeu courant */
	GListStore        *rows;	/* GtMacroRow */
	GListStore        *list_rows;	/* GtMacroListRow */
	GtkSelectionModel *selection;	/* GtkMultiSelection sur rows (référence aussi tenue par la vue) */
	GtkSelectionModel *list_selection;	/* GtkMultiSelection sur list_rows */
	GtkWidget         *message;	/* « macro-editor-error » : consigne de capture ou refus */
	GtMacroRow        *capture_row;	/* ligne qui attend son raccourci ; NULL hors capture */
	GtkWidget         *defaults;	/* « macro-defaults » (GtkGrid, pointeur faible) : table des arguments de la seule ligne sélectionnée */
	GtkWidget         *defaults_scroll;	/* défilement de la table (pointeur faible), masqué sans argument */
	gint               defaults_n;	/* nombre d'arguments affichés */
	GtMacroRow        *defaults_row;	/* ligne affichée (référence), dont notify::action refait la table ; NULL si aucune */
	gulong             defaults_notify_id;
	gboolean           defaults_building;	/* reconstruction de la table : ses champs n'écrivent pas dans la ligne */
	GtkWidget         *preview_box;	/* « macro-preview-box » (pointeurs faibles) : aperçu de la ligne envoyée */
	GtkWidget         *preview;	/* « macro-preview » */
	GtkWidget         *preview_bytes;	/* « macro-preview-bytes » */
	GtkWidget         *template_info;	/* « macro-template-info » : modèle lié, caché si l'onglet n'est pas lié */
	gboolean           asking;	/* question du modèle en cours (spec modèles §2.7) : OK sans effet, fermeture refusée */
} MacroEditor;

/* Question posée sur un onglet lié : la réponse retrouve l'éditeur par sa fenêtre (jamais le MacroEditor, libéré avec elle) et n'applique rien
 * si la génération des macros a changé entre-temps */
typedef struct
{
	GWeakRef         window;
	guint64          generation;
	GtMacrosEditKind kind;	/* DEFAULTS, STRUCTURE ; NONE : question de conflit du modèle */
} TemplateQuestion;

/* Cellule d'une colonne : propriété de la ligne, nom du widget, sélection de la vue */
typedef struct
{
	const gchar       *prop;
	const gchar       *cell_name;
	GtkSelectionModel *selection;
} CellSpec;

/* xgettext:no-c-format */
static const gchar *help_text = N_(
	"Action: the data sent on the port. Plain text, plus the escapes \\r (carriage return), \\n (line feed), \\t (tab), \\a, \\b, \\f, \\v, "
	"\\\\ (backslash) and \\xHH (one byte in hexadecimal, e.g. \\x0D).\n\n"
	"Arguments: each specifier gets a field in the macro panel; the value typed there is used for sending but never saved; set default values, limits and character sets in the table of arguments below the list. %d %i (integer), %u %o %x %X "
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

static void on_defaults_items_changed(GListModel *model, guint position, guint removed, guint added, gpointer data);
static void on_defaults_selection_changed(GtkSelectionModel *model, guint position, guint n_items, gpointer data);
static void defaults_unwatch(MacroEditor *editor);

/* État de l'éditeur dont w fait partie (fenêtre racine) ; NULL si la fenêtre est en destruction */
static MacroEditor *editor_of(GtkWidget *w)
{
	GtkRoot *root = w != NULL ? gtk_widget_get_root(w) : NULL;

	return root != NULL ? g_object_get_data(G_OBJECT(root), EDITOR_KEY) : NULL;
}

/* Macros de la session éditée ; NULL si elle n'existe plus ou est arrêtée */
static GtMacros *editor_macros(MacroEditor *editor)
{
	GtSession *s = g_weak_ref_get(&editor->session);
	GtMacros *m = s != NULL && !gt_session_is_shut_down(s) ? gt_session_get_macros(s) : NULL;

	g_clear_object(&s);	/* la session est tenue par le gestionnaire tant qu'elle n'est pas arrêtée */
	return m;
}

static void set_message(MacroEditor *editor, const gchar *text, gboolean error)
{
	gtk_label_set_text(GTK_LABEL(editor->message), text != NULL ? text : "");
	gtk_widget_set_visible(editor->message, text != NULL && text[0] != '\0');
	if(error)
		gtk_widget_add_css_class(editor->message, "error");
	else
		gtk_widget_remove_css_class(editor->message, "error");
}

static void stop_capture(MacroEditor *editor)
{
	if(editor->capture_row == NULL)
		return;
	g_clear_object(&editor->capture_row);
	set_message(editor, NULL, FALSE);
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

static GListStore *page_store(MacroEditor *editor, gint page)
{
	return page == PAGE_MACROS ? editor->rows : editor->list_rows;
}

static GtkSelectionModel *page_selection(MacroEditor *editor, gint page)
{
	return page == PAGE_MACROS ? editor->selection : editor->list_selection;
}

/* --- Cellules --- */

/* Passer une cellule en édition sélectionne sa ligne seule et annule une capture */
static void on_cell_editing(GObject *label, GParamSpec *pspec, gpointer cell)
{
	CellSpec *spec = g_object_get_data(label, "spec");
	MacroEditor *editor = editor_of(GTK_WIDGET(label));

	if(editor == NULL || !gtk_editable_label_get_editing(GTK_EDITABLE_LABEL(label)))
		return;
	stop_capture(editor);
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

/* Largeur minimale d'une colonne : celle de son titre (marges de l'en-tête et poignée de redimensionnement comprises) */
#define COLUMN_TITLE_PADDING 28

static gint column_title_width(GtkColumnView *view, const gchar *title)
{
	PangoLayout *layout = gtk_widget_create_pango_layout(GTK_WIDGET(view), title);
	gint w = 0;

	pango_layout_get_pixel_size(layout, &w, NULL);
	g_object_unref(layout);
	return w + COLUMN_TITLE_PADDING;
}

/* Redimensionnement à la souris : jamais plus étroit que le titre (la nouvelle valeur ne renotifie pas au-delà du minimum) */
static void on_column_fixed_width(GtkColumnViewColumn *column, GParamSpec *pspec, gpointer data)
{
	gint min = GPOINTER_TO_INT(data);
	gint w = gtk_column_view_column_get_fixed_width(column);

	if(w >= 0 && w < min)
		gtk_column_view_column_set_fixed_width(column, min);
}

static void add_column(GtkColumnView *view, GtkSelectionModel *selection, const gchar *title, const gchar *prop, const gchar *cell_name, gboolean editable,
           gint width)
{
	GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
	CellSpec *spec = g_new0(CellSpec, 1);
	GtkColumnViewColumn *column;
	gint min;

	spec->prop = prop;
	spec->cell_name = cell_name;
	spec->selection = selection;
	g_object_set_data_full(G_OBJECT(factory), "spec", spec, g_free);
	g_signal_connect(factory, "setup", G_CALLBACK(editable ? editable_setup : shortcut_setup), spec);
	g_signal_connect(factory, "bind", G_CALLBACK(editable ? editable_bind : shortcut_bind), spec);
	g_signal_connect(factory, "unbind", G_CALLBACK(cell_unbind), spec);
	column = gtk_column_view_column_new(title, factory);	/* prend la fabrique */
	gtk_column_view_column_set_resizable(column, TRUE);
	/* Largeur fixe de départ, seule la dernière colonne (width 0) prend la place restante : avec plusieurs colonnes « expand », GTK répartit
	 * la largeur gagnée ou perdue par un redimensionnement entre elles, et les deux bords de la colonne tirée bougent à la fois */
	min = column_title_width(view, title);
	if(width > 0)
		gtk_column_view_column_set_fixed_width(column, MAX(width, min));
	gtk_column_view_column_set_expand(column, width == 0);
	g_signal_connect(column, "notify::fixed-width", G_CALLBACK(on_column_fixed_width), GINT_TO_POINTER(min));
	/* Aucun trieur : un clic sur l'en-tête ne trie pas, l'ordre est celui de l'utilisateur (§6.5) */
	gtk_column_view_append_column(view, column);
	g_object_unref(column);
}

/* --- Boutons des pages --- */

static void on_add(GtkButton *button, gpointer data)
{
	MacroEditor *editor = editor_of(GTK_WIDGET(button));
	gint page = GPOINTER_TO_INT(data);
	GListStore *store;
	guint n = 0, *pos, at;
	gpointer row;

	if(editor == NULL)
		return;
	store = page_store(editor, page);
	pos = selected_positions(page_selection(editor, page), &n);
	at = gt_macro_rows_insert_position(pos, n, g_list_model_get_n_items(G_LIST_MODEL(store)));
	row = page == PAGE_MACROS ? (gpointer)gt_macro_row_new(0, "", "", "", "") : (gpointer)gt_macro_list_row_new("NewList", "display", "value");
	stop_capture(editor);
	g_list_store_insert(store, at, row);
	g_object_unref(row);
	gtk_selection_model_select_item(page_selection(editor, page), at, TRUE);
	g_free(pos);
}

static void on_delete(GtkButton *button, gpointer data)
{
	MacroEditor *editor = editor_of(GTK_WIDGET(button));
	gint page = GPOINTER_TO_INT(data);
	guint n = 0, *pos;

	if(editor == NULL)
		return;
	pos = selected_positions(page_selection(editor, page), &n);
	stop_capture(editor);
	gt_macro_rows_remove(page_store(editor, page), pos, n);
	g_free(pos);
}

static void move_selected(MacroEditor *editor, gint page, gint delta)
{
	guint n = 0, *pos = selected_positions(page_selection(editor, page), &n);

	stop_capture(editor);
	if(gt_macro_rows_move(page_store(editor, page), pos, n, delta))
		reselect(page_selection(editor, page), pos, n);	/* la sélection suit les lignes déplacées */
	g_free(pos);
}

static void on_up(GtkButton *button, gpointer data)
{
	MacroEditor *editor = editor_of(GTK_WIDGET(button));

	if(editor == NULL)
		return;
	move_selected(editor, GPOINTER_TO_INT(data), -1);
}

static void on_down(GtkButton *button, gpointer data)
{
	MacroEditor *editor = editor_of(GTK_WIDGET(button));

	if(editor == NULL)
		return;
	move_selected(editor, GPOINTER_TO_INT(data), +1);
}

static void on_unshortcut(GtkButton *button, gpointer data)
{
	MacroEditor *editor = editor_of(GTK_WIDGET(button));
	guint n = 0, *pos;

	if(editor == NULL)
		return;
	pos = selected_positions(editor->selection, &n);
	stop_capture(editor);
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
	MacroEditor *editor = editor_of(GTK_WIDGET(button));
	guint n = 0, *pos;

	if(editor == NULL)
		return;
	pos = selected_positions(editor->selection, &n);
	stop_capture(editor);
	if(n == 0)
		set_message(editor, _("Select a macro first."), TRUE);
	else
	{
		gchar *text;

		editor->capture_row = g_list_model_get_item(G_LIST_MODEL(editor->rows), pos[0]);
		text = g_strdup_printf(_("Press the new shortcut for \"%s\" (Escape cancels)."), gt_macro_row_get_label(editor->capture_row));
		set_message(editor, text, FALSE);
		g_free(text);
	}
	g_free(pos);
}

/* Raccourci déjà pris par une action de l'application ou par une autre ligne : message de refus et TRUE */
static gboolean refuse_if_taken(MacroEditor *editor, const gchar *accel)
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
		set_message(editor, text, TRUE);
	g_free(text);
	g_array_unref(owners);	/* owner pointait dans ce tableau */
	return taken;
}

/* Capture : toutes les touches traitées sont consommées (phase de capture de la fenêtre) */
static gboolean on_key_pressed(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer data)
{
	MacroEditor *editor = editor_of(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller)));
	gchar *accel = NULL;

	if(editor == NULL || editor->capture_row == NULL)
		return GDK_EVENT_PROPAGATE;
	switch(gt_macro_accel_from_key(keyval, state, &accel))
	{
	case GT_ACCEL_KEY_MODIFIER:
		return GDK_EVENT_PROPAGATE;
	case GT_ACCEL_KEY_CANCEL:
		stop_capture(editor);
		return GDK_EVENT_STOP;
	case GT_ACCEL_KEY_NEEDS_MODIFIER:
		set_message(editor, _("A shortcut needs Ctrl, Alt or Super, unless it is a function key (F1 to F35). Press other keys, or Escape to cancel."), TRUE);
		return GDK_EVENT_STOP;
	case GT_ACCEL_KEY_ACCEPTED:
		break;
	}
	if(!refuse_if_taken(editor, accel))
	{
		gt_macro_row_set_shortcut(editor->capture_row, accel);
		stop_capture(editor);
	}
	g_free(accel);
	return GDK_EVENT_STOP;
}

static void on_help(GtkButton *button, gpointer data)
{
	MacroEditor *editor = editor_of(GTK_WIDGET(button));

	if(editor == NULL)
		return;
	stop_capture(editor);	/* règle : tout autre bouton annule la capture en cours */
	gt_ui_message(GTK_WINDOW(editor->window), GT_APP_MSG_INFO, _(help_text));
}

/* Ferme la fenêtre si elle n'est pas déjà en destruction (on_destroy retire l'état) */
static void close_editor_window(GtkWidget *window)
{
	if(g_object_get_data(G_OBJECT(window), EDITOR_KEY) != NULL)
		gtk_window_destroy(GTK_WINDOW(window));	/* sans close-request */
}

/* Lignes de l'éditeur, dans l'ordre affiché, avec leur identifiant (arguments et polling suivent l'identifiant, M16) ; listes à libérer */
static GtMacroEdit *editor_edits(MacroEditor *editor, guint *n, GtMacroLists **lists)
{
	*lists = gt_macro_list_rows_to_lists(G_LIST_MODEL(editor->list_rows));
	return gt_macro_rows_to_edits(G_LIST_MODEL(editor->rows), n);
}

/* Chemin unique d'application (lignes relues maintenant) ; le jeu reste le même pointeur : ce module ne se ferme pas par le crochet */
static void commit_rows(MacroEditor *editor, GtMacros *macros)
{
	GtMacroLists *lists;
	guint n = 0;
	GtMacroEdit *edits = editor_edits(editor, &n, &lists);

	gt_macros_commit_edits(macros, edits, n, lists);
	g_free(edits);
	gt_macro_lists_free(lists);
}

static void ask_template(MacroEditor *editor, GtMacros *macros, GtMacrosEditKind kind);

/* « Save to template » (§2.8), l'éditeur restant ouvert jusqu'à SAVED ; TRUE si la fenêtre doit se fermer */
static gboolean save_to_template(MacroEditor *editor, GtMacros *macros, gboolean force)
{
	GtMacroLists *lists;
	guint n = 0;
	GtMacroEdit *edits = editor_edits(editor, &n, &lists);
	GtMacrosSaveResult r = gt_macros_save_template_edits(macros, edits, n, lists, force);

	g_free(edits);
	gt_macro_lists_free(lists);
	if(r == GT_MACROS_SAVED)
	{
		GtSession *s = g_weak_ref_get(&editor->session);

		gt_app_refresh_template_siblings(s);
		g_clear_object(&s);
		return TRUE;
	}
	if(r == GT_MACROS_CONFLICT)
		ask_template(editor, macros, GT_MACROS_EDIT_NONE);
	return FALSE;	/* FAILED : message déjà affiché, rien n'est perdu */
}

/* Réponse à une question du modèle */
static void on_template_choice(gint choice, gpointer data)
{
	TemplateQuestion *q = data;
	GtkWidget *window = g_weak_ref_get(&q->window);
	MacroEditor *editor = window != NULL ? g_object_get_data(G_OBJECT(window), EDITOR_KEY) : NULL;
	GtMacros *macros = editor != NULL ? editor_macros(editor) : NULL;
	gboolean close = FALSE;

	if(editor != NULL)
		editor->asking = FALSE;
	/* Fenêtre détruite, session fermée, jeu remplacé ou changé entre-temps : rien n'est appliqué, la fenêtre (si elle vit) reste ouverte */
	if(macros != NULL && gt_macros_get_set(macros) == editor->set && gt_macros_get_generation(macros) == q->generation && choice >= 0)
	{
		if(q->kind == GT_MACROS_EDIT_NONE)	/* conflit : Overwrite, Reload, Cancel */
		{
			if(choice == 0)
				close = save_to_template(editor, macros, TRUE);
			else if(choice == 1)
				close = gt_macros_reload_template(macros);	/* les modifications sont abandonnées ; échec : message, l'éditeur reste */
		}
		else if(q->kind == GT_MACROS_EDIT_DEFAULTS)	/* Keep in this project, Save to template, Cancel */
		{
			if(choice == 0)
			{
				commit_rows(editor, macros);
				close = TRUE;
			}
			else if(choice == 1)
				close = save_to_template(editor, macros, FALSE);
		}
		else	/* STRUCTURE : Save to template, Detach and keep a copy, Cancel */
		{
			if(choice == 0)
				close = save_to_template(editor, macros, FALSE);
			else if(choice == 1)
			{
				gt_macros_detach_template(macros);
				commit_rows(editor, macros);
				close = TRUE;
			}
		}
	}
	if(close)
		close_editor_window(window);
	g_clear_object(&window);
	g_weak_ref_clear(&q->window);
	g_free(q);
}

/* Pose la question du modèle (kind DEFAULTS, STRUCTURE) ou celle du conflit (NONE) ; indicateur « question en cours » levé */
static void ask_template(MacroEditor *editor, GtMacros *macros, GtMacrosEditKind kind)
{
	TemplateQuestion *q = g_new0(TemplateQuestion, 1);
	const gchar *path = gt_macros_get_template(macros);

	g_weak_ref_init(&q->window, editor->window);
	q->generation = gt_macros_get_generation(macros);
	q->kind = kind;
	editor->asking = TRUE;
	if(kind == GT_MACROS_EDIT_NONE)
	{
		const gchar *const labels[] = { _("Overwrite"), _("Reload"), _("Cancel"), NULL };

		/* Cancel par défaut, Overwrite destructeur : Entrée n'écrase jamais le travail d'un autre */
		gt_ui_choose(GTK_WINDOW(editor->window), _("The macros template changed on disk."), path, labels, 2, 0, on_template_choice, q);
	}
	else
	{
		gchar *base = g_path_get_basename(path);
		gchar *text = g_strdup_printf(_("These macros come from the template %s"), base);

		if(kind == GT_MACROS_EDIT_DEFAULTS)
		{
			const gchar *const labels[] = { _("Keep in this project"), _("Save to template"), _("Cancel"), NULL };

			gt_ui_choose(GTK_WINDOW(editor->window), text,
			             _("Keep the new default values in this project only, or save them to the template, for every project that uses it?"),
			             labels, 0, -1, on_template_choice, q);
		}
		else
		{
			const gchar *const labels[] = { _("Save to template"), _("Detach and keep a copy"), _("Cancel"), NULL };

			gt_ui_choose(GTK_WINDOW(editor->window), text,
			             _("These changes can only be saved to the template (every project that uses it), or the tab can be detached from the "
			               "template and keep its own copy."),
			             labels, 2, -1, on_template_choice, q);
		}
		g_free(text);
		g_free(base);
	}
}

/* OK (ou fermeture par la barre de titre) ; TRUE : la fenêtre peut se fermer maintenant ; FALSE : une question est posée, sa réponse fermera */
static gboolean apply_edits(MacroEditor *editor)
{
	GtMacros *macros = editor_macros(editor);
	GtSession *s;
	GtMacroEdit *edits;
	GtMacroLists *lists;
	GtMacrosEditKind kind;
	guint n = 0;

	/* Session fermée, ou jeu remplacé : ces lignes ne s'appliquent jamais (au nouveau jeu ni à une session arrêtée) */
	if(macros == NULL || gt_macros_get_set(macros) != editor->set)
		return TRUE;
	if(gt_macros_get_template(macros) == NULL)
	{
		commit_rows(editor, macros);	/* editor->set est le jeu courant */
		s = g_weak_ref_get(&editor->session);
		gt_ui_macros_save(gtk_window_get_transient_for(GTK_WINDOW(editor->window)), s, NULL);	/* non NULL : editor_macros l'a vue vivante */
		g_object_unref(s);
		return TRUE;
	}
	/* Onglet lié (spec modèles §2.7) : rien n'est appliqué avant la réponse */
	edits = editor_edits(editor, &n, &lists);
	kind = gt_macros_classify_edits(macros, edits, n, lists);
	g_free(edits);
	gt_macro_lists_free(lists);
	if(kind == GT_MACROS_EDIT_NONE)
		return TRUE;
	ask_template(editor, macros, kind);
	return FALSE;
}

static void on_ok(GtkButton *button, gpointer data)
{
	MacroEditor *editor = editor_of(GTK_WIDGET(button));

	if(editor == NULL || editor->asking)
		return;
	if(apply_edits(editor))
		gtk_window_destroy(GTK_WINDOW(editor->window));	/* sans close-request */
}

static void on_cancel(GtkButton *button, gpointer data)
{
	MacroEditor *editor = editor_of(GTK_WIDGET(button));

	if(editor == NULL)
		return;
	gtk_window_destroy(GTK_WINDOW(editor->window));
}

/* Fermer par la barre de titre vaut OK (comportement conservé), question comprise */
static gboolean on_close_request(GtkWindow *window, gpointer data)
{
	MacroEditor *editor = g_object_get_data(G_OBJECT(window), EDITOR_KEY);

	if(editor == NULL)
		return FALSE;
	if(editor->asking)
		return TRUE;
	return !apply_edits(editor);	/* question posée : fermeture refusée, la réponse fermera */
}

/* Destruction : la session ne désigne plus cette fenêtre ; l'état est retiré de la fenêtre (les rappels encore branchés le trouvent NULL) */
static void on_destroy(GtkWidget *window, gpointer data)
{
	MacroEditor *editor = g_object_get_data(G_OBJECT(window), EDITOR_KEY);
	GtSession *s;

	if(editor == NULL)
		return;
	/* Les modèles peuvent survivre à l'état : plus aucun rappel de la zone des valeurs par défaut */
	g_signal_handlers_disconnect_by_func(editor->rows, on_defaults_items_changed, editor);
	g_signal_handlers_disconnect_by_func(editor->list_rows, on_defaults_items_changed, editor);
	g_signal_handlers_disconnect_by_func(editor->selection, on_defaults_selection_changed, editor);
	s = g_weak_ref_get(&editor->session);
	if(s != NULL && g_object_get_data(G_OBJECT(s), SESSION_EDITOR_KEY) == window)
		g_object_set_data(G_OBJECT(s), SESSION_EDITOR_KEY, NULL);
	g_clear_object(&s);
	g_object_set_data(G_OBJECT(window), EDITOR_KEY, NULL);	/* editor_free */
}

static void editor_free(gpointer data)
{
	MacroEditor *editor = data;

	g_clear_object(&editor->capture_row);
	defaults_unwatch(editor);
	for(GtkWidget **const *w = (GtkWidget **const[]){ &editor->defaults, &editor->defaults_scroll, &editor->preview_box, &editor->preview,
	                                                  &editor->preview_bytes, &editor->template_info, NULL }; *w != NULL; w++)
		if(**w != NULL)
			g_object_remove_weak_pointer(G_OBJECT(**w), (gpointer *)*w);
	g_clear_object(&editor->selection);
	g_clear_object(&editor->rows);
	g_clear_object(&editor->list_rows);
	g_weak_ref_clear(&editor->session);
	g_free(editor);
}

/* Session fermée pendant l'édition (signal « closed ») : fenêtre fermée sans rien appliquer (spec §3.2-12) */
static void on_session_closed(GtSession *s, gpointer window)
{
	gtk_window_destroy(GTK_WINDOW(window));	/* sans close-request */
}

/* --- Construction --- */

/* Libellé « Template: … » (spec modèles §2.10) : chemin complet du modèle lié, caché si l'onglet n'est pas lié */
static void template_info_update(MacroEditor *editor, GtMacros *macros)
{
	const gchar *path = macros != NULL ? gt_macros_get_template(macros) : NULL;

	if(editor->template_info == NULL)
		return;
	if(path != NULL)
	{
		gchar *text = g_strdup_printf(_("Template: %s"), path);

		gtk_label_set_text(GTK_LABEL(editor->template_info), text);
		g_free(text);
	}
	gtk_widget_set_visible(editor->template_info, path != NULL);
}

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

/* --- Table des arguments (spec des arguments §2.11) : sous la liste, pour la seule ligne sélectionnée ; aperçu de la ligne envoyée (§2.10) --- */

/* Colonnes de la table ; la ligne 0 porte les en-têtes, l'argument k la ligne k + 1 */
enum { COL_RANK, COL_FORMAT, COL_TYPE, COL_DEFAULT, COL_MIN, COL_MAX, COL_CHARSET, N_COLS };

/* Entrées de la liste déroulante des jeux de caractères, dans l'ordre ; la dernière est la classe personnalisée */
static const gchar *const charset_values[] = { "", "printable", "ascii", "latin1" };
#define CHARSET_CLASS G_N_ELEMENTS(charset_values)

static void field_set_error(GtkWidget *w, gboolean error)
{
	if(error)
		gtk_widget_add_css_class(w, "error");
	else
		gtk_widget_remove_css_class(w, "error");
}

static GtkWidget *table_cell(MacroEditor *editor, gint col, gint k)
{
	return gtk_grid_get_child_at(GTK_GRID(editor->defaults), col, k + 1);
}

/* Valeur du jeu de caractères de l'argument k ("" sans cellule) ; *valid FALSE pour une classe vide ou invalide. g_free */
static gchar *charset_of(MacroEditor *editor, gint k, gboolean *valid)
{
	GtkWidget *cell = table_cell(editor, COL_CHARSET, k);
	GtkWidget *dropdown, *cls;
	guint sel;

	*valid = TRUE;
	if(cell == NULL)
		return g_strdup("");
	dropdown = g_object_get_data(G_OBJECT(cell), "dropdown");
	cls = g_object_get_data(G_OBJECT(cell), "class");
	sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown));
	if(sel < CHARSET_CLASS)
	{
		field_set_error(cls, FALSE);
		return g_strdup(charset_values[sel]);
	}
	*valid = gt_macro_class_valid(gtk_editable_get_text(GTK_EDITABLE(cls)));
	field_set_error(cls, !*valid);
	return g_strconcat("class:", gtk_editable_get_text(GTK_EDITABLE(cls)), NULL);
}

/* Vecteur de n chaînes, NULL s'il est entièrement vide (règle de GtMacro) ; prend values */
static gchar **vector_or_null(gchar **values)
{
	for(gchar **p = values; *p != NULL; p++)
		if((*p)[0] != '\0')
			return values;
	g_strfreev(values);
	return NULL;
}

/* Vecteur de la ligne s'il a exactement n éléments, sinon NULL (abandonné par le jeu, comme args) */
static const gchar *const *row_vector(const gchar *const *v, gint n)
{
	return v != NULL && (gint)g_strv_length((gchar **)v) == n ? v : NULL;
}

static const gchar *nth(const gchar *const *v, gint k)
{
	return v != NULL ? v[k] : "";
}

/* Vérifie les champs de la table (classe « error ») et, si write, écrit dans la ligne les vecteurs dont tous les champs sont valides :
 * limites (min, max, jeux), puis valeurs par défaut vérifiées avec les limites retenues de la ligne. Tout vide → NULL. */
static void defaults_commit(MacroEditor *editor, gboolean write)
{
	gint n = editor->defaults_n;
	gchar **min, **max, **cs, **def;
	gboolean min_ok = TRUE, max_ok = TRUE, cs_ok = TRUE, def_ok = TRUE, any_def = FALSE;
	const gchar *const *rmin, *const *rmax, *const *rcs;

	if(editor->defaults == NULL || editor->defaults_building || editor->defaults_row == NULL || n == 0)
		return;
	min = g_new0(gchar *, n + 1);
	max = g_new0(gchar *, n + 1);
	cs = g_new0(gchar *, n + 1);
	def = g_new0(gchar *, n + 1);
	for(gint k = 0; k < n; k++)
	{
		GtkWidget *wmin = table_cell(editor, COL_MIN, k), *wmax = table_cell(editor, COL_MAX, k);
		const gchar *type = g_object_get_data(G_OBJECT(table_cell(editor, COL_DEFAULT, k)), "arg-type");
		gboolean vmin, vmax, vcs;

		min[k] = g_strdup(wmin != NULL ? gtk_editable_get_text(GTK_EDITABLE(wmin)) : "");
		max[k] = g_strdup(wmax != NULL ? gtk_editable_get_text(GTK_EDITABLE(wmax)) : "");
		cs[k] = charset_of(editor, k, &vcs);
		vmin = gt_macro_arg_limit_valid(type, min[k]);
		vmax = gt_macro_arg_limit_valid(type, max[k]);
		if(vmin && vmax && !gt_macro_arg_limits_valid(type, min[k], max[k], ""))
			vmin = vmax = FALSE;	/* min > max : les deux en rouge */
		if(wmin != NULL)
			field_set_error(wmin, !vmin);
		if(wmax != NULL)
			field_set_error(wmax, !vmax);
		min_ok &= vmin;
		max_ok &= vmax;
		cs_ok &= vcs;
	}
	min = vector_or_null(min);
	max = vector_or_null(max);
	cs = vector_or_null(cs);
	if(write)
	{
		gchar **old_min = g_strdupv((gchar **)gt_macro_row_get_arg_min(editor->defaults_row));
		gchar **old_max = g_strdupv((gchar **)gt_macro_row_get_arg_max(editor->defaults_row));
		gchar **old_cs = g_strdupv((gchar **)gt_macro_row_get_arg_charset(editor->defaults_row));

		gt_macro_row_set_limits(editor->defaults_row, (const gchar *const *)(min_ok ? min : old_min), (const gchar *const *)(max_ok ? max : old_max),
		                        (const gchar *const *)(cs_ok ? cs : old_cs));
		g_strfreev(old_min);
		g_strfreev(old_max);
		g_strfreev(old_cs);
		rmin = row_vector(gt_macro_row_get_arg_min(editor->defaults_row), n);
		rmax = row_vector(gt_macro_row_get_arg_max(editor->defaults_row), n);
		rcs = row_vector(gt_macro_row_get_arg_charset(editor->defaults_row), n);
	}
	else
	{
		rmin = min_ok ? (const gchar *const *)min : row_vector(gt_macro_row_get_arg_min(editor->defaults_row), n);
		rmax = max_ok ? (const gchar *const *)max : row_vector(gt_macro_row_get_arg_max(editor->defaults_row), n);
		rcs = cs_ok ? (const gchar *const *)cs : row_vector(gt_macro_row_get_arg_charset(editor->defaults_row), n);
	}
	for(gint k = 0; k < n; k++)
	{
		def[k] = gt_macro_arg_widget_get_value(table_cell(editor, COL_DEFAULT, k));
		any_def |= def[k][0] != '\0';
	}
	for(gint k = 0; k < n; k++)
	{
		GtkWidget *w = table_cell(editor, COL_DEFAULT, k);

		/* bulle : description de l'argument, raison du refus ; un défaut entièrement vide n'est pas vérifié (aucune valeur par défaut) */
		gt_macro_arg_widget_set_limits(w, nth(rmin, k), nth(rmax, k), nth(rcs, k), NULL);
		if(!any_def)
			field_set_error(w, FALSE);
		else
			def_ok &= gt_macro_arg_widget_check(w, NULL);
	}
	if(write && def_ok)
		gt_macro_row_set_args(editor->defaults_row, any_def ? (const gchar *const *)def : NULL);
	g_strfreev(min);
	g_strfreev(max);
	g_strfreev(cs);
	g_strfreev(def);
}

static void on_table_entry_changed(GtkEditable *editable, gpointer data)
{
	MacroEditor *editor = editor_of(GTK_WIDGET(editable));

	if(editor != NULL)
		defaults_commit(editor, TRUE);
}

static void on_default_dropdown_selected(GObject *dropdown, GParamSpec *pspec, gpointer data)
{
	MacroEditor *editor = editor_of(GTK_WIDGET(dropdown));

	if(editor != NULL)
		defaults_commit(editor, TRUE);
}

/* Jeu de caractères choisi : l'entrée de classe n'est visible que pour « Custom class » */
static void on_charset_selected(GObject *dropdown, GParamSpec *pspec, gpointer cls)
{
	MacroEditor *editor = editor_of(GTK_WIDGET(dropdown));

	gtk_widget_set_visible(cls, gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown)) == CHARSET_CLASS);
	if(editor != NULL)
		defaults_commit(editor, TRUE);
}

static void defaults_clear(MacroEditor *editor)
{
	GtkWidget *child;

	while((child = gtk_widget_get_first_child(editor->defaults)) != NULL)
		gtk_grid_remove(GTK_GRID(editor->defaults), child);
	editor->defaults_n = 0;
}

static void defaults_unwatch(MacroEditor *editor)
{
	if(editor->defaults_row != NULL)
	{
		g_signal_handler_disconnect(editor->defaults_row, editor->defaults_notify_id);
		g_clear_object(&editor->defaults_row);
		editor->defaults_notify_id = 0;
	}
}

/* --- Aperçu --- */

static const gchar *preview_color(const GtPreviewRun *run, const gboolean *invalid, gint n_invalid)
{
	switch(run->kind)
	{
	case GT_PREVIEW_ARG:
		return run->arg >= 0 && run->arg < n_invalid && invalid[run->arg] ? "#e01b24" : "#1c71d8";
	case GT_PREVIEW_SPECIAL:
		return "#9141ac";
	default:
		return NULL;
	}
}

/* Aperçu de la ligne affichée : jeu temporaire d'une macro (action, défauts, limites de la ligne, listes de la page Lists) ; masqué hors
 * ligne seule envoyable. Arguments en bleu, en rouge s'ils ne passent pas leurs limites ; caractères spéciaux en violet. */
static void preview_update(MacroEditor *editor)
{
	GtMacroRow *row = editor->defaults_row;
	GtMacroSet *set;
	const GtMacro *macro;
	const gchar *action;
	macro_arg_info_t *infos;
	gboolean *invalid;
	GArray *segments = NULL;
	GPtrArray *runs;
	GBytes *bytes;
	GString *markup;
	const guint8 *data;
	gsize len;
	gchar *hex, *text;
	gint n;
	guint id;

	if(editor->preview_box == NULL || editor->preview == NULL || editor->preview_bytes == NULL)
		return;
	action = row != NULL ? gt_macro_row_get_action(row) : "";
	if(row == NULL || action[0] == '\0' || gt_macro_action_is_separator(action, NULL))
	{
		gtk_widget_set_visible(editor->preview_box, FALSE);
		return;
	}
	set = gt_macro_set_new();
	gt_macro_set_take_lists(set, gt_macro_list_rows_to_lists(G_LIST_MODEL(editor->list_rows)));
	id = gt_macro_set_append(set, gt_macro_row_get_label(row), "", action, "");
	n = macro_count_format_args(gt_macro_set_get_lists(set), action);
	if(row_vector(gt_macro_row_get_args(row), n) != NULL)
		gt_macro_set_set_args(set, id, gt_macro_row_get_args(row), n);
	gt_macro_set_set_limits(set, id, row_vector(gt_macro_row_get_arg_min(row), n), row_vector(gt_macro_row_get_arg_max(row), n),
	                        row_vector(gt_macro_row_get_arg_charset(row), n), n);
	macro = gt_macro_set_find(set, id);
	if(!gt_macro_is_sendable(macro))
	{
		gt_macro_set_free(set);
		gtk_widget_set_visible(editor->preview_box, FALSE);
		return;
	}
	/* Validité de chaque argument retenu, avec les limites retenues (listes : toujours valides) */
	infos = macro_get_arg_infos(gt_macro_set_get_lists(set), action, &n);
	invalid = g_new0(gboolean, MAX(n, 1));
	for(gint k = 0; macro->args != NULL && k < n; k++)
		invalid[k] = g_strcmp0(infos[k].type, "l") != 0
		             && gt_macro_arg_check(infos[k].type, macro->args[k], nth((const gchar *const *)macro->arg_min, k),
		                                   nth((const gchar *const *)macro->arg_max, k), nth((const gchar *const *)macro->arg_charset, k),
		                                   NULL) != GT_MACRO_ARG_OK;
	bytes = gt_macro_expand_segments(set, macro, NULL, -1, &segments);
	data = g_bytes_get_data(bytes, &len);
	runs = gt_macro_preview_runs(data, len, segments);
	markup = g_string_new(NULL);
	for(guint i = 0; i < runs->len; i++)
	{
		const GtPreviewRun *run = g_ptr_array_index(runs, i);
		const gchar *color = preview_color(run, invalid, n);
		gchar *escaped = g_markup_escape_text(run->text, -1);

		if(color == NULL)
			g_string_append(markup, escaped);
		else
			g_string_append_printf(markup, "<span foreground=\"%s\"%s>%s</span>", color,
			                       run->kind == GT_PREVIEW_ARG ? " weight=\"600\"" : "", escaped);
		g_free(escaped);
	}
	gtk_label_set_markup(GTK_LABEL(editor->preview), markup->str);
	hex = gt_macro_preview_hex(data, len, 256);
	text = g_strdup_printf(ngettext("%u byte", "%u bytes", (gulong)len), (guint)len);
	if(len > 0)
	{
		gchar *full = g_strconcat(hex, " · ", text, NULL);

		g_free(text);
		text = full;
	}
	gtk_label_set_text(GTK_LABEL(editor->preview_bytes), text);
	gtk_widget_set_visible(editor->preview_box, TRUE);
	g_free(text);
	g_free(hex);
	g_string_free(markup, TRUE);
	g_ptr_array_unref(runs);
	if(segments != NULL)
		g_array_unref(segments);
	g_bytes_unref(bytes);
	g_free(invalid);
	macro_arg_infos_free(infos, n);
	gt_macro_set_free(set);
}

static void defaults_rebuild(MacroEditor *editor);

/* Ligne affichée modifiée : action → table refaite ; défauts ou limites → aperçu */
static void on_defaults_row_notify(GObject *row, GParamSpec *pspec, gpointer data)
{
	MacroEditor *editor = data;
	const gchar *name = g_param_spec_get_name(pspec);

	if(g_strcmp0(name, "action") == 0)
		defaults_rebuild(editor);
	else if(g_strcmp0(name, "args") == 0 || g_str_has_prefix(name, "arg-"))
		preview_update(editor);
}

static GtkWidget *table_header(const gchar *text)
{
	GtkWidget *label = gtk_label_new(text);

	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_widget_add_css_class(label, "heading");
	return label;
}

static GtkWidget *table_text(const gchar *text)
{
	GtkWidget *label = gtk_label_new(text);

	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	return label;
}

/* Nom de widget et nom accessible « <prefix>-<k> » */
static void name_field(GtkWidget *w, const gchar *prefix, gint k)
{
	gchar *name = g_strdup_printf("%s-%d", prefix, k);

	gtk_widget_set_name(w, name);
	gtk_accessible_update_property(GTK_ACCESSIBLE(w), GTK_ACCESSIBLE_PROPERTY_LABEL, name, -1);
	g_free(name);
}

static GtkWidget *limit_entry(const gchar *prefix, gint k, const gchar *initial)
{
	GtkWidget *w = gtk_entry_new();

	gtk_editable_set_width_chars(GTK_EDITABLE(w), 8);
	gtk_editable_set_max_width_chars(GTK_EDITABLE(w), 8);
	gtk_editable_set_text(GTK_EDITABLE(w), initial);
	name_field(w, prefix, k);
	g_signal_connect(w, "changed", G_CALLBACK(on_table_entry_changed), NULL);
	return w;
}

/* Cellule Charset d'un %s : liste déroulante et entrée de classe (visible pour « Custom class ») */
static GtkWidget *charset_cell(gint k, const gchar *initial)
{
	const char *const labels[] = { _("UTF-8"), _("Printable ASCII"), _("ASCII"), _("8 bits (Latin-1)"), _("Custom class"), NULL };
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	GtkWidget *dropdown = gtk_drop_down_new_from_strings(labels);
	GtkWidget *cls = gtk_entry_new();
	GtMacroCharset kind = GT_MACRO_CHARSET_UTF8;
	gchar *class_text = NULL;
	guint sel = 0;

	if(gt_macro_charset_parse(initial, &kind, &class_text))
		sel = kind == GT_MACRO_CHARSET_PRINTABLE ? 1 : kind == GT_MACRO_CHARSET_ASCII ? 2 : kind == GT_MACRO_CHARSET_LATIN1 ? 3
		      : kind == GT_MACRO_CHARSET_CLASS ? CHARSET_CLASS : 0;
	gtk_drop_down_set_selected(GTK_DROP_DOWN(dropdown), sel);
	name_field(dropdown, "macro-charset", k);
	gtk_editable_set_width_chars(GTK_EDITABLE(cls), 10);
	gtk_editable_set_max_width_chars(GTK_EDITABLE(cls), 10);
	gtk_editable_set_text(GTK_EDITABLE(cls), class_text != NULL ? class_text : "");
	gtk_entry_set_placeholder_text(GTK_ENTRY(cls), "A-Z0-9_-");
	name_field(cls, "macro-class", k);
	gtk_widget_set_visible(cls, sel == CHARSET_CLASS);
	g_free(class_text);
	g_signal_connect(dropdown, "notify::selected", G_CALLBACK(on_charset_selected), cls);
	g_signal_connect(cls, "changed", G_CALLBACK(on_table_entry_changed), NULL);
	gtk_box_append(GTK_BOX(box), dropdown);
	gtk_box_append(GTK_BOX(box), cls);
	g_object_set_data(G_OBJECT(box), "dropdown", dropdown);
	g_object_set_data(G_OBJECT(box), "class", cls);
	return box;
}

/* Table refaite : sélection, action de la ligne affichée ou listes ont changé */
static void defaults_rebuild(MacroEditor *editor)
{
	guint n = 0;
	guint *pos = selected_positions(editor->selection, &n);
	GtMacroRow *row = n == 1 ? g_list_model_get_item(G_LIST_MODEL(editor->rows), pos[0]) : NULL;
	macro_arg_info_t *infos = NULL;
	GtMacroLists *lists = NULL;
	const gchar *const headers[N_COLS] = { _("#"), _("Format"), _("Type"), _("Default"), _("Min"), _("Max"), _("Charset") };
	const gchar *const *args, *const *mins, *const *maxs, *const *css;
	gint n_args = 0;

	g_free(pos);
	if(editor->defaults == NULL)	/* table détruite avant l'état (fenêtre en destruction) */
	{
		g_clear_object(&row);
		return;
	}
	editor->defaults_building = TRUE;
	defaults_clear(editor);
	if(row != editor->defaults_row)
	{
		defaults_unwatch(editor);
		if(row != NULL)
		{
			editor->defaults_row = g_object_ref(row);
			editor->defaults_notify_id = g_signal_connect(row, "notify", G_CALLBACK(on_defaults_row_notify), editor);
		}
	}
	if(row != NULL)
	{
		lists = gt_macro_list_rows_to_lists(G_LIST_MODEL(editor->list_rows));
		infos = macro_get_arg_infos(lists, gt_macro_row_get_action(row), &n_args);
	}
	editor->defaults_n = n_args;
	if(n_args > 0)
	{
		args = row_vector(gt_macro_row_get_args(row), n_args);
		mins = row_vector(gt_macro_row_get_arg_min(row), n_args);
		maxs = row_vector(gt_macro_row_get_arg_max(row), n_args);
		css = row_vector(gt_macro_row_get_arg_charset(row), n_args);
		for(gint c = 0; c < N_COLS; c++)
			gtk_grid_attach(GTK_GRID(editor->defaults), table_header(headers[c]), c, 0, 1, 1);
		for(gint k = 0; k < n_args; k++)
		{
			const gchar *type = infos[k].type;
			gboolean text = g_strcmp0(type, "s") == 0;
			gchar *rank = g_strdup_printf("%d", k + 1);
			GtkWidget *w;

			gtk_grid_attach(GTK_GRID(editor->defaults), table_text(rank), COL_RANK, k + 1, 1, 1);
			g_free(rank);
			gtk_grid_attach(GTK_GRID(editor->defaults), table_text(infos[k].spec), COL_FORMAT, k + 1, 1, 1);
			gtk_grid_attach(GTK_GRID(editor->defaults), table_text(text ? _("text (bytes)") : gt_macro_arg_type_name(type)), COL_TYPE, k + 1, 1, 1);
			w = gt_macro_arg_widget_new(lists, &infos[k], args != NULL ? args[k] : "", NULL);
			if(GTK_IS_DROP_DOWN(w))
				g_signal_connect(w, "notify::selected", G_CALLBACK(on_default_dropdown_selected), NULL);
			else
			{
				gtk_editable_set_width_chars(GTK_EDITABLE(w), 10);
				gtk_editable_set_max_width_chars(GTK_EDITABLE(w), 10);
				g_signal_connect(w, "changed", G_CALLBACK(on_table_entry_changed), NULL);
			}
			name_field(w, "macro-default", k);
			gtk_grid_attach(GTK_GRID(editor->defaults), w, COL_DEFAULT, k + 1, 1, 1);
			if(gt_macro_arg_type_has_limits(type))
			{
				gtk_grid_attach(GTK_GRID(editor->defaults), limit_entry("macro-min", k, nth(mins, k)), COL_MIN, k + 1, 1, 1);
				gtk_grid_attach(GTK_GRID(editor->defaults), limit_entry("macro-max", k, nth(maxs, k)), COL_MAX, k + 1, 1, 1);
			}
			if(text)
				gtk_grid_attach(GTK_GRID(editor->defaults), charset_cell(k, nth(css, k)), COL_CHARSET, k + 1, 1, 1);
		}
	}
	gtk_widget_set_visible(editor->defaults_scroll, n_args > 0);
	if(infos != NULL)
		macro_arg_infos_free(infos, n_args);
	if(lists != NULL)
		gt_macro_lists_free(lists);
	g_clear_object(&row);
	editor->defaults_building = FALSE;
	defaults_commit(editor, FALSE);	/* champs vérifiés avec les limites affichées, sans rien écrire */
	preview_update(editor);
}

static void on_defaults_selection_changed(GtkSelectionModel *model, guint position, guint n_items, gpointer data)
{
	defaults_rebuild(data);
}

static void on_defaults_items_changed(GListModel *model, guint position, guint removed, guint added, gpointer data)
{
	defaults_rebuild(data);
}

/* Retour sur la page Macros : table refaite (les listes ont pu changer sur la page Lists : valeurs et noms à jour) */
static void on_stack_page(GObject *stack, GParamSpec *pspec, gpointer data)
{
	MacroEditor *editor = editor_of(GTK_WIDGET(stack));

	if(editor != NULL && g_strcmp0(gtk_stack_get_visible_child_name(GTK_STACK(stack)), "macros") == 0)
		defaults_rebuild(editor);
}

/* Table (« macro-defaults ») dans un défilement horizontal : la fenêtre ne s'élargit pas pour elle */
static GtkWidget *build_defaults_box(MacroEditor *editor)
{
	GtkWidget *scrolled = gtk_scrolled_window_new();
	GtkWidget *grid = gtk_grid_new();

	gtk_widget_set_name(grid, "macro-defaults");
	gtk_accessible_update_property(GTK_ACCESSIBLE(grid), GTK_ACCESSIBLE_PROPERTY_LABEL, _("Arguments"), -1);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
	gtk_widget_set_halign(grid, GTK_ALIGN_START);	/* colonnes à leur largeur naturelle */
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
	gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scrolled), TRUE);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), grid);
	gtk_widget_set_visible(scrolled, FALSE);
	editor->defaults = grid;
	editor->defaults_scroll = scrolled;
	/* NULL dès que les widgets sont détruits : les rappels ne les touchent plus */
	g_object_add_weak_pointer(G_OBJECT(grid), (gpointer *)&editor->defaults);
	g_object_add_weak_pointer(G_OBJECT(scrolled), (gpointer *)&editor->defaults_scroll);
	return scrolled;
}

/* Aperçu (« macro-preview-box ») : étiquette, ligne en monospace (balisage), octets en hexadécimal */
static GtkWidget *build_preview_box(MacroEditor *editor)
{
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	GtkWidget *title = table_text(_("Line sent (default values):"));
	GtkWidget *line = table_text(NULL);
	GtkWidget *bytes = table_text(NULL);

	gtk_widget_set_name(box, "macro-preview-box");
	gtk_widget_set_name(line, "macro-preview");
	gtk_accessible_update_property(GTK_ACCESSIBLE(line), GTK_ACCESSIBLE_PROPERTY_LABEL, "macro-preview", -1);
	gtk_widget_set_name(bytes, "macro-preview-bytes");
	gtk_accessible_update_property(GTK_ACCESSIBLE(bytes), GTK_ACCESSIBLE_PROPERTY_LABEL, "macro-preview-bytes", -1);
	gtk_widget_add_css_class(title, "dim-label");
	for(GtkWidget **w = (GtkWidget *[]){ line, bytes, NULL }; *w != NULL; w++)
	{
		gtk_widget_add_css_class(*w, "monospace");
		gtk_label_set_selectable(GTK_LABEL(*w), TRUE);
		gtk_label_set_wrap(GTK_LABEL(*w), TRUE);
		gtk_label_set_wrap_mode(GTK_LABEL(*w), PANGO_WRAP_CHAR);
	}
	gtk_box_append(GTK_BOX(box), title);
	gtk_box_append(GTK_BOX(box), line);
	gtk_box_append(GTK_BOX(box), bytes);
	gtk_widget_set_visible(box, FALSE);
	editor->preview_box = box;
	editor->preview = line;
	editor->preview_bytes = bytes;
	g_object_add_weak_pointer(G_OBJECT(box), (gpointer *)&editor->preview_box);
	g_object_add_weak_pointer(G_OBJECT(line), (gpointer *)&editor->preview);
	g_object_add_weak_pointer(G_OBJECT(bytes), (gpointer *)&editor->preview_bytes);
	return box;
}

static GtkWidget *build_page(MacroEditor *editor, gint page)
{
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	GtkWidget *scrolled = gtk_scrolled_window_new();
	GtkWidget *row1 = button_row(), *row2 = button_row();
	GtkSelectionModel *selection = GTK_SELECTION_MODEL(gtk_multi_selection_new(G_LIST_MODEL(g_object_ref(page_store(editor, page)))));
	GtkWidget *view = gtk_column_view_new(selection);	/* prend la sélection */

	gtk_column_view_set_reorderable(GTK_COLUMN_VIEW(view), FALSE);
	gtk_column_view_set_show_column_separators(GTK_COLUMN_VIEW(view), TRUE);
	if(page == PAGE_MACROS)
	{
		editor->selection = g_object_ref(selection);	/* tenue aussi par l'état : la zone des valeurs par défaut s'en déconnecte à la destruction */
		gtk_widget_set_name(view, "macro-list");
		add_column(GTK_COLUMN_VIEW(view), selection, _("Label"), "label", "macro-cell-label", TRUE, 160);
		add_column(GTK_COLUMN_VIEW(view), selection, _("Shortcut"), "shortcut", "macro-cell-shortcut", FALSE, 120);
		add_column(GTK_COLUMN_VIEW(view), selection, _("Action"), "action", "macro-cell-action", TRUE, 280);
		add_column(GTK_COLUMN_VIEW(view), selection, _("Tab"), "tab", "macro-cell-tab", TRUE, 0);
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
		add_column(GTK_COLUMN_VIEW(view), selection, _("List"), "list", "list-cell-name", TRUE, 160);
		add_column(GTK_COLUMN_VIEW(view), selection, _("Display"), "display", "list-cell-display", TRUE, 240);
		add_column(GTK_COLUMN_VIEW(view), selection, _("Value"), "value", "list-cell-value", TRUE, 0);
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
	if(page == PAGE_MACROS)
	{
		gtk_box_append(GTK_BOX(box), build_defaults_box(editor));	/* pointeurs faibles posés par build_defaults_box */
		gtk_box_append(GTK_BOX(box), build_preview_box(editor));
		g_signal_connect(selection, "selection-changed", G_CALLBACK(on_defaults_selection_changed), editor);
		g_signal_connect(editor->rows, "items-changed", G_CALLBACK(on_defaults_items_changed), editor);
		g_signal_connect(editor->list_rows, "items-changed", G_CALLBACK(on_defaults_items_changed), editor);
	}
	gtk_box_append(GTK_BOX(box), row1);
	gtk_box_append(GTK_BOX(box), row2);
	return box;
}

void gt_macro_editor_open(GtkWindow *parent, GtSession *s)
{
	GtkWidget *vbox, *stack, *switcher, *bottom, *open;
	GtkEventController *keys;
	MacroEditor *editor;
	gchar *title;

	if(s == NULL || gt_session_is_shut_down(s))
		return;
	open = g_object_get_data(G_OBJECT(s), SESSION_EDITOR_KEY);
	if(open != NULL)
	{
		gtk_window_present(GTK_WINDOW(open));
		return;
	}
	editor = g_new0(MacroEditor, 1);
	g_weak_ref_init(&editor->session, s);
	editor->set = gt_macros_get_set(gt_session_get_macros(s));
	editor->rows = gt_macro_rows_from_set(editor->set);
	editor->list_rows = gt_macro_list_rows_from_lists(editor->set != NULL ? gt_macro_set_get_lists(editor->set) : NULL);
	editor->window = gtk_window_new();
	g_object_set_data_full(G_OBJECT(editor->window), EDITOR_KEY, editor, editor_free);
	g_object_set_data(G_OBJECT(s), SESSION_EDITOR_KEY, editor->window);
	gtk_widget_set_name(editor->window, "macro-editor");
	/* Titre au nom court de l'onglet (fixé à l'ouverture) */
	title = g_strdup_printf(_("Configure Macros — %s"), gt_session_get_short_name(s));
	gtk_window_set_title(GTK_WINDOW(editor->window), title);
	g_free(title);
	gtk_window_set_default_size(GTK_WINDOW(editor->window), 820, 580);	/* place pour la table des arguments et l'aperçu */
	if(parent != NULL)
	{
		gtk_window_set_transient_for(GTK_WINDOW(editor->window), parent);
		gtk_window_set_destroy_with_parent(GTK_WINDOW(editor->window), TRUE);
	}
	g_signal_connect(editor->window, "close-request", G_CALLBACK(on_close_request), NULL);
	g_signal_connect(editor->window, "destroy", G_CALLBACK(on_destroy), NULL);
	g_signal_connect_object(s, "closed", G_CALLBACK(on_session_closed), editor->window, 0);
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
	gtk_stack_add_titled(GTK_STACK(stack), build_page(editor, PAGE_MACROS), "macros", _("Macros"));
	gtk_stack_add_titled(GTK_STACK(stack), build_page(editor, PAGE_LISTS), "lists", _("Lists"));
	g_signal_connect(stack, "notify::visible-child", G_CALLBACK(on_stack_page), NULL);
	switcher = gtk_stack_switcher_new();
	gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher), GTK_STACK(stack));
	gtk_widget_set_halign(switcher, GTK_ALIGN_CENTER);
	editor->message = gtk_label_new(NULL);
	gtk_widget_set_name(editor->message, "macro-editor-error");
	gtk_label_set_wrap(GTK_LABEL(editor->message), TRUE);
	gtk_label_set_xalign(GTK_LABEL(editor->message), 0.0);
	gtk_widget_set_visible(editor->message, FALSE);
	editor->template_info = gtk_label_new(NULL);
	g_object_add_weak_pointer(G_OBJECT(editor->template_info), (gpointer *)&editor->template_info);
	gtk_widget_set_name(editor->template_info, "macro-template-info");
	gtk_accessible_update_property(GTK_ACCESSIBLE(editor->template_info), GTK_ACCESSIBLE_PROPERTY_LABEL, _("Macros template"), -1);
	gtk_label_set_wrap(GTK_LABEL(editor->template_info), TRUE);
	gtk_label_set_wrap_mode(GTK_LABEL(editor->template_info), PANGO_WRAP_WORD_CHAR);
	gtk_label_set_selectable(GTK_LABEL(editor->template_info), TRUE);
	gtk_label_set_xalign(GTK_LABEL(editor->template_info), 0.0);
	template_info_update(editor, gt_session_get_macros(s));
	bottom = button_row();
	add_button(bottom, _("Help"), "macro-help", G_CALLBACK(on_help), 0);
	add_button(bottom, _("Cancel"), "macro-cancel", G_CALLBACK(on_cancel), 0);
	add_button(bottom, _("OK"), "macro-ok", G_CALLBACK(on_ok), 0);
	gtk_box_append(GTK_BOX(vbox), editor->template_info);
	gtk_box_append(GTK_BOX(vbox), switcher);
	gtk_box_append(GTK_BOX(vbox), stack);
	gtk_box_append(GTK_BOX(vbox), editor->message);
	gtk_box_append(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
	gtk_box_append(GTK_BOX(vbox), bottom);
	gtk_window_set_child(GTK_WINDOW(editor->window), vbox);
	gtk_window_present(GTK_WINDOW(editor->window));
}

/* Comparaison de pointeurs sûre : gt_macros_replace_set crée le nouveau jeu avant de libérer l'ancien et appelle ce crochet à chaque
 * remplacement ; editor->set n'est jamais déréférencé une fois différent du jeu courant. */
void gt_macro_editor_macros_changed(GtSession *s)
{
	GtkWidget *window = s != NULL ? g_object_get_data(G_OBJECT(s), SESSION_EDITOR_KEY) : NULL;
	MacroEditor *editor = window != NULL ? g_object_get_data(G_OBJECT(window), EDITOR_KEY) : NULL;

	if(editor != NULL && gt_macros_get_set(gt_session_get_macros(s)) != editor->set)
		gtk_window_destroy(GTK_WINDOW(window));	/* sans close-request : rien n'est appliqué ni enregistré */
	else if(editor != NULL)
		template_info_update(editor, gt_session_get_macros(s));	/* lien détaché ou rafraîchi */
}

/* app.macros : éditeur de la session active au moment de l'activation */
static void on_macros_action(GSimpleAction *action, GVariant *parameter, gpointer data)
{
	gt_macro_editor_open(gt_app_get_main_window(), gt_app_get_active_session());
}

void gt_macro_editor_install_action(GtkApplication *app)
{
	GSimpleAction *action = g_simple_action_new("macros", NULL);

	g_signal_connect(action, "activate", G_CALLBACK(on_macros_action), NULL);
	g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(action));
	g_object_unref(action);
}
