/* macros_save.c — enregistrement des macros depuis l'interface, avec la question de conflit (spec 6c §3.2-15). Fil principal seulement. */

#include <config.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "app_config.h"
#include "dialogs.h"
#include "macros.h"
#include "macros_save.h"

/* Réponse à la question : la session est retrouvée par référence faible (onglet fermé entre-temps : rien) */
static void on_conflict_choice(gint choice, gpointer data)
{
	GWeakRef *ref = data;
	GtSession *s = g_weak_ref_get(ref);

	if(s != NULL)
	{
		GtMacros *m = gt_session_get_macros(s);

		if(choice == 0)
		{
			if(gt_macros_save_file(m, NULL, TRUE, TRUE) == GT_MACROS_SAVED)
				config_remember_macros_file(s);
		}
		else if(choice == 1)
			gt_macros_reload(m);
		g_object_unref(s);
	}
	g_weak_ref_clear(ref);
	g_free(ref);
}

void gt_ui_macros_save(GtkWindow *parent, GtSession *s, const gchar *path)
{
	GtMacros *m;
	GtMacrosSaveResult r;

	if(s == NULL)
		return;
	m = gt_session_get_macros(s);
	if(m == NULL)
		return;
	if(gt_session_get_in_project(s) && path == NULL)
	{
		gt_macros_edited(m);	/* les macros font partie du projet : rien d'écrit */
		return;
	}
	r = gt_macros_save_file(m, path, TRUE, gt_session_get_in_project(s));	/* export d'un onglet de projet : jamais de conflit */
	if(r == GT_MACROS_SAVED)
		config_remember_macros_file(s);
	else if(r == GT_MACROS_CONFLICT)
	{
		const gchar *const labels[] = { _("Overwrite"), _("Reload"), _("Cancel"), NULL };
		GWeakRef *ref = g_new0(GWeakRef, 1);
		/* Sans fichier courant, la cible est le fichier par défaut, présent et jamais lu par cet onglet */
		gchar *target = gt_macros_get_path(m) != NULL ? g_strdup(gt_macros_get_path(m)) : macros_file_default_path();

		g_weak_ref_init(ref, s);
		/* Cancel par défaut, Overwrite destructeur : Entrée n'écrase jamais le travail du tiers */
		gt_ui_choose(parent, gt_macros_get_path(m) != NULL ? _("The macros file changed on disk.") : _("The macros file already exists."), target, labels,
		             2, 0, on_conflict_choice, ref);
		g_free(target);
	}
}
