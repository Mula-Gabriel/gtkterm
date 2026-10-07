/***********************************************************************/
/* transport.c                                                          */
/* ----------                                                           */
/*           GTKTerm Software                                           */
/*                                                                      */
/*   Purpose                                                            */
/*      Transport par instance : série, client TCP, serveur TCP         */
/*      (sous-projet 2 : aucune dépendance à l'interface)               */
/*                                                                      */
/***********************************************************************/

#include <glib.h>
#include <glib-unix.h>
#include <termios.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/prctl.h>
#include <linux/capability.h>

#include <config.h>
#include <glib/gi18n.h>

#include "i18n.h"
#include "transport.h"
#include "serial_termios.h"

#define BUFFER_RECEPTION 8192
#define WATCH_PRIORITY 10

struct _GtTransport
{
	TransportParams params;		/* copie des paramètres ouverts : la fermeture suit le type ouvert (M1) */
	TransportCallbacks cb;
	gpointer user_data;
	int fd;				/* série, client TCP ou socket d'écoute du serveur ; -1 si fermé */
	int client_fd;			/* serveur : client accepté, -1 sinon */
	guint watch_id;			/* lecture (série, client) ou acceptation (serveur) */
	guint client_watch_id;		/* serveur : lecture du client */
	guint connect_watch_id;		/* client TCP : connexion en cours */
	guint lost_idle_id;		/* signalement différé de LOST */
	struct addrinfo *addrs;		/* client TCP : adresses résolues, gardées pendant la connexion (I4), NULL ensuite */
	struct addrinfo *next_addr;	/* client TCP : prochaine adresse à essayer si la connexion en cours échoue */
	gchar *lost_detail;
	gboolean lost;
	gboolean termios_saved;
	struct termios termios_save;
	gboolean locked;
	guint actual_baudrate;
	dev_t devnum;
};

G_DEFINE_QUARK(gt-transport-error-quark, gt_transport_error)

void transport_params_copy(TransportParams *dst, const TransportParams *src)
{
	*dst = *src;
	dst->port = g_strdup(src->port);
	dst->host = g_strdup(src->host);
	dst->tcp_port = g_strdup(src->tcp_port);
}

void transport_params_clear(TransportParams *p)
{
	g_clear_pointer(&p->port, g_free);
	g_clear_pointer(&p->host, g_free);
	g_clear_pointer(&p->tcp_port, g_free);
}

static void transport_remove_source(guint *id)
{
	if(*id != 0)
	{
		g_source_remove(*id);
		*id = 0;
	}
}

static void transport_stop_watches(GtTransport *t)
{
	transport_remove_source(&t->watch_id);
	transport_remove_source(&t->client_watch_id);
	transport_remove_source(&t->connect_watch_id);
}

static gboolean transport_lost_idle(gpointer data)
{
	GtTransport *t = data;
	gchar *detail = t->lost_detail;

	t->lost_idle_id = 0;
	t->lost_detail = NULL;
	/* Le destinataire peut fermer le transport dans ce rappel : t n'est plus utilisé ensuite */
	if(t->cb.on_event != NULL)
		t->cb.on_event(TRANSPORT_EVENT_LOST, detail, t->user_data);
	g_free(detail);
	return G_SOURCE_REMOVE;
}

/* Arrête les surveillances et signale LOST depuis une source d'inactivité (jamais pendant un appel au transport) */
static void transport_mark_lost(GtTransport *t, const gchar *detail)
{
	if(t->lost)
		return;
	t->lost = TRUE;
	transport_stop_watches(t);
	t->lost_detail = g_strdup(detail);
	t->lost_idle_id = g_idle_add(transport_lost_idle, t);
}

static void transport_close_client(GtTransport *t)
{
	transport_remove_source(&t->client_watch_id);
	if(t->client_fd != -1)
	{
		close(t->client_fd);
		t->client_fd = -1;
	}
}

static gboolean transport_read_cb(gint fd, GIOCondition cond, gpointer data)
{
	GtTransport *t = data;
	guint8 buf[BUFFER_RECEPTION];
	ssize_t n;
	gint err = 0;

	for(;;)
	{
		n = read(fd, buf, sizeof(buf));
		if(n > 0)
		{
			if(t->cb.on_data != NULL)
				t->cb.on_data(buf, (gsize)n, t->user_data);
			if((gsize)n == sizeof(buf))
				continue;
			return G_SOURCE_CONTINUE;
		}
		if(n < 0 && errno == EINTR)
			continue;
		if(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
		{
			if(!(cond & (G_IO_HUP | G_IO_ERR)))
				return G_SOURCE_CONTINUE;
		}
		else if(n < 0)
			err = errno;
		break;
	}

	/* Fin de flux, erreur ou raccrochage */
	if(t->params.type == TRANSPORT_TCP_SERVER)
	{
		t->client_watch_id = 0;		/* cette source se retire en renvoyant G_SOURCE_REMOVE */
		transport_close_client(t);
		if(t->cb.on_event != NULL)
			t->cb.on_event(TRANSPORT_EVENT_CLIENT_GONE, NULL, t->user_data);
		return G_SOURCE_REMOVE;
	}
	t->watch_id = 0;
	transport_mark_lost(t, err != 0 ? g_strerror(err) : _("end of stream"));
	return G_SOURCE_REMOVE;
}

static gboolean transport_open_serial(GtTransport *t, GError **error)
{
	const TransportParams *p = &t->params;
	struct termios termios_p;
	struct stat st;
	unsigned int speed_margin;
	int fd;

	fd = open(p->port, O_RDWR | O_NOCTTY | O_NDELAY);
	if(fd == -1)
	{
		gchar *err_str = strerror_utf8(errno);

		g_set_error(error, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_OPEN, _("Cannot open %s: %s\n"), p->port, err_str);
		g_free(err_str);
		return FALSE;
	}

	if(!isatty(fd))
	{
		close(fd);
		g_set_error(error, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_NOT_TTY, _("%s is not a valid serial port\n"), p->port);
		return FALSE;
	}

	if(p->lock_port)
	{
		if(flock(fd, LOCK_EX | LOCK_NB) == -1)
		{
			close(fd);
			g_set_error_literal(error, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_LOCKED,
			                    _("Cannot lock port! The serial port may currently be in use by another program.\n"));
			return FALSE;
		}
		t->locked = TRUE;
	}

	/* Réglages d'origine relevés avant tout changement, débit compris (M3) : la fermeture les restaure tels quels */
	if(tcgetattr(fd, &t->termios_save) == 0)
		t->termios_saved = TRUE;

	speed_margin = p->baudrate / (3 * (2U + p->bits + !!p->parity));
	t->actual_baudrate = set_port_baudrate(p->baudrate, fd);
	if(t->actual_baudrate < p->baudrate - speed_margin || t->actual_baudrate - speed_margin > p->baudrate)
	{
		if(t->termios_saved)
			tcsetattr(fd, TCSANOW, &t->termios_save);
		t->termios_saved = FALSE;
		close(fd);
		t->locked = FALSE;
		g_set_error(error, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_BAUDRATE, _("Unable to set baud rate %u"), p->baudrate);
		return FALSE;
	}

	if(fstat(fd, &st) == 0)
		t->devnum = st.st_rdev;

	tcgetattr(fd, &termios_p);
	serial_termios_apply(&termios_p, p->bits, p->parity, p->stopbits, p->flow);
	tcsetattr(fd, TCSANOW, &termios_p);
	tcflush(fd, TCOFLUSH);
	tcflush(fd, TCIFLUSH);

	t->fd = fd;
	t->watch_id = g_unix_fd_add_full(WATCH_PRIORITY, fd, G_IO_IN | G_IO_HUP | G_IO_ERR, transport_read_cb, t, NULL);
	return TRUE;
}

static gboolean transport_connect_cb(gint fd, GIOCondition cond, gpointer data);

/* Client TCP : essaie les adresses à partir de rp jusqu'à une connexion établie ou en cours (EINPROGRESS) ; *last_errno reçoit
 * l'erreur du dernier échec synchrone. Connexion en cours : surveillance posée, adresses suivantes retenues pour un refus. */
static gboolean transport_connect_from(GtTransport *t, struct addrinfo *rp, int *last_errno)
{
	for(; rp != NULL; rp = rp->ai_next)
	{
		int fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

		if(fd == -1)
		{
			*last_errno = errno;
			continue;
		}
		fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
		if(connect(fd, rp->ai_addr, rp->ai_addrlen) == 0 || errno == EINPROGRESS)
		{
			/* Connexion asynchrone : CONNECTED ou adresse suivante (LOST après la dernière) quand le descripteur devient
			   inscriptible (M5) */
			t->fd = fd;
			t->next_addr = rp->ai_next;
			t->connect_watch_id = g_unix_fd_add_full(WATCH_PRIORITY, fd, G_IO_OUT, transport_connect_cb, t, NULL);
			return TRUE;
		}
		*last_errno = errno;
		close(fd);
	}
	return FALSE;
}

static void transport_free_addrs(GtTransport *t)
{
	if(t->addrs != NULL)
		freeaddrinfo(t->addrs);
	t->addrs = NULL;
	t->next_addr = NULL;
}

static gboolean transport_connect_cb(gint fd, GIOCondition cond, gpointer data)
{
	GtTransport *t = data;
	int err = 0;
	socklen_t len = sizeof(err);

	t->connect_watch_id = 0;	/* cette source se retire en renvoyant G_SOURCE_REMOVE */
	if(getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0)
		err = errno;
	if(err != 0)
	{
		gchar *msg;

		/* Refus (ou autre échec) de cette adresse : la suivante, s'il en reste (localhost : ::1 puis 127.0.0.1, I4) */
		close(fd);
		t->fd = -1;
		if(transport_connect_from(t, t->next_addr, &err))
			return G_SOURCE_REMOVE;
		transport_free_addrs(t);
		msg = g_strdup_printf(_("Cannot connect to %s:%s: %s\n"), t->params.host, t->params.tcp_port, g_strerror(err));
		transport_mark_lost(t, msg);
		g_free(msg);
		return G_SOURCE_REMOVE;
	}

	transport_free_addrs(t);
	t->watch_id = g_unix_fd_add_full(WATCH_PRIORITY, fd, G_IO_IN | G_IO_HUP | G_IO_ERR, transport_read_cb, t, NULL);
	if(t->cb.on_event != NULL)
		t->cb.on_event(TRANSPORT_EVENT_CONNECTED, NULL, t->user_data);
	return G_SOURCE_REMOVE;
}

static gboolean transport_open_tcp_client(GtTransport *t, GError **error)
{
	struct addrinfo hints, *res;
	int ret, last_errno = 0;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	ret = getaddrinfo(t->params.host, t->params.tcp_port, &hints, &res);
	if(ret != 0)
	{
		g_set_error(error, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_RESOLVE, _("Cannot resolve %s: %s\n"), t->params.host, gai_strerror(ret));
		return FALSE;
	}

	t->addrs = res;
	if(!transport_connect_from(t, res, &last_errno))
	{
		transport_free_addrs(t);
		g_set_error(error, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_CONNECT, _("Cannot connect to %s:%s: %s\n"), t->params.host, t->params.tcp_port,
		            g_strerror(last_errno));
		return FALSE;
	}
	return TRUE;
}

static gboolean transport_accept_cb(gint fd, GIOCondition cond, gpointer data)
{
	GtTransport *t = data;
	int new_fd = accept(fd, NULL, NULL);

	if(new_fd < 0)
	{
		gint err = errno;

		if(err == EAGAIN || err == EWOULDBLOCK || err == EINTR)
			return G_SOURCE_CONTINUE;
		i18n_perror(_("Accept error"));
		if(err == EINVAL || err == EBADF)
		{
			t->watch_id = 0;
			return G_SOURCE_REMOVE;
		}
		return G_SOURCE_CONTINUE;
	}

	/* Un nouveau client remplace l'ancien */
	transport_close_client(t);
	fcntl(new_fd, F_SETFL, fcntl(new_fd, F_GETFL, 0) | O_NONBLOCK);
	t->client_fd = new_fd;
	t->client_watch_id = g_unix_fd_add_full(WATCH_PRIORITY, new_fd, G_IO_IN | G_IO_HUP | G_IO_ERR, transport_read_cb, t, NULL);
	if(t->cb.on_event != NULL)
		t->cb.on_event(TRANSPORT_EVENT_CLIENT_CONNECTED, NULL, t->user_data);
	return G_SOURCE_CONTINUE;
}

static gboolean transport_open_tcp_server(GtTransport *t, GError **error)
{
	struct addrinfo hints, *res;
	const gchar *host = (t->params.host != NULL && t->params.host[0] != '\0') ? t->params.host : NULL;
	int fd, ret, optval = 1;
	int port_num = t->params.tcp_port != NULL ? atoi(t->params.tcp_port) : 0;
	gboolean privileged = (port_num > 0 && port_num < 1024);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	ret = getaddrinfo(host, t->params.tcp_port, &hints, &res);
	if(ret != 0)
	{
		if(host == NULL)
			g_set_error(error, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_RESOLVE, _("Cannot resolve port %s: %s\n"), t->params.tcp_port, gai_strerror(ret));
		else
			g_set_error(error, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_RESOLVE, _("Cannot resolve %s:%s: %s\n"), host, t->params.tcp_port,
			            gai_strerror(ret));
		return FALSE;
	}

	fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if(fd == -1)
	{
		g_set_error(error, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_SOCKET, _("Cannot create socket: %s\n"), g_strerror(errno));
		freeaddrinfo(res);
		return FALSE;
	}

	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

	if(bind(fd, res->ai_addr, res->ai_addrlen) < 0)
	{
		gint bind_errno = errno;

		if(bind_errno == EACCES && privileged)
		{
			prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_NET_BIND_SERVICE, 0, 0);
			if(bind(fd, res->ai_addr, res->ai_addrlen) == 0)
				goto bound;
			bind_errno = errno;
		}
		g_set_error(error, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_BIND, _("Cannot bind to %s:%s: %s%s\n"), t->params.host ? t->params.host : "",
		            t->params.tcp_port, g_strerror(bind_errno),
		            (bind_errno == EACCES && privileged) ? _("\nUse: sudo setcap cap_net_bind_service=+ep gtkterm") : "");
		close(fd);
		freeaddrinfo(res);
		return FALSE;
	}
bound:
	freeaddrinfo(res);

	if(listen(fd, 1) < 0)
	{
		g_set_error(error, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_LISTEN, _("Cannot listen: %s\n"), g_strerror(errno));
		close(fd);
		return FALSE;
	}

	t->fd = fd;
	t->watch_id = g_unix_fd_add_full(WATCH_PRIORITY, fd, G_IO_IN, transport_accept_cb, t, NULL);
	return TRUE;
}

GtTransport *gt_transport_open(const TransportParams *params, const TransportCallbacks *cb, gpointer user_data, GError **error)
{
	GtTransport *t;
	gboolean ok;

	g_return_val_if_fail(params != NULL, NULL);

	t = g_new0(GtTransport, 1);
	transport_params_copy(&t->params, params);
	if(cb != NULL)
		t->cb = *cb;
	t->user_data = user_data;
	t->fd = -1;
	t->client_fd = -1;

	switch(params->type)
	{
	case TRANSPORT_SERIAL:
		ok = transport_open_serial(t, error);
		break;
	case TRANSPORT_TCP_CLIENT:
		ok = transport_open_tcp_client(t, error);
		break;
	case TRANSPORT_TCP_SERVER:
		ok = transport_open_tcp_server(t, error);
		break;
	default:
		g_set_error_literal(error, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_OPEN, _("No open port"));
		ok = FALSE;
	}

	if(!ok)
	{
		transport_params_clear(&t->params);
		g_free(t);
		return NULL;
	}
	return t;
}

void gt_transport_close(GtTransport *t)
{
	if(t == NULL)
		return;

	transport_remove_source(&t->lost_idle_id);
	transport_stop_watches(t);
	transport_close_client(t);

	if(t->fd != -1)
	{
		if(t->params.type == TRANSPORT_SERIAL)
		{
			if(t->termios_saved)
			{
				tcsetattr(t->fd, TCSANOW, &t->termios_save);
				tcflush(t->fd, TCOFLUSH);
				tcflush(t->fd, TCIFLUSH);
			}
			if(t->locked)
				flock(t->fd, LOCK_UN);
		}
		close(t->fd);
	}

	transport_free_addrs(t);
	g_free(t->lost_detail);
	transport_params_clear(&t->params);
	g_free(t);
}

gssize gt_transport_send(GtTransport *t, const guint8 *data, gsize len)
{
	gboolean rs485;
	gssize written;
	gint saved_errno;
	int fd;

	if(t == NULL || len == 0)
		return 0;
	if(t->lost)
	{
		errno = EIO;
		return -1;
	}

	fd = (t->params.type == TRANSPORT_TCP_SERVER) ? t->client_fd : t->fd;
	if(fd == -1 || t->connect_watch_id != 0)
		return 0;

	rs485 = (t->params.type == TRANSPORT_SERIAL && t->params.flow == GT_FLOW_RS485);
	if(rs485)
	{
		ioctl(fd, TIOCMBIS, (int[]){TIOCM_RTS});
		if(t->params.rs485_before_ms > 0)
			usleep(t->params.rs485_before_ms * 1000);
	}

	/* En TCP, MSG_NOSIGNAL : un pair déjà fermé renvoie EPIPE au lieu de tuer GTKTerm par SIGPIPE (C2). */
	if(t->params.type == TRANSPORT_SERIAL)
		written = write(fd, data, len);
	else
		written = send(fd, data, len, MSG_NOSIGNAL);
	saved_errno = errno;

	if(rs485)
	{
		tcdrain(fd);
		if(t->params.rs485_after_ms > 0)
			usleep(t->params.rs485_after_ms * 1000);
		ioctl(fd, TIOCMBIC, (int[]){TIOCM_RTS});
	}

	errno = saved_errno;
	return written;
}

gint gt_transport_get_modem_lines(GtTransport *t)
{
	int stat;

	if(t == NULL || t->params.type != TRANSPORT_SERIAL || t->fd == -1 || t->lost)
		return -1;

	if(ioctl(t->fd, TIOCMGET, &stat) == -1)
	{
		/* Les pseudo-terminaux et certains périphériques n'ont pas de lignes modem (ENOTTY/EINVAL) : ce n'est pas une
		   perte de liaison. Toute autre erreur (EIO...) signifie que le périphérique a disparu. */
		if(errno != EINVAL && errno != ENOTTY)
			transport_mark_lost(t, g_strerror(errno));
		return -1;
	}
	return stat;
}

void gt_transport_toggle_line(GtTransport *t, GtModemLine line)
{
	int stat;
	int bit = (line == GT_MODEM_DTR) ? TIOCM_DTR : TIOCM_RTS;

	if(t == NULL || t->params.type != TRANSPORT_SERIAL || t->fd == -1 || t->lost)
		return;

	if(ioctl(t->fd, TIOCMGET, &stat) == -1)
	{
		i18n_perror(_("Control signals read set signals"));
		return;
	}
	stat ^= bit;
	if(ioctl(t->fd, TIOCMSET, &stat) == -1)
		i18n_perror(line == GT_MODEM_DTR ? _("DTR write") : _("RTS write"));
}

void gt_transport_send_break(GtTransport *t)
{
	if(t == NULL || t->params.type != TRANSPORT_SERIAL || t->fd == -1 || t->lost)
		return;
	tcsendbreak(t->fd, 0);
}

guint gt_transport_get_output_pending(GtTransport *t)
{
	int pending = 0;

	if(t == NULL || t->params.type != TRANSPORT_SERIAL || t->fd == -1 || t->lost)
		return 0;
	if(ioctl(t->fd, TIOCOUTQ, &pending) == -1 || pending < 0)
		return 0;
	return (guint)pending;
}

guint gt_transport_get_actual_baudrate(GtTransport *t)
{
	if(t == NULL || t->params.type != TRANSPORT_SERIAL)
		return 0;
	return t->actual_baudrate;
}

gint gt_transport_get_fd(GtTransport *t)
{
	if(t == NULL)
		return -1;
	if(t->params.type == TRANSPORT_TCP_SERVER)
		return t->client_fd;
	return t->fd;
}

dev_t gt_transport_get_device_number(GtTransport *t)
{
	if(t == NULL)
		return 0;
	return t->devnum;
}
