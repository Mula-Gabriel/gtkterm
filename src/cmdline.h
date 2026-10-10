/***********************************************************************/
/* cmdline.h                                                           */
/* ---------                                                           */
/*           GTKTerm Software                                          */
/*                      (c) Julien Schmitt                             */
/*                                                                     */
/* ------------------------------------------------------------------- */
/*                                                                     */
/*   Purpose                                                           */
/*      Reads the command line (application layer, without GTK)        */
/*      - Header file -                                                */
/*                                                                     */
/***********************************************************************/

#ifndef CMDLINE_H_
#define CMDLINE_H_

#include <glib.h>

#include "profile.h"
#include "session.h"

/* Résultat pour main() : sortir tout de suite (--help : 0 ; option inconnue, valeur manquante ou invalide : 1) ou démarrer */
typedef struct
{
	gboolean exit_now;
	gint exit_code;
	gchar *send_file;	/* option -f, à libérer (gt_cmdline_clear) */
} GtCmdline;

/* Analyse ; erreur ou aide : exit_now, rien d'autre n'est fait (ni configuration lue ni fichier créé). Sinon charge dans s le profil
 * (default, puis celui de -c), applique les autres options au profil de s, quel que soit leur ordre, puis Verify_configuration(s). */
void read_command_line(GtSession *s, int argc, char **argv, GtCmdline *out);
void gt_cmdline_clear(GtCmdline *c);

/* ---- Analyse pure, sans configuration (tests) ---- */

#define GT_CMDLINE_ERROR (gt_cmdline_error_quark())
typedef enum
{
	GT_CMDLINE_ERROR_UNKNOWN_OPTION,
	GT_CMDLINE_ERROR_MISSING_VALUE,
	GT_CMDLINE_ERROR_INVALID_VALUE
} GtCmdlineError;
GQuark gt_cmdline_error_quark(void);

/* Champs du profil donnés sur la ligne de commande */
typedef enum
{
	GT_CMDLINE_BAUDRATE     = 1 << 0,	/* -s, 1 à G_MAXUINT */
	GT_CMDLINE_PARITY       = 1 << 1,	/* -a none|odd|even */
	GT_CMDLINE_STOPBITS     = 1 << 2,	/* -t 1|2 */
	GT_CMDLINE_BITS         = 1 << 3,	/* -b 5 à 8 */
	GT_CMDLINE_PORT         = 1 << 4,	/* -p, non vide */
	GT_CMDLINE_FLOW         = 1 << 5,	/* -w none|xon|rts|rs485 (casse ignorée) */
	GT_CMDLINE_DELAY        = 1 << 6,	/* -d 0 à 500 ms */
	GT_CMDLINE_WAIT_CHAR    = 1 << 7,	/* -r, un seul octet */
	GT_CMDLINE_ECHO         = 1 << 8,	/* -e */
	GT_CMDLINE_NO_LOCK      = 1 << 9,	/* -L */
	GT_CMDLINE_RS485_BEFORE = 1 << 10,	/* -x 0 à 500 ms */
	GT_CMDLINE_RS485_AFTER  = 1 << 11,	/* -y 0 à 500 ms */
	GT_CMDLINE_TRANSPORT    = 1 << 12,	/* -T serial|tcp-client|tcp-server */
	GT_CMDLINE_HOST         = 1 << 13,	/* -H */
	GT_CMDLINE_TCP_PORT     = 1 << 14	/* -P */
} GtCmdlineField;

typedef struct
{
	gboolean help;	/* --help : les options suivantes ne sont pas lues */
	gchar *config_name;	/* -c ; NULL : « default » */
	gchar *send_file;	/* -f */
	guint set;	/* GtCmdlineField des options données */
	GtProfile values;	/* leurs valeurs (mis à zéro, chaînes possédées) */
} GtCmdlineOptions;

/* Lit argv (getopt_long, analyse refaisable) ; FALSE + GT_CMDLINE_ERROR (message traduit) si une option est inconnue, sans valeur ou
 * de valeur invalide. out est toujours à vider par gt_cmdline_options_clear, même après un échec. */
gboolean gt_cmdline_parse(int argc, char **argv, GtCmdlineOptions *out, GError **error);
void     gt_cmdline_apply(const GtCmdlineOptions *o, GtProfile *p);	/* recopie dans p les seuls champs donnés */
void     gt_cmdline_options_clear(GtCmdlineOptions *o);
void     display_help(void);

#endif
