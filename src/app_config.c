/***********************************************************************/
/* app_config.c                                                        */
/*                                                                     */
/*   Purpose                                                           */
/*      Configuration de l'application, sans GTK (sous-projet 6a) :    */
/*      façade du magasin, profil en cours, profils, [general] et      */
/*      état de la fenêtre [window] (extrait de term_config.c)         */
/*                                                                     */
/***********************************************************************/

#include <string.h>

#include <config.h>
#include <glib.h>
#include <glib/gi18n.h>

#include "app_config.h"
#include "app_ui.h"
#include "config_file.h"
#include "macros.h"

#define WINDOW_GROUP "window"
#define WINDOW_DEFAULT_WIDTH 750
#define WINDOW_DEFAULT_HEIGHT 550
#define WINDOW_MAX_SIZE 10000

static GtConfigStore *store = NULL;
static gchar *store_load_error = NULL;	/* erreur de lecture (contient le chemin), signalée par Check_configuration_file */
static gchar *active_config_name = NULL;	/* nom du profil chargé ou enregistré ; NULL : « default » */

GtProfile config;
GtkTermSettings settings;

const gchar *app_config_active_name(void)
{
	return active_config_name != NULL ? active_config_name : "default";
}

void config_file_init(void)
{
	/* $XDG_CONFIG_HOME/.gtktermrc ; ancien $HOME/.gtktermrc migré (config_file_prepare) */
	gchar *path = config_file_prepare(g_get_user_config_dir(), g_get_home_dir());
	GError *error = NULL;

	gt_config_store_free(store);
	g_clear_pointer(&store_load_error, g_free);
	store = gt_config_store_load(path, &error);
	/* Appelé avant bindtextdomain : le message traduit est composé par Check_configuration_file */
	if(error != NULL)
	{
		store_load_error = g_strdup(error->message);
		g_error_free(error);
	}
	g_free(path);
}

GtConfigStore *config_get_store(void)
{
	return store;
}

void config_reload_store(void)
{
	GError *error = NULL;

	if(!gt_config_store_reload_if_changed(store, &error))
	{
		/* Fichier devenu illisible : magasin en lecture seule ; l'enregistrement qui suit le signale (message ou g_printerr) */
		g_printerr("%s\n", error->message);
		g_error_free(error);
	}
}

gboolean config_save_store(gboolean interactive)
{
	GError *error = NULL;

	if(gt_config_store_save(store, &error))
		return TRUE;
	if(interactive)
		gt_app_message(GT_APP_MSG_ERROR, "%s", error->message);
	else
		g_printerr("%s\n", error->message);
	g_error_free(error);
	return FALSE;
}

static void settings_load(void)
{
	g_free(settings.update_url);
	settings.update_url = gt_config_store_get_general_string(store, "update_url", GTKTERM_DEFAULT_UPDATE_URL);
	g_free(settings.update_prefix);
	settings.update_prefix = gt_config_store_get_general_string(store, "update_prefix", GTKTERM_DEFAULT_UPDATE_PREFIX);
	settings.update_check_startup = gt_config_store_get_general_boolean(store, "update_check_startup", TRUE);
	g_free(settings.update_skipped_revision);
	settings.update_skipped_revision = gt_config_store_get_general_string(store, "update_skipped_revision", NULL);
	/* Clé vide (révision ignorée effacée) : aucune */
	if(settings.update_skipped_revision != NULL && settings.update_skipped_revision[0] == '\0')
		g_clear_pointer(&settings.update_skipped_revision, g_free);
}

void config_save_update_url(const gchar *url)
{
	/* URL inchangée (fenêtre seulement fermée) : rien à écrire, pas de message en lecture seule */
	if(g_strcmp0(url, settings.update_url) == 0)
		return;
	gt_profile_set_string(&settings.update_url, url);
	/* Seule la clé modifiée est écrite : update_prefix et update_check_startup réécrits depuis la mémoire effaceraient une modification
	 * faite entre-temps (autre instance, à la main) */
	config_reload_store();
	gt_config_store_set_general_string(store, "update_url", settings.update_url);
	config_save_store(TRUE);
}

void config_save_skipped_revision(const gchar *rev)
{
	const gchar *value = (rev != NULL && rev[0] != '\0') ? rev : NULL;

	if(g_strcmp0(value, settings.update_skipped_revision) == 0)
		return;
	gt_profile_set_string(&settings.update_skipped_revision, value);
	config_reload_store();
	/* NULL : clé vide, relue comme « aucune » (settings_load) */
	gt_config_store_set_general_string(store, "update_skipped_revision", value);
	config_save_store(TRUE);
}

void config_save_update_check_startup(gboolean enabled)
{
	enabled = enabled != FALSE;
	/* Case remise à sa valeur : rien à écrire, pas de message en lecture seule */
	if(enabled == (settings.update_check_startup != FALSE))
		return;
	settings.update_check_startup = enabled;
	/* Seule la clé modifiée est écrite, après relecture : update_url, update_prefix et la révision ignorée restent ceux du fichier */
	config_reload_store();
	gt_config_store_set_general_boolean(store, "update_check_startup", enabled);
	config_save_store(TRUE);
}

void config_remember_macros_file(void)
{
	const gchar *path = macros_file_get_path();

	/* Fichier de macros inchangé (perte de focus d'un argument, même fichier rechoisi) : rien à écrire */
	if(g_strcmp0(path, config.macros_file) == 0)
		return;
	gt_profile_set_string(&config.macros_file, path);
	config_reload_store();
	/* Profil actif supprimé entre-temps : pas de profil fantôme réduit à macros_file, mémoire seulement */
	if(!gt_config_store_has_profile(store, app_config_active_name()))
		return;
	gt_config_store_set_profile_string(store, app_config_active_name(), "macros_file", config.macros_file);
	config_save_store(TRUE);
}

static void hard_default_configuration(void)
{
	gt_profile_clear(&config);
	gt_profile_init_defaults(&config);
	gt_profile_set_string(&settings.update_url, GTKTERM_DEFAULT_UPDATE_URL);
	gt_profile_set_string(&settings.update_prefix, GTKTERM_DEFAULT_UPDATE_PREFIX);
	settings.update_check_startup = TRUE;
	g_clear_pointer(&settings.update_skipped_revision, g_free);
}

/* Profil du fichier copié dans config ; erreur (profil absent) : config et nom actif inchangés */
static gboolean load_profile(const gchar *name, GError **error)
{
	GtProfile loaded = { 0 };
	gboolean lock_port = config.lock_port;

	config_reload_store();
	if(!gt_config_store_get_profile(store, name, &loaded))
	{
		g_set_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_GROUP_NOT_FOUND, _("No section \"%s\" in configuration file\n"), name);
		return FALSE;
	}
	gt_profile_copy(&config, &loaded);
	gt_profile_clear(&loaded);
	/* Réglage de session jamais enregistré : --disable-port-lock survit au chargement d'un profil */
	config.lock_port = lock_port;
	/* Le nom actif n'est retenu qu'une fois le profil trouvé : un nom inconnu ne crée pas de profil à l'enregistrement (C13) */
	gt_profile_set_string(&active_config_name, name);
	/* Fichier de macros du profil ; sans fichier, celui en cours est relu (non interactif : avertissement seulement) */
	macros_load_file(config.macros_file, FALSE);
	/* Pas d'application à l'interface ici : les valeurs ne sont pas encore validées (Verify_configuration) */
	return TRUE;
}

gint Load_configuration_from_file(const gchar *name)
{
	GError *error = NULL;

	if(!load_profile(name, &error))
	{
		gt_app_message(GT_APP_MSG_ERROR, "%s", error->message);
		g_error_free(error);
		return -1;
	}
	return 0;
}

/* Valide le profil en cours (avertissements dans un seul message) ; l'interface l'applique ensuite (jamais de valeur invalide dans VTE) */
void Verify_configuration(void)
{
	GPtrArray *warnings = g_ptr_array_new_with_free_func(g_free);

	if(!gt_profile_validate(&config, warnings))
	{
		gchar *text;

		g_ptr_array_add(warnings, NULL);
		text = g_strjoinv("\n", (gchar **)warnings->pdata);
		gt_app_message(GT_APP_MSG_ERROR, "%s", text);
		g_free(text);
	}
	g_ptr_array_unref(warnings);
}

gint Check_configuration_file(void)
{
	gchar **names;
	gboolean no_profile;

	hard_default_configuration();
	settings_load();
	g_clear_pointer(&active_config_name, g_free);

	/* Fichier illisible : défauts, magasin en lecture seule (rien ne sera écrit) */
	if(store_load_error != NULL)
	{
		/* Le message de lecture contient déjà le chemin du fichier */
		gt_app_message(GT_APP_MSG_ERROR, _("Cannot read configuration file: %s\nThe default configuration is used and nothing will be written to this file."),
		               store_load_error);
		g_clear_pointer(&store_load_error, g_free);
		return -1;
	}

	/* Fichier absent, vide ou sans aucun profil (seulement [window]…) : premier lancement, [profile:default] créé avec les défauts */
	names = gt_config_store_list_profiles(store);
	no_profile = (names[0] == NULL);
	g_strfreev(names);
	if(no_profile)
	{
		gt_config_store_put_profile(store, "default", &config);
		if(config_save_store(TRUE))
			gt_app_message(GT_APP_MSG_INFO, _("Configuration file (%s) with\n[default] configuration has been created.\n"), gt_config_store_get_path(store));
		return 0;
	}

	/* D'autres profils, mais pas « default » (spec §3.2-14) : valeurs par défaut, sans message ; « default » ne sera créé que par un
	 * enregistrement explicite */
	if(!gt_config_store_has_profile(store, "default"))
		return 0;

	return Load_configuration_from_file("default") == 0 ? 0 : -1;
}

gboolean app_config_load_profile(const gchar *name, GError **error)
{
	const GtAppUi *ui;

	if(!load_profile(name, error))
		return FALSE;
	Verify_configuration();
	ui = gt_app_ui_get();
	if(ui->profile_applied != NULL)
		ui->profile_applied();
	return TRUE;
}

gboolean app_config_save_profile(const gchar *name, GError **error)
{
	gchar *normalized = NULL;
	GError *save_error = NULL;

	if(!gt_profile_name_validate(name, &normalized, error))
		return FALSE;
	/* Le profil enregistré prend le fichier de macros en cours */
	gt_profile_set_string(&config.macros_file, macros_file_get_path());
	config_reload_store();
	gt_config_store_put_profile(store, normalized, &config);
	if(!gt_config_store_save(store, &save_error))
	{
		g_propagate_error(error, save_error);
		g_free(normalized);
		return FALSE;
	}
	/* Le nom actif ne change qu'après réussite de l'enregistrement */
	g_free(active_config_name);
	active_config_name = normalized;
	return TRUE;
}

gboolean app_config_delete_profile(const gchar *name, GError **error)
{
	GError *save_error = NULL;

	config_reload_store();
	if(!gt_config_store_delete_profile(store, name))
	{
		g_set_error_literal(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_GROUP_NOT_FOUND, _("Cannot delete section!"));
		return FALSE;
	}
	if(!gt_config_store_save(store, &save_error))
	{
		g_propagate_error(error, save_error);
		return FALSE;
	}
	return TRUE;
}

gchar **app_config_list_profiles(void)
{
	/* Liste à jour : profils enregistrés ou supprimés par une autre instance depuis le démarrage */
	config_reload_store();
	return gt_config_store_list_profiles(store);
}

gboolean app_config_has_profile(const gchar *name)
{
	gchar *trimmed;
	gboolean found;

	if(name == NULL)
		return FALSE;
	trimmed = g_strstrip(g_strdup(name));
	/* Comparaison sans casse : « Default » est le même profil que « default » (C6) ; fichier relu pour une réponse à jour */
	config_reload_store();
	found = gt_config_store_has_profile(store, trimmed);
	g_free(trimmed);
	return found;
}

/* ---- État de la fenêtre ([window]) ---------------------------------------- */

static gboolean read_window_int(GKeyFile *kf, const gchar *key, gint *out)
{
	GError *error = NULL;
	gint v = g_key_file_get_integer(kf, WINDOW_GROUP, key, &error);

	if(error != NULL)
	{
		g_error_free(error);
		return FALSE;
	}
	*out = v;
	return TRUE;
}

/* true/yes/on/1, false/no/off/0 ; out inchangé si la clé manque ou n'est pas reconnue */
static void read_window_bool(GKeyFile *kf, const gchar *key, gboolean *out)
{
	gchar *v = g_key_file_get_value(kf, WINDOW_GROUP, key, NULL);

	if(v != NULL)
		gt_config_parse_boolean(g_strstrip(v), out);
	g_free(v);
}

void app_config_load_window_state(GtWindowState *out)
{
	GKeyFile *kf;
	gint w, h, p;

	out->width = WINDOW_DEFAULT_WIDTH;
	out->height = WINDOW_DEFAULT_HEIGHT;
	out->maximized = FALSE;
	out->h_paned = -1;
	out->v_paned = -1;
	out->macro_panel_visible = TRUE;
	out->script_panel_visible = FALSE;
	out->hex_bar_visible = FALSE;
	if(store == NULL)
		return;
	kf = gt_config_store_get_key_file(store);
	/* Taille aberrante (≤ 0 ou > 10 000, fichier modifié à la main) : taille par défaut, les deux valeurs ensemble */
	if(read_window_int(kf, "width", &w) && read_window_int(kf, "height", &h) && w > 0 && h > 0 && w <= WINDOW_MAX_SIZE && h <= WINDOW_MAX_SIZE)
	{
		out->width = w;
		out->height = h;
	}
	if(read_window_int(kf, "paned_position", &p) && p >= 0)
		out->h_paned = p;
	if(read_window_int(kf, "v_paned_position", &p) && p >= 0)
		out->v_paned = p;
	read_window_bool(kf, "maximized", &out->maximized);
	read_window_bool(kf, "macro_panel_visible", &out->macro_panel_visible);
	read_window_bool(kf, "script_panel_visible", &out->script_panel_visible);
	read_window_bool(kf, "hex_bar_visible", &out->hex_bar_visible);
}

static void write_window_int(GKeyFile *kf, const gchar *key, gint value, gint min)
{
	if(value >= min)
		g_key_file_set_integer(kf, WINDOW_GROUP, key, value);
	else
		g_key_file_remove_key(kf, WINDOW_GROUP, key, NULL);
}

void app_config_save_window_state(const GtWindowState *st)
{
	GKeyFile *kf;

	if(store == NULL)
		return;
	/* Relire avant d'écrire : un profil enregistré par une autre instance depuis le démarrage n'est pas écrasé */
	config_reload_store();
	/* Magasin en lecture seule (fichier illisible) : rien à écrire, l'erreur a déjà été montrée au démarrage */
	if(gt_config_store_is_read_only(store))
		return;
	kf = gt_config_store_get_key_file(store);
	/* Position : plus lue (sans effet sous Wayland, impossible en GTK 4) ; retirée au premier enregistrement */
	g_key_file_remove_key(kf, WINDOW_GROUP, "x", NULL);
	g_key_file_remove_key(kf, WINDOW_GROUP, "y", NULL);
	write_window_int(kf, "width", st->width, 1);
	write_window_int(kf, "height", st->height, 1);
	g_key_file_set_boolean(kf, WINDOW_GROUP, "maximized", st->maximized);
	write_window_int(kf, "paned_position", st->h_paned, 0);
	write_window_int(kf, "v_paned_position", st->v_paned, 0);
	g_key_file_set_boolean(kf, WINDOW_GROUP, "macro_panel_visible", st->macro_panel_visible);
	g_key_file_set_boolean(kf, WINDOW_GROUP, "script_panel_visible", st->script_panel_visible);
	g_key_file_set_boolean(kf, WINDOW_GROUP, "hex_bar_visible", st->hex_bar_visible);
	config_save_store(FALSE);
}
