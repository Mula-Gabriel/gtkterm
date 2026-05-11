/***********************************************************************/
/* script_panel.c                                                      */
/*           GTKTerm Software                                          */
/*                                                                     */
/*   Purpose                                                           */
/*      GTK panel: Lua script editor + console + Run/Stop controls    */
/***********************************************************************/

#include <config.h>
#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>
#include <glib/gi18n.h>

#include "script_panel.h"
#include "script_engine.h"
#include "term_config.h"

GtkWidget *script_panel = NULL;

static GtkWidget *editor_view   = NULL;
static GtkWidget *console_view  = NULL;
static GtkWidget *run_button    = NULL;
static GtkWidget *stop_button   = NULL;
static GtkWidget *script_paned  = NULL;

/* ------------------------------------------------------------------ */
/* Script panel layout (orientation + paned positions)               */
/* ------------------------------------------------------------------ */

static GtkOrientation paned_orientation   = GTK_ORIENTATION_VERTICAL;
static gint           paned_pos_topbottom = 0;
static gint           paned_pos_leftright = 0;
static gchar         *current_script_path = NULL;

/* ------------------------------------------------------------------ */
/* Script editor color configuration                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    GdkRGBA bg;
    GdkRGBA fg;
    GdkRGBA comment;
    GdkRGBA keyword;
    GdkRGBA string_lit;
    GdkRGBA number;
    GdkRGBA function;
    GdkRGBA lineno_fg;
    GdkRGBA lineno_bg;
} ScriptColors;

static ScriptColors sc;
static gboolean sc_has_custom_bg = FALSE;
static gboolean sc_has_custom_fg = FALSE;

static void set_default_colors(void)
{
    sc.bg = term_conf.background_color;
    sc.fg = term_conf.foreground_color;

    gdouble lum = 0.299 * sc.bg.red
                + 0.587 * sc.bg.green
                + 0.114 * sc.bg.blue;

    if (lum < 0.5) {
        gdk_rgba_parse(&sc.comment,    "#888a85");
        gdk_rgba_parse(&sc.keyword,    "#729fcf");
        gdk_rgba_parse(&sc.string_lit, "#8ae234");
        gdk_rgba_parse(&sc.number,     "#ad7fa8");
        gdk_rgba_parse(&sc.function,   "#edd400");
        gdk_rgba_parse(&sc.lineno_fg,  "#888a85");
        sc.lineno_bg       = sc.bg;
        sc.lineno_bg.red   *= 0.80;
        sc.lineno_bg.green *= 0.80;
        sc.lineno_bg.blue  *= 0.80;
    } else {
        gdk_rgba_parse(&sc.comment,    "#808080");
        gdk_rgba_parse(&sc.keyword,    "#204a87");
        gdk_rgba_parse(&sc.string_lit, "#4e9a06");
        gdk_rgba_parse(&sc.number,     "#5c3566");
        gdk_rgba_parse(&sc.function,   "#8f5902");
        gdk_rgba_parse(&sc.lineno_fg,  "#888a85");
        sc.lineno_bg       = sc.bg;
        sc.lineno_bg.red   = MIN(1.0, sc.lineno_bg.red   * 1.08);
        sc.lineno_bg.green = MIN(1.0, sc.lineno_bg.green * 1.08);
        sc.lineno_bg.blue  = MIN(1.0, sc.lineno_bg.blue  * 1.08);
    }
}

static void load_script_colors(void)
{
    set_default_colors();

    const gchar *path = get_config_file_path();
    GKeyFile *kf  = g_key_file_new();

    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        g_key_file_free(kf);
        return;
    }

#define LOAD_COLOR(field, key) do {                               \
    gchar *_s = g_key_file_get_string(kf, "colors", key, NULL);  \
    if (_s) { gdk_rgba_parse(&sc.field, _s); g_free(_s); }       \
} while (0)

    gchar *_bg_s = g_key_file_get_string(kf, "colors", "bg", NULL);
    if (_bg_s) { gdk_rgba_parse(&sc.bg, _bg_s); g_free(_bg_s); sc_has_custom_bg = TRUE; }
    gchar *_fg_s = g_key_file_get_string(kf, "colors", "fg", NULL);
    if (_fg_s) { gdk_rgba_parse(&sc.fg, _fg_s); g_free(_fg_s); sc_has_custom_fg = TRUE; }

    LOAD_COLOR(comment,    "comment");
    LOAD_COLOR(keyword,    "keyword");
    LOAD_COLOR(string_lit, "string");
    LOAD_COLOR(number,     "number");
    LOAD_COLOR(function,   "function");
    LOAD_COLOR(lineno_fg,  "lineno_fg");
    LOAD_COLOR(lineno_bg,  "lineno_bg");

#undef LOAD_COLOR

    /* Layout */
    gchar *_orient = g_key_file_get_string(kf, "layout", "orientation", NULL);
    if (_orient) {
        paned_orientation = g_strcmp0(_orient, "leftright") == 0
            ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;
        g_free(_orient);
    }
    gint _ptb = g_key_file_get_integer(kf, "layout", "pos_topbottom", NULL);
    if (_ptb > 0) paned_pos_topbottom = _ptb;
    gint _plr = g_key_file_get_integer(kf, "layout", "pos_leftright", NULL);
    if (_plr > 0) paned_pos_leftright = _plr;

    /* Session */
    g_free(current_script_path);
    current_script_path = g_key_file_get_string(kf, "session", "script_path", NULL);

    g_key_file_free(kf);
}

static void save_script_colors(void)
{
    const gchar *path = get_config_file_path();
    GKeyFile *kf   = g_key_file_new();
    g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL);

#define SAVE_COLOR(field, key) do {                          \
    gchar *_s = gdk_rgba_to_string(&sc.field);               \
    g_key_file_set_string(kf, "colors", key, _s);            \
    g_free(_s);                                              \
} while (0)

    SAVE_COLOR(bg,         "bg");
    SAVE_COLOR(fg,         "fg");
    SAVE_COLOR(comment,    "comment");
    SAVE_COLOR(keyword,    "keyword");
    SAVE_COLOR(string_lit, "string");
    SAVE_COLOR(number,     "number");
    SAVE_COLOR(function,   "function");
    SAVE_COLOR(lineno_fg,  "lineno_fg");
    SAVE_COLOR(lineno_bg,  "lineno_bg");
    sc_has_custom_bg = TRUE;
    sc_has_custom_fg = TRUE;

#undef SAVE_COLOR

    /* Layout */
    g_key_file_set_string(kf, "layout", "orientation",
        paned_orientation == GTK_ORIENTATION_VERTICAL ? "topbottom" : "leftright");
    if (paned_pos_topbottom > 0)
        g_key_file_set_integer(kf, "layout", "pos_topbottom", paned_pos_topbottom);
    if (paned_pos_leftright > 0)
        g_key_file_set_integer(kf, "layout", "pos_leftright", paned_pos_leftright);

    /* Session */
    if (current_script_path)
        g_key_file_set_string(kf, "session", "script_path", current_script_path);
    else
        g_key_file_remove_key(kf, "session", "script_path", NULL);

    gsize  length;
    gchar *data = g_key_file_to_data(kf, &length, NULL);
    GError *err = NULL;
    if (!g_file_set_contents(path, data, (gssize)length, &err)) {
        g_printerr("Cannot save script state: %s\n", err ? err->message : "?");
        g_clear_error(&err);
    }
    g_free(data);
    g_key_file_free(kf);
}

/* ------------------------------------------------------------------ */
/* Layout orientation switching                                       */
/* ------------------------------------------------------------------ */

static void set_paned_orientation(GtkOrientation orient)
{
    if (!script_paned || orient == paned_orientation) return;

    gint pos = gtk_paned_get_position(GTK_PANED(script_paned));
    if (paned_orientation == GTK_ORIENTATION_VERTICAL)
        paned_pos_topbottom = pos;
    else
        paned_pos_leftright = pos;

    paned_orientation = orient;
    gtk_orientable_set_orientation(GTK_ORIENTABLE(script_paned), orient);

    gint new_pos = (orient == GTK_ORIENTATION_VERTICAL)
                   ? paned_pos_topbottom : paned_pos_leftright;
    if (new_pos > 0)
        gtk_paned_set_position(GTK_PANED(script_paned), new_pos);

    save_script_colors();
}

static void on_orient_activate(GtkMenuItem *item, gpointer data)
{
    (void)item;
    set_paned_orientation((GtkOrientation)GPOINTER_TO_INT(data));
}

static void show_layout_menu(GdkEvent *event)
{
    GtkWidget *menu  = gtk_menu_new();
    gboolean   is_tb = (paned_orientation == GTK_ORIENTATION_VERTICAL);

    GtkWidget *item_h = gtk_check_menu_item_new_with_label(_("Horizontal  (éditeur en haut)"));
    GtkWidget *item_v = gtk_check_menu_item_new_with_label(_("Vertical  (éditeur à gauche)"));
    gtk_check_menu_item_set_draw_as_radio(GTK_CHECK_MENU_ITEM(item_h), TRUE);
    gtk_check_menu_item_set_draw_as_radio(GTK_CHECK_MENU_ITEM(item_v), TRUE);

    g_signal_connect(item_h, "activate", G_CALLBACK(on_orient_activate),
                     GINT_TO_POINTER(GTK_ORIENTATION_VERTICAL));
    g_signal_connect(item_v, "activate", G_CALLBACK(on_orient_activate),
                     GINT_TO_POINTER(GTK_ORIENTATION_HORIZONTAL));

    /* Set active state after connecting signals to avoid spurious activate */
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_h),  is_tb);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_v), !is_tb);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_h);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_v);
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), event);
}

static gboolean on_paned_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    (void)widget; (void)data;
    if (event->button == GDK_BUTTON_SECONDARY) {
        show_layout_menu((GdkEvent *)event);
        return TRUE;
    }
    return FALSE;
}

static gboolean on_toolbar_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    (void)widget; (void)data;
    if (event->button == GDK_BUTTON_SECONDARY) {
        show_layout_menu((GdkEvent *)event);
        return TRUE;
    }
    return FALSE;
}

/* One-shot: apply saved paned position after the widget gets its first allocation */
static void on_paned_map(GtkWidget *widget, gpointer data)
{
    gint pos = GPOINTER_TO_INT(data);
    if (pos > 0)
        gtk_paned_set_position(GTK_PANED(widget), pos);
    g_signal_handlers_disconnect_by_func(widget, G_CALLBACK(on_paned_map), data);
}

static gchar *rgba_to_hex(const GdkRGBA *c)
{
    return g_strdup_printf("#%02x%02x%02x",
        (guint)(CLAMP(c->red,   0.0, 1.0) * 255.0 + 0.5),
        (guint)(CLAMP(c->green, 0.0, 1.0) * 255.0 + 0.5),
        (guint)(CLAMP(c->blue,  0.0, 1.0) * 255.0 + 0.5));
}

static void apply_color_scheme(void)
{
    gchar *bg   = rgba_to_hex(&sc.bg);
    gchar *fg   = rgba_to_hex(&sc.fg);
    gchar *cmt  = rgba_to_hex(&sc.comment);
    gchar *kw   = rgba_to_hex(&sc.keyword);
    gchar *str  = rgba_to_hex(&sc.string_lit);
    gchar *num  = rgba_to_hex(&sc.number);
    gchar *fn   = rgba_to_hex(&sc.function);
    gchar *lnfg = rgba_to_hex(&sc.lineno_fg);
    gchar *lnbg = rgba_to_hex(&sc.lineno_bg);

    gchar *xml = g_strdup_printf(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<style-scheme id=\"gtkterm-custom\" _name=\"GTKTerm Custom\" version=\"1.0\">\n"
        "  <style name=\"text\"                foreground=\"%s\" background=\"%s\"/>\n"
        "  <style name=\"line-numbers\"        foreground=\"%s\" background=\"%s\"/>\n"
        "  <style name=\"current-line-number\" foreground=\"%s\" background=\"%s\"/>\n"
        "  <style name=\"selection\"           foreground=\"%s\" background=\"%s\"/>\n"
        "  <style name=\"def:comment\"         foreground=\"%s\" italic=\"true\"/>\n"
        "  <style name=\"def:shebang\"         foreground=\"%s\" bold=\"true\"/>\n"
        "  <style name=\"def:keyword\"         foreground=\"%s\" bold=\"true\"/>\n"
        "  <style name=\"def:operator\"        foreground=\"%s\"/>\n"
        "  <style name=\"def:string\"          foreground=\"%s\"/>\n"
        "  <style name=\"def:number\"          foreground=\"%s\"/>\n"
        "  <style name=\"def:float\"           foreground=\"%s\"/>\n"
        "  <style name=\"def:function\"        foreground=\"%s\"/>\n"
        "  <style name=\"lua:function\"        foreground=\"%s\"/>\n"
        "  <style name=\"def:identifier\"      foreground=\"%s\"/>\n"
        "</style-scheme>\n",
        fg, bg,
        lnfg, lnbg,
        fg,   lnbg,
        bg,   fg,
        cmt, cmt,
        kw, kw,
        str,
        num, num,
        fn, fn,
        fg);

    /* Write to user GtkSourceView styles directory */
    gchar *styles_dir = g_build_filename(g_get_user_data_dir(),
                                         "gtksourceview-4", "styles", NULL);
    g_mkdir_with_parents(styles_dir, 0700);
    gchar *scheme_path = g_build_filename(styles_dir, "gtkterm-custom.xml", NULL);
    g_file_set_contents(scheme_path, xml, -1, NULL);
    g_free(xml);
    g_free(styles_dir);
    g_free(scheme_path);

    /* Reload and apply */
    GtkSourceStyleSchemeManager *sm = gtk_source_style_scheme_manager_get_default();
    gtk_source_style_scheme_manager_force_rescan(sm);
    GtkSourceStyleScheme *scheme =
        gtk_source_style_scheme_manager_get_scheme(sm, "gtkterm-custom");

    if (scheme) {
        if (editor_view) {
            GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor_view));
            gtk_source_buffer_set_style_scheme(GTK_SOURCE_BUFFER(buf), scheme);
        }
        if (console_view) {
            GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(console_view));
            if (GTK_SOURCE_IS_BUFFER(buf))
                gtk_source_buffer_set_style_scheme(GTK_SOURCE_BUFFER(buf), scheme);
        }
    }

    g_free(bg);  g_free(fg);
    g_free(cmt); g_free(kw); g_free(str);
    g_free(num); g_free(fn);
    g_free(lnfg); g_free(lnbg);
}

/* ------------------------------------------------------------------ */
/* Color preferences dialog                                           */
/* ------------------------------------------------------------------ */

static void on_colors_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget; (void)user_data;

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        _("Couleurs de l'éditeur de script"),
        NULL,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        _("_Réinitialiser"), 1,
        _("_Annuler"),       GTK_RESPONSE_CANCEL,
        _("_Appliquer"),     GTK_RESPONSE_APPLY,
        NULL);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 16);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 16);
    gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 0);

    struct { const char *label; GdkRGBA *color; } rows[] = {
        { _("Fond de l'éditeur"),         &sc.bg         },
        { _("Texte (défaut)"),            &sc.fg         },
        { _("Commentaires"),              &sc.comment    },
        { _("Mots-clés"),                 &sc.keyword    },
        { _("Chaînes de caractères"),     &sc.string_lit },
        { _("Nombres"),                   &sc.number     },
        { _("Fonctions"),                 &sc.function   },
        { _("N° de ligne — texte"),       &sc.lineno_fg  },
        { _("N° de ligne — fond"),        &sc.lineno_bg  },
    };
    int n = (int)(sizeof(rows) / sizeof(rows[0]));
    GtkWidget **btns = g_new(GtkWidget *, n);

    for (int i = 0; i < n; i++) {
        GtkWidget *lbl = gtk_label_new(rows[i].label);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
        btns[i] = gtk_color_button_new_with_rgba(rows[i].color);
        gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(btns[i]), FALSE);
        gtk_grid_attach(GTK_GRID(grid), lbl,    0, i, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), btns[i], 1, i, 1, 1);
    }

    gtk_widget_show_all(dialog);

    gint response;
    while ((response = gtk_dialog_run(GTK_DIALOG(dialog))) != GTK_RESPONSE_CANCEL
           && response != GTK_RESPONSE_DELETE_EVENT) {

        if (response == 1) {
            /* Reset defaults */
            set_default_colors();
            for (int i = 0; i < n; i++)
                gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(btns[i]), rows[i].color);
            continue;
        }

        if (response == GTK_RESPONSE_APPLY) {
            for (int i = 0; i < n; i++)
                gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(btns[i]), rows[i].color);
            save_script_colors();
            apply_color_scheme();
            break;
        }
    }

    g_free(btns);
    gtk_widget_destroy(dialog);
}

/* ------------------------------------------------------------------ */
/* Console append                                                     */
/* ------------------------------------------------------------------ */

static void console_append(const char *text, gpointer user_data)
{
    (void)user_data;
    if (!console_view) return;

    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(console_view));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buf, &end);
    gtk_text_buffer_insert(buf, &end, text, -1);

    GtkTextMark *mark = gtk_text_buffer_get_insert(buf);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(console_view), mark);
}

/* ------------------------------------------------------------------ */
/* Script done callback                                               */
/* ------------------------------------------------------------------ */

static void on_script_done(gpointer user_data)
{
    (void)user_data;
    if (run_button)  gtk_widget_set_sensitive(run_button,  TRUE);
    if (stop_button) gtk_widget_set_sensitive(stop_button, FALSE);
    console_append(_("[Script finished]\n"), NULL);
}

/* ------------------------------------------------------------------ */
/* Toolbar callbacks                                                  */
/* ------------------------------------------------------------------ */

static void on_run_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget; (void)user_data;
    if (!editor_view) return;

    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor_view));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buf, &start, &end);
    char *script = gtk_text_buffer_get_text(buf, &start, &end, FALSE);

    GtkTextBuffer *con_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(console_view));
    gtk_text_buffer_set_text(con_buf, "", -1);

    gtk_widget_set_sensitive(run_button,  FALSE);
    gtk_widget_set_sensitive(stop_button, TRUE);

    script_engine_run(script);
    g_free(script);
}

static void on_stop_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget; (void)user_data;
    script_engine_stop();
}

static void load_file_into_editor(const gchar *filename)
{
    gchar  *contents = NULL;
    GError *err      = NULL;
    if (g_file_get_contents(filename, &contents, NULL, &err)) {
        GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor_view));
        gtk_text_buffer_set_text(buf, contents, -1);
        g_free(contents);
        g_free(current_script_path);
        current_script_path = g_strdup(filename);
    } else {
        GtkWidget *msg = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s",
            err ? err->message : _("Cannot read file"));
        gtk_dialog_run(GTK_DIALOG(msg));
        gtk_widget_destroy(msg);
        g_clear_error(&err);
    }
}

static void on_open_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget; (void)user_data;

    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        _("Open Lua Script"), NULL,
        GTK_FILE_CHOOSER_ACTION_OPEN,
        _("_Cancel"), GTK_RESPONSE_CANCEL,
        _("_Open"),   GTK_RESPONSE_ACCEPT,
        NULL);

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, _("Lua scripts (*.lua)"));
    gtk_file_filter_add_pattern(filter, "*.lua");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    GtkFileFilter *all = gtk_file_filter_new();
    gtk_file_filter_set_name(all, _("All files"));
    gtk_file_filter_add_pattern(all, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), all);

    if (current_script_path)
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), current_script_path);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        load_file_into_editor(filename);
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

static void on_save_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget; (void)user_data;

    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        _("Save Lua Script"), NULL,
        GTK_FILE_CHOOSER_ACTION_SAVE,
        _("_Cancel"), GTK_RESPONSE_CANCEL,
        _("_Save"),   GTK_RESPONSE_ACCEPT,
        NULL);

    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
    if (current_script_path)
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), current_script_path);
    else
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), "script.lua");

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor_view));
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(buf, &start, &end);
        char *text = gtk_text_buffer_get_text(buf, &start, &end, FALSE);
        GError *err = NULL;
        if (g_file_set_contents(filename, text, -1, &err)) {
            g_free(current_script_path);
            current_script_path = g_strdup(filename);
        } else {
            GtkWidget *msg = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
                GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", err->message);
            gtk_dialog_run(GTK_DIALOG(msg));
            gtk_widget_destroy(msg);
            g_error_free(err);
        }
        g_free(text);
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

/* ------------------------------------------------------------------ */
/* create_script_panel()                                              */
/* ------------------------------------------------------------------ */

void create_script_panel(void)
{
    if (script_panel != NULL) return;

    script_engine_setup();
    script_engine_set_log_func(console_append, NULL);
    script_engine_set_done_func(on_script_done, NULL);

    load_script_colors();

    script_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Toolbar */
    GtkWidget *toolbar = gtk_toolbar_new();
    gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_BOTH_HORIZ);

    GtkToolItem *open_btn = gtk_tool_button_new(
        gtk_image_new_from_icon_name("document-open", GTK_ICON_SIZE_SMALL_TOOLBAR),
        _("Open"));
    g_signal_connect(open_btn, "clicked", G_CALLBACK(on_open_clicked), NULL);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), open_btn, -1);

    GtkToolItem *save_btn = gtk_tool_button_new(
        gtk_image_new_from_icon_name("document-save", GTK_ICON_SIZE_SMALL_TOOLBAR),
        _("Save"));
    g_signal_connect(save_btn, "clicked", G_CALLBACK(on_save_clicked), NULL);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), save_btn, -1);

    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), gtk_separator_tool_item_new(), -1);

    GtkToolItem *run_item = gtk_tool_button_new(
        gtk_image_new_from_icon_name("media-playback-start", GTK_ICON_SIZE_SMALL_TOOLBAR),
        _("Run"));
    run_button = GTK_WIDGET(run_item);
    g_signal_connect(run_item, "clicked", G_CALLBACK(on_run_clicked), NULL);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), run_item, -1);

    GtkToolItem *stop_item = gtk_tool_button_new(
        gtk_image_new_from_icon_name("media-playback-stop", GTK_ICON_SIZE_SMALL_TOOLBAR),
        _("Stop"));
    stop_button = GTK_WIDGET(stop_item);
    gtk_widget_set_sensitive(stop_button, FALSE);
    g_signal_connect(stop_item, "clicked", G_CALLBACK(on_stop_clicked), NULL);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), stop_item, -1);

    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), gtk_separator_tool_item_new(), -1);

    GtkToolItem *colors_btn = gtk_tool_button_new(
        gtk_image_new_from_icon_name("preferences-color", GTK_ICON_SIZE_SMALL_TOOLBAR),
        _("Couleurs..."));
    g_signal_connect(colors_btn, "clicked", G_CALLBACK(on_colors_clicked), NULL);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), colors_btn, -1);

    gtk_widget_add_events(toolbar, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(toolbar, "button-press-event",
                     G_CALLBACK(on_toolbar_button_press), NULL);
    gtk_box_pack_start(GTK_BOX(script_panel), toolbar, FALSE, FALSE, 0);

    /* Paned: orientation and position restored from saved state */
    script_paned = gtk_paned_new(paned_orientation);
    GtkWidget *paned = script_paned;
    g_signal_connect(paned, "button-press-event",
                     G_CALLBACK(on_paned_button_press), NULL);

    /* Script editor */
    GtkSourceLanguageManager *lm = gtk_source_language_manager_get_default();
    GtkSourceLanguage *lua_lang  = gtk_source_language_manager_get_language(lm, "lua");

    GtkSourceBuffer *src_buf = gtk_source_buffer_new(NULL);
    if (lua_lang)
        gtk_source_buffer_set_language(src_buf, lua_lang);
    gtk_source_buffer_set_highlight_syntax(src_buf, TRUE);
    gtk_source_buffer_set_highlight_matching_brackets(src_buf, TRUE);

    editor_view = gtk_source_view_new_with_buffer(src_buf);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(editor_view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(editor_view), GTK_WRAP_NONE);
    gtk_source_view_set_show_line_numbers(GTK_SOURCE_VIEW(editor_view), TRUE);
    gtk_source_view_set_auto_indent(GTK_SOURCE_VIEW(editor_view), TRUE);
    gtk_source_view_set_indent_on_tab(GTK_SOURCE_VIEW(editor_view), TRUE);
    gtk_source_view_set_indent_width(GTK_SOURCE_VIEW(editor_view), 4);
    gtk_source_view_set_tab_width(GTK_SOURCE_VIEW(editor_view), 4);
    g_object_unref(src_buf);

    GtkWidget *editor_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(editor_scroll),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(editor_scroll), editor_view);
    gtk_widget_set_size_request(editor_scroll, -1, 150);

    if (current_script_path &&
        g_file_test(current_script_path, G_FILE_TEST_IS_REGULAR)) {
        /* Restore last opened script */
        gchar *contents = NULL;
        if (g_file_get_contents(current_script_path, &contents, NULL, NULL)) {
            gtk_text_buffer_set_text(GTK_TEXT_BUFFER(src_buf), contents, -1);
            g_free(contents);
        } else {
            g_free(current_script_path);
            current_script_path = NULL;
            goto default_text;
        }
    } else {
        g_free(current_script_path);
        current_script_path = NULL;
default_text:
        gtk_text_buffer_set_text(GTK_TEXT_BUFFER(src_buf),
            "-- GTKTerm Lua script\n"
            "-- gtkterm.send(handle, data)\n"
            "-- gtkterm.wait_for(handle, pattern, timeout_ms)\n"
            "-- gtkterm.log(text)\n"
            "-- handle 0 = main serial port\n\n"
            "gtkterm.log(\"Script ready\")\n", -1);
    }

    /* Restore saved paned position when the widget is first mapped */
    gint init_pos = (paned_orientation == GTK_ORIENTATION_VERTICAL)
                    ? paned_pos_topbottom : paned_pos_leftright;
    if (init_pos > 0)
        g_signal_connect(paned, "map", G_CALLBACK(on_paned_map),
                         GINT_TO_POINTER(init_pos));

    gtk_paned_pack1(GTK_PANED(paned), editor_scroll, TRUE, TRUE);

    /* Console: GtkSourceView, same scheme, no language */
    GtkSourceBuffer *con_buf = gtk_source_buffer_new(NULL);
    gtk_source_buffer_set_highlight_syntax(con_buf, FALSE);

    console_view = gtk_source_view_new_with_buffer(con_buf);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(console_view), TRUE);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(console_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(console_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(console_view), GTK_WRAP_WORD_CHAR);
    g_object_unref(con_buf);

    GtkWidget *console_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(console_scroll),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(console_scroll), console_view);
    gtk_widget_set_size_request(console_scroll, -1, 80);

    gtk_paned_pack2(GTK_PANED(paned), console_scroll, FALSE, TRUE);

    gtk_box_pack_start(GTK_BOX(script_panel), paned, TRUE, TRUE, 0);

    gtk_widget_show_all(script_panel);

    /* Apply custom color scheme (writes XML, loads scheme) */
    apply_color_scheme();
}

/* Save current paned position + layout + colors to disk (call on app close). */
void script_panel_save_state(void)
{
    if (!script_paned) return;
    gint pos = gtk_paned_get_position(GTK_PANED(script_paned));
    if (paned_orientation == GTK_ORIENTATION_VERTICAL)
        paned_pos_topbottom = pos;
    else
        paned_pos_leftright = pos;
    save_script_colors();
}

/* Called after term_conf is fully loaded so bg/fg colors are correct. */
void script_panel_refresh_colors(void)
{
    if (script_panel == NULL)
        return;
    if (!sc_has_custom_bg)
        sc.bg = term_conf.background_color;
    if (!sc_has_custom_fg)
        sc.fg = term_conf.foreground_color;
    apply_color_scheme();
}
