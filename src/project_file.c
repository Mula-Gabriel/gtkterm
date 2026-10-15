/* project_file.c — fichier projet .gtkterm (voir project_file.h et les §3.2-1, §4.2 et §6 de la spec du sous-projet 6c). */

#include <config.h>
#include <glib/gi18n.h>
#include <string.h>

#include "macro_file.h"
#include "profile_keyfile.h"
#include "project_file.h"

#define HEADER_GROUP "gtkterm-project"

G_DEFINE_QUARK(gt-project-error-quark, gt_project_error)

GtProjectTab *gt_project_tab_new(void)
{
	GtProjectTab *t = g_new0(GtProjectTab, 1);

	gt_profile_init_defaults(&t->profile);
	t->profile_name = g_strdup("default");
	t->page.width = 0;
	t->page.height = 0;
	t->page.maximized = FALSE;
	t->page.h_paned = -1;
	t->page.v_paned = -1;
	t->page.macro_panel_visible = TRUE;
	t->page.script_panel_visible = FALSE;
	t->page.hex_bar_visible = FALSE;
	t->script_layout = g_strdup("topbottom");
	t->macros = gt_macro_set_new();
	t->script = g_strdup("");
	t->macros_template = NULL;
	t->overrides = gt_macro_overrides_new();
	return t;
}

void gt_project_tab_free(GtProjectTab *t)
{
	if(t == NULL)
		return;
	gt_profile_clear(&t->profile);
	g_free(t->profile_name);
	g_free(t->script_layout);
	gt_macro_set_free(t->macros);
	g_free(t->script);
	g_free(t->macros_template);
	gt_macro_overrides_free(t->overrides);
	g_free(t);
}

GtProjectData *gt_project_data_new(void)
{
	GtProjectData *d = g_new0(GtProjectData, 1);

	d->tabs = g_ptr_array_new_with_free_func((GDestroyNotify)gt_project_tab_free);
	return d;
}

void gt_project_data_free(GtProjectData *d)
{
	if(d == NULL)
		return;
	g_ptr_array_unref(d->tabs);
	g_free(d);
}

/* Sérialisation commune : with_page FALSE = instantané de comparaison (ni état de page, ni script_layout, ni active, ni base d'un onglet
 * lié : un modèle réimporté à l'ouverture ne rend pas le projet « modifié ») */
static GKeyFile *write_key_file(const GtProjectData *d, gboolean with_page)
{
	GKeyFile *kf = g_key_file_new();

	g_key_file_set_integer(kf, HEADER_GROUP, "version", GT_PROJECT_FILE_VERSION);
	if(with_page)
		g_key_file_set_integer(kf, HEADER_GROUP, "active", (gint)d->active);
	for(guint i = 0; i < d->tabs->len; i++)
	{
		const GtProjectTab *t = g_ptr_array_index(d->tabs, i);
		gchar *group = g_strdup_printf("tab:%u", i), *prefix = g_strdup_printf("tab:%u:", i);
		GtProfile copy = { 0 };

		g_key_file_set_string(kf, group, "profile_name", t->profile_name != NULL ? t->profile_name : "default");
		gt_profile_copy(&copy, &t->profile);
		gt_profile_set_string(&copy.macros_file, NULL);	/* le projet ne référence aucun fichier */
		gt_profile_to_key_file(&copy, kf, group);
		gt_profile_clear(&copy);
		if(with_page)
		{
			g_key_file_set_integer(kf, group, "h_paned", t->page.h_paned);
			g_key_file_set_integer(kf, group, "v_paned", t->page.v_paned);
			g_key_file_set_boolean(kf, group, "macro_panel_visible", t->page.macro_panel_visible);
			g_key_file_set_boolean(kf, group, "script_panel_visible", t->page.script_panel_visible);
			g_key_file_set_boolean(kf, group, "hex_bar_visible", t->page.hex_bar_visible);
			g_key_file_set_string(kf, group, "script_layout", t->script_layout != NULL ? t->script_layout : "topbottom");
		}
		g_key_file_set_string(kf, group, "script", t->script != NULL ? t->script : "");
		if(t->macros_template != NULL)
		{
			/* Onglet lié : base (sauf dans l'instantané), puis valeurs propres */
			g_key_file_set_string(kf, group, "macros_template", t->macros_template);
			if(with_page)
				gt_macro_set_to_key_file(t->macros, kf, prefix);
			gt_macro_overrides_to_key_file(t->overrides, kf, prefix);
		}
		else
			gt_macro_set_to_key_file(t->macros, kf, prefix);
		g_free(prefix);
		g_free(group);
	}
	return kf;
}

static gchar *serialize(const GtProjectData *d, gboolean with_page)
{
	GKeyFile *kf = write_key_file(d, with_page);
	gchar *text = g_key_file_to_data(kf, NULL, NULL);

	g_key_file_unref(kf);
	return text;
}

gchar *gt_project_data_to_text(const GtProjectData *d)
{
	return serialize(d, TRUE);
}

gchar *gt_project_data_snapshot(const GtProjectData *d)
{
	return serialize(d, FALSE);
}

gboolean gt_project_data_save(const GtProjectData *d, const gchar *path, GtFileStamp *stamp, GError **error)
{
	gchar *text = gt_project_data_to_text(d);
	gboolean ok = g_file_set_contents_full(path, text, -1, G_FILE_SET_CONTENTS_CONSISTENT, 0644, error);

	g_free(text);
	if(!ok)
		return FALSE;
	if(stamp != NULL)
		gt_file_stamp_take(path, stamp);
	return TRUE;
}

/* Valeur de chaîne : échappement invalide → valeur brute ; octets non UTF-8 (fichier modifié à la main) → remplacés (U+FFFD) avec un
 * avertissement, sinon l'éditeur de script refuserait le texte et le Save suivant le perdrait */
static gchar *read_string(GKeyFile *kf, const gchar *group, const gchar *key, const gchar *path)
{
	GError *err = NULL;
	gchar *s = g_key_file_get_string(kf, group, key, &err);

	if(s == NULL && g_error_matches(err, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_UNKNOWN_ENCODING))
	{
		gchar *raw = g_key_file_get_value(kf, group, key, NULL);
		gchar *valid = g_utf8_make_valid(raw != NULL ? raw : "", -1);
		GKeyFile *tmp = g_key_file_new();

		/* Échappements interprétés sur la valeur rendue valide : ils sont en ASCII, le remplacement ne les touche pas */
		g_key_file_set_value(tmp, group, key, valid);
		s = g_key_file_get_string(tmp, group, key, NULL);
		if(s == NULL)
			s = g_strdup(valid);
		g_warning(_("%s: [%s] %s is not valid UTF-8: invalid bytes replaced"), path, group, key);
		g_key_file_unref(tmp);
		g_free(valid);
		g_free(raw);
	}
	else if(s == NULL && err != NULL && !g_error_matches(err, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND))
		s = g_key_file_get_value(kf, group, key, NULL);
	g_clear_error(&err);
	return s;
}

/* FALSE : erreur posée ; t n'est pas ajouté à d dans ce cas (libéré ici) */
static gboolean read_tab(GKeyFile *kf, guint n, const gchar *path, GtProjectData *d, GError **error)
{
	GtProjectTab *t = gt_project_tab_new();
	gchar *group = g_strdup_printf("tab:%u", n), *prefix = g_strdup_printf("tab:%u:", n), *s;
	gchar *transport = read_string(kf, group, "transport", path);
	TransportType type;

	if(transport == NULL || !gt_transport_from_string(transport, &type))
	{
		g_set_error(error, GT_PROJECT_ERROR, GT_PROJECT_ERROR_BAD_TAB, _("%s: tab %u has no valid port settings"), path, n + 1);
		g_free(transport);
		g_free(group);
		g_free(prefix);
		gt_project_tab_free(t);
		return FALSE;
	}
	g_free(transport);
	gt_profile_from_key_file(kf, group, &t->profile);
	t->profile.lock_port = TRUE;
	gt_profile_set_string(&t->profile.macros_file, NULL);
	s = read_string(kf, group, "profile_name", path);
	if(s != NULL && *s != '\0')
		gt_profile_set_string(&t->profile_name, s);
	g_free(s);
	gt_key_file_read_int(kf, group, "h_paned", &t->page.h_paned);
	gt_key_file_read_int(kf, group, "v_paned", &t->page.v_paned);
	gt_key_file_read_bool(kf, group, "macro_panel_visible", &t->page.macro_panel_visible);
	gt_key_file_read_bool(kf, group, "script_panel_visible", &t->page.script_panel_visible);
	gt_key_file_read_bool(kf, group, "hex_bar_visible", &t->page.hex_bar_visible);
	s = read_string(kf, group, "script_layout", path);
	if(s != NULL && (strcmp(s, "topbottom") == 0 || strcmp(s, "leftright") == 0))
		gt_profile_set_string(&t->script_layout, s);
	g_free(s);
	s = read_string(kf, group, "script", path);
	if(s != NULL)
	{
		g_free(t->script);
		t->script = s;
	}
	gt_macro_set_free(t->macros);
	t->macros = gt_macro_set_from_key_file(kf, prefix, path);
	/* Modèle lié : chemin absolu seulement ; sinon onglet non lié et groupes override ignorés */
	s = read_string(kf, group, "macros_template", path);
	if(s != NULL && g_path_is_absolute(s))
	{
		t->macros_template = s;
		gt_macro_overrides_free(t->overrides);
		t->overrides = gt_macro_overrides_from_key_file(kf, prefix, path);
	}
	else
	{
		if(s != NULL)
			g_warning(_("%s: [%s] macros_template must be an absolute path: ignored"), path, group);
		g_free(s);
	}
	g_ptr_array_add(d->tabs, t);
	g_free(group);
	g_free(prefix);
	return TRUE;
}

gboolean gt_project_data_load(const gchar *path, GtProjectData **out, GtFileStamp *stamp, GError **error)
{
	gchar *contents = NULL, *version;
	GKeyFile *kf;
	GtProjectData *d;
	GError *err = NULL;
	gint active = 0;
	GtFileStamp taken;

	/* Identité relevée avant la lecture : elle ne doit jamais être plus récente que le contenu chargé */
	gt_file_stamp_take(path, &taken);
	if(!g_file_get_contents(path, &contents, NULL, &err))
	{
		if(strstr(err->message, path) == NULL)
			g_prefix_error(&err, "%s: ", path);
		g_propagate_error(error, err);
		return FALSE;
	}
	kf = g_key_file_new();
	if(!g_key_file_load_from_data(kf, contents, -1, G_KEY_FILE_NONE, NULL) || !g_key_file_has_group(kf, HEADER_GROUP))
	{
		g_set_error(error, GT_PROJECT_ERROR, GT_PROJECT_ERROR_NOT_PROJECT, _("%s is not a GTKTerm project"), path);
		g_key_file_unref(kf);
		g_free(contents);
		return FALSE;
	}
	g_free(contents);
	version = g_key_file_get_value(kf, HEADER_GROUP, "version", NULL);
	if(version == NULL || strcmp(g_strstrip(version), "1") != 0)
	{
		g_set_error(error, GT_PROJECT_ERROR, GT_PROJECT_ERROR_VERSION, _("%s: unsupported project version (written by a newer GTKTerm?)"), path);
		g_free(version);
		g_key_file_unref(kf);
		return FALSE;
	}
	g_free(version);
	d = gt_project_data_new();
	for(guint n = 0;; n++)
	{
		gchar *group = g_strdup_printf("tab:%u", n);
		gboolean present = g_key_file_has_group(kf, group);

		g_free(group);
		if(!present)
			break;
		if(!read_tab(kf, n, path, d, error))
		{
			gt_project_data_free(d);
			g_key_file_unref(kf);
			return FALSE;
		}
	}
	if(d->tabs->len == 0)
	{
		g_set_error(error, GT_PROJECT_ERROR, GT_PROJECT_ERROR_NO_TAB, _("%s: the project has no tab"), path);
		gt_project_data_free(d);
		g_key_file_unref(kf);
		return FALSE;
	}
	gt_key_file_read_int(kf, HEADER_GROUP, "active", &active);
	d->active = (active >= 0 && (guint)active < d->tabs->len) ? (guint)active : 0;
	g_key_file_unref(kf);
	if(stamp != NULL)
		*stamp = taken;
	*out = d;
	return TRUE;
}
