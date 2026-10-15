/* Application GTK 4 (sous-projet 6a) : GtkApplication, actions app.*, démarrage et fermeture uniques (spec §4.4). */
#ifndef GT_UI_APP_H_
#define GT_UI_APP_H_

#include <gtk/gtk.h>

#include "cmdline.h"
#include "project.h"
#include "session.h"
#include "session_manager.h"

G_BEGIN_DECLS

#define GT_APP_ID "eu.mulagabriel.gtkterm"

/* Identifiant GT_APP_ID et G_APPLICATION_NON_UNIQUE ; avec le crochet de test actif (GTKTERM_TEST_HOOKS=1) :
 * « eu.mulagabriel.gtkterm.test.p<PID> », application unique sur le bus de session (le harnais la joint par org.gtk.Actions).
 * cmd reste à l'appelant et doit vivre jusqu'au retour de g_application_run. sessions : possédé par main (l'application en garde une
 * référence jusqu'à sa finalisation) ; sa session active est celle de l'onglet affiché. */
GtkApplication   *gt_app_new(const GtCmdline *cmd, GtSessionManager *sessions);
GtSessionManager *gt_app_get_sessions(void);	/* NULL avant gt_app_new ou après la finalisation de l'application */
GtSession        *gt_app_get_active_session(void);	/* NULL avant gt_app_new ; session arrêtée après la fermeture, NULL après la finalisation de l'application */
GtProject        *gt_app_get_project(void);	/* projet de la fenêtre (6c) ; NULL avant gt_app_new ou après la finalisation de l'application */
GtkWindow        *gt_app_get_main_window(void);	/* NULL avant « activate » et après la fermeture */
GtkWidget        *gt_app_get_active_view(void);	/* page de la session active (session_view.h), NULL sans fenêtre ni page */
void              gt_app_foreach_view(GFunc func, gpointer user_data);	/* chaque page non arrêtée, dans l'ordre des onglets ; func(page, user_data) */
void              gt_app_refresh_template_siblings(GtSession *s);	/* autres sessions liées au même modèle de macros que s : relues (§2.8) */
/* Nouvel onglet (plan, décision 7) : session créée sur une copie de profile (nom de profil profile_name, NULL : « default »), son fichier de
 * macros chargé (non interactif ; projet ouvert : copie du jeu de l'onglet actif, aucun fichier, onglet de projet au script vide), page et
 * étiquette insérées à droite de l'onglet actif et affichées, profil appliqué, port ouvert (échec : boîte open-failed préfixée du nom de
 * l'onglet). NULL sans fenêtre ou après la fermeture ; sinon la session (empruntée). */
GtSession        *gt_app_add_session(const GtProfile *profile, const gchar *profile_name);
/* Fermeture d'un onglet : dernier onglet, session absente ou arrêtée : rien. Occupée (script, envoi de fichier) : confirmation d'abord (rien si
 * la session est fermée entre-temps). Puis page arrêtée, session arrêtée (dialogues liés fermés sans rien appliquer), onglet retiré. */
void              gt_app_close_session(GtSession *s);
/* Option -f (fichier proposé au premier envoi) : rendue à la première page seulement (les options de ligne de commande visent le premier
 * onglet), NULL ensuite */
const gchar      *gt_app_take_initial_send_file(void);

G_END_DECLS

#endif
