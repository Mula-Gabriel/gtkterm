/* Bouchon du fichier de macros (macros.c) pour test_app_config et test_cmdline : chemin courant, appels enregistrés
 * (« macros_load_file » : « chemin|interactif » ou « chemin|non-interactif », « (null) » pour NULL). */

#include "macros.h"
#include "stubs.h"

static gchar *chemin = NULL;

gboolean
macros_load_file(const gchar *path, gboolean interactive)
{
	gchar *texte = g_strdup_printf("%s|%s", path != NULL ? path : "(null)", interactive ? "interactif" : "non-interactif");

	stub_record("macros_load_file", texte);
	g_free(texte);
	/* Comme macros.c : NULL garde le chemin courant, ou prend le fichier par défaut */
	if(path != NULL)
		stub_macros_file_set_path(path);
	else if(chemin == NULL)
	{
		gchar *defaut = g_build_filename(g_get_user_config_dir(), "gtkterm_macros.ini", NULL);

		stub_macros_file_set_path(defaut);
		g_free(defaut);
	}
	return TRUE;
}

const gchar *
macros_file_get_path(void)
{
	return chemin;
}

void
stub_macros_file_set_path(const gchar *path)
{
	gchar *copie = g_strdup(path);

	g_free(chemin);
	chemin = copie;
}
