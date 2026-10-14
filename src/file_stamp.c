/* Identité d'un fichier (voir file_stamp.h). */
#include <sys/stat.h>

#include "file_stamp.h"

void gt_file_stamp_take(const gchar *path, GtFileStamp *out)
{
	struct stat st;

	*out = (GtFileStamp){ 0 };
	if(path == NULL || stat(path, &st) != 0)
		return;
	out->exists = TRUE;
	out->dev = (guint64)st.st_dev;
	out->ino = (guint64)st.st_ino;
	out->size = (gint64)st.st_size;
	out->mtime_sec = (gint64)st.st_mtim.tv_sec;
	out->mtime_nsec = (gint64)st.st_mtim.tv_nsec;
}

gboolean gt_file_stamp_equal(const GtFileStamp *a, const GtFileStamp *b)
{
	if(a->exists != b->exists)
		return FALSE;
	if(!a->exists)
		return TRUE;
	return a->dev == b->dev && a->ino == b->ino && a->size == b->size && a->mtime_sec == b->mtime_sec && a->mtime_nsec == b->mtime_nsec;
}

gboolean gt_file_stamp_changed(const gchar *path, const GtFileStamp *known)
{
	GtFileStamp now;

	gt_file_stamp_take(path, &now);
	return !gt_file_stamp_equal(&now, known);
}
