/* Tests de la détection des ports série (serial_ports.c) : ordre semi-numérique, fichier de pilotes factice, motifs par défaut, recherche sur l'esclave
 * d'une paire pty (vrai périphérique caractère, jamais un vrai port série) et liens symboliques d'un dossier temporaire. */
#include <glib.h>
#include <glib/gstdio.h>
#include <pty.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include "serial_ports.h"

static void
test_ordre(void)
{
	g_assert_cmpint(gt_serial_ports_compare("/dev/ttyS2", "/dev/ttyS10"), <, 0);
	g_assert_cmpint(gt_serial_ports_compare("/dev/ttyS10", "/dev/ttyS9"), >, 0);
	g_assert_cmpint(gt_serial_ports_compare("/dev/ttyS1", "/dev/ttyS1"), ==, 0);
	g_assert_cmpint(gt_serial_ports_compare("/dev/ttyS1", "/dev/ttyUSB0"), <, 0);
	g_assert_cmpint(gt_serial_ports_compare("/dev/ttyM5_2", "/dev/ttyM5_10"), <, 0);
	/* même valeur numérique : le plus court d'abord */
	g_assert_cmpint(gt_serial_ports_compare("a1", "a01"), <, 0);
	g_assert_cmpint(gt_serial_ports_compare("a01", "a1"), >, 0);
	/* un préfixe vient avant (term_config.c le classait après : corrigé) */
	g_assert_cmpint(gt_serial_ports_compare("/dev/tty", "/dev/ttyS0"), <, 0);
	g_assert_cmpint(gt_serial_ports_compare("/dev/ttyS0", "/dev/tty"), >, 0);
}

static gchar *
ecrire_pilotes(const gchar *dossier, const gchar *texte)
{
	gchar *fichier = g_build_filename(dossier, "drivers", NULL);

	g_assert_true(g_file_set_contents(fichier, texte, -1, NULL));
	return fichier;
}

static void
test_pilotes(void)
{
	int maitre, esclave;
	char nom[256];
	struct stat st;
	gchar *dossier = g_dir_make_tmp("gtkterm-ports-XXXXXX", NULL);
	gchar *texte, *fichier, *attendu;
	GArray *motifs;
	const GtSerialPattern *m;

	g_assert_cmpint(openpty(&maitre, &esclave, nom, NULL, NULL), ==, 0);
	g_assert_cmpint(stat(nom, &st), ==, 0);
	texte = g_strdup_printf("/dev/tty             /dev/tty        5       0 system:/dev/tty\n"
	                        "faux_dossier         %s     200 0-9 serial\n"
	                        "faux_absent          /nonexistent-gtkterm/ttyX 201 3-4 serial\n"
	                        "pty_un               %s %u %u serial\n"
	                        "ligne invalide\n"
	                        "console              /dev/console    5       1 system:console\n",
	                        dossier, nom, major(st.st_rdev), minor(st.st_rdev));
	fichier = ecrire_pilotes(dossier, texte);
	motifs = gt_serial_patterns_read(fichier);
	g_assert_cmpuint(motifs->len, ==, 3);

	m = &g_array_index(motifs, GtSerialPattern, 0);
	attendu = g_strconcat(dossier, "/[0-9]*", NULL);	/* chemin d'un dossier : ses entrées numériques */
	g_assert_cmpstr(m->pattern, ==, attendu);
	g_assert_cmpuint(m->major, ==, 200);
	g_assert_cmpuint(m->minor, ==, 0);
	g_assert_cmpuint(m->nminors, ==, 10);
	g_free(attendu);

	m = &g_array_index(motifs, GtSerialPattern, 1);	/* chemin absent : préfixe suivi de chiffres */
	g_assert_cmpstr(m->pattern, ==, "/nonexistent-gtkterm/ttyX[0-9]*");
	g_assert_cmpuint(m->minor, ==, 3);
	g_assert_cmpuint(m->nminors, ==, 2);

	m = &g_array_index(motifs, GtSerialPattern, 2);	/* un seul numéro sur un périphérique existant : le chemin exact */
	g_assert_cmpstr(m->pattern, ==, nom);
	g_assert_cmpuint(m->major, ==, major(st.st_rdev));
	g_assert_cmpuint(m->nminors, ==, 1);

	g_array_unref(motifs);
	g_unlink(fichier);
	g_rmdir(dossier);
	close(maitre);
	close(esclave);
	g_free(fichier);
	g_free(texte);
	g_free(dossier);
}

static void
test_defauts(void)
{
	gchar *dossier = g_dir_make_tmp("gtkterm-ports-XXXXXX", NULL);
	gchar *fichier = ecrire_pilotes(dossier, "/dev/tty /dev/tty 5 0 system:/dev/tty\n");
	GArray *motifs;
	gchar *texte;

	/* fichier illisible, puis fichier sans pilote série : motifs par défaut, sans contrôle du numéro */
	for(gint i = 0; i < 2; i++)
	{
		motifs = gt_serial_patterns_read(i == 0 ? "/nonexistent-gtkterm/drivers" : fichier);
		g_assert_cmpuint(motifs->len, ==, 3);
		g_assert_cmpstr(g_array_index(motifs, GtSerialPattern, 0).pattern, ==, "/dev/ttyS*");
		g_assert_cmpuint(g_array_index(motifs, GtSerialPattern, 0).nminors, ==, 0);
		texte = gt_serial_patterns_to_string(motifs);
		g_assert_cmpstr(texte, ==, "\t/dev/ttyS*\n\t/dev/tts/[0-9]*\n\t/dev/usb/tts/[0-9]*\n");
		g_free(texte);
		g_array_unref(motifs);
	}
	g_unlink(fichier);
	g_rmdir(dossier);
	g_free(fichier);
	g_free(dossier);
}

static const GtSerialPort *
port_de(GPtrArray *ports, const gchar *chemin)
{
	for(guint i = 0; i < ports->len; i++)
		if(g_strcmp0(((GtSerialPort *)g_ptr_array_index(ports, i))->path, chemin) == 0)
			return g_ptr_array_index(ports, i);
	return NULL;
}

static void
test_recherche(void)
{
	int maitre, esclave;
	char nom[256];
	struct stat st;
	gchar *dossier = g_dir_make_tmp("gtkterm-ports-XXXXXX", NULL);
	gchar *liens = g_build_filename(dossier, "dev", NULL);
	gchar *lien = g_build_filename(liens, "ttyM5", NULL);
	gchar *ailleurs = g_build_filename(liens, "ailleurs", NULL);
	gchar *ordinaire = g_build_filename(liens, "fichier", NULL);
	gchar *texte, *fichier;
	GArray *motifs;
	GPtrArray *ports;
	const GtSerialPort *p;

	g_assert_cmpint(openpty(&maitre, &esclave, nom, NULL, NULL), ==, 0);
	g_assert_cmpint(stat(nom, &st), ==, 0);
	g_assert_cmpint(g_mkdir(liens, 0700), ==, 0);
	g_assert_cmpint(symlink(nom, lien), ==, 0);
	g_assert_cmpint(symlink("/etc/hostname", ailleurs), ==, 0);	/* lien vers autre chose qu'un port : ignoré */
	g_assert_true(g_file_set_contents(ordinaire, "x", -1, NULL));

	texte = g_strdup_printf("pty_un %s %u %u serial\n", nom, major(st.st_rdev), minor(st.st_rdev));
	fichier = ecrire_pilotes(dossier, texte);
	motifs = gt_serial_patterns_read(fichier);
	ports = gt_serial_ports_find(motifs, liens);
	g_assert_cmpuint(ports->len, ==, 2);
	p = port_de(ports, nom);
	g_assert_nonnull(p);
	g_assert_false(p->is_link);
	p = port_de(ports, lien);
	g_assert_nonnull(p);
	g_assert_true(p->is_link);
	g_assert_cmpint(gt_serial_ports_compare(((GtSerialPort *)g_ptr_array_index(ports, 0))->path,
	                                        ((GtSerialPort *)g_ptr_array_index(ports, 1))->path), <, 0);
	g_ptr_array_unref(ports);

	/* sans dossier de liens : le port seul */
	ports = gt_serial_ports_find(motifs, NULL);
	g_assert_cmpuint(ports->len, ==, 1);
	g_ptr_array_unref(ports);
	g_array_unref(motifs);
	g_free(texte);
	g_free(fichier);

	/* numéro de périphérique d'un autre pilote : ni le port, ni ses liens */
	texte = g_strdup_printf("pty_un %s %u %u serial\n", nom, major(st.st_rdev), minor(st.st_rdev) + 1);
	fichier = ecrire_pilotes(dossier, texte);
	motifs = gt_serial_patterns_read(fichier);
	ports = gt_serial_ports_find(motifs, liens);
	g_assert_cmpuint(ports->len, ==, 0);
	g_ptr_array_unref(ports);
	g_array_unref(motifs);

	g_unlink(lien);
	g_unlink(ailleurs);
	g_unlink(ordinaire);
	g_rmdir(liens);
	g_unlink(fichier);
	g_rmdir(dossier);
	close(maitre);
	close(esclave);
	g_free(texte);
	g_free(fichier);
	g_free(ordinaire);
	g_free(ailleurs);
	g_free(lien);
	g_free(liens);
	g_free(dossier);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/serial-ports/ordre", test_ordre);
	g_test_add_func("/serial-ports/pilotes", test_pilotes);
	g_test_add_func("/serial-ports/defauts", test_defauts);
	g_test_add_func("/serial-ports/recherche", test_recherche);
	return g_test_run();
}
