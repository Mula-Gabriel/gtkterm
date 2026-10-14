/***********************************************************************/
/* profile_keyfile.c                                                   */
/*                                                                     */
/*   Purpose                                                           */
/*      Profil <-> groupe GKeyFile (clés de [profile:...] et [tab:N])  */
/*                                                                     */
/***********************************************************************/

#include <config.h>

#include "config_file.h"
#include "config_store.h"
#include "profile_keyfile.h"

static void read_string(GKeyFile *kf, const gchar *group, const gchar *key, gchar **field)
{
	gchar *v = g_key_file_get_string(kf, group, key, NULL);

	if(v != NULL)
	{
		g_free(*field);
		*field = v;
	}
}

void gt_key_file_read_int(GKeyFile *kf, const gchar *group, const gchar *key, gint *field)
{
	gchar *v = g_key_file_get_value(kf, group, key, NULL);
	gchar *end = NULL;
	gint64 n;

	if(v == NULL)
		return;
	n = g_ascii_strtoll(g_strstrip(v), &end, 10);
	if(end != v && *end == '\0' && n >= G_MININT && n <= G_MAXINT)
		*field = (gint)n;
	g_free(v);
}

static void read_uint(GKeyFile *kf, const gchar *group, const gchar *key, guint *field)
{
	gint n = -1;

	gt_key_file_read_int(kf, group, key, &n);
	if(n > 0)
		*field = (guint)n;
}

void gt_key_file_read_bool(GKeyFile *kf, const gchar *group, const gchar *key, gboolean *field)
{
	gchar *v = g_key_file_get_value(kf, group, key, NULL);

	if(v != NULL)
		gt_config_parse_boolean(g_strstrip(v), field);
	g_free(v);
}

static void read_color(GKeyFile *kf, const gchar *group, const gchar *key, GtColor *c)
{
	gsize n = 0;
	gdouble *v = g_key_file_get_double_list(kf, group, key, &n, NULL);

	if(v != NULL && n == 4)
		*c = (GtColor){ v[0], v[1], v[2], v[3] };
	g_free(v);
}

void gt_profile_from_key_file(GKeyFile *kf, const gchar *g, GtProfile *p)
{
	gchar *s = NULL;

	read_string(kf, g, "transport", &s);
	gt_transport_from_string(s, &p->transport);
	g_clear_pointer(&s, g_free);
	read_string(kf, g, "port", &p->port);
	read_uint(kf, g, "speed", &p->baudrate);
	gt_key_file_read_int(kf, g, "bits", &p->bits);
	gt_key_file_read_int(kf, g, "stopbits", &p->stopbits);
	read_string(kf, g, "parity", &s);
	gt_parity_from_string(s, &p->parity);
	g_clear_pointer(&s, g_free);
	read_string(kf, g, "flow", &s);
	gt_flow_from_string(s, &p->flow);
	g_clear_pointer(&s, g_free);
	gt_key_file_read_int(kf, g, "wait_delay", &p->wait_delay_ms);
	if(g_key_file_has_key(kf, g, "wait_char", NULL))
	{
		gint w = -1;

		gt_key_file_read_int(kf, g, "wait_char", &w);
		p->wait_char = config_wait_char_from_file(w);
	}
	gt_key_file_read_int(kf, g, "rs485_rts_time_before_tx", &p->rs485_before_ms);
	gt_key_file_read_int(kf, g, "rs485_rts_time_after_tx", &p->rs485_after_ms);
	gt_key_file_read_int(kf, g, "activity_idle_timeout", &p->idle_timeout_s);
	gt_key_file_read_int(kf, g, "activity_no_data_timeout", &p->no_data_timeout_s);
	read_string(kf, g, "socket_host", &p->host);
	read_string(kf, g, "socket_port", &p->tcp_port);
	gt_key_file_read_bool(kf, g, "echo", &p->echo);
	gt_key_file_read_bool(kf, g, "crlfauto", &p->crlf_auto);
	gt_key_file_read_bool(kf, g, "autoreconnect_enabled", &p->auto_reconnect);
	gt_key_file_read_bool(kf, g, "esc_clear_screen", &p->esc_clear_screen);
	gt_key_file_read_bool(kf, g, "timestamp", &p->timestamp);
	read_string(kf, g, "font", &p->font);
	gt_key_file_read_bool(kf, g, "term_block_cursor", &p->block_cursor);
	gt_key_file_read_int(kf, g, "term_columns", &p->columns);
	gt_key_file_read_int(kf, g, "term_rows", &p->rows);
	gt_key_file_read_int(kf, g, "term_scrollback", &p->scrollback);
	gt_key_file_read_bool(kf, g, "term_visual_bell", &p->visual_bell);
	read_color(kf, g, "term_foreground", &p->foreground);
	read_color(kf, g, "term_background", &p->background);
	read_string(kf, g, "macros_file", &p->macros_file);
}

static void write_color(GKeyFile *kf, const gchar *group, const gchar *key, const GtColor *c)
{
	gdouble v[4] = { c->red, c->green, c->blue, c->alpha };

	g_key_file_set_double_list(kf, group, key, v, 4);
}

void gt_profile_to_key_file(const GtProfile *p, GKeyFile *kf, const gchar *g)
{
	g_key_file_set_string(kf, g, "transport", gt_transport_to_string(p->transport));
	g_key_file_set_string(kf, g, "port", p->port != NULL ? p->port : "");
	g_key_file_set_integer(kf, g, "speed", (gint)p->baudrate);
	g_key_file_set_integer(kf, g, "bits", p->bits);
	g_key_file_set_integer(kf, g, "stopbits", p->stopbits);
	g_key_file_set_string(kf, g, "parity", gt_parity_to_string(p->parity));
	g_key_file_set_string(kf, g, "flow", gt_flow_to_string(p->flow));
	g_key_file_set_integer(kf, g, "wait_delay", p->wait_delay_ms);
	g_key_file_set_integer(kf, g, "wait_char", p->wait_char);
	g_key_file_set_integer(kf, g, "rs485_rts_time_before_tx", p->rs485_before_ms);
	g_key_file_set_integer(kf, g, "rs485_rts_time_after_tx", p->rs485_after_ms);
	g_key_file_set_integer(kf, g, "activity_idle_timeout", p->idle_timeout_s);
	g_key_file_set_integer(kf, g, "activity_no_data_timeout", p->no_data_timeout_s);
	g_key_file_set_string(kf, g, "socket_host", p->host != NULL ? p->host : "");
	g_key_file_set_string(kf, g, "socket_port", p->tcp_port != NULL ? p->tcp_port : "");
	g_key_file_set_boolean(kf, g, "echo", p->echo);
	g_key_file_set_boolean(kf, g, "crlfauto", p->crlf_auto);
	g_key_file_set_boolean(kf, g, "autoreconnect_enabled", p->auto_reconnect);
	g_key_file_set_boolean(kf, g, "esc_clear_screen", p->esc_clear_screen);
	g_key_file_set_boolean(kf, g, "timestamp", p->timestamp);
	g_key_file_set_string(kf, g, "font", p->font != NULL ? p->font : GT_PROFILE_DEFAULT_FONT);
	g_key_file_set_boolean(kf, g, "term_block_cursor", p->block_cursor);
	g_key_file_set_integer(kf, g, "term_columns", p->columns);
	g_key_file_set_integer(kf, g, "term_rows", p->rows);
	g_key_file_set_integer(kf, g, "term_scrollback", p->scrollback);
	g_key_file_set_boolean(kf, g, "term_visual_bell", p->visual_bell);
	write_color(kf, g, "term_foreground", &p->foreground);
	write_color(kf, g, "term_background", &p->background);
	if(p->macros_file != NULL)
		g_key_file_set_string(kf, g, "macros_file", p->macros_file);
	else
		g_key_file_remove_key(kf, g, "macros_file", NULL);
}
