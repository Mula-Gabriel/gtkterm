/* Dialogue « Terminal » (§6.4) : police à chasse fixe, lignes de défilement, curseur bloc, couleurs du texte et du fond. Effet immédiat sur le profil
 * en cours (config) et sur le terminal par gt_terminal_view_apply_profile ; rien n'est écrit sur disque (« Save configuration » enregistre).
 * Instance unique, modale, transitoire de la fenêtre principale. Remplace Config_Terminal et ses rappels (term_config.c, GTK 3). */
#include <config.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "app_config.h"
#include "profile.h"
#include "app.h"
#include "main_window.h"
#include "terminal_view.h"
#include "dialogs.h"
#include "script_panel.h"
#include "terminal_dialog.h"

/* Même préfixe que menus.ui dans gtkterm.gresource.xml (src/ui/) */
#define TERMINAL_DIALOG_RESOURCE "/eu/mulagabriel/gtkterm/ui/terminal_dialog.ui"

static GtkWindow *terminal_dialog = NULL;	/* dialogue ouvert (pointeur faible) */

static void color_from_rgba(GtColor *c, const GdkRGBA *in)
{
	c->red = in->red;
	c->green = in->green;
	c->blue = in->blue;
	c->alpha = in->alpha;
}

/* Une seule application au terminal, quel que soit le réglage changé */
static void apply_changes(void)
{
	GtkWindow *main_window = gt_app_get_main_window();

	if(main_window != NULL)
		gt_terminal_view_apply_profile(gt_main_window_get_terminal_view(GTK_WIDGET(main_window)), &config);
	gt_script_panel_refresh_colors();
}

/* Filtre du sélecteur de polices : familles à chasse fixe (éléments PangoFontFamily ou PangoFontFace selon le niveau) */
static gboolean monospace_only(gpointer item, gpointer user_data)
{
	PangoFontFamily *family = NULL;

	if(PANGO_IS_FONT_FACE(item))
		family = pango_font_face_get_family(PANGO_FONT_FACE(item));
	else if(PANGO_IS_FONT_FAMILY(item))
		family = PANGO_FONT_FAMILY(item);
	return family != NULL && pango_font_family_is_monospace(family);
}

static void on_font(GObject *button, GParamSpec *pspec, gpointer user_data)
{
	PangoFontDescription *desc = gtk_font_dialog_button_get_font_desc(GTK_FONT_DIALOG_BUTTON(button));

	if(desc == NULL)
		return;
	g_free(config.font);
	config.font = pango_font_description_to_string(desc);
	apply_changes();
}

static void on_scrollback(GtkSpinButton *spin, gpointer user_data)
{
	config.scrollback = gtk_spin_button_get_value_as_int(spin);
	apply_changes();
}

static void on_cursor(GObject *sw, GParamSpec *pspec, gpointer user_data)
{
	config.block_cursor = gtk_switch_get_active(GTK_SWITCH(sw));
	apply_changes();
}

static void on_color(GObject *button, GParamSpec *pspec, gpointer field)
{
	const GdkRGBA *rgba = gtk_color_dialog_button_get_rgba(GTK_COLOR_DIALOG_BUTTON(button));

	color_from_rgba(field, rgba);
	apply_changes();
}

void gt_terminal_dialog_show(GtkWindow *parent)
{
	GtkBuilder *builder;
	GtkWindow *win;
	GtkFontDialogButton *font;
	GtkColorDialogButton *fg, *bg;
	GtkSpinButton *scrollback;
	GtkSwitch *cursor;
	GtkFilter *filter;
	PangoFontDescription *desc;
	GdkRGBA rgba;

	if(terminal_dialog != NULL)
	{
		gtk_window_present(terminal_dialog);
		return;
	}
	builder = gtk_builder_new_from_resource(TERMINAL_DIALOG_RESOURCE);
	win = GTK_WINDOW(gtk_builder_get_object(builder, "terminal_dialog"));
	font = GTK_FONT_DIALOG_BUTTON(gtk_builder_get_object(builder, "font_button"));
	scrollback = GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "scrollback_spin"));
	cursor = GTK_SWITCH(gtk_builder_get_object(builder, "block_cursor_switch"));
	fg = GTK_COLOR_DIALOG_BUTTON(gtk_builder_get_object(builder, "fg_button"));
	bg = GTK_COLOR_DIALOG_BUTTON(gtk_builder_get_object(builder, "bg_button"));
	gtk_window_set_transient_for(win, parent);
	gt_ui_close_on_escape(win);

	filter = GTK_FILTER(gtk_custom_filter_new(monospace_only, NULL, NULL));
	gtk_font_dialog_set_filter(gtk_font_dialog_button_get_dialog(font), filter);
	g_object_unref(filter);

	/* Valeurs du profil en cours, posées avant les signaux : l'ouverture n'écrit rien */
	desc = pango_font_description_from_string(config.font != NULL ? config.font : GT_PROFILE_DEFAULT_FONT);
	gtk_font_dialog_button_set_font_desc(font, desc);
	pango_font_description_free(desc);
	gtk_spin_button_set_value(scrollback, config.scrollback);	/* bornée à 100-99 999 par l'ajustement */
	gtk_switch_set_active(cursor, config.block_cursor);
	gt_color_to_rgba(&config.foreground, &rgba);	/* terminal_view.h */
	gtk_color_dialog_button_set_rgba(fg, &rgba);
	gt_color_to_rgba(&config.background, &rgba);
	gtk_color_dialog_button_set_rgba(bg, &rgba);

	g_signal_connect(font, "notify::font-desc", G_CALLBACK(on_font), NULL);
	g_signal_connect(scrollback, "value-changed", G_CALLBACK(on_scrollback), NULL);
	g_signal_connect(cursor, "notify::active", G_CALLBACK(on_cursor), NULL);
	g_signal_connect(fg, "notify::rgba", G_CALLBACK(on_color), &config.foreground);
	g_signal_connect(bg, "notify::rgba", G_CALLBACK(on_color), &config.background);
	g_signal_connect_swapped(gtk_builder_get_object(builder, "close_button"), "clicked", G_CALLBACK(gtk_window_destroy), win);

	terminal_dialog = win;
	g_object_add_weak_pointer(G_OBJECT(win), (gpointer *)&terminal_dialog);
	gtk_window_present(win);
	g_object_unref(builder);	/* la fenêtre de premier niveau reste tenue par GTK jusqu'à sa destruction */
}

static void on_terminal_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
	gt_terminal_dialog_show(gt_app_get_main_window());
}

void gt_terminal_dialog_install(GtkApplication *app)
{
	static const GActionEntry entries[] = { { .name = "terminal", .activate = on_terminal_action } };

	g_action_map_add_action_entries(G_ACTION_MAP(app), entries, G_N_ELEMENTS(entries), app);
}
