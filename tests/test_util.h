/* Aides communes aux tests unitaires GTKTerm (GLib GTest, sortie TAP).
 * Identifiants de bogues : docs/superpowers/reviews/2026-10-06-revue-code.md */

#ifndef TEST_UTIL_H_
#define TEST_UTIL_H_

#include <glib.h>
#include <glib/gstdio.h>
#include <netdb.h>
#include <string.h>

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

/* Supprime un répertoire temporaire et tout son contenu (sous-dossiers compris, liens non suivis). */
static inline void
test_rmdir_recursive(const gchar *dir)
{
	GDir *d = g_dir_open(dir, 0, NULL);
	const gchar *name;

	if(d == NULL)
		return;
	while((name = g_dir_read_name(d)) != NULL)
	{
		gchar *path = g_build_filename(dir, name, NULL);

		if(g_file_test(path, G_FILE_TEST_IS_DIR) && !g_file_test(path, G_FILE_TEST_IS_SYMLINK))
			test_rmdir_recursive(path);
		else
			g_unlink(path);
		g_free(path);
	}
	g_dir_close(d);
	g_rmdir(dir);
}

/* HOME et dossiers XDG privés, sous une racine temporaire : jamais le vrai ~/.config. À appeler en tête de main(), avant tout appel
 * GLib qui les met en cache (g_get_user_config_dir…). Renvoie la racine (à supprimer par test_rmdir_recursive, puis g_free). */
static inline gchar *
test_private_dirs(void)
{
	static const gchar * const vars[][2] = { { "HOME", "home" }, { "XDG_CONFIG_HOME", "config" }, { "XDG_CACHE_HOME", "cache" },
	                                         { "XDG_DATA_HOME", "data" }, { "XDG_STATE_HOME", "state" } };
	gchar *root = g_dir_make_tmp("gtkterm-test-XXXXXX", NULL);

	g_assert_nonnull(root);
	for(gsize i = 0; i < G_N_ELEMENTS(vars); i++)
	{
		gchar *path = g_build_filename(root, vars[i][1], NULL);

		g_assert_cmpint(g_mkdir_with_parents(path, 0700), ==, 0);
		g_setenv(vars[i][0], path, TRUE);
		g_free(path);
	}
	return root;
}

/* Résolution de « localhost » rendue déterministe (tests des adresses essayées l'une après l'autre) : ::1 d'abord, puis 127.0.0.1,
 * quelle que soit la configuration de la machine. real : la vraie getaddrinfo (__real_getaddrinfo). Rend FALSE (rien fait) pour
 * tout autre nom. Les deux résultats sont chaînés : freeaddrinfo libère l'ensemble. */
static inline gboolean
test_localhost_deterministe(int (*real)(const char *, const char *, const struct addrinfo *, struct addrinfo **),
                            const char *noeud, const char *service, const struct addrinfo *indices, struct addrinfo **res, int *ret)
{
	struct addrinfo *premier = NULL, *second = NULL, *fin;

	if(noeud == NULL || strcmp(noeud, "localhost") != 0)
		return FALSE;
	real("::1", service, indices, &premier);
	*ret = real("127.0.0.1", service, indices, &second);
	if(*ret != 0)
	{
		if(premier != NULL)
			freeaddrinfo(premier);
		return TRUE;
	}
	if(premier == NULL)
		*res = second;
	else
	{
		for(fin = premier; fin->ai_next != NULL; fin = fin->ai_next)
			;
		fin->ai_next = second;
		*res = premier;
	}
	return TRUE;
}

#endif
