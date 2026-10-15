/* macro_editor.c — fenêtre « Configure Macros » (interface GTK 4, spec 6a §6.5) : non modale, une par session (6b, spec §3.2-12), pages
 * Macros et Lists. Les lignes sont des copies (GtMacroRow, GtMacroListRow) : rien n'est appliqué avant OK ou la fermeture par la barre de
 * titre ; Cancel jette tout. Un jeu remplacé pendant l'édition, ou la session fermée, ferme la fenêtre sans rien enregistrer, sauf le
 * Reload de l'éditeur lui-même (spec 2026-10-15 §4), qui refait les lignes depuis le fichier ou le modèle relu. La fenêtre
 * n'est pas rattachée à l'application : ses raccourcis (Ctrl+Maj+Q…) ne s'y appliquent pas, la capture reçoit toutes les touches.
 * L'état est attaché à la fenêtre ; les rappels le retrouvent par la fenêtre racine de leur widget. */

#include <config.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>

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
	GtkSelectionModel *selection;	/* GtkMultiSelection sur la vue filtrée de rows (référence aussi tenue par la vue) */
	GtkSelectionModel *list_selection;	/* GtkMultiSelection sur la vue filtrée de list_rows */
	GtkFilterListModel *filtered[2];	/* vue filtrée de chaque page (spec §3) : la sélection porte sur elle, jamais sur le store */
	GtkCustomFilter   *filter[2];	/* filtre sur la clé de groupe (onglet, liste) */
	GtkWidget         *filter_dd[2];	/* « macro-tab-filter », « lists-filter » (pointeurs faibles) */
	GPtrArray         *filter_keys[2];	/* clé de chaque élément de la liste déroulante ; [0] = NULL pour « All » */
	gchar             *filter_key[2];	/* clé choisie ; NULL : All */
	gboolean           filter_rebuilding;	/* liste déroulante en reconstruction : notify::selected ignoré */
	gboolean           regroup_pending[2];	/* regroupement demandé à la fin de l'édition d'une cellule Tab / List */
	guint              regroup_source;	/* source inactive qui l'exécute ; 0 : aucune */
	GtkWidget         *settings_separator;	/* « macro-settings-separator » (pointeur faible) */
	GtkWidget         *settings_box;	/* boîte de la table des arguments et de l'aperçu (pointeur faible) */
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
	GtkWidget         *source_label;	/* « macro-source-label » (pointeur faible) : modèle, fichier, intégré au projet ou aucun fichier (spec §4) */
	GtkWidget         *reload;	/* « macro-reload » (pointeur faible) : visible pour un modèle ou un fichier, insensible pendant une question */
	gboolean           asking;	/* question en cours (modèle, spec modèles §2.7, ou Reload) : OK et Reload sans effet, fermeture refusée */
	gboolean           reloading;	/* Reload en cours : un jeu remplacé refait les lignes au lieu de fermer la fenêtre */
	gboolean           reloaded;	/* lignes refaites pendant ce Reload (crochet macros_changed) */
	gboolean           prefix_message;	/* message « Prefix of argument %d removed » affiché */
} MacroEditor;

/* Question posée sur un onglet lié : la réponse retrouve l'éditeur par sa fenêtre (jamais le MacroEditor, libéré avec elle) et n'applique rien
 * si la génération des macros a changé entre-temps */
typedef struct
{
	GWeakRef         window;
	guint64          generation;
	GtMacrosEditKind kind;	/* DEFAULTS, STRUCTURE ; NONE : question de conflit du modèle */
} TemplateQuestion;

/* Question de Reload (lignes modifiées) : même modèle que TemplateQuestion */
typedef struct
{
	GWeakRef window;
	guint64  generation;
} ReloadQuestion;

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
static void source_update(MacroEditor *editor, GtMacros *macros);
static void defaults_rebuild(MacroEditor *editor);

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
	editor->prefix_message = FALSE;	/* remis par prefix_message_update après son propre message */
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

/* Question en cours ou non (modèle, Reload) : le bouton Reload suit */
static void asking_set(MacroEditor *editor, gboolean value)
{
	editor->asking = value;
	if(editor->reload != NULL)
		gtk_widget_set_sensitive(editor->reload, !value);
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

static GListStore *page_store(MacroEditor *editor, gint page)
{
	return page == PAGE_MACROS ? editor->rows : editor->list_rows;
}

static GtkSelectionModel *page_selection(MacroEditor *editor, gint page)
{
	return page == PAGE_MACROS ? editor->selection : editor->list_selection;
}

/* Clé de groupe des lignes de la page : onglet d'une macro, nom de liste d'une entrée */
static GtRowKeyFunc page_key(gint page)
{
	return page == PAGE_MACROS ? gt_macro_row_tab_key : gt_macro_list_row_key;
}

static gint compare_guint(gconstpointer a, gconstpointer b)
{
	guint x = *(const guint *)a, y = *(const guint *)b;

	return x < y ? -1 : x > y;
}

/* Positions dans le store des lignes sélectionnées (vue filtrée), croissantes ; g_free */
static guint *selected_store_positions(MacroEditor *editor, gint page, guint *n_out)
{
	GtkSelectionModel *sel = page_selection(editor, page);
	guint n = 0, kept = 0, *pos = selected_positions(sel, &n);

	for(guint k = 0; k < n; k++)
	{
		gpointer item = g_list_model_get_item(G_LIST_MODEL(sel), pos[k]);

		/* Introuvable dans le store (ne devrait pas arriver) : écartée, jamais une position de la vue prise pour une position du store */
		if(item != NULL && g_list_store_find(page_store(editor, page), item, &pos[kept]))
			kept++;
		g_clear_object(&item);
	}
	n = kept;
	qsort(pos, n, sizeof *pos, compare_guint);
	*n_out = n;
	return pos;
}

/* Sélectionne exactement les lignes du store aux positions données, retrouvées par identité dans la vue filtrée (une ligne cachée par le
 * filtre n'est pas sélectionnée) ; un seul selection-changed */
static void reselect_items(MacroEditor *editor, gint page, const guint *store_pos, guint n)
{
	GtkSelectionModel *sel = page_selection(editor, page);
	GListModel *store = G_LIST_MODEL(page_store(editor, page));
	guint n_view = g_list_model_get_n_items(G_LIST_MODEL(sel));
	GtkBitset *selected = gtk_bitset_new_empty(), *mask = gtk_bitset_new_range(0, n_view);

	for(guint k = 0; k < n; k++)
	{
		gpointer item = g_list_model_get_item(store, store_pos[k]);

		for(guint v = 0; item != NULL && v < n_view; v++)
		{
			gpointer shown = g_list_model_get_item(G_LIST_MODEL(sel), v);

			g_object_unref(shown);	/* tenue par le store : seule l'identité compte */
			if(shown == item)
			{
				gtk_bitset_add(selected, v);
				break;
			}
		}
		g_clear_object(&item);
	}
	gtk_selection_model_set_selection(sel, selected, mask);
	gtk_bitset_unref(selected);
	gtk_bitset_unref(mask);
}

/* --- Filtres (spec §3) et regroupement (§2.3) --- */

static gboolean macro_matches(gpointer item, gpointer data)
{
	MacroEditor *editor = data;

	return editor->filter_key[PAGE_MACROS] == NULL || strcmp(gt_macro_row_get_tab(item), editor->filter_key[PAGE_MACROS]) == 0;
}

static gboolean list_matches(gpointer item, gpointer data)
{
	MacroEditor *editor = data;

	return editor->filter_key[PAGE_LISTS] == NULL || strcmp(gt_macro_list_row_get_list(item), editor->filter_key[PAGE_LISTS]) == 0;
}

/* Liste déroulante refaite : « All », puis chaque clé du store dans l'ordre des groupes (onglet vide affiché « General ») ; si la clé choisie
 * a disparu, le filtre revient à « All » */
static void filter_keys_rebuild(MacroEditor *editor, gint page)
{
	GListModel *store = G_LIST_MODEL(page_store(editor, page));
	GtRowKeyFunc key = page_key(page);
	GPtrArray *keys = g_ptr_array_new_with_free_func(g_free);
	GtkStringList *strings = gtk_string_list_new(NULL);
	guint selected = 0;

	g_ptr_array_add(keys, NULL);
	gtk_string_list_append(strings, _("All"));
	for(guint i = 0; i < g_list_model_get_n_items(store); i++)
	{
		gpointer item = g_list_model_get_item(store, i);
		const gchar *k = key(item);
		gboolean seen = FALSE;

		for(guint j = 1; j < keys->len && !seen; j++)
			seen = g_strcmp0(g_ptr_array_index(keys, j), k) == 0;
		if(!seen)
		{
			g_ptr_array_add(keys, g_strdup(k));
			gtk_string_list_append(strings, page == PAGE_MACROS && (k == NULL || k[0] == '\0') ? _("General") : k);
		}
		g_object_unref(item);
	}
	for(guint j = 1; editor->filter_key[page] != NULL && j < keys->len && selected == 0; j++)
		if(g_strcmp0(g_ptr_array_index(keys, j), editor->filter_key[page]) == 0)
			selected = j;
	if(editor->filter_keys[page] != NULL)
		g_ptr_array_unref(editor->filter_keys[page]);
	editor->filter_keys[page] = keys;
	if(editor->filter_dd[page] != NULL)
	{
		editor->filter_rebuilding = TRUE;
		gtk_drop_down_set_model(GTK_DROP_DOWN(editor->filter_dd[page]), G_LIST_MODEL(strings));
		gtk_drop_down_set_selected(GTK_DROP_DOWN(editor->filter_dd[page]), selected);
		editor->filter_rebuilding = FALSE;
	}
	g_object_unref(strings);
	if(selected == 0 && editor->filter_key[page] != NULL)	/* clé disparue : retour à « All » */
	{
		g_clear_pointer(&editor->filter_key[page], g_free);
		gtk_filter_changed(GTK_FILTER(editor->filter[page]), GTK_FILTER_CHANGE_DIFFERENT);
	}
}

/* Filtre choisi par l'utilisateur (non enregistré) */
static void on_filter_selected(GObject *dropdown, GParamSpec *pspec, gpointer data)
{
	MacroEditor *editor = editor_of(GTK_WIDGET(dropdown));
	gint page = GPOINTER_TO_INT(data);
	guint sel;

	if(editor == NULL || editor->filter_rebuilding || editor->filter_keys[page] == NULL)
		return;
	sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown));
	g_free(editor->filter_key[page]);
	editor->filter_key[page] = sel > 0 && sel < editor->filter_keys[page]->len ? g_strdup(g_ptr_array_index(editor->filter_keys[page], sel)) : NULL;
	gtk_filter_changed(GTK_FILTER(editor->filter[page]), GTK_FILTER_CHANGE_DIFFERENT);
}

/* Lignes ajoutées, supprimées ou déplacées : clés du filtre à jour */
static void on_filter_items_changed(GListModel *model, guint position, guint removed, guint added, gpointer data)
{
	MacroEditor *editor = data;

	filter_keys_rebuild(editor, model == G_LIST_MODEL(editor->rows) ? PAGE_MACROS : PAGE_LISTS);
}

/* Regroupe les lignes de la page (§2.1) ; la sélection suit les lignes déplacées (identité d'objet) ; filtre réévalué (une clé a pu changer) */
static void regroup(MacroEditor *editor, gint page)
{
	GListStore *store = page_store(editor, page);
	guint n = 0, *pos = selected_store_positions(editor, page, &n);
	gpointer *items = g_new(gpointer, MAX(n, 1));

	editor->regroup_pending[page] = FALSE;
	for(guint k = 0; k < n; k++)
		items[k] = g_list_model_get_item(G_LIST_MODEL(store), pos[k]);
	if(gt_macro_rows_group(store, page_key(page)))
	{
		for(guint k = 0; k < n; k++)
			g_list_store_find(store, items[k], &pos[k]);
		qsort(pos, n, sizeof *pos, compare_guint);
		reselect_items(editor, page, pos, n);
	}
	for(guint k = 0; k < n; k++)
		g_object_unref(items[k]);
	g_free(items);
	g_free(pos);
	filter_keys_rebuild(editor, page);
	gtk_filter_changed(GTK_FILTER(editor->filter[page]), GTK_FILTER_CHANGE_DIFFERENT);
}

/* Regroupements demandés et pas encore faits (OK ou fermeture avant la source inactive) */
static void regroup_flush(MacroEditor *editor)
{
	g_clear_handle_id(&editor->regroup_source, g_source_remove);
	for(gint page = PAGE_MACROS; page <= PAGE_LISTS; page++)
		if(editor->regroup_pending[page])
			regroup(editor, page);
}

static gboolean regroup_idle(gpointer data)
{
	MacroEditor *editor = data;

	editor->regroup_source = 0;
	regroup_flush(editor);
	return G_SOURCE_REMOVE;
}

/* Hors de l'émission de la cellule qui finit son édition : le store n'est pas réordonné sous le widget qui notifie */
static void regroup_later(MacroEditor *editor, gint page)
{
	editor->regroup_pending[page] = TRUE;
	if(editor->regroup_source == 0)
		editor->regroup_source = g_idle_add(regroup_idle, editor);	/* retirée par editor_free */
}

/* --- Cellules --- */

/* Passer une cellule en édition sélectionne sa ligne seule et annule une capture ; la fin de l'édition d'une cellule Tab ou List regroupe
 * les lignes (§2.3 : à la validation, pas à chaque frappe) */
static void on_cell_editing(GObject *label, GParamSpec *pspec, gpointer cell)
{
	CellSpec *spec = g_object_get_data(label, "spec");
	MacroEditor *editor = editor_of(GTK_WIDGET(label));

	if(editor == NULL)
		return;
	if(gtk_editable_label_get_editing(GTK_EDITABLE_LABEL(label)))
	{
		stop_capture(editor);
		gtk_selection_model_select_item(spec->selection, gtk_column_view_cell_get_position(GTK_COLUMN_VIEW_CELL(cell)), TRUE);
	}
	else if(strcmp(spec->prop, "tab") == 0)
		regroup_later(editor, PAGE_MACROS);
	else if(strcmp(spec->prop, "list") == 0)
		regroup_later(editor, PAGE_LISTS);
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

/* Add / Add Entry (spec §3, §7 bis) : clé du filtre, sinon celle de la dernière ligne sélectionnée (aucune : onglet vide, « NewList ») ;
 * insérée après la dernière ligne sélectionnée si elle est de cette clé, sinon après la dernière ligne de la clé, sinon à la fin ; sélectionnée */
static void on_add(GtkButton *button, gpointer data)
{
	MacroEditor *editor = editor_of(GTK_WIDGET(button));
	gint page = GPOINTER_TO_INT(data);
	GtRowKeyFunc keyf = page_key(page);
	GListStore *store;
	guint n = 0, *pos, at, n_items;
	gchar *key;
	gpointer row;

	if(editor == NULL)
		return;
	store = page_store(editor, page);
	n_items = g_list_model_get_n_items(G_LIST_MODEL(store));
	pos = selected_store_positions(editor, page, &n);
	at = n_items;
	if(editor->filter_key[page] != NULL)
		key = g_strdup(editor->filter_key[page]);
	else if(n > 0)
	{
		gpointer last = g_list_model_get_item(G_LIST_MODEL(store), pos[n - 1]);

		key = g_strdup(keyf(last));
		g_object_unref(last);
	}
	else
		key = g_strdup(page == PAGE_MACROS ? "" : "NewList");
	for(guint i = 0; i < n_items; i++)	/* après la dernière ligne de la clé */
	{
		gpointer item = g_list_model_get_item(G_LIST_MODEL(store), i);

		if(strcmp(keyf(item), key) == 0)
			at = i + 1;
		g_object_unref(item);
	}
	if(n > 0)	/* après la dernière ligne sélectionnée, si elle est de la clé */
	{
		gpointer last = g_list_model_get_item(G_LIST_MODEL(store), pos[n - 1]);

		if(strcmp(keyf(last), key) == 0)
			at = pos[n - 1] + 1;
		g_object_unref(last);
	}
	row = page == PAGE_MACROS ? (gpointer)gt_macro_row_new(0, "", "", "", key) : (gpointer)gt_macro_list_row_new(key, "display", "value");
	stop_capture(editor);
	g_list_store_insert(store, at, row);
	regroup(editor, page);
	if(g_list_store_find(store, row, &at))
		reselect_items(editor, page, &at, 1);
	g_object_unref(row);
	g_free(key);
	g_free(pos);
}

/* Delete : seulement les lignes sélectionnées de la vue (une ligne cachée par le filtre n'est jamais sélectionnée) */
static void on_delete(GtkButton *button, gpointer data)
{
	MacroEditor *editor = editor_of(GTK_WIDGET(button));
	gint page = GPOINTER_TO_INT(data);
	guint n = 0, *pos;

	if(editor == NULL)
		return;
	pos = selected_store_positions(editor, page, &n);
	stop_capture(editor);
	gt_macro_rows_remove(page_store(editor, page), pos, n);
	g_free(pos);
}

/* Move Up / Down : tout ou rien, jamais au-delà du bord du groupe (§2.3, §7 bis) */
static void move_selected(MacroEditor *editor, gint page, gint delta)
{
	guint n = 0, *pos = selected_store_positions(editor, page, &n);

	stop_capture(editor);
	if(gt_macro_rows_move_grouped(page_store(editor, page), pos, n, delta, page_key(page)))
		reselect_items(editor, page, pos, n);	/* la sélection suit les lignes déplacées */
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
	pos = selected_store_positions(editor, PAGE_MACROS, &n);
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
	pos = selected_store_positions(editor, PAGE_MACROS, &n);
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
	regroup_flush(editor);	/* cellule Tab / List validée juste avant OK */
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
		asking_set(editor, FALSE);
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
	asking_set(editor, TRUE);
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

/* Store de la page rempli à nouveau avec les objets de fresh (pris) ; un seul items-changed */
static void store_replace(GListStore *store, GListStore *fresh)
{
	guint n = fresh != NULL ? g_list_model_get_n_items(G_LIST_MODEL(fresh)) : 0;
	gpointer *items = g_new(gpointer, MAX(n, 1));

	for(guint i = 0; i < n; i++)
		items[i] = g_list_model_get_item(G_LIST_MODEL(fresh), i);
	g_list_store_splice(store, 0, g_list_model_get_n_items(G_LIST_MODEL(store)), items, n);
	for(guint i = 0; i < n; i++)
		g_object_unref(items[i]);
	g_free(items);
	g_clear_object(&fresh);
}

/* Lignes refaites depuis set (Reload, spec §4) : macros et listes regroupées, filtres gardés si leur clé existe encore, table des arguments,
 * aperçu et ligne du fichier ; set devient le jeu édité */
static void editor_reload_rows(MacroEditor *editor, GtMacroSet *set)
{
	GtMacros *macros = editor_macros(editor);

	stop_capture(editor);
	g_clear_handle_id(&editor->regroup_source, g_source_remove);	/* regroupements des anciennes lignes : refaits ci-dessous */
	editor->set = set;
	store_replace(editor->rows, gt_macro_rows_from_set(set));
	store_replace(editor->list_rows, gt_macro_list_rows_from_lists(set != NULL ? gt_macro_set_get_lists(set) : NULL));
	regroup(editor, PAGE_MACROS);
	regroup(editor, PAGE_LISTS);
	defaults_rebuild(editor);
	source_update(editor, macros);
	editor->reloaded = TRUE;
}

/* Relit le modèle (onglet lié) ou le fichier courant ; l'éditeur reste ouvert sur les lignes relues ; échec : rien ne change */
static void do_reload(MacroEditor *editor, GtMacros *macros)
{
	const gchar *tpl = gt_macros_get_template(macros);
	const gchar *path = tpl != NULL ? tpl : gt_macros_get_path(macros);
	gboolean ok;

	stop_capture(editor);
	set_message(editor, NULL, FALSE);
	/* Fichier .ini disparu : gt_macros_reload le prendrait pour un jeu vide et jetterait la seule copie restante (spec §4 : rien ne change).
	 * Un modèle disparu est déjà refusé par gt_macros_reload_template. */
	if(tpl == NULL && path != NULL && !g_file_test(path, G_FILE_TEST_EXISTS))
	{
		gchar *text = g_strdup_printf(_("The macros file %s no longer exists."), path);

		set_message(editor, text, TRUE);
		g_free(text);
		return;
	}
	editor->reloading = TRUE;
	editor->reloaded = FALSE;
	ok = gt_macros_get_template(macros) != NULL ? gt_macros_reload_template(macros) : gt_macros_reload(macros);
	editor->reloading = FALSE;
	if(ok && !editor->reloaded)	/* sans crochet macros_changed (ne devrait pas arriver) : lignes refaites quand même */
		editor_reload_rows(editor, gt_macros_get_set(macros));
	if(!ok)
	{
		gchar *text = g_strdup_printf(_("Cannot reload the macros file %s."), path != NULL ? path : "");

		set_message(editor, text, TRUE);	/* la raison détaillée est déjà passée par le crochet message */
		g_free(text);
	}
}

/* Réponse à « Discard your changes and reload the macros file? » : Reload (0) seulement si la fenêtre vit et que rien n'a changé entre-temps */
static void on_reload_choice(gint choice, gpointer data)
{
	ReloadQuestion *q = data;
	GtkWidget *window = g_weak_ref_get(&q->window);
	MacroEditor *editor = window != NULL ? g_object_get_data(G_OBJECT(window), EDITOR_KEY) : NULL;
	GtMacros *macros = editor != NULL ? editor_macros(editor) : NULL;

	if(editor != NULL)
		asking_set(editor, FALSE);
	if(macros != NULL && choice == 0 && gt_macros_get_set(macros) == editor->set && gt_macros_get_generation(macros) == q->generation)
		do_reload(editor, macros);
	g_clear_object(&window);
	g_weak_ref_clear(&q->window);
	g_free(q);
}

/* Reload (spec §4) : question si les lignes diffèrent du jeu courant (même test que l'OK), sinon relecture tout de suite */
static void on_reload(GtkButton *button, gpointer data)
{
	MacroEditor *editor = editor_of(GTK_WIDGET(button));
	GtMacros *macros = editor != NULL ? editor_macros(editor) : NULL;
	GtMacroEdit *edits;
	GtMacroLists *lists;
	GtMacrosEditKind kind;
	guint n = 0;

	if(macros == NULL || editor->asking || gt_macros_get_set(macros) != editor->set)
		return;
	stop_capture(editor);
	edits = editor_edits(editor, &n, &lists);
	kind = gt_macros_classify_edits(macros, edits, n, lists);
	g_free(edits);
	gt_macro_lists_free(lists);
	if(kind == GT_MACROS_EDIT_NONE)
		do_reload(editor, macros);
	else
	{
		ReloadQuestion *q = g_new0(ReloadQuestion, 1);
		const gchar *const labels[] = { _("Reload"), _("Cancel"), NULL };
		const gchar *path = gt_macros_get_template(macros) != NULL ? gt_macros_get_template(macros) : gt_macros_get_path(macros);

		g_weak_ref_init(&q->window, editor->window);
		q->generation = gt_macros_get_generation(macros);
		asking_set(editor, TRUE);
		/* Cancel par défaut, Reload destructeur : Entrée ne jette jamais les modifications */
		gt_ui_choose(GTK_WINDOW(editor->window), _("Discard your changes and reload the macros file?"), path, labels, 1, 0, on_reload_choice, q);
	}
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
	g_signal_handlers_disconnect_by_func(editor->rows, on_filter_items_changed, editor);
	g_signal_handlers_disconnect_by_func(editor->list_rows, on_filter_items_changed, editor);
	g_clear_handle_id(&editor->regroup_source, g_source_remove);
	/* Les filtres tiennent editor sans notification de destruction et survivent peut-être à l'état (vue en cours de destruction) */
	for(gint page = PAGE_MACROS; page <= PAGE_LISTS; page++)
		if(editor->filter[page] != NULL)
			gtk_custom_filter_set_filter_func(editor->filter[page], NULL, NULL, NULL);
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
	g_clear_handle_id(&editor->regroup_source, g_source_remove);
	defaults_unwatch(editor);
	for(GtkWidget **const *w = (GtkWidget **const[]){ &editor->defaults, &editor->defaults_scroll, &editor->preview_box, &editor->preview,
	                                                  &editor->preview_bytes, &editor->source_label, &editor->reload, &editor->filter_dd[PAGE_MACROS],
	                                                  &editor->filter_dd[PAGE_LISTS], &editor->settings_separator, &editor->settings_box, NULL };
	    *w != NULL; w++)
		if(**w != NULL)
			g_object_remove_weak_pointer(G_OBJECT(**w), (gpointer *)*w);
	g_clear_object(&editor->selection);
	for(gint page = PAGE_MACROS; page <= PAGE_LISTS; page++)
	{
		g_clear_object(&editor->filtered[page]);	/* filtres jamais réévalués après l'état : seul l'éditeur change les stores */
		g_clear_object(&editor->filter[page]);
		g_clear_pointer(&editor->filter_keys[page], g_ptr_array_unref);
		g_clear_pointer(&editor->filter_key[page], g_free);
	}
	g_clear_object(&editor->rows);
	g_clear_object(&editor->list_rows);
	g_weak_ref_clear(&editor->session);
	g_free(editor);
}

/* Réglage enregistrable changé (signal « edited » : détachement par le menu, entrée dans un projet…) : ligne du fichier à jour */
static void on_session_edited(GtSession *s, gpointer window)
{
	MacroEditor *editor = g_object_get_data(G_OBJECT(window), EDITOR_KEY);

	if(editor != NULL && !gt_session_is_shut_down(s))
		source_update(editor, gt_session_get_macros(s));
}

/* Session fermée pendant l'édition (signal « closed ») : fenêtre fermée sans rien appliquer (spec §3.2-12) */
static void on_session_closed(GtSession *s, gpointer window)
{
	gtk_window_destroy(GTK_WINDOW(window));	/* sans close-request */
}

/* --- Construction --- */

/* Ligne du fichier visé (spec §4) : « Template: <chemin> » (lié), « Embedded in the project », « File: <chemin> » ou « No macros file » ;
 * Reload visible pour un modèle ou un fichier hors projet */
static void source_update(MacroEditor *editor, GtMacros *macros)
{
	GtSession *s = g_weak_ref_get(&editor->session);
	const gchar *tpl = macros != NULL ? gt_macros_get_template(macros) : NULL;
	const gchar *path = macros != NULL ? gt_macros_get_path(macros) : NULL;
	gboolean in_project = s != NULL && gt_session_get_in_project(s);
	gchar *text;

	if(editor->source_label == NULL)
	{
		g_clear_object(&s);
		return;
	}
	if(tpl != NULL)
		text = g_strdup_printf(_("Template: %s"), tpl);
	else if(in_project)
		text = g_strdup(_("Embedded in the project"));
	else if(path != NULL)
		text = g_strdup_printf(_("File: %s"), path);
	else
		text = g_strdup(_("No macros file"));
	gtk_label_set_text(GTK_LABEL(editor->source_label), text);
	if(editor->reload != NULL)
		gtk_widget_set_visible(editor->reload, tpl != NULL || (!in_project && path != NULL));
	g_free(text);
	g_clear_object(&s);
}

/* Boîte « macro-source » : libellé du fichier visé et bouton Reload */
static GtkWidget *build_source_box(MacroEditor *editor)
{
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget *label = gtk_label_new(NULL);
	GtkWidget *reload = gtk_button_new_with_label(_("Reload"));

	gtk_widget_set_name(box, "macro-source");
	gtk_widget_set_name(label, "macro-source-label");
	gtk_accessible_update_property(GTK_ACCESSIBLE(label), GTK_ACCESSIBLE_PROPERTY_LABEL, _("Macros file"), -1);
	gtk_label_set_wrap(GTK_LABEL(label), TRUE);
	gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
	gtk_label_set_selectable(GTK_LABEL(label), TRUE);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_widget_set_hexpand(label, TRUE);
	gtk_widget_set_name(reload, "macro-reload");
	gtk_accessible_update_property(GTK_ACCESSIBLE(reload), GTK_ACCESSIBLE_PROPERTY_LABEL, _("Reload"), -1);
	gtk_widget_set_valign(reload, GTK_ALIGN_CENTER);
	g_signal_connect(reload, "clicked", G_CALLBACK(on_reload), NULL);
	gtk_box_append(GTK_BOX(box), label);
	gtk_box_append(GTK_BOX(box), reload);
	editor->source_label = label;
	editor->reload = reload;
	g_object_add_weak_pointer(G_OBJECT(label), (gpointer *)&editor->source_label);
	g_object_add_weak_pointer(G_OBJECT(reload), (gpointer *)&editor->reload);
	return box;
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
enum { COL_RANK, COL_FORMAT, COL_TYPE, COL_DEFAULT, COL_MIN, COL_MAX, COL_CHARSET, COL_OPTIONAL, COL_PREFIX, N_COLS };

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

/* Vue de n éléments d'un vecteur de la ligne : tronqué ou complété par "" (règle du cœur, spec §5.1, appliquée à l'OK) ; NULL s'il est
 * entièrement vide ; g_strfreev. Optional et Prefix sont lus ainsi : la ligne garde ses vecteurs pendant une frappe dans l'action. */
static gchar **vector_resize(const gchar *const *v, gint n)
{
	gchar **out = g_new0(gchar *, n + 1);
	gint len = v != NULL ? (gint)g_strv_length((gchar **)v) : 0;

	for(gint k = 0; k < n; k++)
		out[k] = g_strdup(k < len ? v[k] : "");
	return vector_or_null(out);
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
	case GT_PREVIEW_OMITTED:	/* « ∅ » grisé : argument facultatif omis, aucun octet */
		return "#77767b";
	default:
		return NULL;
	}
}

/* Séparateur et boîte des réglages (marge de 12 px) cachés avec la table des arguments et l'aperçu (spec §6) */
static void settings_visibility_update(MacroEditor *editor)
{
	gboolean shown = (editor->defaults_scroll != NULL && gtk_widget_get_visible(editor->defaults_scroll))
	                 || (editor->preview_box != NULL && gtk_widget_get_visible(editor->preview_box));

	if(editor->settings_separator != NULL)
		gtk_widget_set_visible(editor->settings_separator, shown);
	if(editor->settings_box != NULL)
		gtk_widget_set_visible(editor->settings_box, shown);
}

static void preview_render(MacroEditor *editor);

static void preview_update(MacroEditor *editor)
{
	preview_render(editor);
	settings_visibility_update(editor);
}

/* Aperçu de la ligne affichée : jeu temporaire d'une macro (action, défauts, limites de la ligne, listes de la page Lists) ; masqué hors
 * ligne seule envoyable. Arguments en bleu, en rouge s'ils ne passent pas leurs limites ; caractères spéciaux en violet. */
static void preview_render(MacroEditor *editor)
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
	{
		gchar **opt = vector_resize(gt_macro_row_get_arg_optional(row), n), **pre = vector_resize(gt_macro_row_get_arg_prefix(row), n);

		gt_macro_set_set_optional(set, id, (const gchar *const *)opt, (const gchar *const *)pre, n);
		g_strfreev(opt);
		g_strfreev(pre);
	}
	macro = gt_macro_set_find(set, id);
	if(!gt_macro_is_sendable(macro))
	{
		gt_macro_set_free(set);
		gtk_widget_set_visible(editor->preview_box, FALSE);
		return;
	}
	/* Validité de chaque argument retenu, avec les limites retenues (listes : toujours valides ; facultatif vide : omis, jamais vérifié) */
	infos = macro_get_arg_infos(gt_macro_set_get_lists(set), action, &n);
	invalid = g_new0(gboolean, MAX(n, 1));
	for(gint k = 0; macro->args != NULL && k < n; k++)
		invalid[k] = g_strcmp0(infos[k].type, "l") != 0 && !(macro->args[k][0] == '\0' && gt_macro_arg_is_optional(macro, k))
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

/* Action de la ligne affichée modifiée : les vecteurs de la ligne ne sont pas touchés (une frappe passe par des états transitoires, l'OK les
 * ajuste par le cœur) ; un préfixe non vide au-delà du nombre d'arguments est signalé (spec §5.1 : « Prefix of argument %d removed », premier
 * rang perdu), le message est retiré quand le nombre revient */
static void prefix_message_update(MacroEditor *editor, GtMacroRow *row)
{
	const gchar *const *pre = gt_macro_row_get_arg_prefix(row);
	GtMacroLists *lists;
	gint n, len = pre != NULL ? (gint)g_strv_length((gchar **)pre) : 0;

	lists = gt_macro_list_rows_to_lists(G_LIST_MODEL(editor->list_rows));
	n = macro_count_format_args(lists, gt_macro_row_get_action(row));
	gt_macro_lists_free(lists);
	for(gint k = n; k < len; k++)
		if(pre[k][0] != '\0')
		{
			gchar *text = g_strdup_printf(_("Prefix of argument %d removed"), k + 1);

			set_message(editor, text, TRUE);
			editor->prefix_message = TRUE;
			g_free(text);
			return;
		}
	if(editor->prefix_message)
		set_message(editor, NULL, FALSE);
}

/* Ligne affichée modifiée : action → message de préfixe perdu, table refaite ; défauts, limites, Optional ou Prefix → aperçu */
static void on_defaults_row_notify(GObject *row, GParamSpec *pspec, gpointer data)
{
	MacroEditor *editor = data;
	const gchar *name = g_param_spec_get_name(pspec);

	if(g_strcmp0(name, "action") == 0)
	{
		prefix_message_update(editor, GT_MACRO_ROW(row));
		defaults_rebuild(editor);
	}
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

/* Optional et Prefix de l'argument k écrits dans la ligne affichée (vecteurs de defaults_n éléments), puis l'action si elle est donnée :
 * dans cet ordre, les longueurs restent cohérentes quand notify::action refait la table (qui relit les vecteurs) */
static void optional_write(MacroEditor *editor, gint k, gboolean optional, const gchar *prefix, const gchar *action)
{
	GtMacroRow *row = g_object_ref(editor->defaults_row);	/* notify::action peut changer la ligne affichée */
	gint n = editor->defaults_n;
	gchar **ropt = vector_resize(gt_macro_row_get_arg_optional(row), n), **rpre = vector_resize(gt_macro_row_get_arg_prefix(row), n);
	gchar **opt = g_new0(gchar *, n + 1), **pre = g_new0(gchar *, n + 1);

	for(gint i = 0; i < n; i++)
	{
		opt[i] = g_strdup(i == k ? (optional ? "1" : "") : nth((const gchar *const *)ropt, i));
		pre[i] = g_strdup(i == k ? prefix : nth((const gchar *const *)rpre, i));
	}
	g_strfreev(ropt);
	g_strfreev(rpre);
	opt = vector_or_null(opt);
	pre = vector_or_null(pre);
	gt_macro_row_set_optional(row, (const gchar *const *)opt, (const gchar *const *)pre);
	if(action != NULL)
		g_object_set(row, "action", action, NULL);
	g_strfreev(opt);
	g_strfreev(pre);
	g_object_unref(row);
}

static gboolean keep_until_idle(gpointer data)
{
	return G_SOURCE_REMOVE;
}

/* Valeur de l'argument k dans la vue (vector_resize) d'un vecteur de la ligne ; g_free */
static gchar *row_nth(const gchar *const *v, gint n, gint k)
{
	gchar **view = vector_resize(v, n);
	gchar *value = g_strdup(nth((const gchar *const *)view, k));

	g_strfreev(view);
	return value;
}

/* Case Optional de l'argument k : cochée, le texte qui précède l'argument passe de l'action au préfixe (spec §5.4) ; décochée, le préfixe
 * revient dans l'action et le champ est vidé */
static void on_optional_toggled(GtkCheckButton *check, gpointer data)
{
	MacroEditor *editor = editor_of(GTK_WIDGET(check));
	gint k = GPOINTER_TO_INT(data);
	GtMacroLists *lists;
	GtkWidget *entry;
	gchar *action = NULL, *prefix = NULL;
	gboolean on = gtk_check_button_get_active(check);

	if(editor == NULL || editor->defaults_building || editor->defaults_row == NULL || k >= editor->defaults_n)
		return;
	lists = gt_macro_list_rows_to_lists(G_LIST_MODEL(editor->list_rows));
	if(on)
	{
		if(!gt_macro_action_split_prefix(lists, gt_macro_row_get_action(editor->defaults_row), k, &action, &prefix))
		{
			gt_macro_lists_free(lists);
			return;
		}
	}
	else
	{
		prefix = row_nth(gt_macro_row_get_arg_prefix(editor->defaults_row), editor->defaults_n, k);
		action = gt_macro_action_join_prefix(lists, gt_macro_row_get_action(editor->defaults_row), k, prefix);
		g_clear_pointer(&prefix, g_free);
	}
	gt_macro_lists_free(lists);
	if(action != NULL && strcmp(action, gt_macro_row_get_action(editor->defaults_row)) == 0)
		g_clear_pointer(&action, g_free);	/* action inchangée : pas de notify::action */
	/* La table se refait pendant cette émission (notify::action, ou ci-dessous) : la case reste vivante jusqu'à la fin de l'événement qui
	 * l'a basculée */
	g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, keep_until_idle, g_object_ref(check), g_object_unref);
	entry = table_cell(editor, COL_PREFIX, k);
	if(entry != NULL)
	{
		editor->defaults_building = TRUE;	/* le préfixe est écrit dans la ligne par optional_write */
		gtk_editable_set_text(GTK_EDITABLE(entry), on ? prefix : "");
		gtk_widget_set_sensitive(entry, on);
		editor->defaults_building = FALSE;
	}
	optional_write(editor, k, on, on ? prefix : "", action);
	/* Action inchangée : la table n'a pas été refaite, la cellule Default du rang k doit suivre l'état facultatif (vide accepté, « (none) ») */
	if(action == NULL && editor->defaults_row != NULL)
	{
		gboolean focused = gtk_widget_has_focus(GTK_WIDGET(check));
		GtkWidget *again;

		defaults_rebuild(editor);
		again = table_cell(editor, COL_OPTIONAL, k);
		if(focused && again != NULL)
			gtk_widget_grab_focus(again);	/* clavier : la nouvelle case garde le focus */
	}
	g_free(action);
	g_free(prefix);
}

/* Prefix modifié à la main : un préfixe est toujours valide, le vecteur de la ligne est réécrit */
static void on_prefix_changed(GtkEditable *editable, gpointer data)
{
	MacroEditor *editor = editor_of(GTK_WIDGET(editable));
	gint k = GPOINTER_TO_INT(data);
	gchar *value;

	if(editor == NULL || editor->defaults_building || editor->defaults_row == NULL || k >= editor->defaults_n)
		return;
	value = row_nth(gt_macro_row_get_arg_optional(editor->defaults_row), editor->defaults_n, k);
	optional_write(editor, k, strcmp(value, "1") == 0, gtk_editable_get_text(editable), NULL);
	g_free(value);
}

/* Cellules Optional (case) et Prefix (entrée, insensible si la case est décochée) de l'argument k */
static void optional_cells_attach(MacroEditor *editor, gint k, gboolean optional, const gchar *prefix)
{
	GtkWidget *check = gtk_check_button_new();
	GtkWidget *entry = gtk_entry_new();

	gtk_check_button_set_active(GTK_CHECK_BUTTON(check), optional);
	gtk_widget_set_halign(check, GTK_ALIGN_CENTER);
	name_field(check, "macro-optional", k);
	g_signal_connect(check, "toggled", G_CALLBACK(on_optional_toggled), GINT_TO_POINTER(k));
	gtk_editable_set_width_chars(GTK_EDITABLE(entry), 12);
	gtk_editable_set_max_width_chars(GTK_EDITABLE(entry), 12);
	gtk_editable_set_text(GTK_EDITABLE(entry), prefix);
	gtk_widget_set_sensitive(entry, optional);
	name_field(entry, "macro-prefix", k);
	g_signal_connect(entry, "changed", G_CALLBACK(on_prefix_changed), GINT_TO_POINTER(k));
	gtk_grid_attach(GTK_GRID(editor->defaults), check, COL_OPTIONAL, k + 1, 1, 1);
	gtk_grid_attach(GTK_GRID(editor->defaults), entry, COL_PREFIX, k + 1, 1, 1);
}

/* Table refaite : sélection, action de la ligne affichée ou listes ont changé */
static void defaults_rebuild(MacroEditor *editor)
{
	guint n = 0;
	guint *pos = selected_store_positions(editor, PAGE_MACROS, &n);
	GtMacroRow *row = n == 1 ? g_list_model_get_item(G_LIST_MODEL(editor->rows), pos[0]) : NULL;
	macro_arg_info_t *infos = NULL;
	GtMacroLists *lists = NULL;
	const gchar *const headers[N_COLS] = { _("#"), _("Format"), _("Type"), _("Default"), _("Min"), _("Max"), _("Charset"), _("Optional"),
	                                         _("Prefix") };
	const gchar *const *args, *const *mins, *const *maxs, *const *css;
	gchar **opts = NULL, **pres = NULL;
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
		/* Autre ligne affichée : le message « Prefix of argument %d removed » de la précédente ne la concerne plus */
		if(editor->prefix_message)
			set_message(editor, NULL, FALSE);
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
		opts = vector_resize(gt_macro_row_get_arg_optional(row), n_args);	/* vues : les vecteurs de la ligne gardent leur longueur jusqu'à l'OK */
		pres = vector_resize(gt_macro_row_get_arg_prefix(row), n_args);
		for(gint c = 0; c < N_COLS; c++)
			gtk_grid_attach(GTK_GRID(editor->defaults), table_header(headers[c]), c, 0, 1, 1);
		for(gint k = 0; k < n_args; k++)
		{
			const gchar *type = infos[k].type;
			gboolean text = g_strcmp0(type, "s") == 0;
			gchar *rank = g_strdup_printf("%d", k + 1);
			GtkWidget *w;
			gboolean optional;

			gtk_grid_attach(GTK_GRID(editor->defaults), table_text(rank), COL_RANK, k + 1, 1, 1);
			g_free(rank);
			gtk_grid_attach(GTK_GRID(editor->defaults), table_text(infos[k].spec), COL_FORMAT, k + 1, 1, 1);
			gtk_grid_attach(GTK_GRID(editor->defaults), table_text(text ? _("text (bytes)") : gt_macro_arg_type_name(type)), COL_TYPE, k + 1, 1, 1);
			optional = g_strcmp0(nth((const gchar *const *)opts, k), "1") == 0;
			/* Défaut d'un argument facultatif : vide accepté malgré les limites, liste avec « (none) » (valeur "") */
			w = gt_macro_arg_widget_new_full(lists, &infos[k], args != NULL ? args[k] : "", NULL, optional);
			gt_macro_arg_widget_set_optional(w, optional, nth((const gchar *const *)pres, k));
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
			optional_cells_attach(editor, k, optional, nth((const gchar *const *)pres, k));
		}
	}
	gtk_widget_set_visible(editor->defaults_scroll, n_args > 0);
	g_strfreev(opts);
	g_strfreev(pres);
	if(infos != NULL)
		macro_arg_infos_free(infos, n_args);
	if(lists != NULL)
		gt_macro_lists_free(lists);
	g_clear_object(&row);
	editor->defaults_building = FALSE;
	defaults_commit(editor, FALSE);	/* champs vérifiés avec les limites affichées, sans rien écrire */
	preview_update(editor);	/* et visibilité du séparateur */
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

/* Ligne « Tab: » / « List: » et sa liste déroulante de filtre (spec §3), au-dessus du tableau */
static GtkWidget *build_filter_box(MacroEditor *editor, gint page)
{
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	GtkWidget *label = gtk_label_new(page == PAGE_MACROS ? _("Tab:") : _("List:"));
	GtkWidget *dropdown = gtk_drop_down_new(NULL, NULL);

	gtk_widget_set_name(dropdown, page == PAGE_MACROS ? "macro-tab-filter" : "lists-filter");
	gtk_accessible_update_property(GTK_ACCESSIBLE(dropdown), GTK_ACCESSIBLE_PROPERTY_LABEL, page == PAGE_MACROS ? _("Tab filter") : _("List filter"), -1);
	editor->filter_dd[page] = dropdown;
	g_object_add_weak_pointer(G_OBJECT(dropdown), (gpointer *)&editor->filter_dd[page]);
	filter_keys_rebuild(editor, page);	/* modèle : « All » et les clés du store */
	g_signal_connect(dropdown, "notify::selected", G_CALLBACK(on_filter_selected), GINT_TO_POINTER(page));
	gtk_box_append(GTK_BOX(box), label);
	gtk_box_append(GTK_BOX(box), dropdown);
	return box;
}

/* Page : filtre ; tableau (vue filtrée) ; boutons juste dessous ; (Macros) séparateur, marge de 12 px, table des arguments et aperçu (spec §6) */
static GtkWidget *build_page(MacroEditor *editor, gint page)
{
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	GtkWidget *scrolled = gtk_scrolled_window_new();
	GtkWidget *row1 = button_row(), *row2 = button_row();
	GtkSelectionModel *selection;
	GtkWidget *view;

	editor->filter[page] = gtk_custom_filter_new(page == PAGE_MACROS ? macro_matches : list_matches, editor, NULL);
	editor->filtered[page] = gtk_filter_list_model_new(G_LIST_MODEL(g_object_ref(page_store(editor, page))), GTK_FILTER(g_object_ref(editor->filter[page])));
	selection = GTK_SELECTION_MODEL(gtk_multi_selection_new(G_LIST_MODEL(g_object_ref(editor->filtered[page]))));
	view = gtk_column_view_new(selection);	/* prend la sélection */
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
	gtk_box_append(GTK_BOX(box), build_filter_box(editor, page));
	gtk_box_append(GTK_BOX(box), scrolled);
	gtk_box_append(GTK_BOX(box), row1);
	gtk_box_append(GTK_BOX(box), row2);
	/* Clés du filtre suivies après la vue filtrée : elle a déjà traité le changement quand elles sont recalculées */
	g_signal_connect(page_store(editor, page), "items-changed", G_CALLBACK(on_filter_items_changed), editor);
	if(page == PAGE_MACROS)
	{
		GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
		GtkWidget *settings = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

		gtk_widget_set_name(sep, "macro-settings-separator");
		gtk_accessible_update_property(GTK_ACCESSIBLE(sep), GTK_ACCESSIBLE_PROPERTY_LABEL, "macro-settings-separator", -1);
		gtk_widget_set_margin_top(sep, 4);
		gtk_widget_set_margin_top(settings, 12);
		gtk_box_append(GTK_BOX(settings), build_defaults_box(editor));	/* pointeurs faibles posés par build_defaults_box */
		gtk_box_append(GTK_BOX(settings), build_preview_box(editor));
		gtk_box_append(GTK_BOX(box), sep);
		gtk_box_append(GTK_BOX(box), settings);
		editor->settings_separator = sep;
		editor->settings_box = settings;
		g_object_add_weak_pointer(G_OBJECT(sep), (gpointer *)&editor->settings_separator);
		g_object_add_weak_pointer(G_OBJECT(settings), (gpointer *)&editor->settings_box);
		settings_visibility_update(editor);
		g_signal_connect(selection, "selection-changed", G_CALLBACK(on_defaults_selection_changed), editor);
		g_signal_connect(editor->rows, "items-changed", G_CALLBACK(on_defaults_items_changed), editor);
		g_signal_connect(editor->list_rows, "items-changed", G_CALLBACK(on_defaults_items_changed), editor);
	}
	return box;
}

void gt_macro_editor_open(GtkWindow *parent, GtSession *s)
{
	GtkWidget *vbox, *stack, *switcher, *bottom, *open, *source;
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
	gtk_window_set_default_size(GTK_WINDOW(editor->window), 900, 640);	/* place pour la table des arguments et l'aperçu */
	if(parent != NULL)
	{
		gtk_window_set_transient_for(GTK_WINDOW(editor->window), parent);
		gtk_window_set_destroy_with_parent(GTK_WINDOW(editor->window), TRUE);
	}
	g_signal_connect(editor->window, "close-request", G_CALLBACK(on_close_request), NULL);
	g_signal_connect(editor->window, "destroy", G_CALLBACK(on_destroy), NULL);
	g_signal_connect_object(s, "closed", G_CALLBACK(on_session_closed), editor->window, 0);
	g_signal_connect_object(s, "edited", G_CALLBACK(on_session_edited), editor->window, 0);
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
	regroup(editor, PAGE_MACROS);	/* jeu déjà regroupé à la lecture : sans effet, sauf jeu ancien resté en mémoire */
	regroup(editor, PAGE_LISTS);
	switcher = gtk_stack_switcher_new();
	gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher), GTK_STACK(stack));
	gtk_widget_set_halign(switcher, GTK_ALIGN_CENTER);
	editor->message = gtk_label_new(NULL);
	gtk_widget_set_name(editor->message, "macro-editor-error");
	gtk_label_set_wrap(GTK_LABEL(editor->message), TRUE);
	gtk_label_set_xalign(GTK_LABEL(editor->message), 0.0);
	gtk_widget_set_visible(editor->message, FALSE);
	source = build_source_box(editor);
	source_update(editor, gt_session_get_macros(s));
	bottom = button_row();
	add_button(bottom, _("Help"), "macro-help", G_CALLBACK(on_help), 0);
	add_button(bottom, _("Cancel"), "macro-cancel", G_CALLBACK(on_cancel), 0);
	add_button(bottom, _("OK"), "macro-ok", G_CALLBACK(on_ok), 0);
	gtk_box_append(GTK_BOX(vbox), source);
	gtk_box_append(GTK_BOX(vbox), switcher);
	gtk_box_append(GTK_BOX(vbox), stack);
	gtk_box_append(GTK_BOX(vbox), editor->message);
	gtk_box_append(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
	gtk_box_append(GTK_BOX(vbox), bottom);
	gtk_window_set_child(GTK_WINDOW(editor->window), vbox);
	gtk_window_present(GTK_WINDOW(editor->window));
}

/* Comparaison de pointeurs sûre : gt_macros_replace_set crée le nouveau jeu avant de libérer l'ancien et appelle ce crochet à chaque
 * remplacement ; editor->set n'est jamais déréférencé une fois différent du jeu courant. Pendant un Reload de cet éditeur, les lignes sont
 * refaites depuis le jeu relu (remplacé ou modifié en place) et la fenêtre reste ouverte. */
void gt_macro_editor_macros_changed(GtSession *s)
{
	GtkWidget *window = s != NULL ? g_object_get_data(G_OBJECT(s), SESSION_EDITOR_KEY) : NULL;
	MacroEditor *editor = window != NULL ? g_object_get_data(G_OBJECT(window), EDITOR_KEY) : NULL;

	if(editor != NULL && editor->reloading)
		editor_reload_rows(editor, gt_macros_get_set(gt_session_get_macros(s)));
	else if(editor != NULL && gt_macros_get_set(gt_session_get_macros(s)) != editor->set)
		gtk_window_destroy(GTK_WINDOW(window));	/* sans close-request : rien n'est appliqué ni enregistré */
	else if(editor != NULL)
		source_update(editor, gt_session_get_macros(s));	/* lien détaché ou rafraîchi */
}

void gt_macro_editor_close(GtSession *s)
{
	GtkWidget *window = s != NULL ? g_object_get_data(G_OBJECT(s), SESSION_EDITOR_KEY) : NULL;

	if(window != NULL)
		close_editor_window(window);
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
