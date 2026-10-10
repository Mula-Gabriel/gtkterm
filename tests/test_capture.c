/* Tests de capture.c (cœur) : captures RAW et ASCII de l'historique (sous-projet 6a). */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "capture.h"
#include "rx_transform.h"
#include "test_util.h"

/* 1er janvier 1970 + 1 j 02:03:04.005 : « [1.02h.03m.04s.005] » */
#define T1 ((((gint64)86400 + 2 * 3600 + 3 * 60 + 4) * G_USEC_PER_SEC) + 5000)
#define T2 (T1 + 1 * G_USEC_PER_SEC)

static void
ajouter(GtHistory *h, GtRecordKind kind, gint64 t, const void *data, gsize len)
{
	gt_history_append(h, kind, GT_SEND_KEYBOARD, t, data, len);
}

/* RX « AB\0\x01\tC\r\n », TX « tx », RX « \x7f\xc3\xa9\x1b[0m\n » */
static GtHistory *
historique_exemple(void)
{
	GtHistory *h = gt_history_new(1024);

	ajouter(h, GT_RECORD_RX, T1, "AB\0\x01\tC\r\n", 8);
	ajouter(h, GT_RECORD_TX, T1, "tx", 2);
	ajouter(h, GT_RECORD_RX, T2, "\x7f\xc3\xa9\x1b[0m\n", 8);
	return h;
}

static gchar *
lire(const gchar *path, gsize *len)
{
	gchar *contenu = NULL;
	GError *err = NULL;

	g_assert_true(g_file_get_contents(path, &contenu, len, &err));
	g_assert_no_error(err);
	return contenu;
}

static gchar *
horodatage(gint64 t)
{
	GString *s = g_string_new(NULL);

	gt_rx_transform_format_timestamp(t, s);
	return g_string_free(s, FALSE);
}

static void
test_raw_intact(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-capture-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "brut.bin", NULL);
	GtHistory *h = historique_exemple();
	GError *err = NULL;
	gchar *contenu;
	gsize n;

	g_assert_true(gt_capture_save_raw(h, path, &err));
	g_assert_no_error(err);
	contenu = lire(path, &n);
	g_assert_cmpmem(contenu, n, "AB\0\x01\tC\r\n\x7f\xc3\xa9\x1b[0m\n", 16);
	g_free(contenu);

	/* Historique vide : fichier vide, remplacé */
	gt_history_clear(h);
	g_assert_true(gt_capture_save_raw(h, path, &err));
	g_assert_no_error(err);
	contenu = lire(path, &n);
	g_assert_cmpuint(n, ==, 0);
	g_free(contenu);

	gt_history_free(h);
	g_free(path);
	test_rmdir(dir);
	g_free(dir);
}

static void
test_ascii_filtre(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-capture-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "texte.txt", NULL);
	GtHistory *h = historique_exemple();
	GError *err = NULL;
	gchar *contenu;
	gsize n;

	g_assert_true(gt_capture_save_ascii(h, FALSE, FALSE, path, &err));
	g_assert_no_error(err);
	contenu = lire(path, &n);
	/* NUL, 0x01, DEL et ESC retirés ; TAB, CR, LF et l'UTF-8 gardés ; l'envoi « tx » absent */
	g_assert_cmpmem(contenu, n, "AB\tC\r\n\xc3\xa9[0m\n", 12);
	g_free(contenu);
	gt_history_free(h);
	g_free(path);
	test_rmdir(dir);
	g_free(dir);
}

static void
test_ascii_crlf_et_horodatage(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-capture-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "texte.txt", NULL);
	GtHistory *h = gt_history_new(1024);
	GError *err = NULL;
	gchar *contenu, *ts1, *ts2, *attendu;
	gsize n;

	ajouter(h, GT_RECORD_RX, T1, "a\rb\n", 4);
	ajouter(h, GT_RECORD_TX, T1, "x\n", 2);
	ajouter(h, GT_RECORD_RX, T2, "c\n", 2);
	ts1 = horodatage(T1);
	ts2 = horodatage(T2);
	g_assert_cmpstr(ts1, ==, "[1.02h.03m.04s.005] ");

	/* CR/LF auto seul : CR suivi d'un caractère → CR LF ; LF seul → CR LF */
	g_assert_true(gt_capture_save_ascii(h, TRUE, FALSE, path, &err));
	g_assert_no_error(err);
	contenu = lire(path, &n);
	g_assert_cmpstr(contenu, ==, "a\r\nb\r\nc\r\n");
	g_free(contenu);

	/* Horodatage seul : en tête de chaque ligne, à l'heure de réception */
	g_assert_true(gt_capture_save_ascii(h, FALSE, TRUE, path, &err));
	g_assert_no_error(err);
	contenu = lire(path, &n);
	attendu = g_strconcat(ts1, "a\rb\n", ts2, "c\n", NULL);
	g_assert_cmpstr(contenu, ==, attendu);
	g_free(attendu);
	g_free(contenu);

	/* Les deux */
	g_assert_true(gt_capture_save_ascii(h, TRUE, TRUE, path, &err));
	g_assert_no_error(err);
	contenu = lire(path, &n);
	attendu = g_strconcat(ts1, "a\r\n", ts1, "b\r\n", ts2, "c\r\n", NULL);
	g_assert_cmpstr(contenu, ==, attendu);
	g_free(attendu);
	g_free(contenu);

	g_free(ts1);
	g_free(ts2);
	gt_history_free(h);
	g_free(path);
	test_rmdir(dir);
	g_free(dir);
}

static void
test_chemin_impossible(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-capture-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "absent", "capture.txt", NULL);
	gchar *absent = g_build_filename(dir, "absent", NULL);
	GtHistory *h = historique_exemple();
	GError *err = NULL;

	g_assert_false(gt_capture_save_raw(h, path, &err));
	g_assert_error(err, G_FILE_ERROR, G_FILE_ERROR_NOENT);
	g_assert_nonnull(strstr(err->message, path));
	g_clear_error(&err);
	g_assert_false(gt_capture_save_ascii(h, TRUE, TRUE, path, &err));
	g_assert_error(err, G_FILE_ERROR, G_FILE_ERROR_NOENT);
	g_clear_error(&err);
	g_assert_false(g_file_test(absent, G_FILE_TEST_EXISTS));
	g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));

	gt_history_free(h);
	g_free(absent);
	g_free(path);
	test_rmdir(dir);
	g_free(dir);
}

/* Disque plein : l'erreur n'apparaît qu'à la fermeture (flux tamponné) et doit être rapportée */
static void
test_disque_plein(void)
{
	GtHistory *h = historique_exemple();
	GError *err = NULL;

	if(!g_file_test("/dev/full", G_FILE_TEST_EXISTS))
	{
		g_test_skip("/dev/full absent");
		gt_history_free(h);
		return;
	}
	g_assert_false(gt_capture_save_raw(h, "/dev/full", &err));
	g_assert_error(err, G_FILE_ERROR, G_FILE_ERROR_NOSPC);
	g_clear_error(&err);
	g_assert_false(gt_capture_save_ascii(h, FALSE, FALSE, "/dev/full", &err));
	g_assert_error(err, G_FILE_ERROR, G_FILE_ERROR_NOSPC);
	g_clear_error(&err);
	gt_history_free(h);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/capture/raw-intact", test_raw_intact);
	g_test_add_func("/capture/ascii-filtre", test_ascii_filtre);
	g_test_add_func("/capture/ascii-crlf-et-horodatage", test_ascii_crlf_et_horodatage);
	g_test_add_func("/capture/chemin-impossible", test_chemin_impossible);
	g_test_add_func("/capture/disque-plein", test_disque_plein);
	return g_test_run();
}
