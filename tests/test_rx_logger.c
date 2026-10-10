/* Tests de rx_logger.c (cœur) : journal des octets reçus (sous-projet 6a). */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "rx_logger.h"
#include "rx_transform.h"
#include "test_util.h"

#define T1 ((gint64)1000 * G_USEC_PER_SEC)
#define T2 (T1 + 1 * G_USEC_PER_SEC)
#define T3 (T1 + 2 * G_USEC_PER_SEC)
#define T4 (T1 + 3 * G_USEC_PER_SEC)

static gchar *
horodatage(gint64 t)
{
	GString *s = g_string_new(NULL);

	gt_rx_transform_format_timestamp(t, s);
	return g_string_free(s, FALSE);
}

static void
nourrir(GtRxLogger *l, const gchar *texte, gint64 t)
{
	GError *err = NULL;

	g_assert_true(gt_rx_logger_feed(l, (const guint8 *)texte, strlen(texte), t, &err));
	g_assert_no_error(err);
}

static void
verifier_contenu(const gchar *path, const gchar *attendu)
{
	gchar *contenu = NULL;
	gsize n = 0;

	g_assert_true(g_file_get_contents(path, &contenu, &n, NULL));
	g_assert_cmpmem(contenu, n, attendu, strlen(attendu));
	g_free(contenu);
}

static void
test_contenu_transforme(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-journal-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "journal avec espace.log", NULL);
	gchar *ts1 = horodatage(T1), *ts2 = horodatage(T2), *attendu;
	GtRxLogger *l = gt_rx_logger_new();
	GError *err = NULL;

	g_assert_false(gt_rx_logger_is_active(l));
	g_assert_null(gt_rx_logger_get_path(l));
	g_assert_true(gt_rx_logger_start(l, path, TRUE, TRUE, &err));
	g_assert_no_error(err);
	g_assert_true(gt_rx_logger_is_active(l));
	g_assert_false(gt_rx_logger_is_paused(l));
	g_assert_cmpstr(gt_rx_logger_get_path(l), ==, path);

	nourrir(l, "a\n", T1);
	nourrir(l, "b\r\nc", T2);
	/* Chaque réception est vidée sur le disque : lisible sans arrêter le journal */
	attendu = g_strconcat(ts1, "a\r\n", ts2, "b\r\n", ts2, "c", NULL);
	verifier_contenu(path, attendu);
	g_free(attendu);

	/* Options changées en cours de ligne : l'état de ligne est gardé */
	gt_rx_logger_set_options(l, FALSE, FALSE);
	nourrir(l, "d\n", T2);
	attendu = g_strconcat(ts1, "a\r\n", ts2, "b\r\n", ts2, "cd\n", NULL);
	verifier_contenu(path, attendu);
	g_free(attendu);

	gt_rx_logger_free(l);
	g_free(ts1);
	g_free(ts2);
	g_free(path);
	test_rmdir(dir);
	g_free(dir);
}

static void
test_pause_et_reprise(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-journal-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "journal.log", NULL);
	gchar *ts1 = horodatage(T1), *ts3 = horodatage(T3), *ts4 = horodatage(T4), *attendu;
	GtRxLogger *l = gt_rx_logger_new();
	GError *err = NULL;

	g_assert_true(gt_rx_logger_start(l, path, FALSE, TRUE, &err));
	nourrir(l, "abc\nde", T1);

	gt_rx_logger_set_paused(l, TRUE);
	g_assert_true(gt_rx_logger_is_paused(l));
	g_assert_true(gt_rx_logger_is_active(l));
	nourrir(l, "f\ngh", T2);		/* rien d'écrit ; la ligne reçue se termine en cours */
	attendu = g_strconcat(ts1, "abc\n", ts1, "de", NULL);
	verifier_contenu(path, attendu);
	g_free(attendu);

	/* Reprise au milieu d'une ligne : pas d'horodatage répété, la ligne continue */
	gt_rx_logger_set_paused(l, FALSE);
	g_assert_false(gt_rx_logger_is_paused(l));
	nourrir(l, "i\nj", T3);

	/* Pause qui se termine en début de ligne : la reprise horodate la ligne suivante */
	gt_rx_logger_set_paused(l, TRUE);
	nourrir(l, "\n", T3);
	gt_rx_logger_set_paused(l, FALSE);
	nourrir(l, "k", T4);

	attendu = g_strconcat(ts1, "abc\n", ts1, "de", "i\n", ts3, "j", ts4, "k", NULL);
	verifier_contenu(path, attendu);
	g_free(attendu);

	gt_rx_logger_free(l);
	g_free(ts1);
	g_free(ts3);
	g_free(ts4);
	g_free(path);
	test_rmdir(dir);
	g_free(dir);
}

static void
test_clear(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-journal-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "journal.log", NULL);
	gchar *ts2 = horodatage(T2), *attendu;
	GtRxLogger *l = gt_rx_logger_new();
	GError *err = NULL;

	g_assert_true(gt_rx_logger_start(l, path, FALSE, TRUE, &err));
	nourrir(l, "abc\nde", T1);
	g_assert_true(gt_rx_logger_clear(l, &err));
	g_assert_no_error(err);
	verifier_contenu(path, "");
	g_assert_true(gt_rx_logger_is_active(l));

	/* Écrit au début (pas de trou) ; état de ligne remis à zéro : nouvel horodatage */
	nourrir(l, "z\n", T2);
	attendu = g_strconcat(ts2, "z\n", NULL);
	verifier_contenu(path, attendu);
	g_free(attendu);

	gt_rx_logger_free(l);
	g_free(ts2);
	g_free(path);
	test_rmdir(dir);
	g_free(dir);
}

static void
test_stop(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-journal-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "journal.log", NULL);
	GtRxLogger *l = gt_rx_logger_new();
	GError *err = NULL;

	g_assert_true(gt_rx_logger_start(l, path, FALSE, FALSE, &err));
	nourrir(l, "avant\n", T1);
	gt_rx_logger_set_paused(l, TRUE);
	gt_rx_logger_stop(l);
	g_assert_false(gt_rx_logger_is_active(l));
	g_assert_false(gt_rx_logger_is_paused(l));
	g_assert_null(gt_rx_logger_get_path(l));

	/* Inactif : réception ignorée, pause sans effet, clear sans effet */
	nourrir(l, "apres\n", T2);
	gt_rx_logger_set_paused(l, TRUE);
	g_assert_false(gt_rx_logger_is_paused(l));
	g_assert_true(gt_rx_logger_clear(l, &err));
	g_assert_no_error(err);
	gt_rx_logger_stop(l);
	verifier_contenu(path, "avant\n");

	/* Redémarrer sur le même fichier le tronque */
	g_assert_true(gt_rx_logger_start(l, path, FALSE, FALSE, &err));
	verifier_contenu(path, "");
	nourrir(l, "neuf\n", T3);
	/* Un second start ferme le premier journal */
	{
		gchar *autre = g_build_filename(dir, "autre.log", NULL);

		g_assert_true(gt_rx_logger_start(l, autre, FALSE, FALSE, &err));
		nourrir(l, "x", T3);
		verifier_contenu(path, "neuf\n");
		verifier_contenu(autre, "x");
		g_free(autre);
	}
	gt_rx_logger_free(l);
	gt_rx_logger_free(NULL);
	g_free(path);
	test_rmdir(dir);
	g_free(dir);
}

static void
test_ouverture_impossible(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-journal-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "absent", "journal.log", NULL);
	GtRxLogger *l = gt_rx_logger_new();
	GError *err = NULL;

	g_assert_false(gt_rx_logger_start(l, path, FALSE, FALSE, &err));
	g_assert_error(err, G_FILE_ERROR, G_FILE_ERROR_NOENT);
	g_assert_nonnull(strstr(err->message, path));
	g_clear_error(&err);
	g_assert_false(gt_rx_logger_is_active(l));
	g_assert_null(gt_rx_logger_get_path(l));
	gt_rx_logger_free(l);
	g_free(path);
	test_rmdir(dir);
	g_free(dir);
}

static void
test_erreur_ecriture(void)
{
	GtRxLogger *l;
	GError *err = NULL;

	if(!g_file_test("/dev/full", G_FILE_TEST_EXISTS))
	{
		g_test_skip("/dev/full absent");
		return;
	}
	l = gt_rx_logger_new();
	g_assert_true(gt_rx_logger_start(l, "/dev/full", FALSE, FALSE, &err));
	g_assert_no_error(err);

	/* En pause, rien n'est écrit : pas d'erreur */
	gt_rx_logger_set_paused(l, TRUE);
	nourrir(l, "ignore\n", T1);
	g_assert_true(gt_rx_logger_is_active(l));
	gt_rx_logger_set_paused(l, FALSE);

	g_assert_false(gt_rx_logger_feed(l, (const guint8 *)"abc", 3, T2, &err));
	g_assert_error(err, G_FILE_ERROR, G_FILE_ERROR_NOSPC);
	g_assert_nonnull(strstr(err->message, "/dev/full"));
	g_clear_error(&err);
	/* Journal arrêté : la réception suivante ne réessaie pas */
	g_assert_false(gt_rx_logger_is_active(l));
	g_assert_null(gt_rx_logger_get_path(l));
	nourrir(l, "def", T3);
	gt_rx_logger_free(l);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/rx_logger/contenu-transforme", test_contenu_transforme);
	g_test_add_func("/rx_logger/pause-et-reprise", test_pause_et_reprise);
	g_test_add_func("/rx_logger/clear", test_clear);
	g_test_add_func("/rx_logger/stop", test_stop);
	g_test_add_func("/rx_logger/ouverture-impossible", test_ouverture_impossible);
	g_test_add_func("/rx_logger/erreur-ecriture", test_erreur_ecriture);
	return g_test_run();
}
