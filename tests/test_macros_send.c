/* Tests d'intégration du chemin d'envoi de macros.c (bouchons d'interface ; GTK lié sans gtk_init ni fenêtre). */

#include <gtk/gtk.h>
#include <string.h>

#include "interface.h"
#include "macros.h"
#include "macros_list.h"
#include "stubs.h"
#include "test_util.h"

static void
installer_macros(void)
{
	macro_t tab[4];
	gchar *args[] = { "42", NULL };

	memset(tab, 0, sizeof(tab));
	tab[0].label = g_strdup("Vitesse");
	tab[0].shortcut = g_strdup("<Control>F5");
	tab[0].action = g_strdup("V%d\\r");
	tab[0].args = g_strdupv(args);
	tab[1].label = NULL;					/* C8 : macro sans libellé */
	tab[1].shortcut = g_strdup("<Control>F6");
	tab[1].action = g_strdup("SANS_LIBELLE");
	tab[2].label = g_strdup("Pourcent");
	tab[2].shortcut = g_strdup("None");
	tab[2].action = g_strdup("100%%");
	create_shortcuts(tab, 3);
}

static gchar *
envoye(void)
{
	GByteArray *octets = stub_sent();

	return g_strndup((const gchar *)octets->data, octets->len);
}

static void
test_raccourci_avec_arguments_enregistres(void)
{
	gchar *s;

	stub_reset();
	installer_macros();
	shortcut_callback(GINT_TO_POINTER(0));
	s = envoye();
	g_assert_cmpstr(s, ==, "V42\r");
	g_free(s);
	remove_shortcuts();
}

static void
test_raccourci_double_pourcent(void)
{
	gchar *s;

	stub_reset();
	installer_macros();
	shortcut_callback(GINT_TO_POINTER(2));
	s = envoye();
	g_assert_cmpstr(s, ==, "100%");
	g_free(s);
	remove_shortcuts();
}

static void
test_raccourci_sans_libelle(void)
{
	gchar *s;

	stub_reset();
	installer_macros();
	shortcut_callback(GINT_TO_POINTER(1));
	s = envoye();
	g_assert_cmpstr(s, ==, "SANS_LIBELLE");
	g_assert_cmpint(stub_count("Put_temp_message"), ==, 1);
	g_free(s);
	remove_shortcuts();
}

static void
test_indice_hors_bornes(void)
{
	stub_reset();
	installer_macros();
	shortcut_callback(GINT_TO_POINTER(99));
	shortcut_callback(GINT_TO_POINTER(-1));
	g_assert_cmpuint(stub_sent()->len, ==, 0);
	remove_shortcuts();
}

static void
test_raccourci_connecte_une_seule_fois(void)
{
	guint touche, n = 0;
	GdkModifierType modificateurs;

	/* gtk_accel_group_connect interroge GDK : une connexion d'affichage (sans fenetre) est necessaire. */
	if(!gtk_init_check(NULL, NULL))
	{
		g_test_skip("pas d'affichage graphique");
		return;
	}
	stub_reset();
	shortcuts = gtk_accel_group_new();
	installer_macros();
	add_shortcuts();
	add_shortcuts();
	gtk_accelerator_parse("<Control>F6", &touche, &modificateurs);
	g_assert_cmpuint(touche, !=, 0);
	gtk_accel_group_query(shortcuts, touche, modificateurs, &n);
	g_assert_cmpuint(n, ==, 1);
	remove_shortcuts();
	gtk_accel_group_query(shortcuts, touche, modificateurs, &n);
	g_assert_cmpuint(n, ==, 0);
	g_clear_object(&shortcuts);
}

/* M43 : l'accelerateur doit se declarer traite, sinon GTK transmet aussi la touche au terminal. */
static void
test_raccourci_declare_traite(void)
{
	guint touche;
	GdkModifierType modificateurs;
	GtkWidget *fenetre;
	gchar *s;

	if(!gtk_init_check(NULL, NULL))
	{
		g_test_skip("pas d'affichage graphique");
		return;
	}
	stub_reset();
	shortcuts = gtk_accel_group_new();
	installer_macros();
	add_shortcuts();
	fenetre = gtk_window_new(GTK_WINDOW_TOPLEVEL);	/* jamais affichee */
	gtk_window_add_accel_group(GTK_WINDOW(fenetre), shortcuts);
	gtk_accelerator_parse("<Control>F5", &touche, &modificateurs);
	g_assert_cmpuint(touche, !=, 0);
	g_assert_true(gtk_accel_groups_activate(G_OBJECT(fenetre), touche, modificateurs));
	s = envoye();
	g_assert_cmpstr(s, ==, "V42\r");
	g_free(s);
	remove_shortcuts();
	gtk_widget_destroy(fenetre);
	g_clear_object(&shortcuts);
}

/* F1 : %#Nom n'est une liste que si elle existe ; les listes doivent donc etre chargees avant les macros. */
static void
test_chargement_arguments_liste_conserves(void)
{
	gint n = 0;
	macro_t *m;
	gchar *dir = g_dir_make_tmp("gtkterm-macros-XXXXXX", NULL);
	gchar *path, *s;
	const gchar *contenu =
		"[macros]\n"
		"macro_0=SPD::<Control>F5::SPD %#Speed\\\\r::::115200::0::1000\n"
		"macro_1=M::None::M %#Speed %d\\\\r::::9600|5::0::1000\n"
		"[lists]\n"
		"list_0=Speed:9600=9600|115200=115200\n";

	g_assert_nonnull(dir);
	path = test_write_file(dir, "macros.ini", contenu);
	stub_reset();
	macro_lists_free();
	macro_lists_init();
	macros_file_load(path);
	m = get_shortcuts(&n);
	g_assert_cmpint(n, ==, 2);
	g_assert_nonnull(m[0].args);
	g_assert_cmpstr(m[0].args[0], ==, "115200");
	g_assert_nonnull(m[1].args);
	g_assert_cmpstr(m[1].args[0], ==, "9600");
	g_assert_cmpstr(m[1].args[1], ==, "5");
	shortcut_callback(GINT_TO_POINTER(0));
	s = envoye();
	g_assert_cmpstr(s, ==, "SPD 115200\r");
	g_free(s);
	remove_shortcuts();
	macro_lists_free();
	macro_lists_init();
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/macros_send/raccourci-avec-arguments-enregistres", test_raccourci_avec_arguments_enregistres);
	g_test_add_func("/macros_send/raccourci-double-pourcent", test_raccourci_double_pourcent);
	g_test_add_func("/macros_send/raccourci-sans-libelle", test_raccourci_sans_libelle);
	g_test_add_func("/macros_send/indice-hors-bornes", test_indice_hors_bornes);
	g_test_add_func("/macros_send/raccourci-connecte-une-seule-fois", test_raccourci_connecte_une_seule_fois);
	g_test_add_func("/macros_send/raccourci-declare-traite", test_raccourci_declare_traite);
	g_test_add_func("/macros_send/chargement-arguments-liste-conserves", test_chargement_arguments_liste_conserves);
	return g_test_run();
}
