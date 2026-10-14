/***********************************************************************/
/* profile.c                                                           */
/*                                                                     */
/*   Purpose                                                           */
/*      Profil de connexion et d'affichage, sans GTK (sous-projet 3)   */
/*                                                                     */
/***********************************************************************/

#include <string.h>

#include <config.h>
#include <glib/gi18n.h>

#include "profile.h"

static const gchar * const parity_names[] = { "none", "odd", "even" };
static const gchar * const flow_names[] = { "none", "xon", "rts", "rs485" };
static const gchar * const transport_names[] = { "serial", "tcp-client", "tcp-server" };

void gt_profile_init_defaults(GtProfile *p)
{
	memset(p, 0, sizeof(*p));
	p->transport = TRANSPORT_SERIAL;
	p->port = g_strdup(GT_PROFILE_DEFAULT_PORT);
	p->baudrate = 115200;
	p->bits = 8;
	p->stopbits = 1;
	p->parity = GT_PARITY_NONE;
	p->flow = GT_FLOW_NONE;
	p->rs485_before_ms = 30;
	p->rs485_after_ms = 30;
	p->lock_port = TRUE;
	p->host = g_strdup("");
	p->tcp_port = g_strdup("");
	p->wait_delay_ms = 0;
	p->wait_char = -1;
	p->idle_timeout_s = 10;
	p->no_data_timeout_s = 60;
	p->font = g_strdup(GT_PROFILE_DEFAULT_FONT);
	p->block_cursor = TRUE;
	p->columns = 80;
	p->rows = 25;
	p->scrollback = 10000;
	p->visual_bell = FALSE;
	p->foreground = (GtColor){ 0.0, 0.0, 0.0, 1.0 };
	p->background = (GtColor){ 1.0, 1.0, 1.0, 1.0 };
	p->macros_file = NULL;
}

void gt_profile_clear(GtProfile *p)
{
	g_clear_pointer(&p->port, g_free);
	g_clear_pointer(&p->host, g_free);
	g_clear_pointer(&p->tcp_port, g_free);
	g_clear_pointer(&p->font, g_free);
	g_clear_pointer(&p->macros_file, g_free);
}

void gt_profile_copy(GtProfile *dst, const GtProfile *src)
{
	if(dst == src)
		return;
	gt_profile_clear(dst);
	*dst = *src;
	dst->port = g_strdup(src->port);
	dst->host = g_strdup(src->host);
	dst->tcp_port = g_strdup(src->tcp_port);
	dst->font = g_strdup(src->font);
	dst->macros_file = g_strdup(src->macros_file);
}

void gt_profile_set_string(gchar **field, const gchar *value)
{
	gchar *copy = g_strdup(value);

	g_free(*field);
	*field = copy;
}

gboolean gt_profile_validate(GtProfile *p, GPtrArray *warnings)
{
	guint before = warnings->len;

	if(find_standard_baudrate(p->baudrate) == B0)
		g_ptr_array_add(warnings, g_strdup_printf(_("Baud rate %u may not be supported by all hardware"), p->baudrate));

	if(p->stopbits != 1 && p->stopbits != 2)
	{
		g_ptr_array_add(warnings, g_strdup_printf(_("Invalid number of stop-bits: %d\nFalling back to default number of stop-bits number: %d\n"),
		                                          p->stopbits, 1));
		p->stopbits = 1;
	}

	if(p->bits < 5 || p->bits > 8)
	{
		g_ptr_array_add(warnings, g_strdup_printf(_("Invalid number of bits: %d\nFalling back to default number of bits: %d\n"), p->bits, 8));
		p->bits = 8;
	}

	if(p->wait_delay_ms < 0 || p->wait_delay_ms > 500)
	{
		g_ptr_array_add(warnings, g_strdup_printf(_("Invalid delay: %d ms\nFalling back to default delay: %d ms\n"), p->wait_delay_ms, 0));
		p->wait_delay_ms = 0;
	}

	if(p->idle_timeout_s < 0 || p->idle_timeout_s > GT_PROFILE_ACTIVITY_MAX)
	{
		g_ptr_array_add(warnings, g_strdup_printf(_("Invalid idle delay: %d s\nFalling back to default idle delay: %d s\n"), p->idle_timeout_s, 10));
		p->idle_timeout_s = 10;
	}

	if(p->no_data_timeout_s < 0 || p->no_data_timeout_s > GT_PROFILE_ACTIVITY_MAX)
	{
		g_ptr_array_add(warnings, g_strdup_printf(_("Invalid no data delay: %d s\nFalling back to default no data delay: %d s\n"),
		                                          p->no_data_timeout_s, 60));
		p->no_data_timeout_s = 60;
	}

	/* Corrections silencieuses : valeurs absurdes d'un fichier modifié à la main */
	if(p->font == NULL || p->font[0] == '\0')
		gt_profile_set_string(&p->font, GT_PROFILE_DEFAULT_FONT);
	if(p->columns <= 0)
		p->columns = 80;
	if(p->rows <= 0)
		p->rows = 25;
	if(p->scrollback <= 0)
		p->scrollback = 10000;
	if(p->wait_char < -1 || p->wait_char > 255)
		p->wait_char = -1;
	if(p->port == NULL)
		gt_profile_set_string(&p->port, GT_PROFILE_DEFAULT_PORT);
	if(p->host == NULL)
		gt_profile_set_string(&p->host, "");
	if(p->tcp_port == NULL)
		gt_profile_set_string(&p->tcp_port, "");

	return warnings->len == before;
}

G_DEFINE_QUARK(gt-profile-error-quark, gt_profile_error)

/* Groupes réservés du magasin (config_store.c) : un profil de ce nom serait confondu avec eux dans les messages et les listes */
static const gchar * const reserved_names[] = { "window", "colors", "layout", "session", NULL };

gboolean gt_profile_name_validate(const gchar *name, gchar **normalized, GError **error)
{
	gchar *trimmed;

	if(normalized != NULL)
		*normalized = NULL;
	if(name == NULL)
		name = "";
	if(!g_utf8_validate(name, -1, NULL))
	{
		g_set_error_literal(error, GT_PROFILE_ERROR, GT_PROFILE_ERROR_NAME_INVALID_CHAR,
		                    _("The configuration name cannot contain \"[\", \"]\" or control characters."));
		return FALSE;
	}
	trimmed = g_strstrip(g_strdup(name));
	if(trimmed[0] == '\0')
	{
		g_set_error_literal(error, GT_PROFILE_ERROR, GT_PROFILE_ERROR_NAME_EMPTY, _("The configuration name cannot be empty."));
		g_free(trimmed);
		return FALSE;
	}
	if(g_utf8_strlen(trimmed, -1) > GT_PROFILE_NAME_MAX)
	{
		g_set_error(error, GT_PROFILE_ERROR, GT_PROFILE_ERROR_NAME_TOO_LONG, _("The configuration name is too long (%d characters at most)."),
		            GT_PROFILE_NAME_MAX);
		g_free(trimmed);
		return FALSE;
	}
	for(const gchar *c = trimmed; *c != '\0'; c = g_utf8_next_char(c))
	{
		gunichar u = g_utf8_get_char(c);

		if(u == '[' || u == ']' || g_unichar_iscntrl(u))
		{
			g_set_error_literal(error, GT_PROFILE_ERROR, GT_PROFILE_ERROR_NAME_INVALID_CHAR,
			                    _("The configuration name cannot contain \"[\", \"]\" or control characters."));
			g_free(trimmed);
			return FALSE;
		}
	}
	for(gint i = 0; reserved_names[i] != NULL; i++)
		if(g_ascii_strcasecmp(trimmed, reserved_names[i]) == 0)
		{
			g_set_error(error, GT_PROFILE_ERROR, GT_PROFILE_ERROR_NAME_RESERVED, _("\"%s\" is a reserved name."), trimmed);
			g_free(trimmed);
			return FALSE;
		}
	if(normalized != NULL)
		*normalized = trimmed;
	else
		g_free(trimmed);
	return TRUE;
}

void gt_profile_to_transport_params(const GtProfile *p, TransportParams *out)
{
	memset(out, 0, sizeof(*out));
	out->type = p->transport;
	out->port = g_strdup(p->port);
	out->baudrate = p->baudrate;
	out->bits = p->bits;
	out->stopbits = p->stopbits;
	out->parity = p->parity;
	out->flow = p->flow;
	out->rs485_before_ms = p->rs485_before_ms;
	out->rs485_after_ms = p->rs485_after_ms;
	out->lock_port = p->lock_port;
	out->host = g_strdup(p->host);
	out->tcp_port = g_strdup(p->tcp_port);
}

static gint name_index(const gchar * const *names, gint count, const gchar *s)
{
	if(s == NULL)
		return -1;
	for(gint i = 0; i < count; i++)
		if(g_ascii_strcasecmp(names[i], s) == 0)
			return i;
	return -1;
}

const gchar *gt_parity_to_string(GtParity v)
{
	return (v >= GT_PARITY_NONE && v <= GT_PARITY_EVEN) ? parity_names[v] : parity_names[0];
}

gboolean gt_parity_from_string(const gchar *s, GtParity *out)
{
	gint i = name_index(parity_names, G_N_ELEMENTS(parity_names), s);

	if(i < 0)
		return FALSE;
	*out = (GtParity)i;
	return TRUE;
}

const gchar *gt_flow_to_string(GtFlow v)
{
	return (v >= GT_FLOW_NONE && v <= GT_FLOW_RS485) ? flow_names[v] : flow_names[0];
}

gboolean gt_flow_from_string(const gchar *s, GtFlow *out)
{
	gint i = name_index(flow_names, G_N_ELEMENTS(flow_names), s);

	if(i < 0)
		return FALSE;
	*out = (GtFlow)i;
	return TRUE;
}

const gchar *gt_transport_to_string(TransportType v)
{
	return (v >= TRANSPORT_SERIAL && v <= TRANSPORT_TCP_SERVER) ? transport_names[v] : transport_names[0];
}

gboolean gt_transport_from_string(const gchar *s, TransportType *out)
{
	gint i = name_index(transport_names, G_N_ELEMENTS(transport_names), s);

	if(i < 0)
		return FALSE;
	*out = (TransportType)i;
	return TRUE;
}
