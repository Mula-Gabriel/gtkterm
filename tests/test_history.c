/* Tests unitaires de l'historique brut (history.c) : ordre, capacité, copie des octets reçus. */

#include <glib.h>
#include <string.h>

#include "history.h"

/* Résumé « RX:ab|TX1:c » (TX suivi du numéro d'origine) ; à libérer avec g_free. */
static void
resumer(const GtRecord *rec, gpointer user_data)
{
	GString *s = user_data;

	if(s->len > 0)
		g_string_append_c(s, '|');
	if(rec->kind == GT_RECORD_RX)
		g_string_append(s, "RX:");
	else
		g_string_append_printf(s, "TX%d:", (gint)rec->origin);
	g_string_append_len(s, (const gchar *)rec->data, rec->len);
}

static gchar *
resume(GtHistory *h)
{
	GString *s = g_string_new(NULL);

	gt_history_foreach(h, resumer, s);
	return g_string_free(s, FALSE);
}

static void
ajouter(GtHistory *h, GtRecordKind kind, GtSendOrigin origin, const gchar *texte)
{
	gt_history_append(h, kind, origin, 0, (const guint8 *)texte, strlen(texte));
}

static void
test_ajout_et_ordre(void)
{
	GtHistory *h = gt_history_new(1024);
	gchar *s;

	ajouter(h, GT_RECORD_RX, GT_SEND_KEYBOARD, "ab");
	ajouter(h, GT_RECORD_TX, GT_SEND_MACRO, "c");
	ajouter(h, GT_RECORD_RX, GT_SEND_KEYBOARD, "de");
	s = resume(h);
	g_assert_cmpstr(s, ==, "RX:ab|TX1:c|RX:de");
	g_assert_cmpuint(gt_history_size(h), ==, 5);
	g_assert_cmpuint(gt_history_count(h), ==, 3);
	g_free(s);
	gt_history_free(h);
}

static void
test_capacite_retire_les_plus_anciens(void)
{
	GtHistory *h = gt_history_new(10);
	gchar *s;

	ajouter(h, GT_RECORD_RX, GT_SEND_KEYBOARD, "12345");
	ajouter(h, GT_RECORD_TX, GT_SEND_HEX, "678");
	ajouter(h, GT_RECORD_RX, GT_SEND_KEYBOARD, "9AB");
	s = resume(h);
	g_assert_cmpstr(s, ==, "TX3:678|RX:9AB");
	g_assert_cmpuint(gt_history_size(h), ==, 6);
	g_free(s);
	gt_history_free(h);
}

static void
test_enregistrement_plus_grand_que_la_capacite(void)
{
	GtHistory *h = gt_history_new(4);
	gchar *s;

	ajouter(h, GT_RECORD_RX, GT_SEND_KEYBOARD, "xy");
	ajouter(h, GT_RECORD_RX, GT_SEND_KEYBOARD, "abcdef");
	s = resume(h);
	g_assert_cmpstr(s, ==, "RX:cdef");
	g_assert_cmpuint(gt_history_size(h), ==, 4);
	g_free(s);
	gt_history_free(h);
}

static void
test_copy_rx_sans_tx(void)
{
	GtHistory *h = gt_history_new(1024);
	GBytes *rx;

	ajouter(h, GT_RECORD_RX, GT_SEND_KEYBOARD, "ab");
	ajouter(h, GT_RECORD_TX, GT_SEND_KEYBOARD, "XY");
	ajouter(h, GT_RECORD_RX, GT_SEND_KEYBOARD, "cd");
	rx = gt_history_copy_rx(h);
	g_assert_cmpmem(g_bytes_get_data(rx, NULL), g_bytes_get_size(rx), "abcd", 4);
	g_bytes_unref(rx);
	gt_history_free(h);
}

static void
test_vider(void)
{
	GtHistory *h = gt_history_new(1024);
	GBytes *rx;
	gchar *s;

	ajouter(h, GT_RECORD_RX, GT_SEND_KEYBOARD, "ab");
	gt_history_clear(h);
	g_assert_cmpuint(gt_history_count(h), ==, 0);
	g_assert_cmpuint(gt_history_size(h), ==, 0);
	rx = gt_history_copy_rx(h);
	g_assert_cmpuint(g_bytes_get_size(rx), ==, 0);
	g_bytes_unref(rx);
	ajouter(h, GT_RECORD_RX, GT_SEND_KEYBOARD, "z");
	s = resume(h);
	g_assert_cmpstr(s, ==, "RX:z");
	g_free(s);
	gt_history_free(h);
}

static void
test_nombreux_petits_enregistrements(void)
{
	GtHistory *h = gt_history_new(1000);
	GBytes *rx;
	const guint8 *p;
	gsize n;

	for(guint i = 0; i < 5000; i++)
	{
		guint8 octet = (guint8)(i % 256);

		gt_history_append(h, GT_RECORD_RX, GT_SEND_KEYBOARD, i, &octet, 1);
	}
	g_assert_cmpuint(gt_history_count(h), ==, 1000);
	g_assert_cmpuint(gt_history_size(h), ==, 1000);
	rx = gt_history_copy_rx(h);
	p = g_bytes_get_data(rx, &n);
	g_assert_cmpuint(n, ==, 1000);
	for(guint i = 0; i < 1000; i++)
		g_assert_cmpuint(p[i], ==, (4000 + i) % 256);
	g_bytes_unref(rx);
	gt_history_free(h);
}

static void
relever_heure(const GtRecord *rec, gpointer user_data)
{
	*(gint64 *)user_data = rec->wall_time_us;
}

static void
test_heure_conservee(void)
{
	GtHistory *h = gt_history_new(16);
	gint64 heure = 0;

	gt_history_append(h, GT_RECORD_RX, GT_SEND_KEYBOARD, 123456789, (const guint8 *)"a", 1);
	gt_history_foreach(h, relever_heure, &heure);
	g_assert_cmpint(heure, ==, 123456789);
	gt_history_free(h);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/history/ajout-et-ordre", test_ajout_et_ordre);
	g_test_add_func("/history/capacite-retire-les-plus-anciens", test_capacite_retire_les_plus_anciens);
	g_test_add_func("/history/enregistrement-plus-grand-que-la-capacite", test_enregistrement_plus_grand_que_la_capacite);
	g_test_add_func("/history/copy-rx-sans-tx", test_copy_rx_sans_tx);
	g_test_add_func("/history/vider", test_vider);
	g_test_add_func("/history/nombreux-petits-enregistrements", test_nombreux_petits_enregistrements);
	g_test_add_func("/history/heure-conservee", test_heure_conservee);
	return g_test_run();
}
