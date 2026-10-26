/* Liaison du port principal (sous-projet 2) : machine d'état de la connexion, reconnexion, moniteur udev, historique.
 * GObject observé par l'interface (propriétés notifiées, signaux) ; ne dépend ni de GTK ni de VTE.
 *
 * Signaux : « open-failed » (message), « received » (GBytes, heure murale en µs), « sent » (GBytes, GtSendOrigin),
 * « history-cleared » ; propriétés notifiées : « state », « status-text », « auto-reconnect », « reconnect-interval-ms ».
 *
 * Contrat de réentrance :
 * - « received » est émis sous la surveillance du descripteur du transport ; « sent » dans gt_link_send / gt_link_try_send (écriture
 *   directe, donc sous l'appelant de l'envoi, lui-même parfois un gestionnaire de « received ») ou sous la surveillance du descripteur
 *   (file d'émission qui se vide) ; un gestionnaire de « sent » peut envoyer de nouveau (ses octets passent derrière ceux qui attendent) ;
 *   « notify::state » et « notify::status-text » peuvent
 *   l'être sous cette même surveillance (connexion établie, client TCP arrivé ou parti), au signalement différé d'une perte,
 *   sous le moniteur udev, la minuterie de reconnexion, ou dans gt_link_open / gt_link_close ;
 * - leurs gestionnaires ne doivent ni appeler gt_link_open / gt_link_close, ni lancer de boucle imbriquée (g_main_loop_run,
 *   dialogue attendu de façon synchrone…) : différer par g_idle_add ;
 * - « open-failed » n'est émis que dans gt_link_open ou depuis une source d'inactivité (échec asynchrone d'un client TCP),
 *   jamais sous la surveillance : son gestionnaire peut ouvrir une boîte de dialogue. */
#ifndef LINK_H_
#define LINK_H_

#include <glib-object.h>

#include "history.h"
#include "transport.h"

G_BEGIN_DECLS

typedef enum
{
	GT_LINK_CLOSED,
	GT_LINK_CONNECTING,		/* client TCP : connexion demandée, pas encore établie */
	GT_LINK_OPEN,
	GT_LINK_LISTENING,		/* serveur TCP sans client */
	GT_LINK_LOST,
	GT_LINK_RECONNECTING
} GtLinkState;

GType gt_link_state_get_type(void);
#define GT_TYPE_LINK_STATE (gt_link_state_get_type())

#define GT_TYPE_LINK (gt_link_get_type())
G_DECLARE_FINAL_TYPE(GtkTermLink, gt_link, GT, LINK, GObject)

GtkTermLink *gt_link_new(void);

/* Ouverture demandée par l'utilisateur : ferme d'abord la connexion courante. Échec : open-failed, puis FALSE ;
 * avec auto-reconnect (série, client TCP), la liaison passe ensuite en RECONNECTING au lieu de rester CLOSED. */
gboolean     gt_link_open(GtkTermLink *link, const TransportParams *params, GError **error);
/* Fermeture demandée : annule toute reconnexion. */
void         gt_link_close(GtkTermLink *link);
/* Frappe, collage, macros, saisie hexadécimale, send_macro Lua : tout accepté ou rien. len si accepté (écrit tout de suite, le reste dans la
 * file d'émission du transport, écrit dès que le port le prend), 0 si la liaison n'est pas ouverte (rien n'est mis en file : jamais de rejeu
 * après une reconnexion), -1 et errno sinon (ENOBUFS : file pleine, GT_TRANSPORT_QUEUE_MAX). Seuls les octets réellement écrits entrent dans
 * l'historique (TX, heure de l'écriture) et sont émis par « sent », au fil de l'écriture ; la file meurt avec le transport (fermeture, perte). */
gssize       gt_link_send(GtkTermLink *link, const guint8 *data, gsize len, GtSendOrigin origin);
/* Une seule écriture, jamais bloquante, pour les envois qui se régulent eux-mêmes (fichier, send/send_hex Lua sur le port 0) : octets écrits
 * (RS485 : acceptés pour un cycle RTS), 0 si rien n'est ouvert, -1 et errno ; -1 et EAGAIN tant que la file d'émission n'est pas vide. */
gssize       gt_link_try_send(GtkTermLink *link, const guint8 *data, gsize len, GtSendOrigin origin);
/* Octets acceptés par gt_link_send pas encore écrits ; 0 sans transport */
gsize        gt_link_get_queued(GtkTermLink *link);
gint         gt_link_get_modem_lines(GtkTermLink *link);
void         gt_link_toggle_line(GtkTermLink *link, GtModemLine line);
void         gt_link_send_break(GtkTermLink *link);
gint         gt_link_get_fd(GtkTermLink *link);
GtLinkState  gt_link_get_state(GtkTermLink *link);
/* Vrai si la liaison est ouverte (GT_LINK_OPEN) sur un port série : Send break, DTR/RTS et voyants n'ont de sens que là.
 * Un passage d'une liaison TCP ouverte à une liaison série ouverte laisse « state » à OPEN : relire sur notify::state ET notify::status-text. */
gboolean     gt_link_is_serial_open(GtkTermLink *link);
const gchar *gt_link_get_status_text(GtkTermLink *link);
GtHistory   *gt_link_get_history(GtkTermLink *link);
/* Vide l'historique et émet « history-cleared » (commande « Effacer l'écran », Échap). */
void         gt_link_clear_history(GtkTermLink *link);

/* Entrée de test : simule un événement udev (« add » / « remove », numéro de périphérique, chemin du nœud). */
void         gt_link_handle_device_event(GtkTermLink *link, const gchar *action, dev_t devnum, const gchar *path);

G_END_DECLS

#endif
