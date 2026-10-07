/* Tests unitaires du rendu des vues (view_render.c) : ASCII, hexadécimal, écho, rejeu de l'historique. */

#include <glib.h>
#include <string.h>

#include "history.h"
#include "view_render.h"

#define BLEU "\033[0;34m"
#define FIN "\033[0m"

static GtViewOptions
options(GtViewMode mode, gboolean echo)
{
	GtViewOptions o = { mode, echo, FALSE, FALSE, 8, FALSE };

	return o;
}

static gchar *
rx(GtViewRender *r, const gchar *data, gsize len)
{
	GString *out = g_string_new(NULL);

	gt_view_render_rx(r, (const guint8 *)data, len, 0, out);
	return g_string_free(out, FALSE);
}

static gchar *
tx(GtViewRender *r, const gchar *data, GtSendOrigin origine)
{
	GString *out = g_string_new(NULL);

	gt_view_render_tx(r, (const guint8 *)data, strlen(data), origine, out);
	return g_string_free(out, FALSE);
}

#define VERIFIER(expr, attendu) do { gchar *s_ = (expr); g_assert_cmpstr(s_, ==, (attendu)); g_free(s_); } while(0)
#define VERIFIER_DEBUT(expr, debut) do { gchar *s_ = (expr); if(!g_str_has_prefix(s_, (debut))) g_error("debut inattendu : %s", g_strescape(s_, NULL)); g_free(s_); } while(0)

static void
test_ascii_sans_option(void)
{
	GtViewOptions o = options(GT_VIEW_ASCII, FALSE);
	GtViewRender *r = gt_view_render_new(&o);

	VERIFIER(rx(r, "ab\r\n", 4), "ab\r\n");
	gt_view_render_free(r);
}

static void
test_ascii_echo_desactive(void)
{
	GtViewOptions o = options(GT_VIEW_ASCII, FALSE);
	GtViewRender *r = gt_view_render_new(&o);

	VERIFIER(tx(r, "x", GT_SEND_KEYBOARD), "");
	VERIFIER(tx(r, "y", GT_SEND_MACRO), "");
	gt_view_render_free(r);
}

static void
test_ascii_frappe_dans_la_ligne(void)
{
	GtViewOptions o = options(GT_VIEW_ASCII, TRUE);
	GtViewRender *r = gt_view_render_new(&o);

	VERIFIER(rx(r, "abc", 3), "abc");
	VERIFIER(tx(r, "d", GT_SEND_KEYBOARD), BLEU "d" FIN);
	gt_view_render_free(r);
}

static void
test_ascii_envoi_en_bloc_sur_sa_ligne(void)
{
	GtViewOptions o = options(GT_VIEW_ASCII, TRUE);
	GtViewRender *r = gt_view_render_new(&o);

	VERIFIER(rx(r, "abc", 3), "abc");
	VERIFIER(tx(r, "CMD\r\n", GT_SEND_MACRO), "\r\n" BLEU "> CMD" FIN "\r\n");
	VERIFIER(rx(r, "OK", 2), "OK");
	gt_view_render_free(r);
}

static void
test_ascii_envoi_en_bloc_en_debut_de_ligne(void)
{
	GtViewOptions o = options(GT_VIEW_ASCII, TRUE);
	GtViewRender *r = gt_view_render_new(&o);

	VERIFIER(rx(r, "abc\r\n", 5), "abc\r\n");
	VERIFIER(tx(r, "X", GT_SEND_HEX), BLEU "> X" FIN "\r\n");
	gt_view_render_free(r);
}

static void
test_ascii_bloc_apres_frappe_cr(void)
{
	GtViewOptions o = options(GT_VIEW_ASCII, TRUE);
	GtViewRender *r = gt_view_render_new(&o);

	VERIFIER(tx(r, "a\r", GT_SEND_KEYBOARD), BLEU "a\r" FIN);
	VERIFIER(tx(r, "M", GT_SEND_LUA), "\r\n" BLEU "> M" FIN "\r\n");
	gt_view_render_free(r);
}

static void
test_ascii_transformation_appliquee(void)
{
	GtViewOptions o = options(GT_VIEW_ASCII, FALSE);
	GtViewRender *r;

	o.crlf_auto = TRUE;
	r = gt_view_render_new(&o);
	VERIFIER(rx(r, "a\nb", 3), "a\r\nb");
	gt_view_render_free(r);
}

static void
test_hexa_cellule(void)
{
	GtViewOptions o = options(GT_VIEW_HEX, FALSE);
	GtViewRender *r = gt_view_render_new(&o);

	/* colonne 0, 8 octets par ligne : avance = (8 - 0) * 3 + 0 + 2 = 26 */
	VERIFIER(rx(r, "A", 1), "41 \033[26CA\033[27D");
	gt_view_render_free(r);
}

static void
test_hexa_separateur(void)
{
	GtViewOptions o = options(GT_VIEW_HEX, FALSE);
	GtViewRender *r = gt_view_render_new(&o);
	gchar *s = rx(r, "ABCD", 4);

	/* colonne 3 = bytes_per_line / 2 - 1 : avance = (8 - 3) * 3 + 3 + 2 = 20, puis « - » */
	g_assert_nonnull(strstr(s, "44 \033[20CD\033[21D- "));
	g_free(s);
	gt_view_render_free(r);
}

static void
test_hexa_colonne_ascii(void)
{
	GtViewOptions o = options(GT_VIEW_HEX, FALSE);
	GtViewRender *r = gt_view_render_new(&o);
	gchar *s = rx(r, "\x01\x7f\xff", 3);

	g_assert_nonnull(strstr(s, "01 \033[26C.\033[27D"));
	g_assert_nonnull(strstr(s, "7F \033[24C.\033[25D"));
	g_assert_nonnull(strstr(s, "FF \033[22C.\033[23D"));
	g_free(s);
	gt_view_render_free(r);
}

static void
test_hexa_ligne_complete(void)
{
	GtViewOptions o = options(GT_VIEW_HEX, FALSE);
	GtViewRender *r;
	gchar *s;

	o.show_index = TRUE;
	r = gt_view_render_new(&o);
	s = rx(r, "ABCDEFGH", 8);
	g_assert_true(g_str_has_prefix(s, "     0: 41 "));
	g_assert_true(g_str_has_suffix(s, "\r\n"));
	g_free(s);
	VERIFIER_DEBUT(rx(r, "I", 1), "     8: 49 ");
	gt_view_render_free(r);
}

static void
test_hexa_index_non_decale_par_l_echo(void)
{
	GtViewOptions o = options(GT_VIEW_HEX, TRUE);
	GtViewRender *r;
	gchar *s;

	o.show_index = TRUE;
	r = gt_view_render_new(&o);
	VERIFIER_DEBUT(rx(r, "ABC", 3), "     0: 41 ");
	s = tx(r, "Z", GT_SEND_MACRO);
	g_assert_true(g_str_has_prefix(s, "\r\n" BLEU "     >  5A "));
	g_assert_true(g_str_has_suffix(s, "\r\n" FIN));
	g_free(s);
	VERIFIER_DEBUT(rx(r, "DEF", 3), "     3: 44 ");
	gt_view_render_free(r);
}

static void
test_hexa_echo_desactive(void)
{
	GtViewOptions o = options(GT_VIEW_HEX, FALSE);
	GtViewRender *r;

	o.show_index = TRUE;
	r = gt_view_render_new(&o);
	g_free(rx(r, "AB", 2));
	VERIFIER(tx(r, "Z", GT_SEND_MACRO), "");
	/* la ligne reçue continue en colonne 2, sans index ni saut de ligne */
	VERIFIER_DEBUT(rx(r, "C", 1), "43 ");
	gt_view_render_free(r);
}

static void
test_reinitialiser_change_de_vue(void)
{
	GtViewOptions o = options(GT_VIEW_HEX, FALSE);
	GtViewRender *r;

	o.show_index = TRUE;
	r = gt_view_render_new(&o);
	g_free(rx(r, "AB", 2));
	gt_view_render_reset(r, &o);
	VERIFIER_DEBUT(rx(r, "C", 1), "     0: 43 ");
	o.mode = GT_VIEW_ASCII;
	gt_view_render_reset(r, &o);
	VERIFIER(rx(r, "C", 1), "C");
	gt_view_render_free(r);
}

/* ---- Rejeu de l'historique = rendu en direct ---------------------------- */

typedef struct
{
	GtViewRender *r;
	GString *out;
} Rejeu;

static void
rejouer(const GtRecord *rec, gpointer user_data)
{
	Rejeu *j = user_data;

	gt_view_render_record(j->r, rec, j->out);
}

static void
comparer_rejeu(GtViewOptions *o, GtHistory *h, GString *direct)
{
	Rejeu j;

	j.r = gt_view_render_new(o);
	j.out = g_string_new(NULL);
	gt_history_foreach(h, rejouer, &j);
	g_assert_cmpuint(j.out->len, ==, direct->len);
	g_assert_true(memcmp(j.out->str, direct->str, direct->len) == 0);
	g_string_free(j.out, TRUE);
	gt_view_render_free(j.r);
}

static void
ajouter_et_rendre(GtViewRender *r, GtHistory *h, GString *direct, GtRecordKind kind, GtSendOrigin origine, gint64 heure, const gchar *texte)
{
	GtRecord rec = { kind, origine, heure, (const guint8 *)texte, strlen(texte) };

	gt_history_append(h, kind, origine, heure, rec.data, rec.len);
	gt_view_render_record(r, &rec, direct);
}

static void
test_rejeu_egal_direct(void)
{
	for(gint mode = GT_VIEW_ASCII; mode <= GT_VIEW_HEX; mode++)
	{
		GtViewOptions o = { (GtViewMode)mode, TRUE, TRUE, TRUE, 8, TRUE };
		GtViewRender *r = gt_view_render_new(&o);
		GtHistory *h = gt_history_new(GT_HISTORY_DEFAULT_CAPACITY);
		GString *direct = g_string_new(NULL);

		ajouter_et_rendre(r, h, direct, GT_RECORD_RX, GT_SEND_KEYBOARD, 1000000, "L1\n");
		ajouter_et_rendre(r, h, direct, GT_RECORD_TX, GT_SEND_KEYBOARD, 2000000, "k");
		ajouter_et_rendre(r, h, direct, GT_RECORD_RX, GT_SEND_KEYBOARD, 3000000, "L2\r");
		ajouter_et_rendre(r, h, direct, GT_RECORD_TX, GT_SEND_MACRO, 4000000, "M\r\n");
		ajouter_et_rendre(r, h, direct, GT_RECORD_RX, GT_SEND_KEYBOARD, 5000000, "x");
		comparer_rejeu(&o, h, direct);
		g_string_free(direct, TRUE);
		gt_history_free(h);
		gt_view_render_free(r);
	}
}

static void
test_rejeu_egal_direct_grand(void)
{
	GtViewOptions o = { GT_VIEW_HEX, TRUE, FALSE, FALSE, 16, TRUE };
	GtViewRender *r = gt_view_render_new(&o);
	GtHistory *h = gt_history_new(GT_HISTORY_DEFAULT_CAPACITY);
	GString *direct = g_string_new(NULL);

	for(guint i = 0; i < 20000; i++)
	{
		gchar octet[2] = { (gchar)('A' + i % 26), 0 };

		ajouter_et_rendre(r, h, direct, (i % 500 == 499) ? GT_RECORD_TX : GT_RECORD_RX, GT_SEND_MACRO, i, octet);
	}
	comparer_rejeu(&o, h, direct);
	g_string_free(direct, TRUE);
	gt_history_free(h);
	gt_view_render_free(r);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/view_render/ascii-sans-option", test_ascii_sans_option);
	g_test_add_func("/view_render/ascii-echo-desactive", test_ascii_echo_desactive);
	g_test_add_func("/view_render/ascii-frappe-dans-la-ligne", test_ascii_frappe_dans_la_ligne);
	g_test_add_func("/view_render/ascii-envoi-en-bloc-sur-sa-ligne", test_ascii_envoi_en_bloc_sur_sa_ligne);
	g_test_add_func("/view_render/ascii-envoi-en-bloc-en-debut-de-ligne", test_ascii_envoi_en_bloc_en_debut_de_ligne);
	g_test_add_func("/view_render/ascii-bloc-apres-frappe-cr", test_ascii_bloc_apres_frappe_cr);
	g_test_add_func("/view_render/ascii-transformation-appliquee", test_ascii_transformation_appliquee);
	g_test_add_func("/view_render/hexa-cellule", test_hexa_cellule);
	g_test_add_func("/view_render/hexa-separateur", test_hexa_separateur);
	g_test_add_func("/view_render/hexa-colonne-ascii", test_hexa_colonne_ascii);
	g_test_add_func("/view_render/hexa-ligne-complete", test_hexa_ligne_complete);
	g_test_add_func("/view_render/hexa-index-non-decale-par-l-echo", test_hexa_index_non_decale_par_l_echo);
	g_test_add_func("/view_render/hexa-echo-desactive", test_hexa_echo_desactive);
	g_test_add_func("/view_render/reinitialiser-change-de-vue", test_reinitialiser_change_de_vue);
	g_test_add_func("/view_render/rejeu-egal-direct", test_rejeu_egal_direct);
	g_test_add_func("/view_render/rejeu-egal-direct-grand", test_rejeu_egal_direct_grand);
	return g_test_run();
}
