/* Sessions ouvertes et session active (sous-projet 6b, spec §4.3) : voir session_manager.h. */
#include <config.h>

#include "session_manager.h"

struct _GtSessionManager
{
	GObject parent_instance;
	GListStore *sessions;	/* GtSession, ordre des onglets */
	GtSession *active;	/* empruntée : le magasin tient la référence */
};

enum { PROP_0, PROP_ACTIVE, N_PROPS };
static GParamSpec *props[N_PROPS];

G_DEFINE_TYPE(GtSessionManager, gt_session_manager, G_TYPE_OBJECT)

GtSessionManager *gt_session_manager_new(void)
{
	return g_object_new(GT_TYPE_SESSION_MANAGER, NULL);
}

GListModel *gt_session_manager_get_model(GtSessionManager *m)
{
	return G_LIST_MODEL(m->sessions);
}

guint gt_session_manager_get_n(GtSessionManager *m)
{
	return g_list_model_get_n_items(G_LIST_MODEL(m->sessions));
}

GtSession *gt_session_manager_get_nth(GtSessionManager *m, guint i)
{
	GtSession *s;

	if(i >= gt_session_manager_get_n(m))
		return NULL;
	s = g_list_model_get_item(G_LIST_MODEL(m->sessions), i);
	g_object_unref(s);	/* empruntée : le magasin garde sa référence */
	return s;
}

gint gt_session_manager_index_of(GtSessionManager *m, GtSession *s)
{
	guint pos;

	if(s == NULL || !g_list_store_find(m->sessions, s, &pos))
		return -1;
	return (gint)pos;
}

/* Change l'active sans contrôle (remove : NULL quand la dernière session part) ; notifie si elle change */
static void set_active(GtSessionManager *m, GtSession *s)
{
	if(m->active == s)
		return;
	m->active = s;
	g_object_notify_by_pspec(G_OBJECT(m), props[PROP_ACTIVE]);
}

void gt_session_manager_add(GtSessionManager *m, GtSession *s, gint position)
{
	guint n = gt_session_manager_get_n(m);

	g_return_if_fail(GT_IS_SESSION(s));
	g_return_if_fail(gt_session_manager_index_of(m, s) < 0);	/* déjà présente : jamais deux fois dans les onglets */
	if(position < 0 || (guint)position > n)
		position = (gint)n;
	g_list_store_insert(m->sessions, (guint)position, s);
	if(m->active == NULL)
		gt_session_manager_set_active(m, s);
}

void gt_session_manager_remove(GtSessionManager *m, GtSession *s)
{
	gint i = gt_session_manager_index_of(m, s);

	if(i < 0)
		return;
	if(m->active == s)
	{
		GtSession *neighbour = gt_session_manager_get_nth(m, (guint)i + 1);

		if(neighbour == NULL && i > 0)
			neighbour = gt_session_manager_get_nth(m, (guint)i - 1);
		set_active(m, neighbour);	/* NULL : plus aucune session */
	}
	g_list_store_remove(m->sessions, (guint)i);
}

void gt_session_manager_move(GtSessionManager *m, GtSession *s, guint position)
{
	gint i = gt_session_manager_index_of(m, s);
	guint n = gt_session_manager_get_n(m);

	if(i < 0)
		return;
	position = MIN(position, n - 1);
	if((guint)i == position)
		return;
	g_object_ref(s);	/* le retrait rendrait la dernière référence */
	g_list_store_remove(m->sessions, (guint)i);
	g_list_store_insert(m->sessions, position, s);
	g_object_unref(s);
}

GtSession *gt_session_manager_get_active(GtSessionManager *m)
{
	return m->active;
}

/* NULL seulement sans session (en-tête) : refusé sinon, propriété « active » comprise */
void gt_session_manager_set_active(GtSessionManager *m, GtSession *s)
{
	if(s == NULL ? gt_session_manager_get_n(m) > 0 : gt_session_manager_index_of(m, s) < 0)
		return;
	set_active(m, s);
}

static void gt_session_manager_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GtSessionManager *m = GT_SESSION_MANAGER(object);

	switch(prop_id)
	{
	case PROP_ACTIVE:
		g_value_set_object(value, m->active);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}

static void gt_session_manager_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GtSessionManager *m = GT_SESSION_MANAGER(object);

	switch(prop_id)
	{
	case PROP_ACTIVE:
		gt_session_manager_set_active(m, g_value_get_object(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}

static void gt_session_manager_dispose(GObject *object)
{
	GtSessionManager *m = GT_SESSION_MANAGER(object);

	m->active = NULL;
	g_clear_object(&m->sessions);
	G_OBJECT_CLASS(gt_session_manager_parent_class)->dispose(object);
}

static void gt_session_manager_class_init(GtSessionManagerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->get_property = gt_session_manager_get_property;
	object_class->set_property = gt_session_manager_set_property;
	object_class->dispose = gt_session_manager_dispose;
	props[PROP_ACTIVE] = g_param_spec_object("active", NULL, NULL, GT_TYPE_SESSION,
	                                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
	g_object_class_install_properties(object_class, N_PROPS, props);
}

static void gt_session_manager_init(GtSessionManager *m)
{
	m->sessions = g_list_store_new(GT_TYPE_SESSION);
}
