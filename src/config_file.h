/* Fichier de configuration .gtktermrc : préparation du chemin et décodage du caractère d'attente (sans GTK). */
#ifndef CONFIG_FILE_H_
#define CONFIG_FILE_H_

#include <glib.h>

/* Caractère d'attente lu dans .gtktermrc : 1-255 tel quel ; les anciennes valeurs négatives (char signé, ex. -23 pour 0xE9)
 * sont ramenées à 0-255 ; 0 et -1 signifient « aucun » (-1). */
gint config_wait_char_from_file(gint valeur);

/* crée config_dir, migre home_dir/.gtktermrc, renvoie le chemin (à libérer) */
gchar *config_file_prepare(const gchar *config_dir, const gchar *home_dir);

#endif
