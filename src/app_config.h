/* Configuration de l'application (sous-projet 6a, spec §4.2), sans GTK : façade du magasin (.gtktermrc), profils, réglages [general],
 * état de la fenêtre [window]. Le profil en cours et son nom sont ceux d'une session (6b, session.h). Utilisée par l'interface GTK 4 (src/ui/) ; les effets
 * d'interface passent par les crochets GtAppUi (app_ui.h). Fil principal seulement. */
#ifndef APP_CONFIG_H_
#define APP_CONFIG_H_

#include <glib.h>

#include "window_state.h"
#include "profile.h"
#include "config_store.h"
#include "session.h"

/* Réglages globaux (groupe [general] du fichier de configuration) */
typedef struct
{
	gchar *update_url;
	gchar *update_prefix;
	gboolean update_check_startup;
	gchar *update_skipped_version;	/* « Skip this version » : tag vX.Y.Z ignoré ; NULL : aucun */
} GtkTermSettings;

extern GtkTermSettings settings;

#define GTKTERM_DEFAULT_UPDATE_URL "https://github.com/Mula-Gabriel/gtkterm.git"
#define GTKTERM_DEFAULT_UPDATE_PREFIX "/usr/local"

/* Prépare le chemin de .gtktermrc et charge le magasin ; sans interface (avant gettext). Rappelable : l'ancien magasin est libéré. */
void           config_file_init(void);
GtConfigStore *config_get_store(void);
/* Relit le fichier s'il a changé depuis la dernière lecture ou écriture (autre instance, modification à la main) ; à appeler au début
 * de chaque écriture, avant d'appliquer le changement : relire, appliquer un seul changement, enregistrer */
void           config_reload_store(void);
gboolean       config_save_store(gboolean interactive);	/* erreur : gt_app_message si interactive, sinon g_printerr */
/* Écritures partielles : macros_file du profil actif d'une session, update_url et update_skipped_version de [general] (seulement s'ils
 * ont changé) ; jamais les options de ligne de commande */
void           config_remember_macros_file(GtSession *s);	/* clé macros_file du profil actif de s seulement */
void           config_save_update_url(const gchar *url);
void           config_save_skipped_version(const gchar *tag);	/* [general] update_skipped_version ; NULL efface */
void           config_save_update_check_startup(gboolean enabled);	/* [general] update_check_startup, seulement si la valeur change */
/* Dernier projet ouvert (6c) : [general] last_project ; NULL si absente ou vide. Le setter relit, écrit, enregistre (non interactif) ; NULL efface ; rien si inchangé. */
gchar         *app_config_get_last_project(void);	/* relit ; à libérer */
void           app_config_set_last_project(const gchar *path);
/* Démarrage (spec §3.2-14) : défauts (profil de s et [general]), [general] ; fichier illisible : message, -1 ; aucun profil :
 * [profile:default] créé (message) ; « default » absent mais d'autres profils : défauts, sans message ni création ; sinon « default »
 * chargé dans s. */
gint           Check_configuration_file(GtSession *s);
gint           Load_configuration_from_file(GtSession *s, const gchar *name);	/* copie dans le profil de s (lock_port gardé), nom de profil de s, macros de s */
/* Macros du profil de s, règle unique du chargement d'un profil (démarrage, Load profile, onglet libre de Close project) : onglet de projet,
 * macros_file du profil oublié et jeu inchangé ; sinon macros_file du profil, à défaut le fichier courant de s, à défaut le fichier par défaut
 * (non interactif). */
void           app_config_load_profile_macros(GtSession *s);
void           Verify_configuration(GtSession *s);	/* gt_profile_validate du profil de s + un seul gt_app_message ; n'applique rien à l'interface */
gboolean       app_config_load_profile(GtSession *s, const gchar *name, GError **error);	/* Load + Verify + crochet profile_applied(s) */
/* Options de s et auto-reconnect de sa liaison recopiés dans son profil (gt_term_options_store_profile), macros_file = chemin des macros de s,
 * puis relire, écrire, enregistrer ; nom validé ; nom de profil de s changé seulement après réussite */
gboolean       app_config_save_profile(GtSession *s, const gchar *name, GError **error);
gboolean       app_config_delete_profile(const gchar *name, GError **error);
gchar        **app_config_list_profiles(void);	/* relit d'abord ; g_strfreev */
gboolean       app_config_has_profile(const gchar *name);	/* sans casse, après relecture */

/* Clés [window] : width, height, maximized, paned_position (= h_paned), v_paned_position, macro_panel_visible, script_panel_visible,
 * hex_bar_visible ; x et y ne sont plus lus (spec §3.2-7). */
void app_config_load_window_state(GtWindowState *out);	/* défauts : 1000×600, non maximisée, -1, -1, TRUE, FALSE, FALSE ; valeurs aberrantes ignorées */
void app_config_save_window_state(const GtWindowState *st);	/* relit, écrit [window], retire x et y, enregistre (non interactif) ; rien si lecture seule */

#endif
