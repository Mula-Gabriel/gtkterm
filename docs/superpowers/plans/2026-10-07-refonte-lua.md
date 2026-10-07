# Moteur Lua : ports sur le transport par instance, arrêt fiable, envois complets — plan d'implémentation (sous-projet 5)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** faire passer les ports Lua 1 à 15 par `GtTransport` (une instance par port, sur le thread principal) avec une file de réception bornée et sûre entre threads, arrêter réellement un script sur « Stop » (crochet Lua, `os.exit` neutralisé, `print` vers la console), rendre `send`/`send_hex` complets ou en échec explicite, arrêter proprement le script à la fermeture de GTKTerm et ne plus figer l'interface quand un script journalise en boucle.

**Architecture :** deux modules nouveaux dans `gtkterm_core` — `rx_queue.c` (`GtRxQueue` : file d'octets bornée, `GMutex` + `GCond`, attente de motif avec échéance, fermeture et annulation) et `script_port.c` (`GtScriptPort` : un `GtTransport` + une `GtRxQueue`, état atomique, raison de perte ; aides hexadécimal / parité / flux). `script_engine.c` est refondu : table des ports sur `GtScriptPort` manipulée par le seul thread Lua, toute action sur un transport ou la liaison exécutée sur le thread principal par `run_on_main_sync`, crochet d'arrêt, exécution protégée, journal regroupé, `script_engine_shutdown()`. L'interface (`script_panel.c`, `gtkterm.c`) affiche la raison de fin, borne la console et appelle l'arrêt à la fermeture.

**Tech Stack :** C (gnu11), GLib ≥ 2.70 (`GMutex`/`GCond`, `g_cond_wait_until`, `g_utf8_make_valid`, `g_main_context_is_owner`), Lua 5.4 (`lua_sethook`, `lua_atpanic`, `lua_pcall`), GTK 3 / GtkSourceView 4 (interface seulement), Meson ≥ 1.0, GLib GTest (TAP), suite graphique `tests/gui/` (Python + AT-SPI + broadway isolé).

**Spec :** `docs/superpowers/specs/2026-10-07-refonte-lua-design.md`

## Global Constraints

- Langue des réponses et des commentaires : français (identifiants de code en anglais, comme dans la spec).
- Lignes de 200 colonnes au plus (jamais repliées à 80).
- Ne jamais lancer l'interface sur l'écran réel ; ne jamais toucher à la vraie configuration de l'utilisateur (`~/.config/.gtktermrc`, `~/.gtktermrc`, `~/.config/gtkterm_macros.ini`, `~/.cache`, `~/.local`) ; ne jamais ouvrir de vrai port série (`/dev/ttyS*`, `ttyUSB*`, `ttyACM*`, `/dev/serial`) — les tests utilisent `openpty` et `127.0.0.1`.
- Tests unitaires et d'intégration : `meson test -C build` (l'environnement de test vide déjà `DISPLAY`/`WAYLAND_DISPLAY`) ; interface : `meson test -C build --suite gui` (broadway privé, jamais l'écran réel) ; ASan : répertoire `build-asan` (`meson test -C build-asan` doit rester vert ; s'il manque : `meson setup build-asan -Db_sanitize=address,undefined`).
- Chaque message de commit en français, terminé par exactement la ligne `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` (à recopier telle quelle, jamais un autre nom de modèle).
- Un hook PostToolUse lance `ninja -C build` après chaque modification de `src/*.c|*.h` : une erreur de compilation apparaît tout de suite.
- `jq` n'est pas installé (JSON : `python3`).
- Branche `feature/refonte-lua`, déjà extraite : ne pas en changer.
- `rx_queue.c` et `script_port.c` sont dans `gtkterm_core` : aucun en-tête GTK, VTE, Lua, GIO ni d'interface (`link.h`, `interface.h`…).
- Le thread Lua ne touche ni GTK, ni un transport, ni la liaison : il n'accède qu'aux files (`GtRxQueue`) et à l'état atomique des ports ; tout le reste passe par `run_on_main_sync`. Aucun verrou d'une `GtRxQueue` n'est jamais tenu pendant un `run_on_main_sync`.
- Raisons rendues aux scripts : en anglais, stables — `"invalid handle"`, `"already open"`, `"not open"`, `"not connected"`, `"link lost"`, `"timeout"`, `"closed"`, `"invalid bits"`, `"invalid stop"`, `"invalid parity"`, `"invalid flow"`, `"invalid baud"`, `"invalid port"`, `"invalid hex: …"`, ou le message de l'erreur du transport sans fin de ligne.
- Taille des files (ports Lua et port 0) : `GT_SCRIPT_RX_MAX` = 1 Mio. Plafond du journal en attente : 1 Mio. Délai par défaut de `send`, `send_hex`, `wait_for`, connexion TCP : 5000 ms. Pause entre deux essais d'envoi : 2 ms. Arrêt vu au moins toutes les 20 ms. Crochet toutes les 1000 instructions. Console : 10 000 lignes. Fermeture de GTKTerm : 3 s au plus.
- Style : celui du fichier modifié — `script_engine.c` et `script_panel.c` en K&R 4 espaces ; `gtkterm.c`, `stub_link.c` en tabulations + `if(` ; nouveaux fichiers C (cœur et tests) dans le style dominant (tabulations, Allman, `if(`, type de retour sur sa propre ligne). Messages des tests en ASCII.
- Construction : `ninja -C build` ; tests : `meson test -C build --print-errorlogs` ; ASan : `meson test -C build-asan --print-errorlogs` ; interface : `meson test -C build --suite gui --print-errorlogs`.

## Décisions prises par ce plan (interprétations de la spec)

1. **Vidage du journal** : la source unique de vidage est une minuterie de 20 ms à `G_PRIORITY_DEFAULT_IDLE` (`g_timeout_add_full`) et non une source d'inactivité pure : sans ce délai, la boucle principale d'un test (ou d'une interface au repos) se réveille à chaque ligne et le regroupement exigé (moins de 1 000 appels pour 100 000 lignes) n'est pas tenu. La priorité reste « après le rafraîchissement GTK ».
2. **Crochet après l'arrêt** : dès que le crochet (toutes les 1000 instructions) voit l'arrêt, il se réarme à **chaque instruction** (`lua_sethook(L, stop_hook, LUA_MASKCOUNT, 1)`) avant de lever l'erreur ; `os.exit` fait de même. Un `pcall` attrape l'erreur une fois, l'instruction suivante hors du `pcall` la relève : même `while true do pcall(function() while true do end end) end` se termine (avec une période fixe de 1000, ce motif pourrait ne jamais sortir).
3. **Raison de fin** : décidée par les drapeaux, pas par l'objet d'erreur (un gestionnaire `xpcall` interrompu donne « error in error handling ») : `os.exit` demandé → selon le code ; sinon script terminé sans erreur → `FINISHED` ; sinon arrêt demandé → `STOPPED` ; sinon `ERROR`.
4. **Domaine d'erreur de `gt_script_hex_decode`** : `G_CONVERT_ERROR` / `G_CONVERT_ERROR_ILLEGAL_SEQUENCE` au lieu de `G_IO_ERROR_INVALID_DATA` : le cœur reste sans GIO (seul le message est utilisé par le moteur). Messages : `invalid character at position N`, `odd number of hex digits`.
5. **Port 0 jamais « fermé »** : la file du port 0 n'est jamais fermée (la liaison se reconnecte seule) ; `wait_for(0, …)` ne rend donc que des données ou `"timeout"`.
6. **Message de débordement** : le cœur ne journalise pas ; le moteur relève les pertes (`gt_rx_queue_take_dropped`) quand le thread Lua touche une file : `read`/`wait_for`/`clear`/`close` du port, `sleep` (tous les ports) et fin de script (tous les ports). Un script qui ne fait jamais rien de cela n'a le message qu'à la fin.
7. **Texte remis à la fonction de journal** : regroupé, donc protégé : un octet nul d'un `log` est écrit `\0` (il couperait tout le lot) et le lot est rendu UTF-8 valide (`g_utf8_make_valid`, sinon `GtkTextBuffer` refuserait tout le lot).
8. **`tcp_server` sur le port 0** : accepté (le système choisit), mais aucune fonction ne rend le port choisi (la spec n'en prévoit pas) ; les tests choisissent un port libre avant d'ouvrir.
9. **Arguments** : un numéro de port hors 0..15 est traité comme « hors plage » même s'il dépasse un `int` (pas de troncature) ; port TCP hors 1..65535 (client) ou 0..65535 (serveur) → `false, "invalid port"` ; délais bornés à [0, 24 h] (un délai négatif vaut 0) ; `wait_for`/`read`/`clear` d'un port Lua non ouvert → `nil, "closed"` / `""` / rien.
10. **Nom du morceau Lua** : `"=script"` (messages « script:3: boum » au lieu de la première ligne du script).
11. **Panneau** : les pointeurs `editor_view`, `console_view`, `run_button`, `stop_button` sont remis à `NULL` à la destruction des widgets (`gtk_widget_destroyed`) : la fin de script peut être distribuée dans la même itération que la destruction de la fenêtre.
12. **Chaînes** : « Script already running. » devient traduisible ; l'avertissement de `script_engine_shutdown` (sortie d'erreur) ne l'est pas. Les identifiants de fin de script gardent leur `\n` final, comme « [Script finished]\n » existant.

## Review Focus

1. **Script qui attrape tout** (`pcall` en boucle, `pcall` imbriqués, `xpcall` avec gestionnaire, coroutines relancées en boucle) : « Stop » le termine quand même, en `STOPPED` (pas `ERROR`), en moins d'une seconde. Épinglé par `test_script_engine` (`/script_engine/stop-boucles-pcall`, tâche 4).
2. **Journal de données brutes** (`gtkterm.log(gtkterm.read(1))` avec octets nuls ou UTF-8 invalide) : le lot regroupé n'est ni coupé ni perdu, la console reçoit de l'UTF-8 valide. Épinglé par `/script_engine/journal-binaire` (tâche 4).
3. **« Stop » pendant une attente du moteur hors `sleep`/`wait_for`** (connexion TCP qui ne répond pas, envoi bloqué par un lecteur absent) : fin rapide, port refermé, descripteurs rendus. Épinglé par `/script_engine/stop-pendant-connexion` et `/script_engine/stop-pendant-envoi` (tâche 4).
4. **Arguments numériques extrêmes** (port 2³²+1, délai négatif, `sleep(-5)`, port TCP 70000) : aucune troncature ni dépassement, raisons stables. Épinglé par `/script_engine/arguments-extremes` (tâche 3).
5. **Exécutions successives après un arrêt ou un `os.exit`** : drapeaux d'arrêt, de sortie et pertes signalées remis à zéro ; le script suivant s'exécute normalement. Épinglé par `/script_engine/relance-apres-arret` (tâche 4).

## Carte des fichiers

| Fichier | Tâche | Rôle |
|---|---|---|
| `src/rx_queue.c/.h` (créés) | 1 | `GtRxQueue` |
| `src/script_port.c/.h` (créés) | 2 | `GtScriptPort`, `gt_script_hex_decode`, noms de parité et de flux, `GT_SCRIPT_RX_MAX` |
| `src/meson.build` | 1, 2 | `core_sources` |
| `tests/test_rx_queue.c`, `tests/test_script_port.c` (créés), `tests/meson.build` | 1, 2 | unitaires |
| `src/script_engine.c` | 3, 4 | moteur refondu (partie A : ports, envois, raisons ; partie B : arrêt, journal, fin, fermeture) |
| `src/script_engine.h` | 4 | `GtScriptEnd`, `ScriptDoneFunc` à raison, `script_engine_shutdown` |
| `tests/stubs/stub_link.c`, `tests/stubs/stubs.h` | 3 | `gt_link_get_state`, `stub_link_set_state` |
| `tests/test_script_engine.c` | 3, 4 | intégration |
| `src/script_panel.c`, `src/gtkterm.c` | 4 | messages de fin, console bornée, gardes de destruction, appel de l'arrêt |
| `po/POTFILES.in`, `po/fr.po` | 4 | traductions |
| `tests/gui/test_script_arret.py` (créé), `tests/gui/meson.build` | 5 | suite graphique |
| `scripts/11_tcp_server.lua` (créé), `scripts/07_tcp_client.lua` | 6 | exemples |
| `.claude/skills/lua-script/SKILL.md`, `CLAUDE.md`, `tests/README.md`, `docs/superpowers/reviews/2026-10-06-revue-code.md` | 6 | documentation |

---

### Task 1 : file de réception bornée et sûre entre threads (`GtRxQueue`)

**Files :**
- Create : `src/rx_queue.h`, `src/rx_queue.c`
- Modify : `src/meson.build` (`core_sources`), `tests/meson.build` (`unit_tests`)
- Test : `tests/test_rx_queue.c` (créé)

**Interfaces :**
- Consumes : rien.
- Produces (utilisé par les tâches 2 à 4) :
  - `typedef struct _GtRxQueue GtRxQueue;`
  - `typedef enum { GT_RX_WAIT_FOUND, GT_RX_WAIT_TIMEOUT, GT_RX_WAIT_CLOSED, GT_RX_WAIT_CANCELLED } GtRxWaitResult;`
  - `GtRxQueue *gt_rx_queue_new(gsize max_bytes);` `void gt_rx_queue_free(GtRxQueue *q);`
  - `gsize gt_rx_queue_push(GtRxQueue *q, const guint8 *data, gsize len);` (octets jetés par cet appel)
  - `GBytes *gt_rx_queue_take_all(GtRxQueue *q);` `void gt_rx_queue_clear(GtRxQueue *q);` `gsize gt_rx_queue_length(GtRxQueue *q);`
  - `gsize gt_rx_queue_take_dropped(GtRxQueue *q);` `void gt_rx_queue_set_closed(GtRxQueue *q, gboolean closed);` `gboolean gt_rx_queue_is_closed(GtRxQueue *q);` `void gt_rx_queue_reset(GtRxQueue *q);`
  - `GtRxWaitResult gt_rx_queue_wait_for(GtRxQueue *q, const guint8 *pattern, gsize pattern_len, gint64 deadline_us, const gint *cancel, GBytes **out);`

- [ ] **Step 1 : écrire le test (échoue à la compilation)**

Créer `tests/test_rx_queue.c` :

```c
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
```

Dans `tests/meson.build`, dictionnaire `unit_tests`, ajouter après `'test_config_store' : [],` :

```meson
	'test_rx_queue' : [],
```

- [ ] **Step 2 : vérifier l'échec**

Run : `ninja -C build`
Expected : échec de compilation de `test_rx_queue.c` (« rx_queue.h: No such file or directory »).

- [ ] **Step 3 : écrire l'en-tête**

Créer `src/rx_queue.h` :

```c
/* File d'octets reçus, bornée, sûre entre threads (sous-projet 5) : réception d'un port Lua ou du port 0 du moteur.
 * Le producteur (thread principal : transport ou signal « received ») ajoute ; le thread Lua lit, attend un motif, vide.
 * Chaque fonction prend et rend le verrou elle-même et n'appelle aucun code extérieur sous ce verrou : l'appelant ne le tient
 * jamais, en particulier pas pendant un appel synchrone vers le thread principal. */
#ifndef RX_QUEUE_H_
#define RX_QUEUE_H_

#include <glib.h>

typedef struct _GtRxQueue GtRxQueue;

typedef enum
{
	GT_RX_WAIT_FOUND,	/* motif trouvé : *out = données jusqu'au motif inclus, retirées de la file */
	GT_RX_WAIT_TIMEOUT,	/* échéance atteinte sans motif */
	GT_RX_WAIT_CLOSED,	/* file fermée (port perdu ou fermé) et motif absent des données restantes */
	GT_RX_WAIT_CANCELLED	/* *cancel est devenu non nul pendant l'attente */
} GtRxWaitResult;

GtRxQueue      *gt_rx_queue_new          (gsize max_bytes);
void            gt_rx_queue_free         (GtRxQueue *q);
/* Ajoute ; au-delà de max_bytes, les octets les plus anciens sont jetés. Renvoie le nombre d'octets jetés par cet appel. */
gsize           gt_rx_queue_push         (GtRxQueue *q, const guint8 *data, gsize len);
GBytes         *gt_rx_queue_take_all     (GtRxQueue *q);
void            gt_rx_queue_clear        (GtRxQueue *q);
gsize           gt_rx_queue_length       (GtRxQueue *q);
/* Octets jetés (débordement) depuis l'appel précédent, remis à zéro. */
gsize           gt_rx_queue_take_dropped (GtRxQueue *q);
/* Fermée : les attentes en cours et futures se terminent par CLOSED si le motif est absent ; push est sans effet. */
void            gt_rx_queue_set_closed   (GtRxQueue *q, gboolean closed);
gboolean        gt_rx_queue_is_closed    (GtRxQueue *q);
/* Vide, rouvre, remet le compteur de pertes à zéro. */
void            gt_rx_queue_reset        (GtRxQueue *q);
/* deadline_us : temps monotone (g_get_monotonic_time) ; cancel : lu atomiquement au moins toutes les 20 ms, peut être NULL.
 * Ordre des issues à chaque réveil : motif trouvé → FOUND (même fermée ou annulée) ; annulation → CANCELLED ; fermée → CLOSED ;
 * échéance passée → TIMEOUT. Motif vide : FOUND immédiat avec des données vides. *out vaut NULL sauf pour FOUND. */
GtRxWaitResult  gt_rx_queue_wait_for     (GtRxQueue *q, const guint8 *pattern, gsize pattern_len, gint64 deadline_us,
                                          const gint *cancel, GBytes **out);

#endif
```

- [ ] **Step 4 : écrire l'implémentation**

Créer `src/rx_queue.c` :

```c
/***********************************************************************/
/* rx_queue.c                                                           */
/*           GTKTerm Software                                           */
/*                                                                      */
/*   Purpose                                                            */
/*      File d'octets reçus bornée et sûre entre threads                */
/*      (cœur sans GTK, sous-projet 5)                                  */
/***********************************************************************/

#include <string.h>

#include "rx_queue.h"

/* Une attente relit l'annulation au moins à ce rythme */
#define WAIT_STEP_US (20 * G_TIME_SPAN_MILLISECOND)

struct _GtRxQueue
{
	GMutex mutex;
	GCond cond;
	GByteArray *data;
	gsize max_bytes;
	gsize dropped;		/* octets jetés depuis le dernier gt_rx_queue_take_dropped */
	gboolean closed;
};

GtRxQueue *
gt_rx_queue_new(gsize max_bytes)
{
	GtRxQueue *q;

	g_return_val_if_fail(max_bytes > 0 && max_bytes <= G_MAXUINT, NULL);
	q = g_new0(GtRxQueue, 1);
	g_mutex_init(&q->mutex);
	g_cond_init(&q->cond);
	q->data = g_byte_array_new();
	q->max_bytes = max_bytes;
	return q;
}

void
gt_rx_queue_free(GtRxQueue *q)
{
	if(q == NULL)
		return;
	g_byte_array_unref(q->data);
	g_cond_clear(&q->cond);
	g_mutex_clear(&q->mutex);
	g_free(q);
}

gsize
gt_rx_queue_push(GtRxQueue *q, const guint8 *data, gsize len)
{
	gsize drop = 0;

	g_return_val_if_fail(q != NULL, 0);
	if(len == 0)
		return 0;
	g_mutex_lock(&q->mutex);
	if(q->closed)
	{
		g_mutex_unlock(&q->mutex);
		return 0;
	}
	if(len >= q->max_bytes)
	{
		/* Le bloc remplit à lui seul la file : seuls ses max_bytes derniers octets sont gardés */
		drop = q->data->len + (len - q->max_bytes);
		g_byte_array_set_size(q->data, 0);
		g_byte_array_append(q->data, data + (len - q->max_bytes), (guint)q->max_bytes);
	}
	else
	{
		if(q->data->len + len > q->max_bytes)
		{
			drop = q->data->len + len - q->max_bytes;
			g_byte_array_remove_range(q->data, 0, (guint)drop);
		}
		g_byte_array_append(q->data, data, (guint)len);
	}
	q->dropped += drop;
	g_cond_broadcast(&q->cond);
	g_mutex_unlock(&q->mutex);
	return drop;
}

GBytes *
gt_rx_queue_take_all(GtRxQueue *q)
{
	GBytes *b;

	g_return_val_if_fail(q != NULL, NULL);
	g_mutex_lock(&q->mutex);
	b = g_bytes_new(q->data->data, q->data->len);
	g_byte_array_set_size(q->data, 0);
	g_mutex_unlock(&q->mutex);
	return b;
}

void
gt_rx_queue_clear(GtRxQueue *q)
{
	g_return_if_fail(q != NULL);
	g_mutex_lock(&q->mutex);
	g_byte_array_set_size(q->data, 0);
	g_mutex_unlock(&q->mutex);
}

gsize
gt_rx_queue_length(GtRxQueue *q)
{
	gsize n;

	g_return_val_if_fail(q != NULL, 0);
	g_mutex_lock(&q->mutex);
	n = q->data->len;
	g_mutex_unlock(&q->mutex);
	return n;
}

gsize
gt_rx_queue_take_dropped(GtRxQueue *q)
{
	gsize n;

	g_return_val_if_fail(q != NULL, 0);
	g_mutex_lock(&q->mutex);
	n = q->dropped;
	q->dropped = 0;
	g_mutex_unlock(&q->mutex);
	return n;
}

void
gt_rx_queue_set_closed(GtRxQueue *q, gboolean closed)
{
	g_return_if_fail(q != NULL);
	g_mutex_lock(&q->mutex);
	q->closed = closed;
	g_cond_broadcast(&q->cond);
	g_mutex_unlock(&q->mutex);
}

gboolean
gt_rx_queue_is_closed(GtRxQueue *q)
{
	gboolean closed;

	g_return_val_if_fail(q != NULL, TRUE);
	g_mutex_lock(&q->mutex);
	closed = q->closed;
	g_mutex_unlock(&q->mutex);
	return closed;
}

void
gt_rx_queue_reset(GtRxQueue *q)
{
	g_return_if_fail(q != NULL);
	g_mutex_lock(&q->mutex);
	g_byte_array_set_size(q->data, 0);
	q->dropped = 0;
	q->closed = FALSE;
	g_mutex_unlock(&q->mutex);
}

GtRxWaitResult
gt_rx_queue_wait_for(GtRxQueue *q, const guint8 *pattern, gsize pattern_len, gint64 deadline_us, const gint *cancel, GBytes **out)
{
	GtRxWaitResult res;

	g_return_val_if_fail(q != NULL && out != NULL, GT_RX_WAIT_CLOSED);
	*out = NULL;
	g_mutex_lock(&q->mutex);
	for(;;)
	{
		gint64 now;

		if(pattern_len == 0)
		{
			*out = g_bytes_new(NULL, 0);
			res = GT_RX_WAIT_FOUND;
			break;
		}
		if(q->data->len >= pattern_len)
		{
			/* Recherche binaire, longueurs explicites : un octet nul n'arrête rien */
			const guint8 *found = memmem(q->data->data, q->data->len, pattern, pattern_len);

			if(found != NULL)
			{
				gsize end = (gsize)(found - q->data->data) + pattern_len;

				*out = g_bytes_new(q->data->data, end);
				g_byte_array_remove_range(q->data, 0, (guint)end);
				res = GT_RX_WAIT_FOUND;
				break;
			}
		}
		if(cancel != NULL && g_atomic_int_get(cancel) != 0)
		{
			res = GT_RX_WAIT_CANCELLED;
			break;
		}
		if(q->closed)
		{
			res = GT_RX_WAIT_CLOSED;
			break;
		}
		now = g_get_monotonic_time();
		if(now >= deadline_us)
		{
			res = GT_RX_WAIT_TIMEOUT;
			break;
		}
		g_cond_wait_until(&q->cond, &q->mutex, MIN(deadline_us, now + WAIT_STEP_US));
	}
	g_mutex_unlock(&q->mutex);
	return res;
}
```

Dans `src/meson.build`, liste `core_sources`, ajouter après `'config_store.h',` :

```meson
	'rx_queue.c',
	'rx_queue.h',
```

- [ ] **Step 5 : vérifier le succès**

Run : `ninja -C build && meson test -C build test_rx_queue --print-errorlogs`
Expected : `test_rx_queue` OK (12 sous-tests).

- [ ] **Step 6 : ASan**

Run : `ninja -C build-asan && meson test -C build-asan test_rx_queue --print-errorlogs`
Expected : OK, aucun rapport ASan/UBSan.

- [ ] **Step 7 : commit**

```bash
git add src/rx_queue.c src/rx_queue.h src/meson.build tests/test_rx_queue.c tests/meson.build
git commit -m "$(cat <<'EOF'
lua : file de réception bornée et sûre entre threads (GtRxQueue)

File d'octets du cœur pour les ports Lua et le port 0 : plafond avec perte des plus
anciens octets comptée, attente de motif binaire avec échéance, fermeture et
annulation relue toutes les 20 ms.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 2 : port de script sur le transport par instance (`GtScriptPort`), hexadécimal, noms

**Files :**
- Create : `src/script_port.h`, `src/script_port.c`
- Modify : `src/meson.build` (`core_sources`), `tests/meson.build` (`unit_tests`)
- Test : `tests/test_script_port.c` (créé)

**Interfaces :**
- Consumes : `GtRxQueue` (tâche 1) ; `transport.h` : `TransportParams`, `TransportCallbacks`, `TransportEvent`, `gt_transport_open(const TransportParams *, const TransportCallbacks *, gpointer, GError **)`, `gt_transport_close(GtTransport *)`, `gssize gt_transport_send(GtTransport *, const guint8 *, gsize)` (0 : rien d'ouvert ; -1 + `errno`), `GT_TRANSPORT_ERROR` / `GT_TRANSPORT_ERROR_OPEN|LOCKED|CONNECT`, `GtParity`, `GtFlow`.
- Produces (utilisé par les tâches 3 et 4) :
  - `#define GT_SCRIPT_RX_MAX (1024 * 1024)`
  - `typedef enum { GT_SCRIPT_PORT_CONNECTING, GT_SCRIPT_PORT_OPEN, GT_SCRIPT_PORT_LISTENING, GT_SCRIPT_PORT_CONNECTED, GT_SCRIPT_PORT_LOST } GtScriptPortState;`
  - `GtScriptPort *gt_script_port_open(const TransportParams *params, GError **error);` (thread principal)
  - `void gt_script_port_close(GtScriptPort *port);` (thread principal)
  - `gssize gt_script_port_try_send(GtScriptPort *port, const guint8 *data, gsize len, gint *err);` (thread principal)
  - `GtScriptPortState gt_script_port_get_state(GtScriptPort *port);` `GtRxQueue *gt_script_port_get_queue(GtScriptPort *port);` `gchar *gt_script_port_dup_reason(GtScriptPort *port);` (tout thread)
  - `gboolean gt_script_parity_from_name(const gchar *name, GtParity *out);` `gboolean gt_script_flow_from_name(const gchar *name, GtFlow *out);` `GBytes *gt_script_hex_decode(const gchar *text, GError **error);`

- [ ] **Step 1 : écrire le test (échoue à la compilation)**

Créer `tests/test_script_port.c` :

```c
/* Tests de script_port.c (cœur) : port d'un script Lua sur le transport (paire pty, TCP local), décodage hexadécimal, noms
 * de parité et de flux. La boucle GLib par défaut tourne dans les attentes (rappels du transport). */

#include <glib.h>
#include <errno.h>
#include <poll.h>
#include <pty.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "script_port.h"

/* ---- Aides ------------------------------------------------------------ */

static gboolean
attendre_etat(GtScriptPort *port, GtScriptPortState etat, guint ms)
{
	gint64 fin = g_get_monotonic_time() + (gint64)ms * 1000;

	while(gt_script_port_get_state(port) != etat)
	{
		if(g_get_monotonic_time() >= fin)
			return FALSE;
		g_main_context_iteration(NULL, FALSE);
		g_usleep(1000);
	}
	return TRUE;
}

static gboolean
attendre_longueur(GtRxQueue *q, gsize n, guint ms)
{
	gint64 fin = g_get_monotonic_time() + (gint64)ms * 1000;

	while(gt_rx_queue_length(q) < n)
	{
		if(g_get_monotonic_time() >= fin)
			return FALSE;
		g_main_context_iteration(NULL, FALSE);
		g_usleep(1000);
	}
	return TRUE;
}

/* Lit jusqu'à n octets (ou moins au bout de ms) en faisant tourner la boucle GLib */
static gsize
lire(int fd, guint8 *buf, gsize n, guint ms)
{
	gsize lu = 0;
	gint64 fin = g_get_monotonic_time() + (gint64)ms * 1000;

	while(lu < n && g_get_monotonic_time() < fin)
	{
		struct pollfd pfd = { fd, POLLIN, 0 };

		if(poll(&pfd, 1, 10) > 0)
		{
			ssize_t r = read(fd, buf + lu, n - lu);

			if(r > 0)
				lu += (gsize)r;
		}
		g_main_context_iteration(NULL, FALSE);
	}
	return lu;
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

/* Les chaînes restent celles de l'appelant : gt_script_port_open copie les paramètres (copie profonde par le transport) */
static void
params_serie(TransportParams *p, const gchar *chemin, guint debit, gint stop)
{
	memset(p, 0, sizeof(*p));
	p->type = TRANSPORT_SERIAL;
	p->port = (gchar *)chemin;
	p->baudrate = debit;
	p->bits = 8;
	p->stopbits = stop;
	p->parity = GT_PARITY_NONE;
	p->flow = GT_FLOW_NONE;
}

static void
params_tcp(TransportParams *p, TransportType type, const gchar *hote, const gchar *port)
{
	memset(p, 0, sizeof(*p));
	p->type = type;
	p->host = (gchar *)hote;
	p->tcp_port = (gchar *)port;
}

/* Socket d'écoute sur 127.0.0.1, port choisi par le système */
static int
ecoute_locale(guint16 *port)
{
	struct sockaddr_in adresse;
	socklen_t taille = sizeof(adresse);
	int fd = socket(AF_INET, SOCK_STREAM, 0);

	g_assert_cmpint(fd, >=, 0);
	memset(&adresse, 0, sizeof(adresse));
	adresse.sin_family = AF_INET;
	adresse.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	g_assert_cmpint(bind(fd, (struct sockaddr *)&adresse, sizeof(adresse)), ==, 0);
	g_assert_cmpint(listen(fd, 1), ==, 0);
	g_assert_cmpint(getsockname(fd, (struct sockaddr *)&adresse, &taille), ==, 0);
	*port = ntohs(adresse.sin_port);
	return fd;
}

static guint16
port_libre(void)
{
	guint16 port;

	close(ecoute_locale(&port));
	return port;
}

static int
connecter(guint16 port)
{
	struct sockaddr_in adresse;
	int fd = socket(AF_INET, SOCK_STREAM, 0);

	memset(&adresse, 0, sizeof(adresse));
	adresse.sin_family = AF_INET;
	adresse.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	adresse.sin_port = htons(port);
	g_assert_cmpint(connect(fd, (struct sockaddr *)&adresse, sizeof(adresse)), ==, 0);
	return fd;
}

/* ---- Série sur pty ------------------------------------------------------ */

static void
test_serie_pty(void)
{
	int maitre, esclave;
	gchar *chemin;
	TransportParams p;
	GtScriptPort *port;
	GError *err = NULL;
	struct termios tio;
	GBytes *b;
	guint8 buf[8];
	gint e = -1;

	if(openpty(&maitre, &esclave, NULL, NULL, NULL) != 0)
	{
		g_test_skip("openpty indisponible");
		return;
	}
	chemin = g_strdup(ttyname(esclave));
	params_serie(&p, chemin, 115200, 2);
	port = gt_script_port_open(&p, &err);
	g_assert_no_error(err);
	g_assert_nonnull(port);
	g_assert_cmpint(gt_script_port_get_state(port), ==, GT_SCRIPT_PORT_OPEN);
	g_assert_null(gt_script_port_dup_reason(port));

	/* Paramètres appliqués (une pty n'expose que le débit et CSTOPB) */
	g_assert_cmpint(tcgetattr(esclave, &tio), ==, 0);
	g_assert_cmpuint(cfgetospeed(&tio), ==, B115200);
	g_assert_cmpuint(tio.c_cflag & CSTOPB, ==, CSTOPB);

	/* Réception dans la file */
	g_assert_cmpint(write(maitre, "abc", 3), ==, 3);
	g_assert_true(attendre_longueur(gt_script_port_get_queue(port), 3, 5000));
	b = gt_rx_queue_take_all(gt_script_port_get_queue(port));
	verifier_octets(b, "abc", 3);
	g_bytes_unref(b);

	/* Envoi */
	g_assert_cmpint(gt_script_port_try_send(port, (const guint8 *)"xyz", 3, &e), ==, 3);
	g_assert_cmpint(e, ==, 0);
	g_assert_cmpuint(lire(maitre, buf, 3, 5000), ==, 3);
	g_assert_cmpmem(buf, 3, "xyz", 3);

	gt_script_port_close(port);
	/* Fermeture : réglages d'origine restaurés (CSTOPB absent par défaut sur une pty) */
	g_assert_cmpint(tcgetattr(esclave, &tio), ==, 0);
	g_assert_cmpuint(tio.c_cflag & CSTOPB, ==, 0);
	close(esclave);
	close(maitre);
	g_free(chemin);
}

static void
test_peripherique_absent(void)
{
	TransportParams p;
	GError *err = NULL;

	params_serie(&p, "/nonexistent/gtkterm-test-tty", 9600, 1);
	g_assert_null(gt_script_port_open(&p, &err));
	g_assert_error(err, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_OPEN);
	g_clear_error(&err);
}

static void
test_maitre_ferme(void)
{
	int maitre, esclave;
	gchar *chemin, *raison;
	TransportParams p;
	GtScriptPort *port;
	GError *err = NULL;
	gint e = 0;

	if(openpty(&maitre, &esclave, NULL, NULL, NULL) != 0)
	{
		g_test_skip("openpty indisponible");
		return;
	}
	chemin = g_strdup(ttyname(esclave));
	params_serie(&p, chemin, 9600, 1);
	port = gt_script_port_open(&p, &err);
	g_assert_no_error(err);
	close(maitre);
	g_assert_true(attendre_etat(port, GT_SCRIPT_PORT_LOST, 5000));
	g_assert_true(gt_rx_queue_is_closed(gt_script_port_get_queue(port)));
	raison = gt_script_port_dup_reason(port);
	g_assert_nonnull(raison);
	g_assert_cmpuint(strlen(raison), >, 0);
	g_assert_false(g_str_has_suffix(raison, "\n"));
	g_free(raison);
	/* Après la perte, l'envoi échoue avec EIO */
	g_assert_cmpint(gt_script_port_try_send(port, (const guint8 *)"x", 1, &e), ==, -1);
	g_assert_cmpint(e, ==, EIO);
	gt_script_port_close(port);
	close(esclave);
	g_free(chemin);
}

static void
test_verrou(void)
{
	int maitre, esclave;
	gchar *chemin;
	TransportParams p;
	GtScriptPort *a, *b;
	GError *err = NULL;

	if(openpty(&maitre, &esclave, NULL, NULL, NULL) != 0)
	{
		g_test_skip("openpty indisponible");
		return;
	}
	chemin = g_strdup(ttyname(esclave));
	params_serie(&p, chemin, 9600, 1);
	/* lock_port n'est pas demandé par l'appelant : gt_script_port_open le force */
	a = gt_script_port_open(&p, &err);
	g_assert_no_error(err);
	b = gt_script_port_open(&p, &err);
	g_assert_null(b);
	g_assert_error(err, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_LOCKED);
	g_clear_error(&err);
	gt_script_port_close(a);
	b = gt_script_port_open(&p, &err);
	g_assert_no_error(err);
	g_assert_nonnull(b);
	gt_script_port_close(b);
	close(esclave);
	close(maitre);
	g_free(chemin);
}

/* ---- TCP ------------------------------------------------------------------ */

static void
test_client_tcp(void)
{
	guint16 num;
	int ecoute = ecoute_locale(&num), pair;
	gchar *port_txt = g_strdup_printf("%u", num);
	TransportParams p;
	GtScriptPort *port;
	GError *err = NULL;
	GBytes *b;
	guint8 buf[8];
	gint e = -1;

	params_tcp(&p, TRANSPORT_TCP_CLIENT, "127.0.0.1", port_txt);
	port = gt_script_port_open(&p, &err);
	g_assert_no_error(err);
	/* Connexion asynchrone : aucun rappel avant une itération de la boucle */
	g_assert_cmpint(gt_script_port_get_state(port), ==, GT_SCRIPT_PORT_CONNECTING);
	g_assert_true(attendre_etat(port, GT_SCRIPT_PORT_OPEN, 5000));
	pair = accept(ecoute, NULL, NULL);
	g_assert_cmpint(pair, >=, 0);

	g_assert_cmpint(write(pair, "pong", 4), ==, 4);
	g_assert_true(attendre_longueur(gt_script_port_get_queue(port), 4, 5000));
	b = gt_rx_queue_take_all(gt_script_port_get_queue(port));
	verifier_octets(b, "pong", 4);
	g_bytes_unref(b);

	g_assert_cmpint(gt_script_port_try_send(port, (const guint8 *)"ping", 4, &e), ==, 4);
	g_assert_cmpuint(lire(pair, buf, 4, 5000), ==, 4);
	g_assert_cmpmem(buf, 4, "ping", 4);

	gt_script_port_close(port);
	close(pair);
	close(ecoute);
	g_free(port_txt);
}

static void
test_connexion_refusee(void)
{
	gchar *port_txt = g_strdup_printf("%u", port_libre());
	TransportParams p;
	GtScriptPort *port;
	GError *err = NULL;

	params_tcp(&p, TRANSPORT_TCP_CLIENT, "127.0.0.1", port_txt);
	port = gt_script_port_open(&p, &err);
	if(port == NULL)
	{
		/* Refus immédiat du connect non bloquant : erreur d'ouverture */
		g_assert_error(err, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_CONNECT);
		g_clear_error(&err);
	}
	else
	{
		gchar *raison;

		g_assert_true(attendre_etat(port, GT_SCRIPT_PORT_LOST, 5000));
		raison = gt_script_port_dup_reason(port);
		g_assert_nonnull(strstr(raison, "Connection refused"));
		g_assert_false(g_str_has_suffix(raison, "\n"));
		g_assert_true(gt_rx_queue_is_closed(gt_script_port_get_queue(port)));
		g_free(raison);
		gt_script_port_close(port);
	}
	g_free(port_txt);
}

static void
test_serveur(void)
{
	guint16 num = port_libre();
	gchar *port_txt = g_strdup_printf("%u", num);
	TransportParams p;
	GtScriptPort *port;
	GError *err = NULL;
	GBytes *b;
	guint8 buf[8];
	gint e = -1;
	int client;

	params_tcp(&p, TRANSPORT_TCP_SERVER, "127.0.0.1", port_txt);
	port = gt_script_port_open(&p, &err);
	g_assert_no_error(err);
	g_assert_cmpint(gt_script_port_get_state(port), ==, GT_SCRIPT_PORT_LISTENING);
	/* Serveur sans client : rien d'écrit, pas d'erreur */
	g_assert_cmpint(gt_script_port_try_send(port, (const guint8 *)"x", 1, &e), ==, 0);
	g_assert_cmpint(e, ==, 0);

	client = connecter(num);
	g_assert_true(attendre_etat(port, GT_SCRIPT_PORT_CONNECTED, 5000));
	g_assert_cmpint(gt_script_port_try_send(port, (const guint8 *)"hi", 2, &e), ==, 2);
	g_assert_cmpuint(lire(client, buf, 2, 5000), ==, 2);
	g_assert_cmpmem(buf, 2, "hi", 2);
	g_assert_cmpint(write(client, "yo", 2), ==, 2);
	g_assert_true(attendre_longueur(gt_script_port_get_queue(port), 2, 5000));
	b = gt_rx_queue_take_all(gt_script_port_get_queue(port));
	verifier_octets(b, "yo", 2);
	g_bytes_unref(b);

	/* Client parti : on écoute de nouveau, la file reste ouverte pour le client suivant */
	close(client);
	g_assert_true(attendre_etat(port, GT_SCRIPT_PORT_LISTENING, 5000));
	g_assert_false(gt_rx_queue_is_closed(gt_script_port_get_queue(port)));
	g_assert_null(gt_script_port_dup_reason(port));
	gt_script_port_close(port);
	g_free(port_txt);
}

static void
test_eagain_pty_non_lu(void)
{
	int maitre, esclave;
	gchar *chemin;
	TransportParams p;
	GtScriptPort *port;
	GError *err = NULL;
	guint8 bloc[4096];
	gssize n = 0;
	gint e = 0;

	if(openpty(&maitre, &esclave, NULL, NULL, NULL) != 0)
	{
		g_test_skip("openpty indisponible");
		return;
	}
	chemin = g_strdup(ttyname(esclave));
	params_serie(&p, chemin, 9600, 1);
	port = gt_script_port_open(&p, &err);
	g_assert_no_error(err);
	memset(bloc, 'x', sizeof(bloc));
	/* Personne ne lit le maître : le tampon de la pty se remplit, l'écriture non bloquante finit en EAGAIN */
	for(guint i = 0; i < 4096; i++)
	{
		n = gt_script_port_try_send(port, bloc, sizeof(bloc), &e);
		if(n < 0)
			break;
	}
	g_assert_cmpint(n, ==, -1);
	g_assert_true(e == EAGAIN || e == EWOULDBLOCK);
	gt_script_port_close(port);
	close(esclave);
	close(maitre);
	g_free(chemin);
}

/* ---- Décodage hexadécimal, noms --------------------------------------------- */

static void
test_hex_valide(void)
{
	static const struct
	{
		const gchar *texte;
		const gchar *octets;
		gsize taille;
	} cas[] = {
		{ "48656c6C6f", "Hello", 5 },
		{ " 48 65\t6c\r\n6c 6f ", "Hello", 5 },
		{ "00ff7F", "\x00\xff\x7f", 3 },
		{ "", "", 0 },
		{ "  \n", "", 0 },
	};

	for(guint i = 0; i < G_N_ELEMENTS(cas); i++)
	{
		GError *err = NULL;
		GBytes *b = gt_script_hex_decode(cas[i].texte, &err);

		g_assert_no_error(err);
		g_assert_nonnull(b);
		verifier_octets(b, cas[i].octets, cas[i].taille);
		g_bytes_unref(b);
	}
}

static void
test_hex_invalide(void)
{
	static const struct
	{
		const gchar *texte;
		const gchar *message;
	} cas[] = {
		{ "4G", "invalid character at position 1" },
		{ "41 4", "odd number of hex digits" },
		{ "4 1", "invalid character at position 1" },	/* blanc à l'intérieur d'un octet */
		{ "41x2", "invalid character at position 2" },
		{ "0x41", "invalid character at position 1" },
	};

	for(guint i = 0; i < G_N_ELEMENTS(cas); i++)
	{
		GError *err = NULL;

		g_assert_null(gt_script_hex_decode(cas[i].texte, &err));
		g_assert_error(err, G_CONVERT_ERROR, G_CONVERT_ERROR_ILLEGAL_SEQUENCE);
		g_assert_cmpstr(err->message, ==, cas[i].message);
		g_clear_error(&err);
	}
}

static void
test_noms(void)
{
	GtParity parite = GT_PARITY_EVEN;
	GtFlow flux = GT_FLOW_RTSCTS;

	g_assert_true(gt_script_parity_from_name("none", &parite));
	g_assert_cmpint(parite, ==, GT_PARITY_NONE);
	g_assert_true(gt_script_parity_from_name("odd", &parite));
	g_assert_cmpint(parite, ==, GT_PARITY_ODD);
	g_assert_true(gt_script_parity_from_name("even", &parite));
	g_assert_cmpint(parite, ==, GT_PARITY_EVEN);
	g_assert_false(gt_script_parity_from_name("mark", &parite));
	g_assert_false(gt_script_parity_from_name(NULL, &parite));

	g_assert_true(gt_script_flow_from_name("none", &flux));
	g_assert_cmpint(flux, ==, GT_FLOW_NONE);
	g_assert_true(gt_script_flow_from_name("xon", &flux));
	g_assert_cmpint(flux, ==, GT_FLOW_XONXOFF);
	g_assert_true(gt_script_flow_from_name("rts", &flux));
	g_assert_cmpint(flux, ==, GT_FLOW_RTSCTS);
	g_assert_false(gt_script_flow_from_name("rs485", &flux));
	g_assert_false(gt_script_flow_from_name(NULL, &flux));
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/script_port/serie-pty", test_serie_pty);
	g_test_add_func("/script_port/peripherique-absent", test_peripherique_absent);
	g_test_add_func("/script_port/maitre-ferme", test_maitre_ferme);
	g_test_add_func("/script_port/verrou", test_verrou);
	g_test_add_func("/script_port/client-tcp", test_client_tcp);
	g_test_add_func("/script_port/connexion-refusee", test_connexion_refusee);
	g_test_add_func("/script_port/serveur", test_serveur);
	g_test_add_func("/script_port/eagain-pty-non-lu", test_eagain_pty_non_lu);
	g_test_add_func("/script_port/hex-valide", test_hex_valide);
	g_test_add_func("/script_port/hex-invalide", test_hex_invalide);
	g_test_add_func("/script_port/noms", test_noms);
	return g_test_run();
}
```

Dans `tests/meson.build`, dictionnaire `unit_tests`, ajouter après `'test_rx_queue' : [],` :

```meson
	'test_script_port' : [util_dep],
```

- [ ] **Step 2 : vérifier l'échec**

Run : `ninja -C build`
Expected : échec de compilation de `test_script_port.c` (« script_port.h: No such file or directory »).

- [ ] **Step 3 : écrire l'en-tête**

Créer `src/script_port.h` :

```c
/* Port ouvert par un script Lua (sous-projet 5) : un GtTransport et une GtRxQueue, plus un état lisible de n'importe quel thread.
 * Ouverture, fermeture et envoi sur le thread principal (celui de la boucle GLib par défaut, où tournent les rappels du
 * transport) ; état, file et raison lisibles de n'importe quel thread. Événements du transport : CONNECTED → OPEN ;
 * CLIENT_CONNECTED → CONNECTED ; CLIENT_GONE → LISTENING (file ouverte) ; LOST → LOST, raison retenue, file fermée. */
#ifndef SCRIPT_PORT_H_
#define SCRIPT_PORT_H_

#include <glib.h>

#include "rx_queue.h"
#include "transport.h"

/* Taille de la file de réception d'un port Lua et du port 0 du moteur */
#define GT_SCRIPT_RX_MAX (1024 * 1024)

typedef enum
{
	GT_SCRIPT_PORT_CONNECTING,	/* client TCP : connexion en cours */
	GT_SCRIPT_PORT_OPEN,		/* série ouvert, client TCP connecté */
	GT_SCRIPT_PORT_LISTENING,	/* serveur TCP sans client */
	GT_SCRIPT_PORT_CONNECTED,	/* serveur TCP avec un client */
	GT_SCRIPT_PORT_LOST		/* série ou client TCP : liaison perdue ou connexion refusée (pas de reconnexion) */
} GtScriptPortState;

typedef struct _GtScriptPort GtScriptPort;

/* Thread principal. Série : verrou exclusif (lock_port forcé à TRUE), refus si un autre programme ou le port principal le tient. */
GtScriptPort     *gt_script_port_open        (const TransportParams *params, GError **error);
/* Thread principal : ferme le transport (termios restaurés), ferme la file, libère. Tolère NULL. */
void              gt_script_port_close       (GtScriptPort *port);
/* Thread principal : une tentative d'écriture. > 0 octets écrits ; 0 rien d'écrit (connexion en cours, serveur sans client) ;
 * -1 avec *err = errno (EAGAIN : réessayer ; EIO : perdu ; autre : échec). *err vaut 0 si le résultat n'est pas -1. */
gssize            gt_script_port_try_send    (GtScriptPort *port, const guint8 *data, gsize len, gint *err);
/* N'importe quel thread. */
GtScriptPortState gt_script_port_get_state   (GtScriptPort *port);
GtRxQueue        *gt_script_port_get_queue   (GtScriptPort *port);
/* N'importe quel thread : raison de la perte (copie sans fin de ligne, NULL si non perdu). */
gchar            *gt_script_port_dup_reason  (GtScriptPort *port);

/* Aides de l'API Lua, sans Lua. */
gboolean          gt_script_parity_from_name (const gchar *name, GtParity *out);	/* none, odd, even */
gboolean          gt_script_flow_from_name   (const gchar *name, GtFlow *out);		/* none, xon, rts */
/* Paires de chiffres hexadécimaux (casse indifférente), blancs (espace, tabulation, CR, LF) permis entre les octets seulement.
 * Erreur (G_CONVERT_ERROR_ILLEGAL_SEQUENCE ; le cœur n'a pas GIO) : « invalid character at position N » (0-based) ou
 * « odd number of hex digits ». */
GBytes           *gt_script_hex_decode       (const gchar *text, GError **error);

#endif
```

- [ ] **Step 4 : écrire l'implémentation**

Créer `src/script_port.c` :

```c
/***********************************************************************/
/* script_port.c                                                        */
/*           GTKTerm Software                                           */
/*                                                                      */
/*   Purpose                                                            */
/*      Port ouvert par un script Lua : GtTransport + GtRxQueue         */
/*      (cœur sans GTK, sous-projet 5)                                  */
/***********************************************************************/

#include <errno.h>
#include <string.h>

#include "script_port.h"

struct _GtScriptPort
{
	GtTransport *transport;
	GtRxQueue *queue;
	gint state;		/* GtScriptPortState, lu de n'importe quel thread (atomique) */
	GMutex reason_mutex;
	gchar *reason;		/* raison de la perte, sans fin de ligne ; NULL si non perdu */
};

/* Rappels du transport : thread principal (surveillance du descripteur ; LOST depuis une source d'inactivité) */
static void
script_port_on_data(const guint8 *data, gsize len, gpointer user_data)
{
	GtScriptPort *port = user_data;

	gt_rx_queue_push(port->queue, data, len);
}

static void
script_port_on_event(TransportEvent ev, const gchar *detail, gpointer user_data)
{
	GtScriptPort *port = user_data;

	switch(ev)
	{
	case TRANSPORT_EVENT_CONNECTED:
		g_atomic_int_set(&port->state, GT_SCRIPT_PORT_OPEN);
		break;
	case TRANSPORT_EVENT_CLIENT_CONNECTED:
		g_atomic_int_set(&port->state, GT_SCRIPT_PORT_CONNECTED);
		break;
	case TRANSPORT_EVENT_CLIENT_GONE:
		g_atomic_int_set(&port->state, GT_SCRIPT_PORT_LISTENING);
		break;
	case TRANSPORT_EVENT_LOST:
		g_mutex_lock(&port->reason_mutex);
		g_free(port->reason);
		port->reason = g_strchomp(g_strdup(detail != NULL ? detail : "link lost"));
		g_mutex_unlock(&port->reason_mutex);
		/* Raison retenue avant l'état : qui lit LOST trouve la raison */
		g_atomic_int_set(&port->state, GT_SCRIPT_PORT_LOST);
		gt_rx_queue_set_closed(port->queue, TRUE);
		break;
	}
}

static const TransportCallbacks script_port_callbacks = { script_port_on_data, script_port_on_event };

GtScriptPort *
gt_script_port_open(const TransportParams *params, GError **error)
{
	TransportParams p;
	GtScriptPort *port;

	g_return_val_if_fail(params != NULL, NULL);
	port = g_new0(GtScriptPort, 1);
	g_mutex_init(&port->reason_mutex);
	port->queue = gt_rx_queue_new(GT_SCRIPT_RX_MAX);
	switch(params->type)
	{
	case TRANSPORT_TCP_CLIENT:
		port->state = GT_SCRIPT_PORT_CONNECTING;
		break;
	case TRANSPORT_TCP_SERVER:
		port->state = GT_SCRIPT_PORT_LISTENING;
		break;
	default:
		port->state = GT_SCRIPT_PORT_OPEN;
		break;
	}
	/* Copie superficielle : seul lock_port change ; gt_transport_open fait sa propre copie profonde */
	p = *params;
	if(p.type == TRANSPORT_SERIAL)
		p.lock_port = TRUE;
	port->transport = gt_transport_open(&p, &script_port_callbacks, port, error);
	if(port->transport == NULL)
	{
		gt_rx_queue_free(port->queue);
		g_mutex_clear(&port->reason_mutex);
		g_free(port);
		return NULL;
	}
	return port;
}

void
gt_script_port_close(GtScriptPort *port)
{
	if(port == NULL)
		return;
	/* Retire aussi un signalement de perte en attente : plus aucun rappel sur ce port ensuite */
	gt_transport_close(port->transport);
	gt_rx_queue_set_closed(port->queue, TRUE);
	gt_rx_queue_free(port->queue);
	g_free(port->reason);
	g_mutex_clear(&port->reason_mutex);
	g_free(port);
}

gssize
gt_script_port_try_send(GtScriptPort *port, const guint8 *data, gsize len, gint *err)
{
	gssize n;

	*err = 0;
	if(port == NULL)
	{
		*err = EBADF;
		return -1;
	}
	if(len == 0)
		return 0;
	errno = 0;
	n = gt_transport_send(port->transport, data, len);
	if(n < 0)
		*err = errno;	/* relevé tout de suite : errno est propre au thread et le prochain appel système l'écraserait */
	return n;
}

GtScriptPortState
gt_script_port_get_state(GtScriptPort *port)
{
	g_return_val_if_fail(port != NULL, GT_SCRIPT_PORT_LOST);
	return (GtScriptPortState)g_atomic_int_get(&port->state);
}

GtRxQueue *
gt_script_port_get_queue(GtScriptPort *port)
{
	g_return_val_if_fail(port != NULL, NULL);
	return port->queue;
}

gchar *
gt_script_port_dup_reason(GtScriptPort *port)
{
	gchar *r;

	g_return_val_if_fail(port != NULL, NULL);
	g_mutex_lock(&port->reason_mutex);
	r = g_strdup(port->reason);
	g_mutex_unlock(&port->reason_mutex);
	return r;
}

gboolean
gt_script_parity_from_name(const gchar *name, GtParity *out)
{
	if(g_strcmp0(name, "none") == 0)
		*out = GT_PARITY_NONE;
	else if(g_strcmp0(name, "odd") == 0)
		*out = GT_PARITY_ODD;
	else if(g_strcmp0(name, "even") == 0)
		*out = GT_PARITY_EVEN;
	else
		return FALSE;
	return TRUE;
}

gboolean
gt_script_flow_from_name(const gchar *name, GtFlow *out)
{
	if(g_strcmp0(name, "none") == 0)
		*out = GT_FLOW_NONE;
	else if(g_strcmp0(name, "xon") == 0)
		*out = GT_FLOW_XONXOFF;
	else if(g_strcmp0(name, "rts") == 0)
		*out = GT_FLOW_RTSCTS;
	else
		return FALSE;
	return TRUE;
}

GBytes *
gt_script_hex_decode(const gchar *text, GError **error)
{
	GByteArray *out;
	gint high = -1;		/* premier chiffre de l'octet en cours, -1 entre deux octets */

	g_return_val_if_fail(text != NULL, NULL);
	out = g_byte_array_new();
	for(gsize i = 0; text[i] != '\0'; i++)
	{
		gchar c = text[i];
		gint v;

		if(c == ' ' || c == '\t' || c == '\r' || c == '\n')
		{
			if(high < 0)
				continue;
			v = -1;		/* blanc entre les deux chiffres d'un octet : invalide */
		}
		else
			v = g_ascii_xdigit_value(c);
		if(v < 0)
		{
			g_set_error(error, G_CONVERT_ERROR, G_CONVERT_ERROR_ILLEGAL_SEQUENCE, "invalid character at position %zu", i);
			g_byte_array_unref(out);
			return NULL;
		}
		if(high < 0)
			high = v;
		else
		{
			guint8 octet = (guint8)((high << 4) | v);

			g_byte_array_append(out, &octet, 1);
			high = -1;
		}
	}
	if(high >= 0)
	{
		g_set_error_literal(error, G_CONVERT_ERROR, G_CONVERT_ERROR_ILLEGAL_SEQUENCE, "odd number of hex digits");
		g_byte_array_unref(out);
		return NULL;
	}
	return g_byte_array_free_to_bytes(out);
}
```

Dans `src/meson.build`, liste `core_sources`, ajouter après `'rx_queue.h',` :

```meson
	'script_port.c',
	'script_port.h',
```

- [ ] **Step 5 : vérifier le succès**

Run : `ninja -C build && meson test -C build test_script_port test_transport --print-errorlogs`
Expected : `test_script_port` OK (11 sous-tests), `test_transport` toujours OK.

- [ ] **Step 6 : ASan**

Run : `ninja -C build-asan && meson test -C build-asan test_script_port --print-errorlogs`
Expected : OK, aucun rapport.

- [ ] **Step 7 : commit**

```bash
git add src/script_port.c src/script_port.h src/meson.build tests/test_script_port.c tests/meson.build
git commit -m "$(cat <<'EOF'
lua : port de script sur le transport par instance (GtScriptPort)

Un GtTransport et une GtRxQueue par port Lua, état atomique lisible de tout thread,
raison de la perte retenue, verrou série forcé ; décodage hexadécimal strict et noms
de parité et de flux pour l'API Lua.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 3 : moteur, partie A — ports sur `GtScriptPort`, `status`, envois complets, raisons, nettoyage

**Files :**
- Modify : `src/script_engine.c` (réécrit), `tests/stubs/stub_link.c`, `tests/stubs/stubs.h`
- Test : `tests/test_script_engine.c` (étendu)

**Interfaces :**
- Consumes : tâche 1 (`GtRxQueue`), tâche 2 (`GtScriptPort`, `GT_SCRIPT_RX_MAX`, `gt_script_hex_decode`, `gt_script_parity_from_name`, `gt_script_flow_from_name`) ; `link.h` : `gt_link_get_default()`, `gssize gt_link_send(GtkTermLink *, const guint8 *, gsize, GtSendOrigin)` (-1 + `errno`, 0 si rien d'ouvert), `GtLinkState gt_link_get_state(GtkTermLink *)`, signal `received (GBytes *, gint64)` ; `macros.h` / `macro_set.h` / `macros_format.h` (inchangés).
- Produces : API Lua du §3 de la spec (sauf `print`, `os.exit`, interruption par le crochet : tâche 4) ; fonctions statiques réutilisées par la tâche 4 : `raise_stop(lua_State *)`, `pause_interruptible(gint64 us)`, `report_all_overflows(void)`, `end_of_script_cleanup(void)`, `engine_log_line(const char *fmt, ...)`, `run_on_main_sync(GSourceFunc, gpointer)`, `lua_main_port_received`, `main_port_connect_cb`, `main_port_disconnect_cb` ; globales `stop_requested` (`gint`, atomique), `pause_mutex`, `pause_cond`, `main_queue`, `ports[]`, `overflow_reported[]`. Bouchon : `GtLinkState gt_link_get_state(GtkTermLink *)`, `void stub_link_set_state(gint state)`.
- `script_engine.h` inchangé dans cette tâche (`ScriptDoneFunc` sans raison jusqu'à la tâche 4).

- [ ] **Step 1 : bouchon de la liaison**

Dans `tests/stubs/stub_link.c`, après la ligne `static gboolean ecriture_en_echec = FALSE;	/* port fermé ou perdu simulé : rien n'est écrit */`, ajouter :

```c
static GtLinkState etat_lien = GT_LINK_OPEN;	/* état rendu par gt_link_get_state (GT_LINK_OPEN après stub_reset) */
```

Dans `stub_link_reset`, après `ecriture_en_echec = FALSE;`, ajouter :

```c
	etat_lien = GT_LINK_OPEN;
```

Après la fonction `stub_link_set_write_fails`, ajouter :

```c
GtLinkState
gt_link_get_state(GtkTermLink *link)
{
	return etat_lien;
}

/* Simule l'état de la liaison (GtLinkState) vu par gtkterm.status(0) et par l'envoi sur le port 0 */
void
stub_link_set_state(gint state)
{
	etat_lien = (GtLinkState)state;
}
```

Dans `tests/stubs/stubs.h`, après `void         stub_link_set_write_fails(gboolean fails);`, ajouter :

```c
void         stub_link_set_state(gint state);	/* GtLinkState rendu par gt_link_get_state ; GT_LINK_OPEN après stub_reset */
```

- [ ] **Step 2 : écrire les tests de la partie A (échouent à l'exécution)**

Dans `tests/test_script_engine.c` :

1. Remplacer le bloc d'inclusions par :

```c
#include <glib.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <pty.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "script_engine.h"
#include "link.h"
#include "macros_list.h"
#include "stubs.h"
#include "stub_macros.h"
```

2. Juste avant `int\nmain(int argc, char **argv)`, insérer :

```c
/* ---- Sous-projet 5 : aides --------------------------------------------- */

/* Exécute le script ; pilote est appelé toutes les 10 ms pendant l'exécution et renvoie toujours G_SOURCE_CONTINUE. */
static gboolean
executer_avec(const gchar *script, guint delai_ms, GSourceFunc pilote, gpointer data)
{
	guint id = g_timeout_add(10, pilote, data);
	gboolean ok = executer(script, delai_ms);

	g_source_remove(id);
	return ok;
}

static gboolean
journal_contient(const gchar *texte)
{
	return strstr(journal->str, texte) != NULL;
}

/* Nombre réel écrit par le script après « prefixe » dans le journal (format %.3f ou %.6f) */
static gdouble
nombre_apres(const gchar *prefixe)
{
	const gchar *p = strstr(journal->str, prefixe);

	g_assert_nonnull(p);
	return g_ascii_strtod(p + strlen(prefixe), NULL);
}

/* Socket d'écoute sur 127.0.0.1, port choisi par le système, file d'attente « backlog » */
static int
ecoute_locale(guint16 *port, int backlog)
{
	struct sockaddr_in adresse;
	socklen_t taille = sizeof(adresse);
	int fd = socket(AF_INET, SOCK_STREAM, 0);

	g_assert_cmpint(fd, >=, 0);
	memset(&adresse, 0, sizeof(adresse));
	adresse.sin_family = AF_INET;
	adresse.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	g_assert_cmpint(bind(fd, (struct sockaddr *)&adresse, sizeof(adresse)), ==, 0);
	g_assert_cmpint(listen(fd, backlog), ==, 0);
	g_assert_cmpint(getsockname(fd, (struct sockaddr *)&adresse, &taille), ==, 0);
	*port = ntohs(adresse.sin_port);
	return fd;
}

static guint16
port_libre(void)
{
	guint16 port;

	close(ecoute_locale(&port, 1));
	return port;
}

static int
connecter(guint16 port)
{
	struct sockaddr_in adresse;
	int fd = socket(AF_INET, SOCK_STREAM, 0);

	memset(&adresse, 0, sizeof(adresse));
	adresse.sin_family = AF_INET;
	adresse.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	adresse.sin_port = htons(port);
	g_assert_cmpint(connect(fd, (struct sockaddr *)&adresse, sizeof(adresse)), ==, 0);
	return fd;
}

/* Écouteur dont la file d'attente est pleine : une connexion suivante reste « en cours » (SYN ignoré).
 * Renvoie FALSE (cas sauté) si le noyau accepte quand même une deuxième connexion. */
static gboolean
ecouteur_sature(int *ecoute, int *occupant, guint16 *port)
{
	struct sockaddr_in adresse;
	struct pollfd pfd;
	int essai;
	gboolean sature;

	*ecoute = ecoute_locale(port, 0);
	*occupant = connecter(*port);
	essai = socket(AF_INET, SOCK_STREAM, 0);
	fcntl(essai, F_SETFL, O_NONBLOCK);
	memset(&adresse, 0, sizeof(adresse));
	adresse.sin_family = AF_INET;
	adresse.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	adresse.sin_port = htons(*port);
	if(connect(essai, (struct sockaddr *)&adresse, sizeof(adresse)) == 0)
		sature = FALSE;
	else
	{
		pfd.fd = essai;
		pfd.events = POLLOUT;
		pfd.revents = 0;
		sature = (errno == EINPROGRESS && poll(&pfd, 1, 300) == 0);
	}
	close(essai);
	return sature;
}

/* ---- Série sur pty : paramètres et validation ---------------------------- */

typedef struct
{
	int esclave;
	gboolean lu;
	speed_t vitesse;
	tcflag_t cflag;
} ParamsPty;

static gboolean
piloter_parametres(gpointer data)
{
	ParamsPty *p = data;
	struct termios tio;

	if(!p->lu && journal_contient("ouvert\n"))
	{
		g_assert_cmpint(tcgetattr(p->esclave, &tio), ==, 0);
		p->vitesse = cfgetospeed(&tio);
		p->cflag = tio.c_cflag;
		p->lu = TRUE;
		stub_inject_rx("fin", 3);
	}
	return G_SOURCE_CONTINUE;
}

static void
test_serie_parametres(void)
{
	int maitre, esclave;
	ParamsPty p = { 0 };
	gchar *chemin, *script;
	struct termios tio;

	if(openpty(&maitre, &esclave, NULL, NULL, NULL) != 0)
	{
		g_test_skip("openpty indisponible");
		return;
	}
	chemin = g_strdup(ttyname(esclave));
	p.esclave = esclave;
	script = g_strdup_printf("assert(gtkterm.open(1, 'serial', '%s', 115200, 8, 2, 'even', 'none'))\n"
	                         "gtkterm.log('ouvert')\n"
	                         "assert(gtkterm.wait_for(0, 'fin', 10000))\n"
	                         "gtkterm.close(1)\n"
	                         "local ok, err = gtkterm.open(2, 'serial', '%s', 115200, 8, 1, 'mark')\n"
	                         "gtkterm.log('parite ' .. tostring(ok) .. ' ' .. err)\n"
	                         "ok, err = gtkterm.open(2, 'serial', '%s', 115200, 9)\n"
	                         "gtkterm.log('bits ' .. tostring(ok) .. ' ' .. err)\n"
	                         "ok, err = gtkterm.open(2, 'serial', '%s', 0)\n"
	                         "gtkterm.log('debit ' .. tostring(ok) .. ' ' .. err)\n"
	                         "ok, err = gtkterm.open(2, 'serial', '%s', 9600, 8, 3)\n"
	                         "gtkterm.log('stop ' .. tostring(ok) .. ' ' .. err)\n"
	                         "ok, err = gtkterm.open(2, 'serial', '%s', 9600, 8, 1, 0, 5)\n"
	                         "gtkterm.log('flux ' .. tostring(ok) .. ' ' .. err)\n"
	                         "gtkterm.log('etat ' .. gtkterm.status(1) .. ' ' .. gtkterm.status(2))\n",
	                         chemin, chemin, chemin, chemin, chemin, chemin);
	stub_reset();
	g_assert_true(executer_avec(script, 15000, piloter_parametres, &p));
	g_assert_true(p.lu);
	g_assert_cmpuint(p.vitesse, ==, B115200);
	g_assert_cmpuint(p.cflag & CSTOPB, ==, CSTOPB);
	g_assert_true(journal_contient("parite false invalid parity\n"));
	g_assert_true(journal_contient("bits false invalid bits\n"));
	g_assert_true(journal_contient("debit false invalid baud\n"));
	g_assert_true(journal_contient("stop false invalid stop\n"));
	g_assert_true(journal_contient("flux false invalid flow\n"));
	g_assert_true(journal_contient("etat closed closed\n"));
	/* Port refermé par gtkterm.close : réglages d'origine restaurés */
	g_assert_cmpint(tcgetattr(esclave, &tio), ==, 0);
	g_assert_cmpuint(tio.c_cflag & CSTOPB, ==, 0);
	g_free(script);
	g_free(chemin);
	close(esclave);
	close(maitre);
}

/* ---- send : tout ou échec (1 Mio) ------------------------------------------ */

typedef struct
{
	int maitre;
	GByteArray *recu;
	gsize attendu;
} Lecteur;

/* Lit le maître jusqu'à « attendu » octets ou 30 s (poll : jamais bloqué si l'envoi échoue) */
static gpointer
lire_maitre(gpointer data)
{
	Lecteur *l = data;
	guint8 tampon[65536];
	gint64 fin = g_get_monotonic_time() + 30 * G_USEC_PER_SEC;

	while(l->recu->len < l->attendu && g_get_monotonic_time() < fin)
	{
		struct pollfd pfd = { l->maitre, POLLIN, 0 };

		if(poll(&pfd, 1, 50) > 0)
		{
			ssize_t n = read(l->maitre, tampon, sizeof(tampon));

			if(n > 0)
				g_byte_array_append(l->recu, tampon, (guint)n);
			else if(n == 0 || (errno != EAGAIN && errno != EINTR))
				break;
		}
	}
	return NULL;
}

static void
test_send_complet_ou_delai(void)
{
	int ma, ea, mb, eb;
	Lecteur l;
	GThread *th;
	gchar *a, *b, *script;
	gboolean conforme = TRUE;

	if(openpty(&ma, &ea, NULL, NULL, NULL) != 0)
	{
		g_test_skip("openpty indisponible");
		return;
	}
	if(openpty(&mb, &eb, NULL, NULL, NULL) != 0)
	{
		close(ea);
		close(ma);
		g_test_skip("openpty indisponible");
		return;
	}
	a = g_strdup(ttyname(ea));
	b = g_strdup(ttyname(eb));
	l.maitre = mb;
	l.recu = g_byte_array_new();
	l.attendu = 1048576;
	th = g_thread_new("lecteur", lire_maitre, &l);
	/* Port 1 : pty A que personne ne lit (délai) ; port 2 : pty B lue par le thread « lecteur » */
	script = g_strdup_printf("local bloc = {}\n"
	                         "for i = 0, 250 do bloc[#bloc + 1] = string.char(i) end\n"
	                         "local donnees = string.rep(table.concat(bloc), 4178):sub(1, 1048576)\n"
	                         "assert(gtkterm.open(1, 'serial', '%s'))\n"
	                         "local t0 = gtkterm.time()\n"
	                         "local ok, err = gtkterm.send(1, donnees, 300)\n"
	                         "gtkterm.log(string.format('sans lecteur %%s %%s %%.3f', tostring(ok), tostring(err), gtkterm.time() - t0))\n"
	                         "gtkterm.close(1)\n"
	                         "assert(gtkterm.open(2, 'serial', '%s'))\n"
	                         "ok, err = gtkterm.send(2, donnees, 20000)\n"
	                         "gtkterm.log('avec lecteur ' .. tostring(ok) .. ' ' .. tostring(err))\n", a, b);
	stub_reset();
	g_assert_true(executer(script, 40000));
	g_thread_join(th);
	g_assert_true(journal_contient("sans lecteur false timeout "));
	g_assert_cmpfloat(nombre_apres("sans lecteur false timeout "), >=, 0.29);
	g_assert_true(journal_contient("avec lecteur true nil\n"));
	g_assert_cmpuint(l.recu->len, ==, 1048576);
	for(guint i = 0; i < l.recu->len && conforme; i++)
		conforme = (l.recu->data[i] == i % 251);
	g_assert_true(conforme);
	g_byte_array_unref(l.recu);
	g_free(script);
	g_free(a);
	g_free(b);
	close(ea);
	close(ma);
	close(eb);
	close(mb);
}

/* ---- send_hex : texte invalide, rien d'envoyé ------------------------------ */

static void
test_send_hex(void)
{
	GByteArray *octets;

	stub_reset();
	g_assert_true(executer("local ok, err = gtkterm.send_hex(0, '41 4G')\n"
	                       "gtkterm.log('invalide ' .. tostring(ok) .. ' ' .. err)\n"
	                       "ok, err = gtkterm.send_hex(0, '414')\n"
	                       "gtkterm.log('impair ' .. tostring(ok) .. ' ' .. err)\n", 5000));
	g_assert_true(journal_contient("invalide false invalid hex: invalid character at position 4\n"));
	g_assert_true(journal_contient("impair false invalid hex: odd number of hex digits\n"));
	g_assert_cmpuint(stub_sent()->len, ==, 0);
	g_assert_true(executer("assert(gtkterm.send_hex(0, '41 42\\r\\n\\t4a'))\n", 5000));
	octets = stub_sent();
	g_assert_cmpmem(octets->data, octets->len, "ABJ", 3);
}

/* ---- Port perdu : wait_for rend « closed » tout de suite --------------------- */

typedef struct
{
	int maitre;
	gint etape;
	gint64 fermeture_us;
} Perte;

static gboolean
piloter_perte(gpointer data)
{
	Perte *p = data;

	if(p->etape == 0 && journal_contient("ouvert\n"))
	{
		g_assert_cmpint(write(p->maitre, "abcXYZ", 6), ==, 6);
		p->etape = 1;
	}
	else if(p->etape == 1 && journal_contient("pret\n"))
	{
		p->fermeture_us = g_get_monotonic_time();
		close(p->maitre);
		p->maitre = -1;
		p->etape = 2;
	}
	return G_SOURCE_CONTINUE;
}

static void
test_port_perdu(void)
{
	int maitre, esclave;
	Perte p = { -1, 0, 0 };
	gchar *chemin, *script;
	gdouble retour;

	if(openpty(&maitre, &esclave, NULL, NULL, NULL) != 0)
	{
		g_test_skip("openpty indisponible");
		return;
	}
	chemin = g_strdup(ttyname(esclave));
	p.maitre = maitre;
	script = g_strdup_printf("assert(gtkterm.open(1, 'serial', '%s'))\n"
	                         "gtkterm.log('ouvert')\n"
	                         "assert(gtkterm.wait_for(1, 'abc', 10000))\n"
	                         "gtkterm.log('pret')\n"
	                         "local r, err = gtkterm.wait_for(1, 'jamais', 10000)\n"
	                         "gtkterm.log(string.format('fin %%s %%s %%.6f', tostring(r), tostring(err), gtkterm.time()))\n"
	                         "gtkterm.log('reste ' .. gtkterm.read(1))\n"
	                         "gtkterm.log('etat ' .. gtkterm.status(1))\n"
	                         "local ok, e2 = gtkterm.send(1, 'x')\n"
	                         "gtkterm.log('envoi ' .. tostring(ok) .. ' ' .. tostring(e2))\n", chemin);
	stub_reset();
	g_assert_true(executer_avec(script, 20000, piloter_perte, &p));
	g_assert_cmpint(p.etape, ==, 2);
	/* Même horloge (monotone) des deux côtés : moins de 500 ms entre la fermeture du maître et le retour de wait_for */
	retour = nombre_apres("fin nil closed ");
	g_assert_cmpfloat(retour * G_USEC_PER_SEC - (gdouble)p.fermeture_us, <, 500000.0);
	g_assert_true(journal_contient("reste XYZ\n"));
	g_assert_true(journal_contient("etat lost\n"));
	g_assert_true(journal_contient("envoi false link lost\n"));
	g_free(script);
	g_free(chemin);
	close(esclave);
}

/* ---- Serveur TCP : listening → connected → listening ------------------------- */

typedef struct
{
	guint16 port;
	int client;
	gint etape;
	GString *recu;
} Serveur;

static gboolean
piloter_serveur(gpointer data)
{
	Serveur *s = data;
	guint8 tampon[64];
	ssize_t n;

	if(s->etape == 0 && journal_contient("ecoute listening\n"))
	{
		s->client = connecter(s->port);
		fcntl(s->client, F_SETFL, O_NONBLOCK);
		s->etape = 1;
	}
	else if(s->etape == 1 && journal_contient("client connected\n"))
	{
		/* Lecture non bloquante : l'envoi du script a besoin de ce thread (run_on_main_sync) */
		n = read(s->client, tampon, sizeof(tampon));
		if(n > 0)
			g_string_append_len(s->recu, (const gchar *)tampon, n);
		if(strstr(s->recu->str, "bonjour") != NULL)
		{
			g_assert_cmpint(write(s->client, "merci", 5), ==, 5);
			close(s->client);
			s->client = -1;
			s->etape = 2;
		}
	}
	return G_SOURCE_CONTINUE;
}

static void
test_serveur_tcp_status(void)
{
	Serveur s = { port_libre(), -1, 0, g_string_new(NULL) };
	gchar *script = g_strdup_printf("assert(gtkterm.open(1, 'tcp_server', %u, '127.0.0.1'))\n"
	                                "gtkterm.log('ecoute ' .. gtkterm.status(1))\n"
	                                "local t0 = gtkterm.time()\n"
	                                "while gtkterm.status(1) ~= 'connected' and gtkterm.time() - t0 < 10 do gtkterm.sleep(10) end\n"
	                                "gtkterm.log('client ' .. gtkterm.status(1))\n"
	                                "assert(gtkterm.send(1, 'bonjour'))\n"
	                                "gtkterm.log('recu ' .. tostring(gtkterm.wait_for(1, 'merci', 10000)))\n"
	                                "t0 = gtkterm.time()\n"
	                                "while gtkterm.status(1) ~= 'listening' and gtkterm.time() - t0 < 10 do gtkterm.sleep(10) end\n"
	                                "gtkterm.log('parti ' .. gtkterm.status(1))\n", s.port);

	stub_reset();
	g_assert_true(executer_avec(script, 30000, piloter_serveur, &s));
	g_assert_true(journal_contient("ecoute listening\n"));
	g_assert_true(journal_contient("client connected\n"));
	g_assert_true(journal_contient("recu merci\n"));
	g_assert_true(journal_contient("parti listening\n"));
	if(s.client >= 0)
		close(s.client);
	g_string_free(s.recu, TRUE);
	g_free(script);
}

/* ---- Client TCP : délai de connexion ------------------------------------------ */

static void
test_client_tcp_delai(void)
{
	int ecoute, occupant;
	guint16 port;
	gchar *script;

	if(!ecouteur_sature(&ecoute, &occupant, &port))
	{
		close(occupant);
		close(ecoute);
		g_test_skip("file d'attente TCP non saturable ici");
		return;
	}
	script = g_strdup_printf("local t0 = gtkterm.time()\n"
	                         "local ok, err = gtkterm.open(1, 'tcp_client', '127.0.0.1', %u, 300)\n"
	                         "gtkterm.log(string.format('delai %%s %%s %%.3f %%s', tostring(ok), tostring(err), gtkterm.time() - t0, gtkterm.status(1)))\n",
	                         port);
	stub_reset();
	g_assert_true(executer(script, 15000));
	g_assert_true(journal_contient("delai false timeout "));
	g_assert_cmpfloat(nombre_apres("delai false timeout "), >=, 0.29);
	g_assert_true(journal_contient(" closed\n"));
	g_free(script);
	close(occupant);
	close(ecoute);
}

/* ---- Port 0 : liaison en échec, états ------------------------------------------ */

static void
test_liaison_etats_et_echec(void)
{
	static const gchar *script = "local ok, err = gtkterm.send(0, 'x')\n"
	                             "gtkterm.log('envoi ' .. tostring(ok) .. ' ' .. tostring(err) .. ' ' .. gtkterm.status(0))\n";

	stub_reset();
	stub_link_set_write_fails(TRUE);
	g_assert_true(executer(script, 5000));
	g_assert_true(journal_contient("envoi false not connected open\n"));
	stub_link_set_state(GT_LINK_RECONNECTING);
	g_assert_true(executer(script, 5000));
	g_assert_true(journal_contient("envoi false link lost lost\n"));
	stub_link_set_state(GT_LINK_LISTENING);
	g_assert_true(executer("gtkterm.log('etat ' .. gtkterm.status(0))\n", 5000));
	g_assert_true(journal_contient("etat listening\n"));
	stub_link_set_state(GT_LINK_CONNECTING);
	g_assert_true(executer("gtkterm.log('etat ' .. gtkterm.status(0))\n", 5000));
	g_assert_true(journal_contient("etat connecting\n"));
	stub_link_set_state(GT_LINK_CLOSED);
	g_assert_true(executer("gtkterm.log('etat ' .. gtkterm.status(0))\n", 5000));
	g_assert_true(journal_contient("etat closed\n"));
	g_assert_cmpuint(stub_sent()->len, ==, 0);
	stub_reset();
}

/* ---- Arguments hors domaine ---------------------------------------------------- */

static void
test_arguments_extremes(void)
{
	stub_reset();
	g_assert_true(executer("local ok, err = gtkterm.open(0, 'tcp_server', 0)\n"
	                       "gtkterm.log('a ' .. tostring(ok) .. ' ' .. err)\n"
	                       "ok, err = gtkterm.open(4294967297, 'tcp_server', 0)\n"
	                       "gtkterm.log('b ' .. tostring(ok) .. ' ' .. err)\n"
	                       "ok, err = gtkterm.send(16, 'x')\n"
	                       "gtkterm.log('c ' .. tostring(ok) .. ' ' .. err)\n"
	                       "ok, err = gtkterm.send(3, 'x')\n"
	                       "gtkterm.log('d ' .. tostring(ok) .. ' ' .. err)\n"
	                       "local r, e = gtkterm.wait_for(4294967296, 'x', 10)\n"
	                       "gtkterm.log('e ' .. tostring(r) .. ' ' .. e)\n"
	                       "r, e = gtkterm.wait_for(3, 'x', 10)\n"
	                       "gtkterm.log('f ' .. tostring(r) .. ' ' .. e)\n"
	                       "gtkterm.log('g ' .. gtkterm.status(16) .. ' ' .. gtkterm.status(3) .. ' ' .. gtkterm.status(-1))\n"
	                       "local t0 = gtkterm.time()\n"
	                       "gtkterm.sleep(-5)\n"
	                       "r, e = gtkterm.wait_for(0, 'x', -1)\n"
	                       "gtkterm.log(string.format('h %s %s %.3f', tostring(r), e, gtkterm.time() - t0))\n"
	                       "gtkterm.close(99)\n"
	                       "gtkterm.close(-1)\n"
	                       "gtkterm.close(3)\n"
	                       "ok, err = gtkterm.open(1, 'tcp_server', 70000)\n"
	                       "gtkterm.log('i ' .. tostring(ok) .. ' ' .. err)\n"
	                       "ok, err = gtkterm.open(1, 'udp', 1)\n"
	                       "gtkterm.log('j ' .. tostring(ok) .. ' ' .. err)\n"
	                       "gtkterm.log('k ' .. gtkterm.read(3) .. '|' .. gtkterm.read(99) .. '|')\n"
	                       "assert(gtkterm.open(1, 'tcp_server', 0))\n"
	                       "ok, err = gtkterm.open(1, 'tcp_server', 0)\n"
	                       "gtkterm.log('l ' .. tostring(ok) .. ' ' .. err)\n", 10000));
	g_assert_true(journal_contient("a false invalid handle\n"));
	g_assert_true(journal_contient("b false invalid handle\n"));
	g_assert_true(journal_contient("c false invalid handle\n"));
	g_assert_true(journal_contient("d false not open\n"));
	g_assert_true(journal_contient("e nil invalid handle\n"));
	g_assert_true(journal_contient("f nil closed\n"));
	g_assert_true(journal_contient("g closed closed closed\n"));
	g_assert_true(journal_contient("h nil timeout "));
	g_assert_cmpfloat(nombre_apres("h nil timeout "), <, 1.0);
	g_assert_true(journal_contient("i false invalid port\n"));
	g_assert_true(journal_contient("j false unknown type (serial/tcp_client/tcp_server)\n"));
	g_assert_true(journal_contient("k ||\n"));
	g_assert_true(journal_contient("l false already open\n"));
}

/* ---- Débordement de la file du port 0 : signalé une fois par exécution ---------- */

typedef struct
{
	gint etape;
	gchar *gros;
	guint taille;
} Debordement;

static gboolean
piloter_debordement(gpointer data)
{
	Debordement *d = data;

	if(d->etape == 0 && journal_contient("pret1\n"))
	{
		stub_inject_rx(d->gros, d->taille);
		stub_inject_rx("FIN1", 4);
		d->etape = 1;
	}
	else if(d->etape == 1 && journal_contient("pret2\n"))
	{
		stub_inject_rx(d->gros, d->taille);
		stub_inject_rx("FIN2", 4);
		d->etape = 2;
	}
	return G_SOURCE_CONTINUE;
}

static void
test_debordement_signale_une_fois(void)
{
	Debordement d = { 0, NULL, 1536 * 1024 };
	const gchar *message = "Port 0: receive buffer full, oldest data dropped\n";
	const gchar *p;
	guint n = 0;

	d.gros = g_malloc(d.taille);
	memset(d.gros, 'a', d.taille);
	stub_reset();
	g_assert_true(executer_avec("gtkterm.log('pret1')\n"
	                            "gtkterm.log('taille1 ' .. #gtkterm.wait_for(0, 'FIN1', 10000))\n"
	                            "gtkterm.log('pret2')\n"
	                            "gtkterm.log('taille2 ' .. #gtkterm.wait_for(0, 'FIN2', 10000))\n", 20000, piloter_debordement, &d));
	/* Les plus anciens octets sont jetés : il reste exactement 1 Mio, motif compris */
	g_assert_true(journal_contient("taille1 1048576\n"));
	g_assert_true(journal_contient("taille2 1048576\n"));
	for(p = strstr(journal->str, message); p != NULL; p = strstr(p + 1, message))
		n++;
	g_assert_cmpuint(n, ==, 1);
	g_free(d.gros);
}
```

3. Dans `main`, après `g_test_add_func("/script_engine/exemple-10-mise-a-jour", test_exemple_10_mise_a_jour);`, ajouter :

```c
	g_test_add_func("/script_engine/serie-parametres", test_serie_parametres);
	g_test_add_func("/script_engine/send-complet-ou-delai", test_send_complet_ou_delai);
	g_test_add_func("/script_engine/send-hex", test_send_hex);
	g_test_add_func("/script_engine/port-perdu", test_port_perdu);
	g_test_add_func("/script_engine/serveur-tcp-status", test_serveur_tcp_status);
	g_test_add_func("/script_engine/client-tcp-delai", test_client_tcp_delai);
	g_test_add_func("/script_engine/liaison-etats-et-echec", test_liaison_etats_et_echec);
	g_test_add_func("/script_engine/arguments-extremes", test_arguments_extremes);
	g_test_add_func("/script_engine/debordement-signale-une-fois", test_debordement_signale_une_fois);
```

- [ ] **Step 3 : vérifier l'échec**

Run : `ninja -C build && meson test -C build test_script_engine --print-errorlogs`
Expected : la compilation passe (le bouchon fournit `gt_link_get_state`), `test_script_engine` ÉCHOUE sur les nouveaux cas (par exemple `/script_engine/serie-parametres` : « attempt to call a nil value » ou débit 9600, `status` absent).

- [ ] **Step 4 : réécrire `src/script_engine.c` (partie A)**

Remplacer tout le fichier par le contenu ci-dessous (style K&R 4 espaces du fichier). Les sections « Macros et listes » reprennent mot pour mot le code actuel (`list_snapshot_free` … `l_send_macro`).

```c
/***********************************************************************/
/* script_engine.c                                                     */
/*           GTKTerm Software                                          */
/*                                                                     */
/*   Purpose                                                           */
/*      Lua 5.4 scripting engine for GTKTerm                           */
/*      Ports Lua sur GtScriptPort / GtTransport (sous-projet 5),      */
/*      motifs, macros, journal CSV.                                   */
/***********************************************************************/

#include <config.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <locale.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "script_engine.h"
#include "script_port.h"
#include "rx_queue.h"
#include "link.h"
#include "macros.h"
#include "macros_format.h"
#include "macros_list.h"

#define MAX_LUA_PORTS      16
#define DEFAULT_TIMEOUT_MS 5000
#define MAX_TIMEOUT_MS     (24LL * 3600 * 1000)            /* délais bornés à 24 h : ms * 1000 ne déborde jamais */
#define SEND_RETRY_US      (2 * G_TIME_SPAN_MILLISECOND)   /* pause entre deux essais d'envoi sans progrès */
#define CONNECT_POLL_US    (5 * G_TIME_SPAN_MILLISECOND)   /* relevé de l'état pendant l'attente de connexion */
#define STOP_POLL_US       (20 * G_TIME_SPAN_MILLISECOND)  /* l'arrêt est vu au moins toutes les 20 ms */

/* Règle : le thread Lua ne touche ni GTK, ni un transport, ni la liaison. Il n'accède qu'aux files (GtRxQueue, verrouillées par
 * rx_queue.c lui-même) et à l'état atomique des ports, et fait exécuter tout le reste sur le thread principal par
 * run_on_main_sync. Il ne tient jamais le verrou d'une file pendant un run_on_main_sync : le thread principal pousse dans ces
 * files (interblocage sinon). */

/* Table des ports Lua : lue et écrite par le thread Lua seulement ; indice 0 inutilisé (le port 0 est la liaison). */
static GtScriptPort *ports[MAX_LUA_PORTS];
/* Débordement déjà signalé pendant l'exécution en cours, par port (thread Lua ; remis à zéro au lancement). */
static gboolean      overflow_reported[MAX_LUA_PORTS];
/* Port 0 : réception de la liaison, alimentée par le signal « received » sur le thread principal. */
static GtRxQueue    *main_queue = NULL;
static GThread      *lua_thread = NULL;
static GMutex        engine_mutex;
static gint          stop_requested = 0;   /* atomique : « Stop » */
/* Pauses interruptibles du thread Lua (sleep, attente de connexion, reprises d'envoi), réveillées par script_engine_stop. */
static GMutex        pause_mutex;
static GCond         pause_cond;
/* Objet d'erreur de l'arrêt : une adresse propre au moteur, poussée comme valeur légère. */
static const char    stop_sentinel = 0;

static ScriptLogFunc  log_func  = NULL;
static gpointer       log_data  = NULL;
static ScriptDoneFunc done_func = NULL;
static gpointer       done_data = NULL;

/* ------------------------------------------------------------------ */
/* Journal                                                            */
/* ------------------------------------------------------------------ */

typedef struct { char *text; } LogIdleData;

static gboolean log_idle_cb(gpointer user_data)
{
    LogIdleData *d = user_data;
    if (log_func)
        log_func(d->text, log_data);
    g_free(d->text);
    g_free(d);
    return G_SOURCE_REMOVE;
}

static void engine_log(const char *fmt, ...) G_GNUC_PRINTF(1, 2);
static void engine_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *text = g_strdup_vprintf(fmt, ap);
    va_end(ap);

    LogIdleData *d = g_new(LogIdleData, 1);
    d->text = text;
    g_idle_add_full(G_PRIORITY_DEFAULT, log_idle_cb, d, NULL);
}

/* Une ligne du moteur (avertissement, erreur) : texte formaté suivi d'une fin de ligne. */
static void engine_log_line(const char *fmt, ...) G_GNUC_PRINTF(1, 2);
static void engine_log_line(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *text = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    engine_log("%s\n", text);
    g_free(text);
}

/* ------------------------------------------------------------------ */
/* Arrêt et pauses interruptibles                                     */
/* ------------------------------------------------------------------ */

/* Lève l'erreur d'arrêt (valeur légère propre au moteur). Ne rend jamais la main : aucune allocation C ne doit être en cours. */
static int raise_stop(lua_State *L)
{
    lua_pushlightuserdata(L, (void *)&stop_sentinel);
    return lua_error(L);
}

/* Attend au plus us microsecondes ; FALSE si l'arrêt est demandé (avant ou pendant l'attente). script_engine_stop réveille
 * l'attente ; la relecture toutes les 20 ms n'est qu'une sécurité. */
static gboolean pause_interruptible(gint64 us)
{
    gint64 deadline = g_get_monotonic_time() + MAX(us, 0);
    gboolean stopped;

    g_mutex_lock(&pause_mutex);
    while (!(stopped = g_atomic_int_get(&stop_requested)) && g_get_monotonic_time() < deadline)
        g_cond_wait_until(&pause_cond, &pause_mutex, MIN(deadline, g_get_monotonic_time() + STOP_POLL_US));
    g_mutex_unlock(&pause_mutex);
    return !stopped;
}

/* ------------------------------------------------------------------ */
/* Exécution synchrone sur le thread principal                        */
/* ------------------------------------------------------------------ */

/* Le thread Lua ne touche jamais GTK, un transport ni l'état du thread principal (macros, listes, liaison) : il y fait exécuter
 * func et attend sa fin (C10, C11). La source est ajoutée au contexte par défaut, que seul le thread principal fait tourner. */
typedef struct {
    GSourceFunc func;
    gpointer    data;
    gboolean    done;
    GMutex      mutex;
    GCond       cond;
} MainCall;

static gboolean main_call_trampoline(gpointer user_data)
{
    MainCall *mc = user_data;
    mc->func(mc->data);
    g_mutex_lock(&mc->mutex);
    mc->done = TRUE;
    g_cond_signal(&mc->cond);
    g_mutex_unlock(&mc->mutex);
    return G_SOURCE_REMOVE;
}

static void run_on_main_sync(GSourceFunc func, gpointer data)
{
    MainCall mc;
    mc.func = func;
    mc.data = data;
    mc.done = FALSE;
    g_mutex_init(&mc.mutex);
    g_cond_init(&mc.cond);
    g_idle_add_full(G_PRIORITY_DEFAULT, main_call_trampoline, &mc, NULL);
    g_mutex_lock(&mc.mutex);
    while (!mc.done)
        g_cond_wait(&mc.cond, &mc.mutex);
    g_mutex_unlock(&mc.mutex);
    g_mutex_clear(&mc.mutex);
    g_cond_clear(&mc.cond);
}

/* ------------------------------------------------------------------ */
/* Port 0 : réception et état de la liaison                           */
/* ------------------------------------------------------------------ */

/* Octets reçus sur le port principal (signal « received » de la liaison, thread principal) */
static void lua_main_port_received(GtkTermLink *link, GBytes *bytes, gint64 wall_time_us, gpointer user_data)
{
    gsize size;
    const guint8 *data = g_bytes_get_data(bytes, &size);
    gt_rx_queue_push(main_queue, data, size);
}

static gulong main_port_handler = 0;

/* Connexion et déconnexion du gestionnaire sur le thread principal (run_on_main_sync) : l'objet GLib n'est jamais
   manipulé depuis le thread Lua (remarque reportée du sous-projet 1). */
static gboolean main_port_connect_cb(gpointer data)
{
    GtkTermLink *link = gt_link_get_default();

    if (link != NULL && main_port_handler == 0)
        main_port_handler = g_signal_connect(link, "received", G_CALLBACK(lua_main_port_received), NULL);
    return G_SOURCE_REMOVE;
}

static gboolean main_port_disconnect_cb(gpointer data)
{
    GtkTermLink *link = gt_link_get_default();

    if (link != NULL && main_port_handler != 0)
        g_signal_handler_disconnect(link, main_port_handler);
    main_port_handler = 0;
    return G_SOURCE_REMOVE;
}

static gboolean main_link_state_cb(gpointer user_data)
{
    *(GtLinkState *)user_data = gt_link_get_state(gt_link_get_default());
    return G_SOURCE_REMOVE;
}

static gboolean link_state_lost(GtLinkState s)
{
    return s == GT_LINK_LOST || s == GT_LINK_RECONNECTING;
}

static const char *link_state_name(GtLinkState s)
{
    switch (s) {
    case GT_LINK_OPEN:         return "open";
    case GT_LINK_CONNECTING:   return "connecting";
    case GT_LINK_LISTENING:    return "listening";
    case GT_LINK_LOST:
    case GT_LINK_RECONNECTING: return "lost";
    default:                   return "closed";
    }
}

/* ------------------------------------------------------------------ */
/* Files de réception et débordements                                 */
/* ------------------------------------------------------------------ */

/* File de réception du port h (0 : liaison) ; NULL si h est hors plage ou si le port Lua est fermé. Thread Lua. */
static GtRxQueue *queue_for(int handle)
{
    if (handle == 0)
        return main_queue;
    if (handle > 0 && handle < MAX_LUA_PORTS && ports[handle] != NULL)
        return gt_script_port_get_queue(ports[handle]);
    return NULL;
}

/* Signale une fois par port et par exécution que sa file a débordé (thread Lua ; le cœur ne journalise pas). */
static void report_overflow(int handle, GtRxQueue *q)
{
    if (q == NULL || gt_rx_queue_take_dropped(q) == 0 || overflow_reported[handle])
        return;
    overflow_reported[handle] = TRUE;
    engine_log_line(_("Port %d: receive buffer full, oldest data dropped"), handle);
}

static void report_all_overflows(void)
{
    for (int h = 0; h < MAX_LUA_PORTS; h++)
        report_overflow(h, queue_for(h));
}

/* ------------------------------------------------------------------ */
/* Arguments                                                          */
/* ------------------------------------------------------------------ */

/* Numéro de port à la position idx ; -1 hors 0..15 (un grand entier n'est jamais tronqué en int dans la plage). */
static int handle_arg(lua_State *L, int idx)
{
    lua_Integer h = luaL_checkinteger(L, idx);
    return (h >= 0 && h < MAX_LUA_PORTS) ? (int)h : -1;
}

/* Délai facultatif en ms à la position idx (défaut 5000), borné à [0, 24 h], rendu en µs. */
static gint64 opt_timeout_us(lua_State *L, int idx)
{
    lua_Integer ms = luaL_optinteger(L, idx, DEFAULT_TIMEOUT_MS);
    return (gint64)CLAMP(ms, 0, MAX_TIMEOUT_MS) * 1000;
}

/* Parité facultative : 0/1/2 ou "none"/"odd"/"even" ; FALSE hors domaine. */
static gboolean opt_parity(lua_State *L, int idx, GtParity *out)
{
    if (lua_isnoneornil(L, idx)) {
        *out = GT_PARITY_NONE;
        return TRUE;
    }
    if (lua_type(L, idx) == LUA_TSTRING)
        return gt_script_parity_from_name(lua_tostring(L, idx), out);
    lua_Integer v = luaL_checkinteger(L, idx);
    if (v < 0 || v > 2)
        return FALSE;
    *out = (GtParity)v;
    return TRUE;
}

/* Contrôle de flux facultatif : 0/1/2 ou "none"/"xon"/"rts" ; FALSE hors domaine. */
static gboolean opt_flow(lua_State *L, int idx, GtFlow *out)
{
    if (lua_isnoneornil(L, idx)) {
        *out = GT_FLOW_NONE;
        return TRUE;
    }
    if (lua_type(L, idx) == LUA_TSTRING)
        return gt_script_flow_from_name(lua_tostring(L, idx), out);
    lua_Integer v = luaL_checkinteger(L, idx);
    if (v < 0 || v > 2)
        return FALSE;
    *out = (GtFlow)v;
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Ports Lua : ouverture, fermeture, état                             */
/* ------------------------------------------------------------------ */

typedef struct {
    const TransportParams *params;
    GtScriptPort          *port;
    gchar                 *error;   /* message du transport sans fin de ligne, à libérer */
} MainOpenData;

static gboolean main_open_cb(gpointer user_data)
{
    MainOpenData *d = user_data;
    GError *err = NULL;

    d->port = gt_script_port_open(d->params, &err);
    if (d->port == NULL) {
        d->error = g_strchomp(g_strdup(err != NULL ? err->message : "open failed"));
        g_clear_error(&err);
    }
    return G_SOURCE_REMOVE;
}

static gboolean main_close_cb(gpointer user_data)
{
    gt_script_port_close(user_data);
    return G_SOURCE_REMOVE;
}

/* Ferme le port h (thread Lua) : transport fermé sur le thread principal, entrée de la table remise à NULL par ce thread seul. */
static void lua_port_close(int handle)
{
    if (handle < 1 || handle >= MAX_LUA_PORTS || ports[handle] == NULL)
        return;
    report_overflow(handle, gt_script_port_get_queue(ports[handle]));
    run_on_main_sync(main_close_cb, ports[handle]);
    ports[handle] = NULL;
}

static int open_result(lua_State *L, const char *error)
{
    if (error == NULL) {
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushboolean(L, 0);
    lua_pushstring(L, error);
    return 2;
}

/* Client TCP : attend OPEN au plus timeout_us. Perte (refus) ou délai : port refermé, false + raison. Arrêt : le port reste dans
 * la table et sera refermé par le nettoyage de fin de script. */
static int wait_connected(lua_State *L, int handle, gint64 timeout_us)
{
    GtScriptPort *port = ports[handle];
    gint64 deadline = g_get_monotonic_time() + timeout_us;

    for (;;) {
        GtScriptPortState st = gt_script_port_get_state(port);
        gint64 now;

        if (st == GT_SCRIPT_PORT_OPEN)
            return open_result(L, NULL);
        if (st == GT_SCRIPT_PORT_LOST) {
            gchar *reason = gt_script_port_dup_reason(port);
            lua_port_close(handle);
            lua_pushboolean(L, 0);
            lua_pushstring(L, reason != NULL ? reason : "link lost");
            g_free(reason);
            return 2;
        }
        now = g_get_monotonic_time();
        if (now >= deadline) {
            lua_port_close(handle);
            return open_result(L, "timeout");
        }
        if (!pause_interruptible(MIN(CONNECT_POLL_US, deadline - now)))
            return raise_stop(L);
    }
}

static int l_open(lua_State *L)
{
    int handle = handle_arg(L, 1);
    TransportParams params;
    gint64 connect_timeout_us = 0;
    char port_str[16];

    if (handle < 1)
        return open_result(L, "invalid handle");
    if (ports[handle] != NULL)
        return open_result(L, "already open");

    const char *type = luaL_checkstring(L, 2);
    memset(&params, 0, sizeof(params));
    /* Chaînes empruntées à la pile Lua (valides pendant l'appel) : gt_transport_open en fait sa copie */
    if (strcmp(type, "serial") == 0) {
        const char *device = luaL_checkstring(L, 3);
        lua_Integer baud = luaL_optinteger(L, 4, 9600);
        lua_Integer bits = luaL_optinteger(L, 5, 8);
        lua_Integer stop = luaL_optinteger(L, 6, 1);
        GtParity parity;
        GtFlow flow;

        if (baud < 1 || baud > G_MAXUINT)
            return open_result(L, "invalid baud");
        if (bits < 5 || bits > 8)
            return open_result(L, "invalid bits");
        if (stop < 1 || stop > 2)
            return open_result(L, "invalid stop");
        if (!opt_parity(L, 7, &parity))
            return open_result(L, "invalid parity");
        if (!opt_flow(L, 8, &flow))
            return open_result(L, "invalid flow");
        params.type = TRANSPORT_SERIAL;
        params.port = (gchar *)device;
        params.baudrate = (guint)baud;
        params.bits = (gint)bits;
        params.stopbits = (gint)stop;
        params.parity = parity;
        params.flow = flow;
    } else if (strcmp(type, "tcp_client") == 0) {
        const char *host = luaL_checkstring(L, 3);
        lua_Integer port = luaL_checkinteger(L, 4);

        connect_timeout_us = opt_timeout_us(L, 5);
        if (port < 1 || port > 65535)
            return open_result(L, "invalid port");
        g_snprintf(port_str, sizeof(port_str), "%d", (int)port);
        params.type = TRANSPORT_TCP_CLIENT;
        params.host = (gchar *)host;
        params.tcp_port = port_str;
    } else if (strcmp(type, "tcp_server") == 0) {
        lua_Integer port = luaL_checkinteger(L, 3);
        const char *host = luaL_optstring(L, 4, NULL);

        if (port < 0 || port > 65535)
            return open_result(L, "invalid port");
        g_snprintf(port_str, sizeof(port_str), "%d", (int)port);
        params.type = TRANSPORT_TCP_SERVER;
        params.host = (gchar *)host;
        params.tcp_port = port_str;
    } else {
        return open_result(L, "unknown type (serial/tcp_client/tcp_server)");
    }

    MainOpenData d = { &params, NULL, NULL };
    run_on_main_sync(main_open_cb, &d);
    if (d.port == NULL) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, d.error);
        g_free(d.error);
        return 2;
    }
    /* Dans la table dès maintenant : un arrêt pendant l'attente de connexion laisse le port au nettoyage de fin de script */
    ports[handle] = d.port;
    if (params.type == TRANSPORT_TCP_CLIENT)
        return wait_connected(L, handle, connect_timeout_us);
    return open_result(L, NULL);
}

static int l_close(lua_State *L)
{
    lua_port_close(handle_arg(L, 1));
    return 0;
}

static const char *port_state_name(GtScriptPortState s)
{
    switch (s) {
    case GT_SCRIPT_PORT_CONNECTING: return "connecting";
    case GT_SCRIPT_PORT_OPEN:       return "open";
    case GT_SCRIPT_PORT_LISTENING:  return "listening";
    case GT_SCRIPT_PORT_CONNECTED:  return "connected";
    default:                        return "lost";
    }
}

static int l_status(lua_State *L)
{
    int handle = handle_arg(L, 1);

    if (handle == 0) {
        GtLinkState st = GT_LINK_CLOSED;
        run_on_main_sync(main_link_state_cb, &st);
        lua_pushstring(L, link_state_name(st));
    } else if (handle > 0 && ports[handle] != NULL) {
        lua_pushstring(L, port_state_name(gt_script_port_get_state(ports[handle])));
    } else {
        lua_pushstring(L, "closed");
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Envoi complet : send, send_hex                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    GtScriptPort *port;          /* NULL : port 0 (liaison) */
    const guint8 *data;
    gsize         len;
    gssize        written;
    gint          err;           /* errno relevé sur le thread principal juste après l'écriture */
    GtLinkState   link_state;    /* port 0 : état de la liaison après l'essai */
} MainTrySend;

/* Une seule tentative d'écriture, jamais bloquante : le thread principal n'attend jamais un lecteur. */
static gboolean main_try_send_cb(gpointer user_data)
{
    MainTrySend *d = user_data;

    if (d->port != NULL) {
        d->written = gt_script_port_try_send(d->port, d->data, d->len, &d->err);
        return G_SOURCE_REMOVE;
    }
    GtkTermLink *link = gt_link_get_default();
    errno = 0;
    d->written = gt_link_send(link, d->data, d->len, GT_SEND_LUA);
    /* errno est propre au thread : relevé ici, avant tout autre appel (le thread Lua ne le verrait jamais) */
    d->err = d->written < 0 ? errno : 0;
    d->link_state = gt_link_get_state(link);
    return G_SOURCE_REMOVE;
}

typedef enum { SEND_DONE, SEND_FAILED, SEND_STOPPED } SendOutcome;

/* Écrit tout ou échoue (thread Lua) : écritures partielles et EAGAIN repris ici, pause de 2 ms entre deux essais sans progrès,
 * jusqu'au délai. *reason : chaîne statique (ou de g_strerror) en cas d'échec. Le port h est ouvert (vérifié par l'appelant). */
static SendOutcome send_all(int handle, const guint8 *data, gsize len, gint64 timeout_us, const char **reason)
{
    gint64 deadline = g_get_monotonic_time() + timeout_us;
    gsize done = 0;

    while (done < len) {
        MainTrySend d;
        gboolean lost;

        if (g_atomic_int_get(&stop_requested))
            return SEND_STOPPED;
        memset(&d, 0, sizeof(d));
        d.port = handle > 0 ? ports[handle] : NULL;
        d.data = data + done;
        d.len = len - done;
        run_on_main_sync(main_try_send_cb, &d);
        if (d.written > 0) {
            done += (gsize)d.written;
            continue;
        }
        lost = handle > 0 ? gt_script_port_get_state(ports[handle]) == GT_SCRIPT_PORT_LOST : link_state_lost(d.link_state);
        if (d.written == 0) {
            /* Rien d'écrivable : connexion en cours, serveur sans client, liaison fermée ou perdue */
            *reason = lost ? "link lost" : "not connected";
            return SEND_FAILED;
        }
        if (d.err == EAGAIN || d.err == EWOULDBLOCK || d.err == EINTR) {
            if (g_get_monotonic_time() >= deadline) {
                *reason = "timeout";
                return SEND_FAILED;
            }
            if (!pause_interruptible(SEND_RETRY_US))
                return SEND_STOPPED;
            continue;
        }
        *reason = (lost || d.err == EIO) ? "link lost" : g_strerror(d.err);
        return SEND_FAILED;
    }
    return SEND_DONE;
}

static int send_checked(lua_State *L, int handle, const guint8 *data, gsize len, gint64 timeout_us)
{
    const char *reason = NULL;
    SendOutcome o;

    if (handle < 0)
        return open_result(L, "invalid handle");
    if (handle > 0 && ports[handle] == NULL)
        return open_result(L, "not open");
    o = send_all(handle, data, len, timeout_us, &reason);
    if (o == SEND_STOPPED)
        return raise_stop(L);
    return open_result(L, o == SEND_DONE ? NULL : reason);
}

static int l_send(lua_State *L)
{
    int handle = handle_arg(L, 1);
    size_t len;
    const char *data = luaL_checklstring(L, 2, &len);
    gint64 timeout_us = opt_timeout_us(L, 3);

    /* data appartient à la pile Lua : valable pendant tout l'appel, le thread principal le lit pendant run_on_main_sync */
    return send_checked(L, handle, (const guint8 *)data, len, timeout_us);
}

static int l_send_hex(lua_State *L)
{
    int handle = handle_arg(L, 1);
    const char *hex = luaL_checkstring(L, 2);
    gint64 timeout_us = opt_timeout_us(L, 3);
    GError *err = NULL;
    GBytes *bytes = gt_script_hex_decode(hex, &err);
    gsize len;
    const char *data;

    if (bytes == NULL) {
        lua_pushboolean(L, 0);
        lua_pushfstring(L, "invalid hex: %s", err->message);
        g_error_free(err);
        return 2;
    }
    /* Copie dans une chaîne Lua : rien à libérer si l'arrêt lève une erreur pendant l'envoi */
    data = g_bytes_get_data(bytes, &len);
    lua_pushlstring(L, data, len);
    g_bytes_unref(bytes);
    data = lua_tolstring(L, -1, &len);
    return send_checked(L, handle, (const guint8 *)data, len, timeout_us);
}

/* ------------------------------------------------------------------ */
/* Réception : wait_for, read, clear                                  */
/* ------------------------------------------------------------------ */

static int nil_reason(lua_State *L, const char *reason)
{
    lua_pushnil(L);
    lua_pushstring(L, reason);
    return 2;
}

/* wait_for(handle, pattern [, timeout_ms]) -> données jusqu'au motif inclus | nil, "timeout" | nil, "closed" */
static int l_wait_for(lua_State *L)
{
    int handle = handle_arg(L, 1);
    size_t pat_len;
    const char *pat = luaL_checklstring(L, 2, &pat_len);
    gint64 timeout_us = opt_timeout_us(L, 3);
    GtRxQueue *q;
    GBytes *found = NULL;
    GtRxWaitResult res;

    if (handle < 0)
        return nil_reason(L, "invalid handle");
    q = queue_for(handle);
    if (q == NULL)
        return nil_reason(L, "closed");
    /* L'attente prend et rend le verrou de la file elle-même ; l'arrêt est relu au moins toutes les 20 ms */
    res = gt_rx_queue_wait_for(q, (const guint8 *)pat, pat_len, g_get_monotonic_time() + timeout_us, &stop_requested, &found);
    report_overflow(handle, q);
    switch (res) {
    case GT_RX_WAIT_FOUND: {
        gsize n;
        const char *p = g_bytes_get_data(found, &n);
        lua_pushlstring(L, p, n);
        g_bytes_unref(found);
        return 1;
    }
    case GT_RX_WAIT_CANCELLED:
        return raise_stop(L);
    case GT_RX_WAIT_CLOSED:
        return nil_reason(L, "closed");
    default:
        return nil_reason(L, "timeout");
    }
}

static int l_read(lua_State *L)
{
    int handle = handle_arg(L, 1);
    GtRxQueue *q = queue_for(handle);
    GBytes *b;
    gsize n;
    const char *p;

    if (q == NULL) {
        lua_pushstring(L, "");
        return 1;
    }
    report_overflow(handle, q);
    b = gt_rx_queue_take_all(q);
    p = g_bytes_get_data(b, &n);
    lua_pushlstring(L, p, n);
    g_bytes_unref(b);
    return 1;
}

static int l_clear(lua_State *L)
{
    int handle = handle_arg(L, 1);
    GtRxQueue *q = queue_for(handle);

    if (q != NULL) {
        report_overflow(handle, q);
        gt_rx_queue_clear(q);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* sleep, time, log, save_csv                                         */
/* ------------------------------------------------------------------ */

static int l_sleep(lua_State *L)
{
    lua_Integer ms = luaL_checkinteger(L, 1);
    gboolean completed = pause_interruptible((gint64)CLAMP(ms, 0, MAX_TIMEOUT_MS) * 1000);

    report_all_overflows();
    if (!completed)
        return raise_stop(L);
    return 0;
}

/* time() -> secondes (nombre réel, horloge monotone) */
static int l_time(lua_State *L)
{
    lua_pushnumber(L, (lua_Number)g_get_monotonic_time() / 1e6);
    return 1;
}

static int l_log(lua_State *L)
{
    size_t len;
    const char *text = luaL_tolstring(L, 1, &len);
    engine_log("%s\n", text);
    lua_pop(L, 1);
    return 0;
}

static int l_save_csv(lua_State *L)
{
    const char *filename = luaL_checkstring(L, 1);
    int n = lua_gettop(L);

    FILE *f = fopen(filename, "a");
    if (!f) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, strerror(errno));
        return 2;
    }

    for (int i = 2; i <= n; i++) {
        if (i > 2) fprintf(f, ",");
        fprintf(f, "%s", luaL_tolstring(L, i, NULL));
        lua_pop(L, 1);
    }
    fprintf(f, "\n");
    fclose(f);

    lua_pushboolean(L, 1);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Macros et listes (instantanés pris sur le thread principal)        */
/* ------------------------------------------------------------------ */

/* Instantanés des listes et des macros, pris sur le thread principal puis poussés vers Lua (C10). */
typedef struct {
    gchar     *name;
    GPtrArray *displays;
    GPtrArray *values;
} ListSnapshot;

static void list_snapshot_free(gpointer p)
{
    ListSnapshot *s = p;
    g_free(s->name);
    g_ptr_array_unref(s->displays);
    g_ptr_array_unref(s->values);
    g_free(s);
}

static gboolean main_snapshot_lists_cb(gpointer user_data)
{
    GPtrArray *out = user_data;
    const GtMacroLists *lists = gt_macro_set_get_lists(macros_get_set());
    gint n_lists = gt_macro_lists_count(lists);
    for (int i = 0; i < n_lists; i++) {
        ListSnapshot *s = g_new0(ListSnapshot, 1);
        const gchar *name = gt_macro_lists_name(lists, i);
        s->name = g_strdup(name ? name : "");
        s->displays = g_ptr_array_new_with_free_func(g_free);
        s->values = g_ptr_array_new_with_free_func(g_free);
        for (int e = 0; e < gt_macro_lists_entry_count(lists, i); e++) {
            const gchar *disp = gt_macro_lists_entry_display(lists, i, e);
            const gchar *val  = gt_macro_lists_entry_value(lists, i, e);
            g_ptr_array_add(s->displays, g_strdup(disp ? disp : ""));
            g_ptr_array_add(s->values, g_strdup(val ? val : ""));
        }
        g_ptr_array_add(out, s);
    }
    return G_SOURCE_REMOVE;
}

typedef struct {
    gchar            *label;
    gint              n_args;
    macro_arg_info_t *infos;
} MacroSnapshot;

static void macro_snapshot_free(gpointer p)
{
    MacroSnapshot *s = p;
    g_free(s->label);
    macro_arg_infos_free(s->infos, s->n_args);
    g_free(s);
}

static gboolean main_snapshot_macros_cb(gpointer user_data)
{
    GPtrArray *out = user_data;
    GtMacroSet *set = macros_get_set();
    for (guint i = 0; i < gt_macro_set_count(set); i++) {
        const GtMacro *m = gt_macro_set_get(set, i);
        MacroSnapshot *s = g_new0(MacroSnapshot, 1);
        s->label = g_strdup(m->label);
        s->infos = macro_get_arg_infos(gt_macro_set_get_lists(set), m->action, &s->n_args);
        g_ptr_array_add(out, s);
    }
    return G_SOURCE_REMOVE;
}

/* get_lists() -> {{name, entries={{display,value},...}}, ...} */
static int l_get_lists(lua_State *L)
{
    GPtrArray *lists = g_ptr_array_new_with_free_func(list_snapshot_free);
    run_on_main_sync(main_snapshot_lists_cb, lists);
    lua_newtable(L);
    for (guint i = 0; i < lists->len; i++) {
        ListSnapshot *s = g_ptr_array_index(lists, i);
        lua_newtable(L);
        lua_pushstring(L, s->name);
        lua_setfield(L, -2, "name");
        lua_newtable(L);
        for (guint e = 0; e < s->displays->len; e++) {
            lua_newtable(L);
            lua_pushstring(L, g_ptr_array_index(s->displays, e));
            lua_setfield(L, -2, "display");
            lua_pushstring(L, g_ptr_array_index(s->values, e));
            lua_setfield(L, -2, "value");
            lua_rawseti(L, -2, e + 1);
        }
        lua_setfield(L, -2, "entries");
        lua_rawseti(L, -2, i + 1);
    }
    g_ptr_array_unref(lists);
    return 1;
}

/* get_macros() -> table of {index, label, n_args, args} */
static int l_get_macros(lua_State *L)
{
    GPtrArray *snap = g_ptr_array_new_with_free_func(macro_snapshot_free);
    run_on_main_sync(main_snapshot_macros_cb, snap);
    lua_newtable(L);
    for (guint i = 0; i < snap->len; i++) {
        MacroSnapshot *s = g_ptr_array_index(snap, i);
        lua_newtable(L);
        lua_pushinteger(L, i);
        lua_setfield(L, -2, "index");
        lua_pushstring(L, s->label);
        lua_setfield(L, -2, "label");
        lua_pushinteger(L, s->n_args);
        lua_setfield(L, -2, "n_args");
        lua_newtable(L);
        for (int a = 0; a < s->n_args; a++) {
            lua_newtable(L);
            lua_pushstring(L, s->infos[a].type      ? s->infos[a].type      : "s");
            lua_setfield(L, -2, "type");
            lua_pushstring(L, s->infos[a].label     ? s->infos[a].label     : "");
            lua_setfield(L, -2, "label");
            lua_pushstring(L, s->infos[a].list_name ? s->infos[a].list_name : "");
            lua_setfield(L, -2, "list");
            lua_rawseti(L, -2, a + 1);
        }
        lua_setfield(L, -2, "args");
        lua_rawseti(L, -2, i + 1);
    }
    g_ptr_array_unref(snap);
    return 1;
}

typedef struct {
    int          index;     /* -1 : chercher par libellé */
    const char  *label;
    int          n_args;
    gchar      **args;
    gboolean     found;
    gboolean     ok;
} MainMacroData;

/* Résolution et envoi sur le thread principal, par le seul envoi de macro de l'interface (macros_send) : ordre respecté avec
 * gtkterm.send (M27). Sans argument Lua, les arguments enregistrés, comme le raccourci et le polling. */
static gboolean main_send_macro_cb(gpointer user_data)
{
    MainMacroData *d = user_data;
    GtMacroSet *set = macros_get_set();
    const GtMacro *m = d->index >= 0 ? gt_macro_set_get(set, (guint)d->index) : gt_macro_set_find_label(set, d->label);

    d->found = gt_macro_is_sendable(m);
    if (d->found)
        d->ok = macros_send(m->id, (const gchar *const *)d->args, d->n_args > 0 ? d->n_args : -1);
    return G_SOURCE_REMOVE;
}

/* send_macro(index_or_label [, arg1, arg2, ...]) */
static int l_send_macro(lua_State *L)
{
    MainMacroData d = { -1, NULL, 0, NULL, FALSE, FALSE };

    if (lua_type(L, 1) == LUA_TNUMBER)
        d.index = (int)lua_tointeger(L, 1);
    else
        d.label = luaL_checkstring(L, 1);

    d.n_args = lua_gettop(L) - 1;
    d.args = g_new0(gchar *, d.n_args + 1);
    for (int i = 0; i < d.n_args; i++) {
        const char *s = lua_tostring(L, i + 2);
        d.args[i] = g_strdup(s ? s : "");
    }

    run_on_main_sync(main_send_macro_cb, &d);
    g_strfreev(d.args);

    if (!d.found) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "macro not found");
        return 2;
    }
    lua_pushboolean(L, d.ok);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Enregistrement de l'API                                            */
/* ------------------------------------------------------------------ */

static const luaL_Reg gtkterm_funcs[] = {
    { "open",       l_open       },
    { "close",      l_close      },
    { "status",     l_status     },
    { "send",       l_send       },
    { "send_hex",   l_send_hex   },
    { "wait_for",   l_wait_for   },
    { "read",       l_read       },
    { "clear",      l_clear      },
    { "sleep",      l_sleep      },
    { "time",       l_time       },
    { "log",        l_log        },
    { "save_csv",   l_save_csv   },
    { "get_lists",  l_get_lists  },
    { "get_macros", l_get_macros },
    { "send_macro", l_send_macro },
    { NULL, NULL }
};

static void register_api(lua_State *L)
{
    luaL_newlib(L, gtkterm_funcs);
    lua_setglobal(L, "gtkterm");

    /* Convenience: main port handle constant */
    lua_pushinteger(L, 0);
    lua_setglobal(L, "MAIN_PORT");
}

/* ------------------------------------------------------------------ */
/* Fin de script et thread Lua                                        */
/* ------------------------------------------------------------------ */

static gboolean done_idle_cb(gpointer user_data)
{
    GThread *finished;

    /* Le thread Lua a fini : on le joint ici, sur le thread principal, jamais pendant qu'il peut attendre ce thread (M30). */
    g_mutex_lock(&engine_mutex);
    finished = lua_thread;
    lua_thread = NULL;
    g_mutex_unlock(&engine_mutex);
    if (finished)
        g_thread_join(finished);

    if (done_func)
        done_func(done_data);
    return G_SOURCE_REMOVE;
}

/* Fin de script (thread Lua) : pertes signalées, ports Lua refermés sur le thread principal, écoute du port 0 retirée, file du
 * port 0 vidée. */
static void end_of_script_cleanup(void)
{
    report_all_overflows();
    for (int h = 1; h < MAX_LUA_PORTS; h++)
        lua_port_close(h);
    run_on_main_sync(main_port_disconnect_cb, NULL);
    gt_rx_queue_clear(main_queue);
}

static gpointer lua_thread_func(gpointer user_data)
{
    char *script = user_data;

    /* Nombres Lua avec un point quelle que soit la langue (M9) : locale « C » propre à ce thread. */
    locale_t c_locale = newlocale(LC_ALL_MASK, "C", (locale_t)0);
    if (c_locale)
        uselocale(c_locale);

    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    register_api(L);

    /* Écoute du port principal */
    run_on_main_sync(main_port_connect_cb, NULL);

    int ret = luaL_dostring(L, script);
    if (ret != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        engine_log("Lua error: %s\n", err ? err : "(unknown)");
    }

    end_of_script_cleanup();
    lua_close(L);
    g_free(script);

    if (c_locale) {
        uselocale(LC_GLOBAL_LOCALE);
        freelocale(c_locale);
    }

    g_idle_add_full(G_PRIORITY_DEFAULT, done_idle_cb, NULL, NULL);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* API publique                                                       */
/* ------------------------------------------------------------------ */

void script_engine_setup(void)
{
    if (main_queue == NULL)
        main_queue = gt_rx_queue_new(GT_SCRIPT_RX_MAX);
}

void script_engine_run(const char *script_text)
{
    script_engine_setup();
    g_mutex_lock(&engine_mutex);
    if (lua_thread != NULL) {
        g_mutex_unlock(&engine_mutex);
        engine_log_line(_("Script already running."));
        return;
    }
    /* Exécution suivante : drapeau d'arrêt, pertes signalées et file du port 0 remis à zéro avant le thread */
    g_atomic_int_set(&stop_requested, 0);
    memset(overflow_reported, 0, sizeof(overflow_reported));
    gt_rx_queue_reset(main_queue);
    char *copy = g_strdup(script_text);
    lua_thread = g_thread_new("lua-script", lua_thread_func, copy);
    g_mutex_unlock(&engine_mutex);
}

void script_engine_stop(void)
{
    g_atomic_int_set(&stop_requested, 1);
    g_mutex_lock(&pause_mutex);
    g_cond_broadcast(&pause_cond);
    g_mutex_unlock(&pause_mutex);
}

gboolean script_engine_is_running(void)
{
    g_mutex_lock(&engine_mutex);
    gboolean running = (lua_thread != NULL);
    g_mutex_unlock(&engine_mutex);
    return running;
}

void script_engine_set_log_func(ScriptLogFunc func, gpointer user_data)
{
    log_func = func;
    log_data = user_data;
}

void script_engine_set_done_func(ScriptDoneFunc func, gpointer user_data)
{
    done_func = func;
    done_data = user_data;
}
```

Remarques pour l'implémenteur :
- Les `GMutex`/`GCond` statiques n'ont pas besoin de `g_mutex_init` (mémoire statique remise à zéro) : l'ancien `g_mutex_init(&engine_mutex)` de `script_engine_setup` disparaît.
- `luaL_check*` peut lever une erreur Lua (`longjmp`) : dans `l_open`, toutes les vérifications d'arguments précèdent la seule allocation C (`d.error`, libérée juste après `lua_pushstring`).

- [ ] **Step 5 : vérifier qu'aucune duplication ne reste**

Run : `grep -nE "termios|getaddrinfo|L_global|chan_err|GIOChannel|RX_BUF_MAX|configure_serial|baud_constant|tcp_accept_cb" src/script_engine.c`
Expected : aucune ligne.

- [ ] **Step 6 : vérifier le succès**

Run : `ninja -C build && meson test -C build test_script_engine --print-errorlogs`
Expected : `test_script_engine` OK — anciens cas (dont `/script_engine/erreur-avec-ports-ouverts`, `/script_engine/pair-tcp-ferme`, `/script_engine/exemple-10-mise-a-jour`) et les 9 nouveaux.

Run : `meson test -C build --print-errorlogs`
Expected : toute la suite par défaut OK.

- [ ] **Step 7 : ASan**

Run : `ninja -C build-asan && meson test -C build-asan --print-errorlogs`
Expected : OK, aucune fuite ni erreur.

- [ ] **Step 8 : commit**

```bash
git add src/script_engine.c tests/test_script_engine.c tests/stubs/stub_link.c tests/stubs/stubs.h
git commit -m "$(cat <<'EOF'
lua : ports du moteur sur GtScriptPort, envois complets et raisons d'échec

Ports Lua 1 à 15 ouverts, fermés et écrits sur le thread principal par le
transport par instance (débits du transport, IPv6, connexion attendue avec délai,
adresse d'écoute) ; files de réception de 1 Mio avec perte signalée une fois ;
send/send_hex tout ou échec avec reprises sur le thread Lua ; wait_for et open
rendent une raison ; nouvel appel status(). Fin de la duplication termios/TCP et du
code mort L_global/chan_err.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 4 : moteur, partie B — arrêt fiable, `os.exit`, `print`, exécution protégée, journal regroupé, fermeture

**Files :**
- Modify : `src/script_engine.h` (réécrit), `src/script_engine.c` (sections remplacées), `src/script_panel.c`, `src/gtkterm.c`, `po/POTFILES.in`, `po/fr.po`
- Test : `tests/test_script_engine.c` (étendu)

**Interfaces :**
- Consumes : tâche 3 (`raise_stop`, `pause_interruptible`, `report_all_overflows`, `end_of_script_cleanup`, `stop_requested`, `main_port_connect_cb`, `run_on_main_sync`).
- Produces :
  - `typedef enum { GT_SCRIPT_END_FINISHED, GT_SCRIPT_END_ERROR, GT_SCRIPT_END_STOPPED } GtScriptEnd;`
  - `typedef void (*ScriptDoneFunc)(GtScriptEnd end, gpointer user_data);` (changement de signature)
  - `void script_engine_shutdown(void);`
  - Contrat de `ScriptLogFunc` : texte en UTF-8 valide, par lots.

- [ ] **Step 1 : écrire les tests de la partie B (échouent à la compilation)**

Dans `tests/test_script_engine.c` :

1. Remplacer les déclarations et fonctions du haut du fichier, de `static GThread *thread_principal;` jusqu'à la fin de `executer()` incluse, par :

```c
static GThread *thread_principal;
static GMainLoop *boucle = NULL;
static GString *journal;
static gboolean fini;
static guint appels_journal;
static GtScriptEnd derniere_fin;
static gint64 fin_us;

static void
sur_journal(const char *texte, gpointer data)
{
	appels_journal++;
	g_string_append(journal, texte);
}

static void
sur_fin(GtScriptEnd fin_script, gpointer data)
{
	derniere_fin = fin_script;
	fin_us = g_get_monotonic_time();
	fini = TRUE;
	if(boucle != NULL)
		g_main_loop_quit(boucle);
}

static gboolean
sur_delai(gpointer data)
{
	g_main_loop_quit(boucle);
	return G_SOURCE_REMOVE;
}

/* Lance le script et fait tourner la boucle jusqu'à sa fin (TRUE) ou jusqu'au délai (FALSE). */
static gboolean
executer(const gchar *script, guint delai_ms)
{
	guint delai;

	g_string_truncate(journal, 0);
	appels_journal = 0;
	fini = FALSE;
	boucle = g_main_loop_new(NULL, FALSE);
	delai = g_timeout_add(delai_ms, sur_delai, NULL);
	script_engine_run(script);
	g_main_loop_run(boucle);
	if(fini)
		g_source_remove(delai);
	g_main_loop_unref(boucle);
	boucle = NULL;
	return fini;
}
```

2. Avant `int\nmain(int argc, char **argv)`, insérer :

```c
/* ---- Sous-projet 5 : arrêt, os.exit, print, journal, fermeture ---------------- */

static guint arret_id = 0;
static gint64 arret_us = 0;

static gboolean
arreter(gpointer data)
{
	arret_id = 0;
	arret_us = g_get_monotonic_time();
	script_engine_stop();
	return G_SOURCE_REMOVE;
}

/* Lance le script, demande l'arrêt après apres_ms, vérifie une fin STOPPED moins d'une seconde après la demande. */
static void
verifier_arret(const gchar *script, guint apres_ms)
{
	arret_us = 0;
	arret_id = g_timeout_add(apres_ms, arreter, NULL);
	g_assert_true(executer(script, 15000));
	if(arret_id != 0)
	{
		g_source_remove(arret_id);
		arret_id = 0;
	}
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_STOPPED);
	g_assert_cmpint(arret_us, >, 0);
	g_assert_cmpint(fin_us - arret_us, <, G_USEC_PER_SEC);
	g_assert_null(strstr(journal->str, "Lua error"));
}

static void
test_stop_boucle_infinie(void)
{
	stub_reset();
	verifier_arret("while true do end\n", 200);
}

static void
test_stop_boucles_pcall(void)
{
	stub_reset();
	/* pcall qui avale tout, imbriqué dans une boucle infinie */
	verifier_arret("while true do pcall(function() while true do end end) end\n", 200);
	/* pcall court répété */
	verifier_arret("while true do pcall(function() local x = 0 for i = 1, 100 do x = x + i end end) end\n", 200);
	/* xpcall avec gestionnaire de message (interrompu à son tour : « error in error handling ») */
	verifier_arret("while true do xpcall(function() while true do end end, function(e) return e end) end\n", 200);
	/* coroutines relancées en boucle */
	verifier_arret("while true do coroutine.resume(coroutine.create(function() while true do end end)) end\n", 200);
}

static void
test_stop_pendant_sleep(void)
{
	stub_reset();
	verifier_arret("gtkterm.sleep(60000)\ngtkterm.log('apres')\n", 200);
	g_assert_null(strstr(journal->str, "apres"));
}

static void
test_stop_pendant_wait_for(void)
{
	stub_reset();
	verifier_arret("gtkterm.wait_for(0, 'jamais', 60000)\ngtkterm.log('apres')\n", 200);
	g_assert_null(strstr(journal->str, "apres"));
}

static void
test_stop_pendant_connexion(void)
{
	int ecoute, occupant;
	guint16 port;
	guint avant;
	gchar *script;

	if(!ecouteur_sature(&ecoute, &occupant, &port))
	{
		close(occupant);
		close(ecoute);
		g_test_skip("file d'attente TCP non saturable ici");
		return;
	}
	g_main_context_default();
	avant = compter_descripteurs();
	script = g_strdup_printf("gtkterm.open(1, 'tcp_client', '127.0.0.1', %u, 60000)\ngtkterm.log('apres')\n", port);
	stub_reset();
	verifier_arret(script, 300);
	g_assert_null(strstr(journal->str, "apres"));
	/* Port refermé par le nettoyage de fin de script */
	g_assert_cmpuint(compter_descripteurs(), ==, avant);
	g_free(script);
	close(occupant);
	close(ecoute);
}

static void
test_stop_pendant_envoi(void)
{
	int maitre, esclave;
	guint avant;
	gchar *chemin, *script;

	if(openpty(&maitre, &esclave, NULL, NULL, NULL) != 0)
	{
		g_test_skip("openpty indisponible");
		return;
	}
	chemin = g_strdup(ttyname(esclave));
	g_main_context_default();
	avant = compter_descripteurs();
	/* Personne ne lit le maître : l'envoi attend en reprises de 2 ms jusqu'à l'arrêt */
	script = g_strdup_printf("assert(gtkterm.open(1, 'serial', '%s'))\n"
	                         "gtkterm.send(1, string.rep('x', 1048576), 60000)\n"
	                         "gtkterm.log('apres')\n", chemin);
	stub_reset();
	verifier_arret(script, 300);
	g_assert_null(strstr(journal->str, "apres"));
	g_assert_cmpuint(compter_descripteurs(), ==, avant);
	g_free(script);
	g_free(chemin);
	close(esclave);
	close(maitre);
}

static void
test_os_exit(void)
{
	static const struct
	{
		const gchar *script;
		GtScriptEnd fin;
		const gchar *message;
	} cas[] = {
		{ "os.exit()\ngtkterm.log('apres')\n", GT_SCRIPT_END_FINISHED, NULL },
		{ "os.exit(true)\ngtkterm.log('apres')\n", GT_SCRIPT_END_FINISHED, NULL },
		{ "os.exit(0)\ngtkterm.log('apres')\n", GT_SCRIPT_END_FINISHED, NULL },
		{ "os.exit(3)\ngtkterm.log('apres')\n", GT_SCRIPT_END_ERROR, "Script exited with status 3\n" },
		{ "os.exit(false)\ngtkterm.log('apres')\n", GT_SCRIPT_END_ERROR, "Script exited with status 1\n" },
		{ "pcall(os.exit, 4)\ngtkterm.log('apres')\n", GT_SCRIPT_END_ERROR, "Script exited with status 4\n" },
	};

	stub_reset();
	for(guint i = 0; i < G_N_ELEMENTS(cas); i++)
	{
		/* Le processus de test survit : os.exit termine le script, jamais GTKTerm */
		g_assert_true(executer(cas[i].script, 5000));
		g_assert_cmpint(derniere_fin, ==, cas[i].fin);
		g_assert_null(strstr(journal->str, "apres"));
		g_assert_null(strstr(journal->str, "Lua error"));
		if(cas[i].message != NULL)
			g_assert_nonnull(strstr(journal->str, cas[i].message));
		else
			g_assert_null(strstr(journal->str, "Script exited"));
	}
}

static void
test_print(void)
{
	stub_reset();
	g_assert_true(executer("print('a', 1, nil, true)\nprint()\nprint('fin')\n", 5000));
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_FINISHED);
	g_assert_nonnull(strstr(journal->str, "a\t1\tnil\ttrue\n\nfin\n"));
}

static void
test_raisons_de_fin(void)
{
	stub_reset();
	g_assert_true(executer("gtkterm.log('ok')\n", 5000));
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_FINISHED);
	g_assert_null(strstr(journal->str, "Lua error"));

	g_assert_true(executer("error('boum')\n", 5000));
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_ERROR);
	g_assert_nonnull(strstr(journal->str, "Lua error: script:1: boum\n"));

	g_assert_true(executer("x = = 1\n", 5000));
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_ERROR);
	g_assert_nonnull(strstr(journal->str, "Lua error: "));

	g_assert_true(executer("error({})\n", 5000));
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_ERROR);
	g_assert_nonnull(strstr(journal->str, "Lua error: (error object is a table value)\n"));

	g_assert_true(executer("error(42)\n", 5000));
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_ERROR);
	g_assert_nonnull(strstr(journal->str, "Lua error: 42\n"));
}

static void
test_journal_en_rafale(void)
{
	guint lignes = 0;

	stub_reset();
	g_assert_true(executer("for i = 1, 100000 do gtkterm.log(i) end\n", 60000));
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_FINISHED);
	/* Regroupé : bien moins d'un appel par ligne ; rien de perdu sous le plafond (~0,6 Mo) */
	g_assert_cmpuint(appels_journal, <, 1000);
	for(const gchar *p = journal->str; *p != '\0'; p++)
		if(*p == '\n')
			lignes++;
	g_assert_cmpuint(lignes, ==, 100000);
	g_assert_true(g_str_has_prefix(journal->str, "1\n2\n3\n"));
	g_assert_true(g_str_has_suffix(journal->str, "99999\n100000\n"));
	g_assert_null(strstr(journal->str, "dropped"));
}

/* Fait tourner le contexte par défaut jusqu'à ce que le journal contienne marqueur (ou ms) */
static gboolean
iterer_jusqu_a(const gchar *marqueur, guint ms)
{
	gint64 fin = g_get_monotonic_time() + (gint64)ms * 1000;

	while(!journal_contient(marqueur))
	{
		if(g_get_monotonic_time() >= fin)
			return FALSE;
		if(!g_main_context_iteration(NULL, FALSE))
			g_usleep(1000);
	}
	return TRUE;
}

static void
test_journal_plafond(void)
{
	guint lignes_x = 0;
	gint64 fin;

	stub_reset();
	g_string_truncate(journal, 0);
	fini = FALSE;
	script_engine_run("gtkterm.log('pret')\n"
	                  "assert(gtkterm.wait_for(0, 'go', 10000))\n"
	                  "local s = string.rep('x', 1023)\n"
	                  "for i = 1, 3072 do gtkterm.log(s) end\n"
	                  "assert(gtkterm.wait_for(0, 'suite', 10000))\n"
	                  "gtkterm.log('termine')\n");
	g_assert_true(iterer_jusqu_a("pret\n", 10000));
	stub_inject_rx("go", 2);
	/* Thread principal immobile 1 s : 3 Mio journalisés sans vidage possible, seuls les 1024 premiers Kio tiennent */
	g_usleep(G_USEC_PER_SEC);
	g_assert_true(iterer_jusqu_a("dropped]\n", 10000));
	stub_inject_rx("suite", 5);
	fin = g_get_monotonic_time() + 10 * G_USEC_PER_SEC;
	while(!fini && g_get_monotonic_time() < fin)
		if(!g_main_context_iteration(NULL, FALSE))
			g_usleep(1000);
	g_assert_true(fini);
	g_assert_nonnull(strstr(journal->str, "[2097152 bytes of log output dropped]\n"));
	g_assert_nonnull(strstr(journal->str, "termine\n"));
	for(const gchar *p = strstr(journal->str, "xxx"); p != NULL; p = strstr(p + 1, "\nxxx"))
		lignes_x++;
	g_assert_cmpuint(lignes_x, ==, 1024);
}

static void
test_journal_binaire(void)
{
	stub_reset();
	g_assert_true(executer("gtkterm.log('a\\0b')\ngtkterm.log('\\255fin')\ngtkterm.log('suite')\n", 5000));
	/* Octet nul montré « \0 » (il couperait tout le lot) ; UTF-8 réparé (U+FFFD) au lieu de faire refuser le lot */
	g_assert_nonnull(strstr(journal->str, "a\\0b\n"));
	g_assert_nonnull(strstr(journal->str, "\357\277\275fin\nsuite\n"));
	g_assert_true(g_utf8_validate(journal->str, -1, NULL));
}

static void
test_relance_apres_arret(void)
{
	stub_reset();
	verifier_arret("while true do end\n", 200);
	g_assert_true(executer("gtkterm.log('relance')\n", 5000));
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_FINISHED);
	g_assert_nonnull(strstr(journal->str, "relance\n"));
	g_assert_true(executer("os.exit(2)\n", 5000));
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_ERROR);
	g_assert_true(executer("gtkterm.sleep(10)\ngtkterm.log('encore')\n", 5000));
	g_assert_cmpint(derniere_fin, ==, GT_SCRIPT_END_FINISHED);
	g_assert_nonnull(strstr(journal->str, "encore\n"));
	g_assert_null(strstr(journal->str, "Script exited"));
}

static void
test_shutdown_sans_script(void)
{
	g_assert_false(script_engine_is_running());
	script_engine_shutdown();
	g_assert_false(script_engine_is_running());
}

/* Dernier cas : script_engine_shutdown retire les fonctions de journal et de fin (remises ensuite pour la forme). */
static void
test_shutdown_pendant_boucle(void)
{
	int maitre, esclave;
	struct termios tio;
	guint avant;
	gchar *chemin, *script;
	gint64 t0;

	if(openpty(&maitre, &esclave, NULL, NULL, NULL) != 0)
	{
		g_test_skip("openpty indisponible");
		return;
	}
	chemin = g_strdup(ttyname(esclave));
	/* Marque d'origine : CSTOPB posé ; le script ouvre avec 1 bit de stop ; la fermeture doit le restaurer */
	g_assert_cmpint(tcgetattr(esclave, &tio), ==, 0);
	tio.c_cflag |= CSTOPB;
	g_assert_cmpint(tcsetattr(esclave, TCSANOW, &tio), ==, 0);
	g_main_context_default();
	avant = compter_descripteurs();
	script = g_strdup_printf("assert(gtkterm.open(1, 'serial', '%s'))\ngtkterm.log('ouvert')\nwhile true do end\n", chemin);
	stub_reset();
	g_string_truncate(journal, 0);
	fini = FALSE;
	script_engine_run(script);
	g_assert_true(iterer_jusqu_a("ouvert\n", 10000));
	t0 = g_get_monotonic_time();
	script_engine_shutdown();
	g_assert_cmpint(g_get_monotonic_time() - t0, <, 3 * G_USEC_PER_SEC);
	g_assert_false(script_engine_is_running());
	g_assert_false(fini);	/* fonction de fin retirée : jamais appelée */
	g_assert_cmpuint(compter_descripteurs(), ==, avant);
	g_assert_cmpint(tcgetattr(esclave, &tio), ==, 0);
	g_assert_cmpuint(tio.c_cflag & CSTOPB, ==, CSTOPB);
	script_engine_set_log_func(sur_journal, NULL);
	script_engine_set_done_func(sur_fin, NULL);
	g_free(script);
	g_free(chemin);
	close(esclave);
	close(maitre);
}
```

3. Dans `main`, après `g_test_add_func("/script_engine/debordement-signale-une-fois", …);`, ajouter (le cas de fermeture reste le dernier) :

```c
	g_test_add_func("/script_engine/stop-boucle-infinie", test_stop_boucle_infinie);
	g_test_add_func("/script_engine/stop-boucles-pcall", test_stop_boucles_pcall);
	g_test_add_func("/script_engine/stop-pendant-sleep", test_stop_pendant_sleep);
	g_test_add_func("/script_engine/stop-pendant-wait-for", test_stop_pendant_wait_for);
	g_test_add_func("/script_engine/stop-pendant-connexion", test_stop_pendant_connexion);
	g_test_add_func("/script_engine/stop-pendant-envoi", test_stop_pendant_envoi);
	g_test_add_func("/script_engine/os-exit", test_os_exit);
	g_test_add_func("/script_engine/print", test_print);
	g_test_add_func("/script_engine/raisons-de-fin", test_raisons_de_fin);
	g_test_add_func("/script_engine/journal-en-rafale", test_journal_en_rafale);
	g_test_add_func("/script_engine/journal-plafond", test_journal_plafond);
	g_test_add_func("/script_engine/journal-binaire", test_journal_binaire);
	g_test_add_func("/script_engine/relance-apres-arret", test_relance_apres_arret);
	g_test_add_func("/script_engine/shutdown-sans-script", test_shutdown_sans_script);
	g_test_add_func("/script_engine/shutdown-pendant-boucle", test_shutdown_pendant_boucle);
```

- [ ] **Step 2 : vérifier l'échec**

Run : `ninja -C build`
Expected : échec de compilation de `test_script_engine.c` (`GtScriptEnd` inconnu, `script_engine_shutdown` non déclarée).

- [ ] **Step 3 : nouvel en-tête du moteur**

Remplacer tout `src/script_engine.h` par :

```c
#ifndef SCRIPT_ENGINE_H_
#define SCRIPT_ENGINE_H_

#include <glib.h>

/* Moteur Lua (sous-projet 5). Toutes ces fonctions s'appellent depuis le thread principal. */
void     script_engine_setup(void);
void     script_engine_run(const char *script_text);
/* Demande l'arrêt : le script s'arrête à sa prochaine instruction Lua, ou au plus 20 ms après dans une fonction bloquante du
 * moteur. Un appel bloquant de la bibliothèque standard (io.read, os.execute) ne peut pas être interrompu. */
void     script_engine_stop(void);
gboolean script_engine_is_running(void);
/* Fermeture de GTKTerm (après gtk_main) : retire les fonctions de journal et de fin, arrête le script et fait tourner le contexte
 * par défaut jusqu'à la fin du thread (qui referme ses ports par ce thread), au plus 3 s ; au-delà, avertissement, thread abandonné. */
void     script_engine_shutdown(void);

/* Texte du journal, en UTF-8 valide, par lots (plusieurs lignes à la fois). */
typedef void (*ScriptLogFunc)(const char *text, gpointer user_data);
void     script_engine_set_log_func(ScriptLogFunc func, gpointer user_data);

typedef enum
{
	GT_SCRIPT_END_FINISHED,	/* fin normale, os.exit(), os.exit(true) ou os.exit(0) */
	GT_SCRIPT_END_ERROR,	/* erreur Lua ou os.exit non nul : le message est journalisé avant */
	GT_SCRIPT_END_STOPPED	/* « Stop » ou fermeture de GTKTerm */
} GtScriptEnd;

/* Appelée sur le thread principal après le vidage du journal, le thread Lua joint. */
typedef void (*ScriptDoneFunc)(GtScriptEnd end, gpointer user_data);
void     script_engine_set_done_func(ScriptDoneFunc func, gpointer user_data);

#endif
```

- [ ] **Step 4 : remplacer les sections du moteur**

Dans `src/script_engine.c` :

**(a)** Après la ligne `#define STOP_POLL_US ...`, ajouter :

```c
#define HOOK_COUNT          1000                            /* crochet d'arrêt toutes les 1000 instructions */
#define LOG_MAX_PENDING     (1024 * 1024)                   /* journal en attente au plus 1 Mio */
#define LOG_FLUSH_DELAY_MS  20                              /* regroupement du journal : un vidage au plus toutes les 20 ms */
#define SHUTDOWN_TIMEOUT_US (3 * G_USEC_PER_SEC)            /* fermeture de GTKTerm : 3 s au plus pour le thread Lua */
```

Après la ligne `static gint          stop_requested = 0;   /* atomique : « Stop » */`, ajouter :

```c
static gint          exit_requested = 0;   /* atomique : os.exit appelé (stop_requested est alors posé aussi) */
static int           exit_status = 0;      /* code d'os.exit (écrit et lu par le thread Lua seulement) */
```

**(b)** Remplacer toute la section « Journal » (de sa bannière jusqu'à la fin de `engine_log_line`) par :

```c
/* ------------------------------------------------------------------ */
/* Journal                                                            */
/* ------------------------------------------------------------------ */

/* Le texte du thread Lua et des avertissements du moteur s'accumule dans un tampon verrouillé ; une seule source à la fois, à
 * G_PRIORITY_DEFAULT_IDLE (après le rafraîchissement GTK) et 20 ms après le premier ajout, remet tout le tampon en un appel. */
static GMutex   log_mutex;
static GString *log_pending = NULL;     /* texte en attente (log_mutex) */
static gsize    log_dropped = 0;        /* octets jetés au-delà du plafond depuis le dernier vidage (log_mutex) */
static guint    log_source_id = 0;      /* source de vidage programmée, 0 sinon (log_mutex) */

static void log_flush(void);

static gboolean log_flush_cb(gpointer user_data)
{
    g_mutex_lock(&log_mutex);
    log_source_id = 0;
    g_mutex_unlock(&log_mutex);
    log_flush();
    return G_SOURCE_REMOVE;
}

/* N'importe quel thread : ajoute au tampon (plafond 1 Mio : au-delà, compté comme jeté) et programme un vidage s'il n'y en a
 * pas. Un octet nul est écrit « \0 » : il couperait tout le lot remis à la fonction de journal. */
static void log_append_len(const char *text, gsize len)
{
    g_mutex_lock(&log_mutex);
    if (log_pending == NULL)
        log_pending = g_string_new(NULL);
    if (log_pending->len + len > LOG_MAX_PENDING) {
        log_dropped += len;
    } else {
        const char *p = text, *end = text + len;
        while (p < end) {
            const char *nul = memchr(p, '\0', (size_t)(end - p));
            const char *stop = nul != NULL ? nul : end;
            g_string_append_len(log_pending, p, (gssize)(stop - p));
            if (nul == NULL)
                break;
            g_string_append(log_pending, "\\0");
            p = nul + 1;
        }
    }
    if (log_source_id == 0)
        log_source_id = g_timeout_add_full(G_PRIORITY_DEFAULT_IDLE, LOG_FLUSH_DELAY_MS, log_flush_cb, NULL, NULL);
    g_mutex_unlock(&log_mutex);
}

/* Thread principal : tout le texte en attente en un appel (UTF-8 rendu valide), puis la ligne de pertes éventuelle. */
static void log_flush(void)
{
    GString *text = NULL;
    gsize dropped;

    g_mutex_lock(&log_mutex);
    if (log_pending != NULL && log_pending->len > 0) {
        text = log_pending;
        log_pending = g_string_new(NULL);
    }
    dropped = log_dropped;
    log_dropped = 0;
    g_mutex_unlock(&log_mutex);

    if (text != NULL) {
        if (log_func) {
            gchar *valid = g_utf8_make_valid(text->str, (gssize)text->len);
            log_func(valid, log_data);
            g_free(valid);
        }
        g_string_free(text, TRUE);
    }
    if (dropped > 0 && log_func) {
        gchar *msg = g_strdup_printf(_("[%zu bytes of log output dropped]"), dropped);
        gchar *line = g_strconcat(msg, "\n", NULL);
        log_func(line, log_data);
        g_free(line);
        g_free(msg);
    }
}

static void engine_log_line(const char *fmt, ...) G_GNUC_PRINTF(1, 2);
/* Une ligne du moteur (avertissement, erreur) : texte formaté suivi d'une fin de ligne, n'importe quel thread. */
static void engine_log_line(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *text = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    char *line = g_strconcat(text, "\n", NULL);
    log_append_len(line, strlen(line));
    g_free(line);
    g_free(text);
}
```

(`engine_log` et `LogIdleData`/`log_idle_cb` disparaissent : plus aucun appel ne doit rester — l'ancien `engine_log("Lua error: …")` de `lua_thread_func` est remplacé en (g).)

**(c)** Remplacer la fonction `run_on_main_sync` par :

```c
static void run_on_main_sync(GSourceFunc func, gpointer data)
{
    /* Appelée par le thread qui possède le contexte par défaut (thread principal dans une itération) : exécution directe, sinon
     * il attendrait une source qu'il est seul à pouvoir distribuer. Le thread Lua n'en est jamais propriétaire (pendant
     * script_engine_shutdown, le thread principal l'acquiert à chaque itération) : il passe toujours par la source. */
    if (g_main_context_is_owner(g_main_context_default())) {
        func(data);
        return;
    }
    MainCall mc;
    mc.func = func;
    mc.data = data;
    mc.done = FALSE;
    g_mutex_init(&mc.mutex);
    g_cond_init(&mc.cond);
    g_idle_add_full(G_PRIORITY_DEFAULT, main_call_trampoline, &mc, NULL);
    g_mutex_lock(&mc.mutex);
    while (!mc.done)
        g_cond_wait(&mc.cond, &mc.mutex);
    g_mutex_unlock(&mc.mutex);
    g_mutex_clear(&mc.mutex);
    g_cond_clear(&mc.cond);
}
```

**(d)** Remplacer `l_log` par :

```c
static int l_log(lua_State *L)
{
    size_t len;
    luaL_tolstring(L, 1, NULL);
    lua_pushliteral(L, "\n");
    lua_concat(L, 2);
    const char *line = lua_tolstring(L, -1, &len);
    log_append_len(line, len);
    lua_pop(L, 1);
    return 0;
}
```

**(e)** Juste après `l_save_csv`, insérer la section :

```c
/* ------------------------------------------------------------------ */
/* Crochet d'arrêt, os.exit, print, panique                           */
/* ------------------------------------------------------------------ */

/* Crochet de comptage (toutes les 1000 instructions). Dès que l'arrêt est demandé, il se réarme à chaque instruction puis lève
 * l'erreur d'arrêt : un pcall du script l'attrape une fois, l'instruction suivante hors de ce pcall la relève, donc toute
 * boucle finit par sortir (même un pcall qui avale tout, un xpcall dont le gestionnaire est interrompu, une coroutine). */
static void stop_hook(lua_State *L, lua_Debug *ar)
{
    (void)ar;
    if (g_atomic_int_get(&stop_requested)) {
        lua_sethook(L, stop_hook, LUA_MASKCOUNT, 1);
        raise_stop(L);
    }
}

/* os.exit([code]) : termine le script, jamais GTKTerm. Sans argument, true ou 0 : fin normale ; false (1) ou autre entier :
 * fin en erreur avec « Script exited with status N ». */
static int l_os_exit(lua_State *L)
{
    int status;

    if (lua_isnoneornil(L, 1))
        status = 0;
    else if (lua_isboolean(L, 1))
        status = lua_toboolean(L, 1) ? 0 : 1;
    else
        status = (int)luaL_checkinteger(L, 1);
    exit_status = status;
    g_atomic_int_set(&exit_requested, 1);
    g_atomic_int_set(&stop_requested, 1);
    lua_sethook(L, stop_hook, LUA_MASKCOUNT, 1);
    return raise_stop(L);
}

/* print(...) : vers la console, valeurs passées par tostring, séparées par une tabulation, fin de ligne. Concaténation au fil
 * de l'eau : au plus deux valeurs de plus sur la pile, quel que soit le nombre d'arguments. */
static int l_print(lua_State *L)
{
    int n = lua_gettop(L);
    size_t len;

    lua_pushliteral(L, "");
    for (int i = 1; i <= n; i++) {
        if (i > 1) {
            lua_pushliteral(L, "\t");
            lua_concat(L, 2);
        }
        luaL_tolstring(L, i, NULL);
        lua_concat(L, 2);
    }
    lua_pushliteral(L, "\n");
    lua_concat(L, 2);
    const char *text = lua_tolstring(L, -1, &len);
    log_append_len(text, len);
    return 0;
}

/* Ne devrait jamais servir (tout s'exécute sous lua_pcall) ; Lua appelle abort() au retour. */
static int panic_cb(lua_State *L)
{
    const char *msg = lua_tostring(L, -1);
    g_printerr("GTKTerm: Lua panic: %s\n", msg != NULL ? msg : "(error object is not a string)");
    return 0;
}
```

**(f)** Remplacer `register_api` par :

```c
static void register_api(lua_State *L)
{
    luaL_newlib(L, gtkterm_funcs);
    lua_setglobal(L, "gtkterm");

    /* Convenience: main port handle constant */
    lua_pushinteger(L, 0);
    lua_setglobal(L, "MAIN_PORT");

    /* print vers la console, os.exit qui termine le script et non GTKTerm */
    lua_pushcfunction(L, l_print);
    lua_setglobal(L, "print");
    lua_getglobal(L, "os");
    lua_pushcfunction(L, l_os_exit);
    lua_setfield(L, -2, "exit");
    lua_pop(L, 1);
}
```

**(g)** Remplacer toute la section « Fin de script et thread Lua » (de sa bannière jusqu'à la fin de `lua_thread_func`) par :

```c
/* ------------------------------------------------------------------ */
/* Fin de script et thread Lua                                        */
/* ------------------------------------------------------------------ */

static gboolean done_idle_cb(gpointer user_data)
{
    GtScriptEnd end = (GtScriptEnd)GPOINTER_TO_INT(user_data);
    GThread *finished;

    /* Le thread Lua a fini : on le joint ici, sur le thread principal, jamais pendant qu'il peut attendre ce thread (M30). */
    g_mutex_lock(&engine_mutex);
    finished = lua_thread;
    lua_thread = NULL;
    g_mutex_unlock(&engine_mutex);
    if (finished)
        g_thread_join(finished);

    /* Tout le texte du script avant le message de fin */
    log_flush();
    if (done_func)
        done_func(end, done_data);
    return G_SOURCE_REMOVE;
}

/* Fin de script (thread Lua) : pertes signalées, ports Lua refermés sur le thread principal, écoute du port 0 retirée, file du
 * port 0 vidée. */
static void end_of_script_cleanup(void)
{
    report_all_overflows();
    for (int h = 1; h < MAX_LUA_PORTS; h++)
        lua_port_close(h);
    run_on_main_sync(main_port_disconnect_cb, NULL);
    gt_rx_queue_clear(main_queue);
}

/* Message de l'objet d'erreur en haut de la pile, sans exécuter de Lua (aucun __tostring : on est hors de tout pcall). */
static gchar *error_message(lua_State *L)
{
    if (L == NULL)
        return g_strdup("not enough memory");
    switch (lua_type(L, -1)) {
    case LUA_TSTRING:
        return g_strdup(lua_tostring(L, -1));
    case LUA_TNUMBER:
        if (lua_isinteger(L, -1))
            return g_strdup_printf("%lld", (long long)lua_tointeger(L, -1));
        return g_strdup_printf("%.14g", (double)lua_tonumber(L, -1));
    default:
        return g_strdup_printf("(error object is a %s value)", luaL_typename(L, -1));
    }
}

/* Raison de fin, décidée par les drapeaux et non par l'objet d'erreur (un gestionnaire xpcall interrompu donne « error in error
 * handling ») ; journalise le message d'erreur ou de code de sortie avant. Thread Lua. */
static GtScriptEnd script_end_reason(lua_State *L, int ret)
{
    if (g_atomic_int_get(&exit_requested)) {
        if (exit_status == 0)
            return GT_SCRIPT_END_FINISHED;
        engine_log_line(_("Script exited with status %d"), exit_status);
        return GT_SCRIPT_END_ERROR;
    }
    if (ret == LUA_OK)
        return GT_SCRIPT_END_FINISHED;
    if (g_atomic_int_get(&stop_requested))
        return GT_SCRIPT_END_STOPPED;
    gchar *msg = error_message(L);
    engine_log_line(_("Lua error: %s"), msg);
    g_free(msg);
    return GT_SCRIPT_END_ERROR;
}

/* Exécutée par lua_pcall : bibliothèques, API et script. Plus aucune erreur n'est levée hors de cet appel protégé. */
static int protected_main(lua_State *L)
{
    const char *script = lua_touserdata(L, 1);

    luaL_openlibs(L);
    register_api(L);
    if (luaL_loadbuffer(L, script, strlen(script), "=script") != LUA_OK)
        return lua_error(L);
    lua_call(L, 0, 0);
    return 0;
}

static gpointer lua_thread_func(gpointer user_data)
{
    char *script = user_data;
    int ret = LUA_ERRMEM;

    /* Nombres Lua avec un point quelle que soit la langue (M9) : locale « C » propre à ce thread. */
    locale_t c_locale = newlocale(LC_ALL_MASK, "C", (locale_t)0);
    if (c_locale)
        uselocale(c_locale);

    /* Écoute du port principal */
    run_on_main_sync(main_port_connect_cb, NULL);

    lua_State *L = luaL_newstate();
    if (L != NULL) {
        lua_atpanic(L, panic_cb);
        lua_sethook(L, stop_hook, LUA_MASKCOUNT, HOOK_COUNT);
        /* Ni lua_pushcfunction (fonction C sans valeur liée) ni lua_pushlightuserdata n'allouent : rien ne lève hors du pcall */
        lua_pushcfunction(L, protected_main);
        lua_pushlightuserdata(L, script);
        ret = lua_pcall(L, 1, 0, 0);
    }
    GtScriptEnd end = script_end_reason(L, ret);

    end_of_script_cleanup();
    if (L != NULL) {
        /* Crochet retiré : les finaliseurs (__gc) de lua_close ne doivent pas être interrompus */
        lua_sethook(L, NULL, 0, 0);
        lua_close(L);
    }
    g_free(script);

    if (c_locale) {
        uselocale(LC_GLOBAL_LOCALE);
        freelocale(c_locale);
    }

    g_idle_add_full(G_PRIORITY_DEFAULT, done_idle_cb, GINT_TO_POINTER(end), NULL);
    return NULL;
}
```

**(h)** Dans la section « API publique », remplacer `script_engine_run` par la version ci-dessous et ajouter `script_engine_shutdown` après `script_engine_is_running` :

```c
void script_engine_run(const char *script_text)
{
    script_engine_setup();
    g_mutex_lock(&engine_mutex);
    if (lua_thread != NULL) {
        g_mutex_unlock(&engine_mutex);
        engine_log_line(_("Script already running."));
        return;
    }
    /* Exécution suivante : drapeaux d'arrêt et de sortie, pertes signalées et file du port 0 remis à zéro avant le thread */
    g_atomic_int_set(&stop_requested, 0);
    g_atomic_int_set(&exit_requested, 0);
    exit_status = 0;
    memset(overflow_reported, 0, sizeof(overflow_reported));
    gt_rx_queue_reset(main_queue);
    char *copy = g_strdup(script_text);
    lua_thread = g_thread_new("lua-script", lua_thread_func, copy);
    g_mutex_unlock(&engine_mutex);
}
```

```c
void script_engine_shutdown(void)
{
    if (!script_engine_is_running())
        return;
    /* Les widgets du panneau sont détruits ou vont l'être : plus de journal ni de fin vers l'interface */
    script_engine_set_log_func(NULL, NULL);
    script_engine_set_done_func(NULL, NULL);
    script_engine_stop();

    /* Le thread Lua referme ses ports par run_on_main_sync : il a besoin que ce thread fasse tourner le contexte par défaut.
     * done_idle_cb le joint et remet lua_thread à NULL. Itérations non bloquantes : un thread coincé dans io.read ne réveille
     * jamais le contexte. */
    gint64 deadline = g_get_monotonic_time() + SHUTDOWN_TIMEOUT_US;
    while (script_engine_is_running() && g_get_monotonic_time() < deadline) {
        if (!g_main_context_iteration(NULL, FALSE))
            g_usleep(1000);
    }
    if (script_engine_is_running())
        g_warning("Lua script still running after 3 s (blocked in a standard library call?): quitting without waiting for it");
}
```

`script_engine_stop` (tâche 3) ne change pas.

- [ ] **Step 5 : panneau de scripts**

Dans `src/script_panel.c` :

1. Remplacer `console_append` par :

```c
/* Console bornée : au-delà, les plus anciennes lignes sont retirées (une boucle de journal ne fait plus grossir le tampon sans fin) */
#define CONSOLE_MAX_LINES 10000

static void console_append(const char *text, gpointer user_data)
{
    (void)user_data;
    if (!console_view) return;

    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(console_view));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buf, &end);
    gtk_text_buffer_insert(buf, &end, text, -1);

    gint lines = gtk_text_buffer_get_line_count(buf);
    if (lines > CONSOLE_MAX_LINES) {
        GtkTextIter start, cut;
        gtk_text_buffer_get_start_iter(buf, &start);
        gtk_text_buffer_get_iter_at_line(buf, &cut, lines - CONSOLE_MAX_LINES);
        gtk_text_buffer_delete(buf, &start, &cut);
    }

    GtkTextMark *mark = gtk_text_buffer_get_insert(buf);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(console_view), mark);
}
```

2. Remplacer `on_script_done` par :

```c
static void on_script_done(GtScriptEnd end, gpointer user_data)
{
    (void)user_data;
    if (run_button)  gtk_widget_set_sensitive(run_button,  TRUE);
    if (stop_button) gtk_widget_set_sensitive(stop_button, FALSE);
    switch (end) {
    case GT_SCRIPT_END_STOPPED:
        console_append(_("[Script stopped]\n"), NULL);
        break;
    case GT_SCRIPT_END_ERROR:
        console_append(_("[Script finished with an error]\n"), NULL);
        break;
    default:
        console_append(_("[Script finished]\n"), NULL);
        break;
    }
}
```

3. Gardes de destruction (la fin de script peut être distribuée dans la même itération que la destruction de la fenêtre). Dans `create_script_panel` :
   - après `run_button = GTK_WIDGET(run_item);` ajouter `g_signal_connect(run_button, "destroy", G_CALLBACK(gtk_widget_destroyed), &run_button);`
   - après `stop_button = GTK_WIDGET(stop_item);` ajouter `g_signal_connect(stop_button, "destroy", G_CALLBACK(gtk_widget_destroyed), &stop_button);`
   - après `editor_view = gtk_source_view_new_with_buffer(src_buf);` ajouter `g_signal_connect(editor_view, "destroy", G_CALLBACK(gtk_widget_destroyed), &editor_view);`
   - après `console_view = gtk_source_view_new_with_buffer(con_buf);` ajouter `g_signal_connect(console_view, "destroy", G_CALLBACK(gtk_widget_destroyed), &console_view);`

4. Dans `on_run_clicked`, la ligne `GtkTextBuffer *con_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(console_view));` et la suivante deviennent :

```c
    if (console_view) {
        GtkTextBuffer *con_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(console_view));
        gtk_text_buffer_set_text(con_buf, "", -1);
    }
```

- [ ] **Step 6 : arrêt à la fermeture de GTKTerm**

Dans `src/gtkterm.c`, ajouter `#include "script_engine.h"` après `#include "script_panel.h"`, puis remplacer :

```c
	gtk_main();

	/* La fenêtre peut être détruite (bouton « Fermer » de la barre de titre) : plus aucun gestionnaire de l'interface sur la liaison */
```

par :

```c
	gtk_main();

	/* Script Lua en cours : arrêté, ses ports refermés (par ce thread) et son thread joint avant de fermer la liaison qu'il écoute */
	script_engine_shutdown();

	/* La fenêtre peut être détruite (bouton « Fermer » de la barre de titre) : plus aucun gestionnaire de l'interface sur la liaison */
```

- [ ] **Step 7 : traductions**

Dans `po/POTFILES.in`, ajouter `src/script_engine.c` avant `src/script_panel.c`.

À la fin de `po/fr.po`, ajouter :

```po

#: ../src/script_panel.c
msgid "[Script finished]\n"
msgstr "[Script terminé]\n"

#: ../src/script_panel.c
msgid "[Script stopped]\n"
msgstr "[Script arrêté]\n"

#: ../src/script_panel.c
msgid "[Script finished with an error]\n"
msgstr "[Script terminé avec une erreur]\n"

#: ../src/script_engine.c
#, c-format
msgid "Script exited with status %d"
msgstr "Script terminé avec le code %d"

#: ../src/script_engine.c
#, c-format
msgid "Port %d: receive buffer full, oldest data dropped"
msgstr "Port %d : tampon de réception plein, données les plus anciennes jetées"

#: ../src/script_engine.c
#, c-format
msgid "[%zu bytes of log output dropped]"
msgstr "[%zu octets de journal jetés]"

#: ../src/script_engine.c
#, c-format
msgid "Lua error: %s"
msgstr "Erreur Lua : %s"

#: ../src/script_engine.c
msgid "Script already running."
msgstr "Un script est déjà en cours."
```

Run : `msgfmt -c -o /dev/null po/fr.po`
Expected : aucune sortie (code 0).

- [ ] **Step 8 : vérifier le succès**

Run : `grep -nE "luaL_dostring|engine_log\(|LogIdleData" src/script_engine.c`
Expected : aucune ligne.

Run : `ninja -C build && meson test -C build test_script_engine --print-errorlogs`
Expected : `test_script_engine` OK (anciens cas, 9 de la tâche 3, 15 nouveaux).

Run : `meson test -C build --print-errorlogs`
Expected : toute la suite par défaut OK.

- [ ] **Step 9 : ASan**

Run : `ninja -C build-asan && meson test -C build-asan --print-errorlogs`
Expected : OK. Les seuils temporels (1 s pour l'arrêt, 3 s pour la fermeture, 500 ms pour la perte) sont larges devant les durées réelles (quelques dizaines de ms) ; s'ils sautent sous ASan, chercher une attente non interruptible, ne pas élargir les seuils.

- [ ] **Step 10 : commit**

```bash
git add src/script_engine.c src/script_engine.h src/script_panel.c src/gtkterm.c po/POTFILES.in po/fr.po tests/test_script_engine.c
git commit -m "$(cat <<'EOF'
lua : arrêt fiable, os.exit et print, journal regroupé, fermeture propre

« Stop » interrompt aussi une boucle de calcul (crochet Lua relevé après un pcall),
sleep et les attentes du moteur ; os.exit termine le script et non GTKTerm ; print
écrit dans la console ; exécution sous lua_pcall avec lua_atpanic ; fin rapportée
(terminé, arrêté, erreur) ; journal regroupé à basse priorité, plafonné à 1 Mio ;
console bornée à 10 000 lignes ; script arrêté et joint à la fermeture de GTKTerm.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 5 : scénario graphique de l'arrêt (`tests/gui/test_script_arret.py`)

**Files :**
- Create : `tests/gui/test_script_arret.py`
- Modify : `tests/gui/meson.build` (`gui_scenarios`)

**Interfaces :**
- Consumes : tâche 4 (« [Script stopped] », Run/Stop, `script_engine_shutdown` dans `main`) ; `tests/gui/gtktest.py` : `Env`, `menu(app, *path)`, `find`, `find1(app, role, name_re, showing=, pred=)`, `act`, `set_text`, `get_text`, `wait_until`, `check`, `finish(e, quit=False)`, `conclude()`, `Atspi`.
- Repères AT-SPI (lus dans `src/interface.c` et `src/script_panel.c`) : menu « View » → élément à cocher « Script panel » (`menu(e.app, 'View', 'Script panel')`) ; boutons de la barre d'outils du panneau : rôle `push button`, noms « Run » et « Stop » (« Stop » existe aussi comme `menu item` du menu « Log » : filtrer par rôle) ; éditeur et console : `GtkSourceView` de rôle `text` (éditeur modifiable, console non modifiable, terminal VTE de rôle `terminal`) ; fermeture : `push button` « Close » de la `frame` (comme `test_fermeture_fenetre.py`).

- [ ] **Step 1 : écrire le scénario**

Créer `tests/gui/test_script_arret.py` :

```python
# Arrêt d'un script Lua (sous-projet 5) : « Stop » interrompt une boucle de calcul pure (crochet Lua), la console affiche
# « [Script stopped] » et « Run » redevient actif ; fermer la fenêtre pendant une telle boucle termine GTKTerm (code 0) en
# moins de 10 s (script_engine_shutdown après gtk_main).
from gtktest import *

BOUCLE = 'while true do end\n'


def modifiable(n):
    return n.get_state_set().contains(Atspi.StateType.EDITABLE)


def editeur(app):
    """Éditeur du panneau de scripts : le seul objet texte modifiable visible (le terminal VTE a le rôle « terminal »)."""
    return find1(app, 'text', showing=True, pred=modifiable)


def console(app):
    """Texte des objets texte non modifiables visibles : la console du panneau de scripts."""
    return '\n'.join(get_text(t) or '' for t in find(app, 'text', showing=True, pred=lambda n: not modifiable(n)))


def bouton(app, libelle):
    return find1(app, 'push button', '^%s$' % libelle, showing=True)


def sensible(obj):
    return obj.get_state_set().contains(Atspi.StateType.SENSITIVE)


e = Env('script_arret')
e.write_rc(e.profile())
e.start(); time.sleep(2)
menu(e.app, 'View', 'Script panel'); time.sleep(1.0)
try:
    ed = editeur(e.app)
except LookupError:
    with open(os.path.join(e.root, 'arbre.txt'), 'w') as f:
        dump(e.app, out=f)
    raise
set_text(ed, BOUCLE); time.sleep(0.3)
check(get_text(ed) == BOUCLE, "script saisi dans l'editeur", get_text(ed))

run, stop = bouton(e.app, 'Run'), bouton(e.app, 'Stop')
act(run)
check(wait_until(lambda: sensible(stop) and not sensible(run), 5), 'script en cours : Stop actif, Run inactif')
time.sleep(1.0)
check(e.proc.poll() is None, 'GTKTerm toujours vivant pendant la boucle')
act(stop)
check(wait_until(lambda: '[Script stopped]' in console(e.app), 5), 'console : [Script stopped]', console(e.app)[-300:])
check(wait_until(lambda: sensible(run) and not sensible(stop), 5), 'Run de nouveau actif apres Stop')

# Relance, puis fermeture de la fenêtre par le bouton « Close » de la barre de titre pendant la boucle
act(run)
check(wait_until(lambda: sensible(stop), 5), 'script relance')
time.sleep(1.0)
frame = find1(e.app, 'frame')
t0 = time.time()
act(find1(frame, 'push button', '^Close$', showing=True))
try:
    e.proc.wait(10)
except subprocess.TimeoutExpired:
    pass
duree = time.time() - t0
check(e.proc.poll() is not None, 'GTKTerm termine apres la fermeture de la fenetre', '%.1f s' % duree)
check(e.proc.returncode == 0, 'code de sortie nul', e.proc.returncode)
check(duree < 10, 'fin en moins de 10 s', '%.1f s' % duree)
finish(e, quit=False)
sys.exit(conclude())
```

Dans `tests/gui/meson.build`, liste `gui_scenarios`, ajouter après `'test_fermeture_fenetre',` :

```meson
	'test_script_arret',
```

- [ ] **Step 2 : exécuter le scénario (broadway privé uniquement)**

Run : `ninja -C build && meson test -C build --suite gui test_script_arret --print-errorlogs`
Expected : OK. En cas d'échec : lire `build/tests/gui/runs/script_arret/result.txt` (une ligne OK/ECHEC par vérification), `gtkterm.stderr`, et `arbre.txt` si l'éditeur est introuvable. Ne jamais lancer `./build/src/gtkterm` directement.

- [ ] **Step 3 : non-régression de la suite graphique**

Run : `meson test -C build --suite gui --print-errorlogs`
Expected : tous les scénarios OK (dont `test_fermeture_fenetre`).

- [ ] **Step 4 : commit**

```bash
git add tests/gui/test_script_arret.py tests/gui/meson.build
git commit -m "$(cat <<'EOF'
tests : scénario graphique de l'arrêt d'un script Lua

« Stop » sur une boucle de calcul pure affiche « [Script stopped] » et réactive
« Run » ; la fermeture de la fenêtre pendant une boucle termine GTKTerm avec le
code 0 en moins de 10 s.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 6 : exemples et documentation

**Files :**
- Create : `scripts/11_tcp_server.lua`
- Modify : `scripts/07_tcp_client.lua`, `.claude/skills/lua-script/SKILL.md`, `CLAUDE.md`, `tests/README.md`, `docs/superpowers/reviews/2026-10-06-revue-code.md`

**Interfaces :**
- Consumes : API Lua des tâches 3 et 4. L'annexe « Remarques mineures reportées par le sous-projet 5 » de la revue n'est **pas** écrite ici (le contrôleur l'ajoute à la fin, après la relecture finale).

- [ ] **Step 1 : nouvel exemple**

Créer `scripts/11_tcp_server.lua` :

```lua
-- 11_tcp_server.lua
-- Serveur TCP : attend un client, renvoie chaque ligne reçue (écho), s'arrête sur « quit » ou au bout de 60 s.
-- Pour tester : exécutez ce script, puis dans un terminal :  nc 127.0.0.1 5000
-- « Stop » dans le panneau de scripts arrête le script à tout moment ; le port est alors refermé automatiquement.

local PORT = 5000
local DUREE_MAX_S = 60

local ok, err = gtkterm.open(1, "tcp_server", PORT, "127.0.0.1")
if not ok then
    gtkterm.log("Ouverture du serveur impossible : " .. err)
    return
end
gtkterm.log(string.format("En écoute sur 127.0.0.1:%d (état : %s)", PORT, gtkterm.status(1)))

local debut = gtkterm.time()
local etat_precedent = gtkterm.status(1)
while gtkterm.time() - debut < DUREE_MAX_S do
    local etat = gtkterm.status(1)   -- "listening" sans client, "connected" avec un client
    if etat ~= etat_precedent then
        gtkterm.log("État : " .. etat)
        if etat == "connected" then
            gtkterm.send(1, "Bienvenue ! Tapez « quit » pour arrêter.\n")
        end
        etat_precedent = etat
    end
    if etat == "connected" then
        -- Attente courte : on revient vite voir si le client est parti
        local ligne = gtkterm.wait_for(1, "\n", 500)
        if ligne then
            local texte = ligne:gsub("[\r\n]+$", "")
            gtkterm.log("RX : " .. texte)
            if texte == "quit" then
                gtkterm.send(1, "Au revoir.\n")
                break
            end
            local envoye, raison = gtkterm.send(1, "echo: " .. texte .. "\n")
            if not envoye then
                gtkterm.log("Envoi impossible : " .. raison)   -- "not connected" si le client vient de partir
            end
        end
    else
        gtkterm.sleep(100)
    end
end

gtkterm.close(1)
gtkterm.log("Serveur fermé.")
```

- [ ] **Step 2 : relire les exemples existants**

Relire `scripts/01` à `10` contre l'API (aucun changement de sens attendu : `open` série avec parité/flux numériques, `send`/`send_hex` testés en booléen, `wait_for` testé contre `nil`). Seul `07_tcp_client.lua` change, pour montrer le délai de connexion et la raison : remplacer

```lua
local ok, err = gtkterm.open(1, "tcp_client", HOST, PORT)
if not ok then
    gtkterm.log("Connexion échouée : " .. err)
    return
end
```

par

```lua
-- Délai de connexion : 3 s (5 s par défaut). Échec : false et la raison (« timeout », connexion refusée…).
local ok, err = gtkterm.open(1, "tcp_client", HOST, PORT, 3000)
if not ok then
    gtkterm.log("Connexion échouée : " .. err)
    return
end
```

et, dans `query`, remplacer `gtkterm.send(1, cmd .. "\n")` par :

```lua
    local envoye, raison = gtkterm.send(1, cmd .. "\n")
    if not envoye then
        gtkterm.log("Envoi impossible : " .. raison)
        return nil
    end
```

Run : `meson test -C build lua-syntax --print-errorlogs`
Expected : OK (11 exemples analysés par `luac -p`).

- [ ] **Step 3 : compétence `/lua-script`**

Dans `.claude/skills/lua-script/SKILL.md` :
- remplacer la puce `- Prefer \`gtkterm.log(...)\` over \`print\`. Use \`gtkterm.sleep(ms)\` for pacing.` par `- \`gtkterm.log(...)\` and \`print(...)\` both write to the script console. Use \`gtkterm.sleep(ms)\` for pacing. \`os.exit([code])\` ends the script (never GtkTerm); « Stop » interrupts any script, even a pure computation loop.` ;
- remplacer tout le tableau de la section « Engine API (`gtkterm.*`) » par :

```markdown
Failures return `false` (or `nil`) followed by a stable English reason: `"invalid handle"`, `"already open"`, `"not open"`,
`"not connected"`, `"link lost"`, `"timeout"`, `"closed"`, `"invalid bits"` (`stop`, `parity`, `flow`, `baud`, `port`),
`"invalid hex: …"`, or the transport's error message. A wrong argument type raises a Lua error.

| Call | Returns | Notes |
|------|---------|-------|
| `log(value)` / `print(...)` | – | write to the GtkTerm script console (`print`: tab-separated, like standard Lua) |
| `sleep(ms)` | – | block the script ms milliseconds; interrupted at once by « Stop » |
| `time()` | number | monotonic time in seconds, for timing/duration |
| `send(handle, str [, timeout_ms=5000])` | `true` \| `false, err` | everything written or failure: partial writes retried until the timeout (`"timeout"`); nothing writable → `"not connected"`; lost → `"link lost"`; handle 0 = main port |
| `send_hex(handle, "DE AD BE EF" [, timeout_ms])` | `true` \| `false, err` | hex pairs, whitespace between bytes only; invalid text → `false, "invalid hex: …"`, nothing sent |
| `read(handle)` | string | drain everything currently buffered (may be ""); still works on a lost port |
| `wait_for(handle, pattern [, timeout_ms=5000])` | string \| `nil, "timeout"` \| `nil, "closed"` | data up to and including pattern (binary safe); lost/closed port → `"closed"` at once |
| `status(handle)` | string | `"open"`, `"connecting"`, `"listening"`, `"connected"`, `"lost"`, `"closed"` (handle 0: state of the main link) |
| `open(handle, "serial", dev [, baud=9600, bits=8, stop=1, parity=0, flow=0])` | `true` \| `false, err` | any baud the port accepts; parity `0/1/2` or `"none"/"odd"/"even"`, flow `0/1/2` or `"none"/"xon"/"rts"`; port locked |
| `open(handle, "tcp_client", host, port [, timeout_ms=5000])` | `true` \| `false, err` | IPv4/IPv6; waits for the connection (`"timeout"`) |
| `open(handle, "tcp_server", port [, host])` | `true` \| `false, err` | optional listen address; a new client replaces the previous one; `status()` tells whether a client is connected |
| `close(handle)` | – | no effect on a closed port; every port is closed automatically when the script ends or is stopped |
| `clear(handle)` | – | empty the receive buffer of handle (not the terminal) |
| `save_csv(file, col1, col2, ...)` | ok, err | append a CSV row; first call with header strings |
| `send_macro(index_or_label, ...args)` | ok, err | send a configured macro (0-based index or label); true once actually sent |
| `get_macros()` | table | `{ {index=, label=, ...}, ... }` |
| `get_lists()` | table | configured macro lists |

Receive buffers hold 1 MiB per port; on overflow the oldest bytes are dropped and the console says so once per port and run.
« Stop » cannot interrupt a blocking standard-library call (`io.read`, `os.execute`).
```

- [ ] **Step 4 : `CLAUDE.md`**

1. Paragraphe **Tests** : dans la liste des modules sans GTK, remplacer `` `config_store.c`, `macros_format.c` `` par `` `config_store.c`, `rx_queue.c`, `script_port.c`, `macros_format.c` ``.
2. Paragraphe **Layers (sub-project 2)** : remplacer `` `profile.c`, `config_store.c`, plus the older macro/baud/i18n helpers) `` par `` `profile.c`, `config_store.c`, `rx_queue.c`, `script_port.c`, plus the older macro/baud/i18n helpers) ``.
3. Remplacer tout le paragraphe commençant par `**Lua scripting (\`script_engine.c\` + \`script_panel.c\`):**` par :

```markdown
**Lua scripting (`script_engine.c` + `script_panel.c`, sub-project 5):** scripts run on a **background GThread** (`g_thread_new("lua-script", …)`). The Lua thread never touches GTK, a transport or the link: it only uses the receive queues (`GtRxQueue`, `rx_queue.c`: locked, 1 MiB, oldest bytes dropped, overflow reported once per port and run) and the atomic port state, and runs everything else on the main thread through `run_on_main_sync` (direct call when invoked from the thread owning the default context). Lua ports 1–15 are `GtScriptPort` (`script_port.c`, core: one `GtTransport` + one `GtRxQueue` each, opened/closed/written on the main thread; the port table is read and written by the Lua thread only); port 0 is the link, observed through its `received` signal (connected/disconnected on the main thread). `send`/`send_hex` write everything or fail with a reason (partial writes and `EAGAIN` retried on the Lua thread with a 2 ms pause, timeout); failures return `false`/`nil` plus a stable English reason. « Stop » sets an atomic flag: a count hook (`lua_sethook`, every 1000 instructions, then every instruction once stopping) raises a sentinel error that a script's `pcall` cannot keep swallowing, and blocking calls (`sleep` on a `GCond`, `wait_for`, connect wait, send retries) check it at least every 20 ms; `os.exit` ends the script (never GTKTerm), `print` writes to the console, the script runs inside `lua_pcall` of a C function. Log text is batched in a locked buffer flushed by a single source at `G_PRIORITY_DEFAULT_IDLE` (20 ms timer, 1 MiB cap with a dropped-bytes line); the done idle flushes it, joins the thread, then reports `GtScriptEnd` (finished / error / stopped). `script_engine_shutdown()` (called by `main` after `gtk_main()`) stops a running script and pumps the default context until its thread ends (≤ 3 s). `script_panel.c` is the editor/console UI (GtkSourceView 4, console capped at 10 000 lines). **When adding a `gtkterm.*` function: work on the Lua thread, run any GTK/transport/link side effect on the main thread via `run_on_main_sync`, never hold a queue lock across it, and make any blocking wait check `stop_requested` at least every 20 ms.** Example scripts live in `scripts/` (use the `/lua-script` skill to scaffold new ones).
```

- [ ] **Step 5 : `tests/README.md`**

1. Dans la puce « Tests unitaires du cœur GLib », remplacer `` `test_config_store`. `` par `` `test_config_store`, `test_rx_queue` (file de réception des ports Lua), `test_script_port` (port Lua sur le transport, paire pty et TCP local, décodage hexadécimal). ``
2. Dans la puce « Tests d'intégration », après `` `test_script_engine` `` ajouter : `` (moteur Lua réel : ports sur pty et TCP local, envois complets, raisons, « Stop » sur boucle de calcul, `os.exit`, journal regroupé, `script_engine_shutdown` ; le bouchon de la liaison simule son état par `stub_link_set_state`) ``.
3. Dans la section « Suite graphique », à la fin de la puce sur `test_macros_send_affichage.py`, ajouter la puce : `` - `test_script_arret.py` : « Stop » sur `while true do end` (console « [Script stopped] », « Run » réactivé) puis fermeture de la fenêtre pendant la boucle (code 0 en moins de 10 s). ``

- [ ] **Step 6 : revue de code**

Dans `docs/superpowers/reviews/2026-10-06-revue-code.md` :

1. Sous la ligne `- M25. « Stop » n'arrête pas le script …`, ajouter la ligne :
`  Corrigé par le sous-projet 5 (`feature/refonte-lua`) : crochet Lua (`lua_sethook`) relevé après un `pcall`, `sleep` et attentes du moteur interruptibles, `os.exit` termine le script, `lua_atpanic` installé, exécution sous `lua_pcall` ; essais `test_script_engine` et `test_script_arret.py`.`
2. Sous la ligne `- M28. Tampon de réception plein : …`, ajouter :
`  Corrigé par le sous-projet 5 (`feature/refonte-lua`) : file de 1 Mio (`GtRxQueue`) qui jette les plus anciens octets et le signale une fois par port et par exécution ; débits du transport (non standard compris), débit impossible refusé ; écritures partielles et `EAGAIN` repris jusqu'au délai ; `send_hex` refuse un texte invalide sans rien envoyer.`
3. Point d'architecture 6 (ligne commençant par `6. **Fil Lua**`) : ajouter à la fin de la ligne ` — Traité : `send_serial` par le sous-projet 2, macros par le sous-projet 4 ; ports Lua sur `GtScriptPort`/`GtTransport` (état atomique, files verrouillées, plus de termios/TCP dans le moteur) par le sous-projet 5 (`feature/refonte-lua`).`
4. Section 6 « Code mort et duplication » : après `` `L_global`, `chan_err` (Lua) `` ajouter ` (retirés par le sous-projet 5)` ; après `termios/TCP du moteur Lua ≈ transport.c` ajouter ` (retiré par le sous-projet 5)`.
5. Annexe du sous-projet 1, puce **Tâche 8** : après `g_source_remove sur source déjà retirée` ajouter ` (traité par le sous-projet 5 : ports sur GtScriptPort, transport fermé sur le thread principal)` ; après `pas de garde contre run_on_main_sync appelé du thread principal` ajouter ` (traité par le sous-projet 5)` ; après `fermeture de l'appli pendant run_on_main_sync → thread abandonné (pas d'interblocage)` ajouter ` (traité par le sous-projet 5 : script_engine_shutdown)` ; après `fuites sur erreur mémoire Lua (longjmp)` ajouter ` (reporté, hors périmètre du sous-projet 5)`.
6. Annexe du sous-projet 1, puce **Relecture finale** : après `peut figer le rafraîchissement (Stop reste opérant)` ajouter ` (traité par le sous-projet 5 : journal regroupé à G_PRIORITY_DEFAULT_IDLE, plafonné à 1 Mio)`.

- [ ] **Step 7 : vérification d'ensemble**

Run : `ninja -C build && meson test -C build --print-errorlogs && meson test -C build-asan --print-errorlogs`
Expected : tout OK.

Run : `git diff --stat`
Expected : seuls les fichiers de cette tâche.

- [ ] **Step 8 : commit**

```bash
git add scripts/11_tcp_server.lua scripts/07_tcp_client.lua .claude/skills/lua-script/SKILL.md CLAUDE.md tests/README.md docs/superpowers/reviews/2026-10-06-revue-code.md
git commit -m "$(cat <<'EOF'
docs : moteur Lua (exemple serveur TCP, compétence lua-script, CLAUDE.md, revue)

Nouvel exemple 11 (serveur TCP, status, écho, arrêt propre), délai de connexion et
raison d'échec dans l'exemple 07 ; tableau de l'API de /lua-script à jour (status,
délais, raisons, print, os.exit, Stop) ; architecture du moteur dans CLAUDE.md ;
nouveaux tests dans tests/README.md ; M25, M28 et remarques Lua de la revue annotés.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```
