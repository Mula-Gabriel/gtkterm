/* Identité d'un fichier (6c, spec §3.2-15) : périphérique, inode, taille, date de modification à la nanoseconde. L'écriture atomique (fichier
 * temporaire renommé) change l'inode ; une modification sur place change la taille ou la date. */
#ifndef FILE_STAMP_H_
#define FILE_STAMP_H_

#include <glib.h>

typedef struct
{
	gboolean exists;	/* FALSE : fichier absent ou stat impossible (tous les autres champs à 0) */
	guint64 dev, ino;
	gint64 size;
	gint64 mtime_sec, mtime_nsec;
} GtFileStamp;

void     gt_file_stamp_take(const gchar *path, GtFileStamp *out);
gboolean gt_file_stamp_equal(const GtFileStamp *a, const GtFileStamp *b);	/* deux absents : égaux */
gboolean gt_file_stamp_changed(const gchar *path, const GtFileStamp *known);	/* identité actuelle de path ≠ known */

#endif
