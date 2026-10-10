/* macro_shortcuts.c — raccourcis clavier des macros (interface GTK 4, spec 6a §6.5) : un GtkShortcutController de portée globale, en phase de
 * capture, sur la fenêtre principale : il passe avant le terminal. Une entrée par macro qui a un raccourci lisible, par identifiant (M17),
 * refaites au crochet macros_changed. Macro envoyable : touche consommée (M43), même si l'écriture échoue (port fermé) ; sinon (action vide,
 * séparateur, macro disparue) la touche continue vers le terminal. */

#include <gtk/gtk.h>

#include "macro_shortcuts.h"
#include "macros.h"

static GListStore *shortcuts = NULL;	/* GtkShortcut : modèle du contrôleur */

static gboolean shortcut_cb(GtkWidget *widget, GVariant *args, gpointer data)
{
	guint id = GPOINTER_TO_UINT(data);
	gboolean sendable = gt_macro_is_sendable(gt_macro_set_find(macros_get_set(), id));

	if(sendable)
		macros_send(id, NULL, -1);
	return sendable;
}

void gt_macro_shortcuts_rebuild(void)
{
	GtMacroSet *set = macros_get_set();

	if(shortcuts == NULL)
		return;
	g_list_store_remove_all(shortcuts);
	for(guint i = 0; set != NULL && i < gt_macro_set_count(set); i++)
	{
		const GtMacro *m = gt_macro_set_get(set, i);
		GtkShortcutTrigger *trigger;
		GtkShortcut *shortcut;

		if(m->shortcut[0] == '\0')
			continue;
		trigger = gtk_shortcut_trigger_parse_string(m->shortcut);
		if(trigger == NULL)
		{
			g_debug("raccourci de macro illisible, ignoré : %s", m->shortcut);
			continue;
		}
		shortcut = gtk_shortcut_new(trigger, gtk_callback_action_new(shortcut_cb, GUINT_TO_POINTER(m->id), NULL));
		g_list_store_append(shortcuts, shortcut);
		g_object_unref(shortcut);
	}
}

void gt_macro_shortcuts_attach(GtkWidget *main_window)
{
	GtkEventController *controller;

	g_return_if_fail(shortcuts == NULL);
	shortcuts = g_list_store_new(GTK_TYPE_SHORTCUT);
	controller = gtk_shortcut_controller_new_for_model(G_LIST_MODEL(shortcuts));
	gtk_event_controller_set_static_name(controller, "gtkterm-macro-shortcuts");
	gtk_event_controller_set_propagation_phase(controller, GTK_PHASE_CAPTURE);
	gtk_shortcut_controller_set_scope(GTK_SHORTCUT_CONTROLLER(controller), GTK_SHORTCUT_SCOPE_GLOBAL);
	gtk_widget_add_controller(main_window, controller);	/* la fenêtre possède le contrôleur */
	gt_macro_shortcuts_rebuild();
}

void gt_macro_shortcuts_detach(void)
{
	if(shortcuts == NULL)
		return;
	g_list_store_remove_all(shortcuts);
	g_clear_object(&shortcuts);	/* le contrôleur garde sa référence au modèle, vide, jusqu'à la destruction de la fenêtre */
}
