/***********************************************************************/
/* link.c                                                              */
/*                                                                     */
/*   Purpose                                                           */
/*      Liaison du port principal : machine d'état, reconnexion,       */
/*      moniteur udev, historique (sous-projet 2)                      */
/*                                                                     */
/***********************************************************************/

#include <config.h>
#include <glib/gi18n.h>
#include <gudev/gudev.h>
#include <string.h>

#include "device_match.h"
#include "link.h"

struct _GtkTermLink
{
	GObject parent_instance;

	GtLinkState state;
	gchar *status_text;
	gboolean auto_reconnect;
	guint reconnect_interval_ms;

	TransportParams params;		/* derniers paramètres demandés (reconnexion, texte d'état) */
	gboolean have_params;
	GtTransport *transport;
	gboolean user_open_pending;	/* client TCP : ouverture demandée, connexion pas encore établie */
	guint actual_baudrate;		/* dernier débit réel (texte d'état) */
	dev_t devnum;			/* périphérique série ouvert (correspondance udev) */
	guint reconnect_timer_id;
	guint open_seq;			/* incrémenté à chaque ouverture ou fermeture demandée (gestionnaire d'open-failed réentrant) */
	GUdevClient *udev;
	GtHistory *history;
};

enum
{
	PROP_0,
	PROP_STATE,
	PROP_STATUS_TEXT,
	PROP_AUTO_RECONNECT,
	PROP_RECONNECT_INTERVAL_MS,
	N_PROPS
};

enum
{
	SIG_OPEN_FAILED,
	SIG_RECEIVED,
	SIG_SENT,
	SIG_HISTORY_CLEARED,
	N_SIGNALS
};

static GParamSpec *props[N_PROPS];
static guint signals[N_SIGNALS];
static GtkTermLink *default_link = NULL;

G_DEFINE_TYPE(GtkTermLink, gt_link, G_TYPE_OBJECT)

static void link_try_reconnect(GtkTermLink *link);

GType gt_link_state_get_type(void)
{
	static gsize type_id = 0;
	static const GEnumValue values[] =
	{
		{ GT_LINK_CLOSED, "GT_LINK_CLOSED", "closed" },
		{ GT_LINK_CONNECTING, "GT_LINK_CONNECTING", "connecting" },
		{ GT_LINK_OPEN, "GT_LINK_OPEN", "open" },
		{ GT_LINK_LISTENING, "GT_LINK_LISTENING", "listening" },
		{ GT_LINK_LOST, "GT_LINK_LOST", "lost" },
		{ GT_LINK_RECONNECTING, "GT_LINK_RECONNECTING", "reconnecting" },
		{ 0, NULL, NULL }
	};

	if(g_once_init_enter(&type_id))
		g_once_init_leave(&type_id, g_enum_register_static("GtLinkState", values));
	return type_id;
}

/* ---- Texte d'état ------------------------------------------------------ */

static gchar *link_describe(GtkTermLink *link)
{
	static const gchar parity_char[] = { 'N', 'O', 'E' };
	const TransportParams *p = &link->params;
	const gchar *host = (p->host != NULL && p->host[0] != '\0') ? p->host : "*";

	switch(p->type)
	{
	case TRANSPORT_SERIAL:
		return g_strdup_printf("%s  %u-%d-%c-%d", p->port, link->actual_baudrate != 0 ? link->actual_baudrate : p->baudrate, p->bits,
		                       parity_char[p->parity <= GT_PARITY_EVEN ? p->parity : 0], p->stopbits);
	case TRANSPORT_TCP_CLIENT:
		return g_strdup_printf("tcp://%s:%s", p->host, p->tcp_port);
	default:
		if(link->state == GT_LINK_LISTENING)
			return g_strdup_printf(_("tcp://%s:%s (server, no client)"), host, p->tcp_port);
		return g_strdup_printf(_("tcp://%s:%s (server)"), host, p->tcp_port);
	}
}

static void link_update_status(GtkTermLink *link)
{
	gchar *text, *base;

	if(link->state == GT_LINK_CLOSED || !link->have_params)
		text = g_strdup(_("No open port"));
	else
	{
		base = link_describe(link);
		switch(link->state)
		{
		case GT_LINK_CONNECTING:
			text = g_strdup_printf(_("%s — connecting…"), base);
			break;
		case GT_LINK_LOST:
			text = g_strdup_printf(_("%s — lost"), base);
			break;
		case GT_LINK_RECONNECTING:
			text = g_strdup_printf(_("%s — reconnecting…"), base);
			break;
		default:
			text = g_strdup(base);
		}
		g_free(base);
	}

	if(g_strcmp0(text, link->status_text) != 0)
	{
		g_free(link->status_text);
		link->status_text = text;
		g_object_notify_by_pspec(G_OBJECT(link), props[PROP_STATUS_TEXT]);
	}
	else
		g_free(text);
}

static void link_set_state(GtkTermLink *link, GtLinkState state)
{
	if(link->state != state)
	{
		link->state = state;
		g_object_notify_by_pspec(G_OBJECT(link), props[PROP_STATE]);
	}
	link_update_status(link);
}

/* ---- Minuterie de reconnexion et moniteur udev ------------------------- */

static void link_stop_reconnect_timer(GtkTermLink *link)
{
	if(link->reconnect_timer_id != 0)
	{
		g_source_remove(link->reconnect_timer_id);
		link->reconnect_timer_id = 0;
	}
}

static gboolean link_reconnect_tick(gpointer data)
{
	link_try_reconnect(data);
	return G_SOURCE_CONTINUE;
}

static void link_start_reconnect(GtkTermLink *link)
{
	link_set_state(link, GT_LINK_RECONNECTING);
	if(link->reconnect_timer_id == 0)
		link->reconnect_timer_id = g_timeout_add(link->reconnect_interval_ms, link_reconnect_tick, link);
}

static void link_device_event(GtkTermLink *link, const gchar *action, dev_t devnum, const gchar *file, const gchar *const *links);

static void link_udev_uevent(GUdevClient *client, const gchar *action, GUdevDevice *device, gpointer user_data)
{
	if(action == NULL || device == NULL || g_udev_device_get_device_file(device) == NULL)
		return;
	link_device_event(user_data, action, g_udev_device_get_device_number(device), g_udev_device_get_device_file(device),
	                  g_udev_device_get_device_file_symlinks(device));
}

/* Armé à chaque ouverture réussie d'un port série, ou à son échec quand la reconnexion automatique est active ;
   n'ouvre jamais le port de lui-même (M2, M4) */
static void link_arm_udev(GtkTermLink *link)
{
	const gchar *const subsystems[] = { "tty", NULL };

	if(link->udev != NULL)
		return;
	link->udev = g_udev_client_new(subsystems);
	g_signal_connect(link->udev, "uevent", G_CALLBACK(link_udev_uevent), link);
}

static void link_disarm_udev(GtkTermLink *link)
{
	if(link->udev == NULL)
		return;
	g_signal_handlers_disconnect_by_data(link->udev, link);
	g_clear_object(&link->udev);
}

/* ---- Transport --------------------------------------------------------- */

static void link_handle_lost(GtkTermLink *link, const gchar *detail);

static void link_on_data(const guint8 *data, gsize len, gpointer user_data)
{
	GtkTermLink *link = user_data;
	gint64 now = g_get_real_time();
	GBytes *bytes;

	gt_history_append(link->history, GT_RECORD_RX, GT_SEND_KEYBOARD, now, data, len);
	bytes = g_bytes_new(data, len);
	g_signal_emit(link, signals[SIG_RECEIVED], 0, bytes, now);
	g_bytes_unref(bytes);
}

static void link_on_event(TransportEvent ev, const gchar *detail, gpointer user_data)
{
	GtkTermLink *link = user_data;

	switch(ev)
	{
	case TRANSPORT_EVENT_CONNECTED:
		link->user_open_pending = FALSE;
		link_stop_reconnect_timer(link);
		link_set_state(link, GT_LINK_OPEN);
		break;
	case TRANSPORT_EVENT_CLIENT_CONNECTED:
		link_set_state(link, GT_LINK_OPEN);
		break;
	case TRANSPORT_EVENT_CLIENT_GONE:
		link_set_state(link, GT_LINK_LISTENING);
		break;
	case TRANSPORT_EVENT_LOST:
		link_handle_lost(link, detail);
		break;
	}
}

static gboolean link_open_transport(GtkTermLink *link, GError **error)
{
	static const TransportCallbacks callbacks = { link_on_data, link_on_event };
	GtTransport *t = gt_transport_open(&link->params, &callbacks, link, error);

	if(t == NULL)
		return FALSE;
	link->transport = t;
	if(link->params.type == TRANSPORT_SERIAL)
	{
		link->actual_baudrate = gt_transport_get_actual_baudrate(t);
		link->devnum = gt_transport_get_device_number(t);
	}
	return TRUE;
}

static void link_close_transport(GtkTermLink *link)
{
	gt_transport_close(link->transport);
	link->transport = NULL;
}

/* Ouverture demandée échouée : open-failed, puis reconnexion si l'option est active (série et client TCP ; le serveur reste fermé).
   Le gestionnaire d'open-failed ouvre une boîte de dialogue (boucle imbriquée) : s'il a rouvert ou fermé la liaison entre-temps, rien de plus. */
static void link_open_failed(GtkTermLink *link, const gchar *message)
{
	guint seq = link->open_seq;

	link_set_state(link, GT_LINK_CLOSED);
	g_signal_emit(link, signals[SIG_OPEN_FAILED], 0, message != NULL ? message : "");
	if(seq != link->open_seq || link->state != GT_LINK_CLOSED || !link->auto_reconnect || link->params.type == TRANSPORT_TCP_SERVER)
		return;
	if(link->params.type == TRANSPORT_SERIAL)
		link_arm_udev(link);	/* l'« add » du chemin configuré déclenche un essai immédiat */
	link_start_reconnect(link);
}

/* LOST du transport (signalé hors de tout appel au transport) ou udev « remove » */
static void link_handle_lost(GtkTermLink *link, const gchar *detail)
{
	link_close_transport(link);

	if(link->user_open_pending)
	{
		/* Client TCP : la connexion demandée par l'utilisateur a échoué */
		link->user_open_pending = FALSE;
		link_open_failed(link, detail);
		return;
	}
	if(link->state == GT_LINK_RECONNECTING)
		return;		/* tentative de reconnexion TCP échouée : la minuterie continue, en silence */

	link_set_state(link, GT_LINK_LOST);
	if(link->auto_reconnect)
		link_start_reconnect(link);
}

/* Essai silencieux : jamais de open-failed pendant une reconnexion */
static void link_try_reconnect(GtkTermLink *link)
{
	if(link->state != GT_LINK_RECONNECTING || link->transport != NULL)
		return;
	if(!link_open_transport(link, NULL))
		return;
	if(link->params.type == TRANSPORT_TCP_CLIENT)
		return;		/* OPEN à l'événement CONNECTED */
	link_stop_reconnect_timer(link);
	link_set_state(link, link->params.type == TRANSPORT_TCP_SERVER ? GT_LINK_LISTENING : GT_LINK_OPEN);
}

static void link_device_event(GtkTermLink *link, const gchar *action, dev_t devnum, const gchar *file, const gchar *const *links)
{
	if(!link->have_params || link->params.type != TRANSPORT_SERIAL)
		return;

	if(g_strcmp0(action, "remove") == 0 && link->state == GT_LINK_OPEN)
	{
		if(device_match(link->devnum, link->params.port, devnum, file, links))
			link_handle_lost(link, _("device removed"));
	}
	else if(g_strcmp0(action, "add") == 0 && link->state == GT_LINK_RECONNECTING)
	{
		/* Au rebranchement, le numéro peut changer : seul le chemin configuré compte */
		if(device_match(0, link->params.port, devnum, file, links))
			link_try_reconnect(link);
	}
}

void gt_link_handle_device_event(GtkTermLink *link, const gchar *action, dev_t devnum, const gchar *path)
{
	g_return_if_fail(GT_IS_LINK(link));
	link_device_event(link, action, devnum, path, NULL);
}

static void link_shutdown(GtkTermLink *link)
{
	link_stop_reconnect_timer(link);
	link_close_transport(link);
	link->user_open_pending = FALSE;
	link_disarm_udev(link);
}

/* ---- Interface publique ------------------------------------------------ */

GtkTermLink *gt_link_new(void)
{
	return g_object_new(GT_TYPE_LINK, NULL);
}

void gt_link_set_default(GtkTermLink *link)
{
	default_link = link;
}

GtkTermLink *gt_link_get_default(void)
{
	return default_link;
}

gboolean gt_link_open(GtkTermLink *link, const TransportParams *params, GError **error)
{
	GError *local = NULL;

	g_return_val_if_fail(GT_IS_LINK(link), FALSE);
	g_return_val_if_fail(params != NULL, FALSE);

	link_shutdown(link);
	link->open_seq++;
	transport_params_clear(&link->params);
	transport_params_copy(&link->params, params);
	link->have_params = TRUE;
	link->actual_baudrate = 0;
	link->devnum = 0;

	if(!link_open_transport(link, &local))
	{
		link_open_failed(link, local->message);
		g_propagate_error(error, local);
		return FALSE;
	}

	switch(params->type)
	{
	case TRANSPORT_SERIAL:
		link_arm_udev(link);
		link_set_state(link, GT_LINK_OPEN);
		break;
	case TRANSPORT_TCP_SERVER:
		link_set_state(link, GT_LINK_LISTENING);
		break;
	default:
		link->user_open_pending = TRUE;
		link_set_state(link, GT_LINK_CONNECTING);
	}
	return TRUE;
}

void gt_link_close(GtkTermLink *link)
{
	g_return_if_fail(GT_IS_LINK(link));
	link_shutdown(link);
	link->open_seq++;
	link_set_state(link, GT_LINK_CLOSED);
}

gssize gt_link_send(GtkTermLink *link, const guint8 *data, gsize len, GtSendOrigin origin)
{
	gssize written;
	GBytes *bytes;

	g_return_val_if_fail(GT_IS_LINK(link), -1);
	if(link->transport == NULL || len == 0)
		return 0;

	written = gt_transport_send(link->transport, data, len);
	if(written <= 0)
		return written;

	gt_history_append(link->history, GT_RECORD_TX, origin, g_get_real_time(), data, (gsize)written);
	bytes = g_bytes_new(data, (gsize)written);
	g_signal_emit(link, signals[SIG_SENT], 0, bytes, (gint)origin);
	g_bytes_unref(bytes);
	return written;
}

gint gt_link_get_modem_lines(GtkTermLink *link)
{
	g_return_val_if_fail(GT_IS_LINK(link), -1);
	return gt_transport_get_modem_lines(link->transport);
}

void gt_link_toggle_line(GtkTermLink *link, GtModemLine line)
{
	g_return_if_fail(GT_IS_LINK(link));
	gt_transport_toggle_line(link->transport, line);
}

void gt_link_send_break(GtkTermLink *link)
{
	g_return_if_fail(GT_IS_LINK(link));
	gt_transport_send_break(link->transport);
}

gint gt_link_get_fd(GtkTermLink *link)
{
	g_return_val_if_fail(GT_IS_LINK(link), -1);
	return gt_transport_get_fd(link->transport);
}

GtLinkState gt_link_get_state(GtkTermLink *link)
{
	g_return_val_if_fail(GT_IS_LINK(link), GT_LINK_CLOSED);
	return link->state;
}

gboolean gt_link_is_serial_open(GtkTermLink *link)
{
	g_return_val_if_fail(GT_IS_LINK(link), FALSE);
	return link->state == GT_LINK_OPEN && link->transport != NULL && link->params.type == TRANSPORT_SERIAL;
}

const gchar *gt_link_get_status_text(GtkTermLink *link)
{
	g_return_val_if_fail(GT_IS_LINK(link), NULL);
	return link->status_text;
}

GtHistory *gt_link_get_history(GtkTermLink *link)
{
	g_return_val_if_fail(GT_IS_LINK(link), NULL);
	return link->history;
}

void gt_link_clear_history(GtkTermLink *link)
{
	g_return_if_fail(GT_IS_LINK(link));
	gt_history_clear(link->history);
	g_signal_emit(link, signals[SIG_HISTORY_CLEARED], 0);
}

static void link_set_auto_reconnect(GtkTermLink *link, gboolean value)
{
	if(link->auto_reconnect == value)
		return;
	link->auto_reconnect = value;
	g_object_notify_by_pspec(G_OBJECT(link), props[PROP_AUTO_RECONNECT]);

	if(value && link->state == GT_LINK_LOST)
		link_start_reconnect(link);
	else if(!value && link->state == GT_LINK_RECONNECTING)
	{
		link_stop_reconnect_timer(link);
		link_close_transport(link);	/* tentative TCP en cours */
		link_set_state(link, GT_LINK_LOST);
	}
}

/* ---- GObject ----------------------------------------------------------- */

static void gt_link_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GtkTermLink *link = GT_LINK(object);

	switch(prop_id)
	{
	case PROP_STATE:
		g_value_set_enum(value, link->state);
		break;
	case PROP_STATUS_TEXT:
		g_value_set_string(value, link->status_text);
		break;
	case PROP_AUTO_RECONNECT:
		g_value_set_boolean(value, link->auto_reconnect);
		break;
	case PROP_RECONNECT_INTERVAL_MS:
		g_value_set_uint(value, link->reconnect_interval_ms);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}

static void gt_link_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GtkTermLink *link = GT_LINK(object);

	switch(prop_id)
	{
	case PROP_AUTO_RECONNECT:
		link_set_auto_reconnect(link, g_value_get_boolean(value));
		break;
	case PROP_RECONNECT_INTERVAL_MS:
		if(link->reconnect_interval_ms != g_value_get_uint(value))
		{
			link->reconnect_interval_ms = g_value_get_uint(value);
			g_object_notify_by_pspec(object, pspec);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}

static void gt_link_dispose(GObject *object)
{
	GtkTermLink *link = GT_LINK(object);

	link_shutdown(link);
	if(default_link == link)
		default_link = NULL;
	G_OBJECT_CLASS(gt_link_parent_class)->dispose(object);
}

static void gt_link_finalize(GObject *object)
{
	GtkTermLink *link = GT_LINK(object);

	g_free(link->status_text);
	transport_params_clear(&link->params);
	gt_history_free(link->history);
	G_OBJECT_CLASS(gt_link_parent_class)->finalize(object);
}

static void gt_link_class_init(GtkTermLinkClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->get_property = gt_link_get_property;
	object_class->set_property = gt_link_set_property;
	object_class->dispose = gt_link_dispose;
	object_class->finalize = gt_link_finalize;

	props[PROP_STATE] = g_param_spec_enum("state", "State", "Connection state", GT_TYPE_LINK_STATE, GT_LINK_CLOSED,
	                                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	props[PROP_STATUS_TEXT] = g_param_spec_string("status-text", "Status text", "Status bar and window title text", NULL,
	                                              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	props[PROP_AUTO_RECONNECT] = g_param_spec_boolean("auto-reconnect", "Auto reconnect", "Reconnect silently after a link loss", FALSE,
	                                                  G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
	props[PROP_RECONNECT_INTERVAL_MS] = g_param_spec_uint("reconnect-interval-ms", "Reconnect interval", "Delay between reconnection attempts (ms)",
	                                                      10, 600000, 2000, G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
	g_object_class_install_properties(object_class, N_PROPS, props);

	signals[SIG_OPEN_FAILED] = g_signal_new("open-failed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
	                                        G_TYPE_NONE, 1, G_TYPE_STRING);
	signals[SIG_RECEIVED] = g_signal_new("received", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
	                                     G_TYPE_NONE, 2, G_TYPE_BYTES, G_TYPE_INT64);
	signals[SIG_SENT] = g_signal_new("sent", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
	                                 G_TYPE_NONE, 2, G_TYPE_BYTES, G_TYPE_INT);
	signals[SIG_HISTORY_CLEARED] = g_signal_new("history-cleared", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
	                                            G_TYPE_NONE, 0);
}

static void gt_link_init(GtkTermLink *link)
{
	link->state = GT_LINK_CLOSED;
	link->status_text = g_strdup(_("No open port"));
	link->reconnect_interval_ms = 2000;
	link->history = gt_history_new(GT_HISTORY_DEFAULT_CAPACITY);
}
