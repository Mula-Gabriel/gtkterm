/* Tests de project_file.c : fichier projet .gtkterm (aller-retour, format, refus tout ou rien, instantané de comparaison). */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "project_file.h"
#include "test_util.h"

static void
comparer_macro(const GtMacro *a, const GtMacro *b)
{
	g_assert_cmpstr(a->label, ==, b->label);
	g_assert_cmpstr(a->shortcut, ==, b->shortcut);
	g_assert_cmpstr(a->action, ==, b->action);
	g_assert_cmpstr(a->tab, ==, b->tab);
	g_assert_cmpint(a->polling, ==, b->polling);
	g_assert_cmpuint(a->polling_period_ms, ==, b->polling_period_ms);
	g_assert_cmpint(a->args == NULL, ==, b->args == NULL);
	if(a->args != NULL)
	{
		g_assert_cmpuint(g_strv_length(a->args), ==, g_strv_length(b->args));
		for(guint k = 0; a->args[k] != NULL; k++)
			g_assert_cmpstr(a->args[k], ==, b->args[k]);
	}
}

static GtProjectData *
deux_onglets(void)
{
	GtProjectData *d = gt_project_data_new();
	GtProjectTab *t0 = gt_project_tab_new(), *t1 = gt_project_tab_new();
	GtMacroLists *lists = gt_macro_lists_new();
	const gchar *args[] = { "5", NULL };
	guint id;

	t0->profile.transport = TRANSPORT_SERIAL;
	gt_profile_set_string(&t0->profile.port, "/dev/pts/9");
	t0->profile.baudrate = 19200;
	gt_profile_set_string(&t0->profile_name, "banc");
	t0->page.h_paned = 420;
	t0->page.macro_panel_visible = FALSE;
	gt_profile_set_string(&t0->script_layout, "leftright");
	id = gt_macro_set_append(t0->macros, "Ping", "", "PING%d\\r", "");
	gt_macro_set_set_args(t0->macros, id, args, 1);
	gt_macro_set_set_polling(t0->macros, id, TRUE, 250);
	gt_macro_lists_add(lists, "Modes", "Alpha", "a");
	gt_macro_set_take_lists(t0->macros, lists);
	gt_profile_set_string(&t0->script, "-- \303\251\nprint(\"a;b\\\\c\")\n\n");

	t1->profile.transport = TRANSPORT_TCP_CLIENT;
	gt_profile_set_string(&t1->profile.host, "127.0.0.1");
	gt_profile_set_string(&t1->profile.tcp_port, "4000");

	g_ptr_array_add(d->tabs, t0);
	g_ptr_array_add(d->tabs, t1);
	d->active = 1;
	return d;
}

static gchar *
ecrire_fichier(const gchar *dir, const gchar *contenu)
{
	gchar *path = g_build_filename(dir, "p.gtkterm", NULL);

	g_assert_true(g_file_set_contents(path, contenu, -1, NULL));
	return path;
}

static void
test_aller_retour(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-project-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "p.gtkterm", NULL);
	GtProjectData *d = deux_onglets(), *r = NULL;
	GtFileStamp stamp = { 0 };
	GError *error = NULL;

	g_assert_true(gt_project_data_save(d, path, &stamp, &error));
	g_assert_no_error(error);
	g_assert_true(stamp.exists);
	memset(&stamp, 0, sizeof stamp);
	g_assert_true(gt_project_data_load(path, &r, &stamp, &error));
	g_assert_no_error(error);
	g_assert_true(stamp.exists);
	g_assert_nonnull(r);
	g_assert_cmpuint(r->tabs->len, ==, 2);
	g_assert_cmpuint(r->active, ==, 1);
	for(guint i = 0; i < 2; i++)
	{
		const GtProjectTab *a = g_ptr_array_index(d->tabs, i), *b = g_ptr_array_index(r->tabs, i);

		g_assert_cmpint(a->profile.transport, ==, b->profile.transport);
		g_assert_cmpstr(a->profile.port, ==, b->profile.port);
		g_assert_cmpstr(a->profile.host, ==, b->profile.host);
		g_assert_cmpstr(a->profile.tcp_port, ==, b->profile.tcp_port);
		g_assert_cmpuint(a->profile.baudrate, ==, b->profile.baudrate);
		g_assert_true(b->profile.lock_port);
		g_assert_null(b->profile.macros_file);
		g_assert_cmpstr(a->profile_name, ==, b->profile_name);
		g_assert_cmpint(a->page.h_paned, ==, b->page.h_paned);
		g_assert_cmpint(a->page.v_paned, ==, b->page.v_paned);
		g_assert_cmpint(a->page.macro_panel_visible, ==, b->page.macro_panel_visible);
		g_assert_cmpint(a->page.script_panel_visible, ==, b->page.script_panel_visible);
		g_assert_cmpint(a->page.hex_bar_visible, ==, b->page.hex_bar_visible);
		g_assert_cmpstr(a->script_layout, ==, b->script_layout);
		g_assert_cmpstr(a->script, ==, b->script);
		g_assert_cmpuint(gt_macro_set_count(a->macros), ==, gt_macro_set_count(b->macros));
		for(guint k = 0; k < gt_macro_set_count(a->macros); k++)
			comparer_macro(gt_macro_set_get(a->macros, k), gt_macro_set_get(b->macros, k));
		g_assert_cmpint(gt_macro_lists_count(gt_macro_set_get_lists(a->macros)), ==, gt_macro_lists_count(gt_macro_set_get_lists(b->macros)));
	}
	{
		const GtProjectTab *b = g_ptr_array_index(r->tabs, 0);
		const GtMacroLists *l = gt_macro_set_get_lists(b->macros);

		g_assert_cmpint(gt_macro_lists_count(l), ==, 1);
		g_assert_cmpstr(gt_macro_lists_name(l, 0), ==, "Modes");
		g_assert_cmpstr(gt_macro_lists_entry_value(l, 0, 0), ==, "a");
	}
	gt_project_data_free(r);
	gt_project_data_free(d);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

static void
test_aucun_chemin(void)
{
	GtProjectData *d = deux_onglets();
	GtProjectTab *t = g_ptr_array_index(d->tabs, 0);
	gchar *texte;

	gt_profile_set_string(&t->profile.macros_file, "/tmp/x.ini");
	texte = gt_project_data_to_text(d);
	g_assert_null(strstr(texte, "macros_file"));
	g_assert_null(strstr(texte, ".ini"));
	g_assert_null(strstr(texte, ".lua"));
	g_free(texte);
	texte = gt_project_data_snapshot(d);
	g_assert_null(strstr(texte, "macros_file"));
	g_free(texte);
	gt_project_data_free(d);
}

static void
test_format(void)
{
	GtProjectData *d = deux_onglets();
	gchar *t = gt_project_data_to_text(d);

	g_assert_true(g_str_has_prefix(t, "[gtkterm-project]\nversion=1\nactive=1\n"));
	g_assert_nonnull(strstr(t, "[tab:0]\n"));
	g_assert_nonnull(strstr(t, "[tab:0:macro:1]\n"));
	g_assert_nonnull(strstr(t, "[tab:0:list:1]\n"));
	g_assert_nonnull(strstr(t, "[tab:1]\n"));
	g_assert_nonnull(strstr(t, "script_layout=leftright\n"));
	g_assert_nonnull(strstr(t, "profile_name=banc\n"));
	g_free(t);
	gt_project_data_free(d);
}

static void
refus(const gchar *dir, const gchar *contenu, GQuark domain, gint code, const gchar *fragment)
{
	gchar *path = contenu != NULL ? ecrire_fichier(dir, contenu) : g_build_filename(dir, "absent.gtkterm", NULL);
	GtProjectData *out = NULL;
	GError *error = NULL;

	g_assert_false(gt_project_data_load(path, &out, NULL, &error));
	g_assert_null(out);
	g_assert_error(error, domain, code);
	if(fragment != NULL)
		g_assert_nonnull(strstr(error->message, fragment));
	g_clear_error(&error);
	g_free(path);
}

static void
test_refus(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-project-XXXXXX", NULL);

	refus(dir, NULL, G_FILE_ERROR, G_FILE_ERROR_NOENT, "absent.gtkterm");
	refus(dir, "[x", GT_PROJECT_ERROR, GT_PROJECT_ERROR_NOT_PROJECT, NULL);
	refus(dir, "[gtkterm-macros]\nversion=2\n[macro:1]\naction=x\n", GT_PROJECT_ERROR, GT_PROJECT_ERROR_NOT_PROJECT, NULL);
	refus(dir, "[gtkterm-project]\nversion=2\nactive=0\n[tab:0]\ntransport=serial\n", GT_PROJECT_ERROR, GT_PROJECT_ERROR_VERSION, NULL);
	refus(dir, "[gtkterm-project]\nactive=0\n[tab:0]\ntransport=serial\n", GT_PROJECT_ERROR, GT_PROJECT_ERROR_VERSION, NULL);
	refus(dir, "[gtkterm-project]\nversion=1\nactive=0\n", GT_PROJECT_ERROR, GT_PROJECT_ERROR_NO_TAB, NULL);
	refus(dir, "[gtkterm-project]\nversion=1\nactive=0\n[tab:0]\nport=/dev/x\n", GT_PROJECT_ERROR, GT_PROJECT_ERROR_BAD_TAB, "tab 1 has no valid port settings");
	refus(dir, "[gtkterm-project]\nversion=1\nactive=0\n[tab:0]\ntransport=modem\n", GT_PROJECT_ERROR, GT_PROJECT_ERROR_BAD_TAB, "tab 1 has no valid port settings");
	refus(dir, "[gtkterm-project]\nversion=1\nactive=0\n[tab:0]\ntransport=serial\n[tab:1]\ntransport=modem\n", GT_PROJECT_ERROR, GT_PROJECT_ERROR_BAD_TAB, "tab 2 has no valid port settings");
	test_rmdir(dir);
	g_free(dir);
}

static void
test_active_hors_bornes(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-project-XXXXXX", NULL);
	gchar *path = ecrire_fichier(dir, "[gtkterm-project]\nversion=1\nactive=7\n[tab:0]\ntransport=serial\n");
	GtProjectData *r = NULL;

	g_assert_true(gt_project_data_load(path, &r, NULL, NULL));
	g_assert_cmpuint(r->tabs->len, ==, 1);
	g_assert_cmpuint(r->active, ==, 0);
	gt_project_data_free(r);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

/* Script non UTF-8 (projet modifié à la main) : octets invalides remplacés (U+FFFD) avec un avertissement, échappements toujours interprétés ;
 * l'éditeur reçoit un texte valide et le Save suivant le garde */
static void
test_script_non_utf8(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-project-XXXXXX", NULL);
	gchar *path = ecrire_fichier(dir, "[gtkterm-project]\nversion=1\nactive=0\n[tab:0]\ntransport=serial\nscript=print(\"\xff\")\\nfin\n");
	GtProjectData *r = NULL;
	GtProjectTab *t;

	g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*not valid UTF-8*");
	g_assert_true(gt_project_data_load(path, &r, NULL, NULL));
	g_test_assert_expected_messages();
	t = g_ptr_array_index(r->tabs, 0);
	g_assert_true(g_utf8_validate(t->script, -1, NULL));
	g_assert_cmpstr(t->script, ==, "print(\"\xef\xbf\xbd\")\nfin");
	gt_project_data_free(r);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

static void
test_instantane(void)
{
	GtProjectData *d = deux_onglets();
	GtProjectTab *t0 = g_ptr_array_index(d->tabs, 0);
	gchar *ref = gt_project_data_snapshot(d), *autre;

	/* Sans effet sur l'instantané */
	t0->page.h_paned = 111;
	t0->page.hex_bar_visible = TRUE;
	gt_profile_set_string(&t0->script_layout, "topbottom");
	d->active = 0;
	autre = gt_project_data_snapshot(d);
	g_assert_cmpstr(ref, ==, autre);
	g_free(autre);

	/* Avec effet */
	{
		const gchar *args[] = { "6", NULL };
		guint id = gt_macro_set_get(t0->macros, 0)->id;

		gt_macro_set_set_args(t0->macros, id, args, 1);
		autre = gt_project_data_snapshot(d);
		g_assert_cmpstr(ref, !=, autre);
		g_free(autre);
		args[0] = "5";
		gt_macro_set_set_args(t0->macros, id, args, 1);
		gt_macro_set_set_polling(t0->macros, id, TRUE, 500);
		autre = gt_project_data_snapshot(d);
		g_assert_cmpstr(ref, !=, autre);
		g_free(autre);
		gt_macro_set_set_polling(t0->macros, id, TRUE, 250);
	}
	autre = gt_project_data_snapshot(d);
	g_assert_cmpstr(ref, ==, autre);
	g_free(autre);
	t0->script[3] = 'X';
	autre = gt_project_data_snapshot(d);
	g_assert_cmpstr(ref, !=, autre);
	g_free(autre);
	t0->script[3] = '\303';
	t0->profile.baudrate = 9600;
	autre = gt_project_data_snapshot(d);
	g_assert_cmpstr(ref, !=, autre);
	g_free(autre);
	t0->profile.baudrate = 19200;
	g_ptr_array_insert(d->tabs, 0, g_ptr_array_steal_index(d->tabs, 1));
	autre = gt_project_data_snapshot(d);
	g_assert_cmpstr(ref, !=, autre);
	g_free(autre);
	g_free(ref);
	gt_project_data_free(d);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/project_file/aller-retour", test_aller_retour);
	g_test_add_func("/project_file/aucun-chemin", test_aucun_chemin);
	g_test_add_func("/project_file/format", test_format);
	g_test_add_func("/project_file/refus", test_refus);
	g_test_add_func("/project_file/active-hors-bornes", test_active_hors_bornes);
	g_test_add_func("/project_file/script-non-utf8", test_script_non_utf8);
	g_test_add_func("/project_file/instantane", test_instantane);
	return g_test_run();
}
