/***********************************************************************/
/* script_port.c                                                        */
/*           GTKTerm Software                                           */
/*                                                                      */
/*   Purpose                                                            */
/*      Port ouvert par un script Lua : GtTransport + GtRxQueue         */
/*      (cœur sans GTK, sous-projet 5)                                  */
/***********************************************************************/

#include <errno.h>
#include <string.h>

#include "script_port.h"

struct _GtScriptPort
{
	GtTransport *transport;
	GtRxQueue *queue;
	gint state;		/* GtScriptPortState, lu de n'importe quel thread (atomique) */
	GMutex reason_mutex;
	gchar *reason;		/* raison de la perte, sans fin de ligne ; NULL si non perdu */
};

/* Rappels du transport : thread principal (surveillance du descripteur ; LOST depuis une source d'inactivité) */
static void
script_port_on_data(const guint8 *data, gsize len, gpointer user_data)
{
	GtScriptPort *port = user_data;

	gt_rx_queue_push(port->queue, data, len);
}

static void
script_port_on_event(TransportEvent ev, const gchar *detail, gpointer user_data)
{
	GtScriptPort *port = user_data;

	switch(ev)
	{
	case TRANSPORT_EVENT_CONNECTED:
		g_atomic_int_set(&port->state, GT_SCRIPT_PORT_OPEN);
		break;
	case TRANSPORT_EVENT_CLIENT_CONNECTED:
		g_atomic_int_set(&port->state, GT_SCRIPT_PORT_CONNECTED);
		break;
	case TRANSPORT_EVENT_CLIENT_GONE:
		g_atomic_int_set(&port->state, GT_SCRIPT_PORT_LISTENING);
		break;
	case TRANSPORT_EVENT_LOST:
		g_mutex_lock(&port->reason_mutex);
		g_free(port->reason);
		port->reason = g_strchomp(g_strdup(detail != NULL ? detail : "link lost"));
		g_mutex_unlock(&port->reason_mutex);
		/* Raison, puis file fermée, puis état : qui lit LOST trouve la raison et n'attend plus sur une file encore ouverte */
		gt_rx_queue_set_closed(port->queue, TRUE);
		g_atomic_int_set(&port->state, GT_SCRIPT_PORT_LOST);
		break;
	}
}

static const TransportCallbacks script_port_callbacks = { script_port_on_data, script_port_on_event };

GtScriptPort *
gt_script_port_open(const TransportParams *params, GError **error)
{
	TransportParams p;
	GtScriptPort *port;

	g_return_val_if_fail(params != NULL, NULL);
	port = g_new0(GtScriptPort, 1);
	g_mutex_init(&port->reason_mutex);
	port->queue = gt_rx_queue_new(GT_SCRIPT_RX_MAX);
	switch(params->type)
	{
	case TRANSPORT_TCP_CLIENT:
		port->state = GT_SCRIPT_PORT_CONNECTING;
		break;
	case TRANSPORT_TCP_SERVER:
		port->state = GT_SCRIPT_PORT_LISTENING;
		break;
	default:
		port->state = GT_SCRIPT_PORT_OPEN;
		break;
	}
	/* Copie superficielle : seul lock_port change ; gt_transport_open fait sa propre copie profonde */
	p = *params;
	if(p.type == TRANSPORT_SERIAL)
		p.lock_port = TRUE;
	port->transport = gt_transport_open(&p, &script_port_callbacks, port, error);
	if(port->transport == NULL)
	{
		gt_rx_queue_free(port->queue);
		g_mutex_clear(&port->reason_mutex);
		g_free(port);
		return NULL;
	}
	return port;
}

void
gt_script_port_close(GtScriptPort *port)
{
	if(port == NULL)
		return;
	/* Retire aussi un signalement de perte en attente : plus aucun rappel sur ce port ensuite */
	gt_transport_close(port->transport);
	gt_rx_queue_set_closed(port->queue, TRUE);
	gt_rx_queue_free(port->queue);
	g_free(port->reason);
	g_mutex_clear(&port->reason_mutex);
	g_free(port);
}

gssize
gt_script_port_try_send(GtScriptPort *port, const guint8 *data, gsize len, gint *err)
{
	gssize n;

	g_return_val_if_fail(err != NULL, -1);
	*err = 0;
	if(port == NULL)
	{
		*err = EBADF;
		return -1;
	}
	if(len == 0)
		return 0;
	errno = 0;
	n = gt_transport_send(port->transport, data, len);
	if(n < 0)
		*err = errno;	/* relevé tout de suite : errno est propre au thread et le prochain appel système l'écraserait */
	return n;
}

guint
gt_script_port_get_output_pending(GtScriptPort *port)
{
	g_return_val_if_fail(port != NULL, 0);
	return gt_transport_get_output_pending(port->transport);
}

guint
gt_script_port_get_baudrate(GtScriptPort *port)
{
	g_return_val_if_fail(port != NULL, 0);
	return gt_transport_get_actual_baudrate(port->transport);
}

GtScriptPortState
gt_script_port_get_state(GtScriptPort *port)
{
	g_return_val_if_fail(port != NULL, GT_SCRIPT_PORT_LOST);
	return (GtScriptPortState)g_atomic_int_get(&port->state);
}

GtRxQueue *
gt_script_port_get_queue(GtScriptPort *port)
{
	g_return_val_if_fail(port != NULL, NULL);
	return port->queue;
}

gchar *
gt_script_port_dup_reason(GtScriptPort *port)
{
	gchar *r;

	g_return_val_if_fail(port != NULL, NULL);
	g_mutex_lock(&port->reason_mutex);
	r = g_strdup(port->reason);
	g_mutex_unlock(&port->reason_mutex);
	return r;
}

gboolean
gt_script_parity_from_name(const gchar *name, GtParity *out)
{
	if(g_strcmp0(name, "none") == 0)
		*out = GT_PARITY_NONE;
	else if(g_strcmp0(name, "odd") == 0)
		*out = GT_PARITY_ODD;
	else if(g_strcmp0(name, "even") == 0)
		*out = GT_PARITY_EVEN;
	else
		return FALSE;
	return TRUE;
}

gboolean
gt_script_flow_from_name(const gchar *name, GtFlow *out)
{
	if(g_strcmp0(name, "none") == 0)
		*out = GT_FLOW_NONE;
	else if(g_strcmp0(name, "xon") == 0)
		*out = GT_FLOW_XONXOFF;
	else if(g_strcmp0(name, "rts") == 0)
		*out = GT_FLOW_RTSCTS;
	else
		return FALSE;
	return TRUE;
}

GBytes *
gt_script_hex_decode(const gchar *text, GError **error)
{
	GByteArray *out;
	gint high = -1;		/* premier chiffre de l'octet en cours, -1 entre deux octets */

	g_return_val_if_fail(text != NULL, NULL);
	out = g_byte_array_new();
	for(gsize i = 0; text[i] != '\0'; i++)
	{
		gchar c = text[i];
		gint v;

		if(c == ' ' || c == '\t' || c == '\r' || c == '\n')
		{
			if(high < 0)
				continue;
			v = -1;		/* blanc entre les deux chiffres d'un octet : invalide */
		}
		else
			v = g_ascii_xdigit_value(c);
		if(v < 0)
		{
			g_set_error(error, G_CONVERT_ERROR, G_CONVERT_ERROR_ILLEGAL_SEQUENCE, "invalid character at position %zu", i);
			g_byte_array_unref(out);
			return NULL;
		}
		if(high < 0)
			high = v;
		else
		{
			guint8 octet = (guint8)((high << 4) | v);

			g_byte_array_append(out, &octet, 1);
			high = -1;
		}
	}
	if(high >= 0)
	{
		g_set_error_literal(error, G_CONVERT_ERROR, G_CONVERT_ERROR_ILLEGAL_SEQUENCE, "odd number of hex digits");
		g_byte_array_unref(out);
		return NULL;
	}
	return g_byte_array_free_to_bytes(out);
}
