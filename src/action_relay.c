/* Relais d'action (sous-projet 6b, spec §4.4) : voir action_relay.h. */
#include "action_relay.h"

struct _GtActionRelay
{
	GObject parent_instance;
	gchar *name;
	GVariantType *parameter_type;	/* NULL : sans paramètre */
	GVariantType *state_type;	/* NULL : sans état */
	GVariant *state;	/* dernier état connu (référence pleine) ; NULL : sans état */
	gboolean enabled;
	GActionGroup *target;	/* référence ; NULL : aucune */
	gulong added_id, removed_id, enabled_id, state_id;
};

enum
{
	PROP_0,
	PROP_NAME,
	PROP_PARAMETER_TYPE,
	PROP_ENABLED,
	PROP_STATE_TYPE,
	PROP_STATE
};

static void action_iface_init(GActionInterface *iface);

G_DEFINE_TYPE_WITH_CODE(GtActionRelay, gt_action_relay, G_TYPE_OBJECT, G_IMPLEMENT_INTERFACE(G_TYPE_ACTION, action_iface_init))

static void set_enabled(GtActionRelay *r, gboolean enabled)
{
	enabled = enabled != FALSE;
	if(r->enabled == enabled)
		return;
	r->enabled = enabled;
	g_object_notify(G_OBJECT(r), "enabled");
}

/* state : référence empruntée (flottante ou non) ; ignoré s'il n'est pas du type d'état du relais */
static void set_state(GtActionRelay *r, GVariant *state)
{
	if(state == NULL || r->state_type == NULL || !g_variant_is_of_type(state, r->state_type))
		return;
	if(r->state != NULL && g_variant_equal(r->state, state))
		return;
	g_clear_pointer(&r->state, g_variant_unref);
	r->state = g_variant_ref_sink(state);
	g_object_notify(G_OBJECT(r), "state");
}

static void sync_from_target(GtActionRelay *r)
{
	gboolean enabled = FALSE;
	GVariant *state = NULL;

	if(r->target != NULL && g_action_group_query_action(r->target, r->name, &enabled, NULL, NULL, NULL, &state))
	{
		if(state != NULL)
		{
			set_state(r, state);
			g_variant_unref(state);
		}
		set_enabled(r, enabled);
	}
	else
		set_enabled(r, FALSE);
}

static void on_added(GActionGroup *group, const gchar *name, gpointer user_data)
{
	sync_from_target(GT_ACTION_RELAY(user_data));
}

/* GSimpleActionGroup émet « action-removed » alors que l'action est encore interrogeable : pas de relecture, on désactive */
static void on_removed(GActionGroup *group, const gchar *name, gpointer user_data)
{
	set_enabled(GT_ACTION_RELAY(user_data), FALSE);
}

static void on_enabled_changed(GActionGroup *group, const gchar *name, gboolean enabled, gpointer user_data)
{
	set_enabled(GT_ACTION_RELAY(user_data), enabled);
}

static void on_state_changed(GActionGroup *group, const gchar *name, GVariant *value, gpointer user_data)
{
	set_state(GT_ACTION_RELAY(user_data), value);
}

static gulong connect_detailed(GtActionRelay *r, const gchar *signal, GCallback cb)
{
	gchar *detailed = g_strconcat(signal, "::", r->name, NULL);
	gulong id = g_signal_connect(r->target, detailed, cb, r);

	g_free(detailed);
	return id;
}

static void disconnect_target(GtActionRelay *r)
{
	if(r->target == NULL)
		return;
	g_clear_signal_handler(&r->added_id, r->target);
	g_clear_signal_handler(&r->removed_id, r->target);
	g_clear_signal_handler(&r->enabled_id, r->target);
	g_clear_signal_handler(&r->state_id, r->target);
	g_clear_object(&r->target);
}

void gt_action_relay_set_target(GtActionRelay *r, GActionGroup *target)
{
	g_return_if_fail(GT_IS_ACTION_RELAY(r));
	g_return_if_fail(target == NULL || G_IS_ACTION_GROUP(target));

	if(target == r->target)
		return;
	disconnect_target(r);
	if(target != NULL)
	{
		r->target = g_object_ref(target);
		r->added_id = connect_detailed(r, "action-added", G_CALLBACK(on_added));
		r->removed_id = connect_detailed(r, "action-removed", G_CALLBACK(on_removed));
		r->enabled_id = connect_detailed(r, "action-enabled-changed", G_CALLBACK(on_enabled_changed));
		r->state_id = connect_detailed(r, "action-state-changed", G_CALLBACK(on_state_changed));
	}
	sync_from_target(r);
}

GActionGroup *gt_action_relay_get_target(GtActionRelay *r)
{
	g_return_val_if_fail(GT_IS_ACTION_RELAY(r), NULL);
	return r->target;
}

GtActionRelay *gt_action_relay_new(GActionGroup *model, const gchar *name)
{
	const GVariantType *ptype = NULL, *stype = NULL;
	GtActionRelay *r;

	g_return_val_if_fail(G_IS_ACTION_GROUP(model) && name != NULL, NULL);
	if(!g_action_group_query_action(model, name, NULL, &ptype, &stype, NULL, NULL))
		return NULL;
	r = g_object_new(GT_TYPE_ACTION_RELAY, NULL);
	r->name = g_strdup(name);
	r->parameter_type = ptype != NULL ? g_variant_type_copy(ptype) : NULL;
	r->state_type = stype != NULL ? g_variant_type_copy(stype) : NULL;
	gt_action_relay_set_target(r, model);
	return r;
}

/* ---- GAction ---- */

static const gchar *relay_get_name(GAction *a)
{
	return GT_ACTION_RELAY(a)->name;
}

static const GVariantType *relay_get_parameter_type(GAction *a)
{
	return GT_ACTION_RELAY(a)->parameter_type;
}

static const GVariantType *relay_get_state_type(GAction *a)
{
	return GT_ACTION_RELAY(a)->state_type;
}

static GVariant *relay_get_state_hint(GAction *a)
{
	GtActionRelay *r = GT_ACTION_RELAY(a);
	GVariant *hint = NULL;

	/* Action absente de la cible : aucune indication (hint reste NULL) */
	if(r->target == NULL || !g_action_group_query_action(r->target, r->name, NULL, NULL, NULL, &hint, NULL))
		return NULL;
	return hint;
}

static gboolean relay_get_enabled(GAction *a)
{
	return GT_ACTION_RELAY(a)->enabled;
}

static GVariant *relay_get_state(GAction *a)
{
	GtActionRelay *r = GT_ACTION_RELAY(a);

	return r->state != NULL ? g_variant_ref(r->state) : NULL;
}

static void relay_change_state(GAction *a, GVariant *value)
{
	GtActionRelay *r = GT_ACTION_RELAY(a);

	if(r->enabled && r->target != NULL)
		g_action_group_change_action_state(r->target, r->name, value);
	else if(value != NULL && g_variant_is_floating(value))
		g_variant_unref(g_variant_ref_sink(value));	/* valeur flottante non consommée : libérée */
}

static void relay_activate(GAction *a, GVariant *param)
{
	GtActionRelay *r = GT_ACTION_RELAY(a);

	if(r->enabled && r->target != NULL)
		g_action_group_activate_action(r->target, r->name, param);
	else if(param != NULL && g_variant_is_floating(param))
		g_variant_unref(g_variant_ref_sink(param));
}

static void action_iface_init(GActionInterface *iface)
{
	iface->get_name = relay_get_name;
	iface->get_parameter_type = relay_get_parameter_type;
	iface->get_state_type = relay_get_state_type;
	iface->get_state_hint = relay_get_state_hint;
	iface->get_enabled = relay_get_enabled;
	iface->get_state = relay_get_state;
	iface->change_state = relay_change_state;
	iface->activate = relay_activate;
}

/* ---- GObject ---- */

static void gt_action_relay_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GtActionRelay *r = GT_ACTION_RELAY(object);

	switch(prop_id)
	{
	case PROP_NAME:
		g_value_set_string(value, r->name);
		break;
	case PROP_PARAMETER_TYPE:
		g_value_set_boxed(value, r->parameter_type);
		break;
	case PROP_ENABLED:
		g_value_set_boolean(value, r->enabled);
		break;
	case PROP_STATE_TYPE:
		g_value_set_boxed(value, r->state_type);
		break;
	case PROP_STATE:
		g_value_set_variant(value, r->state);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}

static void gt_action_relay_dispose(GObject *object)
{
	disconnect_target(GT_ACTION_RELAY(object));
	G_OBJECT_CLASS(gt_action_relay_parent_class)->dispose(object);
}

static void gt_action_relay_finalize(GObject *object)
{
	GtActionRelay *r = GT_ACTION_RELAY(object);

	g_free(r->name);
	g_clear_pointer(&r->parameter_type, g_variant_type_free);
	g_clear_pointer(&r->state_type, g_variant_type_free);
	g_clear_pointer(&r->state, g_variant_unref);
	G_OBJECT_CLASS(gt_action_relay_parent_class)->finalize(object);
}

static void gt_action_relay_class_init(GtActionRelayClass *klass)
{
	GObjectClass *oc = G_OBJECT_CLASS(klass);

	oc->get_property = gt_action_relay_get_property;
	oc->dispose = gt_action_relay_dispose;
	oc->finalize = gt_action_relay_finalize;
	g_object_class_override_property(oc, PROP_NAME, "name");
	g_object_class_override_property(oc, PROP_PARAMETER_TYPE, "parameter-type");
	g_object_class_override_property(oc, PROP_ENABLED, "enabled");
	g_object_class_override_property(oc, PROP_STATE_TYPE, "state-type");
	g_object_class_override_property(oc, PROP_STATE, "state");
}

static void gt_action_relay_init(GtActionRelay *r)
{
}
