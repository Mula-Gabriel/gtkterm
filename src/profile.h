/* Profil de connexion et d'affichage (sous-projet 3) : une valeur sans GTK, copiée par le profil en cours (et demain par onglet).
 * Une structure mise à zéro est un profil vide valide (gt_profile_clear l'accepte). */
#ifndef PROFILE_H_
#define PROFILE_H_

#include <glib.h>

#include "transport.h"

#define GT_PROFILE_DEFAULT_PORT "/dev/ttyS0"
#define GT_PROFILE_DEFAULT_FONT "Monospace 12"

typedef struct
{
	gdouble red, green, blue, alpha;
} GtColor;

typedef struct
{
	/* connexion */
	TransportType transport;
	gchar *port;			/* série */
	guint baudrate;
	gint bits;			/* 5 à 8 */
	gint stopbits;			/* 1 ou 2 */
	GtParity parity;
	GtFlow flow;
	gint rs485_before_ms;
	gint rs485_after_ms;
	gboolean lock_port;		/* faux avec --disable-port-lock ; jamais enregistré */
	gchar *host;			/* TCP */
	gchar *tcp_port;
	gint wait_delay_ms;		/* envoi de fichier : délai après chaque fin de ligne */
	gint wait_char;			/* envoi de fichier : caractère attendu 0-255, -1 : aucun */
	/* comportement */
	gboolean echo;
	gboolean crlf_auto;
	gboolean auto_reconnect;
	gboolean esc_clear_screen;
	gboolean timestamp;
	/* terminal */
	gchar *font;
	gboolean block_cursor;
	gint columns;
	gint rows;
	gint scrollback;
	gboolean visual_bell;		/* conservé ; VTE 2.91 n'a plus de cloche visuelle */
	GtColor foreground;
	GtColor background;
	/* macros */
	gchar *macros_file;		/* NULL : aucun */
} GtProfile;

/* p non initialisé ou vidé par gt_profile_clear */
void         gt_profile_init_defaults(GtProfile *p);
/* dst initialisé (ou mis à zéro) : vidé puis copie profonde de src */
void         gt_profile_copy(GtProfile *dst, const GtProfile *src);
void         gt_profile_clear(GtProfile *p);
void         gt_profile_set_string(gchar **field, const gchar *value);
/* Corrige les valeurs invalides et ajoute à warnings (chaînes traduites, libérées par le tableau) un message par correction
 * ou alerte. Renvoie TRUE si aucun message n'a été ajouté. */
gboolean     gt_profile_validate(GtProfile *p, GPtrArray *warnings);
/* À libérer avec transport_params_clear */
void         gt_profile_to_transport_params(const GtProfile *p, TransportParams *out);

/* Noms écrits dans .gtktermrc ; lecture sans tenir compte de la casse, out inchangé si le nom est inconnu */
const gchar *gt_parity_to_string(GtParity v);
gboolean     gt_parity_from_string(const gchar *s, GtParity *out);
const gchar *gt_flow_to_string(GtFlow v);
gboolean     gt_flow_from_string(const gchar *s, GtFlow *out);
const gchar *gt_transport_to_string(TransportType v);
gboolean     gt_transport_from_string(const gchar *s, TransportType *out);

#endif
