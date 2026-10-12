/* Magasin de configuration (sous-projet 3) : le seul code qui lit et écrit .gtktermrc.
 * Groupes : [general], [profile:nom], réservés [window] [colors] [layout] [session] (propriétaires : état de la fenêtre,
 * panneau de scripts), groupes inconnus conservés. Les anciens fichiers parsecfg sont migrés en mémoire au chargement. */
#ifndef CONFIG_STORE_H_
#define CONFIG_STORE_H_

#include <glib.h>

#include "profile.h"

typedef struct _GtConfigStore GtConfigStore;

/* Jamais NULL. Fichier absent ou vide : magasin vide inscriptible. Fichier illisible : GError et magasin vide en lecture seule. */
GtConfigStore *gt_config_store_load(const gchar *path, GError **error);
void           gt_config_store_free(GtConfigStore *s);
gboolean       gt_config_store_is_read_only(GtConfigStore *s);
gboolean       gt_config_store_was_migrated(GtConfigStore *s);
const gchar   *gt_config_store_get_path(GtConfigStore *s);
/* Dernière erreur de lecture (magasin en lecture seule), NULL sinon ; retenue par la lecture, effacée à la lecture réussie suivante */
const GError  *gt_config_store_get_read_error(GtConfigStore *s);

/* Profils ; noms comparés sans tenir compte de la casse (g_utf8_casefold), casse d'origine conservée */
gchar        **gt_config_store_list_profiles(GtConfigStore *s);
gboolean       gt_config_store_has_profile(GtConfigStore *s, const gchar *name);
/* out initialisé ou mis à zéro ; vidé puis rempli (défauts pour les clés absentes ou illisibles). FALSE si le profil n'existe pas (out inchangé). */
gboolean       gt_config_store_get_profile(GtConfigStore *s, const gchar *name, GtProfile *out);
void           gt_config_store_put_profile(GtConfigStore *s, const gchar *name, const GtProfile *p);
gboolean       gt_config_store_delete_profile(GtConfigStore *s, const gchar *name);
/* Écriture d'une seule clé (value NULL : clé retirée) ; crée le profil s'il n'existe pas */
void           gt_config_store_set_profile_string(GtConfigStore *s, const gchar *name, const gchar *key, const gchar *value);

/* Réglages globaux [general] */
gchar         *gt_config_store_get_general_string(GtConfigStore *s, const gchar *key, const gchar *fallback);
gboolean       gt_config_store_get_general_boolean(GtConfigStore *s, const gchar *key, gboolean fallback);
void           gt_config_store_set_general_string(GtConfigStore *s, const gchar *key, const gchar *value);
void           gt_config_store_set_general_boolean(GtConfigStore *s, const gchar *key, gboolean value);

/* Accès direct, pour les propriétaires des groupes réservés ; ne pas toucher aux groupes [profile:…] ni [general] par ce biais */
GKeyFile      *gt_config_store_get_key_file(GtConfigStore *s);

/* Relit le fichier si son identité (périphérique, inode, taille, date de modification à la nanoseconde) a changé depuis le dernier
 * chargement ou enregistrement : autre instance, modification à la main. Mêmes règles que gt_config_store_load (migration, lecture
 * seule si illisible, fichier absent : magasin vide inscriptible) ; le contenu en mémoire est remplacé. Fichier inchangé : rien n'est
 * fait. FALSE (GError, magasin en lecture seule) seulement si le fichier est devenu illisible. */
gboolean       gt_config_store_reload_if_changed(GtConfigStore *s, GError **error);

/* Écriture atomique en 0600, dossier créé ; refusée en lecture seule ; copie de sauvegarde unique avant la première écriture d'un fichier migré.
 * Si l'écriture échoue, le contenu en mémoire revient à celui d'avant les changements non enregistrés. */
gboolean       gt_config_store_save(GtConfigStore *s, GError **error);

/* true/yes/on/1 et false/no/off/0, casse ignorée ; out inchangé si le texte n'est pas reconnu */
gboolean       gt_config_parse_boolean(const gchar *s, gboolean *out);

#endif
