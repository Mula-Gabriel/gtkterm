/***********************************************************************/
/* config_store.c                                                      */
/*                                                                     */
/*   Purpose                                                           */
/*      Magasin de configuration : seul lecteur et écrivain de         */
/*      .gtktermrc (GKeyFile), profils, réglages globaux (sous-projet 3)*/
/*                                                                     */
/***********************************************************************/

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <config.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include "config_file.h"
#include "config_store.h"
#include "file_stamp.h"
#include "profile_keyfile.h"

#define PROFILE_PREFIX "profile:"
#define GENERAL_GROUP "general"

struct _GtConfigStore
{
	gchar *path;
	GKeyFile *kf;
	gboolean read_only;
	gboolean migrated;
	gboolean backup_done;
	gchar *original_contents;	/* fichier d'origine d'un magasin migré : copie de sauvegarde */
	GtFileStamp identity;
	gchar *saved_text;	/* contenu du magasin tel que lu ou écrit : retour en arrière si un enregistrement échoue */
	GError *read_error;	/* dernière erreur de lecture (magasin en lecture seule), NULL sinon */
};

gboolean gt_config_parse_boolean(const gchar *s, gboolean *out)
{
	static const gchar * const vrai[] = { "true", "yes", "on", "1", NULL };
	static const gchar * const faux[] = { "false", "no", "off", "0", NULL };

	if(s == NULL)
		return FALSE;
	for(gint i = 0; vrai[i] != NULL; i++)
	{
		if(g_ascii_strcasecmp(s, vrai[i]) == 0)
		{
			*out = TRUE;
			return TRUE;
		}
		if(g_ascii_strcasecmp(s, faux[i]) == 0)
		{
			*out = FALSE;
			return TRUE;
		}
	}
	return FALSE;
}

/* ---- Noms de groupes ---------------------------------------------------- */

static gboolean same_name(const gchar *a, const gchar *b)
{
	gchar *fa = g_utf8_casefold(a, -1);
	gchar *fb = g_utf8_casefold(b, -1);
	gboolean same = (g_strcmp0(fa, fb) == 0);

	g_free(fa);
	g_free(fb);
	return same;
}

/* Groupe « préfixe + nom » existant, nom comparé sans casse ; NULL sinon ; à libérer */
static gchar *find_prefixed_group(GtConfigStore *s, const gchar *prefix, const gchar *name)
{
	gchar **groups = g_key_file_get_groups(s->kf, NULL);
	gchar *found = NULL;
	gsize plen = strlen(prefix);

	for(gint i = 0; groups[i] != NULL && found == NULL; i++)
		if(g_str_has_prefix(groups[i], prefix) && same_name(groups[i] + plen, name))
			found = g_strdup(groups[i]);
	g_strfreev(groups);
	return found;
}

/* Groupe du profil : l'existant (casse d'origine) ou un nouveau nom ; à libérer */
static gchar *profile_group_for_write(GtConfigStore *s, const gchar *name)
{
	gchar *group = find_prefixed_group(s, PROFILE_PREFIX, name);

	return group != NULL ? group : g_strconcat(PROFILE_PREFIX, name, NULL);
}

static gint compare_names(gconstpointer a, gconstpointer b)
{
	const gchar *na = *(const gchar * const *)a;
	const gchar *nb = *(const gchar * const *)b;
	gchar *fa = g_utf8_casefold(na, -1);
	gchar *fb = g_utf8_casefold(nb, -1);
	gint r = g_utf8_collate(fa, fb);

	g_free(fa);
	g_free(fb);
	return r;
}

/* ---- Interface publique ------------------------------------------------- */

static const gchar * const reserved_groups[] = { "window", "colors", "layout", "session", NULL };
static const gchar * const legacy_marker_keys[] = { "port", "speed", "transport_type", "socket_host", "font", "term_rows", NULL };
static const gchar * const general_marker_keys[] = { "port", "speed", "transport_type", NULL };
static const gchar * const legacy_bool_keys[] = { "echo", "crlfauto", "autoreconnect_enabled", "esc_clear_screen", "timestamp", "term_block_cursor",
                                                  "term_visual_bell", NULL };
static const gchar * const update_keys[] = { "update_url", "update_prefix", "update_check_startup", "update_skipped_version", NULL };

static gboolean in_list(const gchar * const *list, const gchar *s)
{
	for(gint i = 0; list[i] != NULL; i++)
		if(g_strcmp0(list[i], s) == 0)
			return TRUE;
	return FALSE;
}

static gboolean is_reserved_group(const gchar *g)
{
	for(gint i = 0; reserved_groups[i] != NULL; i++)
		if(g_ascii_strcasecmp(g, reserved_groups[i]) == 0)
			return TRUE;
	return FALSE;
}

static gboolean has_any_key(GKeyFile *kf, const gchar *g, const gchar * const *keys)
{
	for(gint i = 0; keys[i] != NULL; i++)
		if(g_key_file_has_key(kf, g, keys[i], NULL))
			return TRUE;
	return FALSE;
}

static gboolean is_legacy_profile(GKeyFile *kf, const gchar *g)
{
	if(is_reserved_group(g) || g_str_has_prefix(g, PROFILE_PREFIX) || g_str_has_prefix(g, "project:"))
		return FALSE;
	if(g_strcmp0(g, GENERAL_GROUP) == 0)
		return has_any_key(kf, g, general_marker_keys);
	return has_any_key(kf, g, legacy_marker_keys);
}

/* Retire une paire de guillemets simples ou doubles (écriture parsecfg) ; à libérer */
static gchar *unquote(const gchar *raw)
{
	gchar *v = g_strstrip(g_strdup(raw != NULL ? raw : ""));
	gsize n = strlen(v);

	if(n >= 2 && (v[0] == '"' || v[0] == '\'') && v[n - 1] == v[0])
	{
		memmove(v, v + 1, n - 2);
		v[n - 2] = '\0';
	}
	return v;
}

/* Nom de profil libre (« nom », sinon « nom (2) »…) ; à libérer */
static gchar *unique_profile_name(GtConfigStore *s, const gchar *name)
{
	gchar *candidate = g_strdup(name);

	for(gint n = 2; gt_config_store_has_profile(s, candidate); n++)
	{
		g_free(candidate);
		candidate = g_strdup_printf("%s (%d)", name, n);
	}
	return candidate;
}

static void migrate_colors(GKeyFile *kf, const gchar *old, const gchar *group, const gchar *prefix, const gchar *key, GtColor def)
{
	static const gchar * const parts[] = { "red", "green", "blue", "alpha" };
	gdouble v[4] = { def.red, def.green, def.blue, def.alpha };
	gboolean any = FALSE;

	for(gint i = 0; i < 4; i++)
	{
		gchar *k = g_strconcat(prefix, parts[i], NULL);
		gchar *raw = g_key_file_get_value(kf, old, k, NULL);

		if(raw != NULL)
		{
			/* Avant le correctif M9, parsecfg écrivait les flottants dans la locale de l'utilisateur (« 0,500000 ») */
			gchar *val = g_strdelimit(unquote(raw), ",", '.');
			gchar *end = NULL;
			gdouble d = g_ascii_strtod(val, &end);

			if(end != val)
			{
				v[i] = d;
				any = TRUE;
			}
			g_free(val);
			g_free(raw);
		}
		g_free(k);
	}
	if(any)
		g_key_file_set_double_list(kf, group, key, v, 4);
}

static void migrate_profile(GtConfigStore *s, const gchar *old, gboolean update_source, GKeyFile *update_holder)
{
	GKeyFile *kf = s->kf;
	gchar *name = unquote(old);
	gchar *unique = unique_profile_name(s, name);
	gchar *group = g_strconcat(PROFILE_PREFIX, unique, NULL);
	gchar **keys = g_key_file_get_keys(kf, old, NULL, NULL);
	GtProfile def;

	gt_profile_init_defaults(&def);
	for(gint i = 0; keys != NULL && keys[i] != NULL; i++)
	{
		const gchar *k = keys[i];
		gchar *raw = g_key_file_get_value(kf, old, k, NULL);
		gchar *val = unquote(raw);
		gboolean b;

		if(g_str_has_prefix(k, "term_foreground_") || g_str_has_prefix(k, "term_background_") || g_strcmp0(k, "show_rxtx") == 0 ||
		   g_strcmp0(k, "update_skipped_revision") == 0)
			;	/* couleurs traitées ensemble ci-dessous ; show_rxtx et update_skipped_revision (ancien numéro de révision) abandonnées */
		else if(in_list(update_keys, k))
		{
			if(update_source)
			{
				if(g_strcmp0(k, "update_check_startup") == 0)
				{
					if(gt_config_parse_boolean(val, &b))
						g_key_file_set_boolean(update_holder, "x", k, b);
				}
				else
					g_key_file_set_string(update_holder, "x", k, val);
			}
		}
		else if(g_strcmp0(k, "transport_type") == 0)
		{
			gint64 t;

			if(!g_ascii_string_to_signed(val, 10, G_MININT, G_MAXINT, &t, NULL))
				g_warning("%s: [%s] %s=%s ignored (not a number)", s->path, old, k, val);
			else if(t >= TRANSPORT_SERIAL && t <= TRANSPORT_TCP_SERVER)
				g_key_file_set_string(kf, group, "transport", gt_transport_to_string((TransportType)t));
		}
		else if(g_strcmp0(k, "term_rows") == 0)
			g_key_file_set_string(kf, group, "term_columns", val);	/* l'ancien format inversait lignes et colonnes (M14) */
		else if(g_strcmp0(k, "term_columns") == 0)
			g_key_file_set_string(kf, group, "term_rows", val);
		else if(in_list(legacy_bool_keys, k))
		{
			if(gt_config_parse_boolean(val, &b))
				g_key_file_set_boolean(kf, group, k, b);
		}
		else if(g_strcmp0(k, "wait_char") == 0)
		{
			gint64 w;

			if(!g_ascii_string_to_signed(val, 10, G_MININT, G_MAXINT, &w, NULL))
				g_warning("%s: [%s] %s=%s ignored (not a number)", s->path, old, k, val);
			else
				g_key_file_set_integer(kf, group, k, config_wait_char_from_file((gint)w));
		}
		else
			g_key_file_set_string(kf, group, k, val);
		g_free(val);
		g_free(raw);
	}
	migrate_colors(kf, old, group, "term_foreground_", "term_foreground", def.foreground);
	migrate_colors(kf, old, group, "term_background_", "term_background", def.background);
	g_key_file_remove_group(kf, old, NULL);

	gt_profile_clear(&def);
	g_strfreev(keys);
	g_free(group);
	g_free(unique);
	g_free(name);
}

/* Migre en mémoire les profils de l'ancien format (parsecfg) ; TRUE si au moins un profil a été migré */
static gboolean store_migrate(GtConfigStore *s)
{
	gchar **groups = g_key_file_get_groups(s->kf, NULL);
	GPtrArray *legacy = g_ptr_array_new_with_free_func(g_free);
	GKeyFile *update_holder = g_key_file_new();
	const gchar *source = NULL;

	for(gint i = 0; groups[i] != NULL; i++)
		if(is_legacy_profile(s->kf, groups[i]))
			g_ptr_array_add(legacy, g_strdup(groups[i]));
	g_strfreev(groups);

	/* Réglages de mise à jour : ceux de « default », sinon du premier ancien profil */
	for(guint i = 0; i < legacy->len && source == NULL; i++)
	{
		gchar *n = unquote(g_ptr_array_index(legacy, i));

		if(g_ascii_strcasecmp(n, "default") == 0)
			source = g_ptr_array_index(legacy, i);
		g_free(n);
	}
	if(source == NULL && legacy->len > 0)
		source = g_ptr_array_index(legacy, 0);

	for(guint i = 0; i < legacy->len; i++)
		migrate_profile(s, g_ptr_array_index(legacy, i), g_ptr_array_index(legacy, i) == source, update_holder);

	/* Écrire les réglages de mise à jour après suppression de tous les groupes anciens */
	gchar **update_group_keys = g_key_file_get_keys(update_holder, "x", NULL, NULL);
	for(gint i = 0; update_group_keys != NULL && update_group_keys[i] != NULL; i++)
	{
		const gchar *k = update_group_keys[i];

		if(!g_key_file_has_key(s->kf, GENERAL_GROUP, k, NULL))
		{
			if(g_strcmp0(k, "update_check_startup") == 0)
			{
				gboolean b = g_key_file_get_boolean(update_holder, "x", k, NULL);
				g_key_file_set_boolean(s->kf, GENERAL_GROUP, k, b);
			}
			else
			{
				gchar *v = g_key_file_get_string(update_holder, "x", k, NULL);
				g_key_file_set_string(s->kf, GENERAL_GROUP, k, v);
				g_free(v);
			}
		}
	}
	g_strfreev(update_group_keys);
	g_key_file_free(update_holder);

	gboolean migrated = legacy->len > 0;
	g_ptr_array_unref(legacy);
	return migrated;
}

/* (Re)lit le fichier et remplace le contenu en mémoire. L'identité est relevée avant la lecture : une écriture concurrente pendant
 * la lecture sera vue à la relecture suivante. FALSE (GError) si le fichier est illisible : magasin vide en lecture seule. */
static gboolean store_read(GtConfigStore *s, GError **error)
{
	GError *local = NULL;
	gchar *contents = NULL;
	gsize len = 0;

	if(s->kf != NULL)
		g_key_file_free(s->kf);
	s->kf = g_key_file_new();
	s->read_only = FALSE;
	s->migrated = FALSE;
	s->backup_done = FALSE;
	g_clear_pointer(&s->original_contents, g_free);
	g_clear_pointer(&s->saved_text, g_free);
	g_clear_error(&s->read_error);
	gt_file_stamp_take(s->path, &s->identity);

	if(!g_file_get_contents(s->path, &contents, &len, &local))
	{
		if(g_error_matches(local, G_FILE_ERROR, G_FILE_ERROR_NOENT))
		{
			g_error_free(local);
			s->saved_text = g_strdup("");
			return TRUE;
		}
		s->read_only = TRUE;
		s->saved_text = g_strdup("");
		s->read_error = g_error_copy(local);
		g_propagate_error(error, local);
		return FALSE;
	}

	if(len > 0 && !g_key_file_load_from_data(s->kf, contents, len, G_KEY_FILE_KEEP_COMMENTS, &local))
	{
		g_key_file_free(s->kf);
		s->kf = g_key_file_new();
		s->read_only = TRUE;
		s->saved_text = g_strdup("");
		s->read_error = g_error_copy(local);
		g_prefix_error(&s->read_error, "%s: ", s->path);
		g_propagate_prefixed_error(error, local, "%s: ", s->path);
		g_free(contents);
		return FALSE;
	}

	if(store_migrate(s))
	{
		s->migrated = TRUE;
		s->original_contents = contents;
	}
	else
		g_free(contents);
	s->saved_text = g_key_file_to_data(s->kf, NULL, NULL);
	return TRUE;
}

GtConfigStore *gt_config_store_load(const gchar *path, GError **error)
{
	GtConfigStore *s = g_new0(GtConfigStore, 1);

	s->path = g_strdup(path);
	store_read(s, error);
	return s;
}

gboolean gt_config_store_reload_if_changed(GtConfigStore *s, GError **error)
{
	if(!gt_file_stamp_changed(s->path, &s->identity))
		return TRUE;
	return store_read(s, error);
}

void gt_config_store_free(GtConfigStore *s)
{
	if(s == NULL)
		return;
	g_key_file_free(s->kf);
	g_free(s->original_contents);
	g_free(s->saved_text);
	g_clear_error(&s->read_error);
	g_free(s->path);
	g_free(s);
}

gboolean gt_config_store_is_read_only(GtConfigStore *s)
{
	return s->read_only;
}

gboolean gt_config_store_was_migrated(GtConfigStore *s)
{
	return s->migrated;
}

const gchar *gt_config_store_get_path(GtConfigStore *s)
{
	return s->path;
}

const GError *gt_config_store_get_read_error(GtConfigStore *s)
{
	return s->read_error;
}

gchar **gt_config_store_list_profiles(GtConfigStore *s)
{
	gchar **groups = g_key_file_get_groups(s->kf, NULL);
	GPtrArray *names = g_ptr_array_new();

	for(gint i = 0; groups[i] != NULL; i++)
		if(g_str_has_prefix(groups[i], PROFILE_PREFIX))
			g_ptr_array_add(names, g_strdup(groups[i] + strlen(PROFILE_PREFIX)));
	g_strfreev(groups);
	g_ptr_array_sort(names, compare_names);
	g_ptr_array_add(names, NULL);
	return (gchar **)g_ptr_array_free(names, FALSE);
}

gboolean gt_config_store_has_profile(GtConfigStore *s, const gchar *name)
{
	gchar *group = find_prefixed_group(s, PROFILE_PREFIX, name);
	gboolean found = (group != NULL);

	g_free(group);
	return found;
}

gboolean gt_config_store_get_profile(GtConfigStore *s, const gchar *name, GtProfile *out)
{
	gchar *group = find_prefixed_group(s, PROFILE_PREFIX, name);

	if(group == NULL)
		return FALSE;
	gt_profile_clear(out);
	gt_profile_init_defaults(out);
	gt_profile_from_key_file(s->kf, group, out);
	g_free(group);
	return TRUE;
}

void gt_config_store_put_profile(GtConfigStore *s, const gchar *name, const GtProfile *p)
{
	gchar *group = profile_group_for_write(s, name);

	gt_profile_to_key_file(p, s->kf, group);
	g_free(group);
}

gboolean gt_config_store_delete_profile(GtConfigStore *s, const gchar *name)
{
	gchar *group = find_prefixed_group(s, PROFILE_PREFIX, name);
	gboolean done = FALSE;

	if(group != NULL)
		done = g_key_file_remove_group(s->kf, group, NULL);
	g_free(group);
	return done;
}

void gt_config_store_set_profile_string(GtConfigStore *s, const gchar *name, const gchar *key, const gchar *value)
{
	gchar *group = profile_group_for_write(s, name);

	if(value != NULL)
		g_key_file_set_string(s->kf, group, key, value);
	else if(g_key_file_has_group(s->kf, group))
		g_key_file_remove_key(s->kf, group, key, NULL);
	g_free(group);
}

gchar *gt_config_store_get_general_string(GtConfigStore *s, const gchar *key, const gchar *fallback)
{
	gchar *v = g_key_file_get_string(s->kf, GENERAL_GROUP, key, NULL);

	return v != NULL ? v : g_strdup(fallback);
}

gboolean gt_config_store_get_general_boolean(GtConfigStore *s, const gchar *key, gboolean fallback)
{
	gboolean v = fallback;

	gt_key_file_read_bool(s->kf, GENERAL_GROUP, key, &v);
	return v;
}

void gt_config_store_set_general_string(GtConfigStore *s, const gchar *key, const gchar *value)
{
	g_key_file_set_string(s->kf, GENERAL_GROUP, key, value != NULL ? value : "");
}

void gt_config_store_set_general_boolean(GtConfigStore *s, const gchar *key, gboolean value)
{
	g_key_file_set_boolean(s->kf, GENERAL_GROUP, key, value);
}

GKeyFile *gt_config_store_get_key_file(GtConfigStore *s)
{
	return s->kf;
}

/* Préfixe le message d'erreur par le chemin du fichier visé, sauf s'il le cite déjà : les erreurs de g_file_set_contents_full
 * ne nomment souvent que le fichier temporaire « chemin.XXXXXX », qui ne compte pas */
static void prefix_path(GError **error, const gchar *path)
{
	gsize n = strlen(path);

	if(error == NULL || *error == NULL)
		return;
	for(const gchar *m = strstr((*error)->message, path); m != NULL; m = strstr(m + 1, path))
		if(m[n] != '.')
			return;
	g_prefix_error(error, "%s: ", path);
}

/* Un enregistrement a échoué ou a été refusé : le contenu en mémoire revient à celui de la dernière lecture ou écriture réussie (vide si le
 * fichier était absent ou illisible) */
static void store_rollback(GtConfigStore *s)
{
	GKeyFile *kf;

	if(s->saved_text == NULL)
		return;
	kf = g_key_file_new();
	if(g_key_file_load_from_data(kf, s->saved_text, -1, G_KEY_FILE_KEEP_COMMENTS, NULL))
	{
		g_key_file_free(s->kf);
		s->kf = kf;
	}
	else
		g_key_file_free(kf);
}

static gboolean store_save(GtConfigStore *s, GError **error)
{
	gchar *dir, *data;
	gsize len;
	gboolean ok;

	if(s->read_only)
	{
		g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_PERM, _("The configuration file %s could not be read: nothing has been written to it."), s->path);
		return FALSE;
	}

	dir = g_path_get_dirname(s->path);
	if(g_mkdir_with_parents(dir, 0700) != 0)
	{
		gint err = errno;

		g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(err), _("Cannot create directory %s: %s"), dir, g_strerror(err));
		g_free(dir);
		return FALSE;
	}
	g_free(dir);

	if(s->migrated && !s->backup_done)
	{
		gchar *backup = g_strconcat(s->path, ".avant-migration", NULL);

		/* Une seule copie : jamais écrasée si elle existe déjà */
		if(!g_file_test(backup, G_FILE_TEST_EXISTS)
		   && !g_file_set_contents_full(backup, s->original_contents, -1, G_FILE_SET_CONTENTS_CONSISTENT, 0600, error))
		{
			prefix_path(error, backup);
			g_free(backup);
			return FALSE;
		}
		g_free(backup);
		s->backup_done = TRUE;
	}

	data = g_key_file_to_data(s->kf, &len, NULL);
	ok = g_file_set_contents_full(s->path, data, (gssize)len, G_FILE_SET_CONTENTS_CONSISTENT, 0600, error);
	g_free(data);
	if(ok)
		gt_file_stamp_take(s->path, &s->identity);	/* son propre enregistrement ne déclenche pas de relecture */
	else
		prefix_path(error, s->path);
	return ok;
}

gboolean gt_config_store_save(GtConfigStore *s, GError **error)
{
	gboolean ok = store_save(s, error);

	if(ok)
	{
		g_free(s->saved_text);
		s->saved_text = g_key_file_to_data(s->kf, NULL, NULL);
	}
	else
		store_rollback(s);
	return ok;
}
