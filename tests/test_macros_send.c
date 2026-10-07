/* Tests d'intégration de macros.c : envoi unique par identifiant, raccourcis, fichier, ordonnanceur (bouchons d'interface ;
 * GTK lié sans gtk_init ni fenêtre, sauf les cas de raccourcis qui ont besoin d'un affichage). */

#include <gtk/gtk.h>
#include <string.h>

#include "interface.h"
#include "macros.h"
#include "stubs.h"
#include "test_util.h"

static guint id_vitesse, id_sans_libelle, id_pourcent, id_separateur;

static void
installer_macros(void)
{
	GtMacroSet *set = gt_macro_set_new();

	id_vitesse = gt_macro_set_append(set, "Vitesse", "<Control>F5", "V%d\\r", "");
	g_assert_true(gt_macro_set_set_arg(set, id_vitesse, 0, "42"));
	id_sans_libelle = gt_macro_set_append(set, NULL, "<Control>F6", "SANS_LIBELLE", "");	/* C8 */
	id_pourcent = gt_macro_set_append(set, "Pourcent", "None", "100%%", "");
	id_separateur = gt_macro_set_append(set, "Sep", "<Control>F7", "%&", "");
	macros_replace_set(set);
}

static void
desinstaller(void)
{
	macros_replace_set(gt_macro_set_new());
}

static gchar *
envoye(void)
{
	GByteArray *octets = stub_sent();

	return g_strndup((const gchar *)octets->data, octets->len);
}

static void
test_arguments_enregistres(void)
{
	gchar *s;

	installer_macros();
	stub_reset();
	g_assert_true(macros_send(id_vitesse, NULL, -1));
	s = envoye();
	g_assert_cmpstr(s, ==, "V42\r");
	g_assert_cmpint(stub_last_origin(), ==, GT_SEND_MACRO);
	g_free(s);
	desinstaller();
}

static void
test_arguments_fournis(void)
{
	const gchar *sept[] = { "7" };
	gchar *s;

	installer_macros();
	stub_reset();
	g_assert_true(macros_send(id_vitesse, sept, 1));
	s = envoye();
	g_assert_cmpstr(s, ==, "V7\r");
	g_free(s);
	desinstaller();
}

static void
test_double_pourcent(void)
{
	gchar *s;

	installer_macros();
	stub_reset();
	macros_send(id_pourcent, NULL, -1);
	s = envoye();
	g_assert_cmpstr(s, ==, "100%");
	g_free(s);
	desinstaller();
}

static void
test_sans_libelle(void)
{
	gchar *s;

	installer_macros();
	stub_reset();
	macros_send(id_sans_libelle, NULL, -1);
	s = envoye();
	g_assert_cmpstr(s, ==, "SANS_LIBELLE");
	g_assert_cmpint(stub_count("Put_temp_message"), ==, 1);
	g_assert_nonnull(strstr(stub_last("Put_temp_message"), "<Control>F6"));
	g_free(s);
	desinstaller();
}

static void
test_inconnue_ou_separateur(void)
{
	installer_macros();
	stub_reset();
	g_assert_false(macros_send(9999999, NULL, -1));
	g_assert_false(macros_send(id_separateur, NULL, -1));
	g_assert_cmpuint(stub_sent()->len, ==, 0);
	g_assert_cmpint(stub_count("Put_temp_message"), ==, 0);
	desinstaller();
}

/* Port ferme ou perdu : rien n'est ecrit, macros_send le signale par FALSE et n'affiche aucun message */
static void
test_ecriture_en_echec(void)
{
	installer_macros();
	stub_reset();
	stub_link_set_write_fails(TRUE);
	g_assert_false(macros_send(id_vitesse, NULL, -1));
	g_assert_cmpuint(stub_sent()->len, ==, 0);
	g_assert_cmpint(stub_count("gt_link_send"), ==, 1);
	/* M3 : aucun octet ecrit, pas de message « envoyee » (le « - lost » de la barre d'etat reste visible) */
	g_assert_cmpint(stub_count("Put_temp_message"), ==, 0);
	stub_link_set_write_fails(FALSE);
	desinstaller();
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
	shortcuts = gtk_accel_group_new();
	installer_macros();
	macros_changed();
	macros_changed();
	gtk_accelerator_parse("<Control>F6", &touche, &modificateurs);
	g_assert_cmpuint(touche, !=, 0);
	gtk_accel_group_query(shortcuts, touche, modificateurs, &n);
	g_assert_cmpuint(n, ==, 1);
	desinstaller();
	gtk_accel_group_query(shortcuts, touche, modificateurs, &n);
	g_assert_cmpuint(n, ==, 0);
	g_clear_object(&shortcuts);
}

/* M43 : l'accelerateur d'une macro envoyable se declare traite ; un separateur laisse passer la touche. */
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
	shortcuts = gtk_accel_group_new();
	installer_macros();
	stub_reset();
	fenetre = gtk_window_new(GTK_WINDOW_TOPLEVEL);	/* jamais affichee */
	gtk_window_add_accel_group(GTK_WINDOW(fenetre), shortcuts);
	gtk_accelerator_parse("<Control>F5", &touche, &modificateurs);
	g_assert_true(gtk_accel_groups_activate(G_OBJECT(fenetre), touche, modificateurs));
	s = envoye();
	g_assert_cmpstr(s, ==, "V42\r");
	g_free(s);
	gtk_accelerator_parse("<Control>F7", &touche, &modificateurs);
	g_assert_false(gtk_accel_groups_activate(G_OBJECT(fenetre), touche, modificateurs));
	/* Spec 6.2 : port ferme (ecriture en echec), la touche d'une macro envoyable reste traitee et n'atteint pas le terminal */
	stub_reset();
	stub_link_set_write_fails(TRUE);
	gtk_accelerator_parse("<Control>F5", &touche, &modificateurs);
	g_assert_true(gtk_accel_groups_activate(G_OBJECT(fenetre), touche, modificateurs));
	g_assert_cmpint(stub_count("gt_link_send"), ==, 1);
	g_assert_cmpuint(stub_sent()->len, ==, 0);
	stub_link_set_write_fails(FALSE);
	desinstaller();
	gtk_widget_destroy(fenetre);
	g_clear_object(&shortcuts);
}

/* Fenetres de premier niveau visibles : seule la fenetre d'edition en ouvre une ici (titre traduit : non compare) */
static gint
fenetres_edition(void)
{
	GList *liste = gtk_window_list_toplevels();
	gint n = 0;

	for(GList *l = liste; l != NULL; l = l->next)
		if(gtk_window_get_window_type(l->data) == GTK_WINDOW_TOPLEVEL && gtk_widget_get_visible(l->data))
			n++;
	g_list_free(liste);
	return n;
}

/* Fenetre d'edition ouverte pendant un changement de jeu : fermee sans enregistrer (ses lignes decrivent l'ancien jeu) */
static void
test_fenetre_edition_fermee_au_changement_de_jeu(void)
{
	gchar *dir, *path;

	if(!gtk_init_check(NULL, NULL))
	{
		g_test_skip("pas d'affichage graphique");
		return;
	}
	dir = g_dir_make_tmp("gtkterm-macros-XXXXXX", NULL);
	path = g_build_filename(dir, "m.ini", NULL);
	g_assert_true(macros_load_file(path, FALSE));		/* fichier absent : jeu vide, chemin retenu */
	installer_macros();
	stub_reset();
	Config_macros(NULL, NULL);
	g_assert_cmpint(fenetres_edition(), ==, 1);
	Config_macros(NULL, NULL);				/* deja ouverte : ramenee au premier plan, pas de seconde fenetre */
	g_assert_cmpint(fenetres_edition(), ==, 1);
	desinstaller();
	g_assert_cmpint(fenetres_edition(), ==, 0);
	g_assert_cmpint(stub_count("config_remember_macros_file"), ==, 0);
	g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
	g_assert_cmpuint(gt_macro_set_count(macros_get_set()), ==, 0);
	Config_macros(NULL, NULL);				/* window remis a NULL : une nouvelle fenetre s'ouvre */
	g_assert_cmpint(fenetres_edition(), ==, 1);
	desinstaller();
	g_assert_cmpint(fenetres_edition(), ==, 0);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

/* F1 : %#Nom n'est une liste que si elle existe ; ancien format lu avec ses listes avant ses macros. */
static void
test_chargement_ancien_format_listes(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macros-XXXXXX", NULL);
	gchar *path, *s;
	GtMacroSet *set;
	const gchar *contenu =
		"[macros]\n"
		"macro_0=SPD::<Control>F5::SPD %#Speed\\\\r::::115200::0::1000\n"
		"macro_1=M::None::M %#Speed %d\\\\r::::9600|5::0::1000\n"
		"[lists]\n"
		"list_0=Speed:9600=9600|115200=115200\n";

	path = test_write_file(dir, "macros.ini", contenu);
	stub_reset();
	g_assert_true(macros_load_file(path, FALSE));
	set = macros_get_set();
	g_assert_cmpuint(gt_macro_set_count(set), ==, 2);
	g_assert_cmpstr(gt_macro_set_get(set, 0)->args[0], ==, "115200");
	g_assert_cmpstr(gt_macro_set_get(set, 1)->args[0], ==, "9600");
	g_assert_cmpstr(gt_macro_set_get(set, 1)->args[1], ==, "5");
	g_assert_cmpstr(macros_file_get_path(), ==, path);
	g_assert_cmpint(stub_count("rebuild_macro_buttons"), >=, 1);
	macros_send(gt_macro_set_get(set, 0)->id, NULL, -1);
	s = envoye();
	g_assert_cmpstr(s, ==, "SPD 115200\r");
	g_free(s);
	desinstaller();
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

static void
test_chargement_fichier_absent(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macros-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "absent.ini", NULL);

	installer_macros();
	stub_reset();
	g_assert_true(macros_load_file(path, TRUE));
	g_assert_cmpuint(gt_macro_set_count(macros_get_set()), ==, 0);
	g_assert_cmpstr(macros_file_get_path(), ==, path);
	g_assert_cmpint(stub_count("show_message"), ==, 0);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

static void
test_chargement_corrompu_garde_le_jeu(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macros-XXXXXX", NULL);
	gchar *bon = g_build_filename(dir, "absent.ini", NULL);
	gchar *corrompu = test_write_file(dir, "corrompu.ini", "[[[\n");

	g_assert_true(macros_load_file(bon, FALSE));
	installer_macros();
	stub_reset();
	g_assert_false(macros_load_file(corrompu, TRUE));
	g_assert_cmpuint(gt_macro_set_count(macros_get_set()), ==, 4);
	g_assert_cmpint(stub_count("show_message"), ==, 1);
	g_assert_cmpstr(macros_file_get_path(), ==, bon);
	desinstaller();
	test_rmdir(dir);
	g_free(bon);
	g_free(corrompu);
	g_free(dir);
}

static void
test_enregistrement_echec_une_seule_boite(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macros-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "absent", "m.ini", NULL);

	installer_macros();
	stub_reset();
	g_assert_false(macros_save_file(path, TRUE));
	g_assert_false(macros_save_file(path, TRUE));
	g_assert_cmpint(stub_count("show_message"), ==, 1);
	desinstaller();
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

/* M1 : un echec deja signale n'ouvre plus de boite, sauf apres macros_forget_save_error (enregistrement explicite du menu) */
static void
test_enregistrement_explicite_reaffiche(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macros-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "absent", "m.ini", NULL);

	installer_macros();
	stub_reset();
	g_assert_false(macros_save_file(path, TRUE));
	g_assert_false(macros_save_file(path, TRUE));
	g_assert_cmpint(stub_count("show_message"), ==, 1);
	macros_forget_save_error();
	g_assert_false(macros_save_file(path, TRUE));
	g_assert_cmpint(stub_count("show_message"), ==, 2);
	desinstaller();
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

static void
iterer_boucle(void)
{
	while(g_main_context_iteration(NULL, FALSE))
		;
}

/* I1 : chargement non interactif (profil) d'un fichier illisible : jeu vide, chemin courant = ce fichier, boite differee ; le
 * premier enregistrement vise ce fichier (jamais le fichier par defaut) et en garde une copie identique a l'original. */
static void
test_chargement_profil_abime(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macros-XXXXXX", NULL);
	const gchar *original = "[gtkterm-macros]\nversion=3\n[macro:1]\naction=x\n";
	gchar *path = test_write_file(dir, "m.ini", original);
	gchar *copie = g_strconcat(path, ".avant-migration", NULL);
	gchar *texte = NULL;

	installer_macros();
	stub_reset();
	g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*m.ini*");
	g_assert_false(macros_load_file(path, FALSE));
	g_test_assert_expected_messages();
	g_assert_cmpuint(gt_macro_set_count(macros_get_set()), ==, 0);
	g_assert_cmpstr(macros_file_get_path(), ==, path);
	g_assert_cmpint(stub_count("show_message"), ==, 0);	/* jamais de boucle imbriquee pendant le chargement du profil */
	iterer_boucle();
	g_assert_cmpint(stub_count("show_message"), ==, 1);
	g_assert_nonnull(strstr(stub_last("show_message"), "m.ini"));
	g_assert_true(macros_save_file(NULL, TRUE));
	g_assert_cmpstr(macros_file_get_path(), ==, path);
	g_assert_true(g_file_get_contents(copie, &texte, NULL, NULL));
	g_assert_cmpstr(texte, ==, original);
	g_free(texte);
	g_assert_true(g_file_get_contents(path, &texte, NULL, NULL));
	g_assert_nonnull(strstr(texte, "version=2"));
	g_free(texte);
	desinstaller();
	test_rmdir(dir);
	g_free(copie);
	g_free(path);
	g_free(dir);
}

static void
test_enregistrement_puis_relecture(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macros-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "m.ini", NULL);

	installer_macros();
	g_assert_true(macros_save_file(path, FALSE));
	g_assert_cmpstr(macros_file_get_path(), ==, path);
	desinstaller();
	g_assert_true(macros_load_file(path, FALSE));
	g_assert_cmpuint(gt_macro_set_count(macros_get_set()), ==, 4);
	g_assert_cmpstr(gt_macro_set_get(macros_get_set(), 0)->args[0], ==, "42");
	desinstaller();
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

/* Charger un autre jeu arrête tout polling ; les rappels d'état atteignent le panneau. */
static void
test_polling_arrete_au_chargement(void)
{
	installer_macros();
	stub_reset();
	g_assert_true(gt_macro_set_set_polling(macros_get_set(), id_vitesse, TRUE, 1000));
	g_assert_true(gt_macro_poller_start(macros_get_poller(), id_vitesse));
	g_assert_cmpuint(stub_sent()->len, ==, 4);			/* envoi immédiat « V42\r » */
	desinstaller();
	g_assert_cmpuint(gt_macro_poller_running_count(macros_get_poller()), ==, 0);
	g_assert_cmpint(stub_count("macro_panel_polling_changed"), ==, 2);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	macros_init();
	g_test_add_func("/macros_send/arguments-enregistres", test_arguments_enregistres);
	g_test_add_func("/macros_send/arguments-fournis", test_arguments_fournis);
	g_test_add_func("/macros_send/double-pourcent", test_double_pourcent);
	g_test_add_func("/macros_send/sans-libelle", test_sans_libelle);
	g_test_add_func("/macros_send/inconnue-ou-separateur", test_inconnue_ou_separateur);
	g_test_add_func("/macros_send/ecriture-en-echec", test_ecriture_en_echec);
	g_test_add_func("/macros_send/raccourci-connecte-une-seule-fois", test_raccourci_connecte_une_seule_fois);
	g_test_add_func("/macros_send/raccourci-declare-traite", test_raccourci_declare_traite);
	g_test_add_func("/macros_send/fenetre-edition-fermee-au-changement-de-jeu", test_fenetre_edition_fermee_au_changement_de_jeu);
	g_test_add_func("/macros_send/chargement-ancien-format-listes", test_chargement_ancien_format_listes);
	g_test_add_func("/macros_send/chargement-fichier-absent", test_chargement_fichier_absent);
	g_test_add_func("/macros_send/chargement-corrompu-garde-le-jeu", test_chargement_corrompu_garde_le_jeu);
	g_test_add_func("/macros_send/enregistrement-echec-une-seule-boite", test_enregistrement_echec_une_seule_boite);
	g_test_add_func("/macros_send/enregistrement-explicite-reaffiche", test_enregistrement_explicite_reaffiche);
	g_test_add_func("/macros_send/chargement-profil-abime", test_chargement_profil_abime);
	g_test_add_func("/macros_send/enregistrement-puis-relecture", test_enregistrement_puis_relecture);
	g_test_add_func("/macros_send/polling-arrete-au-chargement", test_polling_arrete_au_chargement);
	return g_test_run();
}
