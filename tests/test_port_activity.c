/* Tests de port_activity.c (cœur) : état d'activité d'un port, « non lu », échéances. Horloge manuelle (use-timer faux) sauf /source-reelle. */
#include <glib-object.h>

#include "port_activity.h"

#define S (G_GINT64_CONSTANT(1000000))
#define MS (G_GINT64_CONSTANT(1000))

typedef struct
{
	guint notify_state;
	guint notify_unread;
} Compteurs;

static void
sur_notify(GObject *obj, GParamSpec *pspec, gpointer data)
{
	Compteurs *c = data;

	if(g_strcmp0(pspec->name, "state") == 0)
		c->notify_state++;
	else if(g_strcmp0(pspec->name, "unread") == 0)
		c->notify_unread++;
}

static GtPortActivity *
manuel(Compteurs *c)
{
	GtPortActivity *a = g_object_new(GT_TYPE_PORT_ACTIVITY, "use-timer", FALSE, NULL);

	if(c != NULL)
	{
		*c = (Compteurs){ 0 };
		g_signal_connect(a, "notify", G_CALLBACK(sur_notify), c);
	}
	return a;
}

#define ETAT(a, attendu) g_assert_cmpstr(gt_port_activity_state_nick(gt_port_activity_get_state(a)), ==, (attendu))

static void
test_ferme_par_defaut(void)
{
	GtPortActivity *a = manuel(NULL);

	ETAT(a, "closed");
	g_assert_false(gt_port_activity_get_unread(a));
	g_object_unref(a);
}

static void
test_ouvert_reception_repos(void)
{
	Compteurs c;
	GtPortActivity *a = manuel(&c);

	gt_port_activity_set_timeouts(a, 10, 60, 0);
	gt_port_activity_set_link_state(a, GT_PORT_LINK_OPEN, 0);
	ETAT(a, "waiting");
	gt_port_activity_received(a, 1 * S);
	ETAT(a, "receiving");
	gt_port_activity_tick(a, 1 * S + 299 * MS);
	ETAT(a, "receiving");
	gt_port_activity_tick(a, 1 * S + 300 * MS);
	ETAT(a, "active");
	gt_port_activity_tick(a, 11 * S - 1);
	ETAT(a, "active");
	gt_port_activity_tick(a, 11 * S);
	ETAT(a, "idle");
	gt_port_activity_tick(a, 1000 * S);
	ETAT(a, "idle");	/* déjà reçu : jamais dead */
	gt_port_activity_received(a, 1001 * S);
	ETAT(a, "receiving");
	g_object_unref(a);
}

static void
test_flux_continu_sans_notification(void)
{
	Compteurs c;
	GtPortActivity *a = manuel(&c);

	gt_port_activity_set_link_state(a, GT_PORT_LINK_OPEN, 0);
	gt_port_activity_received(a, 0);
	c.notify_state = 0;
	for(gint i = 1; i < 1000; i++)
		gt_port_activity_received(a, i * 10 * MS);
	ETAT(a, "receiving");
	g_assert_cmpuint(c.notify_state, ==, 0);
	g_object_unref(a);
}

static void
test_muet_devient_mort(void)
{
	GtPortActivity *a = manuel(NULL);

	gt_port_activity_set_timeouts(a, 10, 60, 0);
	gt_port_activity_set_link_state(a, GT_PORT_LINK_OPEN, 0);
	gt_port_activity_tick(a, 60 * S - 1);
	ETAT(a, "waiting");
	gt_port_activity_tick(a, 60 * S);
	ETAT(a, "dead");
	gt_port_activity_received(a, 61 * S);
	ETAT(a, "receiving");
	g_object_unref(a);
}

static void
test_reconnexion_repart_de_zero(void)
{
	GtPortActivity *a = manuel(NULL);

	gt_port_activity_set_timeouts(a, 10, 60, 0);
	gt_port_activity_set_link_state(a, GT_PORT_LINK_OPEN, 0);
	gt_port_activity_received(a, 1 * S);
	gt_port_activity_set_link_state(a, GT_PORT_LINK_LOST, 2 * S);
	gt_port_activity_set_link_state(a, GT_PORT_LINK_OPEN, 3 * S);
	ETAT(a, "waiting");
	gt_port_activity_tick(a, 63 * S);
	ETAT(a, "dead");
	g_object_unref(a);
}

static void
test_reset_port_ouvert(void)
{
	GtPortActivity *a = manuel(NULL);

	gt_port_activity_set_timeouts(a, 10, 60, 0);
	gt_port_activity_set_link_state(a, GT_PORT_LINK_OPEN, 0);
	gt_port_activity_received(a, 1 * S);
	gt_port_activity_reset(a, 20 * S);
	ETAT(a, "waiting");
	gt_port_activity_tick(a, 79 * S);
	ETAT(a, "waiting");
	gt_port_activity_tick(a, 80 * S);
	ETAT(a, "dead");
	g_object_unref(a);
}

static void
test_connexion_jamais_morte(void)
{
	GtPortActivity *a = manuel(NULL);

	gt_port_activity_set_timeouts(a, 10, 60, 0);
	gt_port_activity_set_link_state(a, GT_PORT_LINK_LISTENING, 0);
	ETAT(a, "connecting");
	gt_port_activity_tick(a, 1000 * S);
	ETAT(a, "connecting");
	gt_port_activity_set_link_state(a, GT_PORT_LINK_CONNECTING, 1000 * S);
	gt_port_activity_tick(a, 2000 * S);
	ETAT(a, "connecting");
	g_object_unref(a);
}

static void
test_perte_puis_reconnexion(void)
{
	GtPortActivity *a = manuel(NULL);

	gt_port_activity_set_timeouts(a, 10, 60, 0);
	gt_port_activity_set_link_state(a, GT_PORT_LINK_OPEN, 0);
	gt_port_activity_set_link_state(a, GT_PORT_LINK_LOST, 5 * S);
	ETAT(a, "lost");
	gt_port_activity_set_link_state(a, GT_PORT_LINK_RECONNECTING, 5 * S);
	ETAT(a, "reconnecting");
	gt_port_activity_tick(a, 65 * S - 1);
	ETAT(a, "reconnecting");
	gt_port_activity_tick(a, 65 * S);
	ETAT(a, "dead");
	g_object_unref(a);
}

static void
test_echec_ouverture_reconnexion(void)
{
	GtPortActivity *a = manuel(NULL);

	gt_port_activity_set_timeouts(a, 10, 60, 0);
	gt_port_activity_set_link_state(a, GT_PORT_LINK_RECONNECTING, 0);	/* CLOSED → RECONNECTING sans LOST */
	ETAT(a, "reconnecting");
	gt_port_activity_tick(a, 60 * S);
	ETAT(a, "dead");
	g_object_unref(a);
}

static void
test_bascule_auto_reconnect(void)
{
	GtPortActivity *a = manuel(NULL);

	gt_port_activity_set_timeouts(a, 10, 60, 0);
	gt_port_activity_set_link_state(a, GT_PORT_LINK_OPEN, 0);
	gt_port_activity_set_link_state(a, GT_PORT_LINK_RECONNECTING, 10 * S);
	gt_port_activity_set_link_state(a, GT_PORT_LINK_LOST, 30 * S);	/* auto-reconnect désactivé */
	gt_port_activity_set_link_state(a, GT_PORT_LINK_RECONNECTING, 50 * S);	/* réactivé : pas remis à zéro */
	gt_port_activity_tick(a, 70 * S);
	ETAT(a, "dead");
	g_object_unref(a);
}

static void
test_perdu_sans_reconnexion(void)
{
	GtPortActivity *a = manuel(NULL);

	gt_port_activity_set_timeouts(a, 10, 60, 0);
	gt_port_activity_set_link_state(a, GT_PORT_LINK_OPEN, 0);
	gt_port_activity_set_link_state(a, GT_PORT_LINK_LOST, 0);
	gt_port_activity_tick(a, 59 * S);
	ETAT(a, "lost");
	gt_port_activity_tick(a, 60 * S);
	ETAT(a, "dead");
	gt_port_activity_set_link_state(a, GT_PORT_LINK_CLOSED, 61 * S);
	ETAT(a, "closed");
	g_object_unref(a);
}

static void
test_delais_nuls(void)
{
	GtPortActivity *a = manuel(NULL);

	gt_port_activity_set_timeouts(a, 0, 0, 0);
	gt_port_activity_set_link_state(a, GT_PORT_LINK_OPEN, 0);
	gt_port_activity_tick(a, 10000 * S);
	ETAT(a, "waiting");
	gt_port_activity_received(a, 10000 * S);
	gt_port_activity_tick(a, 20000 * S);
	ETAT(a, "active");
	g_object_unref(a);
}

static void
test_delais_changes_en_cours(void)
{
	GtPortActivity *a = manuel(NULL);

	gt_port_activity_set_timeouts(a, 10, 60, 0);
	gt_port_activity_set_link_state(a, GT_PORT_LINK_OPEN, 0);
	gt_port_activity_received(a, 0);
	gt_port_activity_tick(a, 5 * S);
	ETAT(a, "active");
	gt_port_activity_set_timeouts(a, 2, 60, 5 * S);
	ETAT(a, "idle");
	gt_port_activity_set_timeouts(a, 99999, 99999, 5 * S);	/* bornés à 3600 */
	ETAT(a, "active");
	gt_port_activity_tick(a, 3600 * S);
	ETAT(a, "idle");
	g_object_unref(a);
}

static void
test_reception_hors_ouvert_ignoree(void)
{
	Compteurs c;
	GtPortActivity *a = manuel(&c);

	gt_port_activity_set_viewed(a, FALSE);
	gt_port_activity_received(a, 0);
	ETAT(a, "closed");
	g_assert_false(gt_port_activity_get_unread(a));
	g_assert_cmpuint(c.notify_state + c.notify_unread, ==, 0);
	g_object_unref(a);
}

static void
test_non_lu(void)
{
	Compteurs c;
	GtPortActivity *a = manuel(&c);

	gt_port_activity_set_link_state(a, GT_PORT_LINK_OPEN, 0);
	gt_port_activity_received(a, 1 * S);
	g_assert_false(gt_port_activity_get_unread(a));	/* vu par défaut */
	gt_port_activity_set_viewed(a, FALSE);
	g_assert_false(gt_port_activity_get_unread(a));
	gt_port_activity_received(a, 2 * S);
	gt_port_activity_received(a, 3 * S);
	g_assert_true(gt_port_activity_get_unread(a));
	g_assert_cmpuint(c.notify_unread, ==, 1);
	gt_port_activity_set_viewed(a, TRUE);
	g_assert_false(gt_port_activity_get_unread(a));
	g_assert_cmpuint(c.notify_unread, ==, 2);
	g_object_unref(a);
}

static gboolean
sur_garde(gpointer data)
{
	*(gboolean *)data = TRUE;
	return G_SOURCE_REMOVE;
}

/* Itère le contexte par défaut jusqu'à l'état attendu ou ms écoulées (source de garde : jamais bloqué si l'activité n'arme rien) */
static gboolean
attendre_etat(GtPortActivity *a, GtPortActivityState attendu, guint ms)
{
	gboolean fini = FALSE;
	guint garde = g_timeout_add(ms, sur_garde, &fini);

	while(gt_port_activity_get_state(a) != attendu && !fini)
		g_main_context_iteration(NULL, TRUE);
	if(!fini)
		g_source_remove(garde);
	return gt_port_activity_get_state(a) == attendu;
}

static void
test_source_reelle(void)
{
	GtPortActivity *a = gt_port_activity_new();	/* use-timer vrai */
	gboolean fini = FALSE;

	gt_port_activity_set_timeouts(a, 1, 60, g_get_monotonic_time());
	gt_port_activity_set_link_state(a, GT_PORT_LINK_OPEN, g_get_monotonic_time());
	gt_port_activity_received(a, g_get_monotonic_time());
	ETAT(a, "receiving");
	g_assert_true(attendre_etat(a, GT_PORT_ACTIVITY_ACTIVE, 2000));
	g_assert_true(attendre_etat(a, GT_PORT_ACTIVITY_IDLE, 3000));	/* source réarmée sur l'échéance suivante */
	g_object_unref(a);	/* source retirée : aucun rappel après libération (ASan) */
	g_timeout_add(400, sur_garde, &fini);
	while(!fini)
		g_main_context_iteration(NULL, TRUE);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/port-activity/ferme-par-defaut", test_ferme_par_defaut);
	g_test_add_func("/port-activity/ouvert-reception-repos", test_ouvert_reception_repos);
	g_test_add_func("/port-activity/flux-continu-sans-notification", test_flux_continu_sans_notification);
	g_test_add_func("/port-activity/muet-devient-mort", test_muet_devient_mort);
	g_test_add_func("/port-activity/reconnexion-repart-de-zero", test_reconnexion_repart_de_zero);
	g_test_add_func("/port-activity/reset-port-ouvert", test_reset_port_ouvert);
	g_test_add_func("/port-activity/connexion-jamais-morte", test_connexion_jamais_morte);
	g_test_add_func("/port-activity/perte-puis-reconnexion", test_perte_puis_reconnexion);
	g_test_add_func("/port-activity/echec-ouverture-reconnexion", test_echec_ouverture_reconnexion);
	g_test_add_func("/port-activity/bascule-auto-reconnect", test_bascule_auto_reconnect);
	g_test_add_func("/port-activity/perdu-sans-reconnexion", test_perdu_sans_reconnexion);
	g_test_add_func("/port-activity/delais-nuls", test_delais_nuls);
	g_test_add_func("/port-activity/delais-changes-en-cours", test_delais_changes_en_cours);
	g_test_add_func("/port-activity/reception-hors-ouvert-ignoree", test_reception_hors_ouvert_ignoree);
	g_test_add_func("/port-activity/non-lu", test_non_lu);
	g_test_add_func("/port-activity/source-reelle", test_source_reelle);
	return g_test_run();
}
