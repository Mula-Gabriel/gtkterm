/* Projet de la fenêtre (6c, spec §4.3) : voir project.h. */
#include <config.h>

#include <string.h>

#include <glib/gi18n.h>
#include "project.h"

#define CHECK_DELAY_MS 300	/* regroupement à l'inactivité : au plus un recalcul par 300 ms */

struct _GtProject
{
	GObject parent_instance;
	GtSessionManager *sessions;	/* référence */
	gboolean open;
	gchar *path;
	GtFileStamp stamp;
	gboolean stamp_known;	/* FALSE : identité inconnue, pas de contrôle de conflit */
	gchar *saved_snapshot;
	gboolean modified;
	GtProjectPageFunc page_func;
	gpointer page_data;
	guint check_id;		/* source du recalcul différé, 0 : aucune */
	gint64 last_check_us;
	gulong items_id;
	/* session (référence tenue jusqu'à son départ du gestionnaire, traité aussitôt par items-changed) → identifiant du gestionnaire « edited » */
	GHashTable *watched;
};

enum { PROP_0, PROP_OPEN, PROP_PATH, PROP_MODIFIED, N_PROPS };
static GParamSpec *props[N_PROPS];

G_DEFINE_TYPE(GtProject, gt_project, G_TYPE_OBJECT)

static void queue_check(GtProject *p);

static void
set_modified(GtProject *p, gboolean modified)
{
	if(p->modified == modified)
		return;
	p->modified = modified;
	g_object_notify_by_pspec(G_OBJECT(p), props[PROP_MODIFIED]);
}

static void
set_path(GtProject *p, const gchar *path)
{
	if(g_strcmp0(p->path, path) == 0)
		return;
	g_free(p->path);
	p->path = g_strdup(path);
	g_object_notify_by_pspec(G_OBJECT(p), props[PROP_PATH]);
}

static void
cancel_check(GtProject *p)
{
	if(p->check_id != 0)
	{
		g_source_remove(p->check_id);
		p->check_id = 0;
	}
}

static gboolean
check_cb(gpointer data)
{
	GtProject *p = data;

	p->check_id = 0;
	gt_project_check_now(p);
	return G_SOURCE_REMOVE;
}

static void
queue_check(GtProject *p)
{
	gint64 elapsed_ms;
	guint delay;

	if(!p->open || p->check_id != 0)
		return;
	elapsed_ms = (g_get_monotonic_time() - p->last_check_us) / 1000;
	delay = (guint)MAX(0, CHECK_DELAY_MS - elapsed_ms);
	p->check_id = g_timeout_add_full(G_PRIORITY_DEFAULT_IDLE, delay, check_cb, p, NULL);
}

static void
on_edited(GtSession *s, gpointer data)
{
	queue_check(data);
}

static void
watch(GtProject *p, GtSession *s)
{
	gulong id;

	if(g_hash_table_contains(p->watched, s))
		return;
	id = g_signal_connect(s, "edited", G_CALLBACK(on_edited), p);
	g_hash_table_insert(p->watched, g_object_ref(s), GSIZE_TO_POINTER(id));
}

/* Relie les sessions du gestionnaire, oublie celles qui en sont parties */
static void
sync_watched(GtProject *p)
{
	GHashTableIter it;
	gpointer key, value;
	guint n = gt_session_manager_get_n(p->sessions);

	g_hash_table_iter_init(&it, p->watched);
	while(g_hash_table_iter_next(&it, &key, &value))
	{
		if(gt_session_manager_index_of(p->sessions, key) < 0)
		{
			g_signal_handler_disconnect(key, GPOINTER_TO_SIZE(value));
			g_hash_table_iter_remove(&it);	/* rend la référence */
		}
	}
	for(guint i = 0; i < n; i++)
	{
		GtSession *s = gt_session_manager_get_nth(p->sessions, i);

		watch(p, s);
		if(p->open && !gt_session_is_shut_down(s))
			gt_session_set_in_project(s, TRUE);
	}
}

static void
on_items_changed(GListModel *model, guint position, guint removed, guint added, gpointer data)
{
	GtProject *p = data;

	sync_watched(p);
	queue_check(p);
}

GtProject *
gt_project_new(GtSessionManager *sessions)
{
	GtProject *p = g_object_new(GT_TYPE_PROJECT, NULL);

	p->sessions = g_object_ref(sessions);
	p->items_id = g_signal_connect(gt_session_manager_get_model(sessions), "items-changed", G_CALLBACK(on_items_changed), p);
	sync_watched(p);
	return p;
}

void
gt_project_set_page_func(GtProject *p, GtProjectPageFunc func, gpointer user_data)
{
	p->page_func = func;
	p->page_data = user_data;
}

gboolean
gt_project_is_open(GtProject *p)
{
	return p->open;
}

const gchar *
gt_project_get_path(GtProject *p)
{
	return p->path;
}

gchar *
gt_project_get_name(GtProject *p)
{
	gchar *base;

	if(!p->open)
		return g_strdup("");
	if(p->path == NULL)
		return g_strdup(_("Untitled project"));
	base = g_path_get_basename(p->path);
	if(g_str_has_suffix(base, ".gtkterm") && strlen(base) > strlen(".gtkterm"))
		base[strlen(base) - strlen(".gtkterm")] = '\0';
	return base;
}

gboolean
gt_project_is_modified(GtProject *p)
{
	return p->modified;
}

GtProjectData *
gt_project_capture(GtProject *p)
{
	GtProjectData *d = gt_project_data_new();
	GtSession *active = gt_session_manager_get_active(p->sessions);
	guint n = gt_session_manager_get_n(p->sessions);

	for(guint i = 0; i < n; i++)
	{
		GtSession *s = gt_session_manager_get_nth(p->sessions, i);
		GtMacros *m = gt_session_get_macros(s);
		GtProjectTab *t;
		gboolean reconnect = FALSE;

		if(gt_session_is_shut_down(s))
			continue;
		if(s == active)
			d->active = d->tabs->len;
		t = gt_project_tab_new();
		gt_profile_copy(&t->profile, gt_session_get_profile(s));
		gt_term_options_store_profile(gt_session_get_options(s), &t->profile);
		g_object_get(gt_session_get_link(s), "auto-reconnect", &reconnect, NULL);
		t->profile.auto_reconnect = reconnect;
		gt_profile_set_string(&t->profile.macros_file, NULL);
		g_free(t->profile_name);
		t->profile_name = g_strdup(gt_session_get_profile_name(s));
		gt_macro_set_free(t->macros);
		if(gt_macros_get_template(m) != NULL)
		{
			/* Onglet lié (§2.4) : la base, le chemin du modèle et les valeurs propres (diff base → jeu effectif) */
			t->macros = gt_macro_set_copy(gt_macros_get_template_set(m));
			t->macros_template = g_strdup(gt_macros_get_template(m));
			gt_macro_overrides_free(t->overrides);
			t->overrides = gt_macros_get_overrides(m);
		}
		else
			t->macros = gt_macro_set_copy(gt_macros_get_set(m));
		g_free(t->script);
		t->script = g_strdup(gt_session_get_script_text(s));
		if(p->page_func != NULL)
			p->page_func(s, t, p->page_data);
		g_ptr_array_add(d->tabs, t);
	}
	return d;
}

void
gt_project_check_now(GtProject *p)
{
	GtProjectData *d;
	gchar *snap;

	cancel_check(p);
	if(!p->open)
		return;
	d = gt_project_capture(p);
	snap = gt_project_data_snapshot(d);
	set_modified(p, g_strcmp0(snap, p->saved_snapshot) != 0);
	p->last_check_us = g_get_monotonic_time();
	g_free(snap);
	gt_project_data_free(d);
}

void
gt_project_begin(GtProject *p, const gchar *path, const GtFileStamp *stamp, const GtProjectData *saved)
{
	guint n = gt_session_manager_get_n(p->sessions);

	cancel_check(p);
	if(stamp != NULL)
		p->stamp = *stamp;
	else
		memset(&p->stamp, 0, sizeof p->stamp);
	p->stamp_known = stamp != NULL;
	for(guint i = 0; i < n; i++)
	{
		GtSession *s = gt_session_manager_get_nth(p->sessions, i);

		if(!gt_session_is_shut_down(s))
			gt_session_set_in_project(s, TRUE);
	}
	g_free(p->saved_snapshot);
	if(saved != NULL)
		p->saved_snapshot = gt_project_data_snapshot(saved);
	else
	{
		GtProjectData *d = gt_project_capture(p);

		p->saved_snapshot = gt_project_data_snapshot(d);
		gt_project_data_free(d);
	}
	if(!p->open)
	{
		p->open = TRUE;
		g_object_notify_by_pspec(G_OBJECT(p), props[PROP_OPEN]);
	}
	set_path(p, path);
	gt_project_check_now(p);
}

void
gt_project_end(GtProject *p)
{
	cancel_check(p);
	g_clear_pointer(&p->saved_snapshot, g_free);
	memset(&p->stamp, 0, sizeof p->stamp);
	p->stamp_known = FALSE;
	set_path(p, NULL);
	set_modified(p, FALSE);
	if(p->open)
	{
		p->open = FALSE;
		g_object_notify_by_pspec(G_OBJECT(p), props[PROP_OPEN]);
	}
}

GtProjectSaveResult
gt_project_save(GtProject *p, const gchar *path, gboolean force, GError **error)
{
	const gchar *target = path != NULL ? path : p->path;
	GtProjectData *d;
	GtFileStamp stamp;

	if(!p->open)
	{
		g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, _("No project is open"));
		return GT_PROJECT_FAILED;
	}
	if(target == NULL)
	{
		g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, _("The project has no file"));
		return GT_PROJECT_FAILED;
	}
	if(!force && p->stamp_known && g_strcmp0(target, p->path) == 0 && gt_file_stamp_changed(target, &p->stamp))
		return GT_PROJECT_CONFLICT;
	d = gt_project_capture(p);
	if(!gt_project_data_save(d, target, &stamp, error))
	{
		gt_project_data_free(d);
		return GT_PROJECT_FAILED;
	}
	p->stamp = stamp;
	p->stamp_known = TRUE;
	set_path(p, target);
	g_free(p->saved_snapshot);
	p->saved_snapshot = gt_project_data_snapshot(d);
	gt_project_data_free(d);
	cancel_check(p);
	p->last_check_us = g_get_monotonic_time();
	set_modified(p, FALSE);
	return GT_PROJECT_SAVED;
}

static void
gt_project_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GtProject *p = GT_PROJECT(object);

	switch(prop_id)
	{
	case PROP_OPEN:
		g_value_set_boolean(value, p->open);
		break;
	case PROP_PATH:
		g_value_set_string(value, p->path);
		break;
	case PROP_MODIFIED:
		g_value_set_boolean(value, p->modified);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}

static void
gt_project_dispose(GObject *object)
{
	GtProject *p = GT_PROJECT(object);

	cancel_check(p);
	if(p->sessions != NULL)
	{
		GHashTableIter it;
		gpointer key, value;

		g_hash_table_iter_init(&it, p->watched);
		while(g_hash_table_iter_next(&it, &key, &value))
			g_signal_handler_disconnect(key, GPOINTER_TO_SIZE(value));
		g_hash_table_remove_all(p->watched);
		g_signal_handler_disconnect(gt_session_manager_get_model(p->sessions), p->items_id);
		g_clear_object(&p->sessions);
	}
	G_OBJECT_CLASS(gt_project_parent_class)->dispose(object);
}

static void
gt_project_finalize(GObject *object)
{
	GtProject *p = GT_PROJECT(object);

	g_free(p->path);
	g_free(p->saved_snapshot);
	g_hash_table_unref(p->watched);
	G_OBJECT_CLASS(gt_project_parent_class)->finalize(object);
}

static void
gt_project_class_init(GtProjectClass *klass)
{
	GObjectClass *oc = G_OBJECT_CLASS(klass);

	oc->get_property = gt_project_get_property;
	oc->dispose = gt_project_dispose;
	oc->finalize = gt_project_finalize;
	props[PROP_OPEN] = g_param_spec_boolean("open", NULL, NULL, FALSE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	props[PROP_PATH] = g_param_spec_string("path", NULL, NULL, NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	props[PROP_MODIFIED] = g_param_spec_boolean("modified", NULL, NULL, FALSE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_properties(oc, N_PROPS, props);
}

static void
gt_project_init(GtProject *p)
{
	p->watched = g_hash_table_new_full(g_direct_hash, g_direct_equal, g_object_unref, NULL);
}
