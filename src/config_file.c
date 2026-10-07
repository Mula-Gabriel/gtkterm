/* Fichier de configuration .gtktermrc : préparation du chemin et décodage du caractère d'attente, sans GTK. */

#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>

#include "config_file.h"

#define CONFIG_FILE_NAME ".gtktermrc"

gint
config_wait_char_from_file(gint valeur)
{
	if(valeur >= 1 && valeur <= 255)
		return valeur;
	if(valeur <= -2 && valeur >= -128)
		return (guchar)valeur;
	return -1;
}

gchar *
config_file_prepare(const gchar *config_dir, const gchar *home_dir)
{
	gchar *path = g_build_filename(config_dir, CONFIG_FILE_NAME, NULL);
	gchar *old_path = g_build_filename(home_dir, CONFIG_FILE_NAME, NULL);

	/* ~/.config peut ne pas exister (M13). */
	if(g_mkdir_with_parents(config_dir, 0700) != 0)
		g_printerr("Cannot create %s: %s\n", config_dir, g_strerror(errno));

	/* Ancien emplacement $HOME/.gtktermrc : déplacé vers le nouveau s'il n'y a rien là-bas. Seul déplacement du fichier hors du
	 * magasin (config_store.c) ; si rename échoue, la copie est écrite en 0600 comme les enregistrements du magasin. */
	if(!g_file_test(path, G_FILE_TEST_EXISTS) && g_file_test(old_path, G_FILE_TEST_EXISTS) && g_rename(old_path, path) != 0)
	{
		gchar *contents = NULL;
		gsize length = 0;

		if(g_file_get_contents(old_path, &contents, &length, NULL)
		   && g_file_set_contents_full(path, contents, (gssize)length, G_FILE_SET_CONTENTS_CONSISTENT, 0600, NULL))
			g_unlink(old_path);
		g_free(contents);
	}
	g_free(old_path);
	return path;
}
