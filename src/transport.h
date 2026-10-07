/* Transport par instance : série, client TCP, serveur TCP (sous-projet 2).
 * Aucune dépendance à l'interface : paramètres en entrée, fonctions de rappel en sortie, erreurs d'ouverture par GError.
 *
 * Contrat des rappels (boucle GLib par défaut) :
 * - on_data, CONNECTED, CLIENT_CONNECTED, CLIENT_GONE sont appelés depuis la surveillance du descripteur : le destinataire
 *   ne ferme pas le transport dans ces rappels ;
 * - LOST est signalé depuis une source d'inactivité, jamais pendant un appel au transport : le destinataire peut fermer le
 *   transport dans ce rappel. Après la perte, l'envoi échoue (-1, errno = EIO) jusqu'à la fermeture. */
#ifndef TRANSPORT_H_
#define TRANSPORT_H_

#include <glib.h>
#include <sys/types.h>
#ifndef NO_TERMIOS	/* baud.c inclut <linux/termios.h>, incompatible avec <termios.h> */
#include <termios.h>
#endif

typedef enum
{
	TRANSPORT_SERIAL,
	TRANSPORT_TCP_CLIENT,
	TRANSPORT_TCP_SERVER
} TransportType;

/* Valeurs en mémoire seulement : .gtktermrc stocke des chaînes (parité : none/odd/even ; flux : none/xon/rts/rs485) */
typedef enum
{
	GT_PARITY_NONE,
	GT_PARITY_ODD,
	GT_PARITY_EVEN
} GtParity;

typedef enum
{
	GT_FLOW_NONE,
	GT_FLOW_XONXOFF,
	GT_FLOW_RTSCTS,
	GT_FLOW_RS485
} GtFlow;

typedef enum
{
	GT_MODEM_DTR,
	GT_MODEM_RTS
} GtModemLine;

typedef struct
{
	TransportType type;
	gchar *port;			/* série */
	guint baudrate;
	gint bits;			/* 5..8 */
	gint stopbits;			/* 1, 2 */
	GtParity parity;
	GtFlow flow;
	gint rs485_before_ms, rs485_after_ms;
	gboolean lock_port;
	gchar *host;			/* TCP ; vide ou NULL en serveur : toutes les interfaces */
	gchar *tcp_port;
} TransportParams;

/* Copie profonde ; à libérer avec transport_params_clear */
void transport_params_copy(TransportParams *dst, const TransportParams *src);
void transport_params_clear(TransportParams *p);

typedef enum
{
	TRANSPORT_EVENT_CONNECTED,		/* client TCP : connexion établie (asynchrone) */
	TRANSPORT_EVENT_CLIENT_CONNECTED,	/* serveur TCP : un client est accepté */
	TRANSPORT_EVENT_CLIENT_GONE,		/* serveur TCP : le client est parti, on écoute à nouveau */
	TRANSPORT_EVENT_LOST			/* série ou client TCP : liaison perdue ou connexion échouée (detail = raison) */
} TransportEvent;

typedef struct
{
	void (*on_data)(const guint8 *data, gsize len, gpointer user_data);
	void (*on_event)(TransportEvent ev, const gchar *detail, gpointer user_data);
} TransportCallbacks;

#define GT_TRANSPORT_ERROR (gt_transport_error_quark())

typedef enum
{
	GT_TRANSPORT_ERROR_OPEN,
	GT_TRANSPORT_ERROR_NOT_TTY,
	GT_TRANSPORT_ERROR_LOCKED,
	GT_TRANSPORT_ERROR_BAUDRATE,
	GT_TRANSPORT_ERROR_RESOLVE,
	GT_TRANSPORT_ERROR_SOCKET,
	GT_TRANSPORT_ERROR_BIND,
	GT_TRANSPORT_ERROR_LISTEN,
	GT_TRANSPORT_ERROR_CONNECT
} GtTransportError;

GQuark gt_transport_error_quark(void);

typedef struct _GtTransport GtTransport;

/* Toutes les fonctions ci-dessous tolèrent t == NULL (aucun effet, 0 ou -1). */
GtTransport *gt_transport_open(const TransportParams *params, const TransportCallbacks *cb, gpointer user_data, GError **error);
/* Ferme selon les paramètres ouverts (M1) : restauration termios et déverrouillage pour une instance série seulement. */
void         gt_transport_close(GtTransport *t);
/* Octets écrits, 0 si rien n'est ouvert (serveur sans client, connexion en cours), négatif en cas d'erreur. */
gssize       gt_transport_send(GtTransport *t, const guint8 *data, gsize len);
/* Masque TIOCM_*, -1 sans objet (TCP, pty sans lignes modem) ; une erreur de périphérique signale LOST. */
gint         gt_transport_get_modem_lines(GtTransport *t);
void         gt_transport_toggle_line(GtTransport *t, GtModemLine line);
void         gt_transport_send_break(GtTransport *t);
/* Débit réellement obtenu (série), 0 sinon. */
guint        gt_transport_get_actual_baudrate(GtTransport *t);
/* Descripteur d'envoi : série ou client TCP ; serveur : client accepté ; -1 sinon. */
gint         gt_transport_get_fd(GtTransport *t);
/* st_rdev relevé à l'ouverture (série), 0 sinon. */
dev_t        gt_transport_get_device_number(GtTransport *t);

/* Débits (baud.c, baudrates.c) */
struct baudrate
{
	unsigned int baud;
	speed_t speed;
};
extern const struct baudrate baudrate_list[];
extern const int baudrate_count;
extern const gboolean speed_t_is_sane;
speed_t find_standard_baudrate(unsigned int);
unsigned int speed_t_to_baud(speed_t);
unsigned int set_port_baudrate(unsigned int, int);

#endif
