/* Tests de file_stamp.c : identité d'un fichier (absent, réécrit atomiquement, modifié sur place, inchangé). */

#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>

#include "file_stamp.h"

static void test_identite(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-stamp-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "f", NULL);
	GtFileStamp absent, a, b;
	FILE *f;

	gt_file_stamp_take(path, &absent);
	g_assert_false(absent.exists);
	g_assert_false(gt_file_stamp_changed(path, &absent));
	g_assert_true(g_file_set_contents(path, "un", -1, NULL));
	g_assert_true(gt_file_stamp_changed(path, &absent));
	gt_file_stamp_take(path, &a);
	g_assert_true(a.exists);
	g_assert_false(gt_file_stamp_changed(path, &a));
	g_assert_true(g_file_set_contents(path, "un", -1, NULL));	/* atomique : nouvel inode, même taille */
	g_assert_true(gt_file_stamp_changed(path, &a));
	gt_file_stamp_take(path, &b);
	f = fopen(path, "a");
	fputs("deux", f);
	fclose(f);
	g_assert_true(gt_file_stamp_changed(path, &b));	/* sur place : taille */
	g_assert_true(gt_file_stamp_equal(&absent, &absent));
	g_assert_false(gt_file_stamp_equal(&a, &absent));
	g_unlink(path);
	g_rmdir(dir);
	g_free(path);
	g_free(dir);
}

int main(int argc, char *argv[])
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/file_stamp/identite", test_identite);
	return g_test_run();
}
