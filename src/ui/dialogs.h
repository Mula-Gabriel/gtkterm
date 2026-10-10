/* Boîtes de l'interface GTK 4 (spec §6.4) : jamais de boucle imbriquée, réponses par rappel. Table GtAppUi de l'interface GTK 4. */
#ifndef GT_UI_DIALOGS_H_
#define GT_UI_DIALOGS_H_

#include <gtk/gtk.h>

#include "app_ui.h"

G_BEGIN_DECLS

/* Boîtes de message et de confirmation : fenêtres construites par GTKTerm (pas GtkAlertDialog), modales, transitoires du parent et
 * détruites avec lui, classes CSS « dialog » et « message » (repérées ainsi par le harnais gui), texte principal et détail en texte
 * brut sélectionnable, Échap = fermer.
 * Message : texte principal selon kind (« Information », « Warning », « Error »), text en détail, bouton « OK ». Les messages passent
 * par une seule file : un à la fois, dans l'ordre ; avant gt_ui_main_window_ready ils attendent ; après gt_ui_messages_shutdown ils vont
 * sur stderr. parent NULL ou détruit entre-temps : la fenêtre principale. */
void gt_ui_message(GtkWindow *parent, GtAppMessageKind kind, const char *text);
/* Rappels de gt_ui_confirm, gt_ui_open_file et gt_ui_save_file : une seule fois, toujours depuis une source d'inactivité, la boîte refermée
 * (l'appelant peut y détruire la fenêtre parente) ; fenêtre parente détruite pendant l'opération (fermeture de GTKTerm) : boîte détruite
 * avec elle, rappel avec FALSE / NULL (même si un fichier avait été choisi juste avant). Après la fermeture, un rappel peut arriver
 * pendant la vidange finale de g_application_run : services arrêtés, il ne doit rien entreprendre sur FALSE / NULL. */
typedef void (*GtUiConfirmCb)(gboolean accepted, gpointer user_data);
/* question en texte principal, detail (ou NULL) dessous ; boutons « Cancel » et accept_label (par défaut) ; Échap, fermeture ou parent
 * détruit = refus. Le rappel vérifie lui-même que sa cible existe encore. */
void gt_ui_confirm(GtkWindow *parent, const char *question, const char *detail, const char *accept_label, GtUiConfirmCb cb, gpointer user_data);
/* Boîte à plusieurs choix (labels terminés par NULL, sans mnémonique obligatoire ; le premier est le bouton par défaut) : rappel avec l'indice du
 * bouton cliqué, ou -1 si la boîte est fermée sans choix (Échap, fermeture, parent détruit) ; mêmes garanties de rappel que gt_ui_confirm. */
typedef void (*GtUiChooseCb)(gint choice, gpointer user_data);
void gt_ui_choose(GtkWindow *parent, const char *text, const char *detail, const char *const *labels, GtUiChooseCb cb, gpointer user_data);
typedef void (*GtUiFileCb)(GFile *file, gpointer user_data);	/* NULL si annulé */
/* initial : fichier présélectionné, ou dossier de départ s'il en est un ; filters : GListModel de GtkFileFilter ou NULL */
void gt_ui_open_file(GtkWindow *parent, const char *title, GFile *initial, GListModel *filters, GtUiFileCb cb, gpointer user_data);
/* Confirmation d'écrasement native du sélecteur */
void gt_ui_save_file(GtkWindow *parent, const char *title, GFile *initial_folder, const char *initial_name, GtUiFileCb cb, gpointer user_data);
/* Table GtAppUi de l'interface GTK 4 : message → gt_ui_message(NULL, …) ; profile_applied → profil réappliqué et port rouvert ;
 * status_flash (barre d'état), macros_changed et macro_polling_changed (panneau de macros). */
void gt_ui_install_app_hooks(void);
/* Fenêtre principale présentée : les messages en file s'affichent, dans l'ordre */
void gt_ui_main_window_ready(GtkWindow *win);
/* Fermeture (close-request, shutdown, sortie de main avant la fenêtre) : boîte affichée détruite, messages en attente écrits sur stderr ;
 * les messages suivants aussi. Sans effet la deuxième fois. */
void gt_ui_messages_shutdown(void);
/* Échap ferme la fenêtre (window.close, donc close-request) : fenêtres et dialogues de l'interface GTK 4 */
void gt_ui_close_on_escape(GtkWindow *win);

G_END_DECLS

#endif
