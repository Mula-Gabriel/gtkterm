/***********************************************************************/
/* macros.h                                                            */
/* --------                                                            */
/*           GTKTerm Software                                          */
/*                      (c) Julien Schmitt                             */
/*                                                                     */
/* ------------------------------------------------------------------- */
/*                                                                     */
/*   Purpose                                                           */
/*      Macro state of the application (set, file, poller, shortcuts)  */
/*                                                                     */
/***********************************************************************/

#ifndef MACROS_H_
#define MACROS_H_

#include <gtk/gtk.h>

#include "macro_set.h"
#include "macro_poller.h"

/* État des macros de l'application (un seul jeu aujourd'hui, un par projet demain) ; fil principal seulement. */
void           macros_init             (void);	/* jeu vide, ordonnanceur sur la liaison par défaut ; avant create_main_window() */
GtMacroSet    *macros_get_set          (void);
GtMacroPoller *macros_get_poller       (void);
void           macros_replace_set      (GtMacroSet *set);	/* prend le jeu en charge, arrête tout polling, raccourcis + panneau */
/* Seul envoi de macro de l'interface (clic, raccourci, polling, Lua) ; n_args < 0 : arguments enregistrés.
 * Macro inconnue ou non envoyable : FALSE, rien n'est envoyé. TRUE si tous les octets ont été écrits. */
gboolean       macros_send             (guint id, const gchar *const *args, gint n_args);
/* NULL : chemin courant (ou défaut). Fichier absent : jeu vide, TRUE. Fichier illisible : FALSE ; interactif (menu), boîte, jeu et
 * chemin inchangés ; non interactif (profil), jeu vide, chemin courant = path, avertissement + boîte différée (g_idle_add). */
gboolean       macros_load_file        (const gchar *path, gboolean interactive);
gboolean       macros_save_file        (const gchar *path, gboolean interactive);	/* NULL : chemin courant (ou défaut) */
/* Oublie le dernier échec d'enregistrement signalé : le prochain échec, même identique, ouvre de nouveau sa boîte. Appelé avant un
 * enregistrement explicite (menu « Save macros file ») ; les enregistrements implicites (frappe, fenêtre d'édition) ne l'appellent pas. */
void           macros_forget_save_error(void);
const gchar   *macros_file_get_path    (void);
gchar         *macros_file_default_path(void);
void           macros_changed          (void);	/* raccourcis reconnectés + panneau reconstruit */
void           Config_macros           (GtkAction *action, gpointer data);

#endif
