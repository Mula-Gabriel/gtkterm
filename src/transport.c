/***********************************************************************/
/* transport.c                                                          */
/* ----------                                                           */
/*           GTKTerm Software                                           */
/*                                                                      */
/*   Purpose                                                            */
/*      Transport abstraction: serial, TCP client, TCP server           */
/*                                                                      */
/***********************************************************************/

#include <gtk/gtk.h>
#include <glib.h>
#include <termios.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/prctl.h>
#include <linux/capability.h>

#include "term_config.h"
#include "serial.h"
#include "interface.h"
#include "files.h"
#include "buffer.h"
#include "i18n.h"
#include "transport.h"

#include <config.h>
#include <glib/gi18n.h>

#ifdef HAVE_LINUX_SERIAL_H
#include <linux/serial.h>
#endif

static int transport_fd = -1;
static unsigned int serial_port_speed;
static struct termios termios_save;

static guint callback_handler_in, callback_handler_err;
static gboolean callback_activated = FALSE;

static int listen_fd = -1;
static int client_fd = -1;
static guint listen_handler_id = 0;
static guint client_handler_in = 0;
static guint client_handler_err = 0;

static guint reconnect_timer_id = 0;
static guint connect_watch_id = 0;
static gboolean reconnect_in_progress = FALSE;

static gboolean reconnect_timer_cb(gpointer data);
static void transport_close_client(void);

extern struct configuration_port config;
extern gboolean waiting_for_char;

static void transport_schedule_reconnect(void)
{
	if(!config.autoreconnect_enabled)
		return;
	if(config.transport_type == TRANSPORT_TCP_SERVER)
		return;
	if(reconnect_timer_id != 0)
		return;

	reconnect_in_progress = TRUE;
	reconnect_timer_id = g_timeout_add(2000, reconnect_timer_cb, NULL);
}

static gboolean transport_read_cb(GIOChannel *src, GIOCondition cond, gpointer data)
{
	gint bytes_read;
	static gchar c[BUFFER_RECEPTION];
	guint i;

	int fd = (config.transport_type == TRANSPORT_TCP_SERVER) ? client_fd : transport_fd;

	bytes_read = BUFFER_RECEPTION;

	while(bytes_read == BUFFER_RECEPTION)
	{
		bytes_read = read(fd, c, BUFFER_RECEPTION);
		if(bytes_read > 0)
		{
			put_chars(c, bytes_read, config.crlfauto);

			if(config.car != -1 && waiting_for_char == TRUE)
			{
				i = 0;
				while(i < (guint)bytes_read)
				{
					if(c[i] == config.car)
					{
						waiting_for_char = FALSE;
						add_input();
						i = (guint)bytes_read;
					}
					i++;
				}
			}
		}
		else if(bytes_read == -1)
		{
			if(errno != EAGAIN)
				perror(config.port);
		}
		else
		{
			if(config.transport_type == TRANSPORT_TCP_SERVER)
				transport_close_client();
			else if(config.transport_type != TRANSPORT_SERIAL)
			{
				transport_close();
				transport_schedule_reconnect();
			}
			break;
		}
	}

	return TRUE;
}

static gboolean transport_err_cb(GIOChannel *src, GIOCondition cond, gpointer data)
{
	if(config.transport_type == TRANSPORT_TCP_SERVER)
		transport_close_client();
	else
	{
		transport_close();
		transport_schedule_reconnect();
	}
	return TRUE;
}

static void transport_setup_watches(int fd)
{
	GIOChannel *channel = g_io_channel_unix_new(fd);
	g_io_channel_set_close_on_unref(channel, FALSE);

	callback_handler_in = g_io_add_watch_full(channel,
	                      10,
	                      G_IO_IN | G_IO_HUP,
	                      (GIOFunc)transport_read_cb,
	                      NULL, NULL);

	callback_handler_err = g_io_add_watch_full(channel,
	                       10,
	                       G_IO_ERR,
	                       (GIOFunc)transport_err_cb,
	                       NULL, NULL);

	g_io_channel_unref(channel);

	callback_activated = TRUE;
}

static void transport_setup_client_watches(int fd)
{
	GIOChannel *channel = g_io_channel_unix_new(fd);
	g_io_channel_set_close_on_unref(channel, FALSE);

	client_handler_in = g_io_add_watch_full(channel,
	                      10,
	                      G_IO_IN | G_IO_HUP,
	                      (GIOFunc)transport_read_cb,
	                      NULL, NULL);

	client_handler_err = g_io_add_watch_full(channel,
	                       10,
	                       G_IO_ERR,
	                       (GIOFunc)transport_err_cb,
	                       NULL, NULL);

	g_io_channel_unref(channel);
}

static void transport_teardown_client_watches(void)
{
	if(client_handler_in != 0)
	{
		g_source_remove(client_handler_in);
		client_handler_in = 0;
	}
	if(client_handler_err != 0)
	{
		g_source_remove(client_handler_err);
		client_handler_err = 0;
	}
}

static void transport_teardown_watches(void)
{
	if(callback_activated == TRUE)
	{
		g_source_remove(callback_handler_in);
		g_source_remove(callback_handler_err);
		callback_activated = FALSE;
	}
}

static void transport_close_client(void)
{
	if(client_fd == -1)
		return;

	transport_teardown_client_watches();
	close(client_fd);
	client_fd = -1;

	if(config.transport_type == TRANSPORT_TCP_SERVER)
	{
		gchar *status = g_strdup_printf("tcp://%s:%s (listening)",
		                                config.socket_host[0] ? config.socket_host : "*",
		                                config.socket_port);
		Set_status_message(status);
		Set_window_title(status);
		g_free(status);
	}
}

static gboolean transport_open_serial(void)
{
	struct termios termios_p;
	gchar *msg = NULL;
	unsigned int speed_margin;
	int fd;

	fd = open(config.port, O_RDWR | O_NOCTTY | O_NDELAY);

	if(fd == -1)
	{
		gchar *err_str = strerror_utf8(errno);
		msg = g_strdup_printf(_("Cannot open %s: %s\n"), config.port, err_str);
		g_free(err_str);
		show_message(msg, MSG_ERR);
		g_free(msg);
		return FALSE;
	}

	if (!isatty(fd))
	{
		close(fd);
		msg = g_strdup_printf(_("%s is not a valid serial port\n"),
				      config.port);
		show_message(msg, MSG_ERR);
		g_free(msg);
		return FALSE;
	}

	if(! config.disable_port_lock)
	{
	    if(flock(fd, LOCK_EX | LOCK_NB) == -1)
	    {
		close(fd);
		msg = g_strdup_printf(_("Cannot lock port! The serial port may currently be in use by another program.\n"));
		show_message(msg, MSG_ERR);
		g_free(msg);
		return FALSE;
		}
	}

	speed_margin = config.vitesse/(3*(2U+config.bits+!!config.parite));
	serial_port_speed = set_port_baudrate(config.vitesse, fd);

	if (serial_port_speed < config.vitesse - speed_margin ||
	    serial_port_speed - speed_margin > config.vitesse)
	{
		close(fd);
		msg = g_strdup_printf(_("Unable to set baud rate %u"),
					config.vitesse);
		show_message(msg, MSG_ERR);
		g_free(msg);
		return FALSE;
	}

	tcgetattr(fd, &termios_p);
	memcpy(&termios_save, &termios_p, sizeof(struct termios));

	switch(config.bits)
	{
	case 5:
		termios_p.c_cflag |= CS5;
		break;
	case 6:
		termios_p.c_cflag |= CS6;
		break;
	case 7:
		termios_p.c_cflag |= CS7;
		break;
	case 8:
		termios_p.c_cflag |= CS8;
		break;
	}
	switch(config.parite)
	{
	case 1:
		termios_p.c_cflag |= PARODD | PARENB;
		break;
	case 2:
		termios_p.c_cflag |= PARENB;
		break;
	default:
		break;
	}
	if(config.stops == 2)
		termios_p.c_cflag |= CSTOPB;
	termios_p.c_cflag |= CREAD;
	termios_p.c_iflag = IGNPAR | IGNBRK;
	switch(config.flux)
	{
	case 1:
		termios_p.c_iflag |= IXON | IXOFF;
		break;
	case 2:
#ifdef CRTSCTS
		termios_p.c_cflag |= CRTSCTS;
#endif
#ifdef CRTS_IFLOW
		termios_p.c_cflag |= CRTS_IFLOW;
#endif
#ifdef CCTS_OFLOW
		termios_p.c_cflag |= CCTS_OFLOW;
#endif
		break;
	default:
		termios_p.c_cflag |= CLOCAL;
		break;
	}
	termios_p.c_oflag = 0;
	termios_p.c_lflag = 0;
	termios_p.c_cc[VTIME] = 0;
	termios_p.c_cc[VMIN] = 1;
	tcsetattr(fd, TCSANOW, &termios_p);
	tcflush(fd, TCOFLUSH);
	tcflush(fd, TCIFLUSH);

	transport_fd = fd;
	transport_setup_watches(transport_fd);

	Set_local_echo(config.echo);

	return TRUE;
}

static gboolean tcp_connect_complete_cb(GIOChannel *src, GIOCondition cond, gpointer data)
{
	(void)cond;

	g_source_remove(connect_watch_id);
	connect_watch_id = 0;

	if(callback_activated)
		transport_teardown_watches();

	int err = 0;
	socklen_t len = sizeof(err);
	if(getsockopt(transport_fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0)
	{
		close(transport_fd);
		transport_fd = -1;

		if(!reconnect_in_progress)
		{
			gchar *msg = g_strdup_printf(_("Cannot connect to %s:%s: %s\n"),
			                              config.socket_host, config.socket_port,
			                              err ? strerror(err) : strerror(errno));
			show_message(msg, MSG_ERR);
			g_free(msg);
		}

		transport_schedule_reconnect();
		return FALSE;
	}

	reconnect_in_progress = FALSE;
	transport_setup_watches(transport_fd);

	Set_local_echo(config.echo);

	gchar *msg = transport_get_status_string();
	Set_status_message(msg);
	Set_window_title(msg);
	g_free(msg);

	return FALSE;
}

static gboolean transport_open_tcp_client(void)
{
	struct addrinfo hints, *res, *rp;
	int fd = -1;
	gchar *msg;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	int ret = getaddrinfo(config.socket_host, config.socket_port, &hints, &res);
	if(ret != 0)
	{
		if(!reconnect_in_progress)
		{
			msg = g_strdup_printf(_("Cannot resolve %s: %s\n"),
			                      config.socket_host, gai_strerror(ret));
			show_message(msg, MSG_ERR);
			g_free(msg);
		}
		transport_schedule_reconnect();
		return FALSE;
	}

	for(rp = res; rp != NULL; rp = rp->ai_next)
	{
		fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if(fd == -1)
			continue;

		int flags = fcntl(fd, F_GETFL, 0);
		fcntl(fd, F_SETFL, flags | O_NONBLOCK);

		if(connect(fd, rp->ai_addr, rp->ai_addrlen) == 0)
		{
			break;
		}
		else if(errno == EINPROGRESS)
		{
			break;
		}
		close(fd);
		fd = -1;
	}

	freeaddrinfo(res);

	if(fd == -1)
	{
		if(!reconnect_in_progress)
		{
			msg = g_strdup_printf(_("Cannot connect to %s:%s: %s\n"),
			                      config.socket_host, config.socket_port,
			                      strerror(errno));
			show_message(msg, MSG_ERR);
			g_free(msg);
		}
		transport_schedule_reconnect();
		return FALSE;
	}

	transport_fd = fd;

	GIOChannel *channel = g_io_channel_unix_new(fd);
	g_io_channel_set_close_on_unref(channel, FALSE);
	connect_watch_id = g_io_add_watch_full(channel, 10, G_IO_OUT,
	                   (GIOFunc)tcp_connect_complete_cb, NULL, NULL);
	g_io_channel_unref(channel);

	return TRUE;
}

static gboolean tcp_accept_cb(GIOChannel *src, GIOCondition cond, gpointer data)
{
	(void)cond;

	int optval;
	socklen_t optlen = sizeof(optval);
	if(getsockopt(listen_fd, SOL_SOCKET, SO_ACCEPTCONN, &optval, &optlen) < 0 || !optval)
	{
		g_source_remove(listen_handler_id);
		listen_handler_id = 0;
		return FALSE;
	}

	int new_fd = accept(listen_fd, NULL, NULL);
	if(new_fd < 0)
	{
		if(errno != EAGAIN && errno != EWOULDBLOCK)
		{
			gchar *msg = g_strdup_printf(_("Accept error: %s\n"),
			                              strerror(errno));
			show_message(msg, MSG_ERR);
			g_free(msg);

			if(errno == EINVAL || errno == EBADF)
			{
				g_source_remove(listen_handler_id);
				listen_handler_id = 0;
				close(listen_fd);
				listen_fd = -1;
				return FALSE;
			}
		}
		return TRUE;
	}

	if(client_fd != -1)
	{
		transport_teardown_client_watches();
		close(client_fd);
		client_fd = -1;
	}

	int flags = fcntl(new_fd, F_GETFL, 0);
	fcntl(new_fd, F_SETFL, flags | O_NONBLOCK);

	client_fd = new_fd;
	transport_setup_client_watches(client_fd);

	gchar *msg = transport_get_status_string();
	Set_status_message(msg);
	Set_window_title(msg);
	g_free(msg);

	return TRUE;
}

static gboolean transport_open_tcp_server(void)
{
	struct addrinfo hints, *res;
	gchar *msg;
	int fd, optval = 1;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	if(config.socket_host[0] == '\0')
	{
		int ret = getaddrinfo(NULL, config.socket_port, &hints, &res);
		if(ret != 0)
		{
			msg = g_strdup_printf(_("Cannot resolve port %s: %s\n"),
			                      config.socket_port, gai_strerror(ret));
			show_message(msg, MSG_ERR);
			g_free(msg);
			return FALSE;
		}
	}
	else
	{
		int ret = getaddrinfo(config.socket_host, config.socket_port, &hints, &res);
		if(ret != 0)
		{
			msg = g_strdup_printf(_("Cannot resolve %s:%s: %s\n"),
			                      config.socket_host, config.socket_port,
			                      gai_strerror(ret));
			show_message(msg, MSG_ERR);
			g_free(msg);
			return FALSE;
		}
	}

	fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if(fd == -1)
	{
		msg = g_strdup_printf(_("Cannot create socket: %s\n"), strerror(errno));
		show_message(msg, MSG_ERR);
		g_free(msg);
		freeaddrinfo(res);
		return FALSE;
	}

	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

	int flags = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);

	if(bind(fd, res->ai_addr, res->ai_addrlen) < 0)
	{
		if(errno == EACCES && atoi(config.socket_port) > 0 && atoi(config.socket_port) < 1024)
		{
			prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE,
			      CAP_NET_BIND_SERVICE, 0, 0);
			if(bind(fd, res->ai_addr, res->ai_addrlen) == 0)
				goto bound;
		}

		gchar *extra = "";
		if(errno == EACCES && atoi(config.socket_port) > 0 && atoi(config.socket_port) < 1024)
			extra = _("\nUse: sudo setcap cap_net_bind_service=+ep gtkterm");
		msg = g_strdup_printf(_("Cannot bind to %s:%s: %s%s\n"),
		                      config.socket_host, config.socket_port,
		                      strerror(errno), extra);
		show_message(msg, MSG_ERR);
		g_free(msg);
		close(fd);
		freeaddrinfo(res);
		return FALSE;
	}
bound:

	freeaddrinfo(res);

	if(listen(fd, 1) < 0)
	{
		msg = g_strdup_printf(_("Cannot listen: %s\n"), strerror(errno));
		show_message(msg, MSG_ERR);
		g_free(msg);
		close(fd);
		return FALSE;
	}

	listen_fd = fd;
	transport_fd = fd;

	GIOChannel *channel = g_io_channel_unix_new(listen_fd);
	g_io_channel_set_close_on_unref(channel, FALSE);
	listen_handler_id = g_io_add_watch_full(channel, 10, G_IO_IN,
	                       (GIOFunc)tcp_accept_cb, NULL, NULL);
	g_io_channel_unref(channel);

	gchar *status = g_strdup_printf("tcp://%s:%s (listening)",
	                                config.socket_host[0] ? config.socket_host : "*",
	                                config.socket_port);
	Set_status_message(status);
	Set_window_title(status);
	g_free(status);

	return TRUE;
}

static gboolean reconnect_timer_cb(gpointer data)
{
	(void)data;
	reconnect_timer_id = 0;
	reconnect_in_progress = TRUE;
	transport_open();
	if(transport_fd == -1 && config.autoreconnect_enabled
	   && config.transport_type != TRANSPORT_TCP_SERVER)
		transport_schedule_reconnect();
	return FALSE;
}

int transport_open(void)
{
	transport_close();

	switch(config.transport_type)
	{
	case TRANSPORT_SERIAL:
		return transport_open_serial();
	case TRANSPORT_TCP_CLIENT:
		return transport_open_tcp_client();
	case TRANSPORT_TCP_SERVER:
		return transport_open_tcp_server();
	default:
		return FALSE;
	}
}

void transport_close(void)
{
	if(reconnect_timer_id != 0)
	{
		g_source_remove(reconnect_timer_id);
		reconnect_timer_id = 0;
	}

	if(connect_watch_id != 0)
	{
		g_source_remove(connect_watch_id);
		connect_watch_id = 0;
	}

	transport_close_client();

	if(listen_fd != -1)
	{
		if(listen_handler_id != 0)
		{
			g_source_remove(listen_handler_id);
			listen_handler_id = 0;
		}
		close(listen_fd);
		listen_fd = -1;
	}

	if(transport_fd == -1)
		return;

	transport_teardown_watches();

	if(config.transport_type == TRANSPORT_SERIAL)
	{
		tcsetattr(transport_fd, TCSANOW, &termios_save);
		tcflush(transport_fd, TCOFLUSH);
		tcflush(transport_fd, TCIFLUSH);
		if(! config.disable_port_lock)
		{
			flock(transport_fd, LOCK_UN);
		}
	}

	close(transport_fd);
	transport_fd = -1;
}

int transport_send(const char *buf, int len)
{
	int bytes_written = 0;
	int fd;

	if(config.transport_type == TRANSPORT_TCP_SERVER)
	{
		if(client_fd == -1)
			return 0;
		fd = client_fd;
	}
	else
	{
		if(transport_fd == -1)
			return 0;
		fd = transport_fd;
	}

	if(len == 0)
		return 0;

	if(config.transport_type == TRANSPORT_SERIAL && config.flux == 3)
	{
		ioctl(fd, TIOCMBIS, (int[]){TIOCM_RTS});
		if(config.rs485_rts_time_before_transmit > 0)
			usleep(config.rs485_rts_time_before_transmit * 1000);
	}

	bytes_written = write(fd, buf, len);

	if(config.transport_type == TRANSPORT_SERIAL && config.flux == 3)
	{
		tcdrain(fd);
		if(config.rs485_rts_time_after_transmit > 0)
			usleep(config.rs485_rts_time_after_transmit * 1000);
		ioctl(fd, TIOCMBIC, (int[]){TIOCM_RTS});
	}

	return bytes_written;
}

int transport_get_fd(void)
{
	if(config.transport_type == TRANSPORT_TCP_SERVER && client_fd != -1)
		return client_fd;
	return transport_fd;
}

void transport_send_break(void)
{
	if(config.transport_type != TRANSPORT_SERIAL)
		return;

	if(transport_fd == -1)
		return;

	tcsendbreak(transport_fd, 0);
}

int transport_get_signals(void)
{
	static int stat = 0;
	int stat_read;

	if(config.transport_type != TRANSPORT_SERIAL)
		return 0;

	if(config.flux == 3)
	{
		transport_set_signal(1);
	}

	if(transport_fd != -1)
	{
		if(ioctl(transport_fd, TIOCMGET, &stat_read) == -1)
		{
			/* Les pseudo-terminaux (/dev/pts/N, ex: simulateur W32) et certains
			   peripheriques n'ont pas de lignes modem : TIOCMGET echoue avec
			   ENOTTY/EINVAL. Ce n'est pas une erreur de liaison, on ne ferme
			   surtout pas le port (sinon le timer de lecture des signaux le
			   refermait aussitot apres l'ouverture -> ni envoi ni echo). */
			if(errno != EINVAL && errno != ENOTTY)
			{
				i18n_perror(_("Control signals read lis_sig"));
				transport_close();
			}
			return -2;
		}

		if(stat_read == stat)
			return -1;

		stat = stat_read;
		return stat;
	}
	return -1;
}

void transport_set_signal(int signal)
{
	int stat_;

	if(config.transport_type != TRANSPORT_SERIAL)
		return;

	if(transport_fd == -1)
		return;

	if(ioctl(transport_fd, TIOCMGET, &stat_) == -1)
	{
		i18n_perror(_("Control signals read set signals"));
		return;
	}

	if(signal == 0)
	{
		if(stat_ & TIOCM_DTR)
			stat_ &= ~TIOCM_DTR;
		else
			stat_ |= TIOCM_DTR;
		if(ioctl(transport_fd, TIOCMSET, &stat_) == -1)
			i18n_perror(_("DTR write"));
	}
	else if(signal == 1)
	{
		if(stat_ & TIOCM_RTS)
			stat_ &= ~TIOCM_RTS;
		else
			stat_ |= TIOCM_RTS;
		if(ioctl(transport_fd, TIOCMSET, &stat_) == -1)
			i18n_perror(_("RTS write"));
	}
}

gchar *transport_get_status_string(void)
{

	if(config.transport_type == TRANSPORT_SERIAL)
	{
		gchar parity;
		if(transport_fd == -1)
		{
			return g_strdup(_("No open port"));
		}

		switch(config.parite)
		{
		case 0: parity = 'N'; break;
		case 1: parity = 'O'; break;
		case 2: parity = 'E'; break;
		default: parity = 'N'; break;
		}

		return g_strdup_printf("%.15s  %u-%d-%c-%d",
		                       config.port,
		                       serial_port_speed,
		                       config.bits,
		                       parity,
		                       config.stops);
	}
	else if(config.transport_type == TRANSPORT_TCP_CLIENT)
	{
		if(transport_fd == -1)
			return g_strdup(_("No open port"));

		return g_strdup_printf("tcp://%s:%s",
		                       config.socket_host,
		                       config.socket_port);
	}
	else if(config.transport_type == TRANSPORT_TCP_SERVER)
	{
		return g_strdup_printf("tcp://%s:%s (server)%s",
		                       config.socket_host[0] ? config.socket_host : "*",
		                       config.socket_port,
		                       client_fd != -1 ? "" : " (no client)");
	}

	return g_strdup(_("No open port"));
}
