/* Tests unitaires du magasin de configuration (config_store.c) : chargement, profils, réglages globaux, enregistrement. */

#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config_store.h"

static gchar *dossier;

static gchar *
chemin(const gchar *nom)
{
	return g_build_filename(dossier, nom, NULL);
}

static gchar *
ecrire(const gchar *nom, const gchar *contenu)
{
	gchar *p = chemin(nom);

	g_assert_true(g_file_set_contents(p, contenu, -1, NULL));
	return p;
}

static gchar *
lire(const gchar *p)
{
	gchar *c = NULL;

	g_assert_true(g_file_get_contents(p, &c, NULL, NULL));
	return c;
}

static GtConfigStore *
charger(const gchar *p)
{
	GError *err = NULL;
	GtConfigStore *s = gt_config_store_load(p, &err);

	g_assert_no_error(err);
	g_assert_nonnull(s);
	return s;
}

static void
enregistrer(GtConfigStore *s)
{
	GError *err = NULL;

	g_assert_true(gt_config_store_save(s, &err));
	g_assert_no_error(err);
}

static void
test_fichier_absent(void)
{
	gchar *p = chemin("absent.rc");
	GtConfigStore *s = charger(p);
	gchar **noms = gt_config_store_list_profiles(s);
	GtProfile prof = { 0 };

	g_assert_false(gt_config_store_is_read_only(s));
	g_assert_false(gt_config_store_was_migrated(s));
	g_assert_cmpuint(g_strv_length(noms), ==, 0);
	g_assert_false(gt_config_store_has_profile(s, "default"));
	g_assert_false(gt_config_store_get_profile(s, "default", &prof));
	g_assert_cmpstr(gt_config_store_get_path(s), ==, p);
	g_strfreev(noms);
	gt_config_store_free(s);
	g_free(p);
}

static void
test_fichier_vide(void)
{
	gchar *p = ecrire("vide.rc", "");
	GtConfigStore *s = charger(p);
	gchar **noms = gt_config_store_list_profiles(s);

	g_assert_false(gt_config_store_is_read_only(s));
	g_assert_cmpuint(g_strv_length(noms), ==, 0);
	g_strfreev(noms);
	gt_config_store_free(s);
	g_free(p);
}

static void
test_illisible_lecture_seule(void)
{
	gchar *p = ecrire("syntaxe.rc", "ceci n'est pas un fichier ini\n[profile:a]\nport=/dev/x\n");
	gchar *avant = lire(p);
	GError *err = NULL;
	GtConfigStore *s = gt_config_store_load(p, &err);
	gchar *apres;

	g_assert_nonnull(err);
	g_clear_error(&err);
	g_assert_nonnull(s);
	g_assert_true(gt_config_store_is_read_only(s));
	g_assert_false(gt_config_store_has_profile(s, "a"));
	g_assert_false(gt_config_store_save(s, &err));
	g_assert_nonnull(err);
	g_assert_nonnull(strstr(err->message, p));
	g_clear_error(&err);
	apres = lire(p);
	g_assert_cmpstr(apres, ==, avant);
	gt_config_store_free(s);
	g_free(avant);
	g_free(apres);
	g_free(p);

	if(geteuid() != 0)
	{
		p = ecrire("droits.rc", "[profile:a]\nport=/dev/x\n");
		g_assert_cmpint(g_chmod(p, 0), ==, 0);
		s = gt_config_store_load(p, &err);
		g_assert_nonnull(err);
		g_clear_error(&err);
		g_assert_true(gt_config_store_is_read_only(s));
		g_assert_false(gt_config_store_save(s, NULL));
		gt_config_store_free(s);
		g_chmod(p, 0600);
		g_free(p);
	}
}

static void
profil_complet(GtProfile *p)
{
	gt_profile_init_defaults(p);
	p->transport = TRANSPORT_TCP_SERVER;
	gt_profile_set_string(&p->port, "/dev/ttyUSB7");
	p->baudrate = 230400;
	p->bits = 7;
	p->stopbits = 2;
	p->parity = GT_PARITY_ODD;
	p->flow = GT_FLOW_RTSCTS;
	p->rs485_before_ms = 11;
	p->rs485_after_ms = 12;
	gt_profile_set_string(&p->host, "::1");
	gt_profile_set_string(&p->tcp_port, "2001");
	p->wait_delay_ms = 40;
	p->wait_char = 233;
	p->echo = TRUE;
	p->crlf_auto = TRUE;
	p->auto_reconnect = TRUE;
	p->esc_clear_screen = TRUE;
	p->timestamp = TRUE;
	gt_profile_set_string(&p->font, "DejaVu Sans Mono Bold 11");
	p->block_cursor = FALSE;
	p->columns = 132;
	p->rows = 43;
	p->scrollback = 5000;
	p->visual_bell = TRUE;
	p->foreground = (GtColor){ 0.25, 0.5, 0.75, 1.0 };
	p->background = (GtColor){ 0.125, 0.0, 1.0, 0.5 };
	gt_profile_set_string(&p->macros_file, "/home/u/mes macros/a.ini");
}

static void
comparer(const GtProfile *a, const GtProfile *b)
{
	g_assert_cmpint(a->transport, ==, b->transport);
	g_assert_cmpstr(a->port, ==, b->port);
	g_assert_cmpuint(a->baudrate, ==, b->baudrate);
	g_assert_cmpint(a->bits, ==, b->bits);
	g_assert_cmpint(a->stopbits, ==, b->stopbits);
	g_assert_cmpint(a->parity, ==, b->parity);
	g_assert_cmpint(a->flow, ==, b->flow);
	g_assert_cmpint(a->rs485_before_ms, ==, b->rs485_before_ms);
	g_assert_cmpint(a->rs485_after_ms, ==, b->rs485_after_ms);
	g_assert_cmpstr(a->host, ==, b->host);
	g_assert_cmpstr(a->tcp_port, ==, b->tcp_port);
	g_assert_cmpint(a->wait_delay_ms, ==, b->wait_delay_ms);
	g_assert_cmpint(a->wait_char, ==, b->wait_char);
	g_assert_cmpint(a->echo, ==, b->echo);
	g_assert_cmpint(a->crlf_auto, ==, b->crlf_auto);
	g_assert_cmpint(a->auto_reconnect, ==, b->auto_reconnect);
	g_assert_cmpint(a->esc_clear_screen, ==, b->esc_clear_screen);
	g_assert_cmpint(a->timestamp, ==, b->timestamp);
	g_assert_cmpstr(a->font, ==, b->font);
	g_assert_cmpint(a->block_cursor, ==, b->block_cursor);
	g_assert_cmpint(a->columns, ==, b->columns);
	g_assert_cmpint(a->rows, ==, b->rows);
	g_assert_cmpint(a->scrollback, ==, b->scrollback);
	g_assert_cmpint(a->visual_bell, ==, b->visual_bell);
	g_assert_cmpfloat(a->foreground.green, ==, b->foreground.green);
	g_assert_cmpfloat(a->background.alpha, ==, b->background.alpha);
	g_assert_cmpfloat(a->background.red, ==, b->background.red);
	g_assert_cmpstr(a->macros_file, ==, b->macros_file);
}

static void
test_aller_retour(void)
{
	gchar *p = chemin("aller-retour.rc");
	GtConfigStore *s = charger(p);
	GtProfile ecrit, lu = { 0 };
	gchar *contenu;

	profil_complet(&ecrit);
	gt_config_store_put_profile(s, "atelier", &ecrit);
	enregistrer(s);
	gt_config_store_free(s);

	contenu = lire(p);
	g_assert_nonnull(strstr(contenu, "[profile:atelier]"));
	g_assert_nonnull(strstr(contenu, "transport=tcp-server"));
	g_assert_nonnull(strstr(contenu, "parity=odd"));
	g_assert_nonnull(strstr(contenu, "flow=rts"));
	g_assert_nonnull(strstr(contenu, "font=DejaVu Sans Mono Bold 11"));
	g_assert_nonnull(strstr(contenu, "term_columns=132"));
	g_assert_nonnull(strstr(contenu, "echo=true"));
	g_assert_null(strstr(contenu, "lock_port"));

	s = charger(p);
	g_assert_true(gt_config_store_get_profile(s, "atelier", &lu));
	comparer(&lu, &ecrit);
	g_assert_true(lu.lock_port);	/* jamais enregistré : défaut */
	gt_profile_clear(&ecrit);
	gt_profile_clear(&lu);
	gt_config_store_free(s);
	g_free(contenu);
	g_free(p);
}

static void
test_cles_absentes_defauts(void)
{
	gchar *p = ecrire("partiel.rc", "[profile:x]\nport=/dev/ttyX\necho=True\nspeed=9600\n");
	GtConfigStore *s = charger(p);
	GtProfile lu = { 0 };

	g_assert_true(gt_config_store_get_profile(s, "x", &lu));
	g_assert_cmpstr(lu.port, ==, "/dev/ttyX");
	g_assert_cmpuint(lu.baudrate, ==, 9600);
	g_assert_true(lu.echo);
	g_assert_cmpint(lu.bits, ==, 8);
	g_assert_cmpstr(lu.font, ==, "Monospace 12");
	g_assert_null(lu.macros_file);
	gt_profile_clear(&lu);
	gt_config_store_free(s);
	g_free(p);
}

static void
test_valeurs_fautives_ignorees(void)
{
	gchar *p = ecrire("fautif.rc", "[profile:x]\nspeed=vite\nbits=huit\nparity=mark\nterm_foreground=0.5;0.5\necho=peut-etre\nport=/dev/ttyQ\n");
	GtConfigStore *s = charger(p);
	GtProfile lu = { 0 };

	g_assert_true(gt_config_store_get_profile(s, "x", &lu));
	g_assert_cmpuint(lu.baudrate, ==, 115200);
	g_assert_cmpint(lu.bits, ==, 8);
	g_assert_cmpint(lu.parity, ==, GT_PARITY_NONE);
	g_assert_cmpfloat(lu.foreground.alpha, ==, 1.0);
	g_assert_false(lu.echo);
	g_assert_cmpstr(lu.port, ==, "/dev/ttyQ");
	gt_profile_clear(&lu);
	gt_config_store_free(s);
	g_free(p);
}

static void
test_casse_des_noms(void)
{
	gchar *p = chemin("casse.rc");
	GtConfigStore *s = charger(p);
	GtProfile a, lu = { 0 };
	gchar **noms;

	gt_profile_init_defaults(&a);
	gt_profile_set_string(&a.port, "/dev/un");
	gt_config_store_put_profile(s, "Atelier", &a);
	gt_profile_set_string(&a.port, "/dev/deux");
	gt_config_store_put_profile(s, "ATELIER", &a);
	gt_config_store_put_profile(s, "Équipe", &a);
	g_assert_true(gt_config_store_has_profile(s, "atelier"));
	g_assert_true(gt_config_store_has_profile(s, "équipe"));
	noms = gt_config_store_list_profiles(s);
	g_assert_cmpuint(g_strv_length(noms), ==, 2);
	g_assert_cmpstr(noms[0], ==, "Atelier");
	g_assert_cmpstr(noms[1], ==, "Équipe");
	g_strfreev(noms);
	g_assert_true(gt_config_store_get_profile(s, "aTeLiEr", &lu));
	g_assert_cmpstr(lu.port, ==, "/dev/deux");
	g_assert_true(gt_config_store_delete_profile(s, "atelier"));
	g_assert_false(gt_config_store_has_profile(s, "Atelier"));
	g_assert_false(gt_config_store_delete_profile(s, "atelier"));
	gt_profile_clear(&a);
	gt_profile_clear(&lu);
	gt_config_store_free(s);
	g_free(p);
}

static void
test_liste_triee(void)
{
	gchar *p = chemin("tri.rc");
	GtConfigStore *s = charger(p);
	GtProfile a;
	gchar **noms;

	gt_profile_init_defaults(&a);
	gt_config_store_put_profile(s, "b", &a);
	gt_config_store_put_profile(s, "A", &a);
	gt_config_store_put_profile(s, "c", &a);
	noms = gt_config_store_list_profiles(s);
	g_assert_cmpstr(noms[0], ==, "A");
	g_assert_cmpstr(noms[1], ==, "b");
	g_assert_cmpstr(noms[2], ==, "c");
	g_assert_null(noms[3]);
	g_strfreev(noms);
	gt_profile_clear(&a);
	gt_config_store_free(s);
	g_free(p);
}

static void
test_groupes_conserves(void)
{
	gchar *p = ecrire("groupes.rc", "[window]\nwidth=750\n\n[session]\nscript_path=/home/u/mes scripts/a.lua\n\n[extension]\nfoo=bar\n\n[profile:default]\nport=/dev/ttyS1\n");
	GtConfigStore *s = charger(p);
	GtProfile a;
	GKeyFile *kf;
	gchar *v;

	gt_profile_init_defaults(&a);
	gt_config_store_put_profile(s, "autre", &a);
	enregistrer(s);
	gt_config_store_free(s);

	s = charger(p);
	kf = gt_config_store_get_key_file(s);
	g_assert_cmpint(g_key_file_get_integer(kf, "window", "width", NULL), ==, 750);
	v = g_key_file_get_string(kf, "session", "script_path", NULL);
	g_assert_cmpstr(v, ==, "/home/u/mes scripts/a.lua");
	g_free(v);
	v = g_key_file_get_string(kf, "extension", "foo", NULL);
	g_assert_cmpstr(v, ==, "bar");
	g_free(v);
	g_assert_true(gt_config_store_has_profile(s, "default"));
	g_assert_true(gt_config_store_has_profile(s, "autre"));
	gt_profile_clear(&a);
	gt_config_store_free(s);
	g_free(p);
}

static void
test_ecriture_partielle(void)
{
	gchar *p = chemin("partielle.rc");
	GtConfigStore *s = charger(p);
	GtProfile a, lu = { 0 };

	gt_profile_init_defaults(&a);
	gt_profile_set_string(&a.port, "/dev/ttyP");
	gt_config_store_put_profile(s, "default", &a);
	gt_config_store_set_profile_string(s, "DEFAULT", "macros_file", "/m.ini");
	g_assert_true(gt_config_store_get_profile(s, "default", &lu));
	g_assert_cmpstr(lu.port, ==, "/dev/ttyP");
	g_assert_cmpstr(lu.macros_file, ==, "/m.ini");
	gt_config_store_set_profile_string(s, "default", "macros_file", NULL);
	g_assert_true(gt_config_store_get_profile(s, "default", &lu));
	g_assert_null(lu.macros_file);
	gt_profile_clear(&a);
	gt_profile_clear(&lu);
	gt_config_store_free(s);
	g_free(p);
}

static void
test_ecriture_partielle_profil_absent(void)
{
	gchar *p = chemin("absent-partiel.rc");
	GtConfigStore *s = charger(p);
	GtProfile lu = { 0 };

	gt_config_store_set_profile_string(s, "neuf", "macros_file", "/n.ini");
	g_assert_true(gt_config_store_get_profile(s, "neuf", &lu));
	g_assert_cmpstr(lu.macros_file, ==, "/n.ini");
	g_assert_cmpstr(lu.port, ==, "/dev/ttyS0");
	gt_profile_clear(&lu);
	gt_config_store_free(s);
	g_free(p);
}

static void
test_reglages_globaux(void)
{
	gchar *p = chemin("general.rc");
	GtConfigStore *s = charger(p);
	gchar *v;

	v = gt_config_store_get_general_string(s, "update_url", "defaut");
	g_assert_cmpstr(v, ==, "defaut");
	g_free(v);
	g_assert_true(gt_config_store_get_general_boolean(s, "update_check_startup", TRUE));
	gt_config_store_set_general_string(s, "update_url", "https://exemple.invalid/x.git");
	gt_config_store_set_general_boolean(s, "update_check_startup", FALSE);
	enregistrer(s);
	gt_config_store_free(s);

	s = charger(p);
	v = gt_config_store_get_general_string(s, "update_url", "defaut");
	g_assert_cmpstr(v, ==, "https://exemple.invalid/x.git");
	g_free(v);
	g_assert_false(gt_config_store_get_general_boolean(s, "update_check_startup", TRUE));
	gt_config_store_free(s);
	g_free(p);
}

static void
test_enregistrement_droits_et_dossier(void)
{
	gchar *sous = g_build_filename(dossier, "a", "b", NULL);
	gchar *p = g_build_filename(sous, ".gtktermrc", NULL);
	GtConfigStore *s = charger(p);
	GtProfile a;
	struct stat st;

	gt_profile_init_defaults(&a);
	gt_config_store_put_profile(s, "default", &a);
	enregistrer(s);
	g_assert_cmpint(g_stat(p, &st), ==, 0);
	g_assert_cmpint(st.st_mode & 0777, ==, 0600);
	gt_profile_clear(&a);
	gt_config_store_free(s);
	g_unlink(p);
	g_rmdir(sous);
	g_free(p);
	g_free(sous);
	sous = g_build_filename(dossier, "a", NULL);
	g_rmdir(sous);
	g_free(sous);
}

static void
test_booleens_tolerants(void)
{
	static const struct { const gchar *texte; gboolean ok; gboolean valeur; } cas[] = {
		{ "true", TRUE, TRUE }, { "True", TRUE, TRUE }, { "TRUE", TRUE, TRUE }, { "yes", TRUE, TRUE }, { "on", TRUE, TRUE }, { "1", TRUE, TRUE },
		{ "false", TRUE, FALSE }, { "False", TRUE, FALSE }, { "no", TRUE, FALSE }, { "off", TRUE, FALSE }, { "0", TRUE, FALSE },
		{ "peut-etre", FALSE, FALSE }, { "", FALSE, FALSE },
	};

	for(guint i = 0; i < G_N_ELEMENTS(cas); i++)
	{
		gboolean v = 42;

		g_assert_cmpint(gt_config_parse_boolean(cas[i].texte, &v), ==, cas[i].ok);
		if(cas[i].ok)
			g_assert_cmpint(v, ==, cas[i].valeur);
		else
			g_assert_cmpint(v, ==, 42);
	}
	g_assert_false(gt_config_parse_boolean(NULL, NULL));
}

/* Fichier tel que l'écrivait parsecfg (cfgDump) puis complété par GKeyFile (groupes réservés) */
static const gchar ancien_fichier[] =
	"[default]\n"
	"port\t= /dev/ttyUSB0\n"
	"speed\t= 115200\n"
	"bits\t= 8\n"
	"stopbits\t= 1\n"
	"parity\t= none\n"
	"flow\t= none\n"
	"wait_delay\t= 0\n"
	"wait_char\t= -23\n"
	"rs485_rts_time_before_tx\t= 30\n"
	"rs485_rts_time_after_tx\t= 30\n"
	"echo\t= True\n"
	"crlfauto\t= False\n"
	"autoreconnect_enabled\t= True\n"
	"esc_clear_screen\t= False\n"
	"timestamp\t= False\n"
	"show_rxtx\t= False\n"
	"font\t= \"Monospace 12\"\n"
	"term_block_cursor\t= True\n"
	"term_rows\t= 80\n"
	"term_columns\t= 25\n"
	"term_scrollback\t= 10000\n"
	"term_visual_bell\t= True\n"
	"term_foreground_red\t= 0.000000\n"
	"term_foreground_blue\t= 0.000000\n"
	"term_foreground_green\t= 0.000000\n"
	"term_foreground_alpha\t= 0.000000\n"
	"term_background_red\t= 1.000000\n"
	"term_background_blue\t= 0.500000\n"
	"term_background_green\t= 1.000000\n"
	"term_background_alpha\t= 1.000000\n"
	"macros_file\t= \"/home/u/mes macros.ini\"\n"
	"transport_type\t= 0\n"
	"socket_host\t= \n"
	"socket_port\t= \n"
	"update_url\t= https://exemple.invalid/fork.git\n"
	"update_check_startup\t= False\n"
	"update_prefix\t= /opt/gtkterm\n"
	"\n"
	"[atelier]\n"
	"port\t= /dev/ttyACM0\n"
	"speed\t= 9600\n"
	"parity\t= even\n"
	"flow\t= rs485\n"
	"transport_type\t= 1\n"
	"socket_host\t= 192.168.1.10\n"
	"socket_port\t= 2000\n"
	"update_url\t= https://autre.invalid/x.git\n"
	"\n"
	"[\"mon profil\"]\n"
	"port\t= '/dev/serial/by-id/usb-\"x\"'\n"
	"speed\t= 57600\n"
	"\n"
	"[Atelier]\n"
	"port\t= /dev/ttyACM9\n"
	"\n"
	"[window]\n"
	"x=10\n"
	"width=750\n"
	"\n"
	"[session]\n"
	"script_path=/home/u/mes scripts/a.lua\n";

static void
test_migration_ancien_format(void)
{
	gchar *p = ecrire("ancien.rc", ancien_fichier);
	GtConfigStore *s = charger(p);
	GtProfile d = { 0 }, a = { 0 }, m = { 0 }, a2 = { 0 };
	gchar **noms;
	gchar *v;
	GKeyFile *kf;

	g_assert_true(gt_config_store_was_migrated(s));
	g_assert_false(gt_config_store_is_read_only(s));
	noms = gt_config_store_list_profiles(s);
	g_assert_cmpuint(g_strv_length(noms), ==, 4);
	g_assert_cmpstr(noms[0], ==, "atelier");
	g_assert_cmpstr(noms[1], ==, "Atelier (2)");
	g_assert_cmpstr(noms[2], ==, "default");
	g_assert_cmpstr(noms[3], ==, "mon profil");
	g_strfreev(noms);

	g_assert_true(gt_config_store_get_profile(s, "default", &d));
	g_assert_cmpint(d.transport, ==, TRANSPORT_SERIAL);
	g_assert_cmpstr(d.port, ==, "/dev/ttyUSB0");
	g_assert_cmpuint(d.baudrate, ==, 115200);
	g_assert_true(d.echo);
	g_assert_false(d.crlf_auto);
	g_assert_true(d.auto_reconnect);
	g_assert_cmpint(d.wait_char, ==, 233);
	g_assert_cmpstr(d.font, ==, "Monospace 12");
	g_assert_true(d.block_cursor);
	g_assert_cmpint(d.columns, ==, 80);
	g_assert_cmpint(d.rows, ==, 25);
	g_assert_cmpint(d.scrollback, ==, 10000);
	g_assert_true(d.visual_bell);
	g_assert_cmpfloat(d.foreground.alpha, ==, 0.0);
	g_assert_cmpfloat(d.background.blue, ==, 0.5);
	g_assert_cmpfloat(d.background.green, ==, 1.0);
	g_assert_cmpstr(d.macros_file, ==, "/home/u/mes macros.ini");
	g_assert_cmpstr(d.host, ==, "");

	g_assert_true(gt_config_store_get_profile(s, "atelier", &a));
	g_assert_cmpint(a.transport, ==, TRANSPORT_TCP_CLIENT);
	g_assert_cmpstr(a.host, ==, "192.168.1.10");
	g_assert_cmpstr(a.tcp_port, ==, "2000");
	g_assert_cmpint(a.parity, ==, GT_PARITY_EVEN);
	g_assert_cmpint(a.flow, ==, GT_FLOW_RS485);

	g_assert_true(gt_config_store_get_profile(s, "mon profil", &m));
	g_assert_cmpstr(m.port, ==, "/dev/serial/by-id/usb-\"x\"");
	g_assert_true(gt_config_store_get_profile(s, "Atelier (2)", &a2));
	g_assert_cmpstr(a2.port, ==, "/dev/ttyACM9");

	v = gt_config_store_get_general_string(s, "update_url", NULL);
	g_assert_cmpstr(v, ==, "https://exemple.invalid/fork.git");
	g_free(v);
	v = gt_config_store_get_general_string(s, "update_prefix", NULL);
	g_assert_cmpstr(v, ==, "/opt/gtkterm");
	g_free(v);
	g_assert_false(gt_config_store_get_general_boolean(s, "update_check_startup", TRUE));

	kf = gt_config_store_get_key_file(s);
	g_assert_false(g_key_file_has_key(kf, "profile:default", "update_url", NULL));
	g_assert_false(g_key_file_has_key(kf, "profile:atelier", "update_url", NULL));
	g_assert_false(g_key_file_has_key(kf, "profile:default", "show_rxtx", NULL));
	g_assert_false(g_key_file_has_key(kf, "profile:default", "term_foreground_red", NULL));
	g_assert_false(g_key_file_has_group(kf, "default"));
	g_assert_cmpint(g_key_file_get_integer(kf, "window", "width", NULL), ==, 750);
	v = g_key_file_get_string(kf, "session", "script_path", NULL);
	g_assert_cmpstr(v, ==, "/home/u/mes scripts/a.lua");
	g_free(v);

	gt_profile_clear(&d);
	gt_profile_clear(&a);
	gt_profile_clear(&m);
	gt_profile_clear(&a2);
	gt_config_store_free(s);
	g_free(p);
}

static void
test_migration_copie_de_sauvegarde(void)
{
	gchar *p = ecrire("sauvegarde.rc", ancien_fichier);
	gchar *copie = g_strconcat(p, ".avant-migration", NULL);
	GtConfigStore *s = charger(p);
	gchar *c;

	g_assert_false(g_file_test(copie, G_FILE_TEST_EXISTS));
	enregistrer(s);
	c = lire(copie);
	g_assert_cmpstr(c, ==, ancien_fichier);
	g_free(c);
	/* Une seule copie : un second enregistrement ne la réécrit pas */
	g_assert_true(g_file_set_contents(copie, "deja la", -1, NULL));
	enregistrer(s);
	c = lire(copie);
	g_assert_cmpstr(c, ==, "deja la");
	g_free(c);
	gt_config_store_free(s);

	/* Le fichier réécrit n'est plus à migrer */
	s = charger(p);
	g_assert_false(gt_config_store_was_migrated(s));
	g_assert_true(gt_config_store_has_profile(s, "default"));
	c = lire(p);
	g_assert_nonnull(strstr(c, "[profile:default]"));
	g_assert_nonnull(strstr(c, "font=Monospace 12"));
	g_assert_nonnull(strstr(c, "[general]"));
	g_free(c);
	gt_config_store_free(s);
	g_unlink(copie);
	g_free(copie);
	g_free(p);
}

static void
test_migration_style_mixte(void)
{
	gchar *p = ecrire("mixte.rc", "[default]\nport=/dev/ttyUSB1\necho\t= true\nfont=\"Fira Code 10\"\n\n[colors]\nbg=rgb(0,0,0)\n");
	GtConfigStore *s = charger(p);
	GtProfile d = { 0 };

	g_assert_true(gt_config_store_was_migrated(s));
	g_assert_true(gt_config_store_get_profile(s, "default", &d));
	g_assert_cmpstr(d.port, ==, "/dev/ttyUSB1");
	g_assert_true(d.echo);
	g_assert_cmpstr(d.font, ==, "Fira Code 10");
	g_assert_true(g_key_file_has_group(gt_config_store_get_key_file(s), "colors"));
	gt_profile_clear(&d);
	gt_config_store_free(s);
	g_free(p);
}

static void
test_migration_profil_nomme_general(void)
{
	gchar *p = ecrire("general-ancien.rc", "[general]\nport=/dev/ttyUSB2\nspeed=19200\n");
	GtConfigStore *s = charger(p);
	GtProfile g = { 0 };

	g_assert_true(gt_config_store_was_migrated(s));
	g_assert_true(gt_config_store_get_profile(s, "general", &g));
	g_assert_cmpuint(g.baudrate, ==, 19200);
	gt_profile_clear(&g);
	gt_config_store_free(s);
	g_free(p);
}

static void
test_nouveau_format_non_migre(void)
{
	gchar *p = ecrire("nouveau.rc", "[general]\nupdate_url=https://x.invalid/y.git\n\n[profile:default]\nport=/dev/ttyUSB3\n\n[extension]\nfoo=bar\n");
	GtConfigStore *s = charger(p);

	g_assert_false(gt_config_store_was_migrated(s));
	g_assert_true(g_key_file_has_group(gt_config_store_get_key_file(s), "extension"));
	gt_config_store_free(s);
	g_free(p);
}

static void
test_migration_general_et_default(void)
{
	gchar *p = ecrire("general-et-default.rc", "[default]\nport=/dev/ttyUSB0\nupdate_url=https://a.invalid/a.git\nupdate_check_startup=False\n\n[general]\nport=/dev/ttyUSB2\nspeed=19200\n");
	GtConfigStore *s = charger(p);
	GtProfile def = { 0 }, gen = { 0 };
	gchar *v;

	g_assert_true(gt_config_store_was_migrated(s));
	g_assert_true(gt_config_store_get_profile(s, "default", &def));
	g_assert_true(gt_config_store_get_profile(s, "general", &gen));
	v = gt_config_store_get_general_string(s, "update_url", NULL);
	g_assert_cmpstr(v, ==, "https://a.invalid/a.git");
	g_free(v);
	g_assert_false(gt_config_store_get_general_boolean(s, "update_check_startup", TRUE));
	g_assert_false(g_key_file_has_key(gt_config_store_get_key_file(s), "profile:default", "update_url", NULL));
	g_assert_false(g_key_file_has_key(gt_config_store_get_key_file(s), "profile:general", "update_url", NULL));
	gt_profile_clear(&def);
	gt_profile_clear(&gen);
	gt_config_store_free(s);
	g_free(p);

	/* Variante : [general] est le seul ancien profil et porte update_url */
	p = ecrire("general-seul.rc", "[general]\nport=/dev/ttyUSB4\nupdate_url=https://b.invalid/b.git\nupdate_check_startup=True\n");
	s = charger(p);
	g_assert_true(gt_config_store_was_migrated(s));
	g_assert_true(gt_config_store_get_profile(s, "general", &gen));
	v = gt_config_store_get_general_string(s, "update_url", NULL);
	g_assert_cmpstr(v, ==, "https://b.invalid/b.git");
	g_free(v);
	g_assert_true(gt_config_store_get_general_boolean(s, "update_check_startup", FALSE));
	gt_profile_clear(&gen);
	gt_config_store_free(s);
	g_free(p);
}

/* Modification sur place (même inode), comme un éditeur qui réécrit le fichier */
static void
ecrire_sur_place(const gchar *p, const gchar *contenu)
{
	FILE *f = fopen(p, "w");

	g_assert_nonnull(f);
	g_assert_cmpint(fputs(contenu, f), >=, 0);
	g_assert_cmpint(fclose(f), ==, 0);
}

static void
relire(GtConfigStore *s)
{
	GError *err = NULL;

	g_assert_true(gt_config_store_reload_if_changed(s, &err));
	g_assert_no_error(err);
}

/* Deux GTKTerm sur le même fichier : chaque écriture relit d'abord, aucune ne perd celle de l'autre (F1) */
static void
test_deux_magasins_meme_fichier(void)
{
	gchar *p = ecrire("deux.rc", "[profile:default]\nport=/dev/ttyS0\n");
	GtConfigStore *a = charger(p);
	GtConfigStore *b = charger(p);
	GtProfile prof;
	GKeyFile *kf;

	gt_profile_init_defaults(&prof);
	gt_profile_set_string(&prof.port, "/dev/ttyACM3");
	gt_config_store_put_profile(b, "atelier", &prof);
	enregistrer(b);

	relire(a);
	g_assert_true(gt_config_store_has_profile(a, "atelier"));
	g_key_file_set_integer(gt_config_store_get_key_file(a), "window", "width", 800);
	enregistrer(a);

	/* Suppression dans B, puis A écrit encore : la suppression tient */
	relire(b);
	g_assert_cmpint(g_key_file_get_integer(gt_config_store_get_key_file(b), "window", "width", NULL), ==, 800);
	g_assert_true(gt_config_store_delete_profile(b, "atelier"));
	enregistrer(b);
	relire(a);
	g_assert_false(gt_config_store_has_profile(a, "atelier"));
	g_key_file_set_integer(gt_config_store_get_key_file(a), "window", "x", 12);
	enregistrer(a);
	gt_config_store_free(a);
	gt_config_store_free(b);

	a = charger(p);
	kf = gt_config_store_get_key_file(a);
	g_assert_true(gt_config_store_has_profile(a, "default"));
	g_assert_false(gt_config_store_has_profile(a, "atelier"));
	g_assert_cmpint(g_key_file_get_integer(kf, "window", "width", NULL), ==, 800);
	g_assert_cmpint(g_key_file_get_integer(kf, "window", "x", NULL), ==, 12);
	gt_config_store_free(a);

	/* Le même scénario de départ, B ajoute « atelier » : A le garde après son propre enregistrement */
	a = charger(p);
	b = charger(p);
	gt_config_store_put_profile(b, "atelier", &prof);
	enregistrer(b);
	relire(a);
	g_key_file_set_integer(gt_config_store_get_key_file(a), "window", "height", 600);
	enregistrer(a);
	gt_config_store_free(a);
	gt_config_store_free(b);
	a = charger(p);
	g_assert_true(gt_config_store_has_profile(a, "atelier"));
	g_assert_cmpint(g_key_file_get_integer(gt_config_store_get_key_file(a), "window", "height", NULL), ==, 600);
	gt_config_store_free(a);

	gt_profile_clear(&prof);
	g_free(p);
}

static void
test_relecture_modification_a_la_main(void)
{
	gchar *p = ecrire("main.rc", "[profile:default]\nport=/dev/ttyS0\n");
	GtConfigStore *s = charger(p);
	GtProfile lu = { 0 };
	gchar *c;

	ecrire_sur_place(p, "[profile:default]\nport=/dev/ttyS0\n\n[profile:main]\nport=/dev/ttyUSB9\nspeed=4800\n");
	relire(s);
	g_assert_true(gt_config_store_get_profile(s, "main", &lu));
	g_assert_cmpstr(lu.port, ==, "/dev/ttyUSB9");
	g_assert_cmpuint(lu.baudrate, ==, 4800);
	gt_config_store_set_general_string(s, "update_url", "https://x.invalid/z.git");
	enregistrer(s);
	c = lire(p);
	g_assert_nonnull(strstr(c, "[profile:main]"));
	g_assert_nonnull(strstr(c, "update_url=https://x.invalid/z.git"));
	g_free(c);
	gt_profile_clear(&lu);
	gt_config_store_free(s);
	g_free(p);
}

static void
test_relecture_fichier_inchange(void)
{
	gchar *p = ecrire("inchange.rc", "[profile:default]\nport=/dev/ttyS0\n");
	GtConfigStore *s = charger(p);
	GtProfile prof;

	gt_profile_init_defaults(&prof);
	/* Fichier inchangé depuis le chargement : le contenu en mémoire non enregistré est conservé */
	gt_config_store_put_profile(s, "memoire", &prof);
	relire(s);
	g_assert_true(gt_config_store_has_profile(s, "memoire"));
	/* L'identité est relevée après chaque enregistrement : son propre enregistrement ne déclenche pas de relecture */
	enregistrer(s);
	gt_config_store_put_profile(s, "apres", &prof);
	relire(s);
	g_assert_true(gt_config_store_has_profile(s, "apres"));
	g_assert_true(gt_config_store_has_profile(s, "memoire"));
	gt_profile_clear(&prof);
	gt_config_store_free(s);
	g_free(p);
}

static void
test_relecture_fichier_supprime(void)
{
	gchar *p = ecrire("supprime.rc", "[profile:default]\nport=/dev/ttyS0\n");
	GtConfigStore *s = charger(p);
	gchar **noms;
	gchar *c;

	g_assert_cmpint(g_unlink(p), ==, 0);
	relire(s);
	noms = gt_config_store_list_profiles(s);
	g_assert_cmpuint(g_strv_length(noms), ==, 0);
	g_strfreev(noms);
	g_assert_false(gt_config_store_is_read_only(s));
	g_key_file_set_integer(gt_config_store_get_key_file(s), "window", "width", 640);
	enregistrer(s);
	c = lire(p);
	g_assert_null(strstr(c, "[profile:default]"));
	g_assert_nonnull(strstr(c, "width=640"));
	g_free(c);
	gt_config_store_free(s);
	g_free(p);
}

static void
test_relecture_devenu_illisible_puis_ancien_format(void)
{
	gchar *p = ecrire("relecture.rc", "[profile:default]\nport=/dev/ttyS0\n");
	gchar *copie = g_strconcat(p, ".avant-migration", NULL);
	static const gchar ancien[] = "[default]\nport\t= /dev/ttyUSB5\nspeed\t= 38400\necho\t= True\n";
	GtConfigStore *s = charger(p);
	GtProfile d = { 0 };
	GError *err = NULL;
	gchar *c;

	/* Devenu illisible : lecture seule, rien ne sera écrit par-dessus */
	ecrire_sur_place(p, "ceci n'est pas un fichier ini du tout\n");
	g_assert_false(gt_config_store_reload_if_changed(s, &err));
	g_assert_nonnull(err);
	g_assert_nonnull(strstr(err->message, p));
	g_clear_error(&err);
	g_assert_true(gt_config_store_is_read_only(s));
	g_assert_false(gt_config_store_has_profile(s, "default"));
	g_assert_false(gt_config_store_save(s, NULL));
	/* Pas de nouvelle relecture (ni de nouvelle erreur) tant que le fichier ne change pas */
	relire(s);
	g_assert_true(gt_config_store_is_read_only(s));

	/* Remplacé par un ancien fichier parsecfg : migré, inscriptible, copie de sauvegarde à la première écriture */
	ecrire_sur_place(p, ancien);
	relire(s);
	g_assert_false(gt_config_store_is_read_only(s));
	g_assert_true(gt_config_store_was_migrated(s));
	g_assert_true(gt_config_store_get_profile(s, "default", &d));
	g_assert_cmpuint(d.baudrate, ==, 38400);
	g_assert_true(d.echo);
	enregistrer(s);
	c = lire(copie);
	g_assert_cmpstr(c, ==, ancien);
	g_free(c);

	/* Relu au nouveau format : plus migré */
	ecrire_sur_place(p, "[profile:default]\nport=/dev/ttyUSB6\n\n[profile:b]\nport=/dev/ttyUSB7\n");
	relire(s);
	g_assert_false(gt_config_store_was_migrated(s));
	g_assert_true(gt_config_store_has_profile(s, "b"));

	gt_profile_clear(&d);
	gt_config_store_free(s);
	g_unlink(copie);
	g_free(copie);
	g_free(p);
}

/* Anciens fichiers écrits avant le correctif M9 : flottants dans la locale de l'utilisateur (« 0,500000 ») (F7) */
static void
test_migration_couleurs_virgule(void)
{
	gchar *p = ecrire("virgule.rc", "[default]\nport\t= /dev/ttyUSB0\nterm_background_blue\t= 0,500000\nterm_foreground_red\t= 0,250000\n");
	GtConfigStore *s = charger(p);
	GtProfile d = { 0 };

	g_assert_true(gt_config_store_was_migrated(s));
	g_assert_true(gt_config_store_get_profile(s, "default", &d));
	g_assert_cmpfloat(d.background.blue, ==, 0.5);
	g_assert_cmpfloat(d.foreground.red, ==, 0.25);
	gt_profile_clear(&d);
	gt_config_store_free(s);
	g_free(p);
}

/* Le message cite le fichier visé lui-même, pas seulement son fichier temporaire « chemin.XXXXXX » */
static gboolean
cite_le_chemin(const gchar *message, const gchar *p)
{
	gsize n = strlen(p);

	for(const gchar *m = strstr(message, p); m != NULL; m = strstr(m + 1, p))
		if(m[n] != '.')
			return TRUE;
	return FALSE;
}

/* Erreurs d'écriture : le chemin du fichier de configuration (ou de la copie de sauvegarde), pas celui du temporaire (F8) */
static void
test_erreur_ecriture_cite_le_chemin(void)
{
	gchar *ro;
	gchar *p, *q, *copie;
	GtConfigStore *s;
	GError *err = NULL;

	if(geteuid() == 0)
	{
		g_test_skip("root ignore les droits du dossier");
		return;
	}
	ro = chemin("lecture-seule");
	g_assert_cmpint(g_mkdir(ro, 0700), ==, 0);
	p = g_build_filename(ro, ".gtktermrc", NULL);
	q = g_build_filename(ro, "ancien.rc", NULL);
	copie = g_strconcat(q, ".avant-migration", NULL);
	g_assert_true(g_file_set_contents(p, "[profile:default]\nport=/dev/ttyS0\n", -1, NULL));
	g_assert_true(g_file_set_contents(q, "[default]\nport\t= /dev/ttyUSB0\n", -1, NULL));
	g_assert_cmpint(g_chmod(ro, 0500), ==, 0);

	s = charger(p);
	gt_config_store_set_general_string(s, "update_url", "https://x.invalid/w.git");
	g_assert_false(gt_config_store_save(s, &err));
	g_assert_nonnull(err);
	g_test_message("%s", err->message);
	g_assert_true(cite_le_chemin(err->message, p));
	g_clear_error(&err);
	gt_config_store_free(s);

	s = charger(q);
	g_assert_true(gt_config_store_was_migrated(s));
	g_assert_false(gt_config_store_save(s, &err));
	g_assert_nonnull(err);
	g_test_message("%s", err->message);
	g_assert_true(cite_le_chemin(err->message, copie));
	g_clear_error(&err);
	gt_config_store_free(s);

	g_chmod(ro, 0700);
	g_unlink(p);
	g_unlink(q);
	g_rmdir(ro);
	g_free(copie);
	g_free(q);
	g_free(p);
	g_free(ro);
}

static void
nettoyer(void)
{
	GDir *d = g_dir_open(dossier, 0, NULL);
	const gchar *nom;

	while(d != NULL && (nom = g_dir_read_name(d)) != NULL)
	{
		gchar *p = chemin(nom);

		g_chmod(p, 0600);
		g_unlink(p);
		g_free(p);
	}
	if(d != NULL)
		g_dir_close(d);
	g_rmdir(dossier);
}

int
main(int argc, char **argv)
{
	gint r;

	g_test_init(&argc, &argv, NULL);
	dossier = g_dir_make_tmp("gtkterm-store-XXXXXX", NULL);
	g_assert_nonnull(dossier);
	g_test_add_func("/config_store/fichier-absent", test_fichier_absent);
	g_test_add_func("/config_store/fichier-vide", test_fichier_vide);
	g_test_add_func("/config_store/illisible-lecture-seule", test_illisible_lecture_seule);
	g_test_add_func("/config_store/aller-retour", test_aller_retour);
	g_test_add_func("/config_store/cles-absentes-defauts", test_cles_absentes_defauts);
	g_test_add_func("/config_store/valeurs-fautives-ignorees", test_valeurs_fautives_ignorees);
	g_test_add_func("/config_store/casse-des-noms", test_casse_des_noms);
	g_test_add_func("/config_store/liste-triee", test_liste_triee);
	g_test_add_func("/config_store/groupes-conserves", test_groupes_conserves);
	g_test_add_func("/config_store/ecriture-partielle", test_ecriture_partielle);
	g_test_add_func("/config_store/ecriture-partielle-profil-absent", test_ecriture_partielle_profil_absent);
	g_test_add_func("/config_store/reglages-globaux", test_reglages_globaux);
	g_test_add_func("/config_store/enregistrement-droits-et-dossier", test_enregistrement_droits_et_dossier);
	g_test_add_func("/config_store/booleens-tolerants", test_booleens_tolerants);
	g_test_add_func("/config_store/migration-ancien-format", test_migration_ancien_format);
	g_test_add_func("/config_store/migration-copie-de-sauvegarde", test_migration_copie_de_sauvegarde);
	g_test_add_func("/config_store/migration-style-mixte", test_migration_style_mixte);
	g_test_add_func("/config_store/migration-profil-nomme-general", test_migration_profil_nomme_general);
	g_test_add_func("/config_store/nouveau-format-non-migre", test_nouveau_format_non_migre);
	g_test_add_func("/config_store/migration-general-et-default", test_migration_general_et_default);
	g_test_add_func("/config_store/migration-couleurs-virgule", test_migration_couleurs_virgule);
	g_test_add_func("/config_store/deux-magasins-meme-fichier", test_deux_magasins_meme_fichier);
	g_test_add_func("/config_store/relecture-modification-a-la-main", test_relecture_modification_a_la_main);
	g_test_add_func("/config_store/relecture-fichier-inchange", test_relecture_fichier_inchange);
	g_test_add_func("/config_store/relecture-fichier-supprime", test_relecture_fichier_supprime);
	g_test_add_func("/config_store/relecture-devenu-illisible-puis-ancien-format", test_relecture_devenu_illisible_puis_ancien_format);
	g_test_add_func("/config_store/erreur-ecriture-cite-le-chemin", test_erreur_ecriture_cite_le_chemin);
	r = g_test_run();
	nettoyer();
	g_free(dossier);
	return r;
}
