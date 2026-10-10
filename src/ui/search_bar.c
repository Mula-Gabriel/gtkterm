/***********************************************************************/
/* search_bar.c                                                        */
/*                                                                     */
/*   Purpose                                                           */
/*      Recherche dans le terminal (sous-projet 6a ; d'après search.c  */
/*      de Tomi Lähteenmäki)                                           */
/*                                                                     */
/***********************************************************************/

#include <config.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <vte/vte.h>

#define PCRE2_CODE_UNIT_WIDTH 0
#include <pcre2.h>

#include "search_bar.h"

#define SEARCH_BAR_KEY "gt-search-bar"

typedef enum
{
	FIND_PREVIOUS,
	FIND_NEXT
} FindDirection;

typedef struct
{
	GtkWidget *bar;		/* GtkSearchBar « search-bar » */
	GtkWidget *entry;	/* « search-entry » */
	GtkWidget *prev, *next;
	GtkWidget *error;	/* « search-error » */
	VteTerminal *vte;	/* pointeur faible */
	VteRegex *regex;	/* expression du texte courant, NULL : à compiler */
} SearchBar;

static void search(SearchBar *sb, FindDirection dir);	/* compile si besoin ; erreur → show_error ; sinon set_regex + find_next/previous */
static void show_error(SearchBar *sb, const gchar *message);	/* NULL : cache search-error et retire la classe « error » de l'entrée */
static void on_changed(GtkEditable *editable, SearchBar *sb);	/* oublie l'expression, show_error(NULL), boutons sensibles si texte non vide */
static void on_activate(GtkSearchEntry *entry, SearchBar *sb);	/* search(FIND_NEXT) */
static void on_prev_clicked(GtkButton *button, SearchBar *sb);	/* search(FIND_PREVIOUS) */
static void on_next_clicked(GtkButton *button, SearchBar *sb);	/* search(FIND_NEXT) */
static gboolean on_key_pressed(GtkEventControllerKey *c, guint keyval, guint keycode, GdkModifierType state, SearchBar *sb);
	/* Return / KP_Enter / ISO_Enter avec exactement GDK_SHIFT_MASK (parmi Ctrl, Maj, Alt, Super) : search(FIND_PREVIOUS), TRUE ; sinon FALSE */
static void on_mode_changed(GObject *bar, GParamSpec *pspec, SearchBar *sb);
	/* ouvert : focus sur l'entrée ; fermé : vte_terminal_search_set_regex(vte, NULL, 0), vte_terminal_unselect_all, show_error(NULL), focus au VTE */
static void on_destroy(GtkWidget *bar, SearchBar *sb);	/* vte_regex_unref, retire les pointeurs faibles, libère */
static void on_find(GSimpleAction *action, GVariant *param, gpointer bar);	/* gt_search_bar_toggle(bar) */

GtkWidget *gt_search_bar_new(VteTerminal *vte)
{
	SearchBar *sb = g_new0(SearchBar, 1);
	GtkWidget *outer, *linked;
	GtkEventController *keys;

	sb->vte = vte;
	g_object_add_weak_pointer(G_OBJECT(vte), (gpointer *)&sb->vte);
	vte_terminal_search_set_wrap_around(vte, TRUE);

	sb->bar = gtk_search_bar_new();
	gtk_widget_set_name(sb->bar, "search-bar");
	gtk_search_bar_set_show_close_button(GTK_SEARCH_BAR(sb->bar), TRUE);

	outer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	linked = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_add_css_class(linked, "linked");

	sb->entry = gtk_search_entry_new();
	gtk_widget_set_name(sb->entry, "search-entry");
	gtk_editable_set_width_chars(GTK_EDITABLE(sb->entry), 30);
	gtk_box_append(GTK_BOX(linked), sb->entry);

	sb->prev = gtk_button_new_from_icon_name("go-up-symbolic");
	gtk_widget_set_tooltip_text(sb->prev, _("Previous match"));
	gtk_widget_set_sensitive(sb->prev, FALSE);
	gtk_box_append(GTK_BOX(linked), sb->prev);

	sb->next = gtk_button_new_from_icon_name("go-down-symbolic");
	gtk_widget_set_tooltip_text(sb->next, _("Next match"));
	gtk_widget_set_sensitive(sb->next, FALSE);
	gtk_box_append(GTK_BOX(linked), sb->next);

	sb->error = gtk_label_new(NULL);
	gtk_widget_set_name(sb->error, "search-error");
	gtk_widget_add_css_class(sb->error, "error");
	gtk_widget_set_visible(sb->error, FALSE);

	gtk_box_append(GTK_BOX(outer), linked);
	gtk_box_append(GTK_BOX(outer), sb->error);
	gtk_search_bar_set_child(GTK_SEARCH_BAR(sb->bar), outer);
	gtk_search_bar_connect_entry(GTK_SEARCH_BAR(sb->bar), GTK_EDITABLE(sb->entry));

	g_signal_connect(sb->entry, "changed", G_CALLBACK(on_changed), sb);
	g_signal_connect(sb->entry, "activate", G_CALLBACK(on_activate), sb);
	g_signal_connect(sb->prev, "clicked", G_CALLBACK(on_prev_clicked), sb);
	g_signal_connect(sb->next, "clicked", G_CALLBACK(on_next_clicked), sb);
	keys = gtk_event_controller_key_new();
	gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
	g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key_pressed), sb);
	gtk_widget_add_controller(sb->entry, keys);
	g_signal_connect(sb->bar, "notify::search-mode-enabled", G_CALLBACK(on_mode_changed), sb);
	g_signal_connect(sb->bar, "destroy", G_CALLBACK(on_destroy), sb);
	g_object_set_data(G_OBJECT(sb->bar), SEARCH_BAR_KEY, sb);
	return sb->bar;
}

void gt_search_bar_toggle(GtkWidget *bar)
{
	GtkSearchBar *sbar = GTK_SEARCH_BAR(bar);

	gtk_search_bar_set_search_mode(sbar, !gtk_search_bar_get_search_mode(sbar));
}

void gt_search_bar_install_action(GActionMap *tab_actions, GtkWidget *bar)
{
	GSimpleAction *action = g_simple_action_new("find", NULL);

	/* Rappel lié à la barre : déconnecté à sa finalisation (l'action ne la garde pas en vie) ; raccourci : gt_app_actions_set_accels */
	g_signal_connect_object(action, "activate", G_CALLBACK(on_find), bar, 0);
	g_action_map_add_action(tab_actions, G_ACTION(action));
	g_object_unref(action);
}

static void show_error(SearchBar *sb, const gchar *message)
{
	gtk_label_set_text(GTK_LABEL(sb->error), message != NULL ? message : "");
	gtk_widget_set_visible(sb->error, message != NULL);
	if(message != NULL)
		gtk_widget_add_css_class(sb->entry, "error");
	else
		gtk_widget_remove_css_class(sb->entry, "error");
}

static void search(SearchBar *sb, FindDirection dir)
{
	const gchar *text = gtk_editable_get_text(GTK_EDITABLE(sb->entry));

	if(text[0] == '\0' || sb->vte == NULL)
		return;
	if(sb->regex == NULL)
	{
		GError *err = NULL;

		sb->regex = vte_regex_new_for_search(text, -1, PCRE2_MULTILINE | PCRE2_CASELESS, &err);
		if(sb->regex == NULL)
		{
			show_error(sb, err != NULL ? err->message : _("Invalid regular expression"));
			g_clear_error(&err);
			return;
		}
	}
	show_error(sb, NULL);
	vte_terminal_search_set_regex(sb->vte, sb->regex, 0);
	if(dir == FIND_NEXT)
		vte_terminal_search_find_next(sb->vte);
	else
		vte_terminal_search_find_previous(sb->vte);
}

static void on_changed(GtkEditable *editable, SearchBar *sb)
{
	gboolean has_text = gtk_editable_get_text(editable)[0] != '\0';

	g_clear_pointer(&sb->regex, vte_regex_unref);
	show_error(sb, NULL);
	gtk_widget_set_sensitive(sb->prev, has_text);
	gtk_widget_set_sensitive(sb->next, has_text);
}

static void on_activate(GtkSearchEntry *entry, SearchBar *sb)
{
	search(sb, FIND_NEXT);
}

static void on_prev_clicked(GtkButton *button, SearchBar *sb)
{
	search(sb, FIND_PREVIOUS);
}

static void on_next_clicked(GtkButton *button, SearchBar *sb)
{
	search(sb, FIND_NEXT);
}

static gboolean on_key_pressed(GtkEventControllerKey *c, guint keyval, guint keycode, GdkModifierType state, SearchBar *sb)
{
	GdkModifierType mods = state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_ALT_MASK | GDK_SUPER_MASK);

	if((keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter || keyval == GDK_KEY_ISO_Enter) && mods == GDK_SHIFT_MASK)
	{
		search(sb, FIND_PREVIOUS);
		return TRUE;
	}
	return FALSE;
}

static void on_mode_changed(GObject *bar, GParamSpec *pspec, SearchBar *sb)
{
	if(gtk_search_bar_get_search_mode(GTK_SEARCH_BAR(bar)))
	{
		gtk_widget_grab_focus(sb->entry);
		return;
	}
	if(sb->vte != NULL)
	{
		vte_terminal_search_set_regex(sb->vte, NULL, 0);
		vte_terminal_unselect_all(sb->vte);
	}
	show_error(sb, NULL);
	if(sb->vte != NULL)
		gtk_widget_grab_focus(GTK_WIDGET(sb->vte));
}

static void on_destroy(GtkWidget *bar, SearchBar *sb)
{
	g_clear_pointer(&sb->regex, vte_regex_unref);
	if(sb->vte != NULL)
		g_object_remove_weak_pointer(G_OBJECT(sb->vte), (gpointer *)&sb->vte);
	g_free(sb);
}

static void on_find(GSimpleAction *action, GVariant *param, gpointer bar)
{
	gt_search_bar_toggle(bar);
}
