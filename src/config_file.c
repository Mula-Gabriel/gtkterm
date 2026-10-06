/* Fichier de configuration .gtktermrc : profils (parsecfg) et groupes GKeyFile réservés, sans GTK. */

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <glib.h>
#include <glib/gstdio.h>

#include "config_file.h"

#define CONFIG_FILE_NAME ".gtktermrc"

/* Groupes écrits par GKeyFile dans le même fichier : jamais des profils. */
static const gchar * const reserved_names[] = { "window", "colors", "layout", "session", NULL };

gint
config_wait_char_from_file(gint valeur)
{
	if(valeur >= 1 && valeur <= 255)
		return valeur;
	if(valeur <= -2 && valeur >= -128)
		return (guchar)valeur;
	return -1;
}

gboolean
config_file_is_reserved_name(const gchar *name)
{
	if(name == NULL)
		return FALSE;
	for(gint i = 0; reserved_names[i] != NULL; i++)
		if(g_ascii_strcasecmp(name, reserved_names[i]) == 0)
			return TRUE;
	return FALSE;
}

/* Nom de la section si la ligne est un en-tête « [nom] » (blancs initiaux tolérés), NULL sinon ; à libérer. */
static gchar *
section_name(const gchar *line)
{
	const gchar *end;

	while(*line == ' ' || *line == '\t')
		line++;
	if(*line != '[')
		return NULL;
	end = strchr(line + 1, ']');
	if(end == NULL)
		return NULL;
	return g_strstrip(g_strndup(line + 1, end - line - 1));
}

/* Sépare le contenu : renvoie les lignes hors groupes réservés, place les groupes réservés dans *extras_out. */
static gchar *
split_reserved(const gchar *contents, gchar **extras_out)
{
	GString *kept = g_string_new(NULL);
	GString *extras = g_string_new(NULL);
	gchar **lines = g_strsplit(contents, "\n", -1);
	gboolean in_reserved = FALSE;

	for(gint i = 0; lines[i] != NULL; i++)
	{
		gchar *name = section_name(lines[i]);
		GString *dest;

		if(name != NULL)
		{
			in_reserved = config_file_is_reserved_name(name);
			g_free(name);
		}
		dest = in_reserved ? extras : kept;
		g_string_append(dest, lines[i]);
		if(lines[i + 1] != NULL)
			g_string_append_c(dest, '\n');
	}
	g_strfreev(lines);
	*extras_out = g_string_free(extras, FALSE);
	return g_string_free(kept, FALSE);
}

gchar *
config_file_strip_reserved_groups(const gchar *path)
{
	gchar *contents = NULL;
	gchar *extras = NULL;
	gchar *kept;

	if(!g_file_get_contents(path, &contents, NULL, NULL))
		return g_strdup("");
	kept = split_reserved(contents, &extras);
	g_file_set_contents(path, kept, -1, NULL);
	g_free(kept);
	g_free(contents);
	return extras;
}

void
config_file_restore_reserved_groups(const gchar *path, const gchar *extras)
{
	gchar *existing = NULL;
	gsize length = 0;
	GString *out;

	if(extras == NULL || extras[0] == '\0')
		return;
	if(!g_file_get_contents(path, &existing, &length, NULL))
	{
		existing = g_strdup("");
		length = 0;
	}
	out = g_string_new_len(existing, length);
	g_free(existing);
	if(out->len > 0 && out->str[out->len - 1] != '\n')
		g_string_append_c(out, '\n');
	g_string_append(out, extras);
	g_file_set_contents(path, out->str, out->len, NULL);
	g_string_free(out, TRUE);
}

gint
config_file_parse_profiles(const gchar *path, cfgStruct cfg[])
{
	gchar *contents = NULL;
	gchar *extras = NULL;
	gchar *kept;
	gchar *tmp_path = NULL;
	gint fd;
	gint max = -1;

	/* Fichier absent ou illisible : même comportement qu'avant (cfgParse signale l'erreur). */
	if(!g_file_get_contents(path, &contents, NULL, NULL))
		return cfgParse(path, cfg, CFG_INI);

	/* Lecture sur une copie sans les groupes GKeyFile : le fichier de l'utilisateur n'est jamais modifié (C5, C13). */
	kept = split_reserved(contents, &extras);
	fd = g_file_open_tmp("gtkterm-XXXXXX.ini", &tmp_path, NULL);
	if(fd >= 0)
	{
		close(fd);
		if(g_file_set_contents(tmp_path, kept, -1, NULL))
			max = cfgParse(tmp_path, cfg, CFG_INI);
		g_unlink(tmp_path);
	}
	g_free(tmp_path);
	g_free(kept);
	g_free(extras);
	g_free(contents);
	return max;
}

gint
config_file_find_profile(const gchar *name, gint max)
{
	for(gint i = 0; i < max; i++)
		if(g_ascii_strcasecmp(name, cfgSectionNumberToName(i)) == 0)
			return i;
	return -1;
}

gint
config_file_remove_section(const gchar *path, const gchar *section)
{
	gchar *contents = NULL;
	gchar **lines;
	GString *out;
	gboolean skipping = FALSE;
	gboolean found = FALSE;
	gint result = -1;

	if(!g_file_get_contents(path, &contents, NULL, NULL))
		return -1;
	out = g_string_new(NULL);
	lines = g_strsplit(contents, "\n", -1);
	for(gint i = 0; lines[i] != NULL; i++)
	{
		gchar *name = section_name(lines[i]);

		if(name != NULL)
		{
			skipping = (g_ascii_strcasecmp(name, section) == 0);
			found = found || skipping;
			g_free(name);
		}
		if(!skipping)
		{
			g_string_append(out, lines[i]);
			if(lines[i + 1] != NULL)
				g_string_append_c(out, '\n');
		}
	}
	/* En-têtes reconnus seulement en début de ligne (M11 : une valeur « [::1] » ne coupe plus la section) ; écriture atomique. */
	if(found && g_file_set_contents(path, out->str, out->len, NULL))
		result = 0;
	g_strfreev(lines);
	g_string_free(out, TRUE);
	g_free(contents);
	return result;
}

gchar *
config_file_prepare(const gchar *config_dir, const gchar *home_dir)
{
	gchar *path = g_build_filename(config_dir, CONFIG_FILE_NAME, NULL);
	gchar *old_path = g_build_filename(home_dir, CONFIG_FILE_NAME, NULL);

	/* ~/.config peut ne pas exister (M13). */
	if(g_mkdir_with_parents(config_dir, 0700) != 0)
		g_printerr("Cannot create %s: %s\n", config_dir, g_strerror(errno));

	/* Ancien emplacement $HOME/.gtktermrc : déplacé vers le nouveau s'il n'y a rien là-bas. */
	if(!g_file_test(path, G_FILE_TEST_EXISTS) && g_file_test(old_path, G_FILE_TEST_EXISTS) && g_rename(old_path, path) != 0)
	{
		gchar *contents = NULL;
		gsize length = 0;

		if(g_file_get_contents(old_path, &contents, &length, NULL) && g_file_set_contents(path, contents, length, NULL))
			g_unlink(old_path);
		g_free(contents);
	}
	g_free(old_path);
	return path;
}
