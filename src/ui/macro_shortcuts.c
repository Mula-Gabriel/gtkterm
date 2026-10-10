/* macro_shortcuts.c — raccourcis clavier des macros (interface GTK 4, spec 6a §6.5 ; par page en 6b) : un GtkShortcutController de portée
 * locale, en phase de capture, sur la page d'onglet : il passe avant le terminal et ne voit que les touches dont la cible est dans la page
 * affichée. Une entrée par macro qui a un raccourci lisible, par identifiant (M17), refaites au crochet macros_changed de la session. Macro
 * envoyable : touche consommée (M43), même si l'écriture échoue (port fermé) ; sinon (action vide, séparateur, macro disparue, page arrêtée)
 * la touche continue vers le terminal. */

#include <gtk/gtk.h>

#include "macro_shortcuts.h"
#include "macros.h"

#define SHORTCUTS_KEY "gt-macro-shortcuts"	/* sur la page : MacroShortcuts, libéré avec elle */

typedef struct
{
	GtSession  *session;	/* référence */
	GListStore *shortcuts;	/* GtkShortcut : modèle du contrôleur (référence partagée avec lui) */
	gboolean    detached;
} MacroShortcuts;

static MacroShortcuts *shortcuts_of(GtkWidget *view)
{
	return view != NULL ? g_object_get_data(G_OBJECT(view), SHORTCUTS_KEY) : NULL;
}

static void macro_shortcuts_free(gpointer data)
{
	MacroShortcuts *ms = data;

	g_clear_object(&ms->shortcuts);
	g_object_unref(ms->session);
	g_free(ms);
}

/* widget : la page qui porte le contrôleur */
static gboolean shortcut_cb(GtkWidget *widget, GVariant *args, gpointer data)
{
	MacroShortcuts *ms = shortcuts_of(widget);
	guint id = GPOINTER_TO_UINT(data);
	GtMacros *macros;
	gboolean sendable;

	if(ms == NULL || ms->detached)
		return FALSE;
	macros = gt_session_get_macros(ms->session);
	sendable = gt_macro_is_sendable(gt_macro_set_find(gt_macros_get_set(macros), id));
	if(sendable)
		gt_macros_send(macros, id, NULL, -1);
	return sendable;
}

void gt_macro_shortcuts_rebuild(GtkWidget *view)
{
	MacroShortcuts *ms = shortcuts_of(view);
	GtMacroSet *set;

	if(ms == NULL || ms->detached)
		return;
	set = gt_macros_get_set(gt_session_get_macros(ms->session));
	g_list_store_remove_all(ms->shortcuts);
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
		g_list_store_append(ms->shortcuts, shortcut);
		g_object_unref(shortcut);
	}
}

void gt_macro_shortcuts_attach(GtkWidget *view, GtSession *s)
{
	GtkEventController *controller;
	MacroShortcuts *ms;

	g_return_if_fail(GTK_IS_WIDGET(view) && GT_IS_SESSION(s));
	g_return_if_fail(shortcuts_of(view) == NULL);
	ms = g_new0(MacroShortcuts, 1);
	ms->session = g_object_ref(s);
	ms->shortcuts = g_list_store_new(GTK_TYPE_SHORTCUT);
	g_object_set_data_full(G_OBJECT(view), SHORTCUTS_KEY, ms, macro_shortcuts_free);
	controller = gtk_shortcut_controller_new_for_model(G_LIST_MODEL(ms->shortcuts));
	gtk_event_controller_set_static_name(controller, "gtkterm-macro-shortcuts");
	gtk_event_controller_set_propagation_phase(controller, GTK_PHASE_CAPTURE);
	gtk_shortcut_controller_set_scope(GTK_SHORTCUT_CONTROLLER(controller), GTK_SHORTCUT_SCOPE_LOCAL);
	gtk_widget_add_controller(view, controller);	/* la page possède le contrôleur */
	gt_macro_shortcuts_rebuild(view);
}

void gt_macro_shortcuts_detach(GtkWidget *view)
{
	MacroShortcuts *ms = shortcuts_of(view);

	if(ms == NULL || ms->detached)
		return;
	ms->detached = TRUE;
	g_list_store_remove_all(ms->shortcuts);	/* le contrôleur garde sa référence au modèle, vide, jusqu'à la destruction de la page */
}
