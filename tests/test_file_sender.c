/* Tests de file_sender.c (cœur) : envoi de fichier par une fonction d'écriture factice et la boucle GLib (sous-projet 6a). */

#include <errno.h>
#include <gio/gio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "file_sender.h"
#include "test_util.h"

#define MAX_LF 64

typedef struct
{
	/* écriture factice */
	GByteArray *recu;
	guint appels;
	gsize max_par_appel;		/* 0 : tout accepter */
	guint zero_tous_les;		/* n > 0 : un appel sur n rend 0 (port plein) */
	gboolean erreur;		/* rend -1 (errno = EIO) */
	gint64 instants_lf[MAX_LF];	/* heure (monotone) de chaque écriture qui finit par LF */
	guint n_lf;
	/* signaux */
	guint n_done;
	GError *erreur_done;
	guint n_progress;
	guint64 dernier_sent;
	guint64 dernier_total;
	gboolean liberer_dans_done;
	GtFileSender *s;
} Ctx;

static gssize
ecrire(const guint8 *data, gsize len, gpointer user_data)
{
	Ctx *c = user_data;

	c->appels++;
	if(c->erreur)
	{
		errno = EIO;
		return -1;
	}
	if(c->zero_tous_les > 0 && c->appels % c->zero_tous_les == 0)
		return 0;
	if(c->max_par_appel > 0 && len > c->max_par_appel)
		len = c->max_par_appel;
	g_byte_array_append(c->recu, data, (guint)len);
	if(data[len - 1] == '\n' && c->n_lf < MAX_LF)
		c->instants_lf[c->n_lf++] = g_get_monotonic_time();
	return (gssize)len;
}

static void
sur_progress(GtFileSender *s, guint64 sent, guint64 total, gpointer data)
{
	Ctx *c = data;

	c->n_progress++;
	g_assert_cmpuint(sent, >, c->dernier_sent);
	g_assert_cmpuint(sent, <=, total);
	c->dernier_sent = sent;
	c->dernier_total = total;
}

static void
sur_done(GtFileSender *s, GError *error, gpointer data)
{
	Ctx *c = data;

	c->n_done++;
	g_clear_error(&c->erreur_done);
	if(error != NULL)
		c->erreur_done = g_error_copy(error);
	if(c->liberer_dans_done)
	{
		g_object_unref(c->s);
		c->s = NULL;
	}
}

static void
ctx_init(Ctx *c, gint delai_ms, gint car)
{
	memset(c, 0, sizeof(*c));
	c->recu = g_byte_array_new();
	c->s = gt_file_sender_new(ecrire, c, delai_ms, car);
	g_signal_connect(c->s, "progress", G_CALLBACK(sur_progress), c);
	g_signal_connect(c->s, "done", G_CALLBACK(sur_done), c);
}

static void
ctx_clear(Ctx *c)
{
	if(c->s != NULL)
		g_object_unref(c->s);
	g_byte_array_unref(c->recu);
	g_clear_error(&c->erreur_done);
}

static gboolean
reveil(gpointer data)
{
	*(gboolean *)data = TRUE;
	return G_SOURCE_REMOVE;
}

/* Fait tourner la boucle jusqu'à pred(c) ou l'échéance ; rend pred(c) */
static gboolean
tourner_jusqua(Ctx *c, gboolean (*pred)(Ctx *), guint ms)
{
	gboolean expire = FALSE;
	guint id = g_timeout_add(ms, reveil, &expire);

	while(!pred(c) && !expire)
		g_main_context_iteration(NULL, TRUE);
	if(!expire)
		g_source_remove(id);
	return pred(c);
}

static gboolean
jamais(Ctx *c)
{
	return FALSE;
}

static void
tourner_pendant(guint ms)
{
	tourner_jusqua(NULL, jamais, ms);
}

static gboolean
fini(Ctx *c)
{
	return c->n_done > 0;
}

static guint n_attendu;

static gboolean
recu_n(Ctx *c)
{
	return c->recu->len >= n_attendu;
}

static gchar *
fichier_temp(gchar **dir, const gchar *contenu)
{
	*dir = g_dir_make_tmp("gtkterm-envoi-XXXXXX", NULL);
	return test_write_file(*dir, "envoi.txt", contenu);
}

static void
nettoyer(gchar *dir, gchar *path)
{
	g_free(path);
	test_rmdir(dir);
	g_free(dir);
}

static void
test_mille_lignes_intactes(void)
{
	GString *contenu = g_string_new(NULL);
	gchar *dir, *path;
	GError *err = NULL;
	Ctx c;

	for(guint i = 0; i < 1000; i++)
		g_string_append_printf(contenu, "ligne %04u : 0123456789abcdefghijklmnopqrstuvwxyz\n", i);
	path = fichier_temp(&dir, contenu->str);
	ctx_init(&c, 0, -1);
	/* Écritures partielles (37 octets au plus) et un appel sur 50 « port plein » (réessai après 10 ms) */
	c.max_par_appel = 37;
	c.zero_tous_les = 50;
	g_assert_true(gt_file_sender_start(c.s, path, &err));
	g_assert_no_error(err);
	g_assert_cmpuint(gt_file_sender_get_total(c.s), ==, contenu->len);
	g_assert_cmpuint(c.n_done, ==, 0);	/* rien d'écrit avant la boucle */
	g_assert_cmpuint(c.recu->len, ==, 0);
	g_assert_true(tourner_jusqua(&c, fini, 20000));
	g_assert_no_error(c.erreur_done);
	g_assert_cmpmem(c.recu->data, c.recu->len, contenu->str, contenu->len);
	g_assert_cmpuint(gt_file_sender_get_sent(c.s), ==, contenu->len);
	g_assert_cmpuint(c.dernier_sent, ==, contenu->len);
	g_assert_cmpuint(c.dernier_total, ==, contenu->len);
	tourner_pendant(50);
	g_assert_cmpuint(c.n_done, ==, 1);
	ctx_clear(&c);
	g_string_free(contenu, TRUE);
	nettoyer(dir, path);
}

static void
test_delai_fin_de_ligne(void)
{
	gchar *dir, *path = fichier_temp(&dir, "un\ndeux\ntrois\nquatre\n");
	GError *err = NULL;
	gint64 debut;
	Ctx c;

	ctx_init(&c, 50, 'K');	/* le délai l'emporte sur l'attente de caractère */
	debut = g_get_monotonic_time();
	g_assert_true(gt_file_sender_start(c.s, path, &err));
	g_assert_true(tourner_jusqua(&c, fini, 5000));
	g_assert_no_error(c.erreur_done);
	g_assert_cmpmem(c.recu->data, c.recu->len, "un\ndeux\ntrois\nquatre\n", 21);
	/* Une écriture par ligne, chacune suivie de 50 ms (marge grossière : 40 ms) ; "done" après le délai de la dernière */
	g_assert_cmpuint(c.n_lf, ==, 4);
	for(guint i = 1; i < c.n_lf; i++)
		g_assert_cmpint(c.instants_lf[i] - c.instants_lf[i - 1], >=, 40 * G_TIME_SPAN_MILLISECOND);
	g_assert_cmpint(g_get_monotonic_time() - debut, >=, 4 * 40 * G_TIME_SPAN_MILLISECOND);
	ctx_clear(&c);
	nettoyer(dir, path);
}

static void
test_attente_caractere(void)
{
	gchar *dir, *path = fichier_temp(&dir, "a\nb\nc\n");
	GError *err = NULL;
	Ctx c;

	ctx_init(&c, 0, 'K');
	g_assert_true(gt_file_sender_start(c.s, path, &err));
	n_attendu = 2;
	g_assert_true(tourner_jusqua(&c, recu_n, 2000));
	tourner_pendant(100);
	g_assert_cmpmem(c.recu->data, c.recu->len, "a\n", 2);

	/* Octets reçus sans le caractère : toujours bloqué */
	gt_file_sender_feed_rx(c.s, (const guint8 *)"xyz", 3);
	tourner_pendant(50);
	g_assert_cmpuint(c.recu->len, ==, 2);

	gt_file_sender_feed_rx(c.s, (const guint8 *)"zK", 2);
	n_attendu = 4;
	g_assert_true(tourner_jusqua(&c, recu_n, 2000));
	tourner_pendant(50);
	g_assert_cmpmem(c.recu->data, c.recu->len, "a\nb\n", 4);

	gt_file_sender_feed_rx(c.s, (const guint8 *)"K", 1);
	n_attendu = 6;
	g_assert_true(tourner_jusqua(&c, recu_n, 2000));
	/* La dernière ligne attend aussi sa réponse avant "done" */
	tourner_pendant(50);
	g_assert_cmpuint(c.n_done, ==, 0);
	gt_file_sender_feed_rx(c.s, (const guint8 *)"K", 1);
	g_assert_true(tourner_jusqua(&c, fini, 2000));
	g_assert_no_error(c.erreur_done);
	g_assert_cmpmem(c.recu->data, c.recu->len, "a\nb\nc\n", 6);
	ctx_clear(&c);
	nettoyer(dir, path);
}

static void
test_fichier_vide(void)
{
	gchar *dir, *path = fichier_temp(&dir, "");
	GError *err = NULL;
	Ctx c;

	ctx_init(&c, 0, -1);
	g_assert_true(gt_file_sender_start(c.s, path, &err));
	g_assert_no_error(err);
	g_assert_cmpuint(gt_file_sender_get_total(c.s), ==, 0);
	g_assert_cmpuint(c.n_done, ==, 0);	/* émis depuis la boucle, pas pendant start */
	g_assert_true(tourner_jusqua(&c, fini, 2000));
	g_assert_no_error(c.erreur_done);
	g_assert_cmpuint(c.appels, ==, 0);
	g_assert_cmpuint(c.n_progress, ==, 0);
	ctx_clear(&c);
	nettoyer(dir, path);
}

static void
test_annulation(void)
{
	gchar *dir, *path = fichier_temp(&dir, "un\ndeux\ntrois\n");
	GError *err = NULL;
	Ctx c;

	ctx_init(&c, 200, -1);
	g_assert_true(gt_file_sender_start(c.s, path, &err));
	n_attendu = 3;
	g_assert_true(tourner_jusqua(&c, recu_n, 2000));
	/* Pendant le délai de 200 ms : annulation, "done" tout de suite */
	gt_file_sender_cancel(c.s);
	g_assert_cmpuint(c.n_done, ==, 1);
	g_assert_error(c.erreur_done, G_IO_ERROR, G_IO_ERROR_CANCELLED);
	/* Minuterie retirée : plus rien ne part, pas de second "done" */
	tourner_pendant(300);
	g_assert_cmpuint(c.recu->len, ==, 3);
	g_assert_cmpuint(c.n_done, ==, 1);
	gt_file_sender_cancel(c.s);
	gt_file_sender_link_lost(c.s);
	g_assert_cmpuint(c.n_done, ==, 1);

	/* Annulation d'un fichier vide avant l'idle : un seul "done", annulé */
	{
		gchar *dir2, *vide = fichier_temp(&dir2, "");

		g_assert_true(gt_file_sender_start(c.s, vide, &err));
		gt_file_sender_cancel(c.s);
		g_assert_cmpuint(c.n_done, ==, 2);
		g_assert_error(c.erreur_done, G_IO_ERROR, G_IO_ERROR_CANCELLED);
		tourner_pendant(50);
		g_assert_cmpuint(c.n_done, ==, 2);
		nettoyer(dir2, vide);
	}
	ctx_clear(&c);
	nettoyer(dir, path);
}

static void
test_perte_pendant_attente(void)
{
	gchar *dir, *path = fichier_temp(&dir, "a\nb\n");
	GError *err = NULL;
	Ctx c;

	ctx_init(&c, 0, 'K');
	g_assert_true(gt_file_sender_start(c.s, path, &err));
	n_attendu = 2;
	g_assert_true(tourner_jusqua(&c, recu_n, 2000));
	gt_file_sender_link_lost(c.s);
	g_assert_cmpuint(c.n_done, ==, 1);
	g_assert_error(c.erreur_done, G_IO_ERROR, G_IO_ERROR_BROKEN_PIPE);
	/* Le caractère qui arrive ensuite ne relance rien */
	gt_file_sender_feed_rx(c.s, (const guint8 *)"K", 1);
	tourner_pendant(50);
	g_assert_cmpuint(c.recu->len, ==, 2);
	gt_file_sender_link_lost(c.s);
	g_assert_cmpuint(c.n_done, ==, 1);
	ctx_clear(&c);
	nettoyer(dir, path);
}

static void
test_erreur_ecriture(void)
{
	gchar *dir, *path = fichier_temp(&dir, "abc\n");
	GError *err = NULL;
	Ctx c;

	ctx_init(&c, 0, -1);
	c.erreur = TRUE;
	g_assert_true(gt_file_sender_start(c.s, path, &err));
	g_assert_true(tourner_jusqua(&c, fini, 2000));
	g_assert_nonnull(c.erreur_done);
	g_assert_true(c.erreur_done->domain == G_IO_ERROR);
	g_assert_cmpuint(c.appels, ==, 1);
	tourner_pendant(50);
	g_assert_cmpuint(c.n_done, ==, 1);
	g_assert_cmpuint(c.appels, ==, 1);
	ctx_clear(&c);
	nettoyer(dir, path);
}

static void
test_fichier_inexistant(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-envoi-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "absent.txt", NULL);
	GError *err = NULL;
	Ctx c;

	ctx_init(&c, 0, -1);
	g_assert_false(gt_file_sender_start(c.s, path, &err));
	g_assert_error(err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_clear_error(&err);
	/* Un dossier n'est pas un fichier ordinaire */
	g_assert_false(gt_file_sender_start(c.s, dir, &err));
	g_assert_error(err, G_IO_ERROR, G_IO_ERROR_NOT_REGULAR_FILE);
	g_clear_error(&err);
	tourner_pendant(50);
	g_assert_cmpuint(c.n_done, ==, 0);
	ctx_clear(&c);
	nettoyer(dir, path);
}

/* Une FIFO sans écrivain : l'ouverture ne doit pas bloquer (O_NONBLOCK), le fichier est refusé avec un message clair. alarm() borne le
 * blocage d'une ouverture fautive : le test échoue (SIGALRM) au lieu de geler jusqu'au délai de meson. */
static void
test_fifo_refusee(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-envoi-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "tube", NULL);
	GError *err = NULL;
	Ctx c;

	g_assert_cmpint(mkfifo(path, 0600), ==, 0);
	ctx_init(&c, 0, -1);
	alarm(5);
	g_assert_false(gt_file_sender_start(c.s, path, &err));
	alarm(0);
	g_assert_error(err, G_IO_ERROR, G_IO_ERROR_NOT_REGULAR_FILE);
	g_assert_nonnull(strstr(err->message, path));
	g_assert_nonnull(strstr(err->message, "not a regular file"));
	g_clear_error(&err);
	tourner_pendant(50);
	g_assert_cmpuint(c.n_done, ==, 0);
	ctx_clear(&c);
	nettoyer(dir, path);
}

static void
test_deja_en_cours(void)
{
	gchar *dir, *path = fichier_temp(&dir, "a\nb\n");
	GError *err = NULL;
	Ctx c;

	ctx_init(&c, 0, 'K');
	g_assert_true(gt_file_sender_start(c.s, path, &err));
	g_assert_false(gt_file_sender_start(c.s, path, &err));
	g_assert_error(err, G_IO_ERROR, G_IO_ERROR_PENDING);
	g_clear_error(&err);
	gt_file_sender_cancel(c.s);
	/* Après la fin, un nouvel envoi repart de zéro */
	g_assert_true(gt_file_sender_start(c.s, path, &err));
	g_assert_cmpuint(gt_file_sender_get_sent(c.s), ==, 0);
	gt_file_sender_cancel(c.s);
	g_assert_cmpuint(c.n_done, ==, 2);
	ctx_clear(&c);
	nettoyer(dir, path);
}

static void
test_liberation_dans_done(void)
{
	gchar *dir, *path = fichier_temp(&dir, "abc\n");
	GError *err = NULL;
	Ctx c;

	ctx_init(&c, 0, -1);
	c.liberer_dans_done = TRUE;
	g_assert_true(gt_file_sender_start(c.s, path, &err));
	g_assert_true(tourner_jusqua(&c, fini, 2000));
	g_assert_null(c.s);
	g_assert_no_error(c.erreur_done);

	/* Même chose depuis cancel (appel direct, pas depuis la boucle) */
	ctx_clear(&c);
	ctx_init(&c, 0, 'K');
	c.liberer_dans_done = TRUE;
	g_assert_true(gt_file_sender_start(c.s, path, &err));
	n_attendu = 4;
	g_assert_true(tourner_jusqua(&c, recu_n, 2000));
	gt_file_sender_cancel(c.s);
	g_assert_null(c.s);
	tourner_pendant(50);
	ctx_clear(&c);

	/* Libéré pendant un envoi : arrêt silencieux, aucune source orpheline */
	ctx_init(&c, 100, -1);
	g_assert_true(gt_file_sender_start(c.s, path, &err));
	g_object_unref(c.s);
	c.s = NULL;
	tourner_pendant(150);
	g_assert_cmpuint(c.n_done, ==, 0);
	g_assert_cmpuint(c.recu->len, ==, 0);
	ctx_clear(&c);
	nettoyer(dir, path);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/file_sender/mille-lignes-intactes", test_mille_lignes_intactes);
	g_test_add_func("/file_sender/delai-fin-de-ligne", test_delai_fin_de_ligne);
	g_test_add_func("/file_sender/attente-caractere", test_attente_caractere);
	g_test_add_func("/file_sender/fichier-vide", test_fichier_vide);
	g_test_add_func("/file_sender/annulation", test_annulation);
	g_test_add_func("/file_sender/perte-pendant-attente", test_perte_pendant_attente);
	g_test_add_func("/file_sender/erreur-ecriture", test_erreur_ecriture);
	g_test_add_func("/file_sender/fichier-inexistant", test_fichier_inexistant);
	g_test_add_func("/file_sender/fifo-refusee", test_fifo_refusee);
	g_test_add_func("/file_sender/deja-en-cours", test_deja_en_cours);
	g_test_add_func("/file_sender/liberation-dans-done", test_liberation_dans_done);
	return g_test_run();
}
