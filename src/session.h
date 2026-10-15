/* Session d'onglet (sous-projet 6b, spec §4.2) : tout l'état d'une instance de GTKTerm présentée dans un onglet. Couche application, sans GTK ;
 * fil principal seulement. */
#ifndef SESSION_H_
#define SESSION_H_

#include <gio/gio.h>

#include "link.h"
#include "macros.h"
#include "port_activity.h"
#include "profile.h"
#include "script_engine.h"
#include "term_options.h"

G_BEGIN_DECLS

#define GT_TYPE_SESSION (gt_session_get_type())
G_DECLARE_FINAL_TYPE(GtSession, gt_session, GT, SESSION, GObject)

/* Session fermée : liaison, options, profil (copie profonde de profile, ou défauts si NULL ; lock_port compris), nom de profil (NULL : « default »),
 * macros (jeu vide, aucun fichier), moteur Lua, groupe d'actions d'onglet. N'ouvre aucun port et ne charge aucun fichier. */
GtSession      *gt_session_new(const GtProfile *profile, const gchar *profile_name);
guint           gt_session_get_id(GtSession *s);	/* 1, 2, 3… dans l'ordre de création ; jamais réutilisé dans le processus */
GtkTermLink    *gt_session_get_link(GtSession *s);
GtTermOptions  *gt_session_get_options(GtSession *s);
GtProfile      *gt_session_get_profile(GtSession *s);	/* profil en cours de l'onglet (dialogues, ligne de commande, chargement) */
const gchar    *gt_session_get_profile_name(GtSession *s);	/* « default » si aucun */
void            gt_session_set_profile_name(GtSession *s, const gchar *name);	/* NULL : « default » */
GtMacros       *gt_session_get_macros(GtSession *s);
GtScriptEngine *gt_session_get_script_engine(GtSession *s);
GtPortActivity *gt_session_get_activity(GtSession *s);	/* activité du port de l'onglet (indicateurs) */
GActionGroup   *gt_session_get_actions(GtSession *s);	/* en fait un GSimpleActionGroup (G_ACTION_MAP) : la page y ajoute les siennes */
const gchar    *gt_session_get_short_name(GtSession *s);	/* propriété « short-name » ; calculé par gt_session_short_name_for(profil) */
gchar          *gt_session_short_name_for(const GtProfile *p);	/* série : nom de base du port ; client TCP : « hôte:port » ; serveur : « :port » */
void            gt_session_set_file_sending(GtSession *s, gboolean sending);	/* tenu par l'envoi de fichier de la page */
gboolean        gt_session_is_busy(GtSession *s);	/* script en cours ou envoi de fichier en cours */
gboolean        gt_session_is_shut_down(GtSession *s);
/* Projet (6c) : propriété « in-project » (onglet d'un projet : macros intégrées) ; propriété « script-text » (texte de l'éditeur de script, jamais NULL,
 * notifiée seulement si elle change) ; signal « edited » (aucun argument) : un réglage enregistrable a pu changer (plan, décision 2). */
gboolean        gt_session_get_in_project(GtSession *s);
void            gt_session_set_in_project(GtSession *s, gboolean in_project);	/* gt_macros_set_embedded ; edited */
const gchar    *gt_session_get_script_text(GtSession *s);
void            gt_session_set_script_text(GtSession *s, const gchar *text);	/* NULL : "" */
void            gt_session_edited(GtSession *s);	/* émet « edited » (rien après shutdown) */
/* Options ← profil (un seul « changed »), auto-reconnect de la liaison, nom court recalculé ; ne rouvre pas le port, n'appelle aucun crochet */
void            gt_session_apply_profile(GtSession *s);
void            gt_session_open_port(GtSession *s);	/* port du profil (nom court recalculé) ; échec : signal open-failed de la liaison */
/* Ouverture automatique (démarrage, projet, SIGUSR1) : comme gt_session_open_port, mais gt_session_get_open_quiet() rend TRUE jusqu'à la
 * prochaine ouverture : l'interface signale un échec sans boîte. Tout échec d'ouverture met l'activité du port à dead. */
void            gt_session_open_port_quiet(GtSession *s);
gboolean        gt_session_get_open_quiet(GtSession *s);
void            gt_session_close_port(GtSession *s);
/* Arrêt ordonné (plan, décision 3, étape b) : moteur Lua, macros, actions désactivées, liaison fermée, puis signal « closed ». Idempotent ; la session
 * reste un objet valide mais inerte (ouvrir un port, envoyer une macro ou lancer un script ne fait plus rien). */
void            gt_session_shutdown(GtSession *s);

G_END_DECLS

#endif
