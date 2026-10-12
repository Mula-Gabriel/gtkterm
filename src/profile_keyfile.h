/* Profil ↔ groupe GKeyFile (6c, extrait de config_store.c) : clés de [profile:…] de .gtktermrc et de [tab:N] d'un projet. */
#ifndef PROFILE_KEYFILE_H_
#define PROFILE_KEYFILE_H_

#include <glib.h>

#include "profile.h"

/* Écrit toutes les clés du profil dans group (macros_file : écrite si non NULL, sinon retirée) */
void gt_profile_to_key_file(const GtProfile *p, GKeyFile *kf, const gchar *group);
/* Lecture tolérante : p déjà rempli (défauts) ; une clé absente ou illisible garde la valeur de p */
void gt_profile_from_key_file(GKeyFile *kf, const gchar *group, GtProfile *p);
/* Booléen tolérant (true/yes/on/1, false/no/off/0) ; *field inchangé si absent ou illisible */
void gt_key_file_read_bool(GKeyFile *kf, const gchar *group, const gchar *key, gboolean *field);
/* Entier tolérant (blancs rognés, base 10, dans gint) ; *field inchangé sinon */
void gt_key_file_read_int(GKeyFile *kf, const gchar *group, const gchar *key, gint *field);

#endif
