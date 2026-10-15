/* macro_file.c — fichier de macros (voir macro_file.h et le §5 de la spec du sous-projet 4). */

#include <config.h>
#include <errno.h>
#include <glib/gi18n.h>
#include <stdlib.h>
#include <string.h>

#include "macro_file.h"

#define HEADER_GROUP "gtkterm-macros"
#define FILE_VERSION 2
#define BACKUP_SUFFIX ".avant-migration"

/* Clé absente : "". Échappement inconnu (« action=AT\x0D » tapé à la main) : le texte brut, qui est la syntaxe des macros, plutôt
 * qu'un champ vidé puis enregistré vide. */
static gchar *
get_string_or_empty(GKeyFile *kf, const gchar *group, const gchar *key)
{
	GError *local = NULL;
	gchar *s = g_key_file_get_string(kf, group, key, &local);

	if(g_error_matches(local, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE))
		s = g_key_file_get_value(kf, group, key, NULL);
	g_clear_error(&local);
	return s != NULL ? s : g_strdup("");
}

/* Période lue comme texte : g_key_file_get_uint64 accepterait « -5 » (g_ascii_strtoull) */
static guint
get_period(GKeyFile *kf, const gchar *group)
{
	gchar *s = g_key_file_get_value(kf, group, "polling_period_ms", NULL);
	guint64 v = 0;
	guint period = GT_MACRO_PERIOD_DEFAULT_MS;

	if(s != NULL && g_ascii_string_to_unsigned(g_strstrip(s), 10, 0, G_MAXUINT64, &v, NULL) && v > 0)
		period = (guint)MIN(v, (guint64)GT_MACRO_PERIOD_MAX_MS);
	g_free(s);
	return period;
}

/* Liste « a;b;c » : g_key_file_get_string_list, sinon (échappement inconnu) éléments bruts coupés aux « ; » non échappés, « \; » → « ; » */
static gchar **
get_list_tolerant(GKeyFile *kf, const gchar *group, const gchar *key, const gchar *origin, gsize *n_out)
{
	GError *local = NULL;
	gchar **v;
	gchar *raw, *msg;
	GPtrArray *items;
	GString *cur;

	*n_out = 0;
	v = g_key_file_get_string_list(kf, group, key, n_out, &local);
	if(local == NULL || !g_error_matches(local, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE))
	{
		g_clear_error(&local);
		return v;
	}
	g_clear_error(&local);
	raw = g_key_file_get_value(kf, group, key, NULL);
	msg = g_strdup_printf(_("%s: [%s] %s: invalid escape, value kept as written"), origin, group, key);
	g_warning("%s", msg);
	g_free(msg);
	items = g_ptr_array_new();
	cur = g_string_new(NULL);
	for(const gchar *c = raw != NULL ? raw : ""; *c != '\0'; c++)
	{
		if(c[0] == '\\' && c[1] == ';')
		{
			g_string_append_c(cur, ';');
			c++;
		}
		else if(c[0] == ';')
		{
			g_ptr_array_add(items, g_string_free(cur, FALSE));
			cur = g_string_new(NULL);
		}
		else
			g_string_append_c(cur, c[0]);
	}
	if(cur->len > 0)
		g_ptr_array_add(items, g_string_free(cur, FALSE));
	else
		g_string_free(cur, TRUE);
	*n_out = items->len;
	g_ptr_array_add(items, NULL);
	g_free(raw);
	return (gchar **)g_ptr_array_free(items, FALSE);
}

/* Le groupe « <prefix><kind> » (kind : « macro: » ou « list: ») ? */
static gboolean
group_is(const gchar *group, const gchar *prefix, const gchar *kind)
{
	return g_str_has_prefix(group, prefix) && g_str_has_prefix(group + strlen(prefix), kind);
}

GtMacroSet *
gt_macro_set_from_key_file(GKeyFile *kf, const gchar *prefix, const gchar *origin)
{
	gchar **groups = g_key_file_get_groups(kf, NULL);
	GtMacroLists *lists;
	GtMacroSet *set;

	/* Les listes d'abord : « %#Nom » n'est un argument que si la liste existe (décompte des arguments enregistrés) */
	lists = gt_macro_lists_new();
	for(guint g = 0; groups[g] != NULL; g++)
	{
		gchar *name;
		gchar **displays, **values;
		gsize n_displays = 0, n_values = 0;

		if(!group_is(groups[g], prefix, "list:"))
			continue;
		name = get_string_or_empty(kf, groups[g], "name");
		displays = get_list_tolerant(kf, groups[g], "displays", origin, &n_displays);
		values = get_list_tolerant(kf, groups[g], "values", origin, &n_values);
		/* Une liste sans entrée existe quand même */
		if(name[0] != '\0')
			gt_macro_lists_add_list(lists, name);
		for(gsize k = 0; name[0] != '\0' && k < n_displays; k++)
			gt_macro_lists_add(lists, name, displays[k], k < n_values ? values[k] : NULL);
		g_strfreev(displays);
		g_strfreev(values);
		g_free(name);
	}
	set = gt_macro_set_new();
	gt_macro_set_take_lists(set, lists);
	for(guint g = 0; groups[g] != NULL; g++)
	{
		gchar *label, *shortcut, *action, *tab;
		gchar **args;
		gsize n_args = 0, n_lim = 0;
		GError *local = NULL;
		gboolean polling;
		guint id;

		if(!group_is(groups[g], prefix, "macro:"))
			continue;
		label = get_string_or_empty(kf, groups[g], "label");
		shortcut = get_string_or_empty(kf, groups[g], "shortcut");
		action = get_string_or_empty(kf, groups[g], "action");
		tab = get_string_or_empty(kf, groups[g], "tab");
		id = gt_macro_set_append(set, label, shortcut, action, tab);
		args = get_list_tolerant(kf, groups[g], "args", origin, &n_args);
		/* Refusé (arguments abandonnés) si leur nombre ne correspond pas aux spécificateurs */
		if(args != NULL)
			gt_macro_set_set_args(set, id, (const gchar *const *)args, (gint)n_args);
		/* Limites des arguments : une limite invalide (ou un vecteur de mauvaise longueur) est remplacée par aucune, avec avertissement */
		{
			static const gchar *const keys[3] = { "arg_min", "arg_max", "arg_charset" };
			gchar **vec[3];
			gint rc;

			for(gint v = 0; v < 3; v++)
			{
				gsize len = 0;

				vec[v] = get_list_tolerant(kf, groups[g], keys[v], origin, &len);
				if(vec[v] != NULL && n_lim == 0)
					n_lim = len;
			}
			if(vec[0] != NULL || vec[1] != NULL || vec[2] != NULL)
			{
				rc = gt_macro_set_set_limits(set, id, (const gchar *const *)vec[0], (const gchar *const *)vec[1],
				                             (const gchar *const *)vec[2], (gint)n_lim);
				if(rc != 0)
				{
					gchar *msg = g_strdup_printf(_("%s: [%s] invalid argument limits replaced by none"), origin, groups[g]);

					g_warning("%s", msg);
					g_free(msg);
				}
			}
			for(gint v = 0; v < 3; v++)
				g_strfreev(vec[v]);
		}
		polling = g_key_file_get_boolean(kf, groups[g], "polling", &local);
		if(local != NULL)
		{
			polling = FALSE;
			g_clear_error(&local);
		}
		gt_macro_set_set_polling(set, id, polling, get_period(kf, groups[g]));
		g_strfreev(args);
		g_free(label);
		g_free(shortcut);
		g_free(action);
		g_free(tab);
	}
	g_strfreev(groups);
	/* Regroupement par onglet à la lecture (spec 2026-10-15-nom-projet §2.2) : .ini, modèle, base en cache, jeu intégré d'un projet */
	gt_macro_set_group_by_tab(set);
	return set;
}

static GtMacroSet *
load_v2(GKeyFile *kf, const gchar *path, GError **error)
{
	gchar *version = g_key_file_get_value(kf, HEADER_GROUP, "version", NULL);
	gboolean known = version != NULL && strcmp(g_strstrip(version), "2") == 0;

	g_free(version);
	if(!known)
	{
		g_set_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE, _("Unsupported macros file version (written by a newer GTKTerm?)"));
		g_prefix_error(error, "%s: ", path);
		return NULL;
	}
	return gt_macro_set_from_key_file(kf, "", path);
}

/* Ancien format, ligne de [macros] : « libellé::raccourci::action::onglet::arg1|arg2::polling(0/1)::période », les champs de
 * queue sont facultatifs et le dernier prend le reste de la ligne (M21 : séparateurs jamais échappés, lus tels quels). */
static void
legacy_add_macro(GtMacroSet *set, const gchar *value)
{
	gchar **f = g_strsplit(value, "::", 7);
	guint n = g_strv_length(f);
	guint id;

	/* Valeur vide ou sans au moins deux « :: » : pas une macro */
	if(n < 3)
	{
		g_strfreev(f);
		return;
	}
	id = gt_macro_set_append(set, f[0], f[1], f[2], n > 3 ? f[3] : "");
	if(n > 4)
	{
		gchar **args = g_strsplit(f[4], "|", -1);

		gt_macro_set_set_args(set, id, (const gchar *const *)args, (gint)g_strv_length(args));
		g_strfreev(args);
	}
	gt_macro_set_set_polling(set, id, n > 5 && strcmp(f[5], "1") == 0, n > 6 ? (guint)MIN(strtoul(f[6], NULL, 10), (gulong)GT_MACRO_PERIOD_MAX_MS) : 0);
	g_strfreev(f);
}

/* Ancien format, ligne de [lists] : « Nom:affichage=valeur|affichage » */
static void
legacy_add_list(GtMacroLists *lists, const gchar *value)
{
	const gchar *colon = strchr(value, ':');
	gchar *name;
	gchar **entries;

	if(colon == NULL || colon == value)
		return;
	name = g_strndup(value, colon - value);
	entries = g_strsplit(colon + 1, "|", -1);
	for(guint i = 0; entries[i] != NULL; i++)
	{
		gchar *eq = strchr(entries[i], '=');

		if(eq == NULL)
			gt_macro_lists_add(lists, name, entries[i], entries[i]);
		else
		{
			*eq = '\0';
			gt_macro_lists_add(lists, name, entries[i], eq + 1);
		}
	}
	g_strfreev(entries);
	g_free(name);
}

static void
legacy_read_group(GKeyFile *kf, const gchar *group, void (*add)(gpointer target, const gchar *value), gpointer target)
{
	gchar **keys = g_key_file_get_keys(kf, group, NULL, NULL);

	for(guint i = 0; keys != NULL && keys[i] != NULL; i++)
	{
		gchar *value = g_key_file_get_string(kf, group, keys[i], NULL);

		if(value != NULL && value[0] != '\0')
			add(target, value);
		g_free(value);
	}
	g_strfreev(keys);
}

static void
legacy_add_list_cb(gpointer target, const gchar *value)
{
	legacy_add_list(target, value);
}

static void
legacy_add_macro_cb(gpointer target, const gchar *value)
{
	legacy_add_macro(target, value);
}

static GtMacroSet *
load_legacy(GKeyFile *kf)
{
	GtMacroSet *set = gt_macro_set_new();
	GtMacroLists *lists = gt_macro_lists_new();

	/* Sans [macros], le jeu n'a aucune macro : plus de mélange « anciennes macros, nouvelles listes » (M21) */
	legacy_read_group(kf, "lists", legacy_add_list_cb, lists);
	gt_macro_set_take_lists(set, lists);
	legacy_read_group(kf, "macros", legacy_add_macro_cb, set);
	gt_macro_set_group_by_tab(set);
	return set;
}

gboolean
gt_macro_file_load(const gchar *path, GtMacroSet **set_out, GError **error)
{
	gchar *data = NULL;
	gsize len = 0;
	GKeyFile *kf;
	GtMacroSet *set;
	GError *local = NULL;
	gchar **groups;
	gboolean no_group;

	if(!g_file_get_contents(path, &data, &len, &local))
	{
		/* Droits insuffisants : la cause réelle (chemin et motif du système) plutôt que le texte générique de GLib */
		if(g_error_matches(local, G_FILE_ERROR, G_FILE_ERROR_ACCES) || g_error_matches(local, G_FILE_ERROR, G_FILE_ERROR_PERM))
		{
			gint err = g_error_matches(local, G_FILE_ERROR, G_FILE_ERROR_ACCES) ? EACCES : EPERM;

			g_set_error(error, G_FILE_ERROR, local->code, "%s: %s", path, g_strerror(err));
			g_clear_error(&local);
		}
		else
			g_propagate_error(error, local);
		return FALSE;
	}
	kf = g_key_file_new();
	if(len > 0 && !g_key_file_load_from_data(kf, data, len, G_KEY_FILE_NONE, &local))
	{
		g_propagate_prefixed_error(error, local, "%s: ", path);
		g_key_file_free(kf);
		g_free(data);
		return FALSE;
	}
	g_free(data);
	groups = g_key_file_get_groups(kf, NULL);
	no_group = groups[0] == NULL;
	g_strfreev(groups);
	if(g_key_file_has_group(kf, HEADER_GROUP))
		set = load_v2(kf, path, error);
	else if(g_key_file_has_group(kf, "macros") || g_key_file_has_group(kf, "lists") || no_group)
		set = load_legacy(kf);	/* ancien format, ou fichier vide (sans aucun groupe) : jeu vide */
	else
	{
		/* Des groupes, aucun de connu : fichier étranger refusé, jamais lu comme un jeu vide qui l'écraserait ensuite */
		g_set_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_GROUP_NOT_FOUND, _("%s is not a GTKTerm macros file"), path);
		set = NULL;
	}
	g_key_file_free(kf);
	if(set == NULL)
		return FALSE;
	*set_out = set;
	return TRUE;
}

/* Fichier v2 lisible : syntaxe valide, en-tête présent, version connue (ce que load_v2 accepte) */
static gboolean
is_readable_v2(const gchar *data, gsize len)
{
	GKeyFile *kf = g_key_file_new();
	gboolean ok = g_key_file_load_from_data(kf, data, len, G_KEY_FILE_NONE, NULL) && g_key_file_has_group(kf, HEADER_GROUP);
	gchar *version = ok ? g_key_file_get_value(kf, HEADER_GROUP, "version", NULL) : NULL;

	ok = version != NULL && strcmp(g_strstrip(version), "2") == 0;
	g_free(version);
	g_key_file_free(kf);
	return ok;
}

/* Avant d'écraser un fichier présent, non vide, qui n'est pas un v2 lisible (ancien format, syntaxe invalide, version inconnue,
 * fichier étranger), on le copie à l'identique dans <fichier>.avant-migration, une seule fois. Une copie existante n'est jamais
 * écrasée : identique au fichier, l'enregistrement continue ; différente, il est refusé (rien n'est écrit). Fichier illisible
 * (droits) : refusé aussi, faute de pouvoir le copier. */
static gboolean
backup_before_overwrite(const gchar *path, GError **error)
{
	gchar *data = NULL, *backup, *old = NULL;
	gsize len = 0, old_len = 0;
	GError *local = NULL;
	gboolean ok = TRUE;

	if(!g_file_get_contents(path, &data, &len, &local))
	{
		if(g_error_matches(local, G_FILE_ERROR, G_FILE_ERROR_NOENT))
		{
			g_error_free(local);
			return TRUE;
		}
		g_propagate_error(error, local);
		return FALSE;
	}
	if(len == 0 || is_readable_v2(data, len))
	{
		g_free(data);
		return TRUE;
	}
	backup = g_strconcat(path, BACKUP_SUFFIX, NULL);
	if(!g_file_test(backup, G_FILE_TEST_EXISTS))
		ok = g_file_set_contents(backup, data, len, error);
	else if(!g_file_get_contents(backup, &old, &old_len, NULL) || old_len != len || memcmp(old, data, len) != 0)
	{
		g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_EXIST,
		            _("%s was not overwritten: it is not in the current macros format and its backup %s already exists with a different content. Rename or delete %s, then save again."),
		            path, backup, backup);
		ok = FALSE;
	}
	g_free(old);
	g_free(backup);
	g_free(data);
	return ok;
}

void
gt_macro_set_to_key_file(const GtMacroSet *set, GKeyFile *kf, const gchar *prefix)
{
	const GtMacroLists *lists = gt_macro_set_get_lists(set);

	for(guint i = 0; i < gt_macro_set_count(set); i++)
	{
		const GtMacro *m = gt_macro_set_get(set, i);
		gchar *group = g_strdup_printf("%smacro:%u", prefix, i + 1);

		/* Un groupe de même nom est remplacé */
		g_key_file_remove_group(kf, group, NULL);
		g_key_file_set_string(kf, group, "label", m->label);
		g_key_file_set_string(kf, group, "shortcut", m->shortcut);
		g_key_file_set_string(kf, group, "action", m->action);
		g_key_file_set_string(kf, group, "tab", m->tab);
		if(m->args != NULL)
			g_key_file_set_string_list(kf, group, "args", (const gchar *const *)m->args, g_strv_length(m->args));
		if(m->arg_min != NULL)
			g_key_file_set_string_list(kf, group, "arg_min", (const gchar *const *)m->arg_min, g_strv_length(m->arg_min));
		if(m->arg_max != NULL)
			g_key_file_set_string_list(kf, group, "arg_max", (const gchar *const *)m->arg_max, g_strv_length(m->arg_max));
		if(m->arg_charset != NULL)
			g_key_file_set_string_list(kf, group, "arg_charset", (const gchar *const *)m->arg_charset, g_strv_length(m->arg_charset));
		g_key_file_set_boolean(kf, group, "polling", m->polling);
		g_key_file_set_uint64(kf, group, "polling_period_ms", m->polling_period_ms);
		g_free(group);
	}
	for(gint li = 0; li < gt_macro_lists_count(lists); li++)
	{
		gint n = gt_macro_lists_entry_count(lists, li);
		const gchar **displays = g_new0(const gchar *, n + 1);
		const gchar **values = g_new0(const gchar *, n + 1);
		gchar *group = g_strdup_printf("%slist:%d", prefix, li + 1);

		for(gint ei = 0; ei < n; ei++)
		{
			displays[ei] = gt_macro_lists_entry_display(lists, li, ei);
			values[ei] = gt_macro_lists_entry_value(lists, li, ei);
		}
		g_key_file_remove_group(kf, group, NULL);
		g_key_file_set_string(kf, group, "name", gt_macro_lists_name(lists, li));
		g_key_file_set_string_list(kf, group, "displays", displays, n);
		g_key_file_set_string_list(kf, group, "values", values, n);
		g_free(group);
		g_free(displays);
		g_free(values);
	}
}

gboolean
gt_macro_file_save(const GtMacroSet *set, const gchar *path, GError **error)
{
	GKeyFile *kf;
	gchar *data;
	gsize len = 0;
	gboolean ok;

	if(!backup_before_overwrite(path, error))
		return FALSE;
	kf = g_key_file_new();
	g_key_file_set_integer(kf, HEADER_GROUP, "version", FILE_VERSION);
	gt_macro_set_to_key_file(set, kf, "");
	data = g_key_file_to_data(kf, &len, NULL);
	g_key_file_free(kf);
	/* Écriture atomique (fichier temporaire puis renommage) ; le message d'erreur de GLib cite le chemin */
	ok = g_file_set_contents(path, data, len, error);
	g_free(data);
	return ok;
}
