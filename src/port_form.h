/* Saisie du dialogue Port, sans widget (sous-projet 6a) : le dialogue remplit un GtPortForm ; gt_port_form_apply contrôle la saisie et produit le profil.
 * Testable sans affichage (tests/test_port_form.c). */
#ifndef PORT_FORM_H_
#define PORT_FORM_H_

#include <glib.h>

#include "profile.h"

#define GT_PORT_FORM_DEFAULT_HOST "localhost"
#define GT_PORT_FORM_DEFAULT_TCP_PORT "2323"
#define GT_PORT_FORM_DEFAULT_BAUDRATE 115200
#define GT_PORT_FORM_DELAY_MAX 500	/* ms : délai de fin de ligne, RS-485 avant et après */

#define GT_PORT_FORM_ERROR (gt_port_form_error_quark())
typedef enum
{
	GT_PORT_FORM_ERROR_DEVICE,	/* série : chemin du port vide */
	GT_PORT_FORM_ERROR_BAUDRATE,	/* série : débit qui n'est pas un entier de 1 à G_MAXINT */
	GT_PORT_FORM_ERROR_SERIAL,	/* série : bits (5-8) ou bits de stop (1-2) hors plage */
	GT_PORT_FORM_ERROR_HOST,	/* client TCP sans hôte */
	GT_PORT_FORM_ERROR_TCP_PORT,	/* port TCP qui n'est pas un entier de 1 à 65535 */
	GT_PORT_FORM_ERROR_DELAY,	/* délai hors de 0 à GT_PORT_FORM_DELAY_MAX ms */
	GT_PORT_FORM_ERROR_WAIT_CHAR,	/* attente cochée sans exactement un caractère ASCII */
	GT_PORT_FORM_ERROR_ACTIVITY	/* délai d'activité hors de 0 à GT_PROFILE_ACTIVITY_MAX s */
} GtPortFormError;
GQuark gt_port_form_error_quark(void);

typedef struct
{
	TransportType transport;
	gchar *device;	/* série : texte saisi */
	gchar *baudrate;	/* série : texte saisi */
	GtParity parity;
	gint bits;
	gint stopbits;
	GtFlow flow;
	gchar *host;	/* TCP : texte saisi */
	gchar *tcp_port;	/* TCP : texte saisi */
	gint wait_delay_ms;
	gboolean wait_char_enabled;
	gchar *wait_char;	/* texte saisi (UTF-8) */
	gint rs485_before_ms;
	gint rs485_after_ms;
	gint idle_timeout_s;
	gint no_data_timeout_s;
} GtPortForm;

/* f non initialisé (ou vidé par gt_port_form_clear). Valeurs affichées : hôte « localhost » et port « 2323 » si le profil n'en a pas, débit 115200 si le
 * profil vaut 0, bits et bits de stop hors plage ramenés à 8 et 1, délais bornés à 0-500, délais d'activité bornés à 0-3600 ; attente : cochée si wait_char ≥ 0, texte = le caractère s'il
 * est ASCII imprimable ou espace (1-127), vide sinon (OK la refusera). Le profil n'est jamais modifié. */
void     gt_port_form_from_profile(GtPortForm *f, const GtProfile *p);
void     gt_port_form_clear(GtPortForm *f);
/* Contrôle la saisie : champs série en série, champs TCP en TCP, envoi de fichier (délai, attente) toujours ; textes rognés. Succès : *out (initialisé
 * ou mis à zéro, distinct de base) = copie de base modifiée par le formulaire ; les champs de l'autre transport et ceux que le dialogue n'édite pas
 * restent ceux de base ; attente de caractère = délai de fin de ligne nul. Échec : *out inchangé, première erreur dans l'ordre du dialogue,
 * message traduit destiné à l'utilisateur. */
gboolean gt_port_form_apply(const GtPortForm *f, const GtProfile *base, GtProfile *out, GError **error);

#endif
