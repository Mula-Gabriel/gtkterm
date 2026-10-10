/* Configuration de l'application (sous-projet 6a, spec §4.2), sans GTK : façade du magasin (.gtktermrc), profil en cours, profils,
 * réglages [general], état de la fenêtre [window]. Utilisée par l'interface GTK 4 (src/ui/) ; les effets
 * d'interface passent par les crochets GtAppUi (app_ui.h). Fil principal seulement. */
#ifndef APP_CONFIG_H_
#define APP_CONFIG_H_

#include <glib.h>

#include "profile.h"
#include "config_store.h"

/* Profil en cours : profil chargé, modifié par les options de ligne de commande et les dialogues (jamais enregistré sans action explicite) */
extern GtProfile config;

/* Réglages globaux (groupe [general] du fichier de configuration) */
typedef struct
{
	gchar *update_url;
	gchar *update_prefix;
	gboolean update_check_startup;
	gchar *update_skipped_revision;	/* « Skip this version » ; NULL : aucune */
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
/* Écritures partielles : macros_file du profil actif, update_url et update_skipped_revision de [general] (seulement s'ils ont changé) ;
 * jamais les options de ligne de commande */
void           config_remember_macros_file(void);
void           config_save_update_url(const gchar *url);
void           config_save_skipped_revision(const gchar *rev);	/* [general] update_skipped_revision ; NULL efface */
void           config_save_update_check_startup(gboolean enabled);	/* [general] update_check_startup, seulement si la valeur change */
const gchar   *app_config_active_name(void);	/* « default » par défaut */
/* Démarrage (spec §3.2-14) : défauts, [general] ; fichier illisible : message, -1 ; aucun profil : [profile:default] créé (message) ;
 * « default » absent mais d'autres profils : défauts, sans message ni création ; sinon « default » chargé. */
gint           Check_configuration_file(void);
gint           Load_configuration_from_file(const gchar *name);	/* copie dans config (lock_port gardé), nom actif, fichier de macros non interactif */
void           Verify_configuration(void);	/* gt_profile_validate + un seul gt_app_message ; n'applique rien à l'interface */
gboolean       app_config_load_profile(const gchar *name, GError **error);	/* Load + Verify + crochet profile_applied */
gboolean       app_config_save_profile(const gchar *name, GError **error);	/* nom validé ; macros_file = chemin courant ; relire, put, save */
gboolean       app_config_delete_profile(const gchar *name, GError **error);
gchar        **app_config_list_profiles(void);	/* relit d'abord ; g_strfreev */
gboolean       app_config_has_profile(const gchar *name);	/* sans casse, après relecture */

typedef struct
{
	gint width, height;	/* ≤ 0 : taille par défaut */
	gboolean maximized;
	gint h_paned, v_paned;	/* < 0 : position par défaut */
	gboolean macro_panel_visible, script_panel_visible, hex_bar_visible;
} GtWindowState;

/* Clés [window] : width, height, maximized, paned_position (= h_paned), v_paned_position, macro_panel_visible, script_panel_visible,
 * hex_bar_visible ; x et y ne sont plus lus (spec §3.2-7). */
void app_config_load_window_state(GtWindowState *out);	/* défauts : 750×550, non maximisée, -1, -1, TRUE, FALSE, FALSE ; valeurs aberrantes ignorées */
void app_config_save_window_state(const GtWindowState *st);	/* relit, écrit [window], retire x et y, enregistre (non interactif) ; rien si lecture seule */

#endif
