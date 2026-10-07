/* Port ouvert par un script Lua (sous-projet 5) : un GtTransport et une GtRxQueue, plus un état lisible de n'importe quel thread.
 * Ouverture, fermeture et envoi sur le thread principal (celui de la boucle GLib par défaut, où tournent les rappels du
 * transport) ; état, file et raison lisibles de n'importe quel thread. Événements du transport : CONNECTED → OPEN ;
 * CLIENT_CONNECTED → CONNECTED ; CLIENT_GONE → LISTENING (file ouverte) ; LOST → LOST, raison retenue, file fermée. */
#ifndef SCRIPT_PORT_H_
#define SCRIPT_PORT_H_

#include <glib.h>

#include "rx_queue.h"
#include "transport.h"

/* Taille de la file de réception d'un port Lua et du port 0 du moteur */
#define GT_SCRIPT_RX_MAX (1024 * 1024)

typedef enum
{
	GT_SCRIPT_PORT_CONNECTING,	/* client TCP : connexion en cours */
	GT_SCRIPT_PORT_OPEN,		/* série ouvert, client TCP connecté */
	GT_SCRIPT_PORT_LISTENING,	/* serveur TCP sans client */
	GT_SCRIPT_PORT_CONNECTED,	/* serveur TCP avec un client */
	GT_SCRIPT_PORT_LOST		/* série ou client TCP : liaison perdue ou connexion refusée (pas de reconnexion) */
} GtScriptPortState;

typedef struct _GtScriptPort GtScriptPort;

/* Thread principal. Série : verrou exclusif (lock_port forcé à TRUE), refus si un autre programme ou le port principal le tient. */
GtScriptPort     *gt_script_port_open        (const TransportParams *params, GError **error);
/* Thread principal : ferme le transport (termios restaurés), ferme la file, libère. Tolère NULL. */
void              gt_script_port_close       (GtScriptPort *port);
/* Thread principal : une tentative d'écriture. > 0 octets écrits ; 0 rien d'écrit (connexion en cours, serveur sans client) ;
 * -1 avec *err = errno (EAGAIN : réessayer ; EIO : perdu ; autre : échec). *err vaut 0 si le résultat n'est pas -1. */
gssize            gt_script_port_try_send    (GtScriptPort *port, const guint8 *data, gsize len, gint *err);
/* Thread principal, jamais bloquant : octets en attente d'émission (série : TIOCOUTQ ; 0 hors série ou en cas d'erreur) et débit
 * obtenu (série, 0 sinon), pour une vidange bornée avant la fermeture (le transport jette la sortie en attente en fermant). */
guint             gt_script_port_get_output_pending (GtScriptPort *port);
guint             gt_script_port_get_baudrate (GtScriptPort *port);
/* N'importe quel thread. */
GtScriptPortState gt_script_port_get_state   (GtScriptPort *port);
GtRxQueue        *gt_script_port_get_queue   (GtScriptPort *port);
/* N'importe quel thread : raison de la perte (copie sans fin de ligne, NULL si non perdu). */
gchar            *gt_script_port_dup_reason  (GtScriptPort *port);

/* Aides de l'API Lua, sans Lua. */
gboolean          gt_script_parity_from_name (const gchar *name, GtParity *out);	/* none, odd, even */
gboolean          gt_script_flow_from_name   (const gchar *name, GtFlow *out);		/* none, xon, rts */
/* Paires de chiffres hexadécimaux (casse indifférente), blancs (espace, tabulation, CR, LF) permis entre les octets seulement.
 * Erreur (G_CONVERT_ERROR_ILLEGAL_SEQUENCE ; le cœur n'a pas GIO) : « invalid character at position N » (0-based) ou
 * « odd number of hex digits ». */
GBytes           *gt_script_hex_decode       (const gchar *text, GError **error);

#endif
