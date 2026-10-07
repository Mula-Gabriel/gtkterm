/* Tests unitaires de la transformation à la lecture (rx_transform.c) : CR/LF automatique et horodatage. */

#include <glib.h>
#include <string.h>

#include "rx_transform.h"

/* 1 jour 02:03:04.005 -> "[1.02h.03m.04s.005] " ; T2 = une seconde plus tard */
#define T1 ((((gint64)1 * 86400 + 2 * 3600 + 3 * 60 + 4) * G_USEC_PER_SEC) + 5000)
#define T2 (T1 + G_USEC_PER_SEC)
#define H1 "[1.02h.03m.04s.005] "
#define H2 "[1.02h.03m.05s.005] "

static gchar *
nourrir(GtRxTransform *tr, const gchar *data, gsize len, gint64 heure)
{
	GString *out = g_string_new(NULL);

	gt_rx_transform_feed(tr, (const guint8 *)data, len, heure, out);
	return g_string_free(out, FALSE);
}

static void
verifier(GtRxTransform *tr, const gchar *entree, gint64 heure, const gchar *attendu)
{
	gchar *s = nourrir(tr, entree, strlen(entree), heure);

	g_assert_cmpstr(s, ==, attendu);
	g_free(s);
}

static void
test_format_horodatage(void)
{
	GString *s = g_string_new(NULL);

	gt_rx_transform_format_timestamp(T1, s);
	g_assert_cmpstr(s->str, ==, H1);
	g_string_free(s, TRUE);
}

static void
test_crlf_cas(void)
{
	static const struct { const gchar *entree; const gchar *attendu; } cas[] = {
		{ "a\rb", "a\r\nb" },
		{ "a\nb", "a\r\nb" },
		{ "a\r\nb", "a\r\nb" },
		{ "a\r\rb", "a\r\n\r\nb" },
	};

	for(guint i = 0; i < G_N_ELEMENTS(cas); i++)
	{
		GtRxTransform *tr = gt_rx_transform_new(TRUE, FALSE);

		verifier(tr, cas[i].entree, T1, cas[i].attendu);
		gt_rx_transform_free(tr);
	}
}

static void
test_crlf_coupe_entre_deux_appels(void)
{
	GtRxTransform *tr = gt_rx_transform_new(TRUE, FALSE);

	verifier(tr, "a\r", T1, "a\r");
	verifier(tr, "b", T1, "\nb");
	verifier(tr, "c\r", T1, "c\r");
	verifier(tr, "\nd", T1, "\nd");
	gt_rx_transform_free(tr);
}

static void
test_horodatage_debut_de_ligne(void)
{
	GtRxTransform *tr = gt_rx_transform_new(FALSE, TRUE);

	verifier(tr, "x\ny", T1, H1 "x\n" H1 "y");
	gt_rx_transform_free(tr);
}

static void
test_horodatage_ligne_coupee(void)
{
	GtRxTransform *tr = gt_rx_transform_new(FALSE, TRUE);

	verifier(tr, "ab\n", T1, H1 "ab\n");
	verifier(tr, "cd", T2, H2 "cd");
	verifier(tr, "ef", T2, "ef");
	gt_rx_transform_free(tr);
}

static void
test_crlf_et_horodatage(void)
{
	GtRxTransform *tr = gt_rx_transform_new(TRUE, TRUE);

	verifier(tr, "a\rb", T1, H1 "a\r\n" H1 "b");
	gt_rx_transform_free(tr);
}

static void
test_options_desactivees(void)
{
	GtRxTransform *tr = gt_rx_transform_new(FALSE, FALSE);
	gchar *s = nourrir(tr, "\x00\xff\r\n", 4, T1);

	g_assert_cmpmem(s, 4, "\x00\xff\r\n", 4);
	g_free(s);
	/* Fin de ligne vue sans option : l'horodatage activé ensuite part de la ligne suivante */
	gt_rx_transform_set_options(tr, FALSE, TRUE);
	verifier(tr, "z", T1, H1 "z");
	gt_rx_transform_free(tr);
}

static void
test_reinitialiser(void)
{
	GtRxTransform *tr = gt_rx_transform_new(TRUE, FALSE);

	verifier(tr, "a\r", T1, "a\r");
	gt_rx_transform_reset(tr);
	verifier(tr, "b", T1, "b");
	gt_rx_transform_free(tr);
}

static void
test_options_changees_en_cours(void)
{
	GtRxTransform *tr = gt_rx_transform_new(TRUE, FALSE);

	verifier(tr, "a\r", T1, "a\r");
	gt_rx_transform_set_options(tr, TRUE, TRUE);
	verifier(tr, "b", T2, "\n" H2 "b");
	gt_rx_transform_free(tr);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/rx_transform/format-horodatage", test_format_horodatage);
	g_test_add_func("/rx_transform/crlf-cas", test_crlf_cas);
	g_test_add_func("/rx_transform/crlf-coupe-entre-deux-appels", test_crlf_coupe_entre_deux_appels);
	g_test_add_func("/rx_transform/horodatage-debut-de-ligne", test_horodatage_debut_de_ligne);
	g_test_add_func("/rx_transform/horodatage-ligne-coupee", test_horodatage_ligne_coupee);
	g_test_add_func("/rx_transform/crlf-et-horodatage", test_crlf_et_horodatage);
	g_test_add_func("/rx_transform/options-desactivees", test_options_desactivees);
	g_test_add_func("/rx_transform/reinitialiser", test_reinitialiser);
	g_test_add_func("/rx_transform/options-changees-en-cours", test_options_changees_en_cours);
	return g_test_run();
}
