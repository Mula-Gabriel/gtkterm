/* Tests de hex_input.c (cœur) : analyse de la saisie hexadécimale et historique de la barre d'envoi (sous-projet 6a). */

#include <glib.h>
#include <string.h>

#include "hex_input.h"

static void
verifier_analyse(const gchar *texte, const void *attendu, gsize taille)
{
	GError *err = NULL;
	GBytes *b = gt_hex_input_parse(texte, &err);
	gsize n = 0;
	const guint8 *p;

	g_assert_no_error(err);
	g_assert_nonnull(b);
	p = g_bytes_get_data(b, &n);
	g_assert_cmpuint(n, ==, taille);
	if(taille > 0)
		g_assert_cmpmem(p, n, attendu, taille);
	g_bytes_unref(b);
}

static void
verifier_refus(const gchar *texte, const gchar *jeton)
{
	GError *err = NULL;
	GBytes *b = gt_hex_input_parse(texte, &err);

	g_assert_null(b);
	g_assert_error(err, GT_HEX_INPUT_ERROR, GT_HEX_INPUT_ERROR_INVALID);
	g_assert_cmpstr(err->message, ==, jeton);
	g_error_free(err);
}

static void
test_valides(void)
{
	verifier_analyse("41 42;43", "ABC", 3);
	verifier_analyse("a", "\x0a", 1);
	verifier_analyse("0A;0b", "\x0a\x0b", 2);
	verifier_analyse("  ff ;; 00\t7f ", "\xff\x00\x7f", 3);
	verifier_analyse("1 2 3", "\x01\x02\x03", 3);
}

static void
test_vides(void)
{
	verifier_analyse("", NULL, 0);
	verifier_analyse("   ", NULL, 0);
	verifier_analyse(";", NULL, 0);
	verifier_analyse(" ; ;; ", NULL, 0);
}

static void
test_refus(void)
{
	verifier_refus("1FF", "1FF");
	verifier_refus("-1", "-1");
	verifier_refus("G1", "G1");
	verifier_refus("4 1x", "1x");
	verifier_refus("41 0x42", "0x42");
	verifier_refus("41;é", "é");
	/* Le refus ne dépend pas d'un éventuel NULL pour l'erreur */
	g_assert_null(gt_hex_input_parse("zz", NULL));
}

static void
test_historique_navigation(void)
{
	GtHexHistory *h = gt_hex_history_new(50);

	g_assert_null(gt_hex_history_previous(h));
	g_assert_cmpstr(gt_hex_history_next(h), ==, "");
	gt_hex_history_add(h, "01");
	gt_hex_history_add(h, "02");
	gt_hex_history_add(h, "03");
	g_assert_cmpstr(gt_hex_history_previous(h), ==, "03");
	g_assert_cmpstr(gt_hex_history_previous(h), ==, "02");
	g_assert_cmpstr(gt_hex_history_previous(h), ==, "01");
	g_assert_null(gt_hex_history_previous(h));
	g_assert_cmpstr(gt_hex_history_next(h), ==, "02");
	g_assert_cmpstr(gt_hex_history_next(h), ==, "03");
	g_assert_cmpstr(gt_hex_history_next(h), ==, "");
	g_assert_cmpstr(gt_hex_history_next(h), ==, "");
	g_assert_cmpstr(gt_hex_history_previous(h), ==, "03");
	gt_hex_history_free(h);
	gt_hex_history_free(NULL);
}

static void
test_historique_curseur_remis_a_la_fin(void)
{
	GtHexHistory *h = gt_hex_history_new(50);

	gt_hex_history_add(h, "01");
	gt_hex_history_add(h, "02");
	g_assert_cmpstr(gt_hex_history_previous(h), ==, "02");
	g_assert_cmpstr(gt_hex_history_previous(h), ==, "01");
	gt_hex_history_add(h, "04");
	g_assert_cmpstr(gt_hex_history_previous(h), ==, "04");
	/* Un ajout ignoré (vide) remet aussi le curseur à la fin */
	g_assert_cmpstr(gt_hex_history_previous(h), ==, "02");
	gt_hex_history_add(h, "");
	g_assert_cmpstr(gt_hex_history_previous(h), ==, "04");
	gt_hex_history_free(h);
}

static void
test_historique_vide_et_doublon(void)
{
	GtHexHistory *h = gt_hex_history_new(50);

	gt_hex_history_add(h, "");
	gt_hex_history_add(h, "   ");
	gt_hex_history_add(h, NULL);
	g_assert_null(gt_hex_history_previous(h));
	gt_hex_history_add(h, "41");
	gt_hex_history_add(h, "41");
	gt_hex_history_add(h, "42");
	gt_hex_history_add(h, "41");
	g_assert_cmpstr(gt_hex_history_previous(h), ==, "41");
	g_assert_cmpstr(gt_hex_history_previous(h), ==, "42");
	g_assert_cmpstr(gt_hex_history_previous(h), ==, "41");
	g_assert_null(gt_hex_history_previous(h));
	gt_hex_history_free(h);
}

static void
test_historique_max(void)
{
	GtHexHistory *h = gt_hex_history_new(3);
	gchar texte[8];

	for(guint i = 1; i <= 5; i++)
	{
		g_snprintf(texte, sizeof(texte), "%02X", i);
		gt_hex_history_add(h, texte);
	}
	g_assert_cmpstr(gt_hex_history_previous(h), ==, "05");
	g_assert_cmpstr(gt_hex_history_previous(h), ==, "04");
	g_assert_cmpstr(gt_hex_history_previous(h), ==, "03");
	g_assert_null(gt_hex_history_previous(h));
	gt_hex_history_free(h);

	/* max 0 : au moins une entrée gardée */
	h = gt_hex_history_new(0);
	gt_hex_history_add(h, "01");
	gt_hex_history_add(h, "02");
	g_assert_cmpstr(gt_hex_history_previous(h), ==, "02");
	g_assert_null(gt_hex_history_previous(h));
	gt_hex_history_free(h);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/hex_input/valides", test_valides);
	g_test_add_func("/hex_input/vides", test_vides);
	g_test_add_func("/hex_input/refus", test_refus);
	g_test_add_func("/hex_input/historique-navigation", test_historique_navigation);
	g_test_add_func("/hex_input/historique-curseur-remis-a-la-fin", test_historique_curseur_remis_a_la_fin);
	g_test_add_func("/hex_input/historique-vide-et-doublon", test_historique_vide_et_doublon);
	g_test_add_func("/hex_input/historique-max", test_historique_max);
	return g_test_run();
}
