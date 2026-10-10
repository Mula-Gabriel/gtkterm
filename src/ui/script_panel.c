/* Panneau de script Lua (sous-projet 6a), GtkSourceView 5. Le moteur (script_engine.c) ne touche jamais GTK : ce module est sa seule frontière. */
#include <config.h>
#include <string.h>
#include <glib/gi18n.h>
#include <gtksourceview/gtksource.h>

#include "app.h"
#include "app_config.h"
#include "config_store.h"
#include "dialogs.h"
#include "script_engine.h"
#include "script_panel.h"
#include "terminal_view.h"	/* gt_color_to_rgba */

#define CONSOLE_MAX_LINES 10000
#define SCHEME_ID "gtkterm-custom"

enum { SC_BG, SC_FG, SC_COMMENT, SC_KEYWORD, SC_STRING, SC_NUMBER, SC_FUNCTION, SC_LINENO_FG, SC_LINENO_BG, SC_N };
static const char *const sc_keys[SC_N] = { "bg", "fg", "comment", "keyword", "string", "number", "function", "lineno_fg", "lineno_bg" };
static const char *const sc_labels[SC_N] = { N_("Editor background"), N_("Default text"), N_("Comments"), N_("Keywords"), N_("Strings"), N_("Numbers"),
                                              N_("Functions"), N_("Line numbers"), N_("Line numbers background") };

typedef struct
{
	GdkRGBA c[SC_N];	/* couleurs effectives */
	gboolean custom[SC_N];	/* choisie par l'utilisateur (écrite dans [colors]) */
} ScriptColors;

typedef struct
{
	GtkWidget *panel;	/* pointeur faible */
	GtkWidget *editor, *console, *run, *stop, *paned, *editor_scroll, *console_scroll, *layout_popover;
	GtkWidget *colors_window;	/* pointeur faible, instance unique */
	GtkWidget *color_buttons[SC_N];
	ScriptColors colors_base;	/* état montré par la fenêtre des couleurs (ouverture : colors ; après Reset : rien de choisi) */
	GSimpleAction *layout_action;
	GtkOrientation orientation;	/* VERTICAL = éditeur en haut (« topbottom ») */
	gint pos_topbottom, pos_leftright, pending_pos;
	gchar *script_path;
	ScriptColors colors;
} ScriptPanel;

static ScriptPanel s;

static const char DEFAULT_SCRIPT[] =
	"-- GTKTerm Lua script\n"
	"-- gtkterm.send(handle, data)\n"
	"-- gtkterm.wait_for(handle, pattern, timeout_ms)\n"
	"-- gtkterm.log(text)\n"
	"-- handle 0 = main serial port\n\n"
	"gtkterm.log(\"Script ready\")\n";

/* ---- couleurs : logique pure ---- */

static void term_colors(GdkRGBA *bg, GdkRGBA *fg)
{
	gt_color_to_rgba(&config.background, bg);	/* terminal_view.h ; schéma sans transparence */
	gt_color_to_rgba(&config.foreground, fg);
	bg->alpha = fg->alpha = 1.0f;
}

static gchar *rgba_to_hex(const GdkRGBA *c)
{
	return g_strdup_printf("#%02x%02x%02x", (guint)(CLAMP(c->red, 0.0f, 1.0f) * 255.0f + 0.5f), (guint)(CLAMP(c->green, 0.0f, 1.0f) * 255.0f + 0.5f),
	                       (guint)(CLAMP(c->blue, 0.0f, 1.0f) * 255.0f + 0.5f));
}

static gboolean same_color(const GdkRGBA *a, const GdkRGBA *b)
{
	gchar *x = rgba_to_hex(a);
	gchar *y = rgba_to_hex(b);
	gboolean same = g_strcmp0(x, y) == 0;

	g_free(x);
	g_free(y);
	return same;
}

/* def[SC_BG] et def[SC_FG] sont donnés ; remplit les 7 autres : palette Tango sombre ou claire selon la luminance du fond (comme GTK 3) */
static void script_colors_defaults(GdkRGBA def[SC_N])
{
	static const char *const dark[6] = { "#888a85", "#729fcf", "#8ae234", "#ad7fa8", "#edd400", "#888a85" };
	static const char *const light[6] = { "#808080", "#204a87", "#4e9a06", "#5c3566", "#8f5902", "#888a85" };
	const GdkRGBA bg = def[SC_BG];
	float lum = 0.299f * bg.red + 0.587f * bg.green + 0.114f * bg.blue;
	const char *const *palette = lum < 0.5f ? dark : light;
	float k = lum < 0.5f ? 0.80f : 1.08f;

	for(int i = 0; i < 6; i++)
		gdk_rgba_parse(&def[SC_COMMENT + i], palette[i]);	/* SC_COMMENT … SC_LINENO_FG sont consécutifs */
	def[SC_LINENO_BG] = bg;
	def[SC_LINENO_BG].red = MIN(1.0f, bg.red * k);
	def[SC_LINENO_BG].green = MIN(1.0f, bg.green * k);
	def[SC_LINENO_BG].blue = MIN(1.0f, bg.blue * k);
}

/* Couleurs effectives : les couleurs choisies restent, les autres suivent le terminal (fond, texte) et la palette calculée sur le fond retenu */
static void script_colors_resolve(ScriptColors *sc, const GdkRGBA *term_bg, const GdkRGBA *term_fg)
{
	GdkRGBA def[SC_N];

	def[SC_BG] = sc->custom[SC_BG] ? sc->c[SC_BG] : *term_bg;
	def[SC_FG] = sc->custom[SC_FG] ? sc->c[SC_FG] : *term_fg;
	script_colors_defaults(def);
	for(int i = 0; i < SC_N; i++)
		if(!sc->custom[i])
			sc->c[i] = def[i];
}

/* Valeurs proposées (dialogue ou fichier) : une couleur est choisie si elle diffère de sa valeur par défaut (fond et texte : terminal ;
 * autres : palette calculée sur le fond et le texte proposés) */
static void script_colors_from_values(ScriptColors *sc, const GdkRGBA values[SC_N], const GdkRGBA *term_bg, const GdkRGBA *term_fg)
{
	GdkRGBA def[SC_N];

	def[SC_BG] = values[SC_BG];
	def[SC_FG] = values[SC_FG];
	script_colors_defaults(def);
	def[SC_BG] = *term_bg;
	def[SC_FG] = *term_fg;
	for(int i = 0; i < SC_N; i++)
	{
		sc->c[i] = values[i];
		sc->custom[i] = !same_color(&values[i], &def[i]);
	}
}

/* Apply de la fenêtre des couleurs (règle 9) : seules les couleurs modifiées dans la fenêtre (différentes de base) sont réévaluées — choisies si elles
 * diffèrent de leur valeur par défaut (fond, texte : terminal ; autres : palette du fond retenu) ; les autres gardent l'état de base, et celles qui ne
 * sont pas choisies suivent le nouveau fond (lineno_bg n'est pas écrit parce que le fond a changé). */
static void script_colors_apply_edits(ScriptColors *sc, const ScriptColors *base, const GdkRGBA values[SC_N], const GdkRGBA *term_bg,
                                      const GdkRGBA *term_fg)
{
	gboolean changed[SC_N];
	GdkRGBA def[SC_N];

	*sc = *base;
	for(int i = 0; i < SC_N; i++)
	{
		changed[i] = !same_color(&values[i], &base->c[i]);
		if(changed[i])
		{
			sc->c[i] = values[i];
			sc->custom[i] = TRUE;
		}
	}
	def[SC_BG] = sc->custom[SC_BG] ? sc->c[SC_BG] : *term_bg;
	def[SC_FG] = sc->custom[SC_FG] ? sc->c[SC_FG] : *term_fg;
	script_colors_defaults(def);
	def[SC_BG] = *term_bg;
	def[SC_FG] = *term_fg;
	for(int i = 0; i < SC_N; i++)
		if(changed[i])
			sc->custom[i] = !same_color(&values[i], &def[i]);
	script_colors_resolve(sc, term_bg, term_fg);
}

/* Lecture de [colors] (règle 8) : clé présente et différente de sa valeur par défaut = choisie */
static void script_colors_load(ScriptColors *sc, GKeyFile *kf, const GdkRGBA *term_bg, const GdkRGBA *term_fg)
{
	ScriptColors def = { 0 };
	GdkRGBA values[SC_N];
	gboolean present[SC_N];

	script_colors_resolve(&def, term_bg, term_fg);	/* rien de choisi : valeurs par défaut */
	for(int i = 0; i < SC_N; i++)
	{
		gchar *v = g_key_file_get_string(kf, "colors", sc_keys[i], NULL);

		present[i] = v != NULL && gdk_rgba_parse(&values[i], v);
		if(!present[i])
			values[i] = def.c[i];
		g_free(v);
	}
	script_colors_from_values(sc, values, term_bg, term_fg);
	for(int i = 0; i < SC_N; i++)
		if(!present[i])
			sc->custom[i] = FALSE;
	script_colors_resolve(sc, term_bg, term_fg);
}

/* Écriture de [colors] : clé écrite si choisie, retirée sinon */
static void script_colors_store(const ScriptColors *sc, GKeyFile *kf)
{
	for(int i = 0; i < SC_N; i++)
	{
		if(sc->custom[i])
		{
			gchar *v = gdk_rgba_to_string(&sc->c[i]);

			g_key_file_set_string(kf, "colors", sc_keys[i], v);
			g_free(v);
		}
		else
			g_key_file_remove_key(kf, "colors", sc_keys[i], NULL);
	}
}

static gchar *script_scheme_xml(const ScriptColors *sc)
{
	gchar *h[SC_N];
	gchar *xml;

	for(int i = 0; i < SC_N; i++)
		h[i] = rgba_to_hex(&sc->c[i]);
	xml = g_strdup_printf("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	                      "<style-scheme id=\"" SCHEME_ID "\" name=\"GTKTerm Custom\" version=\"1.0\">\n"
	                      "  <style name=\"text\" foreground=\"%s\" background=\"%s\"/>\n"
	                      "  <style name=\"line-numbers\" foreground=\"%s\" background=\"%s\"/>\n"
	                      "  <style name=\"current-line-number\" foreground=\"%s\" background=\"%s\"/>\n"
	                      "  <style name=\"selection\" foreground=\"%s\" background=\"%s\"/>\n"
	                      "  <style name=\"def:comment\" foreground=\"%s\" italic=\"true\"/>\n"
	                      "  <style name=\"def:shebang\" foreground=\"%s\" bold=\"true\"/>\n"
	                      "  <style name=\"def:keyword\" foreground=\"%s\" bold=\"true\"/>\n"
	                      "  <style name=\"def:operator\" foreground=\"%s\"/>\n"
	                      "  <style name=\"def:string\" foreground=\"%s\"/>\n"
	                      "  <style name=\"def:number\" foreground=\"%s\"/>\n"
	                      "  <style name=\"def:floating-point\" foreground=\"%s\"/>\n"
	                      "  <style name=\"def:function\" foreground=\"%s\"/>\n"
	                      "  <style name=\"lua:function\" foreground=\"%s\"/>\n"
	                      "  <style name=\"def:identifier\" foreground=\"%s\"/>\n"
	                      "</style-scheme>\n",
	                      h[SC_FG], h[SC_BG], h[SC_LINENO_FG], h[SC_LINENO_BG], h[SC_FG], h[SC_LINENO_BG], h[SC_BG], h[SC_FG], h[SC_COMMENT], h[SC_COMMENT],
	                      h[SC_KEYWORD], h[SC_KEYWORD], h[SC_STRING], h[SC_NUMBER], h[SC_NUMBER], h[SC_FUNCTION], h[SC_FUNCTION], h[SC_FG]);
	for(int i = 0; i < SC_N; i++)
		g_free(h[i]);
	return xml;
}

/* Règle 10 : dossier propre à GTKTerm, ajouté une fois en tête du chemin de recherche */
static void apply_scheme(void)
{
	static gboolean path_added = FALSE;
	GtkSourceStyleSchemeManager *sm = gtk_source_style_scheme_manager_get_default();
	gchar *dir = g_build_filename(g_get_user_data_dir(), "gtkterm", "styles", NULL);
	gchar *file = g_build_filename(dir, SCHEME_ID ".xml", NULL);
	gchar *xml = script_scheme_xml(&s.colors);

	if(!path_added)
	{
		gtk_source_style_scheme_manager_prepend_search_path(sm, dir);
		path_added = TRUE;
	}
	if(g_mkdir_with_parents(dir, 0700) == 0 && g_file_set_contents(file, xml, -1, NULL))
	{
		GtkSourceStyleScheme *scheme;

		gtk_source_style_scheme_manager_force_rescan(sm);
		scheme = gtk_source_style_scheme_manager_get_scheme(sm, SCHEME_ID);
		if(scheme != NULL && s.editor != NULL)
			gtk_source_buffer_set_style_scheme(GTK_SOURCE_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(s.editor))), scheme);
		if(scheme != NULL && s.console != NULL)
			gtk_source_buffer_set_style_scheme(GTK_SOURCE_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(s.console))), scheme);
	}
	g_free(xml);
	g_free(file);
	g_free(dir);
}

/* ---- console et moteur ---- */

static void console_append(const char *text, gpointer user_data)
{
	GtkTextBuffer *buf;
	GtkTextIter end;
	gint lines;

	if(s.console == NULL)
		return;
	buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(s.console));
	gtk_text_buffer_get_end_iter(buf, &end);
	gtk_text_buffer_insert(buf, &end, text, -1);
	lines = gtk_text_buffer_get_line_count(buf);
	if(lines > CONSOLE_MAX_LINES)
	{
		GtkTextIter start, cut;

		gtk_text_buffer_get_start_iter(buf, &start);
		gtk_text_buffer_get_iter_at_line(buf, &cut, lines - CONSOLE_MAX_LINES);
		gtk_text_buffer_delete(buf, &start, &cut);
	}
	gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(s.console), gtk_text_buffer_get_mark(buf, "console-end"));
}

static void on_script_done(GtScriptEnd end, gpointer user_data)
{
	if(s.panel == NULL)
		return;
	gtk_widget_set_sensitive(s.run, TRUE);
	gtk_widget_set_sensitive(s.stop, FALSE);
	switch(end)
	{
		case GT_SCRIPT_END_STOPPED:
			console_append(_("[Script stopped]\n"), NULL);
			break;
		case GT_SCRIPT_END_ERROR:
			console_append(_("[Script finished with an error]\n"), NULL);
			break;
		default:
			console_append(_("[Script finished]\n"), NULL);
			break;
	}
}

/* ---- barre d'outils (règles 4 et 5) ---- */

static GtkTextBuffer *editor_buffer(void)
{
	return gtk_text_view_get_buffer(GTK_TEXT_VIEW(s.editor));
}

static void on_run_clicked(GtkButton *b, gpointer user_data)
{
	GtkTextBuffer *buf = editor_buffer();
	GtkTextIter start, end;
	gchar *text;

	gtk_text_buffer_get_bounds(buf, &start, &end);
	text = gtk_text_buffer_get_text(buf, &start, &end, FALSE);
	gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(s.console)), "", -1);	/* la marque console-end survit */
	gtk_widget_set_sensitive(s.run, FALSE);
	gtk_widget_set_sensitive(s.stop, TRUE);
	script_engine_run(text);
	g_free(text);
}

static void on_stop_clicked(GtkButton *b, gpointer user_data)
{
	script_engine_stop();
}

static void show_error(const char *what, const GError *err)
{
	gchar *msg = g_strdup_printf("%s : %s", what, err != NULL ? err->message : "");

	gt_ui_message(gt_app_get_main_window(), GT_APP_MSG_ERROR, msg);
	g_free(msg);
}

static void on_open_chosen(GFile *file, gpointer user_data)
{
	gchar *contents = NULL;
	gsize len = 0;
	GError *err = NULL;
	gchar *path;

	if(s.panel == NULL || file == NULL)
		return;
	if(!g_file_load_contents(file, NULL, &contents, &len, NULL, &err))
	{
		show_error(_("Cannot read file"), err);
		g_clear_error(&err);
		return;
	}
	if(!g_utf8_validate(contents, (gssize)len, NULL))
	{
		GError *e2 = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "not valid UTF-8");

		show_error(_("Cannot read file"), e2);
		g_error_free(e2);
		g_free(contents);
		return;
	}
	gtk_text_buffer_set_text(editor_buffer(), contents, (gint)len);
	g_free(contents);
	path = g_file_get_path(file);
	if(path != NULL)
	{
		g_free(s.script_path);
		s.script_path = path;
	}
	gt_script_panel_save_state();
}

static void on_open_clicked(GtkButton *b, gpointer user_data)
{
	GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
	GtkFileFilter *f = gtk_file_filter_new();
	GFile *initial = s.script_path != NULL ? g_file_new_for_path(s.script_path) : NULL;

	gtk_file_filter_set_name(f, _("Lua scripts (*.lua)"));
	gtk_file_filter_add_pattern(f, "*.lua");
	g_list_store_append(filters, f);
	g_object_unref(f);
	f = gtk_file_filter_new();
	gtk_file_filter_set_name(f, _("All files"));
	gtk_file_filter_add_pattern(f, "*");
	g_list_store_append(filters, f);
	g_object_unref(f);
	gt_ui_open_file(gt_app_get_main_window(), _("Open Lua Script"), initial, G_LIST_MODEL(filters), on_open_chosen, NULL);
	g_object_unref(filters);
	g_clear_object(&initial);
}

static void on_save_chosen(GFile *file, gpointer user_data)
{
	GtkTextBuffer *buf;
	GtkTextIter start, end;
	gchar *text, *path;
	GError *err = NULL;

	if(s.panel == NULL || file == NULL)
		return;
	path = g_file_get_path(file);
	if(path == NULL)
		return;
	buf = editor_buffer();
	gtk_text_buffer_get_bounds(buf, &start, &end);
	text = gtk_text_buffer_get_text(buf, &start, &end, FALSE);
	if(g_file_set_contents(path, text, -1, &err))
	{
		g_free(s.script_path);
		s.script_path = path;
		path = NULL;
		gt_script_panel_save_state();
	}
	else
	{
		show_error(_("Cannot write file"), err);
		g_clear_error(&err);
	}
	g_free(text);
	g_free(path);
}

static void on_save_clicked(GtkButton *b, gpointer user_data)
{
	GFile *folder = NULL;
	gchar *name = NULL;

	if(s.script_path != NULL)
	{
		GFile *f = g_file_new_for_path(s.script_path);

		folder = g_file_get_parent(f);
		name = g_file_get_basename(f);
		g_object_unref(f);
	}
	gt_ui_save_file(gt_app_get_main_window(), _("Save Lua Script"), folder, name != NULL ? name : "script.lua", on_save_chosen, NULL);
	g_clear_object(&folder);
	g_free(name);
}

static void colors_window_open(void);

static void on_colors_clicked(GtkButton *b, gpointer user_data)
{
	colors_window_open();
}

static GtkWidget *tool_button(const char *icon, const char *label, const char *name, GCallback cb)
{
	GtkWidget *b = gtk_button_new();
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

	gtk_box_append(GTK_BOX(box), gtk_image_new_from_icon_name(icon));
	gtk_box_append(GTK_BOX(box), gtk_label_new(label));
	gtk_button_set_child(GTK_BUTTON(b), box);
	gtk_button_set_has_frame(GTK_BUTTON(b), FALSE);
	gtk_widget_set_name(b, name);
	gtk_accessible_update_property(GTK_ACCESSIBLE(b), GTK_ACCESSIBLE_PROPERTY_LABEL, label, -1);
	g_signal_connect(b, "clicked", cb, NULL);
	return b;
}

/* ---- disposition (règle 6) ---- */

static gint *pos_slot(GtkOrientation o)
{
	return o == GTK_ORIENTATION_VERTICAL ? &s.pos_topbottom : &s.pos_leftright;
}

static void on_max_position(GObject *paned, GParamSpec *pspec, gpointer user_data)
{
	gint max;

	if(s.pending_pos <= 0)
		return;
	g_object_get(paned, "max-position", &max, NULL);
	if(max <= 0)
		return;
	gtk_paned_set_position(GTK_PANED(paned), MIN(s.pending_pos, max));
	s.pending_pos = 0;
}

static void on_layout_change_state(GSimpleAction *a, GVariant *value, gpointer user_data)
{
	const gchar *v = g_variant_get_string(value, NULL);
	GtkOrientation o;
	gint pos, max;

	if(s.panel == NULL)
		return;
	if(g_strcmp0(v, "topbottom") == 0)
		o = GTK_ORIENTATION_VERTICAL;
	else if(g_strcmp0(v, "leftright") == 0)
		o = GTK_ORIENTATION_HORIZONTAL;
	else
		return;
	if(o == s.orientation)
	{
		g_simple_action_set_state(a, value);
		return;
	}
	pos = gtk_paned_get_position(GTK_PANED(s.paned));
	if(pos > 0 && s.pending_pos <= 0)
		*pos_slot(s.orientation) = pos;
	s.orientation = o;
	gtk_orientable_set_orientation(GTK_ORIENTABLE(s.paned), o);
	pos = *pos_slot(o);
	g_object_get(s.paned, "max-position", &max, NULL);
	if(pos > 0)
	{
		if(max > 0)
		{
			gtk_paned_set_position(GTK_PANED(s.paned), MIN(pos, max));
			s.pending_pos = 0;
		}
		else
			s.pending_pos = pos;
	}
	else
		s.pending_pos = 0;
	g_simple_action_set_state(a, value);
	gt_script_panel_save_state();
}

static void on_secondary_click(GtkGestureClick *g, gint n_press, gdouble x, gdouble y, gpointer user_data)
{
	GtkWidget *src = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g));
	graphene_point_t in = GRAPHENE_POINT_INIT((float)x, (float)y), out;
	GdkRectangle r;

	if(s.panel == NULL)
		return;
	if(src == s.paned)
	{
		GtkWidget *hit = gtk_widget_pick(src, x, y, GTK_PICK_DEFAULT);

		if(hit != NULL && (hit == s.editor_scroll || hit == s.console_scroll || gtk_widget_is_ancestor(hit, s.editor_scroll) ||
		                   gtk_widget_is_ancestor(hit, s.console_scroll)))
			return;
	}
	if(!gtk_widget_compute_point(src, s.panel, &in, &out))
		return;
	r.x = (int)out.x;
	r.y = (int)out.y;
	r.width = r.height = 1;
	gtk_popover_set_pointing_to(GTK_POPOVER(s.layout_popover), &r);
	gtk_popover_popup(GTK_POPOVER(s.layout_popover));
	gtk_gesture_set_state(GTK_GESTURE(g), GTK_EVENT_SEQUENCE_CLAIMED);
}

static void add_secondary_click(GtkWidget *w)
{
	GtkGesture *g = gtk_gesture_click_new();

	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(g), GDK_BUTTON_SECONDARY);
	gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(g), GTK_PHASE_BUBBLE);
	g_signal_connect(g, "pressed", G_CALLBACK(on_secondary_click), NULL);
	gtk_widget_add_controller(w, GTK_EVENT_CONTROLLER(g));
}

static GtkWidget *layout_popover_new(void)
{
	GMenu *menu = g_menu_new();
	GMenuItem *item = g_menu_item_new(_("Editor on top"), NULL);
	GtkWidget *popover;

	g_menu_item_set_action_and_target_value(item, "app.script-layout", g_variant_new_string("topbottom"));
	g_menu_append_item(menu, item);
	g_object_unref(item);
	item = g_menu_item_new(_("Editor on the left"), NULL);
	g_menu_item_set_action_and_target_value(item, "app.script-layout", g_variant_new_string("leftright"));
	g_menu_append_item(menu, item);
	g_object_unref(item);
	popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
	g_object_unref(menu);
	gtk_widget_set_name(popover, "script-layout-popover");
	gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
	return popover;
}

/* ---- fenêtre des couleurs (règle 9) ---- */

static void colors_buttons_set(const ScriptColors *sc)
{
	for(int i = 0; i < SC_N; i++)
		if(s.color_buttons[i] != NULL)
			gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(s.color_buttons[i]), &sc->c[i]);
}

static void on_colors_reset(GtkButton *b, gpointer user_data)
{
	GdkRGBA bg, fg;

	memset(&s.colors_base, 0, sizeof s.colors_base);
	term_colors(&bg, &fg);
	script_colors_resolve(&s.colors_base, &bg, &fg);
	colors_buttons_set(&s.colors_base);
}

static void on_colors_cancel(GtkButton *b, gpointer user_data)
{
	gtk_window_destroy(GTK_WINDOW(user_data));
}

static void on_colors_apply(GtkButton *b, gpointer user_data)
{
	GdkRGBA bg, fg, values[SC_N];

	for(int i = 0; i < SC_N; i++)
	{
		if(s.color_buttons[i] == NULL)
			return;
		values[i] = *gtk_color_dialog_button_get_rgba(GTK_COLOR_DIALOG_BUTTON(s.color_buttons[i]));
		values[i].alpha = 1.0f;
	}
	term_colors(&bg, &fg);
	script_colors_apply_edits(&s.colors, &s.colors_base, values, &bg, &fg);
	apply_scheme();
	gt_script_panel_save_state();
	gtk_window_destroy(GTK_WINDOW(user_data));
}

static void on_colors_window_destroy(GtkWidget *w, gpointer user_data)
{
	for(int i = 0; i < SC_N; i++)
		s.color_buttons[i] = NULL;
}

static void colors_window_open(void)
{
	GtkWidget *win, *grid, *row, *reset, *cancel, *apply;

	if(s.panel == NULL)
		return;
	if(s.colors_window != NULL)
	{
		gtk_window_present(GTK_WINDOW(s.colors_window));
		return;
	}
	s.colors_base = s.colors;
	win = gtk_window_new();
	gtk_widget_set_name(win, "script-colors-window");
	gtk_window_set_title(GTK_WINDOW(win), _("Script Editor Colors"));
	gtk_window_set_transient_for(GTK_WINDOW(win), gt_app_get_main_window());
	gtk_window_set_modal(GTK_WINDOW(win), TRUE);
	gtk_window_set_destroy_with_parent(GTK_WINDOW(win), TRUE);
	gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
	s.colors_window = win;
	g_object_add_weak_pointer(G_OBJECT(win), (gpointer *)&s.colors_window);
	g_signal_connect(win, "destroy", G_CALLBACK(on_colors_window_destroy), NULL);

	grid = gtk_grid_new();
	gtk_widget_set_margin_top(grid, 16);
	gtk_widget_set_margin_bottom(grid, 16);
	gtk_widget_set_margin_start(grid, 16);
	gtk_widget_set_margin_end(grid, 16);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 16);
	for(int i = 0; i < SC_N; i++)
	{
		GtkWidget *label = gtk_label_new(_(sc_labels[i]));
		GtkColorDialog *d = gtk_color_dialog_new();
		gchar *name = g_strdup_printf("script-color-%s", sc_keys[i]);

		gtk_widget_set_halign(label, GTK_ALIGN_START);
		gtk_color_dialog_set_with_alpha(d, FALSE);
		gtk_color_dialog_set_title(d, _(sc_labels[i]));
		s.color_buttons[i] = gtk_color_dialog_button_new(d);
		gtk_widget_set_name(s.color_buttons[i], name);
		g_free(name);
		gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(s.color_buttons[i]), &s.colors.c[i]);
		gtk_grid_attach(GTK_GRID(grid), label, 0, i, 1, 1);
		gtk_grid_attach(GTK_GRID(grid), s.color_buttons[i], 1, i, 1, 1);
	}
	row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_halign(row, GTK_ALIGN_END);
	gtk_widget_set_margin_top(row, 8);
	reset = gtk_button_new_with_mnemonic(_("_Reset"));
	gtk_widget_set_name(reset, "script-colors-reset");
	g_signal_connect(reset, "clicked", G_CALLBACK(on_colors_reset), NULL);
	cancel = gtk_button_new_with_mnemonic(_("_Cancel"));
	gtk_widget_set_name(cancel, "script-colors-cancel");
	g_signal_connect(cancel, "clicked", G_CALLBACK(on_colors_cancel), win);
	apply = gtk_button_new_with_mnemonic(_("_Apply"));
	gtk_widget_set_name(apply, "script-colors-apply");
	g_signal_connect(apply, "clicked", G_CALLBACK(on_colors_apply), win);
	gtk_box_append(GTK_BOX(row), reset);
	gtk_box_append(GTK_BOX(row), cancel);
	gtk_box_append(GTK_BOX(row), apply);
	gtk_grid_attach(GTK_GRID(grid), row, 0, SC_N, 2, 1);
	gtk_window_set_child(GTK_WINDOW(win), grid);
	gt_ui_close_on_escape(GTK_WINDOW(win));
	gtk_window_present(GTK_WINDOW(win));
}

/* ---- état ---- */

static void layout_load(GKeyFile *kf)
{
	gchar *o = g_key_file_get_string(kf, "layout", "orientation", NULL);
	gint v;

	s.orientation = g_strcmp0(o, "leftright") == 0 ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;
	g_free(o);
	v = g_key_file_get_integer(kf, "layout", "pos_topbottom", NULL);
	s.pos_topbottom = v > 0 ? v : 0;
	v = g_key_file_get_integer(kf, "layout", "pos_leftright", NULL);
	s.pos_leftright = v > 0 ? v : 0;
	g_free(s.script_path);
	s.script_path = g_key_file_get_string(kf, "session", "script_path", NULL);
	if(s.script_path != NULL && *s.script_path == '\0')
		g_clear_pointer(&s.script_path, g_free);
}

static void on_panel_destroy(GtkWidget *panel, gpointer user_data)
{
	script_engine_set_log_func(NULL, NULL);
	script_engine_set_done_func(NULL, NULL);
	if(s.layout_popover != NULL)
	{
		g_object_remove_weak_pointer(G_OBJECT(s.layout_popover), (gpointer *)&s.layout_popover);
		gtk_widget_unparent(s.layout_popover);
	}
	s.layout_popover = NULL;
	s.editor = s.console = s.run = s.stop = s.paned = s.editor_scroll = s.console_scroll = NULL;
	s.layout_action = NULL;
	s.panel = NULL;
	g_clear_pointer(&s.script_path, g_free);	/* l'état a déjà été enregistré par close-request */
}

GtkWidget *gt_script_panel_new(GtkApplication *app)
{
	static gboolean source_init = FALSE;
	GKeyFile *kf = gt_config_store_get_key_file(config_get_store());
	GdkRGBA bg, fg;
	GtkWidget *toolbar, *scroll;
	GtkSourceBuffer *ebuf, *cbuf;
	GtkSourceLanguage *lua;
	GtkTextIter end;
	gchar *contents = NULL;

	g_return_val_if_fail(s.panel == NULL, s.panel);
	if(!source_init)
	{
		gtk_source_init();
		source_init = TRUE;
	}
	term_colors(&bg, &fg);
	script_colors_load(&s.colors, kf, &bg, &fg);
	layout_load(kf);

	s.panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_name(s.panel, "script-panel");
	g_object_add_weak_pointer(G_OBJECT(s.panel), (gpointer *)&s.panel);

	toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	gtk_widget_add_css_class(toolbar, "toolbar");
	gtk_box_append(GTK_BOX(toolbar), tool_button("document-open", _("Open"), "script-open", G_CALLBACK(on_open_clicked)));
	gtk_box_append(GTK_BOX(toolbar), tool_button("document-save", _("Save"), "script-save", G_CALLBACK(on_save_clicked)));
	gtk_box_append(GTK_BOX(toolbar), gtk_separator_new(GTK_ORIENTATION_VERTICAL));
	s.run = tool_button("media-playback-start", _("Run"), "script-run", G_CALLBACK(on_run_clicked));
	s.stop = tool_button("media-playback-stop", _("Stop"), "script-stop", G_CALLBACK(on_stop_clicked));
	gtk_widget_set_sensitive(s.stop, FALSE);
	gtk_box_append(GTK_BOX(toolbar), s.run);
	gtk_box_append(GTK_BOX(toolbar), s.stop);
	gtk_box_append(GTK_BOX(toolbar), gtk_separator_new(GTK_ORIENTATION_VERTICAL));
	gtk_box_append(GTK_BOX(toolbar), tool_button("preferences-color", _("Colors..."), "script-colors", G_CALLBACK(on_colors_clicked)));
	add_secondary_click(toolbar);
	gtk_box_append(GTK_BOX(s.panel), toolbar);

	/* Éditeur */
	ebuf = gtk_source_buffer_new(NULL);
	lua = gtk_source_language_manager_get_language(gtk_source_language_manager_get_default(), "lua");
	if(lua != NULL)
		gtk_source_buffer_set_language(ebuf, lua);
	gtk_source_buffer_set_highlight_syntax(ebuf, TRUE);
	gtk_source_buffer_set_highlight_matching_brackets(ebuf, TRUE);
	s.editor = gtk_source_view_new_with_buffer(ebuf);
	g_object_unref(ebuf);
	gtk_widget_set_name(s.editor, "script-editor");
	gtk_text_view_set_monospace(GTK_TEXT_VIEW(s.editor), TRUE);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(s.editor), GTK_WRAP_NONE);
	gtk_source_view_set_show_line_numbers(GTK_SOURCE_VIEW(s.editor), TRUE);
	gtk_source_view_set_auto_indent(GTK_SOURCE_VIEW(s.editor), TRUE);
	gtk_source_view_set_indent_on_tab(GTK_SOURCE_VIEW(s.editor), TRUE);
	gtk_source_view_set_indent_width(GTK_SOURCE_VIEW(s.editor), 4);
	gtk_source_view_set_tab_width(GTK_SOURCE_VIEW(s.editor), 4);
	if(s.script_path != NULL && g_file_get_contents(s.script_path, &contents, NULL, NULL) && g_utf8_validate(contents, -1, NULL))
		gtk_text_buffer_set_text(GTK_TEXT_BUFFER(ebuf), contents, -1);
	else
	{
		g_clear_pointer(&s.script_path, g_free);
		gtk_text_buffer_set_text(GTK_TEXT_BUFFER(ebuf), DEFAULT_SCRIPT, -1);
	}
	g_free(contents);
	gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(ebuf), FALSE);
	scroll = gtk_scrolled_window_new();
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), s.editor);
	gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 150);
	s.editor_scroll = scroll;

	/* Console */
	cbuf = gtk_source_buffer_new(NULL);
	gtk_source_buffer_set_highlight_syntax(cbuf, FALSE);
	s.console = gtk_source_view_new_with_buffer(cbuf);
	gtk_widget_set_name(s.console, "script-console");
	gtk_text_view_set_monospace(GTK_TEXT_VIEW(s.console), TRUE);
	gtk_text_view_set_editable(GTK_TEXT_VIEW(s.console), FALSE);
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(s.console), FALSE);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(s.console), GTK_WRAP_WORD_CHAR);
	gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(cbuf), &end);
	gtk_text_buffer_create_mark(GTK_TEXT_BUFFER(cbuf), "console-end", &end, FALSE);
	g_object_unref(cbuf);
	scroll = gtk_scrolled_window_new();
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), s.console);
	gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 80);
	s.console_scroll = scroll;

	s.paned = gtk_paned_new(s.orientation);
	gtk_widget_set_name(s.paned, "script-paned");
	gtk_paned_set_start_child(GTK_PANED(s.paned), s.editor_scroll);
	gtk_paned_set_resize_start_child(GTK_PANED(s.paned), TRUE);
	gtk_paned_set_shrink_start_child(GTK_PANED(s.paned), TRUE);
	gtk_paned_set_end_child(GTK_PANED(s.paned), s.console_scroll);
	gtk_paned_set_resize_end_child(GTK_PANED(s.paned), FALSE);
	gtk_paned_set_shrink_end_child(GTK_PANED(s.paned), TRUE);
	gtk_widget_set_vexpand(s.paned, TRUE);
	s.pending_pos = *pos_slot(s.orientation);
	g_signal_connect(s.paned, "notify::max-position", G_CALLBACK(on_max_position), NULL);
	add_secondary_click(s.paned);
	gtk_box_append(GTK_BOX(s.panel), s.paned);

	s.layout_popover = layout_popover_new();
	gtk_widget_set_parent(s.layout_popover, s.panel);
	g_object_add_weak_pointer(G_OBJECT(s.layout_popover), (gpointer *)&s.layout_popover);	/* GtkBox détruit ses enfants avant « destroy » */
	g_signal_connect(s.panel, "destroy", G_CALLBACK(on_panel_destroy), NULL);

	s.layout_action = g_simple_action_new_stateful("script-layout", G_VARIANT_TYPE_STRING,
	                                               g_variant_new_string(s.orientation == GTK_ORIENTATION_VERTICAL ? "topbottom" : "leftright"));
	g_signal_connect(s.layout_action, "change-state", G_CALLBACK(on_layout_change_state), NULL);
	g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(s.layout_action));
	g_object_unref(s.layout_action);

	script_engine_setup();
	script_engine_set_log_func(console_append, NULL);
	script_engine_set_done_func(on_script_done, NULL);
	apply_scheme();
	return s.panel;
}

void gt_script_panel_refresh_colors(void)
{
	GdkRGBA bg, fg;

	if(s.panel == NULL)
		return;
	term_colors(&bg, &fg);
	script_colors_resolve(&s.colors, &bg, &fg);
	apply_scheme();
}

void gt_script_panel_store_state(GKeyFile *kf)
{
	gint pos;

	if(s.panel == NULL)
		return;
	pos = gtk_paned_get_position(GTK_PANED(s.paned));
	if(pos > 0 && s.pending_pos <= 0)	/* panneau jamais affiché : la position lue n'a pas de sens, on garde celle du fichier */
		*pos_slot(s.orientation) = pos;
	script_colors_store(&s.colors, kf);
	g_key_file_set_string(kf, "layout", "orientation", s.orientation == GTK_ORIENTATION_VERTICAL ? "topbottom" : "leftright");
	if(s.pos_topbottom > 0)
		g_key_file_set_integer(kf, "layout", "pos_topbottom", s.pos_topbottom);
	if(s.pos_leftright > 0)
		g_key_file_set_integer(kf, "layout", "pos_leftright", s.pos_leftright);
	if(s.script_path != NULL)
		g_key_file_set_string(kf, "session", "script_path", s.script_path);
	else
		g_key_file_remove_key(kf, "session", "script_path", NULL);
}

void gt_script_panel_save_state(void)
{
	GtConfigStore *store;

	if(s.panel == NULL)
		return;
	config_reload_store();	/* relire avant d'écrire : une autre instance a pu enregistrer depuis */
	store = config_get_store();
	gt_script_panel_store_state(gt_config_store_get_key_file(store));
	if(!gt_config_store_is_read_only(store))
		config_save_store(FALSE);
}
