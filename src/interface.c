/***********************************************************************/
/* widgets.c                                                           */
/* ---------                                                           */
/*           GTKTerm Software                                          */
/*                      (c) Julien Schmitt                             */
/*                                                                     */
/* ------------------------------------------------------------------- */
/*                                                                     */
/*   Purpose                                                           */
/*      Functions for the management of the GUI for the main window    */
/*                                                                     */
/*   ChangeLog                                                         */
/*   (All changes by Julien Schmitt except when explicitly written)    */
/*                                                                     */
/*       - 1.01  : The put_hexadecimal partly function rewritten.      */
/*                 The vte_terminal_get_cursor_position function does  */
/*                 not return always the actual column.                */
/*                 Now it uses an internal column-index (virt_col_pos).*/
/*                 (Willem van den Akker)                              */
/*      - 0.99.7 : Changed keyboard shortcuts to <ctrl><shift>         */
/*	            (Ken Peek)                                         */
/*      - 0.99.6 : Added scrollbar and copy/paste (Zach Davis)         */
/*                                                                     */
/*      - 0.99.5 : Make package buildable on pure *BSD by changing the */
/*                 include to asm/termios.h by sys/ttycom.h            */
/*                 Print message without converting it into the locale */
/*                 in show_message()                                   */
/*                 Set backspace key binding to backspace so that the  */
/*                 backspace works. It would even be nicer if the      */
/*                 behaviour of this key could be configured !         */
/*      - 0.99.4 : - Sebastien Bacher -                                */
/*                 Added functions for CR LF auto mode                 */
/*                 Fixed put_text() to have \r\n for the VTE Widget    */
/*                 Rewritten put_hexadecimal() function                */
/*                 - Julien -                                          */
/*                 Modified send_serial to return the actual number of */
/*                 bytes written, and also only display exactly what   */
/*                 is written                                          */
/*      - 0.99.3 : Modified to use a VTE terminal                      */
/*      - 0.99.2 : Internationalization                                */
/*      - 0.99.0 : \b byte now handled correctly by the ascii widget   */
/*                 SUPPR (0x7F) also prints correctly                  */
/*                 adapted for macros                                  */
/*                 modified "about" dialog                             */
/*      - 0.98.6 : fixed possible buffer overrun in hex send           */
/*                 new "Send break" option                             */
/*      - 0.98.5 : icons in the menu                                   */
/*                 bug fixed with local echo and hexadecimal           */
/*                 modified hexadecimal send separator, and bug fixed  */
/*      - 0.98.4 : new hexadecimal display / send                      */
/*      - 0.98.3 : put_text() modified to fit with 0x0D 0x0A           */
/*      - 0.98.2 : added local echo by Julien                          */
/*      - 0.98 : file creation by Julien                               */
/*                                                                     */
/***********************************************************************/

#include "config.h"

#include <gtk/gtk.h>
#ifdef HAVE_LINUX_TERMIOS_H
# include <linux/termios.h>	/* For control signals */
# define NO_TERMIOS		/* Conflicts with <termios.h> */
#elif defined (HAVE_SYS_TTYCOM_H)
#endif
#include <vte/vte.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "term_config.h"
#include "files.h"
#include "search.h"
#include "serial.h"
#include "interface.h"
#include "buffer.h"
#include "macros.h"
#include "auto_config.h"
#include "logging.h"
#include "device_monitor.h"

#include <glib/gprintf.h>
#include <glib/gi18n.h>

guint id;
gboolean echo_on;
gboolean autoreconnect_on;
gboolean crlfauto_on;
gboolean esc_clear_screen_on;
gboolean timestamp_on = 0;
GtkWidget *StatusBar;
GtkWidget *signals[6];
static GtkWidget *Hex_Box;
GtkWidget *searchBar;
GtkWidget *scrolled_window;
GtkWidget *Fenetre;
GtkWidget *popup_menu;
GtkAccelGroup *shortcuts;
GtkWidget *display = NULL;
GtkWidget *macro_panel;
GtkWidget *macro_notebook;
static GtkWidget *macro_tab_flowbox;
static GtkWidget *macro_stack;
static GHashTable *hidden_macro_tabs;

/* Polling state per macro */
typedef struct {
	gint       macro_index;
	guint      period_ms;
	guint64    last_fire_us;
	gboolean   enabled;
	gboolean   running;
	GtkWidget *button;
	/* For macros with arguments */
	gint       n_args;
	gchar    **args;
} macro_polling_t;

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

/* GAction infrastructure (for state management: enable/disable, toggle, radio) */
static GSimpleAction *action_local_echo;
static GSimpleAction *action_autoreconnect;
static GSimpleAction *action_crlf_auto;
static GSimpleAction *action_esc_clear_screen;
static GSimpleAction *action_timestamp;
static GSimpleAction *action_view_index;
static GSimpleAction *action_view_send_hex;
static GSimpleAction *action_view_macro_panel;

/* Radio actions */
static GSimpleAction *action_view_ascii;
static GSimpleAction *action_view_hex;
static GSimpleAction *action_view_hex_chars;

/* Menu item references for enable/disable control */
static GtkWidget *menu_item_edit_copy;
static GtkWidget *menu_item_edit_copy_popup;
static GtkWidget *menu_item_log_to_file;
static GtkWidget *menu_item_log_pause_resume;
static GtkWidget *menu_item_log_stop;
static GtkWidget *menu_item_log_clear;
static GtkWidget *menu_item_send_break;
static GtkWidget *menu_item_toggle_dtr;
static GtkWidget *menu_item_toggle_rts;

GtkWidget *Text;
GtkTextBuffer *buffer;
GtkTextIter iter;

GList *hex_history = NULL;  // To store the history of entered texts
GList *current_hex = NULL;  // Pointer to the current item in history

extern struct configuration_port config;

/* Variables for hexadecimal display */
static gint bytes_per_line = 16;
static gchar blank_data[128];
static guint total_bytes;
static gboolean show_index = FALSE;
guint virt_col_pos = 0;

/* Local functions prototype */
void signals_send_break_callback(GtkAction *action, gpointer data);
void signals_toggle_DTR_callback(GtkAction *action, gpointer data);
void signals_toggle_RTS_callback(GtkAction *action, gpointer data);
void signals_close_port(GtkAction *action, gpointer data);
void signals_open_port(GtkAction *action, gpointer data);
void help_about_callback(GtkAction *action, gpointer data);
gboolean Envoie_car(GtkWidget *, GdkEventKey *, gpointer);
gboolean control_signals_read(void);
void echo_toggled_callback(GSimpleAction *action, GVariant *parameter, gpointer data);
void Autoreconnect_toggled_callback(GSimpleAction *action, GVariant *parameter, gpointer data);
void CR_LF_auto_toggled_callback(GSimpleAction *action, GVariant *parameter, gpointer data);
void esc_clear_screen_toggled_callback(GSimpleAction *action, GVariant *parameter, gpointer data);
void timestamp_toggled_callback(GSimpleAction *action, GVariant *parameter, gpointer data);
void view_radio_callback(GtkWidget *widget, gpointer data);
void view_hex_chars_radio_callback(GtkWidget *widget, gpointer data);
void view_index_toggled_callback(GSimpleAction *action, GVariant *parameter, gpointer data);
void view_send_hex_toggled_callback(GSimpleAction *action, GVariant *parameter, gpointer data);
void initialize_hexadecimal_display(void);
gboolean Send_Hexadecimal(GtkWidget *, GdkEventKey *, gpointer);
gboolean pop_message(void);
static void Got_Input(VteTerminal *, gchar *, guint, gpointer);
void edit_copy_callback(GtkWidget *widget, gpointer data);
void update_copy_sensivity(VteTerminal *terminal, gpointer data);
void edit_paste_callback(GtkWidget *widget, gpointer data);
void edit_find_callback(GtkWidget *widget, gpointer data);
void edit_select_all_callback(GtkWidget *widget, gpointer data);

void view_macro_panel_toggled_callback(GSimpleAction *action, GVariant *parameter, gpointer data);
static void on_macro_button_clicked_with_polling(GtkWidget *widget, gpointer data);
static gboolean on_macro_button_right_click(GtkWidget *button, GdkEventButton *event, gpointer user_data);
static gboolean on_macro_notebook_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data);
static macro_polling_t *get_polling_state(gint macro_index);
static void toggle_polling_run(gint macro_index);
static void on_polling_period_changed(GtkWidget *entry, gpointer user_data);
static gboolean polling_blink_callback(gpointer user_data);
static void on_polling_mode_toggled(GtkCheckMenuItem *check_item, gpointer user_data);
static void send_macro_by_index(gint macro_index);
static void on_macro_tab_clicked(GtkToggleButton *btn, gpointer user_data);
static void create_macro_panel(void);
void rebuild_macro_buttons(void);
void set_saved_data(GtkWidget *widget, gboolean direction);
void update_hex_history(GtkWidget *widget);
gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data);

/* Menu */
static void create_actions_and_menu(void);

void view_send_hex_toggled_callback(GSimpleAction *action, GVariant *parameter, gpointer data)
{
	if (g_variant_get_boolean(parameter))
		gtk_widget_show(GTK_WIDGET(Hex_Box));
	else
		gtk_widget_hide(GTK_WIDGET(Hex_Box));
}

void view_macro_panel_toggled_callback(GSimpleAction *action, GVariant *parameter, gpointer data)
{
	gboolean visible = g_variant_get_boolean(parameter);

	if (macro_panel != NULL)
	{
		gtk_widget_set_visible(macro_panel, visible);
	}
}

void view_index_toggled_callback(GSimpleAction *action, GVariant *parameter, gpointer data)
{
	show_index = g_variant_get_boolean(parameter);
	set_view(HEXADECIMAL_VIEW);
}



void set_view(guint type)
{
	clear_display();
	set_clear_func(clear_display);
	switch(type)
	{
	case ASCII_VIEW:
		g_simple_action_set_state(action_view_ascii, g_variant_new_boolean(TRUE));
		g_simple_action_set_enabled(action_view_index, FALSE);
		g_simple_action_set_enabled(action_view_hex_chars, FALSE);
		total_bytes = 0;
		set_display_func(put_text);
		break;
	case HEXADECIMAL_VIEW:
		g_simple_action_set_state(action_view_hex, g_variant_new_boolean(TRUE));
		g_simple_action_set_enabled(action_view_index, TRUE);
		g_simple_action_set_enabled(action_view_hex_chars, TRUE);
		total_bytes = 0;
		virt_col_pos = 0;
		set_display_func(put_hexadecimal);
		break;
	default:
		set_display_func(NULL);
	}
	write_buffer();
}

void view_radio_callback(GtkWidget *widget, gpointer data)
{
	set_view(GPOINTER_TO_INT(data));
}

void view_hex_chars_radio_callback(GtkWidget *widget, gpointer data)
{
	bytes_per_line = GPOINTER_TO_INT(data);
	set_view(HEXADECIMAL_VIEW);
}

void Set_local_echo(gboolean echo)
{
	echo_on = echo;
	g_simple_action_set_state(action_local_echo, g_variant_new_boolean(echo_on));
}

void echo_toggled_callback(GSimpleAction *action, GVariant *parameter, gpointer data)
{
	echo_on = g_variant_get_boolean(parameter);
	configure_echo(echo_on);
}

void Set_crlfauto(gboolean crlfauto)
{
	crlfauto_on = crlfauto;
	g_simple_action_set_state(action_crlf_auto, g_variant_new_boolean(crlfauto_on));
}

void Set_autoreconnect_enabled(gboolean autoreconnect_enabled)
{
	autoreconnect_on = autoreconnect_enabled;
	g_simple_action_set_state(action_autoreconnect, g_variant_new_boolean(autoreconnect_on));
}

void Autoreconnect_toggled_callback(GSimpleAction *action, GVariant *parameter, gpointer data)
{
	autoreconnect_on = g_variant_get_boolean(parameter);
	configure_autoreconnect_enable(autoreconnect_on);
}

void CR_LF_auto_toggled_callback(GSimpleAction *action, GVariant *parameter, gpointer data)
{
	crlfauto_on = g_variant_get_boolean(parameter);
	configure_crlfauto(crlfauto_on);
}

void Set_esc_clear_screen(gboolean esc_clear_screen)
{
	esc_clear_screen_on = esc_clear_screen;
	g_simple_action_set_state(action_esc_clear_screen, g_variant_new_boolean(esc_clear_screen_on));
}

void esc_clear_screen_toggled_callback(GSimpleAction *action, GVariant *parameter, gpointer data)
{
	esc_clear_screen_on = g_variant_get_boolean(parameter);
	configure_esc_clear_screen(esc_clear_screen_on);
}

void Set_timestamp(gboolean timestamp)
{
	timestamp_on = timestamp;
	g_simple_action_set_state(action_timestamp, g_variant_new_boolean(timestamp_on));
}

void timestamp_toggled_callback(GSimpleAction *action, GVariant *parameter, gpointer data)
{
	timestamp_on = g_variant_get_boolean(parameter);
	config.timestamp = timestamp_on ? TRUE : FALSE;
}


	void toggle_logging_pause_resume(gboolean currentlyLogging)
{
	if (currentlyLogging)
		gtk_menu_item_set_label(GTK_MENU_ITEM(menu_item_log_pause_resume), _("Pause"));
	else
		gtk_menu_item_set_label(GTK_MENU_ITEM(menu_item_log_pause_resume), _("Resume"));
}

void toggle_logging_sensitivity(gboolean currentlyLogging)
{
	gtk_widget_set_sensitive(menu_item_log_to_file, !currentlyLogging);
	gtk_widget_set_sensitive(menu_item_log_pause_resume, currentlyLogging);
	gtk_widget_set_sensitive(menu_item_log_stop, currentlyLogging);
	gtk_widget_set_sensitive(menu_item_log_clear, currentlyLogging);
}

gboolean terminal_button_press_callback(GtkWidget *widget,
                                        GdkEventButton *event,
                                        gpointer data)
{
	if (event->type == GDK_BUTTON_PRESS &&
	    event->button == 3 &&
	    (event->state & gtk_accelerator_get_default_mod_mask()) == 0)
	{
		/* Update copy sensitivity based on current selection */
		update_copy_sensivity(VTE_TERMINAL(widget), NULL);
		gtk_menu_popup_at_pointer(GTK_MENU(popup_menu), (const GdkEvent *)event);
		/* Stop further handlers from processing this event */
		g_signal_stop_emission_by_name(G_OBJECT(widget), "button-press-event");
		return TRUE;
	}
	return FALSE;
}

void terminal_popup_menu_callback(GtkWidget *widget, gpointer data)
{
	gtk_menu_popup_at_pointer(GTK_MENU(popup_menu), NULL);
}

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

static void create_macro_panel(void)
{
	hidden_macro_tabs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	macro_polling_table = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)free_polling_args);
	macro_button_table = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);

	/* Conteneur principal du panneau latéral */
	GtkWidget *panel_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_size_request(panel_vbox, 170, -1);

	/* Barre d'onglets wrappable */
	macro_tab_flowbox = gtk_flow_box_new();
	gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(macro_tab_flowbox), 4);
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

/* Helper: connect a menu item's activate signal to a callback */
static void connect_menu_item_callback(GtkWidget *item, GCallback callback)
{
	g_signal_connect(item, "activate", callback, NULL);
}

/* Helper: connect a check menu item to a toggle action */
static void check_activate_toggle(GtkWidget *item, GSimpleAction *action)
{
	gboolean current = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item));
	g_action_change_state(G_ACTION(action), g_variant_new_boolean(current));
}

static void action_state_sync_to_item(GSimpleAction *action, GParamSpec *pspec,
                                      GtkCheckMenuItem *item)
{
	GVariant *state = g_action_get_state(G_ACTION(action));
	gtk_check_menu_item_set_active(item, g_variant_get_boolean(state));
	g_variant_unref(state);
}

static void connect_check_to_toggle_action(GtkCheckMenuItem *item, GSimpleAction *action)
{
	g_signal_connect(item, "activate", G_CALLBACK(check_activate_toggle), action);
	g_signal_connect(action, "notify::state",
	                 G_CALLBACK(action_state_sync_to_item), item);
}

/* Helper: build a menu from a submenu and return it */
static GtkWidget *build_submenu(GtkWidget *menu_shell, const char *title, GCallback populate_cb)
{
	GtkWidget *menu_item = gtk_menu_item_new_with_mnemonic(title);
	GtkWidget *menu = gtk_menu_new();
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item), menu);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_shell), menu_item);
	((void(*)(GtkWidget *))populate_cb)(menu);
	return menu;
}

static void populate_file_menu(GtkWidget *menu)
{
	GtkWidget *item;
	item = gtk_menu_item_new_with_mnemonic(_("Clear screen"));
	connect_menu_item_callback(item, G_CALLBACK(clear_buffer));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_mnemonic(_("Clear scrollback"));
	connect_menu_item_callback(item, G_CALLBACK(clear_scrollback));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_mnemonic(_("Send RAW file"));
	connect_menu_item_callback(item, G_CALLBACK(send_raw_file));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_mnemonic(_("Save RAW file"));
	connect_menu_item_callback(item, G_CALLBACK(save_raw_file));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_mnemonic(_("Save ASCII file"));
	connect_menu_item_callback(item, G_CALLBACK(save_ascii_file));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

	item = gtk_menu_item_new_with_mnemonic(_("Quit"));
	connect_menu_item_callback(item, G_CALLBACK(gtk_main_quit));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
}

static void populate_edit_menu(GtkWidget *menu)
{
	GtkWidget *item;
	item = gtk_menu_item_new_with_mnemonic(_("Copy"));
	menu_item_edit_copy = item;
	connect_menu_item_callback(item, G_CALLBACK(edit_copy_callback));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_mnemonic(_("Paste"));
	connect_menu_item_callback(item, G_CALLBACK(edit_paste_callback));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_mnemonic(_("Find"));
	connect_menu_item_callback(item, G_CALLBACK(edit_find_callback));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

	item = gtk_menu_item_new_with_mnemonic(_("Select All"));
	connect_menu_item_callback(item, G_CALLBACK(edit_select_all_callback));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
}

static void populate_log_menu(GtkWidget *menu)
{
	GtkWidget *item;
	item = gtk_menu_item_new_with_mnemonic(_("To file..."));
	menu_item_log_to_file = item;
	connect_menu_item_callback(item, G_CALLBACK(logging_start));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_mnemonic(_("Pause/Resume"));
	menu_item_log_pause_resume = item;
	connect_menu_item_callback(item, G_CALLBACK(logging_pause_resume));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_mnemonic(_("Stop"));
	menu_item_log_stop = item;
	connect_menu_item_callback(item, G_CALLBACK(logging_stop));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_mnemonic(_("Clear"));
	menu_item_log_clear = item;
	connect_menu_item_callback(item, G_CALLBACK(logging_clear));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
}

static void populate_config_menu(GtkWidget *menu)
{
	GtkWidget *item;
	item = gtk_menu_item_new_with_mnemonic(_("Port"));
	connect_menu_item_callback(item, G_CALLBACK(Config_Port_Fenetre));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_mnemonic(_("Main window"));
	connect_menu_item_callback(item, G_CALLBACK(Config_Terminal));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_check_menu_item_new_with_mnemonic(_("Local echo"));
	connect_check_to_toggle_action(GTK_CHECK_MENU_ITEM(item), action_local_echo);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_check_menu_item_new_with_mnemonic(_("Autoreconnect"));
	connect_check_to_toggle_action(GTK_CHECK_MENU_ITEM(item), action_autoreconnect);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_check_menu_item_new_with_mnemonic(_("CR LF auto"));
	connect_check_to_toggle_action(GTK_CHECK_MENU_ITEM(item), action_crlf_auto);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_check_menu_item_new_with_mnemonic(_("ESC clear screen"));
	connect_check_to_toggle_action(GTK_CHECK_MENU_ITEM(item), action_esc_clear_screen);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_check_menu_item_new_with_mnemonic(_("Timestamp"));
	connect_check_to_toggle_action(GTK_CHECK_MENU_ITEM(item), action_timestamp);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_mnemonic(_("Macros"));
	connect_menu_item_callback(item, G_CALLBACK(Config_macros));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_mnemonic(_("Load macros file..."));
	connect_menu_item_callback(item, G_CALLBACK(load_macros_file_callback));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_mnemonic(_("Save macros file"));
	connect_menu_item_callback(item, G_CALLBACK(save_macros_file_callback));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

	item = gtk_menu_item_new_with_mnemonic(_("Load configuration"));
	connect_menu_item_callback(item, G_CALLBACK(select_config_callback));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_mnemonic(_("Save configuration"));
	connect_menu_item_callback(item, G_CALLBACK(save_config_callback));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_mnemonic(_("Delete configuration"));
	connect_menu_item_callback(item, G_CALLBACK(delete_config_callback));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
}

static void populate_signals_menu(GtkWidget *menu)
{
	GtkWidget *item;
	item = gtk_menu_item_new_with_mnemonic(_("Send break"));
	menu_item_send_break = item;
	connect_menu_item_callback(item, G_CALLBACK(signals_send_break_callback));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_mnemonic(_("Open Port"));
	connect_menu_item_callback(item, G_CALLBACK(signals_open_port));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_mnemonic(_("Close Port"));
	connect_menu_item_callback(item, G_CALLBACK(signals_close_port));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_mnemonic(_("Toggle DTR"));
	menu_item_toggle_dtr = item;
	connect_menu_item_callback(item, G_CALLBACK(signals_toggle_DTR_callback));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_mnemonic(_("Toggle RTS"));
	menu_item_toggle_rts = item;
	connect_menu_item_callback(item, G_CALLBACK(signals_toggle_RTS_callback));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
}

static void populate_view_menu(GtkWidget *menu)
{
	GtkWidget *item;
	GSList *radio_group = NULL;

	/* ASCII/Hex radio */
	item = gtk_radio_menu_item_new_with_mnemonic(NULL, _("ASCII"));
	radio_group = g_slist_prepend(radio_group, item);
	g_signal_connect(item, "activate", G_CALLBACK(view_radio_callback), GINT_TO_POINTER(ASCII_VIEW));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_radio_menu_item_new_with_mnemonic(radio_group, _("Hexadecimal"));
	radio_group = g_slist_prepend(radio_group, item);
	g_signal_connect(item, "activate", G_CALLBACK(view_radio_callback), GINT_TO_POINTER(HEXADECIMAL_VIEW));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	g_slist_free(radio_group);

	/* Hex chars submenu */
	GtkWidget *hex_submenu = gtk_menu_new();
	GtkWidget *hex_menu_item = gtk_menu_item_new_with_mnemonic(_("Hexadecimal chars"));
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(hex_menu_item), hex_submenu);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), hex_menu_item);

	GSList *hex_radio_group = NULL;
	item = gtk_radio_menu_item_new_with_mnemonic(NULL, "_8");
	hex_radio_group = g_slist_prepend(hex_radio_group, item);
	g_signal_connect(item, "activate", G_CALLBACK(view_hex_chars_radio_callback), GINT_TO_POINTER(8));
	gtk_menu_shell_append(GTK_MENU_SHELL(hex_submenu), item);

	item = gtk_radio_menu_item_new_with_mnemonic(hex_radio_group, "1_0");
	hex_radio_group = g_slist_prepend(hex_radio_group, item);
	g_signal_connect(item, "activate", G_CALLBACK(view_hex_chars_radio_callback), GINT_TO_POINTER(10));
	gtk_menu_shell_append(GTK_MENU_SHELL(hex_submenu), item);

	item = gtk_radio_menu_item_new_with_mnemonic(hex_radio_group, "_16");
	hex_radio_group = g_slist_prepend(hex_radio_group, item);
	g_signal_connect(item, "activate", G_CALLBACK(view_hex_chars_radio_callback), GINT_TO_POINTER(16));
	gtk_menu_shell_append(GTK_MENU_SHELL(hex_submenu), item);

	item = gtk_radio_menu_item_new_with_mnemonic(hex_radio_group, "_24");
	hex_radio_group = g_slist_prepend(hex_radio_group, item);
	g_signal_connect(item, "activate", G_CALLBACK(view_hex_chars_radio_callback), GINT_TO_POINTER(24));
	gtk_menu_shell_append(GTK_MENU_SHELL(hex_submenu), item);

	item = gtk_radio_menu_item_new_with_mnemonic(hex_radio_group, "_32");
	hex_radio_group = g_slist_prepend(hex_radio_group, item);
	g_signal_connect(item, "activate", G_CALLBACK(view_hex_chars_radio_callback), GINT_TO_POINTER(32));
	gtk_menu_shell_append(GTK_MENU_SHELL(hex_submenu), item);
	g_slist_free(hex_radio_group);

	/* Toggle items */
	item = gtk_check_menu_item_new_with_mnemonic(_("Show index"));
	connect_check_to_toggle_action(GTK_CHECK_MENU_ITEM(item), action_view_index);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

	item = gtk_check_menu_item_new_with_mnemonic(_("Send hexadecimal data"));
	connect_check_to_toggle_action(GTK_CHECK_MENU_ITEM(item), action_view_send_hex);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_check_menu_item_new_with_mnemonic(_("Macro panel"));
	connect_check_to_toggle_action(GTK_CHECK_MENU_ITEM(item), action_view_macro_panel);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), TRUE);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
}

static void populate_help_menu(GtkWidget *menu)
{
	GtkWidget *item;
	item = gtk_menu_item_new_with_mnemonic(_("About"));
	connect_menu_item_callback(item, G_CALLBACK(help_about_callback));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
}

static void populate_popup_menu(GtkWidget *menu)
{
	GtkWidget *item;
	item = gtk_menu_item_new_with_mnemonic(_("Copy"));
	menu_item_edit_copy_popup = item;
	connect_menu_item_callback(item, G_CALLBACK(edit_copy_callback));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_mnemonic(_("Paste"));
	connect_menu_item_callback(item, G_CALLBACK(edit_paste_callback));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_mnemonic(_("Find"));
	connect_menu_item_callback(item, G_CALLBACK(edit_find_callback));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

	item = gtk_menu_item_new_with_mnemonic(_("Select All"));
	connect_menu_item_callback(item, G_CALLBACK(edit_select_all_callback));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
}

static void create_actions_and_menu(void)
{
	/* Create toggle actions (stateful boolean) */
	action_local_echo = g_simple_action_new_stateful("local-echo", G_VARIANT_TYPE_BOOLEAN, g_variant_new_boolean(FALSE));
	g_object_ref_sink(G_OBJECT(action_local_echo));
	g_signal_connect(action_local_echo, "change-state", G_CALLBACK(echo_toggled_callback), NULL);

	action_autoreconnect = g_simple_action_new_stateful("autoreconnect", G_VARIANT_TYPE_BOOLEAN, g_variant_new_boolean(FALSE));
	g_object_ref_sink(G_OBJECT(action_autoreconnect));
	g_signal_connect(action_autoreconnect, "change-state", G_CALLBACK(Autoreconnect_toggled_callback), NULL);

	action_crlf_auto = g_simple_action_new_stateful("crlf-auto", G_VARIANT_TYPE_BOOLEAN, g_variant_new_boolean(FALSE));
	g_object_ref_sink(G_OBJECT(action_crlf_auto));
	g_signal_connect(action_crlf_auto, "change-state", G_CALLBACK(CR_LF_auto_toggled_callback), NULL);

	action_esc_clear_screen = g_simple_action_new_stateful("esc-clear-screen", G_VARIANT_TYPE_BOOLEAN, g_variant_new_boolean(FALSE));
	g_object_ref_sink(G_OBJECT(action_esc_clear_screen));
	g_signal_connect(action_esc_clear_screen, "change-state", G_CALLBACK(esc_clear_screen_toggled_callback), NULL);

	action_timestamp = g_simple_action_new_stateful("timestamp", G_VARIANT_TYPE_BOOLEAN, g_variant_new_boolean(FALSE));
	g_object_ref_sink(G_OBJECT(action_timestamp));
	g_signal_connect(action_timestamp, "change-state", G_CALLBACK(timestamp_toggled_callback), NULL);

	action_view_index = g_simple_action_new_stateful("view-index", G_VARIANT_TYPE_BOOLEAN, g_variant_new_boolean(FALSE));
	g_object_ref_sink(G_OBJECT(action_view_index));
	g_signal_connect(action_view_index, "change-state", G_CALLBACK(view_index_toggled_callback), NULL);

	action_view_send_hex = g_simple_action_new_stateful("view-send-hex", G_VARIANT_TYPE_BOOLEAN, g_variant_new_boolean(FALSE));
	g_object_ref_sink(G_OBJECT(action_view_send_hex));
	g_signal_connect(action_view_send_hex, "change-state", G_CALLBACK(view_send_hex_toggled_callback), NULL);

	action_view_macro_panel = g_simple_action_new_stateful("view-macro-panel", G_VARIANT_TYPE_BOOLEAN, g_variant_new_boolean(TRUE));
	g_object_ref_sink(G_OBJECT(action_view_macro_panel));
	g_signal_connect(action_view_macro_panel, "change-state", G_CALLBACK(view_macro_panel_toggled_callback), NULL);

	/* Radio actions */
	action_view_ascii = g_simple_action_new_stateful("view-ascii", G_VARIANT_TYPE_BOOLEAN, g_variant_new_boolean(TRUE));
	g_object_ref_sink(G_OBJECT(action_view_ascii));

	action_view_hex = g_simple_action_new_stateful("view-hex", G_VARIANT_TYPE_BOOLEAN, g_variant_new_boolean(FALSE));
	g_object_ref_sink(G_OBJECT(action_view_hex));

	action_view_hex_chars = g_simple_action_new_stateful("view-hex-chars", G_VARIANT_TYPE_BOOLEAN, g_variant_new_boolean(FALSE));
	g_object_ref_sink(G_OBJECT(action_view_hex_chars));

	/* Build menubar */
	GtkWidget *menubar = gtk_menu_bar_new();
	build_submenu(menubar, _("File"), (GCallback)populate_file_menu);
	build_submenu(menubar, _("Edit"), (GCallback)populate_edit_menu);
	build_submenu(menubar, _("Log"), (GCallback)populate_log_menu);
	build_submenu(menubar, _("Configuration"), (GCallback)populate_config_menu);
	build_submenu(menubar, _("Control signals"), (GCallback)populate_signals_menu);
	build_submenu(menubar, _("View"), (GCallback)populate_view_menu);
	build_submenu(menubar, _("Help"), (GCallback)populate_help_menu);

	/* Store menubar for packing */
	g_object_set_data(G_OBJECT(Fenetre), "menubar", menubar);

	/* Build popup menu */
	popup_menu = gtk_menu_new();
	populate_popup_menu(popup_menu);
	gtk_widget_show_all(popup_menu);
}

void create_main_window(void)
{
	GtkWidget *main_vbox, *label;
	GtkWidget *hex_send_entry;

	Fenetre = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	shortcuts = gtk_accel_group_new();
	gtk_window_add_accel_group(GTK_WINDOW(Fenetre), GTK_ACCEL_GROUP(shortcuts));

	g_signal_connect(GTK_WIDGET(Fenetre), "destroy", (GCallback)gtk_main_quit, NULL);

	Set_window_title("GTKTerm");

	main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(Fenetre), main_vbox);

	/* Create GActions and build menu */
	create_actions_and_menu();

	GtkWidget *menubar = GTK_WIDGET(g_object_get_data(G_OBJECT(Fenetre), "menubar"));
	gtk_box_pack_start(GTK_BOX(main_vbox), menubar, FALSE, TRUE, 0);

	/* create vte window */
	display = vte_terminal_new();

	/* set terminal properties, these could probably be made user configurable */
	vte_terminal_set_scroll_on_output(VTE_TERMINAL(display), FALSE);
	vte_terminal_set_scroll_on_keystroke(VTE_TERMINAL(display), TRUE);
	vte_terminal_set_mouse_autohide(VTE_TERMINAL(display), TRUE);
	vte_terminal_set_backspace_binding(VTE_TERMINAL(display),
	                                   VTE_ERASE_ASCII_BACKSPACE);

	clear_display();

	searchBar = search_bar_new(GTK_WINDOW(Fenetre), VTE_TERMINAL(display));
	gtk_box_pack_start(GTK_BOX(main_vbox), GTK_WIDGET(searchBar), FALSE, FALSE, 0);

        /* Créer le panneau de macros */
	create_macro_panel();

	/* Créer un paned horizontal pour le terminal et le panneau de macros */
	GtkWidget *h_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);

	/* make vte window scrollable */
	scrolled_window = gtk_scrolled_window_new(NULL, gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (display)));

	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
	                               GTK_POLICY_AUTOMATIC,
	                               GTK_POLICY_AUTOMATIC);

	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled_window),
	                                    GTK_SHADOW_NONE);

	gtk_container_add(GTK_CONTAINER(scrolled_window), GTK_WIDGET(display));

	/* Ajouter le terminal (gauche) et le panneau de macros (droite) au paned */
	gtk_paned_pack1(GTK_PANED(h_paned), scrolled_window, TRUE, TRUE);
	gtk_paned_pack2(GTK_PANED(h_paned), macro_panel, FALSE, FALSE);

	/* Ajouter le paned au main_vbox au lieu du scrolled_window */
	gtk_box_pack_start(GTK_BOX(main_vbox), h_paned, TRUE, TRUE, 0);

	g_signal_connect(G_OBJECT(display), "button-press-event",
	                 G_CALLBACK(terminal_button_press_callback), NULL);

	g_signal_connect(G_OBJECT(display), "popup-menu",
	                 G_CALLBACK(terminal_popup_menu_callback), NULL);

	g_signal_connect(G_OBJECT(display), "selection-changed",
	                 G_CALLBACK(update_copy_sensivity), NULL);
	update_copy_sensivity(VTE_TERMINAL(display), NULL);

	/* set up logging buttons availability */
	toggle_logging_pause_resume(FALSE);
	toggle_logging_sensitivity(FALSE);

	/* send hex char box (hidden when not in use) */
	Hex_Box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	label = gtk_label_new(_("Hexadecimal data to send (separator: ';' or space): "));
	gtk_box_pack_start(GTK_BOX(Hex_Box), label, FALSE, FALSE, 5);
	hex_send_entry = gtk_entry_new();
        g_signal_connect(GTK_WIDGET(hex_send_entry), "key-press-event", G_CALLBACK(on_key_press), NULL);
	g_signal_connect(GTK_WIDGET(hex_send_entry), "activate", (GCallback)Send_Hexadecimal, NULL);
	gtk_box_pack_start(GTK_BOX(Hex_Box), hex_send_entry, TRUE, TRUE, 5);
	gtk_box_pack_start(GTK_BOX(main_vbox), Hex_Box, FALSE, TRUE, 2);

	/* status bar */
	StatusBar = gtk_statusbar_new();
	gtk_box_pack_start(GTK_BOX(main_vbox), StatusBar, FALSE, FALSE, 0);
	id = gtk_statusbar_get_context_id(GTK_STATUSBAR(StatusBar), "Messages");

	label = gtk_label_new("RI");
	gtk_box_pack_end(GTK_BOX(StatusBar), label, FALSE, TRUE, 5);
	gtk_widget_set_sensitive(GTK_WIDGET(label), FALSE);
	signals[0] = label;

	label = gtk_label_new("DSR");
	gtk_box_pack_end(GTK_BOX(StatusBar), label, FALSE, TRUE, 5);
	signals[1] = label;

	label = gtk_label_new("CD");
	gtk_box_pack_end(GTK_BOX(StatusBar), label, FALSE, TRUE, 5);
	signals[2] = label;

	label = gtk_label_new("CTS");
	gtk_box_pack_end(GTK_BOX(StatusBar), label, FALSE, TRUE, 5);
	signals[3] = label;

	label = gtk_label_new("RTS");
	gtk_box_pack_end(GTK_BOX(StatusBar), label, FALSE, TRUE, 5);
	signals[4] = label;

	label = gtk_label_new("DTR");
	gtk_box_pack_end(GTK_BOX(StatusBar), label, FALSE, TRUE, 5);
	signals[5] = label;

	g_signal_connect_after(GTK_WIDGET(display), "commit", G_CALLBACK(Got_Input), NULL);

	g_timeout_add(POLL_DELAY, (GSourceFunc)control_signals_read, NULL);

	gtk_window_set_default_size(GTK_WINDOW(Fenetre), 750, 550);
	gtk_widget_show_all(Fenetre);
	search_bar_hide(searchBar);
	gtk_widget_hide(GTK_WIDGET(Hex_Box));
}

void initialize_hexadecimal_display(void)
{
	total_bytes = 0;
	memset(blank_data, ' ', 128);
	blank_data[bytes_per_line * 3 + 5] = 0;
}

void put_hexadecimal(const gchar *string, guint size)
{
	static gchar data[128];
	static gchar data_byte[16];
	gint i = 0;

	if(size == 0)
		return;

	while(i < size)
	{
		while(gtk_events_pending()) gtk_main_iteration();

		/* Print hexadecimal characters */
		data[0] = 0;

		while(virt_col_pos < bytes_per_line && i < size)
		{
			gint avance=0;
			gchar ascii[1];

			if(show_index)
			{
				/* First byte on line */
				if(virt_col_pos == 0)
				{
					sprintf(data, "%6d: ", total_bytes);
					vte_terminal_feed(VTE_TERMINAL(display), data, strlen(data));
				}
			}

			sprintf(data_byte, "%02X ", (guchar)string[i]);
			log_chars(data_byte, 3);
			vte_terminal_feed(VTE_TERMINAL(display), data_byte, 3);

			avance = (bytes_per_line - virt_col_pos) * 3 + virt_col_pos + 2;
			/* Move forward */
			sprintf(data_byte, "%c[%dC", 27, avance);
			vte_terminal_feed(VTE_TERMINAL(display), data_byte, strlen(data_byte));

			/* Print ascii characters */
			ascii[0] = (string[i] > 0x1F) ? string[i] : '.';
			vte_terminal_feed(VTE_TERMINAL(display), ascii, 1);

			/* Move backward */
			sprintf(data_byte, "%c[%dD", 27, avance + 1);
			vte_terminal_feed(VTE_TERMINAL(display), data_byte, strlen(data_byte));

			if(virt_col_pos == bytes_per_line / 2 - 1)
				vte_terminal_feed(VTE_TERMINAL(display), "- ", strlen("- "));

			virt_col_pos++;
			i++;

			/* End of line ? */
			if(virt_col_pos == bytes_per_line)
			{
				vte_terminal_feed(VTE_TERMINAL(display), "\r\n", 2);
				total_bytes += virt_col_pos;
				virt_col_pos = 0;
			}

		}

	}
}

void put_text(const gchar *string, guint size)
{
	log_chars(string, size);
	vte_terminal_feed(VTE_TERMINAL(display), string, size);
}

gint send_serial(gchar *string, gint len)
{
	gint bytes_written;

	bytes_written = Send_chars(string, len);
	if(bytes_written > 0)
	{
		if(echo_on)
			put_chars(string, bytes_written, crlfauto_on, esc_clear_screen_on);
	}

	return bytes_written;
}


static void Got_Input(VteTerminal *widget, gchar *text, guint length, gpointer ptr)
{
	send_serial(text, length);
}

gboolean Envoie_car(GtkWidget *widget, GdkEventKey *event, gpointer pointer)
{
	if(g_utf8_validate(event->string, 1, NULL))
		send_serial(event->string, 1);

	return FALSE;
}


void help_about_callback(GtkAction *action, gpointer data)
{
	gchar *authors[] = {"Julien Schimtt", "Zach Davis", "Florian Euchner", "Stephan Enderlein",
			    "Kevin Picot", NULL};
	gchar *comments_program = _("GTKTerm is a simple GTK+ terminal used to communicate with the serial port.");
	gchar comments[256];
	GError *error = NULL;
	GdkPixbuf *logo = NULL;

	logo = gdk_pixbuf_new_from_resource ("/org/gtk/gtkterm/gtkterm_64x64.png", &error);
	g_snprintf(comments, sizeof(comments), "%s\n\n%s", RELEASE_DATE, comments_program);

	gtk_show_about_dialog(GTK_WINDOW(Fenetre),
	                      "program-name", "GTKTerm fork MGU",
	                      "logo", logo,
	                      "version", VERSION,
	                      "comments", comments,
	                      "copyright", "Copyright © Julien Schimtt",
	                      "authors", authors,
	                      "website", "https://github.com/Mula-Gabriel/gtkterm",
	                      "website-label", "https://github.com/Mula-Gabriel/gtkterm",
	                      "license-type", GTK_LICENSE_LGPL_3_0,
	                      NULL);
}

void show_control_signals(int stat)
{
	if(stat & TIOCM_RI)
		gtk_widget_set_sensitive(GTK_WIDGET(signals[0]), TRUE);
	else
		gtk_widget_set_sensitive(GTK_WIDGET(signals[0]), FALSE);
	if(stat & TIOCM_DSR)
		gtk_widget_set_sensitive(GTK_WIDGET(signals[1]), TRUE);
	else
		gtk_widget_set_sensitive(GTK_WIDGET(signals[1]), FALSE);
	if(stat & TIOCM_CD)
		gtk_widget_set_sensitive(GTK_WIDGET(signals[2]), TRUE);
	else
		gtk_widget_set_sensitive(GTK_WIDGET(signals[2]), FALSE);
	if(stat & TIOCM_CTS)
		gtk_widget_set_sensitive(GTK_WIDGET(signals[3]), TRUE);
	else
		gtk_widget_set_sensitive(GTK_WIDGET(signals[3]), FALSE);
	if(stat & TIOCM_RTS)
		gtk_widget_set_sensitive(GTK_WIDGET(signals[4]), TRUE);
	else
		gtk_widget_set_sensitive(GTK_WIDGET(signals[4]), FALSE);
	if(stat & TIOCM_DTR)
		gtk_widget_set_sensitive(GTK_WIDGET(signals[5]), TRUE);
	else
		gtk_widget_set_sensitive(GTK_WIDGET(signals[5]), FALSE);
}

void signals_send_break_callback(GtkAction *action, gpointer data)
{
	sendbreak();
	Put_temp_message(_("Break signal sent!"), 800);
}

void signals_toggle_DTR_callback(GtkAction *action, gpointer data)
{
	Set_signals(0);
}

void signals_toggle_RTS_callback(GtkAction *action, gpointer data)
{
	Set_signals(1);
}

void signals_close_port(GtkAction *action, gpointer data)
{
	interface_close_port();
}

void signals_open_port(GtkAction *action, gpointer data)
{
	interface_open_port();
}

gboolean control_signals_read(void)
{
	int state;
	gboolean is_serial = (config.transport_type == TRANSPORT_SERIAL);

	if(!is_serial)
	{
		for(int i = 0; i < 6; i++)
			gtk_widget_hide(signals[i]);
		gtk_widget_set_sensitive(menu_item_send_break, FALSE);
		gtk_widget_set_sensitive(menu_item_toggle_dtr, FALSE);
		gtk_widget_set_sensitive(menu_item_toggle_rts, FALSE);
		return TRUE;
	}

	for(int i = 0; i < 6; i++)
		gtk_widget_show(signals[i]);
	gtk_widget_set_sensitive(menu_item_send_break, TRUE);
	gtk_widget_set_sensitive(menu_item_toggle_dtr, TRUE);
	gtk_widget_set_sensitive(menu_item_toggle_rts, TRUE);

	state = lis_sig();
	if(state >= 0)
		show_control_signals(state);

	return TRUE;
}

void Set_status_message(gchar *msg)
{
	gtk_statusbar_pop(GTK_STATUSBAR(StatusBar), id);
	gtk_statusbar_push(GTK_STATUSBAR(StatusBar), id, msg);
}

void Set_window_title(gchar *msg)
{
	gchar* header = g_strdup_printf("GTKTerm - %s", msg);
	gtk_window_set_title(GTK_WINDOW(Fenetre), header);
	g_free(header);
}

void interface_open_port(void)
{
	Config_port();

	gchar *message;
	message = get_port_string();
	Set_status_message(message);
	Set_window_title(message);
	g_free(message);
}

void interface_close_port(void)
{
	Close_port();

	gchar *message;
	message = get_port_string();
	Set_status_message(message);
	Set_window_title(message);
	g_free(message);
}

void show_message(gchar *message, gint type_msg)
{
	GtkWidget *Fenetre_msg;

	if(type_msg==MSG_ERR)
	{
		Fenetre_msg = gtk_message_dialog_new(GTK_WINDOW(Fenetre),
		                                     GTK_DIALOG_DESTROY_WITH_PARENT,
		                                     GTK_MESSAGE_ERROR,
		                                     GTK_BUTTONS_OK,
		                                     message, NULL);
	}
	else if(type_msg==MSG_WRN)
	{
		Fenetre_msg = gtk_message_dialog_new(GTK_WINDOW(Fenetre),
		                                     GTK_DIALOG_DESTROY_WITH_PARENT,
		                                     GTK_MESSAGE_WARNING,
		                                     GTK_BUTTONS_OK,
		                                     message, NULL);
	}
	else
		return;

	gtk_dialog_run(GTK_DIALOG(Fenetre_msg));
	gtk_widget_destroy(Fenetre_msg);
}

gboolean Send_Hexadecimal(GtkWidget *widget, GdkEventKey *event, gpointer pointer)
{
	guint i, j;
	gchar *text, *message, **tokens, *buff;
	guint scan_val;

	text = (gchar *)gtk_entry_get_text(GTK_ENTRY(widget));

	if(strlen(text) == 0)
	{
		message = g_strdup_printf(_("0 byte(s) sent!"));
		Put_temp_message(message, 1500);
		gtk_entry_set_text(GTK_ENTRY(widget), "");
		g_free(message);
		return FALSE;
	}

	tokens = g_strsplit_set(text, " ;", -1);
	buff = g_malloc(g_strv_length(tokens));

	for(i = 0, j = 0; tokens[i] != NULL; i++)
	{
		if(tokens[i][0] == '\0')
			continue;
		if(sscanf(tokens[i], "%02X", &scan_val) != 1)
		{
			Put_temp_message(_("Improper formatted hex input, 0 bytes sent!"),
			                 1500);
			g_free(buff);
			g_strfreev(tokens);
			return FALSE;
		}
		buff[j++] = scan_val;
	}

	send_serial(buff, j);
	g_free(buff);

	message = g_strdup_printf(_("%d byte(s) sent!"), j);
    update_hex_history(widget);
	Put_temp_message(message, 2000);
	gtk_entry_set_text(GTK_ENTRY(widget), "");
	g_strfreev(tokens);

	return FALSE;
}

void Put_temp_message(const gchar *text, gint time)
{
	/* time in ms */
	gtk_statusbar_push(GTK_STATUSBAR(StatusBar), id, text);
	g_timeout_add(time, (GSourceFunc)pop_message, NULL);
}

gboolean pop_message(void)
{
	gtk_statusbar_pop(GTK_STATUSBAR(StatusBar), id);

	return FALSE;
}

void clear_display(void)
{
	initialize_hexadecimal_display();
	if(display)
		vte_terminal_reset(VTE_TERMINAL(display), TRUE, TRUE);
}

void edit_copy_callback(GtkWidget *widget, gpointer data)
{
	GtkClipboard *clipboard;
	gchar *text;

	if (!display)
		return;

	text = vte_terminal_get_text_selected(VTE_TERMINAL(display),
	                                      VTE_FORMAT_TEXT);
	if (text) {
		clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
		gtk_clipboard_set_text(clipboard, text, -1);
		g_free(text);
	}
}

void update_copy_sensivity(VteTerminal *terminal, gpointer data)
{
	gboolean can_copy;

	can_copy = vte_terminal_get_has_selection(VTE_TERMINAL(terminal));

	gtk_widget_set_sensitive(menu_item_edit_copy, can_copy);
	gtk_widget_set_sensitive(menu_item_edit_copy_popup, can_copy);
}

void edit_paste_callback(GtkWidget *widget, gpointer data)
{
	vte_terminal_paste_clipboard(VTE_TERMINAL(display));
}

void edit_find_callback(GtkWidget *widget, gpointer data)
{
	if (gtk_widget_is_visible(searchBar))
		search_bar_hide(searchBar);
	else
		search_bar_show(searchBar);
}

void edit_select_all_callback(GtkWidget *widget, gpointer data)
{
	vte_terminal_select_all(VTE_TERMINAL(display));
}

// Callback for "key-press-event"
gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    switch (event->keyval) {
    case GDK_KEY_Up:        
        set_saved_data(widget, TRUE);  // TRUE for KEY_UP
        return TRUE;  // Event handled
    case GDK_KEY_Down:        
        set_saved_data(widget, FALSE);  // FALSE for KEY_DOWN
        return TRUE;  // Event handled
    default:
        return FALSE;  // Event not handled, propagate further
    }
}

// Function to update the hex history when a new text is entered
void update_hex_history(GtkWidget *widget) {
    const gchar *text = gtk_entry_get_text(GTK_ENTRY(widget));

    // Only add non-empty texts to history
    if (g_strcmp0(text, "") == 0) {
        return;
    }

    if (!current_hex) {
        hex_history = g_list_append(hex_history, g_strdup(text));
    } else {
        const gchar *current_text = (const gchar *)current_hex->data;

        if (g_strcmp0(current_text, text) == 0) {
            gchar *old_data = current_hex->data;
            gchar *dup = g_strdup(current_text);
            g_free(old_data);
            hex_history = g_list_remove(hex_history, old_data);
            hex_history = g_list_append(hex_history, dup);
        } else {
            hex_history = g_list_append(hex_history, g_strdup(text));
        }
    }

    // Reset current_hex to NULL after adding or moving an entry
    current_hex = NULL;
}

// Function to get the previous/next item from the history
void set_saved_data(GtkWidget *widget, gboolean direction) {
    if (!hex_history) {
        return;
    }

    if (direction) {
        // KEY_UP pressed, go to the previous history item
        if (!current_hex) {
            current_hex = g_list_last(hex_history);
        }
        else if (current_hex && current_hex->prev) {
            current_hex = current_hex->prev;
        }
        else
            return;
        const gchar *prev_text = (const gchar *)current_hex->data;
        gtk_entry_set_text(GTK_ENTRY(widget), prev_text);  // Set text in entry
    } else {
        // KEY_DOWN pressed, go to the next history item
        if (current_hex && current_hex->next) {
            current_hex = current_hex->next;
            const gchar *next_text = (const gchar *)current_hex->data;
            gtk_entry_set_text(GTK_ENTRY(widget), next_text);  // Set text in entry
        } else {
            // If no further history, clear the entry
            gtk_entry_set_text(GTK_ENTRY(widget), "");
            current_hex = NULL;  // Reset the pointer
        }
    }
}

