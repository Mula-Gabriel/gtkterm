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
#include "macro_panel.h"
#include "buffer.h"
#include "macros.h"
#include "terminal_display.h"
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
static GtkWidget *h_paned = NULL;

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

/* Local functions prototype */
void signals_send_break_callback(GtkAction *action, gpointer data);
void signals_toggle_DTR_callback(GtkAction *action, gpointer data);
void signals_toggle_RTS_callback(GtkAction *action, gpointer data);
void signals_close_port(GtkAction *action, gpointer data);
void signals_open_port(GtkAction *action, gpointer data);
void help_about_callback(GtkAction *action, gpointer data);
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
gboolean Send_Hexadecimal(GtkWidget *, GdkEventKey *, gpointer);
gboolean pop_message(void);
void edit_copy_callback(GtkWidget *widget, gpointer data);
void update_copy_sensivity(VteTerminal *terminal, gpointer data);
void edit_paste_callback(GtkWidget *widget, gpointer data);
void edit_find_callback(GtkWidget *widget, gpointer data);
void edit_select_all_callback(GtkWidget *widget, gpointer data);

void set_saved_data(GtkWidget *widget, gboolean direction);
void update_hex_history(GtkWidget *widget);
gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data);

static gboolean on_window_delete_event(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
	save_window_state(widget, h_paned);
	return FALSE;
}

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
	set_hex_show_index(g_variant_get_boolean(parameter));
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
		set_display_func(put_text);
		break;
	case HEXADECIMAL_VIEW:
		g_simple_action_set_state(action_view_hex, g_variant_new_boolean(TRUE));
		g_simple_action_set_enabled(action_view_index, TRUE);
		g_simple_action_set_enabled(action_view_hex_chars, TRUE);
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
	set_hex_bytes_per_line(GPOINTER_TO_INT(data));
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
	gtk_widget_add_accelerator(item, "activate", shortcuts, GDK_KEY_l, GDK_CONTROL_MASK | GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_mnemonic(_("Clear scrollback"));
	connect_menu_item_callback(item, G_CALLBACK(clear_scrollback));
	gtk_widget_add_accelerator(item, "activate", shortcuts, GDK_KEY_k, GDK_CONTROL_MASK | GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_mnemonic(_("Send RAW file"));
	connect_menu_item_callback(item, G_CALLBACK(send_raw_file));
	gtk_widget_add_accelerator(item, "activate", shortcuts, GDK_KEY_r, GDK_CONTROL_MASK | GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);
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
	gtk_widget_add_accelerator(item, "activate", shortcuts, GDK_KEY_q, GDK_CONTROL_MASK | GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
}

static void populate_edit_menu(GtkWidget *menu)
{
	GtkWidget *item;
	item = gtk_menu_item_new_with_mnemonic(_("Copy"));
	menu_item_edit_copy = item;
	connect_menu_item_callback(item, G_CALLBACK(edit_copy_callback));
	gtk_widget_add_accelerator(item, "activate", shortcuts, GDK_KEY_c, GDK_CONTROL_MASK | GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_mnemonic(_("Paste"));
	connect_menu_item_callback(item, G_CALLBACK(edit_paste_callback));
	gtk_widget_add_accelerator(item, "activate", shortcuts, GDK_KEY_v, GDK_CONTROL_MASK | GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_mnemonic(_("Find"));
	connect_menu_item_callback(item, G_CALLBACK(edit_find_callback));
	gtk_widget_add_accelerator(item, "activate", shortcuts, GDK_KEY_f, GDK_CONTROL_MASK | GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

	item = gtk_menu_item_new_with_mnemonic(_("Select All"));
	connect_menu_item_callback(item, G_CALLBACK(edit_select_all_callback));
	gtk_widget_add_accelerator(item, "activate", shortcuts, GDK_KEY_a, GDK_CONTROL_MASK | GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);
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
	gtk_widget_add_accelerator(item, "activate", shortcuts, GDK_KEY_s, GDK_CONTROL_MASK | GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);
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
	gtk_widget_add_accelerator(item, "activate", shortcuts, GDK_KEY_b, GDK_CONTROL_MASK | GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator(item, "activate", shortcuts, GDK_KEY_b, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_mnemonic(_("Open Port"));
	connect_menu_item_callback(item, G_CALLBACK(signals_open_port));
	gtk_widget_add_accelerator(item, "activate", shortcuts, GDK_KEY_F5, 0, GTK_ACCEL_VISIBLE);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_mnemonic(_("Close Port"));
	connect_menu_item_callback(item, G_CALLBACK(signals_close_port));
	gtk_widget_add_accelerator(item, "activate", shortcuts, GDK_KEY_F6, 0, GTK_ACCEL_VISIBLE);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_mnemonic(_("Toggle DTR"));
	menu_item_toggle_dtr = item;
	connect_menu_item_callback(item, G_CALLBACK(signals_toggle_DTR_callback));
	gtk_widget_add_accelerator(item, "activate", shortcuts, GDK_KEY_F7, 0, GTK_ACCEL_VISIBLE);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_mnemonic(_("Toggle RTS"));
	menu_item_toggle_rts = item;
	connect_menu_item_callback(item, G_CALLBACK(signals_toggle_RTS_callback));
	gtk_widget_add_accelerator(item, "activate", shortcuts, GDK_KEY_F8, 0, GTK_ACCEL_VISIBLE);
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
	g_signal_connect(GTK_WIDGET(Fenetre), "delete-event", G_CALLBACK(on_window_delete_event), NULL);

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
	h_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);

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
	load_window_state(Fenetre, h_paned);
	gtk_widget_show_all(Fenetre);
	search_bar_hide(searchBar);
	gtk_widget_hide(GTK_WIDGET(Hex_Box));
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

