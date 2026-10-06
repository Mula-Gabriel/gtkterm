/* Aides communes aux tests unitaires GTKTerm (GLib GTest, sortie TAP).
 * Identifiants de bogues : docs/superpowers/reviews/2026-10-06-revue-code.md */

#ifndef TEST_UTIL_H_
#define TEST_UTIL_H_

#include <glib.h>
#include <glib/gstdio.h>

/* Conclut un cas de test. Messages en ASCII : les tests tournent en locale C et GLib
 * remplacerait tout caractère non ASCII par « ? » dans la sortie TAP.
 *   bug == NULL : cas ordinaire, échec s'il n'est pas conforme.
 *   bug != NULL : défaut connu de la revue (ex. "M18").
 *                 Non conforme -> « incomplete » (# TODO en TAP) : la suite reste verte.
 *                 Conforme     -> échec : le bogue semble corrigé, il faut retirer le marquage. */
static inline void
test_conclude(gboolean conforme, const gchar *bug, const gchar *detail)
{
	if(bug == NULL)
	{
		if(!conforme)
			g_test_fail_printf("%s", detail);
	}
	else if(conforme)
		g_test_fail_printf("bogue %s semble corrige (%s) : retirer le marquage", bug, detail);
	else
		g_test_incomplete_printf("bogue %s - sous-projet 1 : %s", bug, detail);
}

/* Représentation hexadécimale « 61 5C 62 » ; à libérer avec g_free. */
static inline gchar *
test_hex(const guint8 *data, gsize len)
{
	GString *s = g_string_new(NULL);

	for(gsize i = 0; i < len; i++)
		g_string_append_printf(s, i ? " %02X" : "%02X", data[i]);
	return g_string_free(s, FALSE);
}

/* Écrit content dans dir/name ; renvoie le chemin, à libérer avec g_free. */
static inline gchar *
test_write_file(const gchar *dir, const gchar *name, const gchar *content)
{
	gchar *path = g_build_filename(dir, name, NULL);

	g_assert_true(g_file_set_contents(path, content, -1, NULL));
	return path;
}

/* Supprime un répertoire temporaire plat (créé par g_dir_make_tmp). */
static inline void
test_rmdir(const gchar *dir)
{
	GDir *d = g_dir_open(dir, 0, NULL);
	const gchar *name;

	if(d == NULL)
		return;
	while((name = g_dir_read_name(d)) != NULL)
	{
		gchar *path = g_build_filename(dir, name, NULL);
		g_unlink(path);
		g_free(path);
	}
	g_dir_close(d);
	g_rmdir(dir);
}

#endif
