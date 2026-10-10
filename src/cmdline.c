/***********************************************************************/
/* cmdline.c                                                           */
/* ---------                                                           */
/*           GTKTerm Software                                          */
/*                      (c) Julien Schmitt                             */
/*                                                                     */
/* ------------------------------------------------------------------- */
/*                                                                     */
/*   Purpose                                                           */
/*      Reads the command line (application layer, without GTK)        */
/*                                                                     */
/*   ChangeLog                                                         */
/*      - 0.99.2 : Internationalization                                */
/*      - 0.98.3 : modified for configuration file                     */
/*      - 0.98.2 : added --echo                                        */
/*      - 0.98 : file creation by Julien                               */
/*                                                                     */
/***********************************************************************/

#include <getopt.h>
#include <string.h>

#include <config.h>
#include <glib.h>
#include <glib/gi18n.h>

#include "app_config.h"
#include "cmdline.h"
#include "i18n.h"

#define SHORT_OPTIONS ":s:a:t:b:f:p:w:d:r:heLc:x:y:T:H:P:"	/* « : » en tête : valeur manquante = « : », distincte d'une option inconnue */

static const struct option long_options[] =
{
	{"speed", 1, 0, 's'},
	{"parity", 1, 0, 'a'},
	{"stopbits", 1, 0, 't'},
	{"bits", 1, 0, 'b'},
	{"file", 1, 0, 'f'},
	{"port", 1, 0, 'p'},
	{"flow", 1, 0, 'w'},
	{"delay", 1, 0, 'd'},
	{"char", 1, 0, 'r'},
	{"help", 0, 0, 'h'},
	{"echo", 0, 0, 'e'},
	{"disable-port-lock", 0, 0, 'L'},
	{"rts_time_before", 1, 0, 'x'},
	{"rts_time_after", 1, 0, 'y'},
	{"config", 1, 0, 'c'},
	{"transport", 1, 0, 'T'},
	{"host", 1, 0, 'H'},
	{"tcp-port", 1, 0, 'P'},
	{0, 0, 0, 0}
};

G_DEFINE_QUARK(gt-cmdline-error-quark, gt_cmdline_error)

void display_help(void)
{
	i18n_printf(_("\nGTKTerm version %s\n"), VERSION);
	i18n_printf(_("\t (c) Julien Schmitt\n"));
	i18n_printf(_("\nThis program is released under the terms of the GPL V.2\n"));
	i18n_printf(_("\t ** Use at your own risks! **\n"));
	i18n_printf(_("\nCommand line options\n"));
	i18n_printf(_("--help or -h: this help screen\n"));
	i18n_printf(_("--config <configuration> or -c: load configuration\n"));
	i18n_printf(_("--port <device> or -p: serial port device (default /dev/ttyS0)\n"));
	i18n_printf(_("--speed <speed> or -s: serial port speed (default 9600)\n"));
	i18n_printf(_("--bits <bits> or -b: number of bits (default 8)\n"));
	i18n_printf(_("--stopbits <stopbits> or -t: number of stopbits (default 1)\n"));
	i18n_printf(_("--parity <none | odd | even> or -a: parity (default none)\n"));
	i18n_printf(_("--flow <none | Xon | RTS | RS485> or -w: flow control (default none)\n"));
	i18n_printf(_("--delay <ms> or -d: end of line delay in ms (default none)\n"));
	i18n_printf(_("--char <char> or -r: wait for a special char at end of line (default none)\n"));
	i18n_printf(_("--file <filename> or -f: default file to send (default none)\n"));
	i18n_printf(_("--rts_time_before <ms> or -x: for RS-485, time in ms before transmit with rts on\n"));
	i18n_printf(_("--rts_time_after <ms> or -y: for RS-485, time in ms after transmit with rts on\n"));
	i18n_printf(_("--echo or -e: switch on local echo\n"));
	i18n_printf(_("--disable-port-lock or -L: does not lock serial port. Allows to send to serial port from different terminals\n"));
	i18n_printf(_("                      Note: incoming data are displayed randomly on only one terminal\n"));
	i18n_printf(_("--transport <serial|tcp-client|tcp-server> or -T: transport type\n"));
	i18n_printf(_("--host <hostname> or -H: TCP host\n"));
	i18n_printf(_("--tcp-port <port> or -P: TCP port\n"));
	i18n_printf("\n");
}

/* Nom long d'une option (« --speed ») d'après son caractère ; à libérer */
static gchar *option_name(gint c)
{
	for(gsize i = 0; long_options[i].name != NULL; i++)
		if(long_options[i].val == c)
			return g_strdup_printf("--%s", long_options[i].name);
	return g_strdup_printf("-%c", c);
}

static gboolean invalid_value(GError **error, gint c, const gchar *value)
{
	gchar *name = option_name(c);

	g_set_error(error, GT_CMDLINE_ERROR, GT_CMDLINE_ERROR_INVALID_VALUE, _("Invalid value for option %s: \"%s\""), name, value);
	g_free(name);
	return FALSE;
}

/* Entier décimal entier (ni espace, ni reste, ni signe superflu) dans [min, max] ; remplace atoi, qui rendait 0 pour « abc » */
static gboolean parse_int(const gchar *text, gint64 min, gint64 max, gint *out)
{
	gint64 v;

	if(!g_ascii_string_to_signed(text, 10, min, max, &v, NULL))
		return FALSE;
	*out = (gint)v;
	return TRUE;
}

gboolean gt_cmdline_parse(int argc, char **argv, GtCmdlineOptions *out, GError **error)
{
	gint c;

	memset(out, 0, sizeof(*out));
	optind = 0;	/* GNU : réinitialisation complète, l'analyse peut être refaite (tests) */
	opterr = 0;	/* pas de message de getopt : les nôtres sont traduits */
	while((c = getopt_long(argc, argv, SHORT_OPTIONS, long_options, NULL)) != -1)
	{
		guint64 u;
		gchar *name;

		switch(c)
		{
		case 'h':
			/* L'aide sort tout de suite (code 0) : les options suivantes ne sont pas lues */
			out->help = TRUE;
			return TRUE;
		case 'c':
			gt_profile_set_string(&out->config_name, optarg);
			break;
		case 'f':
			gt_profile_set_string(&out->send_file, optarg);
			break;
		case 's':
			if(!g_ascii_string_to_unsigned(optarg, 10, 1, G_MAXUINT, &u, NULL))
				return invalid_value(error, c, optarg);
			out->values.baudrate = (guint)u;
			out->set |= GT_CMDLINE_BAUDRATE;
			break;
		case 'a':
			/* « none » accepté : il était ignoré (le profil gardait sa parité) */
			if(!gt_parity_from_string(optarg, &out->values.parity))
				return invalid_value(error, c, optarg);
			out->set |= GT_CMDLINE_PARITY;
			break;
		case 't':
			if(!parse_int(optarg, 1, 2, &out->values.stopbits))
				return invalid_value(error, c, optarg);
			out->set |= GT_CMDLINE_STOPBITS;
			break;
		case 'b':
			if(!parse_int(optarg, 5, 8, &out->values.bits))
				return invalid_value(error, c, optarg);
			out->set |= GT_CMDLINE_BITS;
			break;
		case 'p':
			if(optarg[0] == '\0')
				return invalid_value(error, c, optarg);
			gt_profile_set_string(&out->values.port, optarg);
			out->set |= GT_CMDLINE_PORT;
			break;
		case 'w':
			/* « none » accepté (il était ignoré) ; Xon, RTS, RS485 sans tenir compte de la casse */
			if(!gt_flow_from_string(optarg, &out->values.flow))
				return invalid_value(error, c, optarg);
			out->set |= GT_CMDLINE_FLOW;
			break;
		case 'd':
			if(!parse_int(optarg, 0, 500, &out->values.wait_delay_ms))
				return invalid_value(error, c, optarg);
			out->set |= GT_CMDLINE_DELAY;
			break;
		case 'r':
			if(strlen(optarg) != 1)
				return invalid_value(error, c, optarg);
			out->values.wait_char = (guchar)optarg[0];
			out->set |= GT_CMDLINE_WAIT_CHAR;
			break;
		case 'e':
			out->set |= GT_CMDLINE_ECHO;
			break;
		case 'L':
			out->set |= GT_CMDLINE_NO_LOCK;
			break;
		case 'x':
			if(!parse_int(optarg, 0, 500, &out->values.rs485_before_ms))
				return invalid_value(error, c, optarg);
			out->set |= GT_CMDLINE_RS485_BEFORE;
			break;
		case 'y':
			if(!parse_int(optarg, 0, 500, &out->values.rs485_after_ms))
				return invalid_value(error, c, optarg);
			out->set |= GT_CMDLINE_RS485_AFTER;
			break;
		case 'T':
			/* « serial » accepté : il était ignoré (pas de retour au série depuis un profil TCP) */
			if(!gt_transport_from_string(optarg, &out->values.transport))
				return invalid_value(error, c, optarg);
			out->set |= GT_CMDLINE_TRANSPORT;
			break;
		case 'H':
			gt_profile_set_string(&out->values.host, optarg);
			out->set |= GT_CMDLINE_HOST;
			break;
		case 'P':
			gt_profile_set_string(&out->values.tcp_port, optarg);
			out->set |= GT_CMDLINE_TCP_PORT;
			break;
		case ':':
			name = option_name(optopt);
			g_set_error(error, GT_CMDLINE_ERROR, GT_CMDLINE_ERROR_MISSING_VALUE, _("Missing value for option %s"), name);
			g_free(name);
			return FALSE;
		default:
			/* Élément fautif tel qu'écrit (« --nimporte », « -Z », « --echo=1 ») */
			g_set_error(error, GT_CMDLINE_ERROR, GT_CMDLINE_ERROR_UNKNOWN_OPTION, _("Unknown command line option: %s"),
			            optind > 0 && optind <= argc ? argv[optind - 1] : "?");
			return FALSE;
		}
	}
	return TRUE;
}

void gt_cmdline_apply(const GtCmdlineOptions *o, GtProfile *p)
{
	const GtProfile *v = &o->values;

	if(o->set & GT_CMDLINE_BAUDRATE)
		p->baudrate = v->baudrate;
	if(o->set & GT_CMDLINE_PARITY)
		p->parity = v->parity;
	if(o->set & GT_CMDLINE_STOPBITS)
		p->stopbits = v->stopbits;
	if(o->set & GT_CMDLINE_BITS)
		p->bits = v->bits;
	if(o->set & GT_CMDLINE_PORT)
		gt_profile_set_string(&p->port, v->port);
	if(o->set & GT_CMDLINE_FLOW)
		p->flow = v->flow;
	if(o->set & GT_CMDLINE_DELAY)
		p->wait_delay_ms = v->wait_delay_ms;
	if(o->set & GT_CMDLINE_WAIT_CHAR)
		p->wait_char = v->wait_char;
	if(o->set & GT_CMDLINE_ECHO)
		p->echo = TRUE;
	if(o->set & GT_CMDLINE_NO_LOCK)
		p->lock_port = FALSE;
	if(o->set & GT_CMDLINE_RS485_BEFORE)
		p->rs485_before_ms = v->rs485_before_ms;
	if(o->set & GT_CMDLINE_RS485_AFTER)
		p->rs485_after_ms = v->rs485_after_ms;
	if(o->set & GT_CMDLINE_TRANSPORT)
		p->transport = v->transport;
	if(o->set & GT_CMDLINE_HOST)
		gt_profile_set_string(&p->host, v->host);
	if(o->set & GT_CMDLINE_TCP_PORT)
		gt_profile_set_string(&p->tcp_port, v->tcp_port);
}

void gt_cmdline_options_clear(GtCmdlineOptions *o)
{
	g_clear_pointer(&o->config_name, g_free);
	g_clear_pointer(&o->send_file, g_free);
	gt_profile_clear(&o->values);
	o->set = 0;
	o->help = FALSE;
}

void read_command_line(int argc, char **argv, GtCmdline *out)
{
	GtCmdlineOptions o;
	GError *error = NULL;

	memset(out, 0, sizeof(*out));
	if(!gt_cmdline_parse(argc, argv, &o, &error))
	{
		g_printerr("%s\n", error->message);
		g_error_free(error);
		gt_cmdline_options_clear(&o);
		out->exit_now = TRUE;
		out->exit_code = 1;
		return;
	}
	if(o.help)
	{
		display_help();
		gt_cmdline_options_clear(&o);
		out->exit_now = TRUE;
		out->exit_code = 0;
		return;
	}

	/* need a working configuration file ! */
	Check_configuration_file();
	/* Le profil d'abord, les options ensuite : elles le complètent quel que soit leur ordre (--disable-port-lock survit au profil) */
	if(o.config_name != NULL)
		Load_configuration_from_file(o.config_name);
	gt_cmdline_apply(&o, &config);
	Verify_configuration();
	out->send_file = g_steal_pointer(&o.send_file);
	gt_cmdline_options_clear(&o);
}

void gt_cmdline_clear(GtCmdline *c)
{
	g_clear_pointer(&c->send_file, g_free);
	c->exit_now = FALSE;
	c->exit_code = 0;
}
