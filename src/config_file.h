/* Fichier de configuration .gtktermrc : fonctions sans GTK (testées par tests/test_config_file.c). */
#ifndef CONFIG_FILE_H_
#define CONFIG_FILE_H_

#include <glib.h>
#include "parsecfg.h"

/* Caractère d'attente lu dans .gtktermrc : 1-255 tel quel ; les anciennes valeurs négatives (char signé, ex. -23 pour 0xE9)
 * sont ramenées à 0-255 ; 0 et -1 signifient « aucun » (-1). */
gint config_wait_char_from_file(gint valeur);

/* crée config_dir, migre home_dir/.gtktermrc, renvoie le chemin (à libérer) */
gchar *config_file_prepare(const gchar *config_dir, const gchar *home_dir);
/* lecture des profils sans les groupes réservés ; fichier non modifié */
gint config_file_parse_profiles(const gchar *path, cfgStruct cfg[]);
/* indice du profil (casse ignorée) ou -1 */
gint config_file_find_profile(const gchar *name, gint max);
/* window, colors, layout, session (casse ignorée) */
gboolean config_file_is_reserved_name(const gchar *name);
/* 0 si retirée, -1 sinon ; réécriture atomique */
gint config_file_remove_section(const gchar *path, const gchar *section);
/* retire les groupes réservés du fichier, les renvoie (à libérer) */
gchar *config_file_strip_reserved_groups(const gchar *path);
/* les remet en fin de fichier */
void config_file_restore_reserved_groups(const gchar *path, const gchar *extras);

#endif
