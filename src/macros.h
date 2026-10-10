/***********************************************************************/
/* macros.h                                                            */
/* --------                                                            */
/*           GTKTerm Software                                          */
/*                      (c) Julien Schmitt                             */
/*                                                                     */
/* ------------------------------------------------------------------- */
/*                                                                     */
/*   Purpose                                                           */
/*      Macro state of the application (set, file, poller, sending),   */
/*      without GTK                                                    */
/*                                                                     */
/***********************************************************************/

#ifndef MACROS_H_
#define MACROS_H_

#include <glib.h>

#include "macro_set.h"
#include "macro_poller.h"

/* État des macros de l'application (un seul jeu aujourd'hui, un par projet demain), sans GTK ; fil principal seulement. Effets
 * d'interface par les crochets GtAppUi (app_ui.h) : macros_changed, macro_polling_changed, status_flash, message. */
void           macros_init             (void);	/* jeu vide, ordonnanceur ; avant la fenêtre principale */
GtMacroSet    *macros_get_set          (void);
GtMacroPoller *macros_get_poller       (void);
void           macros_shutdown         (void);	/* fermeture de GTKTerm : polling arrêté sans rappel d'état, ordonnanceur libéré */
/* Prend le jeu en charge, arrête tout polling, puis crochet macros_changed. Le jeu courant change d'adresse : une fenêtre qui édite
 * l'ancien jeu (« Configure Macros ») le reconnaît en comparant macros_get_set() au jeu qu'elle édite, et se ferme sans enregistrer. */
void           macros_replace_set      (GtMacroSet *set);
/* Envoi de macro de l'interface (clic, raccourci, polling) ; n_args < 0 : arguments enregistrés. Macro inconnue ou non envoyable :
 * FALSE, rien n'est envoyé. TRUE si tous les octets ont été écrits. Au moins un octet écrit : « Macro "…" sent! » (crochet status_flash). */
gboolean       macros_send             (guint id, const gchar *const *args, gint n_args);
/* Même contrat, sans message : moteur Lua (spec Lua §1 : un script n'annonce pas ses envois) */
gboolean       macros_send_quiet       (guint id, const gchar *const *args, gint n_args);
/* NULL : chemin courant (ou défaut). Fichier absent : jeu vide, TRUE. Fichier illisible : FALSE ; interactif (menu), message, jeu et
 * chemin inchangés ; non interactif (profil), jeu vide, chemin courant = path, avertissement + message différé (g_idle_add). */
gboolean       macros_load_file        (const gchar *path, gboolean interactive);
gboolean       macros_save_file        (const gchar *path, gboolean interactive);	/* NULL : chemin courant (ou défaut) */
/* Oublie le dernier échec d'enregistrement signalé : le prochain échec, même identique, est de nouveau signalé. Appelé avant un
 * enregistrement explicite (menu « Save macros file ») ; les enregistrements implicites (frappe, fenêtre d'édition) ne l'appellent pas. */
void           macros_forget_save_error(void);
const gchar   *macros_file_get_path    (void);
gchar         *macros_file_default_path(void);
void           macros_changed          (void);	/* crochet macros_changed : panneau et raccourcis à reconstruire */

#endif
