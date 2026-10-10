/* Saisie du dialogue Port (voir port_form.h). */
#include <config.h>
#include <glib/gi18n.h>
#include <string.h>

#include "port_form.h"

G_DEFINE_QUARK(gt-port-form-error-quark, gt_port_form_error)

void gt_port_form_from_profile(GtPortForm *f, const GtProfile *p)
{
	memset(f, 0, sizeof(*f));
	f->transport = p->transport;
	f->device = g_strdup(p->port != NULL ? p->port : "");
	f->baudrate = g_strdup_printf("%u", p->baudrate != 0 ? p->baudrate : GT_PORT_FORM_DEFAULT_BAUDRATE);
	f->parity = p->parity;
	f->bits = (p->bits >= 5 && p->bits <= 8) ? p->bits : 8;
	f->stopbits = (p->stopbits == 1 || p->stopbits == 2) ? p->stopbits : 1;
	f->flow = p->flow;
	f->host = g_strdup((p->host != NULL && p->host[0] != '\0') ? p->host : GT_PORT_FORM_DEFAULT_HOST);
	f->tcp_port = g_strdup((p->tcp_port != NULL && p->tcp_port[0] != '\0') ? p->tcp_port : GT_PORT_FORM_DEFAULT_TCP_PORT);
	f->wait_delay_ms = CLAMP(p->wait_delay_ms, 0, GT_PORT_FORM_DELAY_MAX);
	f->wait_char_enabled = p->wait_char >= 0 && p->wait_char <= 255;
	if(p->wait_char >= 1 && p->wait_char <= 127)
		f->wait_char = g_strdup_printf("%c", (char)p->wait_char);
	else
		f->wait_char = g_strdup("");
	f->rs485_before_ms = CLAMP(p->rs485_before_ms, 0, GT_PORT_FORM_DELAY_MAX);
	f->rs485_after_ms = CLAMP(p->rs485_after_ms, 0, GT_PORT_FORM_DELAY_MAX);
}

void gt_port_form_clear(GtPortForm *f)
{
	g_clear_pointer(&f->device, g_free);
	g_clear_pointer(&f->baudrate, g_free);
	g_clear_pointer(&f->host, g_free);
	g_clear_pointer(&f->tcp_port, g_free);
	g_clear_pointer(&f->wait_char, g_free);
}

/* Texte rogné (copie à libérer) ; NULL vaut "" */
static gchar *trimmed(const gchar *text)
{
	return g_strstrip(g_strdup(text != NULL ? text : ""));
}

/* Entier décimal sans signe ni espace intérieur, dans [min, max] */
static gboolean parse_number(const gchar *text, guint64 min, guint64 max, guint64 *out)
{
	gchar *t = trimmed(text);
	gboolean ok = g_ascii_string_to_unsigned(t, 10, min, max, out, NULL);

	g_free(t);
	return ok;
}

static gboolean delay_ok(gint ms)
{
	return ms >= 0 && ms <= GT_PORT_FORM_DELAY_MAX;
}

gboolean gt_port_form_apply(const GtPortForm *f, const GtProfile *base, GtProfile *out, GError **error)
{
	gboolean serial = f->transport == TRANSPORT_SERIAL;
	gchar *device = trimmed(f->device), *host = trimmed(f->host);
	guint64 baudrate = 0, tcp_port = 0;
	gint wait_char = -1;
	gboolean ok = FALSE;

	if(serial)
	{
		if(device[0] == '\0')
		{
			g_set_error_literal(error, GT_PORT_FORM_ERROR, GT_PORT_FORM_ERROR_DEVICE, _("Enter the serial port to open (for example /dev/ttyUSB0)."));
			goto out;
		}
		if(!parse_number(f->baudrate, 1, G_MAXINT, &baudrate))
		{
			g_set_error(error, GT_PORT_FORM_ERROR, GT_PORT_FORM_ERROR_BAUDRATE, _("The baud rate must be a whole number between 1 and %d."), G_MAXINT);
			goto out;
		}
		if(f->bits < 5 || f->bits > 8 || (f->stopbits != 1 && f->stopbits != 2))
		{
			g_set_error_literal(error, GT_PORT_FORM_ERROR, GT_PORT_FORM_ERROR_SERIAL, _("Invalid number of data bits or stop bits."));
			goto out;
		}
		if(!delay_ok(f->rs485_before_ms) || !delay_ok(f->rs485_after_ms))
		{
			g_set_error(error, GT_PORT_FORM_ERROR, GT_PORT_FORM_ERROR_DELAY, _("Delays must be between 0 and %d ms."), GT_PORT_FORM_DELAY_MAX);
			goto out;
		}
	}
	else
	{
		if(f->transport == TRANSPORT_TCP_CLIENT && host[0] == '\0')
		{
			g_set_error_literal(error, GT_PORT_FORM_ERROR, GT_PORT_FORM_ERROR_HOST, _("Enter the host to connect to."));
			goto out;
		}
		if(!parse_number(f->tcp_port, 1, 65535, &tcp_port))
		{
			g_set_error_literal(error, GT_PORT_FORM_ERROR, GT_PORT_FORM_ERROR_TCP_PORT, _("The TCP port must be a number between 1 and 65535."));
			goto out;
		}
	}
	if(!delay_ok(f->wait_delay_ms))
	{
		g_set_error(error, GT_PORT_FORM_ERROR, GT_PORT_FORM_ERROR_DELAY, _("Delays must be between 0 and %d ms."), GT_PORT_FORM_DELAY_MAX);
		goto out;
	}
	if(f->wait_char_enabled)
	{
		const gchar *c = f->wait_char != NULL ? f->wait_char : "";

		if(strlen(c) != 1 || (guchar)c[0] > 0x7F)
		{
			g_set_error_literal(error, GT_PORT_FORM_ERROR, GT_PORT_FORM_ERROR_WAIT_CHAR,
			                    _("Enter the character to wait for: exactly one ASCII character."));
			goto out;
		}
		wait_char = (guchar)c[0];
	}

	gt_profile_copy(out, base);
	out->transport = f->transport;
	if(serial)
	{
		gt_profile_set_string(&out->port, device);
		out->baudrate = (guint)baudrate;
		out->parity = f->parity;
		out->bits = f->bits;
		out->stopbits = f->stopbits;
		out->flow = f->flow;
		out->rs485_before_ms = f->rs485_before_ms;
		out->rs485_after_ms = f->rs485_after_ms;
	}
	else
	{
		gchar *port_text = g_strdup_printf("%" G_GUINT64_FORMAT, tcp_port);

		gt_profile_set_string(&out->host, host);
		gt_profile_set_string(&out->tcp_port, port_text);
		g_free(port_text);
	}
	out->wait_char = wait_char;
	out->wait_delay_ms = wait_char >= 0 ? 0 : f->wait_delay_ms;
	ok = TRUE;
out:
	g_free(device);
	g_free(host);
	return ok;
}
