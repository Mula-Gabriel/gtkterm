/* Tests de GtSession (src/session.c, sous-projet 6b) : nom court, profil copié et appliqué, indépendance de deux sessions (historique,
 * options, macros), arrêt ordonné, session occupée. Vraie couche application (gtkterm_app), ports pty (openpty) ; HOME et XDG privés. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <string.h>
#include <unistd.h>

#include "session.h"
#include "test_util.h"

/* Itère le contexte par défaut jusqu'à pred(data) ou ms écoulées ; rend pred(data) */
static gboolean
iterate_until(gboolean (*pred)(gpointer), gpointer data, guint ms)
{
	gint64 fin = g_get_monotonic_time() + (gint64)ms * 1000;

	while(!pred(data) && g_get_monotonic_time() < fin)
	{
		if(!g_main_context_iteration(NULL, FALSE))
			g_usleep(1000);
	}
	return pred(data);
}

typedef struct
{
	int maitre;
	int esclave;
} Pty;

static void
pty_ouvrir(Pty *p)
{
	g_assert_cmpint(openpty(&p->maitre, &p->esclave, NULL, NULL, NULL), ==, 0);
	g_assert_cmpint(fcntl(p->maitre, F_SETFL, fcntl(p->maitre, F_GETFL) | O_NONBLOCK), ==, 0);
}

static void
pty_fermer(Pty *p)
{
	close(p->esclave);
	close(p->maitre);
}

/* Octets disponibles sur le maître pendant ms (ce que la session a envoyé) ; à libérer */
static GByteArray *
pty_lire(Pty *p, guint ms)
{
	GByteArray *lu = g_byte_array_new();
	gint64 fin = g_get_monotonic_time() + (gint64)ms * 1000;

	while(g_get_monotonic_time() < fin)
	{
		guint8 tampon[256];
		ssize_t n;

		g_main_context_iteration(NULL, FALSE);
		n = read(p->maitre, tampon, sizeof(tampon));
		if(n > 0)
			g_byte_array_append(lu, tampon, (guint)n);
		else
			g_usleep(2000);
	}
	return lu;
}

/* Profil série sur l'esclave de la pty (copie profonde dans gt_session_new) */
static GtSession *
session_sur_pty(Pty *p)
{
	GtProfile prof;
	GtSession *s;

	gt_profile_init_defaults(&prof);
	gt_profile_set_string(&prof.port, ttyname(p->esclave));
	s = gt_session_new(&prof, NULL);
	gt_profile_clear(&prof);
	return s;
}

static gboolean
liaison_ouverte(gpointer data)
{
	return gt_link_get_state(gt_session_get_link(data)) == GT_LINK_OPEN;
}

static gboolean
historique_non_vide(gpointer data)
{
	return gt_history_size(gt_link_get_history(gt_session_get_link(data))) > 0;
}

static void
compter(GObject *o, GParamSpec *p, gpointer user_data)
{
	(*(guint *)user_data)++;
}

/* ---- Nom court ------------------------------------------------------------ */

static void
test_nom_court(void)
{
	GtProfile p;
	gchar *nom;
	GtSession *s;
	guint notes = 0;

	gt_profile_init_defaults(&p);
	gt_profile_set_string(&p.port, "/dev/pts/7");
	nom = gt_session_short_name_for(&p);
	g_assert_cmpstr(nom, ==, "7");	/* g_path_get_basename */
	g_free(nom);
	gt_profile_set_string(&p.port, "/dev/ttyUSB0");
	nom = gt_session_short_name_for(&p);
	g_assert_cmpstr(nom, ==, "ttyUSB0");
	g_free(nom);
	gt_profile_set_string(&p.port, "");
	nom = gt_session_short_name_for(&p);
	g_assert_cmpstr(nom, ==, "?");
	g_free(nom);
	p.transport = TRANSPORT_TCP_CLIENT;
	gt_profile_set_string(&p.host, "machine");
	gt_profile_set_string(&p.tcp_port, "4000");
	nom = gt_session_short_name_for(&p);
	g_assert_cmpstr(nom, ==, "machine:4000");
	g_free(nom);
	p.transport = TRANSPORT_TCP_SERVER;
	gt_profile_set_string(&p.tcp_port, "2323");
	nom = gt_session_short_name_for(&p);
	g_assert_cmpstr(nom, ==, ":2323");
	g_free(nom);

	/* Changement de port puis application : une seule notification ; rien de changé : aucune */
	p.transport = TRANSPORT_SERIAL;
	gt_profile_set_string(&p.port, "/dev/pts/7");
	s = gt_session_new(&p, NULL);
	g_assert_cmpstr(gt_session_get_short_name(s), ==, "7");
	g_signal_connect(s, "notify::short-name", G_CALLBACK(compter), &notes);
	gt_profile_set_string(&gt_session_get_profile(s)->port, "/dev/pts/9");
	gt_session_apply_profile(s);
	g_assert_cmpuint(notes, ==, 1);
	g_assert_cmpstr(gt_session_get_short_name(s), ==, "9");
	gt_session_apply_profile(s);
	g_assert_cmpuint(notes, ==, 1);
	g_object_unref(s);
	gt_profile_clear(&p);
}

/* ---- Profil ------------------------------------------------------------------ */

static void
test_profil(void)
{
	GtProfile p;
	GtSession *s, *d;
	gboolean b = FALSE;

	gt_profile_init_defaults(&p);
	p.baudrate = 19200;
	p.echo = TRUE;
	p.crlf_auto = TRUE;
	p.timestamp = TRUE;
	p.esc_clear_screen = TRUE;
	p.auto_reconnect = FALSE;
	s = gt_session_new(&p, "atelier");
	p.baudrate = 300;	/* copie profonde : la session ne voit pas ce changement */
	gt_profile_set_string(&p.port, "/dev/autre");
	g_assert_cmpuint(gt_session_get_profile(s)->baudrate, ==, 19200);
	g_assert_cmpstr(gt_session_get_profile(s)->port, ==, GT_PROFILE_DEFAULT_PORT);
	g_assert_cmpstr(gt_session_get_profile_name(s), ==, "atelier");
	gt_session_set_profile_name(s, NULL);
	g_assert_cmpstr(gt_session_get_profile_name(s), ==, "default");
	gt_session_set_profile_name(s, "autre");
	g_assert_cmpstr(gt_session_get_profile_name(s), ==, "autre");

	/* Rien d'appliqué à la création ; apply_profile : options et auto-reconnect */
	g_object_get(gt_session_get_options(s), "echo", &b, NULL);
	g_assert_false(b);
	gt_session_apply_profile(s);
	g_object_get(gt_session_get_options(s), "echo", &b, NULL);
	g_assert_true(b);
	g_object_get(gt_session_get_options(s), "crlf-auto", &b, NULL);
	g_assert_true(b);
	g_object_get(gt_session_get_options(s), "timestamp", &b, NULL);
	g_assert_true(b);
	g_object_get(gt_session_get_options(s), "esc-clear-screen", &b, NULL);
	g_assert_true(b);
	g_object_get(gt_session_get_link(s), "auto-reconnect", &b, NULL);
	g_assert_false(b);
	gt_session_get_profile(s)->auto_reconnect = TRUE;
	gt_session_apply_profile(s);
	g_object_get(gt_session_get_link(s), "auto-reconnect", &b, NULL);
	g_assert_true(b);

	d = gt_session_new(NULL, NULL);
	g_assert_cmpstr(gt_session_get_profile_name(d), ==, "default");
	g_assert_cmpuint(gt_session_get_profile(d)->baudrate, ==, 115200);
	g_assert_true(gt_session_get_profile(d)->lock_port);
	g_assert_nonnull(gt_session_get_macros(d));
	g_assert_nonnull(gt_session_get_script_engine(d));
	g_assert_true(G_IS_ACTION_MAP(gt_session_get_actions(d)));
	g_assert_true(gt_macros_get_owner(gt_session_get_macros(d)) == d);
	g_assert_true(gt_macros_get_link(gt_session_get_macros(d)) == gt_session_get_link(d));
	g_assert_cmpuint(gt_session_get_id(d), >, gt_session_get_id(s));
	g_assert_cmpint(gt_link_get_state(gt_session_get_link(d)), ==, GT_LINK_CLOSED);	/* aucun port ouvert à la création */
	g_object_unref(d);
	g_object_unref(s);
	gt_profile_clear(&p);
}

/* ---- Deux sessions indépendantes ------------------------------------------------ */

static void
test_deux_sessions_independantes(void)
{
	Pty p1, p2;
	GtSession *s1, *s2;
	GBytes *rx;
	GByteArray *lu;
	GtMacroSet *set;
	guint id;
	gboolean echo = TRUE;

	pty_ouvrir(&p1);
	pty_ouvrir(&p2);
	s1 = session_sur_pty(&p1);
	s2 = session_sur_pty(&p2);
	gt_session_open_port(s1);
	gt_session_open_port(s2);
	g_assert_true(iterate_until(liaison_ouverte, s1, 2000));
	g_assert_true(iterate_until(liaison_ouverte, s2, 2000));

	g_assert_cmpint(write(p1.maitre, "A", 1), ==, 1);
	g_assert_cmpint(write(p2.maitre, "B", 1), ==, 1);
	g_assert_true(iterate_until(historique_non_vide, s1, 2000));
	g_assert_true(iterate_until(historique_non_vide, s2, 2000));
	g_byte_array_unref(pty_lire(&p1, 50));	/* laisse arriver un éventuel octet en trop */
	rx = gt_history_copy_rx(gt_link_get_history(gt_session_get_link(s1)));
	g_assert_cmpmem(g_bytes_get_data(rx, NULL), g_bytes_get_size(rx), "A", 1);
	g_bytes_unref(rx);
	rx = gt_history_copy_rx(gt_link_get_history(gt_session_get_link(s2)));
	g_assert_cmpmem(g_bytes_get_data(rx, NULL), g_bytes_get_size(rx), "B", 1);
	g_bytes_unref(rx);

	/* Écho de la session 1 seulement */
	g_object_set(gt_session_get_options(s1), "echo", TRUE, NULL);
	g_object_get(gt_session_get_options(s2), "echo", &echo, NULL);
	g_assert_false(echo);

	/* Macro de la session 1 : sur la pty 1 seulement */
	set = gt_macro_set_new();
	id = gt_macro_set_append(set, "m", NULL, "XYZ", NULL);
	gt_macros_replace_set(gt_session_get_macros(s1), set);
	g_assert_true(gt_macros_send(gt_session_get_macros(s1), id, NULL, -1));
	lu = pty_lire(&p1, 200);
	g_assert_cmpmem(lu->data, lu->len, "XYZ", 3);
	g_byte_array_unref(lu);
	lu = pty_lire(&p2, 100);
	g_assert_cmpuint(lu->len, ==, 0);
	g_byte_array_unref(lu);
	g_assert_false(gt_macros_send(gt_session_get_macros(s2), id, NULL, -1));	/* jeu vide de la session 2 */

	g_object_unref(s1);
	g_object_unref(s2);
	pty_fermer(&p1);
	pty_fermer(&p2);
}

/* ---- Arrêt ---------------------------------------------------------------------- */

static void
on_closed(GtSession *s, gpointer user_data)
{
	(*(guint *)user_data)++;
}

static void
test_arret(void)
{
	Pty p;
	GtSession *s;
	GSimpleAction *a = g_simple_action_new("essai", NULL);
	guint fermetures = 0;

	pty_ouvrir(&p);
	s = session_sur_pty(&p);
	g_action_map_add_action(G_ACTION_MAP(gt_session_get_actions(s)), G_ACTION(a));
	g_signal_connect(s, "closed", G_CALLBACK(on_closed), &fermetures);
	gt_session_open_port(s);
	g_assert_true(iterate_until(liaison_ouverte, s, 2000));
	g_assert_false(gt_session_is_shut_down(s));
	g_assert_true(g_action_group_get_action_enabled(gt_session_get_actions(s), "essai"));

	gt_session_shutdown(s);
	g_assert_cmpuint(fermetures, ==, 1);
	g_assert_true(gt_session_is_shut_down(s));
	g_assert_cmpint(gt_link_get_state(gt_session_get_link(s)), ==, GT_LINK_CLOSED);
	g_assert_false(g_action_group_get_action_enabled(gt_session_get_actions(s), "essai"));
	gt_session_shutdown(s);
	g_assert_cmpuint(fermetures, ==, 1);
	gt_session_open_port(s);	/* inerte */
	g_assert_cmpint(gt_link_get_state(gt_session_get_link(s)), ==, GT_LINK_CLOSED);
	g_assert_false(gt_session_is_busy(s));
	g_object_unref(s);	/* dispose : pas de second « closed » */
	g_assert_cmpuint(fermetures, ==, 1);
	g_object_unref(a);
	pty_fermer(&p);
}

/* ---- Actions de session (tâche 5, plan décision 4) ---------------------------------- */

static const gchar *const actions_de_session[] = {
	"local-echo", "crlf-auto", "timestamp", "esc-clear-screen", "hex-index", "view-mode", "hex-bytes", "auto-reconnect",
	"clear-screen", "open-port", "close-port", "send-break", "toggle-dtr", "toggle-rts", NULL
};

static gboolean
echo_de(GtSession *s)
{
	gboolean echo = FALSE;

	g_object_get(gt_session_get_options(s), "echo", &echo, NULL);
	return echo;
}

static gboolean
liaison_fermee(gpointer data)
{
	return gt_link_get_state(gt_session_get_link(data)) == GT_LINK_CLOSED;
}

static void
test_actions(void)
{
	Pty p;
	GtSession *s, *autre;
	GActionGroup *g;
	gchar **noms;
	gboolean reconnexion = TRUE;

	pty_ouvrir(&p);
	s = session_sur_pty(&p);
	autre = gt_session_new(NULL, NULL);
	g = gt_session_get_actions(s);

	/* Exactement les actions de la décision 4 */
	noms = g_action_group_list_actions(g);
	g_assert_cmpuint(g_strv_length(noms), ==, g_strv_length((gchar **)actions_de_session));
	for(const gchar *const *n = actions_de_session; *n != NULL; n++)
		g_assert_true(g_strv_contains((const gchar *const *)noms, *n));
	g_strfreev(noms);

	/* Types conservés depuis 6a */
	g_assert_true(g_variant_type_equal(g_action_group_get_action_state_type(g, "local-echo"), G_VARIANT_TYPE_BOOLEAN));
	g_assert_true(g_variant_type_equal(g_action_group_get_action_parameter_type(g, "view-mode"), G_VARIANT_TYPE_STRING));
	g_assert_true(g_variant_type_equal(g_action_group_get_action_state_type(g, "hex-bytes"), G_VARIANT_TYPE_UINT32));
	g_assert_null(g_action_group_get_action_state_type(g, "send-break"));

	/* Signaux de contrôle : seulement sur une liaison série ouverte */
	g_assert_false(g_action_group_get_action_enabled(g, "send-break"));
	g_assert_false(g_action_group_get_action_enabled(g, "toggle-dtr"));
	g_assert_false(g_action_group_get_action_enabled(g, "toggle-rts"));
	g_action_group_activate_action(g, "open-port", NULL);
	g_assert_true(iterate_until(liaison_ouverte, s, 2000));
	g_assert_true(g_action_group_get_action_enabled(g, "send-break"));
	g_assert_true(g_action_group_get_action_enabled(g, "toggle-rts"));
	g_assert_false(g_action_group_get_action_enabled(gt_session_get_actions(autre), "send-break"));	/* l'autre session reste fermée */
	g_action_group_activate_action(g, "close-port", NULL);
	g_assert_true(iterate_until(liaison_fermee, s, 2000));
	g_assert_false(g_action_group_get_action_enabled(g, "send-break"));

	/* Actions à état sur les options et la liaison de cette session seulement */
	g_action_group_change_action_state(g, "local-echo", g_variant_new_boolean(TRUE));
	g_assert_true(echo_de(s));
	g_assert_false(echo_de(autre));
	g_action_group_change_action_state(gt_session_get_actions(autre), "auto-reconnect", g_variant_new_boolean(TRUE));
	g_object_get(gt_session_get_link(s), "auto-reconnect", &reconnexion, NULL);
	g_assert_false(reconnexion);
	g_object_get(gt_session_get_link(autre), "auto-reconnect", &reconnexion, NULL);
	g_assert_true(reconnexion);

	/* Arrêt : actions simples désactivées, GPropertyAction (qui garderaient leur objet actif) retirées */
	gt_session_shutdown(s);
	g_assert_false(g_action_group_has_action(g, "local-echo"));
	g_assert_false(g_action_group_has_action(g, "view-mode"));
	g_assert_false(g_action_group_has_action(g, "auto-reconnect"));
	g_assert_true(g_action_group_has_action(g, "open-port"));
	g_assert_false(g_action_group_get_action_enabled(g, "open-port"));
	g_assert_false(g_action_group_get_action_enabled(g, "clear-screen"));
	g_assert_true(g_action_group_has_action(gt_session_get_actions(autre), "local-echo"));
	g_object_unref(autre);
	g_object_unref(s);
	pty_fermer(&p);
}

/* ---- Occupée ---------------------------------------------------------------------- */

static void
test_occupee(void)
{
	GtSession *s = gt_session_new(NULL, NULL);
	gint64 debut;

	g_assert_false(gt_session_is_busy(s));
	gt_session_set_file_sending(s, TRUE);
	g_assert_true(gt_session_is_busy(s));
	gt_session_set_file_sending(s, FALSE);
	g_assert_false(gt_session_is_busy(s));
	gt_script_engine_run(gt_session_get_script_engine(s), "while true do gtkterm.sleep(10) end");
	g_assert_true(gt_session_is_busy(s));
	debut = g_get_monotonic_time();
	gt_session_shutdown(s);
	g_assert_cmpint(g_get_monotonic_time() - debut, <, 3 * G_USEC_PER_SEC);
	g_assert_false(gt_script_engine_is_running(gt_session_get_script_engine(s)));
	g_assert_false(gt_session_is_busy(s));
	g_object_unref(s);
}

/* ---- Activité du port ----------------------------------------------------------- */

static const gchar *
activite_nick(GtSession *s)
{
	return gt_port_activity_state_nick(gt_port_activity_get_state(gt_session_get_activity(s)));
}

static gboolean
activite_est(GtSession *s, const gchar *nick)
{
	return g_strcmp0(activite_nick(s), nick) == 0;
}

static gboolean
activite_idle(gpointer data)
{
	return activite_est(data, "idle");
}

static gboolean
activite_dead(gpointer data)
{
	return activite_est(data, "dead");
}

static gboolean
activite_recue(gpointer data)
{
	return activite_est(data, "receiving") || activite_est(data, "active");
}

static void
test_activite(void)
{
	Pty p;
	GtSession *s;

	pty_ouvrir(&p);
	s = session_sur_pty(&p);
	gt_session_get_profile(s)->idle_timeout_s = 1;
	gt_session_get_profile(s)->no_data_timeout_s = 2;
	g_assert_cmpstr(activite_nick(s), ==, "closed");
	gt_session_open_port(s);
	g_assert_true(iterate_until(liaison_ouverte, s, 5000));
	g_assert_cmpstr(activite_nick(s), ==, "waiting");

	g_assert_cmpint(write(p.maitre, "x", 1), ==, 1);
	g_assert_true(iterate_until(activite_recue, s, 5000));
	g_assert_true(iterate_until(activite_idle, s, 10000));

	/* Réouverture d'un port déjà ouvert : repart de zéro, puis muet jusqu'au délai sans données */
	gt_session_open_port(s);
	g_assert_cmpstr(activite_nick(s), ==, "waiting");
	g_assert_true(iterate_until(activite_dead, s, 12000));

	gt_session_close_port(s);
	g_assert_cmpstr(activite_nick(s), ==, "closed");
	g_object_unref(s);
	pty_fermer(&p);
}

static gboolean
une_seconde_ecoulee(gpointer data)
{
	return g_get_monotonic_time() >= *(gint64 *)data;
}

static void
test_activite_delais_profil(void)
{
	Pty p;
	GtSession *s;
	gint64 fin;

	pty_ouvrir(&p);
	s = session_sur_pty(&p);
	gt_session_get_profile(s)->idle_timeout_s = 1;
	gt_session_get_profile(s)->no_data_timeout_s = 1;
	gt_session_apply_profile(s);
	gt_session_get_profile(s)->idle_timeout_s = 0;
	gt_session_get_profile(s)->no_data_timeout_s = 0;
	gt_session_apply_profile(s);	/* les nouveaux délais remplacent les anciens */
	gt_session_open_port(s);
	g_assert_true(iterate_until(liaison_ouverte, s, 5000));
	fin = g_get_monotonic_time() + 1500000;
	iterate_until(une_seconde_ecoulee, &fin, 3000);
	g_assert_cmpstr(activite_nick(s), ==, "waiting");	/* désactivés : jamais dead */
	g_object_unref(s);
	pty_fermer(&p);
}

/* ---- Projet (6c) : in-project, script-text, signal edited ---- */

static void
compter_signal(gpointer data)
{
	(*(gint *)data)++;
}

static void
compter_notify(GObject *o, GParamSpec *pspec, gpointer data)
{
	(*(gint *)data)++;
}

static void
test_in_project(void)
{
	GtSession *s = gt_session_new(NULL, NULL);
	gint notifies = 0, edites = 0;

	g_assert_false(gt_session_get_in_project(s));
	g_assert_false(gt_macros_is_embedded(gt_session_get_macros(s)));
	g_signal_connect(s, "notify::in-project", G_CALLBACK(compter_notify), &notifies);
	g_signal_connect_swapped(s, "edited", G_CALLBACK(compter_signal), &edites);
	gt_session_set_in_project(s, TRUE);
	g_assert_true(gt_session_get_in_project(s));
	g_assert_true(gt_macros_is_embedded(gt_session_get_macros(s)));
	g_assert_cmpint(notifies, ==, 1);
	g_assert_cmpint(edites, ==, 1);
	gt_session_set_in_project(s, TRUE);	/* inchangé : rien */
	g_assert_cmpint(notifies, ==, 1);
	g_assert_cmpint(edites, ==, 1);
	g_object_unref(s);
}

static void
test_script_text(void)
{
	GtSession *s = gt_session_new(NULL, NULL);
	gint notifies = 0, edites = 0;

	g_assert_cmpstr(gt_session_get_script_text(s), ==, "");
	g_signal_connect(s, "notify::script-text", G_CALLBACK(compter_notify), &notifies);
	g_signal_connect_swapped(s, "edited", G_CALLBACK(compter_signal), &edites);
	gt_session_set_script_text(s, "");
	gt_session_set_script_text(s, NULL);
	g_assert_cmpint(notifies, ==, 0);
	g_assert_cmpint(edites, ==, 0);
	gt_session_set_script_text(s, "print('a')");
	g_assert_cmpstr(gt_session_get_script_text(s), ==, "print('a')");
	g_assert_cmpint(notifies, ==, 1);
	g_assert_cmpint(edites, ==, 1);
	gt_session_set_script_text(s, "print('a')");
	g_assert_cmpint(notifies, ==, 1);
	g_assert_cmpint(edites, ==, 1);
	gt_session_set_script_text(s, NULL);
	g_assert_cmpstr(gt_session_get_script_text(s), ==, "");
	g_assert_cmpint(edites, ==, 2);
	g_object_unref(s);
}

static void
test_edited(void)
{
	GtSession *s = gt_session_new(NULL, NULL);
	gint n = 0;

	g_signal_connect_swapped(s, "edited", G_CALLBACK(compter_signal), &n);
	g_object_set(gt_session_get_options(s), "echo", TRUE, NULL);
	g_assert_cmpint(n, ==, 1);
	g_object_set(gt_session_get_link(s), "auto-reconnect", TRUE, NULL);
	g_assert_cmpint(n, ==, 2);
	/* Profil identique à l'état courant (echo et auto-reconnect déjà réglés ci-dessus) : ni « changed » des options ni notify de la liaison,
	 * le seul « edited » est celui qui termine gt_session_apply_profile */
	gt_session_get_profile(s)->echo = TRUE;
	gt_session_get_profile(s)->auto_reconnect = TRUE;
	gt_session_apply_profile(s);
	g_assert_cmpint(n, ==, 3);
	gt_macros_changed(gt_session_get_macros(s));
	g_assert_cmpint(n, ==, 4);
	gt_session_edited(s);
	g_assert_cmpint(n, ==, 5);
	gt_session_shutdown(s);
	gt_session_edited(s);
	g_object_set(gt_session_get_options(s), "echo", FALSE, NULL);
	gt_macros_changed(gt_session_get_macros(s));
	g_assert_cmpint(n, ==, 5);
	g_object_unref(s);
}

int
main(int argc, char *argv[])
{
	gchar *racine = test_private_dirs();	/* HOME et XDG privés : jamais le vrai ~/.config */
	gint ret;

	g_test_init(&argc, &argv, NULL);
	script_engine_setup();
	g_test_add_func("/session/nom-court", test_nom_court);
	g_test_add_func("/session/profil", test_profil);
	g_test_add_func("/session/deux-sessions-independantes", test_deux_sessions_independantes);
	g_test_add_func("/session/arret", test_arret);
	g_test_add_func("/session/occupee", test_occupee);
	g_test_add_func("/session/actions", test_actions);
	g_test_add_func("/session/activite", test_activite);
	g_test_add_func("/session/activite-delais-profil", test_activite_delais_profil);
	g_test_add_func("/session/in-project", test_in_project);
	g_test_add_func("/session/script-text", test_script_text);
	g_test_add_func("/session/edited", test_edited);
	ret = g_test_run();
	test_rmdir_recursive(racine);
	g_free(racine);
	return ret;
}
