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
#include <sys/stat.h>

#include <config.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include "config_file.h"
#include "config_store.h"

#define PROFILE_PREFIX "profile:"
#define GENERAL_GROUP "general"

/* Identité du fichier au dernier chargement ou enregistrement : l'écriture atomique (fichier temporaire renommé) change l'inode,
 * une modification sur place change la taille ou la date de modification */
typedef struct
{
	gboolean exists;
	dev_t dev;
	ino_t ino;
	off_t size;
	struct timespec mtime;
} FileIdentity;

struct _GtConfigStore
{
	gchar *path;
	GKeyFile *kf;
	gboolean read_only;
	gboolean migrated;
	gboolean backup_done;
	gchar *original_contents;	/* fichier d'origine d'un magasin migré : copie de sauvegarde */
	FileIdentity identity;
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

/* ---- Lecture tolérante d'un profil -------------------------------------- */

static void read_string(GKeyFile *kf, const gchar *group, const gchar *key, gchar **field)
{
	gchar *v = g_key_file_get_string(kf, group, key, NULL);

	if(v != NULL)
	{
		g_free(*field);
		*field = v;
	}
}

static void read_int(GKeyFile *kf, const gchar *group, const gchar *key, gint *field)
{
	gchar *v = g_key_file_get_value(kf, group, key, NULL);
	gchar *end = NULL;
	gint64 n;

	if(v == NULL)
		return;
	n = g_ascii_strtoll(g_strstrip(v), &end, 10);
	if(end != v && *end == '\0' && n >= G_MININT && n <= G_MAXINT)
		*field = (gint)n;
	g_free(v);
}

static void read_uint(GKeyFile *kf, const gchar *group, const gchar *key, guint *field)
{
	gint n = -1;

	read_int(kf, group, key, &n);
	if(n > 0)
		*field = (guint)n;
}

static void read_bool(GKeyFile *kf, const gchar *group, const gchar *key, gboolean *field)
{
	gchar *v = g_key_file_get_value(kf, group, key, NULL);

	if(v != NULL)
		gt_config_parse_boolean(g_strstrip(v), field);
	g_free(v);
}

static void read_color(GKeyFile *kf, const gchar *group, const gchar *key, GtColor *c)
{
	gsize n = 0;
	gdouble *v = g_key_file_get_double_list(kf, group, key, &n, NULL);

	if(v != NULL && n == 4)
		*c = (GtColor){ v[0], v[1], v[2], v[3] };
	g_free(v);
}

static void read_profile(GKeyFile *kf, const gchar *g, GtProfile *p)
{
	gchar *s = NULL;

	read_string(kf, g, "transport", &s);
	gt_transport_from_string(s, &p->transport);
	g_clear_pointer(&s, g_free);
	read_string(kf, g, "port", &p->port);
	read_uint(kf, g, "speed", &p->baudrate);
	read_int(kf, g, "bits", &p->bits);
	read_int(kf, g, "stopbits", &p->stopbits);
	read_string(kf, g, "parity", &s);
	gt_parity_from_string(s, &p->parity);
	g_clear_pointer(&s, g_free);
	read_string(kf, g, "flow", &s);
	gt_flow_from_string(s, &p->flow);
	g_clear_pointer(&s, g_free);
	read_int(kf, g, "wait_delay", &p->wait_delay_ms);
	if(g_key_file_has_key(kf, g, "wait_char", NULL))
	{
		gint w = -1;

		read_int(kf, g, "wait_char", &w);
		p->wait_char = config_wait_char_from_file(w);
	}
	read_int(kf, g, "rs485_rts_time_before_tx", &p->rs485_before_ms);
	read_int(kf, g, "rs485_rts_time_after_tx", &p->rs485_after_ms);
	read_string(kf, g, "socket_host", &p->host);
	read_string(kf, g, "socket_port", &p->tcp_port);
	read_bool(kf, g, "echo", &p->echo);
	read_bool(kf, g, "crlfauto", &p->crlf_auto);
	read_bool(kf, g, "autoreconnect_enabled", &p->auto_reconnect);
	read_bool(kf, g, "esc_clear_screen", &p->esc_clear_screen);
	read_bool(kf, g, "timestamp", &p->timestamp);
	read_string(kf, g, "font", &p->font);
	read_bool(kf, g, "term_block_cursor", &p->block_cursor);
	read_int(kf, g, "term_columns", &p->columns);
	read_int(kf, g, "term_rows", &p->rows);
	read_int(kf, g, "term_scrollback", &p->scrollback);
	read_bool(kf, g, "term_visual_bell", &p->visual_bell);
	read_color(kf, g, "term_foreground", &p->foreground);
	read_color(kf, g, "term_background", &p->background);
	read_string(kf, g, "macros_file", &p->macros_file);
}

static void write_color(GKeyFile *kf, const gchar *group, const gchar *key, const GtColor *c)
{
	gdouble v[4] = { c->red, c->green, c->blue, c->alpha };

	g_key_file_set_double_list(kf, group, key, v, 4);
}

static void write_profile(GKeyFile *kf, const gchar *g, const GtProfile *p)
{
	g_key_file_set_string(kf, g, "transport", gt_transport_to_string(p->transport));
	g_key_file_set_string(kf, g, "port", p->port != NULL ? p->port : "");
	g_key_file_set_integer(kf, g, "speed", (gint)p->baudrate);
	g_key_file_set_integer(kf, g, "bits", p->bits);
	g_key_file_set_integer(kf, g, "stopbits", p->stopbits);
	g_key_file_set_string(kf, g, "parity", gt_parity_to_string(p->parity));
	g_key_file_set_string(kf, g, "flow", gt_flow_to_string(p->flow));
	g_key_file_set_integer(kf, g, "wait_delay", p->wait_delay_ms);
	g_key_file_set_integer(kf, g, "wait_char", p->wait_char);
	g_key_file_set_integer(kf, g, "rs485_rts_time_before_tx", p->rs485_before_ms);
	g_key_file_set_integer(kf, g, "rs485_rts_time_after_tx", p->rs485_after_ms);
	g_key_file_set_string(kf, g, "socket_host", p->host != NULL ? p->host : "");
	g_key_file_set_string(kf, g, "socket_port", p->tcp_port != NULL ? p->tcp_port : "");
	g_key_file_set_boolean(kf, g, "echo", p->echo);
	g_key_file_set_boolean(kf, g, "crlfauto", p->crlf_auto);
	g_key_file_set_boolean(kf, g, "autoreconnect_enabled", p->auto_reconnect);
	g_key_file_set_boolean(kf, g, "esc_clear_screen", p->esc_clear_screen);
	g_key_file_set_boolean(kf, g, "timestamp", p->timestamp);
	g_key_file_set_string(kf, g, "font", p->font != NULL ? p->font : GT_PROFILE_DEFAULT_FONT);
	g_key_file_set_boolean(kf, g, "term_block_cursor", p->block_cursor);
	g_key_file_set_integer(kf, g, "term_columns", p->columns);
	g_key_file_set_integer(kf, g, "term_rows", p->rows);
	g_key_file_set_integer(kf, g, "term_scrollback", p->scrollback);
	g_key_file_set_boolean(kf, g, "term_visual_bell", p->visual_bell);
	write_color(kf, g, "term_foreground", &p->foreground);
	write_color(kf, g, "term_background", &p->background);
	if(p->macros_file != NULL)
		g_key_file_set_string(kf, g, "macros_file", p->macros_file);
	else
		g_key_file_remove_key(kf, g, "macros_file", NULL);
}

/* ---- Interface publique ------------------------------------------------- */

static const gchar * const reserved_groups[] = { "window", "colors", "layout", "session", NULL };
static const gchar * const legacy_marker_keys[] = { "port", "speed", "transport_type", "socket_host", "font", "term_rows", NULL };
static const gchar * const general_marker_keys[] = { "port", "speed", "transport_type", NULL };
static const gchar * const legacy_bool_keys[] = { "echo", "crlfauto", "autoreconnect_enabled", "esc_clear_screen", "timestamp", "term_block_cursor",
                                                  "term_visual_bell", NULL };
static const gchar * const update_keys[] = { "update_url", "update_prefix", "update_check_startup", NULL };

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

		if(g_str_has_prefix(k, "term_foreground_") || g_str_has_prefix(k, "term_background_") || g_strcmp0(k, "show_rxtx") == 0)
			;	/* couleurs traitées ensemble ci-dessous ; show_rxtx abandonnée */
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
			gint t = atoi(val);

			if(t >= TRANSPORT_SERIAL && t <= TRANSPORT_TCP_SERVER)
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
			g_key_file_set_integer(kf, group, k, config_wait_char_from_file(atoi(val)));
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

static void identity_take(const gchar *path, FileIdentity *id)
{
	struct stat st;

	*id = (FileIdentity){ 0 };
	if(stat(path, &st) != 0)
		return;
	id->exists = TRUE;
	id->dev = st.st_dev;
	id->ino = st.st_ino;
	id->size = st.st_size;
	id->mtime = st.st_mtim;
}

static gboolean identity_equal(const FileIdentity *a, const FileIdentity *b)
{
	if(a->exists != b->exists)
		return FALSE;
	if(!a->exists)
		return TRUE;
	return a->dev == b->dev && a->ino == b->ino && a->size == b->size && a->mtime.tv_sec == b->mtime.tv_sec && a->mtime.tv_nsec == b->mtime.tv_nsec;
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
	identity_take(s->path, &s->identity);

	if(!g_file_get_contents(s->path, &contents, &len, &local))
	{
		if(g_error_matches(local, G_FILE_ERROR, G_FILE_ERROR_NOENT))
		{
			g_error_free(local);
			return TRUE;
		}
		s->read_only = TRUE;
		g_propagate_error(error, local);
		return FALSE;
	}

	if(len > 0 && !g_key_file_load_from_data(s->kf, contents, len, G_KEY_FILE_KEEP_COMMENTS, &local))
	{
		g_key_file_free(s->kf);
		s->kf = g_key_file_new();
		s->read_only = TRUE;
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
	FileIdentity now;

	identity_take(s->path, &now);
	if(identity_equal(&now, &s->identity))
		return TRUE;
	return store_read(s, error);
}

void gt_config_store_free(GtConfigStore *s)
{
	if(s == NULL)
		return;
	g_key_file_free(s->kf);
	g_free(s->original_contents);
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
	read_profile(s->kf, group, out);
	g_free(group);
	return TRUE;
}

void gt_config_store_put_profile(GtConfigStore *s, const gchar *name, const GtProfile *p)
{
	gchar *group = profile_group_for_write(s, name);

	write_profile(s->kf, group, p);
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

	read_bool(s->kf, GENERAL_GROUP, key, &v);
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

gboolean gt_config_store_save(GtConfigStore *s, GError **error)
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
		identity_take(s->path, &s->identity);	/* son propre enregistrement ne déclenche pas de relecture */
	else
		prefix_path(error, s->path);
	return ok;
}
