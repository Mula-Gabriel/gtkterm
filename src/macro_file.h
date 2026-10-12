/* macro_file.h — lecture et écriture du fichier de macros (format v2 échappé par GKeyFile, lecture de l'ancien format,
 * copie <fichier>.avant-migration avant d'écraser tout fichier qui n'est pas un v2 lisible). */

#ifndef MACRO_FILE_H_
#define MACRO_FILE_H_

#include <glib.h>

#include "macro_set.h"

/* Fichier absent : FALSE + G_FILE_ERROR_NOENT. Fichier vide ou sans groupe : jeu vide. Fichier avec des groupes mais aucun groupe
 * connu : FALSE + G_KEY_FILE_ERROR_GROUP_NOT_FOUND. *set_out n'est écrit qu'en cas de succès. */
gboolean gt_macro_file_load(const gchar *path, GtMacroSet **set_out, GError **error);
/* Fichier présent, non vide, pas un v2 lisible : copié d'abord dans <fichier>.avant-migration ; si cette copie existe déjà avec un
 * autre contenu, FALSE + G_FILE_ERROR_EXIST et rien n'est écrit. */
gboolean gt_macro_file_save(const GtMacroSet *set, const gchar *path, GError **error);

/* Jeu ↔ groupes de kf préfixés (6c) : prefix "" → [macro:N] et [list:K] (fichier .ini) ; "tab:0:" → [tab:0:macro:N]… Sans en-tête.
 * Écriture : groupes ajoutés dans l'ordre (macros 1…n, puis listes 1…k) ; groupes existants de même nom remplacés. */
void        gt_macro_set_to_key_file(const GtMacroSet *set, GKeyFile *kf, const gchar *prefix);
/* Lecture tolérante, jamais NULL : groupes « prefix macro:* » et « prefix list:* » dans l'ordre du fichier ; un échappement invalide garde la
 * valeur brute (g_warning citant origin et le groupe) ; une liste sans entrée existe quand même. */
GtMacroSet *gt_macro_set_from_key_file(GKeyFile *kf, const gchar *prefix, const gchar *origin);

#endif
