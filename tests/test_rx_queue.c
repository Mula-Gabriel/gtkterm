/* Tests de rx_queue.c (cœur) : file d'octets reçus bornée et sûre entre threads (sous-projet 5). */

#include <glib.h>
#include <string.h>

#include "rx_queue.h"

static void
pousser(GtRxQueue *q, const gchar *texte)
{
	gt_rx_queue_push(q, (const guint8 *)texte, strlen(texte));
}

static void
verifier_octets(GBytes *b, const void *attendu, gsize taille)
{
	gsize n = 0;
	const guint8 *p = g_bytes_get_data(b, &n);

	g_assert_cmpuint(n, ==, taille);
	if(taille > 0)
		g_assert_cmpmem(p, n, attendu, taille);
}

static void
test_ajout_et_lecture(void)
{
	GtRxQueue *q = gt_rx_queue_new(64);
	GBytes *b;

	pousser(q, "abc");
	pousser(q, "def");
	g_assert_cmpuint(gt_rx_queue_length(q), ==, 6);
	b = gt_rx_queue_take_all(q);
	verifier_octets(b, "abcdef", 6);
	g_bytes_unref(b);
	g_assert_cmpuint(gt_rx_queue_length(q), ==, 0);
	b = gt_rx_queue_take_all(q);
	verifier_octets(b, "", 0);
	g_bytes_unref(b);
	gt_rx_queue_free(q);
	gt_rx_queue_free(NULL);
}

static void
test_clear(void)
{
	GtRxQueue *q = gt_rx_queue_new(64);

	pousser(q, "abc");
	gt_rx_queue_clear(q);
	g_assert_cmpuint(gt_rx_queue_length(q), ==, 0);
	g_assert_false(gt_rx_queue_is_closed(q));
	gt_rx_queue_free(q);
}

static void
test_debordement(void)
{
	GtRxQueue *q = gt_rx_queue_new(8);
	GBytes *b;

	g_assert_cmpuint(gt_rx_queue_push(q, (const guint8 *)"12345", 5), ==, 0);
	/* 5 + 5 > 8 : les 2 plus anciens octets sont jetés */
	g_assert_cmpuint(gt_rx_queue_push(q, (const guint8 *)"6789A", 5), ==, 2);
	g_assert_cmpuint(gt_rx_queue_take_dropped(q), ==, 2);
	g_assert_cmpuint(gt_rx_queue_take_dropped(q), ==, 0);
	b = gt_rx_queue_take_all(q);
	verifier_octets(b, "3456789A", 8);
	g_bytes_unref(b);

	/* Un bloc plus grand que la file : seuls ses 8 derniers octets restent ; tout le reste est compté */
	pousser(q, "xy");
	g_assert_cmpuint(gt_rx_queue_push(q, (const guint8 *)"abcdefghijklmnopqrst", 20), ==, 14);
	g_assert_cmpuint(gt_rx_queue_take_dropped(q), ==, 14);
	b = gt_rx_queue_take_all(q);
	verifier_octets(b, "mnopqrst", 8);
	g_bytes_unref(b);
	gt_rx_queue_free(q);
}

static void
test_recherche_binaire(void)
{
	GtRxQueue *q = gt_rx_queue_new(64);
	GBytes *b = NULL;

	gt_rx_queue_push(q, (const guint8 *)"\x01\x00\x02OK\x00\x02", 7);
	g_assert_cmpint(gt_rx_queue_wait_for(q, (const guint8 *)"\x00\x02", 2, g_get_monotonic_time(), NULL, &b), ==, GT_RX_WAIT_FOUND);
	verifier_octets(b, "\x01\x00\x02", 3);
	g_bytes_unref(b);
	g_assert_cmpuint(gt_rx_queue_length(q), ==, 4);
	g_assert_cmpint(gt_rx_queue_wait_for(q, (const guint8 *)"\x00\x02", 2, g_get_monotonic_time(), NULL, &b), ==, GT_RX_WAIT_FOUND);
	verifier_octets(b, "OK\x00\x02", 4);
	g_bytes_unref(b);
	gt_rx_queue_free(q);
}

static void
test_motif_vide(void)
{
	GtRxQueue *q = gt_rx_queue_new(64);
	GBytes *b = NULL;

	pousser(q, "abc");
	g_assert_cmpint(gt_rx_queue_wait_for(q, (const guint8 *)"", 0, g_get_monotonic_time(), NULL, &b), ==, GT_RX_WAIT_FOUND);
	verifier_octets(b, "", 0);
	g_bytes_unref(b);
	g_assert_cmpuint(gt_rx_queue_length(q), ==, 3);
	gt_rx_queue_free(q);
}

static void
test_motif_a_cheval(void)
{
	GtRxQueue *q = gt_rx_queue_new(64);
	GBytes *b = NULL;

	pousser(q, "AB");
	g_assert_cmpint(gt_rx_queue_wait_for(q, (const guint8 *)"BC", 2, g_get_monotonic_time(), NULL, &b), ==, GT_RX_WAIT_TIMEOUT);
	g_assert_null(b);
	pousser(q, "CD");
	g_assert_cmpint(gt_rx_queue_wait_for(q, (const guint8 *)"BC", 2, g_get_monotonic_time(), NULL, &b), ==, GT_RX_WAIT_FOUND);
	verifier_octets(b, "ABC", 3);
	g_bytes_unref(b);
	g_assert_cmpuint(gt_rx_queue_length(q), ==, 1);
	gt_rx_queue_free(q);
}

static void
test_echeance(void)
{
	GtRxQueue *q = gt_rx_queue_new(64);
	GBytes *b = NULL;
	gint64 debut = g_get_monotonic_time();

	g_assert_cmpint(gt_rx_queue_wait_for(q, (const guint8 *)"x", 1, debut + 50 * G_TIME_SPAN_MILLISECOND, NULL, &b), ==, GT_RX_WAIT_TIMEOUT);
	g_assert_null(b);
	g_assert_cmpint(g_get_monotonic_time() - debut, >=, 50 * G_TIME_SPAN_MILLISECOND);
	g_assert_cmpint(g_get_monotonic_time() - debut, <, 5 * G_TIME_SPAN_SECOND);
	gt_rx_queue_free(q);
}

/* Attente sur un autre thread : résultat et instant du retour */
typedef struct
{
	GtRxQueue *q;
	const gchar *motif;
	gint64 echeance;
	const gint *annulation;
	GtRxWaitResult resultat;
	GBytes *donnees;
	gint64 retour_us;
} Attente;

static gpointer
attendre(gpointer data)
{
	Attente *a = data;

	a->resultat = gt_rx_queue_wait_for(a->q, (const guint8 *)a->motif, strlen(a->motif), a->echeance, a->annulation, &a->donnees);
	a->retour_us = g_get_monotonic_time();
	return NULL;
}

static void
test_fermeture_pendant_attente(void)
{
	Attente a = { gt_rx_queue_new(64), "jamais", g_get_monotonic_time() + 10 * G_TIME_SPAN_SECOND, NULL, GT_RX_WAIT_FOUND, NULL, 0 };
	GThread *th = g_thread_new("attente", attendre, &a);
	gint64 fermeture;

	g_usleep(50 * G_TIME_SPAN_MILLISECOND);
	fermeture = g_get_monotonic_time();
	gt_rx_queue_set_closed(a.q, TRUE);
	g_thread_join(th);
	g_assert_cmpint(a.resultat, ==, GT_RX_WAIT_CLOSED);
	g_assert_null(a.donnees);
	g_assert_cmpint(a.retour_us - fermeture, <, 500 * G_TIME_SPAN_MILLISECOND);
	gt_rx_queue_free(a.q);
}

static void
test_donnees_avant_fermeture(void)
{
	GtRxQueue *q = gt_rx_queue_new(64);
	GBytes *b = NULL;

	pousser(q, "xyz\nreste");
	gt_rx_queue_set_closed(q, TRUE);
	g_assert_true(gt_rx_queue_is_closed(q));
	/* Fermée : ce qui est déjà là est rendu, puis CLOSED sans attendre l'échéance */
	g_assert_cmpint(gt_rx_queue_wait_for(q, (const guint8 *)"\n", 1, g_get_monotonic_time() + 10 * G_TIME_SPAN_SECOND, NULL, &b), ==, GT_RX_WAIT_FOUND);
	verifier_octets(b, "xyz\n", 4);
	g_bytes_unref(b);
	b = NULL;
	g_assert_cmpint(gt_rx_queue_wait_for(q, (const guint8 *)"\n", 1, g_get_monotonic_time() + 10 * G_TIME_SPAN_SECOND, NULL, &b), ==, GT_RX_WAIT_CLOSED);
	g_assert_null(b);
	/* Ajout sans effet sur une file fermée ; « read » rend encore ce qui a été reçu */
	g_assert_cmpuint(gt_rx_queue_push(q, (const guint8 *)"zz", 2), ==, 0);
	b = gt_rx_queue_take_all(q);
	verifier_octets(b, "reste", 5);
	g_bytes_unref(b);
	gt_rx_queue_free(q);
}

static void
test_annulation(void)
{
	gint annulation = 0;
	Attente a = { gt_rx_queue_new(64), "jamais", g_get_monotonic_time() + 10 * G_TIME_SPAN_SECOND, &annulation, GT_RX_WAIT_FOUND, NULL, 0 };
	GThread *th = g_thread_new("attente", attendre, &a);
	gint64 demande;

	g_usleep(50 * G_TIME_SPAN_MILLISECOND);
	demande = g_get_monotonic_time();
	/* Aucun réveil explicite : l'attente relit le drapeau au moins toutes les 20 ms */
	g_atomic_int_set(&annulation, 1);
	g_thread_join(th);
	g_assert_cmpint(a.resultat, ==, GT_RX_WAIT_CANCELLED);
	g_assert_cmpint(a.retour_us - demande, <, 100 * G_TIME_SPAN_MILLISECOND);
	gt_rx_queue_free(a.q);
}

static gpointer
produire(gpointer data)
{
	GtRxQueue *q = data;

	for(guint i = 0; i < 1000; i++)
	{
		gt_rx_queue_push(q, (const guint8 *)"0123456789", 10);
		if(i % 100 == 0)
			g_usleep(1000);
	}
	gt_rx_queue_push(q, (const guint8 *)"FIN", 3);
	return NULL;
}

static void
test_producteur_autre_thread(void)
{
	GtRxQueue *q = gt_rx_queue_new(1024 * 1024);
	GThread *th = g_thread_new("producteur", produire, q);
	GBytes *b = NULL;

	g_assert_cmpint(gt_rx_queue_wait_for(q, (const guint8 *)"FIN", 3, g_get_monotonic_time() + 10 * G_TIME_SPAN_SECOND, NULL, &b), ==, GT_RX_WAIT_FOUND);
	g_thread_join(th);
	g_assert_cmpuint(g_bytes_get_size(b), ==, 10003);
	g_bytes_unref(b);
	gt_rx_queue_free(q);
}

static void
test_reset(void)
{
	GtRxQueue *q = gt_rx_queue_new(4);

	pousser(q, "abcdef");
	gt_rx_queue_set_closed(q, TRUE);
	gt_rx_queue_reset(q);
	g_assert_false(gt_rx_queue_is_closed(q));
	g_assert_cmpuint(gt_rx_queue_length(q), ==, 0);
	g_assert_cmpuint(gt_rx_queue_take_dropped(q), ==, 0);
	pousser(q, "ab");
	g_assert_cmpuint(gt_rx_queue_length(q), ==, 2);
	gt_rx_queue_free(q);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/rx_queue/ajout-et-lecture", test_ajout_et_lecture);
	g_test_add_func("/rx_queue/clear", test_clear);
	g_test_add_func("/rx_queue/debordement", test_debordement);
	g_test_add_func("/rx_queue/recherche-binaire", test_recherche_binaire);
	g_test_add_func("/rx_queue/motif-vide", test_motif_vide);
	g_test_add_func("/rx_queue/motif-a-cheval", test_motif_a_cheval);
	g_test_add_func("/rx_queue/echeance", test_echeance);
	g_test_add_func("/rx_queue/fermeture-pendant-attente", test_fermeture_pendant_attente);
	g_test_add_func("/rx_queue/donnees-avant-fermeture", test_donnees_avant_fermeture);
	g_test_add_func("/rx_queue/annulation", test_annulation);
	g_test_add_func("/rx_queue/producteur-autre-thread", test_producteur_autre_thread);
	g_test_add_func("/rx_queue/reset", test_reset);
	return g_test_run();
}
