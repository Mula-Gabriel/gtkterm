/* Session d'onglet (sous-projet 6b, spec §4.2) : tout l'état d'une instance de GTKTerm présentée dans un onglet. Couche application, sans GTK ;
 * fil principal seulement. */
#ifndef SESSION_H_
#define SESSION_H_

#include <gio/gio.h>

#include "link.h"
#include "macros.h"
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
GActionGroup   *gt_session_get_actions(GtSession *s);	/* en fait un GSimpleActionGroup (G_ACTION_MAP) : la page y ajoute les siennes */
const gchar    *gt_session_get_short_name(GtSession *s);	/* propriété « short-name » ; calculé par gt_session_short_name_for(profil) */
gchar          *gt_session_short_name_for(const GtProfile *p);	/* série : nom de base du port ; client TCP : « hôte:port » ; serveur : « :port » */
void            gt_session_set_file_sending(GtSession *s, gboolean sending);	/* tenu par l'envoi de fichier de la page */
gboolean        gt_session_is_busy(GtSession *s);	/* script en cours ou envoi de fichier en cours */
gboolean        gt_session_is_shut_down(GtSession *s);
/* Options ← profil (un seul « changed »), auto-reconnect de la liaison, nom court recalculé ; ne rouvre pas le port, n'appelle aucun crochet */
void            gt_session_apply_profile(GtSession *s);
void            gt_session_open_port(GtSession *s);	/* port du profil (nom court recalculé) ; échec : signal open-failed de la liaison */
void            gt_session_close_port(GtSession *s);
/* Arrêt ordonné (plan, décision 3, étape b) : moteur Lua, macros, actions désactivées, liaison fermée, puis signal « closed ». Idempotent ; la session
 * reste un objet valide mais inerte (ouvrir un port, envoyer une macro ou lancer un script ne fait plus rien). */
void            gt_session_shutdown(GtSession *s);

G_END_DECLS

#endif
