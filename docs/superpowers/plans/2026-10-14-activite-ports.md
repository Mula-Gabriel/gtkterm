# Indicateurs d'activité des ports — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** un indicateur d'activité (pastille, spinner, Zzz, tête de mort, gras « non lu ») dans chaque onglet, et dans la barre d'état avec un seul onglet.

**Architecture:** un modèle sans GTK `GtPortActivity` (cœur) calcule l'état à partir d'événements datés ; chaque `GtSession` le branche sur sa liaison ;
un widget `activity_icon.c` (UI) l'affiche, animé par le code (pas de CSS animé : le bureau de l'utilisateur réduit les animations).

**Tech Stack:** C, GLib/GObject, GTK 4.14 (plafond imposé par le compilateur dans `src/ui/`), Meson, GTest, suite GUI Python sous broadway.

Spec : `docs/superpowers/specs/2026-10-14-activite-ports-design.md` (à lire en entier avant toute tâche). Règles : `CLAUDE.md` (couches, style :
tabulations, accolades Allman, `if(`, commentaires et commits en français, chaînes visibles en anglais via `_()` + traduction `po/fr.po`).
Branche : `feature/activite-ports`. Jamais de fenêtre sur l'écran réel, jamais `~/.config/.gtktermrc`, jamais de vrai port série.

Commandes :
- construire : `ninja -C build`
- tests : `meson test -C build` (unitaires) ; `meson test -C build --suite gui` (GUI, une seule suite GUI à la fois) ;
  un test : `meson test -C build test_port_activity -v`
- ASan : `meson setup build-asan -Db_sanitize=address,undefined` (une fois, si absent) puis `meson test -C build-asan`

---

## Fichiers

| Fichier | Rôle |
|---|---|
| `src/port_activity.c/.h` (créés) | `GtPortActivity` : état, « non lu », échéances |
| `tests/test_port_activity.c` (créé) | tests unitaires du modèle |
| `src/profile.c/.h`, `src/config_store.c` | champs `idle_timeout_s`, `no_data_timeout_s`, clés `activity_*` |
| `src/port_form.c/.h` | champs du formulaire Port |
| `src/session.c/.h` | une activité par session, branchée sur la liaison |
| `src/ui/activity_icon.c/.h` (créés) | widget d'emplacement 18 × 16 |
| `src/ui/icons/scalable/actions/skull-symbolic.svg` (créé) | tête de mort |
| `src/ui/gtkterm.gresource.xml`, `src/ui/meson.build` | ressource, module |
| `src/ui/tab_label.c`, `src/ui/status_bar.c/.h`, `src/ui/session_view.c`, `src/ui/app.c`, `src/ui/port_dialog.c`, `src/ui/test_hooks.c` | intégration |
| `tests/gui/test_activite_ports.py` (créé), `tests/gui/meson.build` | scénario GUI |
| `po/POTFILES.in`, `po/fr.po`, `README.md`, `CLAUDE.md` | doc, traductions |

---

### Task 1 : modèle `GtPortActivity` (cœur)

**Files:**
- Create: `src/port_activity.h`, `src/port_activity.c`, `tests/test_port_activity.c`
- Modify: `src/meson.build` (`core_sources`), `tests/meson.build` (`unit_tests`)

- [ ] **Step 1 : en-tête**

`src/port_activity.h` :

```c
/* Activité d'un port (indicateurs d'onglet et de barre d'état) : état calculé à partir d'événements datés (heure monotone, µs). Cœur, sans GTK
 * ni liaison ; fil principal seulement. Voir docs/superpowers/specs/2026-10-14-activite-ports-design.md §1. */
#ifndef PORT_ACTIVITY_H_
#define PORT_ACTIVITY_H_

#include <glib-object.h>

G_BEGIN_DECLS

#define GT_PORT_ACTIVITY_RX_PULSE_US 300000	/* durée de l'état « réception » après le dernier octet */
#define GT_PORT_ACTIVITY_TIMEOUT_MAX 3600	/* s : délais de repos et sans données */

typedef enum
{
	GT_PORT_ACTIVITY_CLOSED,
	GT_PORT_ACTIVITY_CONNECTING,	/* client TCP en connexion, ou serveur TCP sans client */
	GT_PORT_ACTIVITY_WAITING,	/* ouvert, rien reçu encore */
	GT_PORT_ACTIVITY_ACTIVE,	/* ouvert, a reçu il y a moins que le délai de repos */
	GT_PORT_ACTIVITY_RECEIVING,	/* dernier octet il y a moins de 300 ms */
	GT_PORT_ACTIVITY_IDLE,	/* a reçu, puis plus rien depuis le délai de repos */
	GT_PORT_ACTIVITY_LOST,
	GT_PORT_ACTIVITY_RECONNECTING,
	GT_PORT_ACTIVITY_DEAD	/* ouvert sans un octet, ou perdu, depuis le délai sans données */
} GtPortActivityState;

/* État de la liaison vu par le modèle (converti depuis GtLinkState par la session : le cœur n'inclut pas link.h) */
typedef enum
{
	GT_PORT_LINK_CLOSED,
	GT_PORT_LINK_CONNECTING,
	GT_PORT_LINK_OPEN,
	GT_PORT_LINK_LISTENING,
	GT_PORT_LINK_LOST,
	GT_PORT_LINK_RECONNECTING
} GtPortActivityLink;

GType gt_port_activity_state_get_type(void);
#define GT_TYPE_PORT_ACTIVITY_STATE (gt_port_activity_state_get_type())

#define GT_TYPE_PORT_ACTIVITY (gt_port_activity_get_type())
G_DECLARE_FINAL_TYPE(GtPortActivity, gt_port_activity, GT, PORT_ACTIVITY, GObject)

/* Propriétés : « state » (GtPortActivityState, nicks closed, connecting, waiting, active, receiving, idle, lost, reconnecting, dead), « unread »
 * (booléen), notifiées seulement sur changement ; « use-timer » (construction, vrai par défaut : échéances par une source GLib ; faux : les tests
 * appellent tick). Délais par défaut 10 s et 60 s ; « vu » vrai par défaut. */
GtPortActivity     *gt_port_activity_new(void);
void                gt_port_activity_set_link_state(GtPortActivity *a, GtPortActivityLink st, gint64 now_us);
void                gt_port_activity_received(GtPortActivity *a, gint64 now_us);	/* ignoré hors GT_PORT_LINK_OPEN */
void                gt_port_activity_reset(GtPortActivity *a, gint64 now_us);	/* réouverture d'un port déjà ouvert : repart de zéro */
/* 0 désactive l'état (idle ou dead) ; valeurs bornées à GT_PORT_ACTIVITY_TIMEOUT_MAX ; état réévalué à now_us */
void                gt_port_activity_set_timeouts(GtPortActivity *a, guint idle_s, guint no_data_s, gint64 now_us);
void                gt_port_activity_set_viewed(GtPortActivity *a, gboolean viewed);
void                gt_port_activity_tick(GtPortActivity *a, gint64 now_us);
GtPortActivityState gt_port_activity_get_state(GtPortActivity *a);
gboolean            gt_port_activity_get_unread(GtPortActivity *a);
const gchar        *gt_port_activity_state_nick(GtPortActivityState st);

G_END_DECLS

#endif
```

- [ ] **Step 2 : tests qui échouent**

`tests/test_port_activity.c` (style de `tests/test_term_options.c`) :

```c
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
est_actif(gpointer data)
{
	return gt_port_activity_get_state(data) == GT_PORT_ACTIVITY_ACTIVE;
}

static void
test_source_reelle(void)
{
	GtPortActivity *a = gt_port_activity_new();	/* use-timer vrai */
	gint64 fin;

	gt_port_activity_set_link_state(a, GT_PORT_LINK_OPEN, g_get_monotonic_time());
	gt_port_activity_received(a, g_get_monotonic_time());
	ETAT(a, "receiving");
	fin = g_get_monotonic_time() + 2 * S;
	while(!est_actif(a) && g_get_monotonic_time() < fin)
		g_main_context_iteration(NULL, TRUE);
	ETAT(a, "active");
	g_object_unref(a);	/* source retirée : aucun rappel après libération */
	fin = g_get_monotonic_time() + 400 * MS;
	while(g_get_monotonic_time() < fin)
		g_main_context_iteration(NULL, FALSE);
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
```

Ajouter `'test_port_activity' : [],` à `unit_tests` (`tests/meson.build`, ordre alphabétique libre, après `'test_port_form'`), et
`'port_activity.c', 'port_activity.h',` à `core_sources` (`src/meson.build`, après `port_form.h`).

- [ ] **Step 3 :** `ninja -C build` → échec de lien (fonctions absentes). Attendu.

- [ ] **Step 4 : implémentation** `src/port_activity.c` (style dominant : tabulations, Allman, `if(`) :

```c
/* Activité d'un port : voir port_activity.h. */
#include <config.h>

#include "port_activity.h"

struct _GtPortActivity
{
	GObject parent_instance;
	GtPortActivityLink link;
	GtPortActivityState state;
	gint64 opened_us;	/* entrée en OPEN (ou reset) */
	gint64 lost_us;	/* entrée dans {LOST, RECONNECTING} depuis un autre état */
	gint64 last_rx_us;	/* -1 : rien reçu depuis l'entrée en OPEN */
	guint idle_s;
	guint no_data_s;
	gboolean viewed;
	gboolean unread;
	gboolean use_timer;
	guint timer_id;
	gint64 timer_due_us;
};

enum { PROP_0, PROP_STATE, PROP_UNREAD, PROP_USE_TIMER, N_PROPS };
static GParamSpec *props[N_PROPS];

G_DEFINE_TYPE(GtPortActivity, gt_port_activity, G_TYPE_OBJECT)

static const GEnumValue state_values[] =
{
	{ GT_PORT_ACTIVITY_CLOSED, "GT_PORT_ACTIVITY_CLOSED", "closed" },
	{ GT_PORT_ACTIVITY_CONNECTING, "GT_PORT_ACTIVITY_CONNECTING", "connecting" },
	{ GT_PORT_ACTIVITY_WAITING, "GT_PORT_ACTIVITY_WAITING", "waiting" },
	{ GT_PORT_ACTIVITY_ACTIVE, "GT_PORT_ACTIVITY_ACTIVE", "active" },
	{ GT_PORT_ACTIVITY_RECEIVING, "GT_PORT_ACTIVITY_RECEIVING", "receiving" },
	{ GT_PORT_ACTIVITY_IDLE, "GT_PORT_ACTIVITY_IDLE", "idle" },
	{ GT_PORT_ACTIVITY_LOST, "GT_PORT_ACTIVITY_LOST", "lost" },
	{ GT_PORT_ACTIVITY_RECONNECTING, "GT_PORT_ACTIVITY_RECONNECTING", "reconnecting" },
	{ GT_PORT_ACTIVITY_DEAD, "GT_PORT_ACTIVITY_DEAD", "dead" },
	{ 0, NULL, NULL }
};

GType gt_port_activity_state_get_type(void)
{
	static gsize type_id = 0;

	if(g_once_init_enter(&type_id))
		g_once_init_leave(&type_id, g_enum_register_static("GtPortActivityState", state_values));
	return type_id;
}

const gchar *gt_port_activity_state_nick(GtPortActivityState st)
{
	for(guint i = 0; state_values[i].value_nick != NULL; i++)
		if(state_values[i].value == (gint)st)
			return state_values[i].value_nick;
	return "?";
}

static gboolean is_lost(GtPortActivityLink l)
{
	return l == GT_PORT_LINK_LOST || l == GT_PORT_LINK_RECONNECTING;
}

static GtPortActivityState compute(GtPortActivity *a, gint64 now)
{
	gint64 no_data = (gint64)a->no_data_s * G_USEC_PER_SEC;
	gint64 idle = (gint64)a->idle_s * G_USEC_PER_SEC;

	switch(a->link)
	{
	case GT_PORT_LINK_CONNECTING:
	case GT_PORT_LINK_LISTENING:
		return GT_PORT_ACTIVITY_CONNECTING;
	case GT_PORT_LINK_LOST:
	case GT_PORT_LINK_RECONNECTING:
		if(no_data > 0 && now - a->lost_us >= no_data)
			return GT_PORT_ACTIVITY_DEAD;
		return a->link == GT_PORT_LINK_LOST ? GT_PORT_ACTIVITY_LOST : GT_PORT_ACTIVITY_RECONNECTING;
	case GT_PORT_LINK_OPEN:
		if(a->last_rx_us < 0)
			return (no_data > 0 && now - a->opened_us >= no_data) ? GT_PORT_ACTIVITY_DEAD : GT_PORT_ACTIVITY_WAITING;
		if(now - a->last_rx_us < GT_PORT_ACTIVITY_RX_PULSE_US)
			return GT_PORT_ACTIVITY_RECEIVING;
		if(idle > 0 && now - a->last_rx_us >= idle)
			return GT_PORT_ACTIVITY_IDLE;
		return GT_PORT_ACTIVITY_ACTIVE;
	default:
		return GT_PORT_ACTIVITY_CLOSED;
	}
}

/* Prochaine échéance strictement après now, -1 sans échéance */
static gint64 next_deadline(GtPortActivity *a, gint64 now)
{
	gint64 no_data = (gint64)a->no_data_s * G_USEC_PER_SEC;
	gint64 idle = (gint64)a->idle_s * G_USEC_PER_SEC;
	gint64 due = -1;
	gint64 c[2] = { -1, -1 };

	if(a->link == GT_PORT_LINK_OPEN && a->last_rx_us < 0 && no_data > 0)
		c[0] = a->opened_us + no_data;
	else if(a->link == GT_PORT_LINK_OPEN && a->last_rx_us >= 0)
	{
		c[0] = a->last_rx_us + GT_PORT_ACTIVITY_RX_PULSE_US;
		if(idle > 0)
			c[1] = a->last_rx_us + idle;
	}
	else if(is_lost(a->link) && no_data > 0)
		c[0] = a->lost_us + no_data;
	for(guint i = 0; i < G_N_ELEMENTS(c); i++)
		if(c[i] > now && (due < 0 || c[i] < due))
			due = c[i];
	return due;
}

static void update(GtPortActivity *a, gint64 now);

static gboolean on_timer(gpointer data)
{
	GtPortActivity *a = data;

	a->timer_id = 0;
	update(a, g_get_monotonic_time());
	return G_SOURCE_REMOVE;
}

/* Une seule source : réarmée seulement si l'échéance est plus proche que celle déjà armée (une réception repousse les échéances : aucune
 * source par paquet) ; à l'expiration, update réarme sur la suivante */
static void schedule(GtPortActivity *a, gint64 now)
{
	gint64 due;

	if(!a->use_timer)
		return;
	due = next_deadline(a, now);
	if(due < 0)
	{
		g_clear_handle_id(&a->timer_id, g_source_remove);
		return;
	}
	if(a->timer_id != 0 && a->timer_due_us <= due)
		return;
	g_clear_handle_id(&a->timer_id, g_source_remove);
	a->timer_due_us = due;
	a->timer_id = g_timeout_add((guint)MAX((due - now + 999) / 1000, 1), on_timer, a);
}

static void update(GtPortActivity *a, gint64 now)
{
	GtPortActivityState st = compute(a, now);

	if(st != a->state)
	{
		a->state = st;
		g_object_notify_by_pspec(G_OBJECT(a), props[PROP_STATE]);
	}
	schedule(a, now);
}

static void set_unread(GtPortActivity *a, gboolean unread)
{
	if(a->unread == unread)
		return;
	a->unread = unread;
	g_object_notify_by_pspec(G_OBJECT(a), props[PROP_UNREAD]);
}

GtPortActivity *gt_port_activity_new(void)
{
	return g_object_new(GT_TYPE_PORT_ACTIVITY, NULL);
}

void gt_port_activity_set_link_state(GtPortActivity *a, GtPortActivityLink st, gint64 now_us)
{
	if(st == GT_PORT_LINK_OPEN && a->link != GT_PORT_LINK_OPEN)
	{
		a->opened_us = now_us;
		a->last_rx_us = -1;
	}
	if(is_lost(st) && !is_lost(a->link))
		a->lost_us = now_us;
	a->link = st;
	update(a, now_us);
}

void gt_port_activity_received(GtPortActivity *a, gint64 now_us)
{
	if(a->link != GT_PORT_LINK_OPEN)
		return;
	a->last_rx_us = now_us;
	if(!a->viewed)
		set_unread(a, TRUE);
	update(a, now_us);
}

void gt_port_activity_reset(GtPortActivity *a, gint64 now_us)
{
	if(a->link == GT_PORT_LINK_OPEN)
	{
		a->opened_us = now_us;
		a->last_rx_us = -1;
	}
	update(a, now_us);
}

void gt_port_activity_set_timeouts(GtPortActivity *a, guint idle_s, guint no_data_s, gint64 now_us)
{
	a->idle_s = MIN(idle_s, GT_PORT_ACTIVITY_TIMEOUT_MAX);
	a->no_data_s = MIN(no_data_s, GT_PORT_ACTIVITY_TIMEOUT_MAX);
	g_clear_handle_id(&a->timer_id, g_source_remove);	/* échéances changées : la source est recalculée */
	update(a, now_us);
}

void gt_port_activity_set_viewed(GtPortActivity *a, gboolean viewed)
{
	a->viewed = viewed != FALSE;
	if(a->viewed)
		set_unread(a, FALSE);
}

void gt_port_activity_tick(GtPortActivity *a, gint64 now_us)
{
	update(a, now_us);
}

GtPortActivityState gt_port_activity_get_state(GtPortActivity *a)
{
	return a->state;
}

gboolean gt_port_activity_get_unread(GtPortActivity *a)
{
	return a->unread;
}

static void gt_port_activity_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GtPortActivity *a = GT_PORT_ACTIVITY(object);

	switch(prop_id)
	{
	case PROP_STATE:
		g_value_set_enum(value, a->state);
		break;
	case PROP_UNREAD:
		g_value_set_boolean(value, a->unread);
		break;
	case PROP_USE_TIMER:
		g_value_set_boolean(value, a->use_timer);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}

static void gt_port_activity_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GtPortActivity *a = GT_PORT_ACTIVITY(object);

	switch(prop_id)
	{
	case PROP_USE_TIMER:
		a->use_timer = g_value_get_boolean(value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}

static void gt_port_activity_dispose(GObject *object)
{
	GtPortActivity *a = GT_PORT_ACTIVITY(object);

	g_clear_handle_id(&a->timer_id, g_source_remove);
	G_OBJECT_CLASS(gt_port_activity_parent_class)->dispose(object);
}

static void gt_port_activity_class_init(GtPortActivityClass *klass)
{
	GObjectClass *oc = G_OBJECT_CLASS(klass);

	oc->get_property = gt_port_activity_get_property;
	oc->set_property = gt_port_activity_set_property;
	oc->dispose = gt_port_activity_dispose;
	props[PROP_STATE] = g_param_spec_enum("state", NULL, NULL, GT_TYPE_PORT_ACTIVITY_STATE, GT_PORT_ACTIVITY_CLOSED,
	                                      G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
	props[PROP_UNREAD] = g_param_spec_boolean("unread", NULL, NULL, FALSE, G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
	props[PROP_USE_TIMER] = g_param_spec_boolean("use-timer", NULL, NULL, TRUE,
	                                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
	g_object_class_install_properties(oc, N_PROPS, props);
}

static void gt_port_activity_init(GtPortActivity *a)
{
	a->link = GT_PORT_LINK_CLOSED;
	a->state = GT_PORT_ACTIVITY_CLOSED;
	a->last_rx_us = -1;
	a->idle_s = 10;
	a->no_data_s = 60;
	a->viewed = TRUE;
	a->use_timer = TRUE;
}
```

- [ ] **Step 5 :** `ninja -C build && meson test -C build test_port_activity -v` → tous PASS. Corriger le code (pas les tests) en cas d'échec,
  sauf erreur manifeste d'un test par rapport à la spec.

- [ ] **Step 6 : commit**

```bash
git add src/port_activity.c src/port_activity.h tests/test_port_activity.c src/meson.build tests/meson.build
git commit -m "cœur : GtPortActivity, état d'activité d'un port (réception, repos, perte, port muet) et « non lu »"
```

---

### Task 2 : délais dans le profil

**Files:**
- Modify: `src/profile.h` (struct `GtProfile`, après `wait_char`), `src/profile.c` (`gt_profile_init_defaults`, `gt_profile_validate`),
  `src/config_store.c` (`read_profile` ≈ l. 190, `write_profile` ≈ l. 234)
- Test: `tests/test_profile.c`, `tests/test_config_store.c`

- [ ] **Step 1 : tests qui échouent.** Dans `tests/test_profile.c`, un cas `/profile/delais-activite` :

```c
static void
test_delais_activite(void)
{
	GtProfile p;
	GPtrArray *w = g_ptr_array_new_with_free_func(g_free);

	gt_profile_init_defaults(&p);
	g_assert_cmpint(p.idle_timeout_s, ==, 10);
	g_assert_cmpint(p.no_data_timeout_s, ==, 60);
	p.idle_timeout_s = 0;
	p.no_data_timeout_s = 3600;
	g_assert_true(gt_profile_validate(&p, w));
	g_assert_cmpint(p.idle_timeout_s, ==, 0);
	p.idle_timeout_s = -1;
	p.no_data_timeout_s = 3601;
	g_assert_false(gt_profile_validate(&p, w));
	g_assert_cmpuint(w->len, ==, 2);
	g_assert_cmpint(p.idle_timeout_s, ==, 10);
	g_assert_cmpint(p.no_data_timeout_s, ==, 60);
	gt_profile_clear(&p);
	g_ptr_array_unref(w);
}
```

Dans `tests/test_config_store.c`, un cas `/config_store/delais-activite` qui suit le modèle des cas voisins d'aller-retour de profil (lire le
fichier pour réutiliser ses aides : écriture d'un `.gtktermrc` temporaire, ouverture du magasin, lecture du profil) : (1) un profil avec
`activity_idle_timeout=0` et `activity_no_data_timeout=120` se relit en 0 et 120 ; (2) un profil sans ces clés donne 10 et 60 ; (3) un profil
enregistré puis relu garde 0 (la clé est écrite). Si des tests existants comparent le texte complet d'un profil écrit, y ajouter les deux
nouvelles lignes `activity_idle_timeout=10` et `activity_no_data_timeout=60` à l'endroit où `write_profile` les écrit.

- [ ] **Step 2 :** `ninja -C build` → échec de compilation (champs absents). Attendu.

- [ ] **Step 3 : implémentation**

`src/profile.h`, dans la struct après `gint wait_char;` :

```c
	/* activité du port (indicateurs), en secondes, 0 à GT_PROFILE_ACTIVITY_MAX, 0 : désactivé */
	gint idle_timeout_s;		/* repos (Zzz) après ce silence ; défaut 10 */
	gint no_data_timeout_s;		/* port muet ou perdu (tête de mort) après ce délai ; défaut 60 */
```

et près des autres `#define` : `#define GT_PROFILE_ACTIVITY_MAX 3600`.

`src/profile.c`, `gt_profile_init_defaults` après `p->wait_char = -1;` : `p->idle_timeout_s = 10;` et `p->no_data_timeout_s = 60;`.
`gt_profile_validate`, après le bloc `wait_delay_ms` :

```c
	if(p->idle_timeout_s < 0 || p->idle_timeout_s > GT_PROFILE_ACTIVITY_MAX)
	{
		g_ptr_array_add(warnings, g_strdup_printf(_("Invalid idle delay: %d s\nFalling back to default idle delay: %d s\n"), p->idle_timeout_s, 10));
		p->idle_timeout_s = 10;
	}

	if(p->no_data_timeout_s < 0 || p->no_data_timeout_s > GT_PROFILE_ACTIVITY_MAX)
	{
		g_ptr_array_add(warnings, g_strdup_printf(_("Invalid no data delay: %d s\nFalling back to default no data delay: %d s\n"),
		                                          p->no_data_timeout_s, 60));
		p->no_data_timeout_s = 60;
	}
```

`src/config_store.c` : `read_profile`, après `rs485_rts_time_after_tx` : `read_int(kf, g, "activity_idle_timeout", &p->idle_timeout_s);` et
`read_int(kf, g, "activity_no_data_timeout", &p->no_data_timeout_s);` (**`read_int`**, pas `read_uint` qui ignore 0). `write_profile`, au même
endroit : `g_key_file_set_integer(kf, g, "activity_idle_timeout", p->idle_timeout_s);` et
`g_key_file_set_integer(kf, g, "activity_no_data_timeout", p->no_data_timeout_s);`.

- [ ] **Step 4 :** `ninja -C build && meson test -C build` → tout PASS (dont `test_app_config`, `test_cmdline`).
- [ ] **Step 5 : commit** — `git commit -am "profil : délais de repos et sans données (activity_idle_timeout, activity_no_data_timeout)"` (après `git add` des tests).

---

### Task 3 : délais dans le formulaire Port (cœur)

**Files:** Modify `src/port_form.h`, `src/port_form.c` ; Test `tests/test_port_form.c`

- [ ] **Step 1 : tests qui échouent** (`tests/test_port_form.c`, lire les aides du fichier) : `/port-form/delais-activite` —
  `gt_port_form_from_profile` d'un profil à 0 / 120 donne `idle_timeout_s == 0`, `no_data_timeout_s == 120` ; un profil à 5000 donne 3600
  (borné) ; `gt_port_form_apply` avec 7 / 30 recopie 7 / 30 dans `out` en série **et** en client TCP ; avec `idle_timeout_s = 3601` échoue avec
  `GT_PORT_FORM_ERROR_ACTIVITY` et `out` inchangé.
- [ ] **Step 2 :** compilation en échec. Attendu.
- [ ] **Step 3 : implémentation.** `port_form.h` : champs `gint idle_timeout_s; gint no_data_timeout_s;` après `rs485_after_ms` ; code d'erreur
  `GT_PORT_FORM_ERROR_ACTIVITY` (commentaire : « délai d'activité hors de 0 à GT_PROFILE_ACTIVITY_MAX s ») ajouté en **dernier** dans l'enum.
  `port_form.c` : dans `from_profile`, `f->idle_timeout_s = CLAMP(p->idle_timeout_s, 0, GT_PROFILE_ACTIVITY_MAX);` (idem `no_data`) ; dans
  `apply`, après le contrôle de `wait_char` et avant `gt_profile_copy` :

```c
	if(f->idle_timeout_s < 0 || f->idle_timeout_s > GT_PROFILE_ACTIVITY_MAX || f->no_data_timeout_s < 0 || f->no_data_timeout_s > GT_PROFILE_ACTIVITY_MAX)
	{
		g_set_error(error, GT_PORT_FORM_ERROR, GT_PORT_FORM_ERROR_ACTIVITY, _("Activity delays must be between 0 and %d s."), GT_PROFILE_ACTIVITY_MAX);
		goto out;
	}
```

  et, après `out->wait_delay_ms = …;` : `out->idle_timeout_s = f->idle_timeout_s;` `out->no_data_timeout_s = f->no_data_timeout_s;`.
  Mettre à jour le commentaire de `gt_port_form_from_profile` dans l'en-tête (« délais d'activité bornés à 0-3600 »).
- [ ] **Step 4 :** `ninja -C build && meson test -C build` → PASS.
- [ ] **Step 5 : commit** — « formulaire Port : délais d'activité ».

---

### Task 4 : activité de la session

**Files:** Modify `src/session.h`, `src/session.c` ; Test `tests/test_session.c`

- [ ] **Step 1 : tests qui échouent** (`tests/test_session.c`, réutiliser `pty_ouvrir`, `iterate_until` et l'ouverture de port des cas existants —
  lire `test_profil` / `test_deux_sessions_independantes` pour la façon d'ouvrir une session sur une pty) :
  `/session/activite` : session sur une pty, profil avec `idle_timeout_s = 1`, `no_data_timeout_s = 2` → `gt_session_open_port` → état
  `waiting` ; écriture de `"x"` côté maître → `iterate_until` état `receiving` (ou `active` si déjà passé) puis `idle` en ≤ 3 s ;
  `gt_session_open_port` de nouveau (réouverture d'un port ouvert) → `waiting` ; puis `dead` en ≤ 4 s ; `gt_session_close_port` → `closed`.
  `/session/activite-delais-profil` : `gt_session_apply_profile` après changement des délais du profil les applique (0 / 0 : port ouvert et muet
  reste `waiting` au-delà de 1 s).
  Lire l'état par `gt_port_activity_state_nick(gt_port_activity_get_state(gt_session_get_activity(s)))`.
- [ ] **Step 2 :** échec de compilation. Attendu.
- [ ] **Step 3 : implémentation**

`session.h` : `#include "port_activity.h"` et
`GtPortActivity *gt_session_get_activity(GtSession *s);	/* activité du port de l'onglet (indicateurs) */`.

`session.c` : champ `GtPortActivity *activity;` dans la struct ;

```c
static GtPortActivityLink activity_link_state(GtkTermLink *link)
{
	switch(gt_link_get_state(link))
	{
	case GT_LINK_CONNECTING: return GT_PORT_LINK_CONNECTING;
	case GT_LINK_OPEN: return GT_PORT_LINK_OPEN;
	case GT_LINK_LISTENING: return GT_PORT_LINK_LISTENING;
	case GT_LINK_LOST: return GT_PORT_LINK_LOST;
	case GT_LINK_RECONNECTING: return GT_PORT_LINK_RECONNECTING;
	default: return GT_PORT_LINK_CLOSED;
	}
}

/* notify::state de la liaison (peut tourner sous la surveillance du transport : ne fait que mettre à jour le modèle) */
static void on_link_state_activity(GObject *link, GParamSpec *pspec, gpointer user_data)
{
	GtSession *s = user_data;

	gt_port_activity_set_link_state(s->activity, activity_link_state(s->link), g_get_monotonic_time());
}

/* received de la liaison : même contrainte de réentrance */
static void on_link_received(GtkTermLink *link, GBytes *data, gint64 wall_time_us, gpointer user_data)
{
	gt_port_activity_received(GT_SESSION(user_data)->activity, g_get_monotonic_time());
}

static void apply_activity_timeouts(GtSession *s)
{
	gt_port_activity_set_timeouts(s->activity, (guint)MAX(s->profile.idle_timeout_s, 0), (guint)MAX(s->profile.no_data_timeout_s, 0),
	                              g_get_monotonic_time());
}
```

Vérifier dans `link.h` la signature exacte du signal `received` et l'adapter. Dans `gt_session_new`, après `install_actions(s);` :

```c
	s->activity = gt_port_activity_new();
	apply_activity_timeouts(s);
	g_signal_connect(s->link, "notify::state", G_CALLBACK(on_link_state_activity), s);
	g_signal_connect(s->link, "received", G_CALLBACK(on_link_received), s);
```

`gt_session_apply_profile` : `apply_activity_timeouts(s);` à la fin. `gt_session_open_port` : après `update_short_name(s);`,
`apply_activity_timeouts(s);` puis `gt_port_activity_reset(s->activity, g_get_monotonic_time());`, avant `gt_link_open`. Getter
`gt_session_get_activity`. Dans `gt_session_dispose`, après `g_signal_handlers_disconnect_by_data(s->link, s);` : `g_clear_object(&s->activity);`.
(Les gestionnaires sont déconnectés par `disconnect_by_data` existant ; `gt_session_shutdown` ferme la liaison, donc l'activité passe à `closed`
et retire sa source.)

- [ ] **Step 4 :** `ninja -C build && meson test -C build` → PASS.
- [ ] **Step 5 : commit** — « session : activité du port branchée sur la liaison ».

---

### Task 5 : widget `activity_icon` (UI)

> **Réalisation (écart au code ci-dessous) :** les indicateurs sont superposés dans un `GtkOverlay` (boîte 18 × 16 en enfant principal, un
> seul indicateur opaque par `gtk_widget_set_opacity`, aucun pour `closed`) au lieu d'une `GtkStack` : changer l'enfant visible d'une
> `GtkStack` dans l'étiquette d'un onglet du `GtkNotebook` défilable provoquait des avertissements de mesure GTK (« Trying to measure
> GtkImage … for width of 0 », GTK 4.24). Le spinner tourne par une minuterie `g_timeout_add(66, …)` (angle tiré de `g_get_monotonic_time()`)
> au lieu de `gtk_widget_add_tick_callback`, armée et retirée aux mêmes conditions. L'overlay a le rôle accessible `GTK_ACCESSIBLE_ROLE_IMG`,
> ses indicateurs sont cachés à l'accessibilité. Référence : `src/ui/activity_icon.c`.

**Files:**
- Create: `src/ui/activity_icon.h`, `src/ui/activity_icon.c`, `src/ui/icons/scalable/actions/skull-symbolic.svg`
- Modify: `src/ui/meson.build` (sources de l'UI, à côté de `tab_label.c`), `src/ui/gtkterm.gresource.xml`, `src/ui/app.c` (CSS, chemin d'icônes), `po/POTFILES.in`

- [ ] **Step 1 : SVG** (16 × 16, symbolique : une seule couleur `#222`, recolorée par GTK) :

```xml
<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24"><path fill="#222222" d="M12 2C7 2 3.5 5.6 3.5 10.2c0 2.7 1.2 4.7 3 5.9V19c0 .6.4 1 1 1h1.3v-2h1.4v2h3.6v-2h1.4v2h1.3c.6 0 1-.4 1-1v-2.9c1.8-1.2 3-3.2 3-5.9C20.5 5.6 17 2 12 2zM8.6 13.6a2 2 0 1 1 0-4 2 2 0 0 1 0 4zm6.8 0a2 2 0 1 1 0-4 2 2 0 0 1 0 4zM12 16.4l-1.1-2.2h2.2L12 16.4z"/></svg>
```

`gtkterm.gresource.xml`, dans le `gresource` existant (préfixe `/eu/mulagabriel/gtkterm/ui`) :
`<file preprocess="xml-stripblanks">icons/scalable/actions/skull-symbolic.svg</file>`.

- [ ] **Step 2 : en-tête** `src/ui/activity_icon.h` :

```c
/* Indicateur d'activité d'un port (onglet, barre d'état) : emplacement 18 × 16 px montrant l'état d'un GtPortActivity (pastille, spinner,
 * Zzz, tête de mort). Animé par le code (rappel par image, minuterie), pas en CSS : les animations CSS s'arrêtent quand le bureau les réduit. */
#ifndef GT_UI_ACTIVITY_ICON_H_
#define GT_UI_ACTIVITY_ICON_H_

#include <gtk/gtk.h>

#include "port_activity.h"

G_BEGIN_DECLS

/* name : nom stable du widget (tests). La classe CSS « activity-<nick> » suit l'état ; nom accessible traduit selon l'état. */
GtkWidget *gt_activity_icon_new(GtPortActivity *activity, const char *name);

G_END_DECLS

#endif
```

- [ ] **Step 3 : implémentation** `src/ui/activity_icon.c` :

```c
/* Indicateur d'activité d'un port : voir activity_icon.h. */
#include <config.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <math.h>

#include "activity_icon.h"

#define ICON_KEY "gt-activity-icon"
#define SPIN_FRAME_US 66000	/* ≈ 15 images par seconde */
#define SPIN_PERIOD_US 900000	/* un tour */
#define ZZZ_STEP_MS 800

typedef struct
{
	GtkWidget *stack;	/* le widget rendu (non possédé) */
	GtPortActivity *activity;	/* référence */
	GtkWidget *dot, *spinner, *skull, *zzz;
	GtkWidget *z[3];
	GtPortActivityState state;
	guint tick_id;
	gint64 last_frame_us;
	gdouble angle;
	guint zzz_id;
	gint zzz_step;
} ActivityIcon;

static const gchar *accessible_label(GtPortActivityState st)
{
	switch(st)
	{
	case GT_PORT_ACTIVITY_CONNECTING: return _("Connecting");
	case GT_PORT_ACTIVITY_WAITING: return _("Waiting for data");
	case GT_PORT_ACTIVITY_ACTIVE: return _("Port open");
	case GT_PORT_ACTIVITY_RECEIVING: return _("Receiving data");
	case GT_PORT_ACTIVITY_IDLE: return _("Idle");
	case GT_PORT_ACTIVITY_LOST: return _("Connection lost");
	case GT_PORT_ACTIVITY_RECONNECTING: return _("Reconnecting");
	case GT_PORT_ACTIVITY_DEAD: return _("No data received");
	default: return _("Port closed");
	}
}

static void draw_spinner(GtkDrawingArea *area, cairo_t *cr, int w, int h, gpointer data)
{
	ActivityIcon *icon = data;
	GdkRGBA c;
	gdouble r = MIN(w, h) / 2.0 - 1.5;

	gtk_widget_get_color(GTK_WIDGET(area), &c);	/* couleur fixée par la classe CSS connecting / reconnecting */
	cairo_set_line_width(cr, 2.0);
	cairo_set_source_rgba(cr, c.red, c.green, c.blue, c.alpha * 0.25);
	cairo_arc(cr, w / 2.0, h / 2.0, r, 0, 2 * G_PI);
	cairo_stroke(cr);
	cairo_set_source_rgba(cr, c.red, c.green, c.blue, c.alpha);
	cairo_arc(cr, w / 2.0, h / 2.0, r, icon->angle, icon->angle + G_PI / 2);
	cairo_stroke(cr);
}

static gboolean on_spin_tick(GtkWidget *widget, GdkFrameClock *clock, gpointer data)
{
	ActivityIcon *icon = data;
	gint64 now = gdk_frame_clock_get_frame_time(clock);

	if(now - icon->last_frame_us >= SPIN_FRAME_US)
	{
		icon->last_frame_us = now;
		icon->angle = fmod((gdouble)now / SPIN_PERIOD_US, 1.0) * 2 * G_PI;
		gtk_widget_queue_draw(widget);
	}
	return G_SOURCE_CONTINUE;
}

static void zzz_show(ActivityIcon *icon)
{
	gtk_widget_set_opacity(icon->z[1], icon->zzz_step >= 1 ? 1.0 : 0.0);
	gtk_widget_set_opacity(icon->z[2], icon->zzz_step >= 2 ? 1.0 : 0.0);
}

static gboolean on_zzz_step(gpointer data)
{
	ActivityIcon *icon = data;

	icon->zzz_step = (icon->zzz_step + 1) % 3;	/* chenillard : Z, Z z, Z z z, Z… */
	zzz_show(icon);
	return G_SOURCE_CONTINUE;
}

/* Animations seulement pour l'état affiché et un widget cartographié */
static void update_animations(ActivityIcon *icon)
{
	gboolean mapped = gtk_widget_get_mapped(icon->stack);
	gboolean spin = mapped && (icon->state == GT_PORT_ACTIVITY_CONNECTING || icon->state == GT_PORT_ACTIVITY_RECONNECTING);
	gboolean zzz = mapped && icon->state == GT_PORT_ACTIVITY_IDLE;

	if(spin && icon->tick_id == 0)
		icon->tick_id = gtk_widget_add_tick_callback(icon->spinner, on_spin_tick, icon, NULL);
	else if(!spin && icon->tick_id != 0)
	{
		gtk_widget_remove_tick_callback(icon->spinner, icon->tick_id);
		icon->tick_id = 0;
	}
	if(zzz && icon->zzz_id == 0)
	{
		icon->zzz_step = 0;
		zzz_show(icon);
		icon->zzz_id = g_timeout_add(ZZZ_STEP_MS, on_zzz_step, icon);
	}
	else if(!zzz)
		g_clear_handle_id(&icon->zzz_id, g_source_remove);
}

static void on_state(GObject *object, GParamSpec *pspec, gpointer data)
{
	ActivityIcon *icon = data;
	GtPortActivityState st = gt_port_activity_get_state(icon->activity);
	gchar *old_class = g_strdup_printf("activity-%s", gt_port_activity_state_nick(icon->state));
	gchar *new_class = g_strdup_printf("activity-%s", gt_port_activity_state_nick(st));
	const gchar *page = "none";

	gtk_widget_remove_css_class(icon->stack, old_class);
	gtk_widget_add_css_class(icon->stack, new_class);
	icon->state = st;
	switch(st)
	{
	case GT_PORT_ACTIVITY_WAITING:
	case GT_PORT_ACTIVITY_ACTIVE:
	case GT_PORT_ACTIVITY_RECEIVING:
	case GT_PORT_ACTIVITY_LOST:
		page = "dot";
		break;
	case GT_PORT_ACTIVITY_CONNECTING:
	case GT_PORT_ACTIVITY_RECONNECTING:
		page = "spinner";
		break;
	case GT_PORT_ACTIVITY_IDLE:
		page = "zzz";
		break;
	case GT_PORT_ACTIVITY_DEAD:
		page = "skull";
		break;
	default:
		break;
	}
	if(st == GT_PORT_ACTIVITY_RECEIVING)
		gtk_widget_add_css_class(icon->dot, "receiving");
	else
		gtk_widget_remove_css_class(icon->dot, "receiving");
	if(st == GT_PORT_ACTIVITY_LOST)
		gtk_widget_add_css_class(icon->dot, "lost");
	else
		gtk_widget_remove_css_class(icon->dot, "lost");
	if(st == GT_PORT_ACTIVITY_RECONNECTING)
	{
		gtk_widget_add_css_class(icon->spinner, "reconnecting");
		gtk_widget_remove_css_class(icon->spinner, "connecting");
	}
	else
	{
		gtk_widget_add_css_class(icon->spinner, "connecting");
		gtk_widget_remove_css_class(icon->spinner, "reconnecting");
	}
	gtk_stack_set_visible_child_name(GTK_STACK(icon->stack), page);
	gtk_accessible_update_property(GTK_ACCESSIBLE(icon->stack), GTK_ACCESSIBLE_PROPERTY_LABEL, accessible_label(st), -1);
	update_animations(icon);
	g_free(old_class);
	g_free(new_class);
}

static void on_map_changed(GtkWidget *widget, gpointer data)
{
	update_animations(data);
}

static void on_destroy(GtkWidget *widget, gpointer data)
{
	ActivityIcon *icon = data;

	g_clear_handle_id(&icon->zzz_id, g_source_remove);
	if(icon->tick_id != 0)
		gtk_widget_remove_tick_callback(icon->spinner, icon->tick_id);
	g_signal_handlers_disconnect_by_data(icon->activity, icon);
	g_object_unref(icon->activity);
	g_object_set_data(G_OBJECT(widget), ICON_KEY, NULL);
	g_free(icon);
}

GtkWidget *gt_activity_icon_new(GtPortActivity *activity, const char *name)
{
	ActivityIcon *icon = g_new0(ActivityIcon, 1);
	static const char *const z_text[3] = { "Z", "z", "z" };
	static const GtkAlign z_valign[3] = { GTK_ALIGN_END, GTK_ALIGN_CENTER, GTK_ALIGN_START };

	icon->activity = g_object_ref(activity);
	icon->state = GT_PORT_ACTIVITY_CLOSED;
	icon->stack = gtk_stack_new();
	gtk_widget_set_name(icon->stack, name);
	gtk_widget_add_css_class(icon->stack, "activity-icon");
	gtk_widget_set_size_request(icon->stack, 18, 16);
	gtk_widget_set_valign(icon->stack, GTK_ALIGN_CENTER);
	gtk_stack_set_transition_type(GTK_STACK(icon->stack), GTK_STACK_TRANSITION_TYPE_NONE);

	gtk_stack_add_named(GTK_STACK(icon->stack), gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0), "none");

	icon->dot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_add_css_class(icon->dot, "activity-dot");
	gtk_widget_set_halign(icon->dot, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(icon->dot, GTK_ALIGN_CENTER);
	gtk_stack_add_named(GTK_STACK(icon->stack), icon->dot, "dot");

	icon->spinner = gtk_drawing_area_new();
	gtk_widget_add_css_class(icon->spinner, "activity-spinner");
	gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(icon->spinner), 12);
	gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(icon->spinner), 12);
	gtk_widget_set_halign(icon->spinner, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(icon->spinner, GTK_ALIGN_CENTER);
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(icon->spinner), draw_spinner, icon, NULL);
	gtk_stack_add_named(GTK_STACK(icon->stack), icon->spinner, "spinner");

	icon->skull = gtk_image_new_from_icon_name("skull-symbolic");
	gtk_image_set_pixel_size(GTK_IMAGE(icon->skull), 14);
	gtk_widget_add_css_class(icon->skull, "activity-skull");
	gtk_stack_add_named(GTK_STACK(icon->stack), icon->skull, "skull");

	icon->zzz = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_add_css_class(icon->zzz, "activity-zzz");
	gtk_widget_set_halign(icon->zzz, GTK_ALIGN_CENTER);
	for(guint i = 0; i < 3; i++)
	{
		gchar *cls = g_strdup_printf("z%u", i + 1);

		icon->z[i] = gtk_label_new(z_text[i]);
		gtk_widget_add_css_class(icon->z[i], cls);
		gtk_widget_set_valign(icon->z[i], z_valign[i]);
		gtk_box_append(GTK_BOX(icon->zzz), icon->z[i]);
		g_free(cls);
	}
	gtk_stack_add_named(GTK_STACK(icon->stack), icon->zzz, "zzz");

	g_object_set_data(G_OBJECT(icon->stack), ICON_KEY, icon);
	g_signal_connect(activity, "notify::state", G_CALLBACK(on_state), icon);	/* déconnecté par on_destroy */
	g_signal_connect(icon->stack, "map", G_CALLBACK(on_map_changed), icon);
	g_signal_connect(icon->stack, "unmap", G_CALLBACK(on_map_changed), icon);
	g_signal_connect(icon->stack, "destroy", G_CALLBACK(on_destroy), icon);
	on_state(NULL, NULL, icon);
	return icon->stack;
}
```


- [ ] **Step 4 : CSS et chemin d'icônes dans `src/ui/app.c`** (`on_activate`, à la place du `load_from_string(".lost label { … }")`) :

```c
	/* Indicateurs d'activité des ports (activity_icon.c) ; onglet muet grisé, onglet non lu en gras */
	css = gtk_css_provider_new();
	gtk_css_provider_load_from_string(css,
		".activity-dot { min-width: 8px; min-height: 8px; border-radius: 50%; border: 1px solid alpha(currentColor, 0.4); }\n"
		".activity-dot.receiving { background-color: #2ec27e; border-color: #2ec27e; }\n"
		".activity-dot.lost { background-color: #e01b24; border-color: #e01b24; }\n"
		".activity-spinner.connecting { color: #3584e4; }\n"
		".activity-spinner.reconnecting { color: #e01b24; }\n"
		".activity-zzz { color: #3584e4; font-weight: 800; }\n"
		".activity-zzz .z1 { font-size: 10px; }\n"
		".activity-zzz .z2 { font-size: 8px; }\n"
		".activity-zzz .z3 { font-size: 6px; }\n"
		".dead #tab-name { opacity: 0.55; }\n"
		".unread #tab-name { font-weight: bold; }\n");
	gtk_style_context_add_provider_for_display(gtk_widget_get_display(gt_app.main_window), GTK_STYLE_PROVIDER(css),
	                                           GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(css);
	gtk_icon_theme_add_resource_path(gtk_icon_theme_get_for_display(gtk_widget_get_display(gt_app.main_window)), "/eu/mulagabriel/gtkterm/ui/icons");
```

Ajouter `src/ui/activity_icon.c` à `src/ui/meson.build` et à `po/POTFILES.in` (ordre alphabétique des fichiers `src/ui/`).

- [ ] **Step 5 :** `ninja -C build` → compile sans avertissement (`-Werror=deprecated-declarations` : aucune API > 4.14 ; `gtk_widget_get_color`
  est 4.10, `gtk_icon_theme_add_resource_path` 4.0, `gtk_drawing_area_set_draw_func` 4.0). `meson test -C build` → PASS.
- [ ] **Step 6 : commit** — « interface : indicateur d'activité d'un port (pastille, spinner, Zzz, tête de mort), animé par le code ».

---

### Task 6 : intégration (onglet, barre d'état, « vu », crochet de test)

**Files:** Modify `src/ui/tab_label.c`, `src/ui/status_bar.h`, `src/ui/status_bar.c`, `src/ui/session_view.c` (l. 137), `src/ui/app.c`
(`update_tab_state`, `on_active_session`), `src/ui/test_hooks.c` (`on_test_sessions`)

- [ ] **Step 1 : `tab_label.c`.** `#include "activity_icon.h"`. Dans `gt_tab_label_new`, avant `gtk_box_append(GTK_BOX(box), name);` :

```c
	gtk_box_append(GTK_BOX(box), gt_activity_icon_new(gt_session_get_activity(s), "tab-activity"));
```

Ajouter :

```c
/* notify::state et notify::unread de l'activité : nom grisé (port muet) ou en gras (reçu hors vue) */
static void on_activity(GtPortActivity *activity, GParamSpec *pspec, gpointer box)
{
	if(gt_port_activity_get_state(activity) == GT_PORT_ACTIVITY_DEAD)
		gtk_widget_add_css_class(GTK_WIDGET(box), "dead");
	else
		gtk_widget_remove_css_class(GTK_WIDGET(box), "dead");
	if(gt_port_activity_get_unread(activity))
		gtk_widget_add_css_class(GTK_WIDGET(box), "unread");
	else
		gtk_widget_remove_css_class(GTK_WIDGET(box), "unread");
}
```

connecté par `g_signal_connect_object(gt_session_get_activity(s), "notify::state", G_CALLBACK(on_activity), box, 0);` et de même pour
`notify::unread`, puis `on_activity(gt_session_get_activity(s), NULL, box);`. Garder `on_link_state` (classe `lost`, utilisée par les tests).

- [ ] **Step 2 : `status_bar`.** Signature `GtkWidget *gt_status_bar_new(GtkTermLink *link, GtPortActivity *activity);` et
  `void gt_status_bar_set_activity_visible(GtkWidget *bar, gboolean visible);` (commentaire : « indicateur d'activité visible ; app.c le montre
  seulement avec un onglet »). Dans la struct : `GtkWidget *activity;`. Dans `gt_status_bar_new`, avant `sb->text` :

```c
	sb->activity = gt_activity_icon_new(activity, "status-activity");
	gtk_widget_set_visible(sb->activity, FALSE);
	gtk_box_append(GTK_BOX(sb->box), sb->activity);
```

et

```c
void gt_status_bar_set_activity_visible(GtkWidget *bar, gboolean visible)
{
	StatusBar *sb = bar != NULL ? g_object_get_data(G_OBJECT(bar), STATUS_BAR_KEY) : NULL;

	if(sb != NULL)
		gtk_widget_set_visible(sb->activity, visible);
}
```

`session_view.c:137` : `status = gt_status_bar_new(link, gt_session_get_activity(s));`.

- [ ] **Step 3 : `app.c`.** Dans `update_tab_state`, dans la boucle sur les pages :

```c
		GtkWidget *view = gtk_notebook_get_nth_page(nb, i);

		gt_tab_label_set_closable(gtk_notebook_get_tab_label(nb, view), n > 1);
		if(!gt_session_view_is_shut_down(view))
			gt_status_bar_set_activity_visible(gt_session_view_get_slot(view, "status-bar"), n == 1);
```

(`#include "status_bar.h"` si absent.) Dans `on_active_session`, après `gt_app_retarget_relays(s);` :

```c
	for(guint i = 0; i < gt_session_manager_get_n(GT_SESSION_MANAGER(sessions)); i++)
	{
		GtSession *other = gt_session_manager_get_nth(GT_SESSION_MANAGER(sessions), i);

		gt_port_activity_set_viewed(gt_session_get_activity(other), other == s);
	}
```

Vérifier que `update_tab_state()` est aussi appelée après l'ajout et la suppression d'une page (sinon l'appeler là) : la barre d'état de
l'unique onglet restant doit réafficher l'indicateur.

- [ ] **Step 4 : `test_hooks.c`.** Dans `on_test_sessions`, après `busy` :

```c
		g_variant_builder_add(&d, "{sv}", "activity",
		                      g_variant_new_string(gt_port_activity_state_nick(gt_port_activity_get_state(gt_session_get_activity(s)))));
		g_variant_builder_add(&d, "{sv}", "unread", g_variant_new_boolean(gt_port_activity_get_unread(gt_session_get_activity(s))));
```

Mettre à jour le commentaire de la fonction (liste des clés) et la docstring de `Env.sessions()` dans `tests/gui/gtktest.py`.

- [ ] **Step 5 :** `ninja -C build && meson test -C build` → PASS ; `meson test -C build --suite gui` → PASS (aucune régression : `test_onglets`,
  `test_lien_perdu`, `test_status_bar` en particulier).
- [ ] **Step 6 : commit** — « interface : indicateur d'activité dans les onglets et la barre d'état (un seul onglet), non lu en gras ».

---

### Task 7 : réglages dans la fenêtre Port

**Files:** Modify `src/ui/port_dialog.c` (struct `PortDialog`, `read_form`, `build_advanced`)

- [ ] **Step 1 :** champs `GtkSpinButton *idle_timeout, *no_data_timeout;` ; fabrique :

```c
static GtkSpinButton *seconds_spin_new(const char *name, gint value)
{
	GtkWidget *spin = gtk_spin_button_new_with_range(0, GT_PROFILE_ACTIVITY_MAX, 1);

	gtk_widget_set_name(spin, name);
	gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 0);
	gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin), TRUE);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), value);
	return GTK_SPIN_BUTTON(spin);
}
```

Dans `build_advanced`, après le cadre « ASCII file transfer » :

```c
	activity_grid = grid_new();
	pd->idle_timeout = seconds_spin_new("port-idle-timeout", form->idle_timeout_s);
	grid_label(activity_grid, _("Id_le after (seconds, 0: never):"), GTK_WIDGET(pd->idle_timeout), 0, 0);
	gtk_grid_attach(GTK_GRID(activity_grid), GTK_WIDGET(pd->idle_timeout), 1, 0, 1, 1);
	pd->no_data_timeout = seconds_spin_new("port-no-data-timeout", form->no_data_timeout_s);
	grid_label(activity_grid, _("_No data after (seconds, 0: never):"), GTK_WIDGET(pd->no_data_timeout), 0, 1);
	gtk_grid_attach(GTK_GRID(activity_grid), GTK_WIDGET(pd->no_data_timeout), 1, 1, 1, 1);
	gtk_box_append(GTK_BOX(box), frame_new(_("Port activity"), activity_grid));
```

(déclarer `GtkWidget *activity_grid;`). Vérifier que les mnémoniques `_I` et `_N` ne sont pas déjà pris dans le dialogue (`grep -n '_("' src/ui/port_dialog.c`) ;
sinon en choisir d'autres libres. `read_form` : `f->idle_timeout_s = spin_value(pd->idle_timeout);` `f->no_data_timeout_s = spin_value(pd->no_data_timeout);`.
Mettre à jour le commentaire d'en-tête du fichier (liste des réglages).

- [ ] **Step 2 :** `ninja -C build && meson test -C build` → PASS ; `meson test -C build --suite gui` → `test_port_dialog` PASS.
- [ ] **Step 3 : commit** — « fenêtre Port : délais d'activité du port ».

---

### Task 8 : scénario GUI

**Files:** Create `tests/gui/test_activite_ports.py` ; Modify `tests/gui/meson.build` (liste `gui_scenarios`, groupe « barres, liaison »)

- [ ] **Step 1 : scénario** (modèle : `test_lien_perdu.py`, `test_onglets.py`) :

```python
# Indicateurs d'activité des ports : attente, réception, actif, repos, port muet (tête de mort), non lu sur l'onglet caché, indicateur de la barre
# d'état avec un seul onglet, délais réglés dans la fenêtre Port puis enregistrés par Save configuration.
import sys
import time
from gtktest import *


def activite(e, index=0):
    s = e.sessions()
    return s[index]['activity'] if len(s) > index else None


e = Env('activite_ports')
e.write_config(e.general() + e.profile(activity_idle_timeout=2, activity_no_data_timeout=5))
e.start(port=False)
check(wait_until(lambda: e.active_session()['state'] == 'open', 5), 'port ouvert')
check(wait_until(lambda: activite(e) in ('waiting', 'dead'), 3), 'ouvert sans donnees : attente (%r)' % activite(e))
check(wait_until(lambda: e.find(name='status-activity'), 3), 'un seul onglet : indicateur dans la barre d etat')
check(e.find(name='status-activity', css='activity-waiting') or e.find(name='status-activity', css='activity-dead'),
      'classe CSS de l etat sur l indicateur')

# Réception continue : receiving ; arrêt : active puis idle
fin = time.time() + 3
vu = set()
while time.time() < fin and 'receiving' not in vu:
    e.write_pty(b'x')
    vu.add(activite(e))
    time.sleep(0.05)
check('receiving' in vu, 'reception continue : receiving', vu)
check(wait_until(lambda: activite(e) == 'active', 3), 'arret de la reception : actif (%r)' % activite(e))
check(wait_until(lambda: activite(e) == 'idle', 4), 'silence de 2 s : repos (%r)' % activite(e))
time.sleep(4)
check(activite(e) == 'idle', 'deja recu : jamais tete de mort (%r)' % activite(e))

# Deuxième onglet muet : tête de mort ; l'indicateur quitte la barre d'état
p2 = e.new_pty()
open_tab(e, p2.port)
check(wait_until(lambda: not e.find(name='status-activity'), 3), 'deux onglets : plus d indicateur dans la barre d etat')
check(wait_until(lambda: len(e.find(name='tab-activity')) == 2, 3), 'deux onglets : un indicateur par onglet')
check(wait_until(lambda: activite(e, 1) == 'dead', 8), 'onglet muet 5 s : tete de mort (%r)' % activite(e, 1))
check(wait_until(lambda: e.find(name='tab-label', css='dead'), 3), 'etiquette de l onglet muet grisee')

# Non lu : réception sur le premier onglet, caché
e.write_pty(b'y')
check(wait_until(lambda: e.sessions()[0]['unread'] is True, 3), 'reception hors vue : non lu')
e.action('previous-tab')
check(wait_until(lambda: e.sessions()[0]['unread'] is False, 3), 'onglet affiche : non lu efface')

# Délais réglés dans la fenêtre Port, OK, puis Save configuration : relus dans le fichier
e.action('port')
e.find1(name='port-dialog')
e.set_text('port-idle-timeout', '7')
e.set_text('port-no-data-timeout', '0')
e.activate('port-ok')
check(wait_until(lambda: not e.find(name='port-dialog'), 5), 'dialogue Port ferme')
# Enregistrement : suivre test_profils.py pour « Save configuration » (action, nom du profil, confirmation) ; puis :
cfg = e.config_group('profile:default')
check(cfg.get('activity_idle_timeout') == '7' and cfg.get('activity_no_data_timeout') == '0', 'delais enregistres', cfg)
e.finish()
sys.exit(conclude())
```

Compléter l'étape « Save configuration » en recopiant exactement la séquence de `tests/gui/test_profils.py` (lire ce fichier), et vérifier que
`e.set_text` fonctionne sur un `GtkSpinButton` (sinon, utiliser la même méthode que `test_port_dialog.py` pour ses champs de délai). Vérifier le
format rendu par `config_group` (lire `gtktest.py`).

- [ ] **Step 2 :** ajouter `'test_activite_ports',` à `gui_scenarios` ; `ninja -C build && meson test -C build --suite gui test_activite_ports -v`
  → PASS. Le relancer 3 fois (`--repeat 3`) pour écarter l'instabilité ; s'il est instable, allonger les échéances des `wait_until`, jamais par
  des pauses fixes.
- [ ] **Step 3 : commit** — « tests gui : indicateurs d'activité des ports ».

---

### Task 9 : documentation, traductions, vérification finale

**Files:** `po/fr.po`, `README.md`, `CLAUDE.md`

- [ ] **Step 1 :** `ninja -C build gtkterm-pot gtkterm-update-po`, puis traduire en français dans `po/fr.po` chaque nouvelle msgid (aucune
  entrée `fuzzy` ni vide pour elles) : « Connecting » → « Connexion », « Waiting for data » → « En attente de données », « Port open » → « Port
  ouvert », « Receiving data » → « Réception de données », « Idle » → « Au repos », « Connection lost » → « Liaison perdue », « Reconnecting » →
  « Reconnexion », « No data received » → « Aucune donnée reçue », « Port closed » → « Port fermé », « Port activity » → « Activité du port »,
  « Id_le after (seconds, 0: never): » → « R_epos après (secondes, 0 : jamais) : », « _No data after (seconds, 0: never): » → « Sa_ns données
  après (secondes, 0 : jamais) : », les deux messages de validation et « Activity delays must be between 0 and %d s. » → « Les délais d'activité
  doivent être compris entre 0 et %d s. » (mnémoniques uniques dans le dialogue en français : r, s, p, d, l, a sont pris, d'où « R_epos » et « Sa_ns » ; l'expander devient « Options de confi_guration avancées », dont le « a » doublait « _Annuler »).
- [ ] **Step 2 :** `README.md` : paragraphe « Port activity indicators » dans la liste des fonctionnalités (états, délais réglables dans Port ›
  Advanced). `CLAUDE.md` : `port_activity.c` (`GtPortActivity`) dans la liste du cœur ; `activity_icon.c` dans les modules UI ; champs du profil
  et clés `activity_*` ; `test-sessions` gagne `activity`, `unread` ; la classe `lost` n'est plus en italique ; indicateur de barre d'état visible
  avec un seul onglet.
- [ ] **Step 3 : vérification complète** : `ninja -C build && meson test -C build` ; `meson test -C build --suite gui` ; build ASan
  (`meson test -C build-asan`) ; `grep -rn 'gtk/gtk.h' src/*.c src/*.h` vide.
- [ ] **Step 4 : commit** — « docs, traductions : indicateurs d'activité des ports ».
