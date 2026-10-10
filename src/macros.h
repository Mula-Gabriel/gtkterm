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

#include "app_ui.h"
#include "link.h"
#include "macro_set.h"
#include "macro_poller.h"

/* État des macros d'un onglet (sous-projet 4, instances en 6b) : jeu, chemin du fichier, ordonnanceur de polling sur la liaison de l'onglet. Sans GTK ;
 * fil principal seulement. Effets d'interface par les crochets GtAppUi (macros_changed, macro_polling_changed, status_flash, message), avec owner. */
typedef struct _GtMacros GtMacros;

/* Jeu vide, aucun fichier ; envois et polling sur link (référence gardée) ; owner : session passée aux crochets (non possédée, NULL permis) */
GtMacros      *gt_macros_new             (GtkTermLink *link, GtSession *owner);
/* Fermeture : polling arrêté sans rappel d'état, ordonnanceur libéré ; ensuite gt_macros_send rend FALSE sans rien envoyer. Idempotente ; NULL accepté. */
void           gt_macros_shutdown        (GtMacros *m);
void           gt_macros_free            (GtMacros *m);	/* shutdown puis libération ; NULL accepté */
GtSession     *gt_macros_get_owner       (GtMacros *m);
GtkTermLink   *gt_macros_get_link        (GtMacros *m);
GtMacroSet    *gt_macros_get_set         (GtMacros *m);
GtMacroPoller *gt_macros_get_poller      (GtMacros *m);	/* NULL après shutdown */
/* Prend le jeu en charge, arrête tout polling, puis crochet macros_changed(owner). Une fenêtre qui édite l'ancien jeu le reconnaît en comparant
 * gt_macros_get_set() au jeu qu'elle édite, et se ferme sans enregistrer. */
void           gt_macros_replace_set     (GtMacros *m, GtMacroSet *set);
/* Envoi de macro de l'interface (clic, raccourci, polling) sur la liaison de m ; n_args < 0 : arguments enregistrés. Macro inconnue ou non envoyable,
 * ou m arrêté : FALSE, rien n'est envoyé. TRUE si tous les octets ont été écrits. Au moins un octet écrit : « Macro "…" sent! » (status_flash(owner)). */
gboolean       gt_macros_send            (GtMacros *m, guint id, const gchar *const *args, gint n_args);
gboolean       gt_macros_send_quiet      (GtMacros *m, guint id, const gchar *const *args, gint n_args);	/* moteur Lua : sans message */
/* NULL : chemin courant (ou défaut). Fichier absent : jeu vide, TRUE. Fichier illisible : FALSE ; interactif (menu), message, jeu et chemin inchangés ;
 * non interactif (profil), jeu vide, chemin courant = path, avertissement + message différé (g_idle_add). */
gboolean       gt_macros_load_file       (GtMacros *m, const gchar *path, gboolean interactive);
gboolean       gt_macros_save_file       (GtMacros *m, const gchar *path, gboolean interactive);	/* NULL : chemin courant (ou défaut) */
/* Oublie le dernier échec d'enregistrement signalé : le prochain échec, même identique, est de nouveau signalé. Appelé avant un enregistrement
 * explicite (menu « Save macros file ») ; les enregistrements implicites (frappe, fenêtre d'édition) ne l'appellent pas. */
void           gt_macros_forget_save_error(GtMacros *m);
const gchar   *gt_macros_get_path        (GtMacros *m);	/* NULL : aucun fichier chargé ni enregistré */
gchar         *macros_file_default_path  (void);
void           gt_macros_changed         (GtMacros *m);	/* crochet macros_changed(owner) : panneau et raccourcis à reconstruire */

#endif
