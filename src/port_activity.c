/* Activité d'un port : voir port_activity.h. */
#include <config.h>

#include "port_activity.h"

struct _GtPortActivity
{
	GObject parent_instance;
	GtPortActivityLink link;
	GtPortActivityState state;
	gint64 opened_us;	/* entrée en OPEN (ou reset) */
	gint64 lost_us;	/* entrée dans {LOST, RECONNECTING} depuis un autre état */
	gint64 last_rx_us;	/* -1 : rien reçu depuis l'entrée en OPEN */
	guint idle_s;
	guint no_data_s;
	gboolean viewed;
	gboolean unread;
	gboolean use_timer;
	guint timer_id;
	gint64 timer_due_us;
};

enum { PROP_0, PROP_STATE, PROP_UNREAD, PROP_USE_TIMER, N_PROPS };
static GParamSpec *props[N_PROPS];

G_DEFINE_TYPE(GtPortActivity, gt_port_activity, G_TYPE_OBJECT)

static const GEnumValue state_values[] =
{
	{ GT_PORT_ACTIVITY_CLOSED, "GT_PORT_ACTIVITY_CLOSED", "closed" },
	{ GT_PORT_ACTIVITY_CONNECTING, "GT_PORT_ACTIVITY_CONNECTING", "connecting" },
	{ GT_PORT_ACTIVITY_WAITING, "GT_PORT_ACTIVITY_WAITING", "waiting" },
	{ GT_PORT_ACTIVITY_ACTIVE, "GT_PORT_ACTIVITY_ACTIVE", "active" },
	{ GT_PORT_ACTIVITY_RECEIVING, "GT_PORT_ACTIVITY_RECEIVING", "receiving" },
	{ GT_PORT_ACTIVITY_IDLE, "GT_PORT_ACTIVITY_IDLE", "idle" },
	{ GT_PORT_ACTIVITY_LOST, "GT_PORT_ACTIVITY_LOST", "lost" },
	{ GT_PORT_ACTIVITY_RECONNECTING, "GT_PORT_ACTIVITY_RECONNECTING", "reconnecting" },
	{ GT_PORT_ACTIVITY_DEAD, "GT_PORT_ACTIVITY_DEAD", "dead" },
	{ 0, NULL, NULL }
};

GType gt_port_activity_state_get_type(void)
{
	static gsize type_id = 0;

	if(g_once_init_enter(&type_id))
		g_once_init_leave(&type_id, g_enum_register_static("GtPortActivityState", state_values));
	return type_id;
}

const gchar *gt_port_activity_state_nick(GtPortActivityState st)
{
	for(guint i = 0; state_values[i].value_nick != NULL; i++)
		if(state_values[i].value == (gint)st)
			return state_values[i].value_nick;
	return "?";
}

static gboolean is_lost(GtPortActivityLink l)
{
	return l == GT_PORT_LINK_LOST || l == GT_PORT_LINK_RECONNECTING;
}

static GtPortActivityState compute(GtPortActivity *a, gint64 now)
{
	gint64 no_data = (gint64)a->no_data_s * G_USEC_PER_SEC;
	gint64 idle = (gint64)a->idle_s * G_USEC_PER_SEC;

	switch(a->link)
	{
	case GT_PORT_LINK_CONNECTING:
	case GT_PORT_LINK_LISTENING:
		return GT_PORT_ACTIVITY_CONNECTING;
	case GT_PORT_LINK_LOST:
	case GT_PORT_LINK_RECONNECTING:
		if(no_data > 0 && now - a->lost_us >= no_data)
			return GT_PORT_ACTIVITY_DEAD;
		return a->link == GT_PORT_LINK_LOST ? GT_PORT_ACTIVITY_LOST : GT_PORT_ACTIVITY_RECONNECTING;
	case GT_PORT_LINK_OPEN:
		if(a->last_rx_us < 0)
			return (no_data > 0 && now - a->opened_us >= no_data) ? GT_PORT_ACTIVITY_DEAD : GT_PORT_ACTIVITY_WAITING;
		if(now - a->last_rx_us < GT_PORT_ACTIVITY_RX_PULSE_US)
			return GT_PORT_ACTIVITY_RECEIVING;
		if(idle > 0 && now - a->last_rx_us >= idle)
			return GT_PORT_ACTIVITY_IDLE;
		return GT_PORT_ACTIVITY_ACTIVE;
	default:
		return GT_PORT_ACTIVITY_CLOSED;
	}
}

/* Prochaine échéance strictement après now, -1 sans échéance */
static gint64 next_deadline(GtPortActivity *a, gint64 now)
{
	gint64 no_data = (gint64)a->no_data_s * G_USEC_PER_SEC;
	gint64 idle = (gint64)a->idle_s * G_USEC_PER_SEC;
	gint64 due = -1;
	gint64 c[2] = { -1, -1 };

	if(a->link == GT_PORT_LINK_OPEN && a->last_rx_us < 0 && no_data > 0)
		c[0] = a->opened_us + no_data;
	else if(a->link == GT_PORT_LINK_OPEN && a->last_rx_us >= 0)
	{
		c[0] = a->last_rx_us + GT_PORT_ACTIVITY_RX_PULSE_US;
		if(idle > 0)
			c[1] = a->last_rx_us + idle;
	}
	else if(is_lost(a->link) && no_data > 0)
		c[0] = a->lost_us + no_data;
	for(guint i = 0; i < G_N_ELEMENTS(c); i++)
		if(c[i] > now && (due < 0 || c[i] < due))
			due = c[i];
	return due;
}

static void update(GtPortActivity *a, gint64 now);

static gboolean on_timer(gpointer data)
{
	GtPortActivity *a = data;

	a->timer_id = 0;
	update(a, g_get_monotonic_time());
	return G_SOURCE_REMOVE;
}

/* Une seule source : réarmée seulement si l'échéance est plus proche que celle déjà armée (une réception repousse les échéances : aucune
 * source par paquet) ; à l'expiration, update réarme sur la suivante */
static void schedule(GtPortActivity *a, gint64 now)
{
	gint64 due;

	if(!a->use_timer)
		return;
	due = next_deadline(a, now);
	if(due < 0)
	{
		g_clear_handle_id(&a->timer_id, g_source_remove);
		return;
	}
	if(a->timer_id != 0 && a->timer_due_us <= due)
		return;
	g_clear_handle_id(&a->timer_id, g_source_remove);
	a->timer_due_us = due;
	a->timer_id = g_timeout_add((guint)MAX((due - now + 999) / 1000, 1), on_timer, a);
}

static void update(GtPortActivity *a, gint64 now)
{
	GtPortActivityState st = compute(a, now);

	if(st != a->state)
	{
		a->state = st;
		g_object_notify_by_pspec(G_OBJECT(a), props[PROP_STATE]);
	}
	schedule(a, now);
}

static void set_unread(GtPortActivity *a, gboolean unread)
{
	if(a->unread == unread)
		return;
	a->unread = unread;
	g_object_notify_by_pspec(G_OBJECT(a), props[PROP_UNREAD]);
}

GtPortActivity *gt_port_activity_new(void)
{
	return g_object_new(GT_TYPE_PORT_ACTIVITY, NULL);
}

void gt_port_activity_set_link_state(GtPortActivity *a, GtPortActivityLink st, gint64 now_us)
{
	if(st == GT_PORT_LINK_OPEN && a->link != GT_PORT_LINK_OPEN)
	{
		a->opened_us = now_us;
		a->last_rx_us = -1;
	}
	if(is_lost(st) && !is_lost(a->link))
		a->lost_us = now_us;
	a->link = st;
	update(a, now_us);
}

void gt_port_activity_received(GtPortActivity *a, gint64 now_us)
{
	if(a->link != GT_PORT_LINK_OPEN)
		return;
	a->last_rx_us = now_us;
	if(!a->viewed)
		set_unread(a, TRUE);
	update(a, now_us);
}

void gt_port_activity_reset(GtPortActivity *a, gint64 now_us)
{
	if(a->link == GT_PORT_LINK_OPEN)
	{
		a->opened_us = now_us;
		a->last_rx_us = -1;
	}
	update(a, now_us);
}

void gt_port_activity_set_timeouts(GtPortActivity *a, guint idle_s, guint no_data_s, gint64 now_us)
{
	a->idle_s = MIN(idle_s, GT_PORT_ACTIVITY_TIMEOUT_MAX);
	a->no_data_s = MIN(no_data_s, GT_PORT_ACTIVITY_TIMEOUT_MAX);
	g_clear_handle_id(&a->timer_id, g_source_remove);	/* échéances changées : la source est recalculée */
	update(a, now_us);
}

void gt_port_activity_set_viewed(GtPortActivity *a, gboolean viewed)
{
	a->viewed = viewed != FALSE;
	if(a->viewed)
		set_unread(a, FALSE);
}

void gt_port_activity_tick(GtPortActivity *a, gint64 now_us)
{
	update(a, now_us);
}

GtPortActivityState gt_port_activity_get_state(GtPortActivity *a)
{
	return a->state;
}

gboolean gt_port_activity_get_unread(GtPortActivity *a)
{
	return a->unread;
}

static void gt_port_activity_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GtPortActivity *a = GT_PORT_ACTIVITY(object);

	switch(prop_id)
	{
	case PROP_STATE:
		g_value_set_enum(value, a->state);
		break;
	case PROP_UNREAD:
		g_value_set_boolean(value, a->unread);
		break;
	case PROP_USE_TIMER:
		g_value_set_boolean(value, a->use_timer);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}

static void gt_port_activity_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GtPortActivity *a = GT_PORT_ACTIVITY(object);

	switch(prop_id)
	{
	case PROP_USE_TIMER:
		a->use_timer = g_value_get_boolean(value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}

static void gt_port_activity_dispose(GObject *object)
{
	GtPortActivity *a = GT_PORT_ACTIVITY(object);

	g_clear_handle_id(&a->timer_id, g_source_remove);
	G_OBJECT_CLASS(gt_port_activity_parent_class)->dispose(object);
}

static void gt_port_activity_class_init(GtPortActivityClass *klass)
{
	GObjectClass *oc = G_OBJECT_CLASS(klass);

	oc->get_property = gt_port_activity_get_property;
	oc->set_property = gt_port_activity_set_property;
	oc->dispose = gt_port_activity_dispose;
	props[PROP_STATE] = g_param_spec_enum("state", NULL, NULL, GT_TYPE_PORT_ACTIVITY_STATE, GT_PORT_ACTIVITY_CLOSED,
	                                      G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
	props[PROP_UNREAD] = g_param_spec_boolean("unread", NULL, NULL, FALSE, G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
	props[PROP_USE_TIMER] = g_param_spec_boolean("use-timer", NULL, NULL, TRUE,
	                                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
	g_object_class_install_properties(oc, N_PROPS, props);
}

static void gt_port_activity_init(GtPortActivity *a)
{
	a->link = GT_PORT_LINK_CLOSED;
	a->state = GT_PORT_ACTIVITY_CLOSED;
	a->last_rx_us = -1;
	a->idle_s = 10;
	a->no_data_s = 60;
	a->viewed = TRUE;
	a->use_timer = TRUE;
}
