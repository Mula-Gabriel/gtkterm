/***********************************************************************/
/* config.h                                                            */
/* --------                                                            */
/*           GTKTerm Software                                          */
/*                      (c) Julien Schmitt                             */
/*                                                                     */
/* ------------------------------------------------------------------- */
/*                                                                     */
/*   Purpose                                                           */
/*      Configuration of the serial port                               */
/*      - Header file -                                                */
/*                                                                     */
/***********************************************************************/

#ifndef TERM_CONFIG_H_
#define TERM_CONFIG_H_

#include "transport.h"
#include "profile.h"
#include "config_store.h"

/* Prépare le chemin de .gtktermrc et charge le magasin ; sans interface (appelé avant gtk_init) */
void config_file_init(void);
GtConfigStore *config_get_store(void);
/* Relit le fichier s'il a changé depuis la dernière lecture ou écriture (autre instance, modification à la main) ; à appeler au début
 * de chaque écriture, avant d'appliquer le changement : relire, appliquer un seul changement, enregistrer */
void config_reload_store(void);
/* Enregistre le magasin ; en cas d'erreur : boîte si interactive, sinon g_printerr */
gboolean config_save_store(gboolean interactive);
/* Écritures partielles : macros_file du profil actif, update_url de [general] (seulement s'il a changé) ; jamais les options de
 * ligne de commande */
void config_remember_macros_file(void);
void config_save_update_url(const gchar *url);
void save_window_state(GtkWidget *window, GtkWidget *paned, GtkWidget *script_paned, gboolean script_visible);
void load_window_state(GtkWidget *window, GtkWidget *paned, GtkWidget *script_paned, gboolean *script_visible_out);
void ConfigFlags(void);
void Config_Port_Fenetre(GtkAction *action, gpointer data);
gint Lis_Config(GtkWidget *bouton, GtkWidget **Combos);
void Config_Terminal(GtkAction *action, gpointer data);
void select_config_callback(GtkAction *action, gpointer data);
void save_config_callback(GtkAction *action, gpointer data);
void delete_config_callback(GtkAction *action, gpointer data);
void load_macros_file_callback(GtkAction *action, gpointer data);
void save_macros_file_callback(GtkAction *action, gpointer data);
/* Valide le profil en cours (une seule boîte d'avertissements) puis l'applique au terminal */
void Verify_configuration(void);
gint Load_configuration_from_file(const gchar *name);
gint Check_configuration_file(void);
void check_text_input(GtkEditable *editable,
                      gchar       *new_text,
                      gint         new_text_length,
                      gint        *position,
                      gpointer     user_data);
void clear_scrollback(void);

void configure_echo(gboolean echo);
void configure_crlfauto(gboolean crlfauto);
void configure_autoreconnect_enable(gboolean autoreconnect);
void configure_esc_clear_screen(gboolean esc_clear_screen);

/* Profil en cours : profil chargé, modifié par les options de ligne de commande et les dialogues (jamais enregistré sans action explicite) */
extern GtProfile config;

/* Réglages globaux (groupe [general] du fichier de configuration) */
typedef struct
{
	gchar *update_url;
	gchar *update_prefix;
	gboolean update_check_startup;
} GtkTermSettings;

extern GtkTermSettings settings;

#define GTKTERM_DEFAULT_UPDATE_URL "https://github.com/Mula-Gabriel/gtkterm.git"
#define GTKTERM_DEFAULT_UPDATE_PREFIX "/usr/local"

void config_color_to_rgba(const GtColor *c, GdkRGBA *out);
/* Applique au terminal la police, les couleurs, la taille (colonnes × lignes), le défilement et la forme du curseur du profil en cours */
void config_apply_to_terminal(void);

#endif
