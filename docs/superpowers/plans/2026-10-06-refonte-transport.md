# Cœur transport sans GTK, historique brut, envoi et écho — plan d'implémentation (sous-projet 2)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** séparer le transport de l'interface, gérer la connexion du port principal par une seule machine d'état (`GtkTermLink`), garder les octets bruts et les envois dans un historique étiqueté, faire passer tous les envois par un seul chemin et redessiner l'écho à sa place.

**Architecture :** trois couches. Le **cœur** (`gtkterm_core`, GLib seule) reçoit `transport.c` par instance, `history.c`, `rx_transform.c`, `device_match.c` et `view_render.c` (rendu pur des vues ASCII/hexadécimale, testable sans fenêtre). La **liaison** (`link.c`, GObject + GUdev) possède le transport, l'historique, la reconnexion et le moniteur udev ; elle émet `received`, `sent`, `open-failed`, `history-cleared` et notifie `state` / `status-text`. L'**interface** GTK 3 s'abonne à la liaison et n'envoie plus que par `gt_link_send()`. `serial.c`, `buffer.c` et `device_monitor.c` disparaissent.

**Tech Stack :** C (gnu11), GLib ≥ 2.70 / GObject / GIO, GUdev ≥ 230, GTK 3 + VTE 2.91 (interface seulement), Meson ≥ 1.0 + Ninja, GLib GTest (TAP), Python 3 + AT-SPI + broadwayd pour la suite graphique.

**Spec :** `docs/superpowers/specs/2026-10-06-refonte-transport-design.md`

## Global Constraints

- `transport.c`, `history.c`, `rx_transform.c`, `device_match.c`, `view_render.c` sont dans `gtkterm_core` et n'incluent aucun en-tête GTK, VTE ou d'interface ; `link.c` n'inclut ni GTK ni VTE (critère d'acceptation 3).
- Plus aucun appel à `show_message`, `Set_status_message` ou `Set_window_title` depuis le transport ou la liaison (critère 4).
- Aucun changement de format de `.gtktermrc` ni d'option de ligne de commande (critère 5) : `parite` reste 0/1/2, `flux` reste 0/1/2/3.
- Chaînes d'interface : identifiants gettext **en anglais** (comme tout le projet), traduction française ajoutée dans `po/fr.po` (tâche 10). Les messages d'erreur d'ouverture reprennent **à l'identique** les identifiants existants (`"Cannot open %s: %s\n"`, etc.).
- Style : respecter celui du fichier modifié. Nouveaux fichiers C : style dominant du projet (tabulations, accolades Allman, `if(` sans espace, type de retour et nom sur la même ligne) ; `terminal_display.c` garde son style GNU à 2 espaces. Lignes jusqu'à 200 colonnes. Commentaires en français.
- Messages des tests (`g_test_message`, libellés d'assertion, `g_test_skip`) en ASCII : les tests tournent en locale C.
- Tests : jamais de vrai port série (pty seulement), jamais la vraie configuration de l'utilisateur ; la suite graphique tourne sur un affichage broadway privé, jamais sur l'écran de l'utilisateur.
- Chaque commit se termine par la ligne `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Messages de commit en français. Branche : `feature/refonte-transport`.
- Construction : `ninja -C build` ; tests : `meson test -C build --print-errorlogs` (suites `unit` et `lint`) ; la suite `gui` se lance avec `meson test -C build --suite gui` (tâche 9).

## Décisions prises par ce plan (écarts motivés à la lettre de la spec)

1. **État `GT_LINK_CONNECTING`** ajouté à `GtLinkState` : client TCP dont la connexion demandée par l'utilisateur n'est pas encore établie. La barre d'état affiche « … — connecting… » au lieu de se dire connectée trop tôt (M5) ; un échec asynchrone de cette connexion émet `open-failed` puis passe à `CLOSED`, comme un échec synchrone.
2. **Signaux à `GBytes`** : `received (GBytes *data, gint64 wall_time_us)`, `sent (GBytes *data, gint origin)`. L'heure de réception accompagne `received` pour que la vue et le journal horodatent avec la même heure que l'historique.
3. **`view_render.c`** (cœur, pur) : le rendu des vues ASCII et hexadécimale (écho compris) est sorti de `terminal_display.c` pour être testé sans fenêtre ; `terminal_display.c` ne fait plus qu'alimenter VTE.
4. **LOST différé** : le transport signale `LOST` depuis une source d'inactivité, jamais pendant un appel au transport ; la liaison peut donc fermer le transport dans ce rappel. `on_data`, `CONNECTED`, `CLIENT_CONNECTED` et `CLIENT_GONE` restent signalés depuis la surveillance du descripteur, et leur destinataire ne ferme pas le transport.
5. **Première ligne horodatée** : `GtRxTransform` démarre « en début de ligne » ; l'ancien `put_chars` n'horodatait pas la toute première ligne (oubli).
6. **Colonne ASCII de la vue hexadécimale** : un octet est affiché s'il est dans 0x20–0x7E, sinon « . » (l'ancien code affichait 0x7F et masquait ≥ 0x80 par effet du `char` signé).
7. **Sauvegarde ASCII** : le filtre « > 0x1F, CR, LF » s'applique à l'octet non signé ; les octets ≥ 0x80 (UTF-8) sont gardés (l'ancien code les retirait par effet du `char` signé).
8. **Pont provisoire** : à la tâche 5, `serial.c` devient un pont de l'ancienne façade vers le nouveau transport, pour que l'application se construise et fonctionne jusqu'à la tâche 7 qui le supprime.
9. **Non-régression graphique** : des scénarios EM du sous-projet 1, seuls ceux qui passent par le chemin d'envoi refait sont repris avec assertions (envoi de fichier avec et sans caractère d'attente, raccourcis de macros, polling). Les autres concernaient des dialogues que ce sous-projet ne touche pas.

## Review Focus

1. **Perte du port pendant un envoi de fichier ou une attente de caractère** : le transfert doit s'arrêter avec un message, sans boucle ni blocage. Épinglé par `test_link` (envoi après perte → 0, descripteur → -1) et par la garde de `add_input()` (tâche 7).
2. **Destruction ou fermeture de la liaison pendant `RECONNECTING` ou `CONNECTING`** : aucune source GLib orpheline, aucun accès après libération. Épinglé par `test_link` (`/link/destruction-pendant-reconnexion`, `/link/fermeture-pendant-connexion`), sous ASan à la tâche 10.
3. **Réception octet par octet (débit lent)** : l'historique se remplit de petits enregistrements ; la capacité doit tenir et le rejeu au changement de vue doit rester identique au direct. Épinglé par `test_history` (`/history/nombreux-petits-enregistrements`) et `test_view_render` (`/view_render/rejeu-egal-direct-grand`).
4. **Option CR/LF ou horodatage changée en cours de ligne** (journal en cours, ligne commencée) : l'état de ligne est conservé. Épinglé par `test_rx_transform` (`/rx_transform/options-changees-en-cours`).
5. **Octets non imprimables ou non UTF-8** (0x00, 0x7F, ≥ 0x80) dans les vues et le transport : affichés « . » en hexadécimal, transmis intacts. Épinglé par `test_view_render` (`/view_render/hexa-colonne-ascii`) et `test_transport` (`/transport/octets-binaires`).

## Carte des fichiers

| Fichier | Tâche | Rôle |
|---|---|---|
| `src/history.c/.h` (créés) | 1 | Historique RX/TX étiqueté, capacité 128 Ko |
| `src/rx_transform.c/.h` (créés) | 2 | CR/LF automatique et horodatage, pur |
| `src/device_match.c/.h` (créés) | 3 | Correspondance d'un événement udev avec le port, pure |
| `src/view_render.c/.h` (créés) | 4 | Rendu ASCII/hexadécimal et écho vers un `GString`, pur |
| `src/transport.c/.h` (réécrits) | 5 | Transport par instance, `GError`, rappels ; table des débits |
| `src/serial.c/.h` | 5 (pont), 7 (supprimés) | — |
| `src/term_config.c/.h`, `src/cmdline.c` | 5, 7 | Enums, `config_to_transport_params`, `configure_*` |
| `src/link.c/.h` (créés) | 6 | `GtkTermLink` |
| `src/gtkterm.c`, `src/interface.c/.h`, `src/user_signals.c` | 7, 8 | Démarrage, abonnement à la liaison |
| `src/device_monitor.c/.h` | 7 (supprimés) | — |
| `src/terminal_display.c/.h`, `src/logging.c/.h`, `src/files.c/.h`, `src/macros.c`, `src/script_engine.c` | 7, 8 | Consommateurs |
| `src/buffer.c/.h` | 8 (supprimés) | — |
| `tests/test_history.c`, `test_rx_transform.c`, `test_device_match.c`, `test_view_render.c`, `test_link.c` (créés), `test_transport.c` (réécrit) | 1–6 | Tests unitaires et d'intégration |
| `tests/stubs/stub_link.c` (créé), `stub_display.c` (supprimé) | 8 | Bouchon de la liaison |
| `tests/gui/` (créé) | 9 | Suite graphique |
| `po/POTFILES.in`, `po/fr.po`, `CLAUDE.md`, `tests/README.md`, revue | 10 | Documentation, traductions |

---

### Task 1 : historique brut étiqueté (`GtHistory`)

**Files :**
- Create : `src/history.h`, `src/history.c`
- Create : `tests/test_history.c`
- Modify : `src/meson.build` (liste `core_sources`), `tests/meson.build` (dictionnaire `unit_tests`)

**Interfaces :**
- Consumes : rien.
- Produces :
  ```c
  typedef enum { GT_RECORD_RX, GT_RECORD_TX } GtRecordKind;
  typedef enum { GT_SEND_KEYBOARD, GT_SEND_MACRO, GT_SEND_LUA, GT_SEND_HEX, GT_SEND_FILE } GtSendOrigin;
  typedef struct { GtRecordKind kind; GtSendOrigin origin; gint64 wall_time_us; const guint8 *data; gsize len; } GtRecord;
  typedef struct _GtHistory GtHistory;
  typedef void (*GtHistoryFunc)(const GtRecord *record, gpointer user_data);
  #define GT_HISTORY_DEFAULT_CAPACITY (128 * 1024)
  GtHistory *gt_history_new(gsize capacity);
  void       gt_history_free(GtHistory *h);
  void       gt_history_append(GtHistory *h, GtRecordKind kind, GtSendOrigin origin, gint64 wall_time_us, const guint8 *data, gsize len);
  void       gt_history_foreach(GtHistory *h, GtHistoryFunc func, gpointer user_data);
  void       gt_history_clear(GtHistory *h);
  GBytes    *gt_history_copy_rx(GtHistory *h);
  gsize      gt_history_size(GtHistory *h);
  guint      gt_history_count(GtHistory *h);
  ```

- [ ] **Step 1 : écrire le test**

`tests/test_history.c` :

```c
/* Tests unitaires de l'historique brut (history.c) : ordre, capacité, copie des octets reçus. */

#include <glib.h>
#include <string.h>

#include "history.h"

/* Résumé « RX:ab|TX1:c » (TX suivi du numéro d'origine) ; à libérer avec g_free. */
static void
resumer(const GtRecord *rec, gpointer user_data)
{
	GString *s = user_data;

	if(s->len > 0)
		g_string_append_c(s, '|');
	if(rec->kind == GT_RECORD_RX)
		g_string_append(s, "RX:");
	else
		g_string_append_printf(s, "TX%d:", (gint)rec->origin);
	g_string_append_len(s, (const gchar *)rec->data, rec->len);
}

static gchar *
resume(GtHistory *h)
{
	GString *s = g_string_new(NULL);

	gt_history_foreach(h, resumer, s);
	return g_string_free(s, FALSE);
}

static void
ajouter(GtHistory *h, GtRecordKind kind, GtSendOrigin origin, const gchar *texte)
{
	gt_history_append(h, kind, origin, 0, (const guint8 *)texte, strlen(texte));
}

static void
test_ajout_et_ordre(void)
{
	GtHistory *h = gt_history_new(1024);
	gchar *s;

	ajouter(h, GT_RECORD_RX, GT_SEND_KEYBOARD, "ab");
	ajouter(h, GT_RECORD_TX, GT_SEND_MACRO, "c");
	ajouter(h, GT_RECORD_RX, GT_SEND_KEYBOARD, "de");
	s = resume(h);
	g_assert_cmpstr(s, ==, "RX:ab|TX1:c|RX:de");
	g_assert_cmpuint(gt_history_size(h), ==, 5);
	g_assert_cmpuint(gt_history_count(h), ==, 3);
	g_free(s);
	gt_history_free(h);
}

static void
test_capacite_retire_les_plus_anciens(void)
{
	GtHistory *h = gt_history_new(10);
	gchar *s;

	ajouter(h, GT_RECORD_RX, GT_SEND_KEYBOARD, "12345");
	ajouter(h, GT_RECORD_TX, GT_SEND_HEX, "678");
	ajouter(h, GT_RECORD_RX, GT_SEND_KEYBOARD, "9AB");
	s = resume(h);
	g_assert_cmpstr(s, ==, "TX3:678|RX:9AB");
	g_assert_cmpuint(gt_history_size(h), ==, 6);
	g_free(s);
	gt_history_free(h);
}

static void
test_enregistrement_plus_grand_que_la_capacite(void)
{
	GtHistory *h = gt_history_new(4);
	gchar *s;

	ajouter(h, GT_RECORD_RX, GT_SEND_KEYBOARD, "xy");
	ajouter(h, GT_RECORD_RX, GT_SEND_KEYBOARD, "abcdef");
	s = resume(h);
	g_assert_cmpstr(s, ==, "RX:cdef");
	g_assert_cmpuint(gt_history_size(h), ==, 4);
	g_free(s);
	gt_history_free(h);
}

static void
test_copy_rx_sans_tx(void)
{
	GtHistory *h = gt_history_new(1024);
	GBytes *rx;

	ajouter(h, GT_RECORD_RX, GT_SEND_KEYBOARD, "ab");
	ajouter(h, GT_RECORD_TX, GT_SEND_KEYBOARD, "XY");
	ajouter(h, GT_RECORD_RX, GT_SEND_KEYBOARD, "cd");
	rx = gt_history_copy_rx(h);
	g_assert_cmpmem(g_bytes_get_data(rx, NULL), g_bytes_get_size(rx), "abcd", 4);
	g_bytes_unref(rx);
	gt_history_free(h);
}

static void
test_vider(void)
{
	GtHistory *h = gt_history_new(1024);
	GBytes *rx;
	gchar *s;

	ajouter(h, GT_RECORD_RX, GT_SEND_KEYBOARD, "ab");
	gt_history_clear(h);
	g_assert_cmpuint(gt_history_count(h), ==, 0);
	g_assert_cmpuint(gt_history_size(h), ==, 0);
	rx = gt_history_copy_rx(h);
	g_assert_cmpuint(g_bytes_get_size(rx), ==, 0);
	g_bytes_unref(rx);
	ajouter(h, GT_RECORD_RX, GT_SEND_KEYBOARD, "z");
	s = resume(h);
	g_assert_cmpstr(s, ==, "RX:z");
	g_free(s);
	gt_history_free(h);
}

static void
test_nombreux_petits_enregistrements(void)
{
	GtHistory *h = gt_history_new(1000);
	GBytes *rx;
	const guint8 *p;
	gsize n;

	for(guint i = 0; i < 5000; i++)
	{
		guint8 octet = (guint8)(i % 256);

		gt_history_append(h, GT_RECORD_RX, GT_SEND_KEYBOARD, i, &octet, 1);
	}
	g_assert_cmpuint(gt_history_count(h), ==, 1000);
	g_assert_cmpuint(gt_history_size(h), ==, 1000);
	rx = gt_history_copy_rx(h);
	p = g_bytes_get_data(rx, &n);
	g_assert_cmpuint(n, ==, 1000);
	for(guint i = 0; i < 1000; i++)
		g_assert_cmpuint(p[i], ==, (4000 + i) % 256);
	g_bytes_unref(rx);
	gt_history_free(h);
}

static void
relever_heure(const GtRecord *rec, gpointer user_data)
{
	*(gint64 *)user_data = rec->wall_time_us;
}

static void
test_heure_conservee(void)
{
	GtHistory *h = gt_history_new(16);
	gint64 heure = 0;

	gt_history_append(h, GT_RECORD_RX, GT_SEND_KEYBOARD, 123456789, (const guint8 *)"a", 1);
	gt_history_foreach(h, relever_heure, &heure);
	g_assert_cmpint(heure, ==, 123456789);
	gt_history_free(h);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/history/ajout-et-ordre", test_ajout_et_ordre);
	g_test_add_func("/history/capacite-retire-les-plus-anciens", test_capacite_retire_les_plus_anciens);
	g_test_add_func("/history/enregistrement-plus-grand-que-la-capacite", test_enregistrement_plus_grand_que_la_capacite);
	g_test_add_func("/history/copy-rx-sans-tx", test_copy_rx_sans_tx);
	g_test_add_func("/history/vider", test_vider);
	g_test_add_func("/history/nombreux-petits-enregistrements", test_nombreux_petits_enregistrements);
	g_test_add_func("/history/heure-conservee", test_heure_conservee);
	return g_test_run();
}
```

Dans `tests/meson.build`, ajouter au dictionnaire `unit_tests` la ligne `'test_history' : [],`.

- [ ] **Step 2 : constater l'échec**

Run : `ninja -C build 2>&1 | tail -5`
Expected : échec de compilation, `history.h: No such file or directory`.

- [ ] **Step 3 : écrire l'implémentation**

`src/history.h` :

```c
/* Historique brut du port principal : octets reçus (RX) et envoyés (TX), dans l'ordre, avec leur heure (sous-projet 2). */
#ifndef HISTORY_H_
#define HISTORY_H_

#include <glib.h>

typedef enum
{
	GT_RECORD_RX,
	GT_RECORD_TX
} GtRecordKind;

/* Origine d'un envoi : décide de l'affichage de l'écho (frappe dans la ligne, le reste sur une ligne « > »). */
typedef enum
{
	GT_SEND_KEYBOARD,
	GT_SEND_MACRO,
	GT_SEND_LUA,
	GT_SEND_HEX,
	GT_SEND_FILE
} GtSendOrigin;

typedef struct
{
	GtRecordKind kind;
	GtSendOrigin origin;		/* TX seulement */
	gint64 wall_time_us;		/* heure de réception ou d'envoi (g_get_real_time) */
	const guint8 *data;
	gsize len;
} GtRecord;

typedef struct _GtHistory GtHistory;
typedef void (*GtHistoryFunc)(const GtRecord *record, gpointer user_data);

#define GT_HISTORY_DEFAULT_CAPACITY (128 * 1024)

GtHistory *gt_history_new(gsize capacity);
void       gt_history_free(GtHistory *h);
/* Ajoute un enregistrement ; retire les plus anciens en entier au-delà de la capacité (octets utiles). Un enregistrement
   plus grand que la capacité n'en garde que la fin. */
void       gt_history_append(GtHistory *h, GtRecordKind kind, GtSendOrigin origin, gint64 wall_time_us, const guint8 *data, gsize len);
/* Du plus ancien au plus récent ; func ne doit pas modifier l'historique. */
void       gt_history_foreach(GtHistory *h, GtHistoryFunc func, gpointer user_data);
void       gt_history_clear(GtHistory *h);
/* Octets reçus seulement, dans l'ordre (sauvegarde brute). */
GBytes    *gt_history_copy_rx(GtHistory *h);
gsize      gt_history_size(GtHistory *h);
guint      gt_history_count(GtHistory *h);

#endif
```

`src/history.c` :

```c
/***********************************************************************/
/* history.c                                                           */
/*                                                                     */
/*   Purpose                                                           */
/*      Historique brut du port principal (sous-projet 2)              */
/*                                                                     */
/***********************************************************************/

#include <string.h>

#include "history.h"

typedef struct
{
	GtRecordKind kind;
	GtSendOrigin origin;
	gint64 wall_time_us;
	gsize len;
	guint8 data[];
} HistoryEntry;

struct _GtHistory
{
	gsize capacity;
	gsize size;		/* octets utiles retenus */
	GQueue entries;		/* HistoryEntry *, du plus ancien au plus récent */
};

GtHistory *gt_history_new(gsize capacity)
{
	GtHistory *h = g_new0(GtHistory, 1);

	h->capacity = capacity > 0 ? capacity : 1;
	g_queue_init(&h->entries);
	return h;
}

void gt_history_free(GtHistory *h)
{
	if(h == NULL)
		return;
	gt_history_clear(h);
	g_free(h);
}

void gt_history_append(GtHistory *h, GtRecordKind kind, GtSendOrigin origin, gint64 wall_time_us, const guint8 *data, gsize len)
{
	HistoryEntry *e;

	if(len == 0)
		return;
	if(len > h->capacity)
	{
		/* Plus grand que la capacité : seule la fin est gardée */
		data += len - h->capacity;
		len = h->capacity;
	}
	while(h->size + len > h->capacity)
	{
		HistoryEntry *old = g_queue_pop_head(&h->entries);

		h->size -= old->len;
		g_free(old);
	}
	e = g_malloc(sizeof(HistoryEntry) + len);
	e->kind = kind;
	e->origin = origin;
	e->wall_time_us = wall_time_us;
	e->len = len;
	memcpy(e->data, data, len);
	g_queue_push_tail(&h->entries, e);
	h->size += len;
}

void gt_history_foreach(GtHistory *h, GtHistoryFunc func, gpointer user_data)
{
	for(GList *l = h->entries.head; l != NULL; l = l->next)
	{
		HistoryEntry *e = l->data;
		GtRecord rec = { e->kind, e->origin, e->wall_time_us, e->data, e->len };

		func(&rec, user_data);
	}
}

void gt_history_clear(GtHistory *h)
{
	HistoryEntry *e;

	while((e = g_queue_pop_head(&h->entries)) != NULL)
		g_free(e);
	h->size = 0;
}

GBytes *gt_history_copy_rx(GtHistory *h)
{
	GByteArray *a = g_byte_array_sized_new((guint)h->size);

	for(GList *l = h->entries.head; l != NULL; l = l->next)
	{
		HistoryEntry *e = l->data;

		if(e->kind == GT_RECORD_RX)
			g_byte_array_append(a, e->data, (guint)e->len);
	}
	return g_byte_array_free_to_bytes(a);
}

gsize gt_history_size(GtHistory *h)
{
	return h->size;
}

guint gt_history_count(GtHistory *h)
{
	return h->entries.length;
}
```

Dans `src/meson.build`, ajouter à `core_sources` (après `'config_file.h',`) :

```meson
	'history.c',
	'history.h',
```

- [ ] **Step 4 : constater la réussite**

Run : `ninja -C build && meson test -C build test_history --print-errorlogs`
Expected : `1/1 ... test_history OK`.

- [ ] **Step 5 : suite complète et commit**

Run : `meson test -C build --print-errorlogs` — Expected : aucun échec.

```bash
git add src/history.c src/history.h src/meson.build tests/test_history.c tests/meson.build
git commit -m "cœur : historique brut étiqueté RX/TX (GtHistory)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2 : transformation à la lecture (`GtRxTransform`)

**Files :**
- Create : `src/rx_transform.h`, `src/rx_transform.c`, `tests/test_rx_transform.c`
- Modify : `src/meson.build` (`core_sources`), `tests/meson.build` (`unit_tests`)

**Interfaces :**
- Consumes : rien.
- Produces :
  ```c
  typedef struct _GtRxTransform GtRxTransform;
  GtRxTransform *gt_rx_transform_new(gboolean crlf_auto, gboolean timestamp);
  void gt_rx_transform_free(GtRxTransform *tr);
  void gt_rx_transform_set_options(GtRxTransform *tr, gboolean crlf_auto, gboolean timestamp);   /* garde l'état de ligne */
  void gt_rx_transform_reset(GtRxTransform *tr);                                                 /* début de ligne, aucun CR en attente */
  void gt_rx_transform_feed(GtRxTransform *tr, const guint8 *data, gsize len, gint64 wall_time_us, GString *out);
  void gt_rx_transform_format_timestamp(gint64 wall_time_us, GString *out);
  ```

Règles (reprises de l'ancien `put_chars` de `buffer.c`, décision 5 du plan pour la première ligne) — pour chaque octet `c` :
1. si CR/LF automatique : `c == '\r'` → si un CR était en attente, émettre `'\n'` et demander un horodatage ; CR en attente. Sinon : `c == '\n'` sans CR en attente → émettre `'\r'` ; caractère ordinaire après un CR → émettre `'\n'` et demander un horodatage ; plus de CR en attente ;
2. si un horodatage est demandé : l'écrire (si l'option est active) avec l'heure passée en argument, puis effacer la demande ;
3. `c == '\n'` → demander un horodatage ;
4. émettre `c`.

- [ ] **Step 1 : écrire le test**

`tests/test_rx_transform.c` :

```c
/* Tests unitaires de la transformation à la lecture (rx_transform.c) : CR/LF automatique et horodatage. */

#include <glib.h>
#include <string.h>

#include "rx_transform.h"

/* 1 jour 02:03:04.005 -> "[1.02h.03m.04s.005] " ; T2 = une seconde plus tard */
#define T1 ((((gint64)1 * 86400 + 2 * 3600 + 3 * 60 + 4) * G_USEC_PER_SEC) + 5000)
#define T2 (T1 + G_USEC_PER_SEC)
#define H1 "[1.02h.03m.04s.005] "
#define H2 "[1.02h.03m.05s.005] "

static gchar *
nourrir(GtRxTransform *tr, const gchar *data, gsize len, gint64 heure)
{
	GString *out = g_string_new(NULL);

	gt_rx_transform_feed(tr, (const guint8 *)data, len, heure, out);
	return g_string_free(out, FALSE);
}

static void
verifier(GtRxTransform *tr, const gchar *entree, gint64 heure, const gchar *attendu)
{
	gchar *s = nourrir(tr, entree, strlen(entree), heure);

	g_assert_cmpstr(s, ==, attendu);
	g_free(s);
}

static void
test_format_horodatage(void)
{
	GString *s = g_string_new(NULL);

	gt_rx_transform_format_timestamp(T1, s);
	g_assert_cmpstr(s->str, ==, H1);
	g_string_free(s, TRUE);
}

static void
test_crlf_cas(void)
{
	static const struct { const gchar *entree; const gchar *attendu; } cas[] = {
		{ "a\rb", "a\r\nb" },
		{ "a\nb", "a\r\nb" },
		{ "a\r\nb", "a\r\nb" },
		{ "a\r\rb", "a\r\n\r\nb" },
	};

	for(guint i = 0; i < G_N_ELEMENTS(cas); i++)
	{
		GtRxTransform *tr = gt_rx_transform_new(TRUE, FALSE);

		verifier(tr, cas[i].entree, T1, cas[i].attendu);
		gt_rx_transform_free(tr);
	}
}

static void
test_crlf_coupe_entre_deux_appels(void)
{
	GtRxTransform *tr = gt_rx_transform_new(TRUE, FALSE);

	verifier(tr, "a\r", T1, "a\r");
	verifier(tr, "b", T1, "\nb");
	verifier(tr, "c\r", T1, "c\r");
	verifier(tr, "\nd", T1, "\nd");
	gt_rx_transform_free(tr);
}

static void
test_horodatage_debut_de_ligne(void)
{
	GtRxTransform *tr = gt_rx_transform_new(FALSE, TRUE);

	verifier(tr, "x\ny", T1, H1 "x\n" H1 "y");
	gt_rx_transform_free(tr);
}

static void
test_horodatage_ligne_coupee(void)
{
	GtRxTransform *tr = gt_rx_transform_new(FALSE, TRUE);

	verifier(tr, "ab\n", T1, H1 "ab\n");
	verifier(tr, "cd", T2, H2 "cd");
	verifier(tr, "ef", T2, "ef");
	gt_rx_transform_free(tr);
}

static void
test_crlf_et_horodatage(void)
{
	GtRxTransform *tr = gt_rx_transform_new(TRUE, TRUE);

	verifier(tr, "a\rb", T1, H1 "a\r\n" H1 "b");
	gt_rx_transform_free(tr);
}

static void
test_options_desactivees(void)
{
	GtRxTransform *tr = gt_rx_transform_new(FALSE, FALSE);
	gchar *s = nourrir(tr, "\x00\xff\r\n", 4, T1);

	g_assert_cmpmem(s, 4, "\x00\xff\r\n", 4);
	g_free(s);
	/* Fin de ligne vue sans option : l'horodatage activé ensuite part de la ligne suivante */
	gt_rx_transform_set_options(tr, FALSE, TRUE);
	verifier(tr, "z", T1, H1 "z");
	gt_rx_transform_free(tr);
}

static void
test_reinitialiser(void)
{
	GtRxTransform *tr = gt_rx_transform_new(TRUE, FALSE);

	verifier(tr, "a\r", T1, "a\r");
	gt_rx_transform_reset(tr);
	verifier(tr, "b", T1, "b");
	gt_rx_transform_free(tr);
}

static void
test_options_changees_en_cours(void)
{
	GtRxTransform *tr = gt_rx_transform_new(TRUE, FALSE);

	verifier(tr, "a\r", T1, "a\r");
	gt_rx_transform_set_options(tr, TRUE, TRUE);
	verifier(tr, "b", T2, "\n" H2 "b");
	gt_rx_transform_free(tr);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/rx_transform/format-horodatage", test_format_horodatage);
	g_test_add_func("/rx_transform/crlf-cas", test_crlf_cas);
	g_test_add_func("/rx_transform/crlf-coupe-entre-deux-appels", test_crlf_coupe_entre_deux_appels);
	g_test_add_func("/rx_transform/horodatage-debut-de-ligne", test_horodatage_debut_de_ligne);
	g_test_add_func("/rx_transform/horodatage-ligne-coupee", test_horodatage_ligne_coupee);
	g_test_add_func("/rx_transform/crlf-et-horodatage", test_crlf_et_horodatage);
	g_test_add_func("/rx_transform/options-desactivees", test_options_desactivees);
	g_test_add_func("/rx_transform/reinitialiser", test_reinitialiser);
	g_test_add_func("/rx_transform/options-changees-en-cours", test_options_changees_en_cours);
	return g_test_run();
}
```

`tests/meson.build`, dictionnaire `unit_tests` : ajouter `'test_rx_transform' : [],`.

- [ ] **Step 2 : constater l'échec**

Run : `ninja -C build 2>&1 | tail -5`
Expected : échec de compilation, `rx_transform.h: No such file or directory`.

- [ ] **Step 3 : écrire l'implémentation**

`src/rx_transform.h` :

```c
/* Transformation des octets reçus à la lecture : CR/LF automatique et horodatage en début de ligne (sous-projet 2).
   Pure : aucune horloge interne, aucune E/S ; l'heure de réception est passée par l'appelant. */
#ifndef RX_TRANSFORM_H_
#define RX_TRANSFORM_H_

#include <glib.h>

typedef struct _GtRxTransform GtRxTransform;

GtRxTransform *gt_rx_transform_new(gboolean crlf_auto, gboolean timestamp);
void           gt_rx_transform_free(GtRxTransform *tr);
/* Change les options sans perdre l'état de ligne (CR en attente, début de ligne). */
void           gt_rx_transform_set_options(GtRxTransform *tr, gboolean crlf_auto, gboolean timestamp);
/* Début de ligne, aucun CR en attente. */
void           gt_rx_transform_reset(GtRxTransform *tr);
/* Ajoute à out le texte produit par data. */
void           gt_rx_transform_feed(GtRxTransform *tr, const guint8 *data, gsize len, gint64 wall_time_us, GString *out);
/* « [j.HHh.MMm.SSs.mmm] » (UTC) suivi d'une espace. */
void           gt_rx_transform_format_timestamp(gint64 wall_time_us, GString *out);

#endif
```

`src/rx_transform.c` :

```c
/***********************************************************************/
/* rx_transform.c                                                      */
/*                                                                     */
/*   Purpose                                                           */
/*      CR/LF automatique et horodatage appliqués à la lecture         */
/*      (règles de l'ancien put_chars de buffer.c, sous-projet 2)      */
/*                                                                     */
/***********************************************************************/

#include "rx_transform.h"

struct _GtRxTransform
{
	gboolean crlf_auto;
	gboolean timestamp;
	gboolean cr_received;		/* dernier octet reçu : CR (CR/LF automatique) */
	gboolean need_timestamp;	/* prochain octet en début de ligne */
};

GtRxTransform *gt_rx_transform_new(gboolean crlf_auto, gboolean timestamp)
{
	GtRxTransform *tr = g_new0(GtRxTransform, 1);

	gt_rx_transform_set_options(tr, crlf_auto, timestamp);
	gt_rx_transform_reset(tr);
	return tr;
}

void gt_rx_transform_free(GtRxTransform *tr)
{
	g_free(tr);
}

void gt_rx_transform_set_options(GtRxTransform *tr, gboolean crlf_auto, gboolean timestamp)
{
	tr->crlf_auto = crlf_auto;
	tr->timestamp = timestamp;
}

void gt_rx_transform_reset(GtRxTransform *tr)
{
	tr->cr_received = FALSE;
	tr->need_timestamp = TRUE;
}

void gt_rx_transform_format_timestamp(gint64 wall_time_us, GString *out)
{
	gint64 sec = wall_time_us / G_USEC_PER_SEC;

	g_string_append_printf(out, "[%d.%02uh.%02um.%02us.%03u] ", (gint)(sec / 86400), (guint)((sec / 3600) % 24), (guint)((sec / 60) % 60),
	                       (guint)(sec % 60), (guint)((wall_time_us / 1000) % 1000));
}

void gt_rx_transform_feed(GtRxTransform *tr, const guint8 *data, gsize len, gint64 wall_time_us, GString *out)
{
	if(!tr->crlf_auto && !tr->timestamp)
	{
		/* Sans option, le texte est l'octet reçu ; on suit seulement les fins de ligne pour une activation ultérieure */
		g_string_append_len(out, (const gchar *)data, (gssize)len);
		if(len > 0)
			tr->need_timestamp = (data[len - 1] == '\n');
		tr->cr_received = FALSE;
		return;
	}

	for(gsize i = 0; i < len; i++)
	{
		guint8 c = data[i];

		if(tr->crlf_auto)
		{
			if(c == '\r')
			{
				/* Deux CR de suite : on insère un saut de ligne */
				if(tr->cr_received)
				{
					g_string_append_c(out, '\n');
					tr->need_timestamp = TRUE;
				}
				tr->cr_received = TRUE;
			}
			else
			{
				/* LF sans CR : on insère le CR ; caractère ordinaire après un CR : on insère le LF */
				if(c == '\n' && !tr->cr_received)
					g_string_append_c(out, '\r');
				else if(c != '\n' && tr->cr_received)
				{
					g_string_append_c(out, '\n');
					tr->need_timestamp = TRUE;
				}
				tr->cr_received = FALSE;
			}
		}

		if(tr->need_timestamp)
		{
			if(tr->timestamp)
				gt_rx_transform_format_timestamp(wall_time_us, out);
			tr->need_timestamp = FALSE;
		}
		if(c == '\n')
			tr->need_timestamp = TRUE;
		g_string_append_c(out, (gchar)c);
	}
}
```

`src/meson.build`, `core_sources` : ajouter `'rx_transform.c',` et `'rx_transform.h',`.

- [ ] **Step 4 : constater la réussite**

Run : `ninja -C build && meson test -C build test_rx_transform --print-errorlogs`
Expected : `test_rx_transform OK`.

- [ ] **Step 5 : suite complète et commit**

Run : `meson test -C build --print-errorlogs` — Expected : aucun échec.

```bash
git add src/rx_transform.c src/rx_transform.h src/meson.build tests/test_rx_transform.c tests/meson.build
git commit -m "cœur : CR/LF automatique et horodatage appliqués à la lecture (GtRxTransform)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3 : correspondance de périphérique (`device_match`)

**Files :**
- Create : `src/device_match.h`, `src/device_match.c`, `tests/test_device_match.c`
- Modify : `src/meson.build` (`core_sources`), `tests/meson.build` (`unit_tests`)

**Interfaces :**
- Produces : `gboolean device_match(dev_t want_devnum, const gchar *want_path, dev_t ev_devnum, const gchar *ev_file, const gchar *const *ev_links);` — vrai si `want_devnum != 0` et égal à `ev_devnum`, ou si `want_path` (non vide) est égal à `ev_file` ou à l'un des liens.

- [ ] **Step 1 : écrire le test**

`tests/test_device_match.c` :

```c
/* Tests unitaires de la correspondance d'un événement udev avec le port ouvert (device_match.c). */

#include <glib.h>
#include <sys/sysmacros.h>

#include "device_match.h"

static void
test_numero_egal(void)
{
	g_assert_true(device_match(makedev(166, 0), "/dev/ttyACM0", makedev(166, 0), "/dev/autre", NULL));
}

static void
test_numero_nul_ignore(void)
{
	g_assert_false(device_match(0, "/dev/ttyACM0", 0, "/dev/autre", NULL));
}

static void
test_chemin_egal(void)
{
	g_assert_true(device_match(0, "/dev/ttyUSB0", makedev(188, 0), "/dev/ttyUSB0", NULL));
}

static void
test_lien_egal(void)
{
	const gchar *const liens[] = { "/dev/serial/by-id/usb-x", "/dev/ttyM5", NULL };

	g_assert_true(device_match(makedev(166, 1), "/dev/ttyM5", makedev(166, 2), "/dev/ttyACM0", liens));
}

static void
test_aucune_correspondance(void)
{
	const gchar *const liens[] = { "/dev/serial/by-id/usb-x", NULL };

	g_assert_false(device_match(makedev(166, 1), "/dev/ttyM5", makedev(166, 2), "/dev/ttyACM0", liens));
}

static void
test_chemin_vide(void)
{
	g_assert_false(device_match(0, "", 0, "", NULL));
	g_assert_false(device_match(0, NULL, 0, "/dev/ttyACM0", NULL));
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/device_match/numero-egal", test_numero_egal);
	g_test_add_func("/device_match/numero-nul-ignore", test_numero_nul_ignore);
	g_test_add_func("/device_match/chemin-egal", test_chemin_egal);
	g_test_add_func("/device_match/lien-egal", test_lien_egal);
	g_test_add_func("/device_match/aucune-correspondance", test_aucune_correspondance);
	g_test_add_func("/device_match/chemin-vide", test_chemin_vide);
	return g_test_run();
}
```

`tests/meson.build`, `unit_tests` : ajouter `'test_device_match' : [],`.

- [ ] **Step 2 : constater l'échec**

Run : `ninja -C build 2>&1 | tail -5` — Expected : `device_match.h: No such file or directory`.

- [ ] **Step 3 : écrire l'implémentation**

`src/device_match.h` :

```c
/* Correspondance d'un événement udev avec le port série ouvert (sous-projet 2, M6). */
#ifndef DEVICE_MATCH_H_
#define DEVICE_MATCH_H_

#include <glib.h>
#include <sys/types.h>

/* Vrai si le numéro de périphérique est connu (non nul) et identique, ou si le chemin voulu est le nœud de l'événement ou
   l'un de ses liens udev (DEVLINKS, présents aussi dans l'événement « remove »). */
gboolean device_match(dev_t want_devnum, const gchar *want_path, dev_t ev_devnum, const gchar *ev_file, const gchar *const *ev_links);

#endif
```

`src/device_match.c` :

```c
/***********************************************************************/
/* device_match.c                                                      */
/*                                                                     */
/*   Purpose                                                           */
/*      Correspondance d'un événement udev avec le port ouvert         */
/*                                                                     */
/***********************************************************************/

#include <string.h>

#include "device_match.h"

gboolean device_match(dev_t want_devnum, const gchar *want_path, dev_t ev_devnum, const gchar *ev_file, const gchar *const *ev_links)
{
	if(want_devnum != 0 && ev_devnum == want_devnum)
		return TRUE;
	if(want_path == NULL || want_path[0] == '\0')
		return FALSE;
	if(ev_file != NULL && strcmp(ev_file, want_path) == 0)
		return TRUE;
	for(; ev_links != NULL && *ev_links != NULL; ev_links++)
	{
		if(strcmp(*ev_links, want_path) == 0)
			return TRUE;
	}
	return FALSE;
}
```

`src/meson.build`, `core_sources` : ajouter `'device_match.c',` et `'device_match.h',`.

- [ ] **Step 4 : constater la réussite**

Run : `ninja -C build && meson test -C build test_device_match --print-errorlogs` — Expected : OK.

- [ ] **Step 5 : suite complète et commit**

Run : `meson test -C build --print-errorlogs` — Expected : aucun échec.

```bash
git add src/device_match.c src/device_match.h src/meson.build tests/test_device_match.c tests/meson.build
git commit -m "cœur : correspondance d'un événement udev par numéro de périphérique, chemin ou lien

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4 : rendu pur des vues et de l'écho (`GtViewRender`)

**Files :**
- Create : `src/view_render.h`, `src/view_render.c`, `tests/test_view_render.c`
- Modify : `src/meson.build` (`core_sources`), `tests/meson.build` (`unit_tests`)

**Interfaces :**
- Consumes : `GtRxTransform` (tâche 2) ; `GtRecord`, `GtSendOrigin`, `GtHistory` (tâche 1).
- Produces :
  ```c
  typedef enum { GT_VIEW_ASCII, GT_VIEW_HEX } GtViewMode;
  typedef struct { GtViewMode mode; gboolean echo; gboolean crlf_auto; gboolean timestamp; guint bytes_per_line; gboolean show_index; } GtViewOptions;
  typedef struct _GtViewRender GtViewRender;
  GtViewRender *gt_view_render_new(const GtViewOptions *opts);
  void gt_view_render_free(GtViewRender *r);
  void gt_view_render_reset(GtViewRender *r, const GtViewOptions *opts);
  void gt_view_render_rx(GtViewRender *r, const guint8 *data, gsize len, gint64 wall_time_us, GString *out);
  void gt_view_render_tx(GtViewRender *r, const guint8 *data, gsize len, GtSendOrigin origin, GString *out);
  void gt_view_render_record(GtViewRender *r, const GtRecord *rec, GString *out);
  ```

Règles (spec §5 et §6, décision 6 du plan) :
- **ASCII, RX** : texte de la `GtRxTransform` de la vue.
- **ASCII, TX** (seulement si `echo`) : origine `GT_SEND_KEYBOARD` → `"\033[0;34m"` + octets + `"\033[0m"`, sans rien ajouter ; autres origines → si la dernière sortie ne finit pas une ligne (`'\n'`), `"\r\n"` d'abord, puis `"\033[0;34m> "` + octets sans les CR/LF finaux + `"\033[0m\r\n"`.
- **Hexadécimal, RX** : repris de l'ancien `put_hexadecimal` (une cellule par octet : `"%02X "`, déplacement `"\033[%uC"` vers la colonne ASCII, caractère ou « . », retour `"\033[%uD"`, `"- "` au milieu de la ligne ; index `"%6u: "` en début de ligne si demandé ; `"\r\n"` en fin de ligne). L'index compte les **seuls octets reçus**.
- **Hexadécimal, TX** (seulement si `echo`) : une ligne reçue entamée est terminée (`"\r\n"`, ses octets comptent dans l'index) ; puis lignes bleues avec les mêmes cellules, préfixées de `"     >  "` (8 colonnes, comme l'index) si l'index est affiché ; `"\r\n"` final ; `"\033[0m"`. L'index et la colonne des octets reçus ne bougent pas.

- [ ] **Step 1 : écrire le test**

`tests/test_view_render.c` :

```c
/* Tests unitaires du rendu des vues (view_render.c) : ASCII, hexadécimal, écho, rejeu de l'historique. */

#include <glib.h>
#include <string.h>

#include "history.h"
#include "view_render.h"

#define BLEU "\033[0;34m"
#define FIN "\033[0m"

static GtViewOptions
options(GtViewMode mode, gboolean echo)
{
	GtViewOptions o = { mode, echo, FALSE, FALSE, 8, FALSE };

	return o;
}

static gchar *
rx(GtViewRender *r, const gchar *data, gsize len)
{
	GString *out = g_string_new(NULL);

	gt_view_render_rx(r, (const guint8 *)data, len, 0, out);
	return g_string_free(out, FALSE);
}

static gchar *
tx(GtViewRender *r, const gchar *data, GtSendOrigin origine)
{
	GString *out = g_string_new(NULL);

	gt_view_render_tx(r, (const guint8 *)data, strlen(data), origine, out);
	return g_string_free(out, FALSE);
}

#define VERIFIER(expr, attendu) do { gchar *s_ = (expr); g_assert_cmpstr(s_, ==, (attendu)); g_free(s_); } while(0)
#define VERIFIER_DEBUT(expr, debut) do { gchar *s_ = (expr); if(!g_str_has_prefix(s_, (debut))) g_error("debut inattendu : %s", g_strescape(s_, NULL)); g_free(s_); } while(0)

static void
test_ascii_sans_option(void)
{
	GtViewOptions o = options(GT_VIEW_ASCII, FALSE);
	GtViewRender *r = gt_view_render_new(&o);

	VERIFIER(rx(r, "ab\r\n", 4), "ab\r\n");
	gt_view_render_free(r);
}

static void
test_ascii_echo_desactive(void)
{
	GtViewOptions o = options(GT_VIEW_ASCII, FALSE);
	GtViewRender *r = gt_view_render_new(&o);

	VERIFIER(tx(r, "x", GT_SEND_KEYBOARD), "");
	VERIFIER(tx(r, "y", GT_SEND_MACRO), "");
	gt_view_render_free(r);
}

static void
test_ascii_frappe_dans_la_ligne(void)
{
	GtViewOptions o = options(GT_VIEW_ASCII, TRUE);
	GtViewRender *r = gt_view_render_new(&o);

	VERIFIER(rx(r, "abc", 3), "abc");
	VERIFIER(tx(r, "d", GT_SEND_KEYBOARD), BLEU "d" FIN);
	gt_view_render_free(r);
}

static void
test_ascii_envoi_en_bloc_sur_sa_ligne(void)
{
	GtViewOptions o = options(GT_VIEW_ASCII, TRUE);
	GtViewRender *r = gt_view_render_new(&o);

	VERIFIER(rx(r, "abc", 3), "abc");
	VERIFIER(tx(r, "CMD\r\n", GT_SEND_MACRO), "\r\n" BLEU "> CMD" FIN "\r\n");
	VERIFIER(rx(r, "OK", 2), "OK");
	gt_view_render_free(r);
}

static void
test_ascii_envoi_en_bloc_en_debut_de_ligne(void)
{
	GtViewOptions o = options(GT_VIEW_ASCII, TRUE);
	GtViewRender *r = gt_view_render_new(&o);

	VERIFIER(rx(r, "abc\r\n", 5), "abc\r\n");
	VERIFIER(tx(r, "X", GT_SEND_HEX), BLEU "> X" FIN "\r\n");
	gt_view_render_free(r);
}

static void
test_ascii_bloc_apres_frappe_cr(void)
{
	GtViewOptions o = options(GT_VIEW_ASCII, TRUE);
	GtViewRender *r = gt_view_render_new(&o);

	VERIFIER(tx(r, "a\r", GT_SEND_KEYBOARD), BLEU "a\r" FIN);
	VERIFIER(tx(r, "M", GT_SEND_LUA), "\r\n" BLEU "> M" FIN "\r\n");
	gt_view_render_free(r);
}

static void
test_ascii_transformation_appliquee(void)
{
	GtViewOptions o = options(GT_VIEW_ASCII, FALSE);
	GtViewRender *r;

	o.crlf_auto = TRUE;
	r = gt_view_render_new(&o);
	VERIFIER(rx(r, "a\nb", 3), "a\r\nb");
	gt_view_render_free(r);
}

static void
test_hexa_cellule(void)
{
	GtViewOptions o = options(GT_VIEW_HEX, FALSE);
	GtViewRender *r = gt_view_render_new(&o);

	/* colonne 0, 8 octets par ligne : avance = (8 - 0) * 3 + 0 + 2 = 26 */
	VERIFIER(rx(r, "A", 1), "41 \033[26CA\033[27D");
	gt_view_render_free(r);
}

static void
test_hexa_separateur(void)
{
	GtViewOptions o = options(GT_VIEW_HEX, FALSE);
	GtViewRender *r = gt_view_render_new(&o);
	gchar *s = rx(r, "ABCD", 4);

	/* colonne 3 = bytes_per_line / 2 - 1 : avance = (8 - 3) * 3 + 3 + 2 = 20, puis « - » */
	g_assert_nonnull(strstr(s, "44 \033[20CD\033[21D- "));
	g_free(s);
	gt_view_render_free(r);
}

static void
test_hexa_colonne_ascii(void)
{
	GtViewOptions o = options(GT_VIEW_HEX, FALSE);
	GtViewRender *r = gt_view_render_new(&o);
	gchar *s = rx(r, "\x01\x7f\xff", 3);

	g_assert_nonnull(strstr(s, "01 \033[26C.\033[27D"));
	g_assert_nonnull(strstr(s, "7F \033[24C.\033[25D"));
	g_assert_nonnull(strstr(s, "FF \033[22C.\033[23D"));
	g_free(s);
	gt_view_render_free(r);
}

static void
test_hexa_ligne_complete(void)
{
	GtViewOptions o = options(GT_VIEW_HEX, FALSE);
	GtViewRender *r;
	gchar *s;

	o.show_index = TRUE;
	r = gt_view_render_new(&o);
	s = rx(r, "ABCDEFGH", 8);
	g_assert_true(g_str_has_prefix(s, "     0: 41 "));
	g_assert_true(g_str_has_suffix(s, "\r\n"));
	g_free(s);
	VERIFIER_DEBUT(rx(r, "I", 1), "     8: 49 ");
	gt_view_render_free(r);
}

static void
test_hexa_index_non_decale_par_l_echo(void)
{
	GtViewOptions o = options(GT_VIEW_HEX, TRUE);
	GtViewRender *r;
	gchar *s;

	o.show_index = TRUE;
	r = gt_view_render_new(&o);
	VERIFIER_DEBUT(rx(r, "ABC", 3), "     0: 41 ");
	s = tx(r, "Z", GT_SEND_MACRO);
	g_assert_true(g_str_has_prefix(s, "\r\n" BLEU "     >  5A "));
	g_assert_true(g_str_has_suffix(s, "\r\n" FIN));
	g_free(s);
	VERIFIER_DEBUT(rx(r, "DEF", 3), "     3: 44 ");
	gt_view_render_free(r);
}

static void
test_hexa_echo_desactive(void)
{
	GtViewOptions o = options(GT_VIEW_HEX, FALSE);
	GtViewRender *r;

	o.show_index = TRUE;
	r = gt_view_render_new(&o);
	g_free(rx(r, "AB", 2));
	VERIFIER(tx(r, "Z", GT_SEND_MACRO), "");
	/* la ligne reçue continue en colonne 2, sans index ni saut de ligne */
	VERIFIER_DEBUT(rx(r, "C", 1), "43 ");
	gt_view_render_free(r);
}

static void
test_reinitialiser_change_de_vue(void)
{
	GtViewOptions o = options(GT_VIEW_HEX, FALSE);
	GtViewRender *r;

	o.show_index = TRUE;
	r = gt_view_render_new(&o);
	g_free(rx(r, "AB", 2));
	gt_view_render_reset(r, &o);
	VERIFIER_DEBUT(rx(r, "C", 1), "     0: 43 ");
	o.mode = GT_VIEW_ASCII;
	gt_view_render_reset(r, &o);
	VERIFIER(rx(r, "C", 1), "C");
	gt_view_render_free(r);
}

/* ---- Rejeu de l'historique = rendu en direct ---------------------------- */

typedef struct
{
	GtViewRender *r;
	GString *out;
} Rejeu;

static void
rejouer(const GtRecord *rec, gpointer user_data)
{
	Rejeu *j = user_data;

	gt_view_render_record(j->r, rec, j->out);
}

static void
comparer_rejeu(GtViewOptions *o, GtHistory *h, GString *direct)
{
	Rejeu j;

	j.r = gt_view_render_new(o);
	j.out = g_string_new(NULL);
	gt_history_foreach(h, rejouer, &j);
	g_assert_cmpuint(j.out->len, ==, direct->len);
	g_assert_true(memcmp(j.out->str, direct->str, direct->len) == 0);
	g_string_free(j.out, TRUE);
	gt_view_render_free(j.r);
}

static void
ajouter_et_rendre(GtViewRender *r, GtHistory *h, GString *direct, GtRecordKind kind, GtSendOrigin origine, gint64 heure, const gchar *texte)
{
	GtRecord rec = { kind, origine, heure, (const guint8 *)texte, strlen(texte) };

	gt_history_append(h, kind, origine, heure, rec.data, rec.len);
	gt_view_render_record(r, &rec, direct);
}

static void
test_rejeu_egal_direct(void)
{
	for(gint mode = GT_VIEW_ASCII; mode <= GT_VIEW_HEX; mode++)
	{
		GtViewOptions o = { (GtViewMode)mode, TRUE, TRUE, TRUE, 8, TRUE };
		GtViewRender *r = gt_view_render_new(&o);
		GtHistory *h = gt_history_new(GT_HISTORY_DEFAULT_CAPACITY);
		GString *direct = g_string_new(NULL);

		ajouter_et_rendre(r, h, direct, GT_RECORD_RX, GT_SEND_KEYBOARD, 1000000, "L1\n");
		ajouter_et_rendre(r, h, direct, GT_RECORD_TX, GT_SEND_KEYBOARD, 2000000, "k");
		ajouter_et_rendre(r, h, direct, GT_RECORD_RX, GT_SEND_KEYBOARD, 3000000, "L2\r");
		ajouter_et_rendre(r, h, direct, GT_RECORD_TX, GT_SEND_MACRO, 4000000, "M\r\n");
		ajouter_et_rendre(r, h, direct, GT_RECORD_RX, GT_SEND_KEYBOARD, 5000000, "x");
		comparer_rejeu(&o, h, direct);
		g_string_free(direct, TRUE);
		gt_history_free(h);
		gt_view_render_free(r);
	}
}

static void
test_rejeu_egal_direct_grand(void)
{
	GtViewOptions o = { GT_VIEW_HEX, TRUE, FALSE, FALSE, 16, TRUE };
	GtViewRender *r = gt_view_render_new(&o);
	GtHistory *h = gt_history_new(GT_HISTORY_DEFAULT_CAPACITY);
	GString *direct = g_string_new(NULL);

	for(guint i = 0; i < 20000; i++)
	{
		gchar octet[2] = { (gchar)('A' + i % 26), 0 };

		ajouter_et_rendre(r, h, direct, (i % 500 == 499) ? GT_RECORD_TX : GT_RECORD_RX, GT_SEND_MACRO, i, octet);
	}
	comparer_rejeu(&o, h, direct);
	g_string_free(direct, TRUE);
	gt_history_free(h);
	gt_view_render_free(r);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/view_render/ascii-sans-option", test_ascii_sans_option);
	g_test_add_func("/view_render/ascii-echo-desactive", test_ascii_echo_desactive);
	g_test_add_func("/view_render/ascii-frappe-dans-la-ligne", test_ascii_frappe_dans_la_ligne);
	g_test_add_func("/view_render/ascii-envoi-en-bloc-sur-sa-ligne", test_ascii_envoi_en_bloc_sur_sa_ligne);
	g_test_add_func("/view_render/ascii-envoi-en-bloc-en-debut-de-ligne", test_ascii_envoi_en_bloc_en_debut_de_ligne);
	g_test_add_func("/view_render/ascii-bloc-apres-frappe-cr", test_ascii_bloc_apres_frappe_cr);
	g_test_add_func("/view_render/ascii-transformation-appliquee", test_ascii_transformation_appliquee);
	g_test_add_func("/view_render/hexa-cellule", test_hexa_cellule);
	g_test_add_func("/view_render/hexa-separateur", test_hexa_separateur);
	g_test_add_func("/view_render/hexa-colonne-ascii", test_hexa_colonne_ascii);
	g_test_add_func("/view_render/hexa-ligne-complete", test_hexa_ligne_complete);
	g_test_add_func("/view_render/hexa-index-non-decale-par-l-echo", test_hexa_index_non_decale_par_l_echo);
	g_test_add_func("/view_render/hexa-echo-desactive", test_hexa_echo_desactive);
	g_test_add_func("/view_render/reinitialiser-change-de-vue", test_reinitialiser_change_de_vue);
	g_test_add_func("/view_render/rejeu-egal-direct", test_rejeu_egal_direct);
	g_test_add_func("/view_render/rejeu-egal-direct-grand", test_rejeu_egal_direct_grand);
	return g_test_run();
}
```

`tests/meson.build`, `unit_tests` : ajouter `'test_view_render' : [],`.

- [ ] **Step 2 : constater l'échec**

Run : `ninja -C build 2>&1 | tail -5` — Expected : `view_render.h: No such file or directory`.

- [ ] **Step 3 : écrire l'implémentation**

`src/view_render.h` :

```c
/* Rendu des vues ASCII et hexadécimale du terminal, écho compris, vers les octets à donner à VTE (sous-projet 2).
   Pur : aucune dépendance GTK ; terminal_display.c se contente d'alimenter VTE avec la sortie. */
#ifndef VIEW_RENDER_H_
#define VIEW_RENDER_H_

#include <glib.h>

#include "history.h"

typedef enum
{
	GT_VIEW_ASCII,
	GT_VIEW_HEX
} GtViewMode;

typedef struct
{
	GtViewMode mode;
	gboolean echo;			/* afficher les octets envoyés */
	gboolean crlf_auto;		/* vue ASCII */
	gboolean timestamp;		/* vue ASCII */
	guint bytes_per_line;		/* vue hexadécimale (2 au minimum) */
	gboolean show_index;		/* vue hexadécimale */
} GtViewOptions;

typedef struct _GtViewRender GtViewRender;

GtViewRender *gt_view_render_new(const GtViewOptions *opts);
void          gt_view_render_free(GtViewRender *r);
/* Nouvelles options et état remis à zéro (vue vidée puis redessinée par l'appelant). */
void          gt_view_render_reset(GtViewRender *r, const GtViewOptions *opts);
void          gt_view_render_rx(GtViewRender *r, const guint8 *data, gsize len, gint64 wall_time_us, GString *out);
void          gt_view_render_tx(GtViewRender *r, const guint8 *data, gsize len, GtSendOrigin origin, GString *out);
/* Rejeu d'un enregistrement de l'historique (RX ou TX). */
void          gt_view_render_record(GtViewRender *r, const GtRecord *rec, GString *out);

#endif
```

`src/view_render.c` :

```c
/***********************************************************************/
/* view_render.c                                                       */
/*                                                                     */
/*   Purpose                                                           */
/*      Rendu des vues ASCII et hexadécimale, écho compris             */
/*      (sous-projet 2 ; vue hexadécimale reprise de put_hexadecimal)  */
/*                                                                     */
/***********************************************************************/

#include <string.h>

#include "rx_transform.h"
#include "view_render.h"

#define COLOR_ECHO "\033[0;34m"
#define COLOR_RESET "\033[0m"

struct _GtViewRender
{
	GtViewOptions opts;
	GtRxTransform *transform;	/* vue ASCII */
	gboolean at_bol;		/* vue ASCII : la dernière sortie finit une ligne */
	guint col;			/* vue hexadécimale : colonne de la ligne reçue en cours */
	guint total_rx;			/* vue hexadécimale : octets reçus sur les lignes terminées (index) */
};

static void view_render_set_options(GtViewRender *r, const GtViewOptions *opts)
{
	r->opts = *opts;
	if(r->opts.bytes_per_line < 2)
		r->opts.bytes_per_line = 2;
	gt_rx_transform_set_options(r->transform, r->opts.crlf_auto, r->opts.timestamp);
	gt_rx_transform_reset(r->transform);
	r->at_bol = TRUE;
	r->col = 0;
	r->total_rx = 0;
}

GtViewRender *gt_view_render_new(const GtViewOptions *opts)
{
	GtViewRender *r = g_new0(GtViewRender, 1);

	r->transform = gt_rx_transform_new(opts->crlf_auto, opts->timestamp);
	view_render_set_options(r, opts);
	return r;
}

void gt_view_render_free(GtViewRender *r)
{
	if(r == NULL)
		return;
	gt_rx_transform_free(r->transform);
	g_free(r);
}

void gt_view_render_reset(GtViewRender *r, const GtViewOptions *opts)
{
	view_render_set_options(r, opts);
}

/* Une cellule de la vue hexadécimale : « XX », caractère dans la colonne ASCII, « - » au milieu de la ligne */
static void hex_put_byte(GtViewRender *r, guint8 c, guint *col, GString *out)
{
	guint bpl = r->opts.bytes_per_line;
	guint avance = (bpl - *col) * 3 + *col + 2;

	g_string_append_printf(out, "%02X ", c);
	g_string_append_printf(out, "\033[%uC", avance);
	g_string_append_c(out, (c >= 0x20 && c < 0x7F) ? (gchar)c : '.');
	g_string_append_printf(out, "\033[%uD", avance + 1);
	if(*col == bpl / 2 - 1)
		g_string_append(out, "- ");
	(*col)++;
}

void gt_view_render_rx(GtViewRender *r, const guint8 *data, gsize len, gint64 wall_time_us, GString *out)
{
	if(r->opts.mode == GT_VIEW_ASCII)
	{
		gsize start = out->len;

		gt_rx_transform_feed(r->transform, data, len, wall_time_us, out);
		if(out->len > start)
			r->at_bol = (out->str[out->len - 1] == '\n');
		return;
	}

	for(gsize i = 0; i < len; i++)
	{
		if(r->col == 0 && r->opts.show_index)
			g_string_append_printf(out, "%6u: ", r->total_rx);
		hex_put_byte(r, data[i], &r->col, out);
		if(r->col == r->opts.bytes_per_line)
		{
			g_string_append(out, "\r\n");
			r->total_rx += r->col;
			r->col = 0;
		}
	}
}

void gt_view_render_tx(GtViewRender *r, const guint8 *data, gsize len, GtSendOrigin origin, GString *out)
{
	if(!r->opts.echo || len == 0)
		return;

	if(r->opts.mode == GT_VIEW_ASCII)
	{
		if(origin == GT_SEND_KEYBOARD)
		{
			/* Frappe : répétée dans la ligne, sans préfixe ni retour à la ligne ajouté */
			g_string_append(out, COLOR_ECHO);
			g_string_append_len(out, (const gchar *)data, (gssize)len);
			g_string_append(out, COLOR_RESET);
			r->at_bol = (data[len - 1] == '\n');
		}
		else
		{
			/* Envoi en bloc : sa propre ligne « > », sans les CR/LF finaux */
			gsize n = len;

			while(n > 0 && (data[n - 1] == '\r' || data[n - 1] == '\n'))
				n--;
			if(!r->at_bol)
				g_string_append(out, "\r\n");
			g_string_append(out, COLOR_ECHO "> ");
			g_string_append_len(out, (const gchar *)data, (gssize)n);
			g_string_append(out, COLOR_RESET "\r\n");
			r->at_bol = TRUE;
		}
		return;
	}

	/* Vue hexadécimale : l'envoi a ses propres lignes et ne compte pas dans l'index des octets reçus */
	guint col = 0;

	if(r->col != 0)
	{
		g_string_append(out, "\r\n");
		r->total_rx += r->col;
		r->col = 0;
	}
	g_string_append(out, COLOR_ECHO);
	for(gsize i = 0; i < len; i++)
	{
		if(col == 0 && r->opts.show_index)
			g_string_append(out, "     >  ");
		hex_put_byte(r, data[i], &col, out);
		if(col == r->opts.bytes_per_line)
		{
			g_string_append(out, "\r\n");
			col = 0;
		}
	}
	if(col != 0)
		g_string_append(out, "\r\n");
	g_string_append(out, COLOR_RESET);
}

void gt_view_render_record(GtViewRender *r, const GtRecord *rec, GString *out)
{
	if(rec->kind == GT_RECORD_RX)
		gt_view_render_rx(r, rec->data, rec->len, rec->wall_time_us, out);
	else
		gt_view_render_tx(r, rec->data, rec->len, rec->origin, out);
}
```

`src/meson.build`, `core_sources` : ajouter `'view_render.c',` et `'view_render.h',`.

- [ ] **Step 4 : constater la réussite**

Run : `ninja -C build && meson test -C build test_view_render --print-errorlogs` — Expected : OK.

- [ ] **Step 5 : suite complète et commit**

Run : `meson test -C build --print-errorlogs` — Expected : aucun échec.

```bash
git add src/view_render.c src/view_render.h src/meson.build tests/test_view_render.c tests/meson.build
git commit -m "cœur : rendu pur des vues ASCII et hexadécimale, écho à sa place (GtViewRender)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5 : transport par instance, sans interface

**Files :**
- Rewrite : `src/transport.h`, `src/transport.c`, `tests/test_transport.c`
- Modify : `src/serial.h`, `src/serial.c` (pont provisoire, décision 8), `src/baud.c`, `src/baudrates.c`, `tests/test_baudrates.c` (inclusion), `src/term_config.h`, `src/term_config.c`, `src/cmdline.c`, `src/files.c`, `src/meson.build`, `tests/meson.build`

**Interfaces :**
- Consumes : `serial_termios_apply(struct termios *tio, gint bits, gint parity, gint stopbits, gint flow)` (sous-projet 1), `set_port_baudrate(unsigned int, int)` (`baud.c`), `strerror_utf8`, `i18n_perror` (`i18n.h`).
- Produces (utilisés par les tâches 6 à 8) :
  ```c
  typedef enum { TRANSPORT_SERIAL, TRANSPORT_TCP_CLIENT, TRANSPORT_TCP_SERVER } TransportType;
  typedef enum { GT_PARITY_NONE, GT_PARITY_ODD, GT_PARITY_EVEN } GtParity;
  typedef enum { GT_FLOW_NONE, GT_FLOW_XONXOFF, GT_FLOW_RTSCTS, GT_FLOW_RS485 } GtFlow;
  typedef enum { GT_MODEM_DTR, GT_MODEM_RTS } GtModemLine;
  typedef struct { TransportType type; gchar *port; guint baudrate; gint bits; gint stopbits; GtParity parity; GtFlow flow;
                   gint rs485_before_ms, rs485_after_ms; gboolean lock_port; gchar *host; gchar *tcp_port; } TransportParams;
  void transport_params_copy(TransportParams *dst, const TransportParams *src);
  void transport_params_clear(TransportParams *p);
  typedef enum { TRANSPORT_EVENT_CONNECTED, TRANSPORT_EVENT_CLIENT_CONNECTED, TRANSPORT_EVENT_CLIENT_GONE, TRANSPORT_EVENT_LOST } TransportEvent;
  typedef struct { void (*on_data)(const guint8 *, gsize, gpointer); void (*on_event)(TransportEvent, const gchar *, gpointer); } TransportCallbacks;
  #define GT_TRANSPORT_ERROR (gt_transport_error_quark())
  typedef enum { GT_TRANSPORT_ERROR_OPEN, GT_TRANSPORT_ERROR_NOT_TTY, GT_TRANSPORT_ERROR_LOCKED, GT_TRANSPORT_ERROR_BAUDRATE, GT_TRANSPORT_ERROR_RESOLVE,
                 GT_TRANSPORT_ERROR_SOCKET, GT_TRANSPORT_ERROR_BIND, GT_TRANSPORT_ERROR_LISTEN, GT_TRANSPORT_ERROR_CONNECT } GtTransportError;
  GtTransport *gt_transport_open(const TransportParams *, const TransportCallbacks *, gpointer user_data, GError **);
  void   gt_transport_close(GtTransport *t);                    /* tolère NULL, comme toutes les fonctions ci-dessous */
  gssize gt_transport_send(GtTransport *t, const guint8 *data, gsize len);
  gint   gt_transport_get_modem_lines(GtTransport *t);          /* masque TIOCM_*, -1 sans objet ; perte signalée en LOST */
  void   gt_transport_toggle_line(GtTransport *t, GtModemLine line);
  void   gt_transport_send_break(GtTransport *t);
  guint  gt_transport_get_actual_baudrate(GtTransport *t);
  gint   gt_transport_get_fd(GtTransport *t);                   /* serveur : client accepté, -1 sinon */
  dev_t  gt_transport_get_device_number(GtTransport *t);
  ```
  Et dans `term_config.h` : champs `GtParity parite;` et `GtFlow flux;`, `void config_to_transport_params(const struct configuration_port *c, TransportParams *p);`.

- [ ] **Step 1 : réécrire le test du transport**

Remplacer tout le contenu de `tests/test_transport.c` par :

```c
/* Tests d'intégration du transport (transport.c, cœur sans interface) sur paire pty et TCP local.
 * ioctl est intercepté (-Wl,--wrap=ioctl) pour simuler les lignes modem RTS/DTR qu'une pty n'a pas. */

#include <glib.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <poll.h>
#include <pty.h>
#include <stdarg.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "transport.h"
#include "config_file.h"

/* ---- Lignes modem simulées ------------------------------------------- */

int __real_ioctl(int fd, unsigned long requete, ...);

static int modem_fd = -1;
static int modem_etat = 0;
static int changements_rts = 0;

int
__wrap_ioctl(int fd, unsigned long requete, ...)
{
	va_list ap;
	void *arg;

	va_start(ap, requete);
	arg = va_arg(ap, void *);
	va_end(ap);

	if(fd == modem_fd && (requete == TIOCMGET || requete == TIOCMSET || requete == TIOCMBIS || requete == TIOCMBIC))
	{
		int ancien = modem_etat;

		if(requete == TIOCMGET)
			*(int *)arg = modem_etat;
		else if(requete == TIOCMSET)
			modem_etat = *(int *)arg;
		else if(requete == TIOCMBIS)
			modem_etat |= *(int *)arg;
		else
			modem_etat &= ~*(int *)arg;
		if((ancien ^ modem_etat) & TIOCM_RTS)
			changements_rts++;
		return 0;
	}
	return __real_ioctl(fd, requete, arg);
}

/* ---- Récepteur des rappels ------------------------------------------- */

typedef struct
{
	GByteArray *octets;
	GArray *evenements;	/* TransportEvent */
	gchar *detail;
} Recepteur;

static void
recepteur_init(Recepteur *r)
{
	r->octets = g_byte_array_new();
	r->evenements = g_array_new(FALSE, FALSE, sizeof(TransportEvent));
	r->detail = NULL;
}

static void
recepteur_liberer(Recepteur *r)
{
	g_byte_array_unref(r->octets);
	g_array_unref(r->evenements);
	g_free(r->detail);
}

static void
sur_donnees(const guint8 *data, gsize len, gpointer user_data)
{
	g_byte_array_append(((Recepteur *)user_data)->octets, data, (guint)len);
}

static void
sur_evenement(TransportEvent ev, const gchar *detail, gpointer user_data)
{
	Recepteur *r = user_data;

	g_array_append_val(r->evenements, ev);
	g_free(r->detail);
	r->detail = g_strdup(detail);
}

static const TransportCallbacks rappels = { sur_donnees, sur_evenement };

static guint
compter(Recepteur *r, TransportEvent ev)
{
	guint n = 0;

	for(guint i = 0; i < r->evenements->len; i++)
		if(g_array_index(r->evenements, TransportEvent, i) == ev)
			n++;
	return n;
}

static void
iterer(guint ms)
{
	gint64 fin = g_get_monotonic_time() + (gint64)ms * 1000;

	while(g_get_monotonic_time() < fin)
	{
		g_main_context_iteration(NULL, FALSE);
		g_usleep(1000);
	}
}

static gboolean
attendre_evenement(Recepteur *r, TransportEvent ev, guint ms)
{
	gint64 fin = g_get_monotonic_time() + (gint64)ms * 1000;

	while(compter(r, ev) == 0 && g_get_monotonic_time() < fin)
	{
		g_main_context_iteration(NULL, FALSE);
		g_usleep(1000);
	}
	return compter(r, ev) > 0;
}

static gboolean
attendre_octets(Recepteur *r, guint n, guint ms)
{
	gint64 fin = g_get_monotonic_time() + (gint64)ms * 1000;

	while(r->octets->len < n && g_get_monotonic_time() < fin)
	{
		g_main_context_iteration(NULL, FALSE);
		g_usleep(1000);
	}
	return r->octets->len >= n;
}

/* ---- Paramètres et ports ---------------------------------------------- */

static void
params_serie(TransportParams *p, const gchar *port)
{
	memset(p, 0, sizeof(*p));
	p->type = TRANSPORT_SERIAL;
	p->port = g_strdup(port);
	p->baudrate = 115200;
	p->bits = 8;
	p->stopbits = 1;
	p->parity = GT_PARITY_NONE;
	p->flow = GT_FLOW_NONE;
	p->lock_port = FALSE;
}

static void
params_tcp(TransportParams *p, TransportType type, guint16 port)
{
	memset(p, 0, sizeof(*p));
	p->type = type;
	p->host = g_strdup("127.0.0.1");
	p->tcp_port = g_strdup_printf("%u", port);
}

typedef struct
{
	int maitre;
	int esclave;
} paire_pty_t;

static gboolean
ouvrir_pty(paire_pty_t *p)
{
	return openpty(&p->maitre, &p->esclave, NULL, NULL, NULL) == 0;
}

static void
fermer_pty(paire_pty_t *p)
{
	if(p->esclave >= 0)
		close(p->esclave);
	if(p->maitre >= 0)
		close(p->maitre);
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

static gsize
lire(int fd, guint8 *buf, gsize taille, guint ms)
{
	gsize lu = 0;
	gint64 fin = g_get_monotonic_time() + (gint64)ms * 1000;

	while(lu < taille && g_get_monotonic_time() < fin)
	{
		struct pollfd pfd = { fd, POLLIN, 0 };

		if(poll(&pfd, 1, 10) > 0)
		{
			ssize_t n = read(fd, buf + lu, taille - lu);

			if(n > 0)
				lu += (gsize)n;
		}
		g_main_context_iteration(NULL, FALSE);
	}
	return lu;
}

/* ---- C4 : aucun réglage hérité, restauration à la fermeture ----------- */

static void
test_termios_sans_heritage(void)
{
	paire_pty_t p;
	struct termios tio;
	TransportParams tp;
	GtTransport *t;
	GError *err = NULL;

	if(!ouvrir_pty(&p))
	{
		g_test_skip("openpty indisponible");
		return;
	}
	g_assert_cmpint(tcgetattr(p.esclave, &tio), ==, 0);
	/* Une pty impose CS8 et sans parité : seuls CRTSCTS et CSTOPB y sont observables. */
	tio.c_cflag = (tio.c_cflag & ~CSIZE) | CS8 | CSTOPB | CRTSCTS;
	g_assert_cmpint(tcsetattr(p.esclave, TCSANOW, &tio), ==, 0);

	params_serie(&tp, ttyname(p.esclave));
	t = gt_transport_open(&tp, &rappels, NULL, &err);
	g_assert_no_error(err);
	g_assert_nonnull(t);
	g_assert_cmpint(tcgetattr(gt_transport_get_fd(t), &tio), ==, 0);
	g_assert_cmpuint(tio.c_cflag & CSIZE, ==, CS8);
	g_assert_cmpuint(tio.c_cflag & (CSTOPB | CRTSCTS), ==, 0);
	gt_transport_close(t);

	/* Fermeture d'une instance série : réglages d'origine restaurés */
	g_assert_cmpint(tcgetattr(p.esclave, &tio), ==, 0);
	g_assert_cmpuint(tio.c_cflag & CSTOPB, ==, CSTOPB);
	transport_params_clear(&tp);
	fermer_pty(&p);
}

/* ---- Numéro de périphérique relevé à l'ouverture ----------------------- */

static void
test_numero_de_peripherique(void)
{
	paire_pty_t p;
	TransportParams tp;
	GtTransport *t;
	struct stat st;

	if(!ouvrir_pty(&p))
	{
		g_test_skip("openpty indisponible");
		return;
	}
	params_serie(&tp, ttyname(p.esclave));
	g_assert_cmpint(stat(tp.port, &st), ==, 0);
	t = gt_transport_open(&tp, &rappels, NULL, NULL);
	g_assert_nonnull(t);
	g_assert_true(gt_transport_get_device_number(t) == st.st_rdev);
	g_assert_cmpuint(gt_transport_get_actual_baudrate(t), ==, 115200);
	gt_transport_close(t);
	transport_params_clear(&tp);
	fermer_pty(&p);
}

/* ---- C3 : RTS stable en RS485 ------------------------------------------ */

static void
test_rs485_rts_stable(void)
{
	paire_pty_t p;
	TransportParams tp;
	GtTransport *t;

	if(!ouvrir_pty(&p))
	{
		g_test_skip("openpty indisponible");
		return;
	}
	params_serie(&tp, ttyname(p.esclave));
	tp.flow = GT_FLOW_RS485;
	t = gt_transport_open(&tp, &rappels, NULL, NULL);
	g_assert_nonnull(t);
	modem_fd = gt_transport_get_fd(t);
	modem_etat = TIOCM_DTR;
	changements_rts = 0;

	for(gint i = 0; i < 10; i++)
		g_assert_cmpint(gt_transport_get_modem_lines(t), ==, TIOCM_DTR);
	g_assert_cmpint(changements_rts, ==, 0);

	g_assert_cmpint(gt_transport_send(t, (const guint8 *)"x", 1), ==, 1);
	g_assert_cmpint(changements_rts, ==, 2);
	g_assert_cmpint(modem_etat & TIOCM_RTS, ==, 0);

	gt_transport_toggle_line(t, GT_MODEM_DTR);
	g_assert_cmpint(modem_etat & TIOCM_DTR, ==, 0);

	modem_fd = -1;
	gt_transport_close(t);
	transport_params_clear(&tp);
	fermer_pty(&p);
}

/* ---- Octets binaires transmis intacts ---------------------------------- */

static void
test_octets_binaires(void)
{
	paire_pty_t p;
	TransportParams tp;
	GtTransport *t;
	Recepteur r;

	if(!ouvrir_pty(&p))
	{
		g_test_skip("openpty indisponible");
		return;
	}
	recepteur_init(&r);
	params_serie(&tp, ttyname(p.esclave));
	t = gt_transport_open(&tp, &rappels, &r, NULL);
	g_assert_nonnull(t);
	g_assert_cmpint(write(p.maitre, "\xff\x00\x41", 3), ==, 3);
	g_assert_true(attendre_octets(&r, 3, 2000));
	g_assert_cmpmem(r.octets->data, r.octets->len, "\xff\x00\x41", 3);
	gt_transport_close(t);
	transport_params_clear(&tp);
	recepteur_liberer(&r);
	fermer_pty(&p);
}

/* ---- M3 : perte signalée à la fermeture du maître pty ------------------ */

static void
test_perte_maitre_pty(void)
{
	paire_pty_t p;
	TransportParams tp;
	GtTransport *t;
	Recepteur r;

	if(!ouvrir_pty(&p))
	{
		g_test_skip("openpty indisponible");
		return;
	}
	recepteur_init(&r);
	params_serie(&tp, ttyname(p.esclave));
	t = gt_transport_open(&tp, &rappels, &r, NULL);
	g_assert_nonnull(t);
	close(p.maitre);
	p.maitre = -1;
	g_assert_true(attendre_evenement(&r, TRANSPORT_EVENT_LOST, 2000));
	g_assert_cmpint(gt_transport_send(t, (const guint8 *)"x", 1), <, 0);
	g_assert_cmpint(gt_transport_get_modem_lines(t), ==, -1);
	gt_transport_close(t);
	transport_params_clear(&tp);
	recepteur_liberer(&r);
	fermer_pty(&p);
}

/* ---- Erreurs d'ouverture remontées par GError -------------------------- */

static void
test_erreurs_ouverture(void)
{
	TransportParams tp;
	GError *err = NULL;

	params_serie(&tp, "/nonexistent/gtkterm-test");
	g_assert_null(gt_transport_open(&tp, &rappels, NULL, &err));
	g_assert_error(err, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_OPEN);
	g_assert_nonnull(strstr(err->message, "/nonexistent/gtkterm-test"));
	g_clear_error(&err);
	transport_params_clear(&tp);

	params_serie(&tp, "/dev/null");
	g_assert_null(gt_transport_open(&tp, &rappels, NULL, &err));
	g_assert_error(err, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_NOT_TTY);
	g_clear_error(&err);
	transport_params_clear(&tp);
}

/* ---- C2 : pair TCP fermé ------------------------------------------------ */

static void
test_tcp_pair_ferme(void)
{
	TransportParams tp;
	GtTransport *t;
	Recepteur r;
	guint16 port;
	int ecoute, pair;
	gssize resultat = 0;

	recepteur_init(&r);
	ecoute = ecoute_locale(&port);
	params_tcp(&tp, TRANSPORT_TCP_CLIENT, port);
	t = gt_transport_open(&tp, &rappels, &r, NULL);
	g_assert_nonnull(t);
	pair = accept(ecoute, NULL, NULL);
	g_assert_cmpint(pair, >=, 0);
	g_assert_true(attendre_evenement(&r, TRANSPORT_EVENT_CONNECTED, 2000));

	close(pair);
	g_usleep(50000);
	/* Le premier envoi peut encore réussir (RST pas encore reçu) ; un suivant échoue au lieu de tuer le processus. */
	for(gint i = 0; i < 3; i++)
	{
		resultat = gt_transport_send(t, (const guint8 *)"abc", 3);
		iterer(20);
	}
	g_assert_cmpint(resultat, <, 0);

	gt_transport_close(t);
	transport_params_clear(&tp);
	recepteur_liberer(&r);
	close(ecoute);
}

/* ---- M5 : fin de flux TCP client signalée ------------------------------ */

static void
test_tcp_fin_de_flux(void)
{
	TransportParams tp;
	GtTransport *t;
	Recepteur r;
	guint16 port;
	int ecoute, pair;

	recepteur_init(&r);
	ecoute = ecoute_locale(&port);
	params_tcp(&tp, TRANSPORT_TCP_CLIENT, port);
	t = gt_transport_open(&tp, &rappels, &r, NULL);
	g_assert_nonnull(t);
	pair = accept(ecoute, NULL, NULL);
	g_assert_true(attendre_evenement(&r, TRANSPORT_EVENT_CONNECTED, 2000));
	g_assert_cmpint(write(pair, "hi", 2), ==, 2);
	g_assert_true(attendre_octets(&r, 2, 2000));
	close(pair);
	g_assert_true(attendre_evenement(&r, TRANSPORT_EVENT_LOST, 2000));
	gt_transport_close(t);
	transport_params_clear(&tp);
	recepteur_liberer(&r);
	close(ecoute);
}

/* ---- Connexion refusée : erreur synchrone ou LOST asynchrone ----------- */

static void
test_tcp_connexion_refusee(void)
{
	TransportParams tp;
	GtTransport *t;
	Recepteur r;
	GError *err = NULL;

	recepteur_init(&r);
	params_tcp(&tp, TRANSPORT_TCP_CLIENT, port_libre());
	t = gt_transport_open(&tp, &rappels, &r, &err);
	if(t == NULL)
	{
		g_assert_error(err, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_CONNECT);
		g_clear_error(&err);
	}
	else
	{
		g_assert_true(attendre_evenement(&r, TRANSPORT_EVENT_LOST, 2000));
		g_assert_cmpuint(compter(&r, TRANSPORT_EVENT_CONNECTED), ==, 0);
		g_assert_nonnull(strstr(r.detail, "Cannot connect"));
		gt_transport_close(t);
	}
	transport_params_clear(&tp);
	recepteur_liberer(&r);
}

/* ---- Serveur TCP : client accepté, données, départ du client ----------- */

static void
test_tcp_serveur_evenements(void)
{
	TransportParams tp;
	GtTransport *t;
	Recepteur r;
	guint16 port = port_libre();
	guint8 buf[2];
	int client;

	recepteur_init(&r);
	params_tcp(&tp, TRANSPORT_TCP_SERVER, port);
	t = gt_transport_open(&tp, &rappels, &r, NULL);
	g_assert_nonnull(t);
	g_assert_cmpint(gt_transport_send(t, (const guint8 *)"x", 1), ==, 0);	/* aucun client */

	client = connecter(port);
	g_assert_true(attendre_evenement(&r, TRANSPORT_EVENT_CLIENT_CONNECTED, 2000));
	g_assert_cmpint(write(client, "hi", 2), ==, 2);
	g_assert_true(attendre_octets(&r, 2, 2000));
	g_assert_cmpint(gt_transport_send(t, (const guint8 *)"yo", 2), ==, 2);
	g_assert_cmpuint(lire(client, buf, 2, 2000), ==, 2);
	g_assert_cmpmem(buf, 2, "yo", 2);

	close(client);
	g_assert_true(attendre_evenement(&r, TRANSPORT_EVENT_CLIENT_GONE, 2000));
	g_assert_cmpint(gt_transport_send(t, (const guint8 *)"x", 1), ==, 0);
	g_assert_cmpuint(compter(&r, TRANSPORT_EVENT_LOST), ==, 0);

	gt_transport_close(t);
	transport_params_clear(&tp);
	recepteur_liberer(&r);
}

/* ---- M1 : fermeture selon le type ouvert (socket d'écoute libérée une fois) */

static void
test_tcp_serveur_fermeture_libere_le_port(void)
{
	TransportParams tp;
	GtTransport *t;

	params_tcp(&tp, TRANSPORT_TCP_SERVER, port_libre());
	t = gt_transport_open(&tp, &rappels, NULL, NULL);
	g_assert_nonnull(t);
	gt_transport_close(t);
	t = gt_transport_open(&tp, &rappels, NULL, NULL);
	g_assert_nonnull(t);
	gt_transport_close(t);
	transport_params_clear(&tp);
}

/* ---- Deux instances simultanées ---------------------------------------- */

static void
test_deux_instances(void)
{
	paire_pty_t p1, p2;
	TransportParams tp1, tp2;
	GtTransport *t1, *t2;
	Recepteur r1, r2;

	if(!ouvrir_pty(&p1) || !ouvrir_pty(&p2))
	{
		g_test_skip("openpty indisponible");
		return;
	}
	recepteur_init(&r1);
	recepteur_init(&r2);
	params_serie(&tp1, ttyname(p1.esclave));
	params_serie(&tp2, ttyname(p2.esclave));
	t1 = gt_transport_open(&tp1, &rappels, &r1, NULL);
	t2 = gt_transport_open(&tp2, &rappels, &r2, NULL);
	g_assert_nonnull(t1);
	g_assert_nonnull(t2);
	g_assert_cmpint(write(p1.maitre, "un", 2), ==, 2);
	g_assert_cmpint(write(p2.maitre, "deux", 4), ==, 4);
	g_assert_true(attendre_octets(&r1, 2, 2000));
	g_assert_true(attendre_octets(&r2, 4, 2000));
	g_assert_cmpmem(r1.octets->data, r1.octets->len, "un", 2);
	g_assert_cmpmem(r2.octets->data, r2.octets->len, "deux", 4);
	gt_transport_close(t1);
	gt_transport_close(t2);
	transport_params_clear(&tp1);
	transport_params_clear(&tp2);
	recepteur_liberer(&r1);
	recepteur_liberer(&r2);
	fermer_pty(&p1);
	fermer_pty(&p2);
}

static void
test_null_tolere(void)
{
	gt_transport_close(NULL);
	g_assert_cmpint(gt_transport_send(NULL, (const guint8 *)"x", 1), ==, 0);
	g_assert_cmpint(gt_transport_get_modem_lines(NULL), ==, -1);
	g_assert_cmpint(gt_transport_get_fd(NULL), ==, -1);
	gt_transport_toggle_line(NULL, GT_MODEM_RTS);
	gt_transport_send_break(NULL);
}

static void
test_wait_char_ancien_codage(void)
{
	g_assert_cmpint(config_wait_char_from_file(0), ==, -1);
	g_assert_cmpint(config_wait_char_from_file(-1), ==, -1);
	g_assert_cmpint(config_wait_char_from_file(65), ==, 65);
	g_assert_cmpint(config_wait_char_from_file(255), ==, 255);
	g_assert_cmpint(config_wait_char_from_file(-23), ==, 233);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/transport/termios-sans-heritage", test_termios_sans_heritage);
	g_test_add_func("/transport/numero-de-peripherique", test_numero_de_peripherique);
	g_test_add_func("/transport/rs485-rts-stable", test_rs485_rts_stable);
	g_test_add_func("/transport/octets-binaires", test_octets_binaires);
	g_test_add_func("/transport/perte-maitre-pty", test_perte_maitre_pty);
	g_test_add_func("/transport/erreurs-ouverture", test_erreurs_ouverture);
	g_test_add_func("/transport/tcp-pair-ferme", test_tcp_pair_ferme);
	g_test_add_func("/transport/tcp-fin-de-flux", test_tcp_fin_de_flux);
	g_test_add_func("/transport/tcp-connexion-refusee", test_tcp_connexion_refusee);
	g_test_add_func("/transport/tcp-serveur-evenements", test_tcp_serveur_evenements);
	g_test_add_func("/transport/tcp-serveur-fermeture-libere-le-port", test_tcp_serveur_fermeture_libere_le_port);
	g_test_add_func("/transport/deux-instances", test_deux_instances);
	g_test_add_func("/transport/null-tolere", test_null_tolere);
	g_test_add_func("/transport/wait-char-ancien-codage", test_wait_char_ancien_codage);
	return g_test_run();
}
```

Dans `tests/meson.build`, remplacer l'entrée `'test_transport'` du dictionnaire `integration_tests` par :

```meson
	'test_transport' : {
		'sources' : [],
		'deps' : [util_dep],
		'link_args' : ['-Wl,--wrap=ioctl'],
	},
```

- [ ] **Step 2 : constater l'échec**

Run : `ninja -C build 2>&1 | grep -m5 error`
Expected : erreurs de compilation de `test_transport.c` (`TransportParams`, `gt_transport_open` inconnus).

- [ ] **Step 3 : réécrire `transport.h`**

Remplacer tout `src/transport.h` par :

```c
/* Transport par instance : série, client TCP, serveur TCP (sous-projet 2).
 * Aucune dépendance à l'interface : paramètres en entrée, fonctions de rappel en sortie, erreurs d'ouverture par GError.
 *
 * Contrat des rappels (boucle GLib par défaut) :
 * - on_data, CONNECTED, CLIENT_CONNECTED, CLIENT_GONE sont appelés depuis la surveillance du descripteur : le destinataire
 *   ne ferme pas le transport dans ces rappels ;
 * - LOST est signalé depuis une source d'inactivité, jamais pendant un appel au transport : le destinataire peut fermer le
 *   transport dans ce rappel. Après la perte, l'envoi échoue (-1, errno = EIO) jusqu'à la fermeture. */
#ifndef TRANSPORT_H_
#define TRANSPORT_H_

#include <glib.h>
#include <sys/types.h>
#include <termios.h>

typedef enum
{
	TRANSPORT_SERIAL,
	TRANSPORT_TCP_CLIENT,
	TRANSPORT_TCP_SERVER
} TransportType;

/* Mêmes valeurs que les champs parite et flux de .gtktermrc (0/1/2 et 0/1/2/3) */
typedef enum
{
	GT_PARITY_NONE,
	GT_PARITY_ODD,
	GT_PARITY_EVEN
} GtParity;

typedef enum
{
	GT_FLOW_NONE,
	GT_FLOW_XONXOFF,
	GT_FLOW_RTSCTS,
	GT_FLOW_RS485
} GtFlow;

typedef enum
{
	GT_MODEM_DTR,
	GT_MODEM_RTS
} GtModemLine;

typedef struct
{
	TransportType type;
	gchar *port;			/* série */
	guint baudrate;
	gint bits;			/* 5..8 */
	gint stopbits;			/* 1, 2 */
	GtParity parity;
	GtFlow flow;
	gint rs485_before_ms, rs485_after_ms;
	gboolean lock_port;
	gchar *host;			/* TCP ; vide ou NULL en serveur : toutes les interfaces */
	gchar *tcp_port;
} TransportParams;

/* Copie profonde ; à libérer avec transport_params_clear */
void transport_params_copy(TransportParams *dst, const TransportParams *src);
void transport_params_clear(TransportParams *p);

typedef enum
{
	TRANSPORT_EVENT_CONNECTED,		/* client TCP : connexion établie (asynchrone) */
	TRANSPORT_EVENT_CLIENT_CONNECTED,	/* serveur TCP : un client est accepté */
	TRANSPORT_EVENT_CLIENT_GONE,		/* serveur TCP : le client est parti, on écoute à nouveau */
	TRANSPORT_EVENT_LOST			/* série ou client TCP : liaison perdue ou connexion échouée (detail = raison) */
} TransportEvent;

typedef struct
{
	void (*on_data)(const guint8 *data, gsize len, gpointer user_data);
	void (*on_event)(TransportEvent ev, const gchar *detail, gpointer user_data);
} TransportCallbacks;

#define GT_TRANSPORT_ERROR (gt_transport_error_quark())

typedef enum
{
	GT_TRANSPORT_ERROR_OPEN,
	GT_TRANSPORT_ERROR_NOT_TTY,
	GT_TRANSPORT_ERROR_LOCKED,
	GT_TRANSPORT_ERROR_BAUDRATE,
	GT_TRANSPORT_ERROR_RESOLVE,
	GT_TRANSPORT_ERROR_SOCKET,
	GT_TRANSPORT_ERROR_BIND,
	GT_TRANSPORT_ERROR_LISTEN,
	GT_TRANSPORT_ERROR_CONNECT
} GtTransportError;

GQuark gt_transport_error_quark(void);

typedef struct _GtTransport GtTransport;

/* Toutes les fonctions ci-dessous tolèrent t == NULL (aucun effet, 0 ou -1). */
GtTransport *gt_transport_open(const TransportParams *params, const TransportCallbacks *cb, gpointer user_data, GError **error);
/* Ferme selon les paramètres ouverts (M1) : restauration termios et déverrouillage pour une instance série seulement. */
void         gt_transport_close(GtTransport *t);
/* Octets écrits, 0 si rien n'est ouvert (serveur sans client, connexion en cours), négatif en cas d'erreur. */
gssize       gt_transport_send(GtTransport *t, const guint8 *data, gsize len);
/* Masque TIOCM_*, -1 sans objet (TCP, pty sans lignes modem) ; une erreur de périphérique signale LOST. */
gint         gt_transport_get_modem_lines(GtTransport *t);
void         gt_transport_toggle_line(GtTransport *t, GtModemLine line);
void         gt_transport_send_break(GtTransport *t);
/* Débit réellement obtenu (série), 0 sinon. */
guint        gt_transport_get_actual_baudrate(GtTransport *t);
/* Descripteur d'envoi : série ou client TCP ; serveur : client accepté ; -1 sinon. */
gint         gt_transport_get_fd(GtTransport *t);
/* st_rdev relevé à l'ouverture (série), 0 sinon. */
dev_t        gt_transport_get_device_number(GtTransport *t);

/* Débits (baud.c, baudrates.c) */
struct baudrate
{
	unsigned int baud;
	speed_t speed;
};
extern const struct baudrate baudrate_list[];
extern const int baudrate_count;
extern const gboolean speed_t_is_sane;
speed_t find_standard_baudrate(unsigned int);
unsigned int speed_t_to_baud(speed_t);
unsigned int set_port_baudrate(unsigned int, int);

#endif
```

- [ ] **Step 4 : réécrire `transport.c`**

Remplacer tout `src/transport.c` par :

```c
/***********************************************************************/
/* transport.c                                                          */
/* ----------                                                           */
/*           GTKTerm Software                                           */
/*                                                                      */
/*   Purpose                                                            */
/*      Transport par instance : série, client TCP, serveur TCP         */
/*      (sous-projet 2 : aucune dépendance à l'interface)               */
/*                                                                      */
/***********************************************************************/

#include <glib.h>
#include <glib-unix.h>
#include <termios.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/prctl.h>
#include <linux/capability.h>

#include <config.h>
#include <glib/gi18n.h>

#include "i18n.h"
#include "transport.h"
#include "serial_termios.h"

#define BUFFER_RECEPTION 8192
#define WATCH_PRIORITY 10

struct _GtTransport
{
	TransportParams params;		/* copie des paramètres ouverts : la fermeture suit le type ouvert (M1) */
	TransportCallbacks cb;
	gpointer user_data;
	int fd;				/* série, client TCP ou socket d'écoute du serveur ; -1 si fermé */
	int client_fd;			/* serveur : client accepté, -1 sinon */
	guint watch_id;			/* lecture (série, client) ou acceptation (serveur) */
	guint client_watch_id;		/* serveur : lecture du client */
	guint connect_watch_id;		/* client TCP : connexion en cours */
	guint lost_idle_id;		/* signalement différé de LOST */
	gchar *lost_detail;
	gboolean lost;
	gboolean termios_saved;
	struct termios termios_save;
	gboolean locked;
	guint actual_baudrate;
	dev_t devnum;
};

G_DEFINE_QUARK(gt-transport-error-quark, gt_transport_error)

void transport_params_copy(TransportParams *dst, const TransportParams *src)
{
	*dst = *src;
	dst->port = g_strdup(src->port);
	dst->host = g_strdup(src->host);
	dst->tcp_port = g_strdup(src->tcp_port);
}

void transport_params_clear(TransportParams *p)
{
	g_clear_pointer(&p->port, g_free);
	g_clear_pointer(&p->host, g_free);
	g_clear_pointer(&p->tcp_port, g_free);
}

static void transport_remove_source(guint *id)
{
	if(*id != 0)
	{
		g_source_remove(*id);
		*id = 0;
	}
}

static void transport_stop_watches(GtTransport *t)
{
	transport_remove_source(&t->watch_id);
	transport_remove_source(&t->client_watch_id);
	transport_remove_source(&t->connect_watch_id);
}

static gboolean transport_lost_idle(gpointer data)
{
	GtTransport *t = data;
	gchar *detail = t->lost_detail;

	t->lost_idle_id = 0;
	t->lost_detail = NULL;
	/* Le destinataire peut fermer le transport dans ce rappel : t n'est plus utilisé ensuite */
	if(t->cb.on_event != NULL)
		t->cb.on_event(TRANSPORT_EVENT_LOST, detail, t->user_data);
	g_free(detail);
	return G_SOURCE_REMOVE;
}

/* Arrête les surveillances et signale LOST depuis une source d'inactivité (jamais pendant un appel au transport) */
static void transport_mark_lost(GtTransport *t, const gchar *detail)
{
	if(t->lost)
		return;
	t->lost = TRUE;
	transport_stop_watches(t);
	t->lost_detail = g_strdup(detail);
	t->lost_idle_id = g_idle_add(transport_lost_idle, t);
}

static void transport_close_client(GtTransport *t)
{
	transport_remove_source(&t->client_watch_id);
	if(t->client_fd != -1)
	{
		close(t->client_fd);
		t->client_fd = -1;
	}
}

static gboolean transport_read_cb(gint fd, GIOCondition cond, gpointer data)
{
	GtTransport *t = data;
	guint8 buf[BUFFER_RECEPTION];
	ssize_t n;
	gint err = 0;

	for(;;)
	{
		n = read(fd, buf, sizeof(buf));
		if(n > 0)
		{
			if(t->cb.on_data != NULL)
				t->cb.on_data(buf, (gsize)n, t->user_data);
			if((gsize)n == sizeof(buf))
				continue;
			return G_SOURCE_CONTINUE;
		}
		if(n < 0 && errno == EINTR)
			continue;
		if(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
		{
			if(!(cond & (G_IO_HUP | G_IO_ERR)))
				return G_SOURCE_CONTINUE;
		}
		else if(n < 0)
			err = errno;
		break;
	}

	/* Fin de flux, erreur ou raccrochage */
	if(t->params.type == TRANSPORT_TCP_SERVER)
	{
		t->client_watch_id = 0;		/* cette source se retire en renvoyant G_SOURCE_REMOVE */
		transport_close_client(t);
		if(t->cb.on_event != NULL)
			t->cb.on_event(TRANSPORT_EVENT_CLIENT_GONE, NULL, t->user_data);
		return G_SOURCE_REMOVE;
	}
	t->watch_id = 0;
	transport_mark_lost(t, err != 0 ? g_strerror(err) : _("end of stream"));
	return G_SOURCE_REMOVE;
}

static gboolean transport_open_serial(GtTransport *t, GError **error)
{
	const TransportParams *p = &t->params;
	struct termios termios_p;
	struct stat st;
	unsigned int speed_margin;
	int fd;

	fd = open(p->port, O_RDWR | O_NOCTTY | O_NDELAY);
	if(fd == -1)
	{
		gchar *err_str = strerror_utf8(errno);

		g_set_error(error, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_OPEN, _("Cannot open %s: %s\n"), p->port, err_str);
		g_free(err_str);
		return FALSE;
	}

	if(!isatty(fd))
	{
		close(fd);
		g_set_error(error, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_NOT_TTY, _("%s is not a valid serial port\n"), p->port);
		return FALSE;
	}

	if(p->lock_port)
	{
		if(flock(fd, LOCK_EX | LOCK_NB) == -1)
		{
			close(fd);
			g_set_error_literal(error, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_LOCKED,
			                    _("Cannot lock port! The serial port may currently be in use by another program.\n"));
			return FALSE;
		}
		t->locked = TRUE;
	}

	speed_margin = p->baudrate / (3 * (2U + p->bits + !!p->parity));
	t->actual_baudrate = set_port_baudrate(p->baudrate, fd);
	if(t->actual_baudrate < p->baudrate - speed_margin || t->actual_baudrate - speed_margin > p->baudrate)
	{
		close(fd);
		t->locked = FALSE;
		g_set_error(error, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_BAUDRATE, _("Unable to set baud rate %u"), p->baudrate);
		return FALSE;
	}

	if(fstat(fd, &st) == 0)
		t->devnum = st.st_rdev;

	tcgetattr(fd, &termios_p);
	t->termios_save = termios_p;
	t->termios_saved = TRUE;
	serial_termios_apply(&termios_p, p->bits, p->parity, p->stopbits, p->flow);
	tcsetattr(fd, TCSANOW, &termios_p);
	tcflush(fd, TCOFLUSH);
	tcflush(fd, TCIFLUSH);

	t->fd = fd;
	t->watch_id = g_unix_fd_add_full(WATCH_PRIORITY, fd, G_IO_IN | G_IO_HUP | G_IO_ERR, transport_read_cb, t, NULL);
	return TRUE;
}

static gboolean transport_connect_cb(gint fd, GIOCondition cond, gpointer data)
{
	GtTransport *t = data;
	int err = 0;
	socklen_t len = sizeof(err);

	t->connect_watch_id = 0;	/* cette source se retire en renvoyant G_SOURCE_REMOVE */
	if(getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0)
		err = errno;
	if(err != 0)
	{
		gchar *msg = g_strdup_printf(_("Cannot connect to %s:%s: %s\n"), t->params.host, t->params.tcp_port, g_strerror(err));

		transport_mark_lost(t, msg);
		g_free(msg);
		return G_SOURCE_REMOVE;
	}

	t->watch_id = g_unix_fd_add_full(WATCH_PRIORITY, fd, G_IO_IN | G_IO_HUP | G_IO_ERR, transport_read_cb, t, NULL);
	if(t->cb.on_event != NULL)
		t->cb.on_event(TRANSPORT_EVENT_CONNECTED, NULL, t->user_data);
	return G_SOURCE_REMOVE;
}

static gboolean transport_open_tcp_client(GtTransport *t, GError **error)
{
	struct addrinfo hints, *res, *rp;
	int fd = -1, ret, last_errno = 0;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	ret = getaddrinfo(t->params.host, t->params.tcp_port, &hints, &res);
	if(ret != 0)
	{
		g_set_error(error, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_RESOLVE, _("Cannot resolve %s: %s\n"), t->params.host, gai_strerror(ret));
		return FALSE;
	}

	for(rp = res; rp != NULL; rp = rp->ai_next)
	{
		fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if(fd == -1)
		{
			last_errno = errno;
			continue;
		}
		fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
		if(connect(fd, rp->ai_addr, rp->ai_addrlen) == 0 || errno == EINPROGRESS)
			break;
		last_errno = errno;
		close(fd);
		fd = -1;
	}
	freeaddrinfo(res);

	if(fd == -1)
	{
		g_set_error(error, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_CONNECT, _("Cannot connect to %s:%s: %s\n"), t->params.host, t->params.tcp_port,
		            g_strerror(last_errno));
		return FALSE;
	}

	/* Connexion asynchrone : CONNECTED ou LOST quand le descripteur devient inscriptible (M5) */
	t->fd = fd;
	t->connect_watch_id = g_unix_fd_add_full(WATCH_PRIORITY, fd, G_IO_OUT, transport_connect_cb, t, NULL);
	return TRUE;
}

static gboolean transport_accept_cb(gint fd, GIOCondition cond, gpointer data)
{
	GtTransport *t = data;
	int new_fd = accept(fd, NULL, NULL);

	if(new_fd < 0)
	{
		gint err = errno;

		if(err == EAGAIN || err == EWOULDBLOCK || err == EINTR)
			return G_SOURCE_CONTINUE;
		i18n_perror(_("Accept error"));
		if(err == EINVAL || err == EBADF)
		{
			t->watch_id = 0;
			return G_SOURCE_REMOVE;
		}
		return G_SOURCE_CONTINUE;
	}

	/* Un nouveau client remplace l'ancien */
	transport_close_client(t);
	fcntl(new_fd, F_SETFL, fcntl(new_fd, F_GETFL, 0) | O_NONBLOCK);
	t->client_fd = new_fd;
	t->client_watch_id = g_unix_fd_add_full(WATCH_PRIORITY, new_fd, G_IO_IN | G_IO_HUP | G_IO_ERR, transport_read_cb, t, NULL);
	if(t->cb.on_event != NULL)
		t->cb.on_event(TRANSPORT_EVENT_CLIENT_CONNECTED, NULL, t->user_data);
	return G_SOURCE_CONTINUE;
}

static gboolean transport_open_tcp_server(GtTransport *t, GError **error)
{
	struct addrinfo hints, *res;
	const gchar *host = (t->params.host != NULL && t->params.host[0] != '\0') ? t->params.host : NULL;
	int fd, ret, optval = 1;
	int port_num = t->params.tcp_port != NULL ? atoi(t->params.tcp_port) : 0;
	gboolean privileged = (port_num > 0 && port_num < 1024);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	ret = getaddrinfo(host, t->params.tcp_port, &hints, &res);
	if(ret != 0)
	{
		if(host == NULL)
			g_set_error(error, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_RESOLVE, _("Cannot resolve port %s: %s\n"), t->params.tcp_port, gai_strerror(ret));
		else
			g_set_error(error, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_RESOLVE, _("Cannot resolve %s:%s: %s\n"), host, t->params.tcp_port,
			            gai_strerror(ret));
		return FALSE;
	}

	fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if(fd == -1)
	{
		g_set_error(error, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_SOCKET, _("Cannot create socket: %s\n"), g_strerror(errno));
		freeaddrinfo(res);
		return FALSE;
	}

	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

	if(bind(fd, res->ai_addr, res->ai_addrlen) < 0)
	{
		gint bind_errno = errno;

		if(bind_errno == EACCES && privileged)
		{
			prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_NET_BIND_SERVICE, 0, 0);
			if(bind(fd, res->ai_addr, res->ai_addrlen) == 0)
				goto bound;
			bind_errno = errno;
		}
		g_set_error(error, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_BIND, _("Cannot bind to %s:%s: %s%s\n"), t->params.host ? t->params.host : "",
		            t->params.tcp_port, g_strerror(bind_errno),
		            (bind_errno == EACCES && privileged) ? _("\nUse: sudo setcap cap_net_bind_service=+ep gtkterm") : "");
		close(fd);
		freeaddrinfo(res);
		return FALSE;
	}
bound:
	freeaddrinfo(res);

	if(listen(fd, 1) < 0)
	{
		g_set_error(error, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_LISTEN, _("Cannot listen: %s\n"), g_strerror(errno));
		close(fd);
		return FALSE;
	}

	t->fd = fd;
	t->watch_id = g_unix_fd_add_full(WATCH_PRIORITY, fd, G_IO_IN, transport_accept_cb, t, NULL);
	return TRUE;
}

GtTransport *gt_transport_open(const TransportParams *params, const TransportCallbacks *cb, gpointer user_data, GError **error)
{
	GtTransport *t;
	gboolean ok;

	g_return_val_if_fail(params != NULL, NULL);

	t = g_new0(GtTransport, 1);
	transport_params_copy(&t->params, params);
	if(cb != NULL)
		t->cb = *cb;
	t->user_data = user_data;
	t->fd = -1;
	t->client_fd = -1;

	switch(params->type)
	{
	case TRANSPORT_SERIAL:
		ok = transport_open_serial(t, error);
		break;
	case TRANSPORT_TCP_CLIENT:
		ok = transport_open_tcp_client(t, error);
		break;
	case TRANSPORT_TCP_SERVER:
		ok = transport_open_tcp_server(t, error);
		break;
	default:
		g_set_error_literal(error, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_OPEN, _("No open port"));
		ok = FALSE;
	}

	if(!ok)
	{
		transport_params_clear(&t->params);
		g_free(t);
		return NULL;
	}
	return t;
}

void gt_transport_close(GtTransport *t)
{
	if(t == NULL)
		return;

	transport_remove_source(&t->lost_idle_id);
	transport_stop_watches(t);
	transport_close_client(t);

	if(t->fd != -1)
	{
		if(t->params.type == TRANSPORT_SERIAL)
		{
			if(t->termios_saved)
			{
				tcsetattr(t->fd, TCSANOW, &t->termios_save);
				tcflush(t->fd, TCOFLUSH);
				tcflush(t->fd, TCIFLUSH);
			}
			if(t->locked)
				flock(t->fd, LOCK_UN);
		}
		close(t->fd);
	}

	g_free(t->lost_detail);
	transport_params_clear(&t->params);
	g_free(t);
}

gssize gt_transport_send(GtTransport *t, const guint8 *data, gsize len)
{
	gboolean rs485;
	gssize written;
	gint saved_errno;
	int fd;

	if(t == NULL || len == 0)
		return 0;
	if(t->lost)
	{
		errno = EIO;
		return -1;
	}

	fd = (t->params.type == TRANSPORT_TCP_SERVER) ? t->client_fd : t->fd;
	if(fd == -1 || t->connect_watch_id != 0)
		return 0;

	rs485 = (t->params.type == TRANSPORT_SERIAL && t->params.flow == GT_FLOW_RS485);
	if(rs485)
	{
		ioctl(fd, TIOCMBIS, (int[]){TIOCM_RTS});
		if(t->params.rs485_before_ms > 0)
			usleep(t->params.rs485_before_ms * 1000);
	}

	/* En TCP, MSG_NOSIGNAL : un pair déjà fermé renvoie EPIPE au lieu de tuer GTKTerm par SIGPIPE (C2). */
	if(t->params.type == TRANSPORT_SERIAL)
		written = write(fd, data, len);
	else
		written = send(fd, data, len, MSG_NOSIGNAL);
	saved_errno = errno;

	if(rs485)
	{
		tcdrain(fd);
		if(t->params.rs485_after_ms > 0)
			usleep(t->params.rs485_after_ms * 1000);
		ioctl(fd, TIOCMBIC, (int[]){TIOCM_RTS});
	}

	errno = saved_errno;
	return written;
}

gint gt_transport_get_modem_lines(GtTransport *t)
{
	int stat;

	if(t == NULL || t->params.type != TRANSPORT_SERIAL || t->fd == -1 || t->lost)
		return -1;

	if(ioctl(t->fd, TIOCMGET, &stat) == -1)
	{
		/* Les pseudo-terminaux et certains périphériques n'ont pas de lignes modem (ENOTTY/EINVAL) : ce n'est pas une
		   perte de liaison. Toute autre erreur (EIO...) signifie que le périphérique a disparu. */
		if(errno != EINVAL && errno != ENOTTY)
			transport_mark_lost(t, g_strerror(errno));
		return -1;
	}
	return stat;
}

void gt_transport_toggle_line(GtTransport *t, GtModemLine line)
{
	int stat;
	int bit = (line == GT_MODEM_DTR) ? TIOCM_DTR : TIOCM_RTS;

	if(t == NULL || t->params.type != TRANSPORT_SERIAL || t->fd == -1 || t->lost)
		return;

	if(ioctl(t->fd, TIOCMGET, &stat) == -1)
	{
		i18n_perror(_("Control signals read set signals"));
		return;
	}
	stat ^= bit;
	if(ioctl(t->fd, TIOCMSET, &stat) == -1)
		i18n_perror(line == GT_MODEM_DTR ? _("DTR write") : _("RTS write"));
}

void gt_transport_send_break(GtTransport *t)
{
	if(t == NULL || t->params.type != TRANSPORT_SERIAL || t->fd == -1 || t->lost)
		return;
	tcsendbreak(t->fd, 0);
}

guint gt_transport_get_actual_baudrate(GtTransport *t)
{
	if(t == NULL || t->params.type != TRANSPORT_SERIAL)
		return 0;
	return t->actual_baudrate;
}

gint gt_transport_get_fd(GtTransport *t)
{
	if(t == NULL)
		return -1;
	if(t->params.type == TRANSPORT_TCP_SERVER)
		return t->client_fd;
	return t->fd;
}

dev_t gt_transport_get_device_number(GtTransport *t)
{
	if(t == NULL)
		return 0;
	return t->devnum;
}
```

- [ ] **Step 5 : déplacer la table des débits, réduire `serial.h` à la façade, typer la configuration**

1. `src/baud.c`, `src/baudrates.c`, `tests/test_baudrates.c` : remplacer `#include "serial.h"` par `#include "transport.h"`.
2. `src/serial.h` : supprimer le bloc `struct baudrate { … }` jusqu'à `unsigned int speed_t_to_baud(speed_t);` inclus, la déclaration `unsigned int set_port_baudrate(unsigned int, int);`, la ligne `extern int serial_port_fd;` et la ligne `#define BUFFER_RECEPTION 8192`. Ajouter `#include "transport.h"` après le bloc `#include <termios.h>`, et la déclaration `int serial_get_fd(void);` après `gchar* get_port_string(void);`. Il garde `Send_chars`, `Config_port`, `Set_signals`, `lis_sig`, `Close_port`, `configure_*`, `sendbreak`, `get_port_string`, `BUFFER_EMISSION`, `LINE_FEED`, `POLL_DELAY`.
3. `src/term_config.h` : remplacer les deux champs

```c
	gint parite;                 // 0 : None, 1 : Odd, 2 : Even
	gint flux;                   // 0 : None, 1 : Xon/Xoff, 2 : RTS/CTS, 3 : RS485halfduplex
```

par

```c
	GtParity parite;             // GT_PARITY_NONE, _ODD, _EVEN (0, 1, 2 dans .gtktermrc)
	GtFlow flux;                 // GT_FLOW_NONE, _XONXOFF, _RTSCTS, _RS485 (0 à 3 dans .gtktermrc)
```

remplacer `#define DEFAULT_PARITY 0` par `#define DEFAULT_PARITY GT_PARITY_NONE` et `#define DEFAULT_FLOW 0` par `#define DEFAULT_FLOW GT_FLOW_NONE`, et ajouter après la définition de `struct configuration_port` :

```c
/* Unique point de conversion de la configuration vers les paramètres du transport ; à libérer avec transport_params_clear */
void config_to_transport_params(const struct configuration_port *c, TransportParams *p);
```

4. `src/term_config.c` : ajouter (après `ConfigFlags`) :

```c
void config_to_transport_params(const struct configuration_port *c, TransportParams *p)
{
	memset(p, 0, sizeof(*p));
	p->type = c->transport_type;
	p->port = g_strdup(c->port);
	p->baudrate = c->vitesse;
	p->bits = c->bits;
	p->stopbits = c->stops;
	p->parity = c->parite;
	p->flow = c->flux;
	p->rs485_before_ms = c->rs485_rts_time_before_transmit;
	p->rs485_after_ms = c->rs485_rts_time_after_transmit;
	p->lock_port = !c->disable_port_lock;
	p->host = g_strdup(c->socket_host);
	p->tcp_port = g_strdup(c->socket_port);
}
```

Puis remplacer les nombres magiques par les noms d'enum dans `term_config.c` et `cmdline.c` (valeurs identiques) : `config.parite = 0|1|2` → `GT_PARITY_NONE|GT_PARITY_ODD|GT_PARITY_EVEN` ; `config.flux = 0|1|2|3` → `GT_FLOW_NONE|GT_FLOW_XONXOFF|GT_FLOW_RTSCTS|GT_FLOW_RS485` ; dans `switch(config.parite)` et `switch(config.flux)`, `case 0:`… → les mêmes noms. Lister les lignes avec `grep -n 'parite\|flux' src/term_config.c src/cmdline.c` et n'en laisser aucune avec un littéral.

5. `src/meson.build` : dans `core_sources`, ajouter `'transport.c',` et `'transport.h',` ; dans `sources` (exécutable), retirer `'transport.c',` et `'transport.h',`.

- [ ] **Step 6 : pont provisoire dans `serial.c`**

Remplacer tout `src/serial.c` par :

```c
/***********************************************************************/
/* serial.c                                                             */
/*                                                                      */
/*   Purpose                                                            */
/*      Pont provisoire (sous-projet 2, tâches 5 et 6) : l'ancienne     */
/*      façade sur la nouvelle interface du transport. Supprimé par la  */
/*      tâche 7, quand l'interface passe par GtkTermLink.               */
/*                                                                      */
/***********************************************************************/

#include <gtk/gtk.h>
#include <glib.h>

#include "term_config.h"
#include "serial.h"
#include "transport.h"
#include "interface.h"
#include "buffer.h"
#include "files.h"

#include <config.h>
#include <glib/gi18n.h>

extern struct configuration_port config;

static GtTransport *pont = NULL;

static void pont_status(void)
{
	gchar *msg = get_port_string();

	Set_status_message(msg);
	Set_window_title(msg);
	g_free(msg);
}

static void pont_on_data(const guint8 *data, gsize len, gpointer user_data)
{
	put_chars((const char *)data, (unsigned int)len, config.crlfauto);
	if(config.car != -1 && waiting_for_char)
	{
		for(gsize i = 0; i < len; i++)
		{
			if(data[i] == (guint8)config.car)
			{
				waiting_for_char = FALSE;
				add_input();
				break;
			}
		}
	}
}

static void pont_on_event(TransportEvent ev, const gchar *detail, gpointer user_data)
{
	if(ev == TRANSPORT_EVENT_LOST)
	{
		gt_transport_close(pont);
		pont = NULL;
	}
	pont_status();
}

int Send_chars(char *string, int length)
{
	return (int)gt_transport_send(pont, (const guint8 *)string, (gsize)length);
}

gboolean Config_port(void)
{
	static const TransportCallbacks callbacks = { pont_on_data, pont_on_event };
	TransportParams params;
	GError *error = NULL;

	Close_port();
	config_to_transport_params(&config, &params);
	pont = gt_transport_open(&params, &callbacks, NULL, &error);
	transport_params_clear(&params);
	if(pont == NULL)
	{
		show_message(error->message, MSG_ERR);
		g_error_free(error);
		return FALSE;
	}
	Set_local_echo(config.echo);
	return TRUE;
}

void Close_port(void)
{
	gt_transport_close(pont);
	pont = NULL;
}

void Set_signals(guint param)
{
	gt_transport_toggle_line(pont, param == 0 ? GT_MODEM_DTR : GT_MODEM_RTS);
}

int lis_sig(void)
{
	return gt_transport_get_modem_lines(pont);
}

void sendbreak(void)
{
	gt_transport_send_break(pont);
}

int serial_get_fd(void)
{
	return gt_transport_get_fd(pont);
}

void configure_echo(gboolean echo)
{
	config.echo = echo;
}

void configure_crlfauto(gboolean crlfauto)
{
	config.crlfauto = crlfauto;
}

void configure_autoreconnect_enable(gboolean autoreconnect)
{
	config.autoreconnect_enabled = autoreconnect;
}

void configure_esc_clear_screen(gboolean esc_clear_screen)
{
	config.esc_clear_screen = esc_clear_screen;
}

gchar *get_port_string(void)
{
	static const gchar parity_char[] = { 'N', 'O', 'E' };

	if(pont == NULL)
		return g_strdup(_("No open port"));
	if(config.transport_type == TRANSPORT_SERIAL)
		return g_strdup_printf("%s  %u-%d-%c-%d", config.port, gt_transport_get_actual_baudrate(pont), config.bits,
		                       parity_char[config.parite <= GT_PARITY_EVEN ? config.parite : 0], config.stops);
	if(config.transport_type == TRANSPORT_TCP_CLIENT)
		return g_strdup_printf("tcp://%s:%s", config.socket_host, config.socket_port);
	return g_strdup_printf("tcp://%s:%s (server)", config.socket_host[0] ? config.socket_host : "*", config.socket_port);
}
```

Dans `src/files.c`, fonction `add_input`, remplacer `transport_get_fd()` par `serial_get_fd()` et retirer `#include "transport.h"` si plus rien ne l'utilise (il reste inclus par `term_config.h`).

- [ ] **Step 7 : constater la réussite**

Run : `ninja -C build 2>&1 | grep -E 'error|warning: implicit' ; meson test -C build test_transport test_baudrates --print-errorlogs`
Expected : aucune erreur de compilation ; `test_transport OK`, `test_baudrates OK`.

- [ ] **Step 8 : suite complète, vérification du cœur et commit**

Run : `meson test -C build --print-errorlogs` — Expected : aucun échec.
Run : `grep -nE '#include *[<"](gtk/|vte/|interface\.h|buffer\.h|files\.h)' src/transport.c` — Expected : aucune sortie.

```bash
git add -A src tests
git commit -m "transport : instance sans interface, rappels et GError ; enums de parité et de flux

Le transport passe dans gtkterm_core : paramètres en entrée, fonctions de rappel en sortie, état par instance,
fermeture selon le type ouvert (M1), LOST signalé à la perte au lieu d'une reconnexion interne. serial.c devient un
pont provisoire vers la nouvelle interface jusqu'au branchement de la liaison.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6 : liaison du port principal (`GtkTermLink`)

**Files :**
- Create : `src/link.h`, `src/link.c`, `tests/test_link.c`
- Modify : `tests/meson.build` (dictionnaire `integration_tests`)

**Interfaces :**
- Consumes : transport (tâche 5), `GtHistory` (tâche 1), `device_match` (tâche 3).
- Produces (utilisés par les tâches 7 à 9) :
  ```c
  typedef enum { GT_LINK_CLOSED, GT_LINK_CONNECTING, GT_LINK_OPEN, GT_LINK_LISTENING, GT_LINK_LOST, GT_LINK_RECONNECTING } GtLinkState;
  #define GT_TYPE_LINK_STATE (gt_link_state_get_type())
  #define GT_TYPE_LINK (gt_link_get_type())
  G_DECLARE_FINAL_TYPE(GtkTermLink, gt_link, GT, LINK, GObject)
  GtkTermLink *gt_link_new(void);
  void         gt_link_set_default(GtkTermLink *link);
  GtkTermLink *gt_link_get_default(void);
  gboolean     gt_link_open(GtkTermLink *link, const TransportParams *params, GError **error);
  void         gt_link_close(GtkTermLink *link);
  gssize       gt_link_send(GtkTermLink *link, const guint8 *data, gsize len, GtSendOrigin origin);
  gint         gt_link_get_modem_lines(GtkTermLink *link);
  void         gt_link_toggle_line(GtkTermLink *link, GtModemLine line);
  void         gt_link_send_break(GtkTermLink *link);
  gint         gt_link_get_fd(GtkTermLink *link);
  GtLinkState  gt_link_get_state(GtkTermLink *link);
  const gchar *gt_link_get_status_text(GtkTermLink *link);
  GtHistory   *gt_link_get_history(GtkTermLink *link);
  void         gt_link_clear_history(GtkTermLink *link);
  void         gt_link_handle_device_event(GtkTermLink *link, const gchar *action, dev_t devnum, const gchar *path);
  ```
  Propriétés : `state` (enum, lecture), `status-text` (chaîne, lecture), `auto-reconnect` (booléen), `reconnect-interval-ms` (entier, 2000).
  Signaux : `open-failed (const gchar *message)` ; `received (GBytes *data, gint64 wall_time_us)` ; `sent (GBytes *data, gint origin)` ; `history-cleared`.
  Gestionnaires : `void (*)(GtkTermLink *, const gchar *, gpointer)`, `void (*)(GtkTermLink *, GBytes *, gint64, gpointer)`, `void (*)(GtkTermLink *, GBytes *, gint, gpointer)`, `void (*)(GtkTermLink *, gpointer)`.
  Textes d'état (identifiants gettext) : `"No open port"`, `"%s  %u-%d-%c-%d"` (chemin complet), `"tcp://%s:%s"`, `"tcp://%s:%s (server)"`, `"tcp://%s:%s (server, no client)"`, suffixes `"%s — connecting…"`, `"%s — lost"`, `"%s — reconnecting…"`.

Machine d'état (spec §3 plus la décision 1) :

| État | Événement | Nouvel état | Effet |
|---|---|---|---|
| tout | `gt_link_open` réussi | `OPEN` (série), `LISTENING` (serveur), `CONNECTING` (client TCP) | moniteur udev armé si série |
| tout | `gt_link_open` échoué | `CLOSED` ; puis `RECONNECTING` si `auto-reconnect` et transport série ou client TCP | `open-failed`, puis minuterie (et moniteur udev armé si série) ; `gt_link_open` renvoie FALSE |
| `CONNECTING` | `CONNECTED` | `OPEN` | — |
| `CONNECTING` | `LOST` (connexion échouée) | `CLOSED` ; puis `RECONNECTING` si `auto-reconnect` | `open-failed` (détail du transport), puis minuterie |
| `OPEN`/`LISTENING` | `LOST`, ou udev « remove » du périphérique ouvert | `LOST` | transport fermé ; puis `RECONNECTING` si `auto-reconnect` |
| `RECONNECTING` | minuterie ou udev « add » du chemin configuré | `OPEN`/`LISTENING` si l'ouverture réussit (client TCP : à `CONNECTED`) | aucun `open-failed` |
| `LISTENING` | `CLIENT_CONNECTED` | `OPEN` | — |
| `OPEN` (serveur) | `CLIENT_GONE` | `LISTENING` | — |
| tout | `gt_link_close` | `CLOSED` | minuterie et moniteur arrêtés |
| `LOST` | `auto-reconnect` → vrai | `RECONNECTING` | — |
| `RECONNECTING` | `auto-reconnect` → faux | `LOST` | minuterie arrêtée, tentative TCP en cours abandonnée |

Amendement (relecture finale, F3) : comme avant la refonte, la reconnexion automatique rattrape aussi une ouverture demandée qui échoue (adaptateur branché ou serveur TCP démarré après GTKTerm). La boîte `open-failed` reste affichée une fois ; si son gestionnaire a rouvert ou fermé la liaison entre-temps, la reconnexion n'est pas lancée. Serveur TCP : inchangé (`CLOSED`).

- [ ] **Step 1 : écrire le test**

`tests/test_link.c` :

```c
/* Tests d'intégration de la liaison (link.c) : machine d'état sur paire pty (lien symbolique recréé) et TCP local.
 * Les événements udev sont simulés par gt_link_handle_device_event ; aucune fenêtre, aucune boîte de dialogue. */

#include <glib.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <poll.h>
#include <pty.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "link.h"

/* ---- Observateur des signaux ----------------------------------------- */

typedef struct
{
	gint open_failed;
	gchar *dernier_message;
	GByteArray *recu;
	GByteArray *envoye;
	gint derniere_origine;
	gint effacements;
} Observateur;

static void
sur_open_failed(GtkTermLink *link, const gchar *message, gpointer data)
{
	Observateur *o = data;

	o->open_failed++;
	g_free(o->dernier_message);
	o->dernier_message = g_strdup(message);
}

static void
sur_received(GtkTermLink *link, GBytes *octets, gint64 heure, gpointer data)
{
	gsize n;
	const guint8 *p = g_bytes_get_data(octets, &n);

	g_assert_cmpint(heure, >, 0);
	g_byte_array_append(((Observateur *)data)->recu, p, (guint)n);
}

static void
sur_sent(GtkTermLink *link, GBytes *octets, gint origine, gpointer data)
{
	Observateur *o = data;
	gsize n;
	const guint8 *p = g_bytes_get_data(octets, &n);

	g_byte_array_append(o->envoye, p, (guint)n);
	o->derniere_origine = origine;
}

static void
sur_cleared(GtkTermLink *link, gpointer data)
{
	((Observateur *)data)->effacements++;
}

static GtkTermLink *
nouvelle_liaison(Observateur *o)
{
	GtkTermLink *link = gt_link_new();

	memset(o, 0, sizeof(*o));
	o->recu = g_byte_array_new();
	o->envoye = g_byte_array_new();
	g_signal_connect(link, "open-failed", G_CALLBACK(sur_open_failed), o);
	g_signal_connect(link, "received", G_CALLBACK(sur_received), o);
	g_signal_connect(link, "sent", G_CALLBACK(sur_sent), o);
	g_signal_connect(link, "history-cleared", G_CALLBACK(sur_cleared), o);
	g_object_set(link, "reconnect-interval-ms", 50, NULL);
	return link;
}

static void
liberer(GtkTermLink *link, Observateur *o)
{
	gt_link_close(link);
	g_object_unref(link);
	g_byte_array_unref(o->recu);
	g_byte_array_unref(o->envoye);
	g_free(o->dernier_message);
}

/* ---- Boucle ------------------------------------------------------------ */

static void
iterer(guint ms)
{
	gint64 fin = g_get_monotonic_time() + (gint64)ms * 1000;

	while(g_get_monotonic_time() < fin)
	{
		g_main_context_iteration(NULL, FALSE);
		g_usleep(1000);
	}
}

static gboolean
attendre_etat(GtkTermLink *link, GtLinkState etat, guint ms)
{
	gint64 fin = g_get_monotonic_time() + (gint64)ms * 1000;

	while(gt_link_get_state(link) != etat && g_get_monotonic_time() < fin)
	{
		g_main_context_iteration(NULL, FALSE);
		g_usleep(1000);
	}
	return gt_link_get_state(link) == etat;
}

static gboolean
attendre_octets(GByteArray *a, guint n, guint ms)
{
	gint64 fin = g_get_monotonic_time() + (gint64)ms * 1000;

	while(a->len < n && g_get_monotonic_time() < fin)
	{
		g_main_context_iteration(NULL, FALSE);
		g_usleep(1000);
	}
	return a->len >= n;
}

static gsize
lire(int fd, guint8 *buf, gsize taille, guint ms)
{
	gsize lu = 0;
	gint64 fin = g_get_monotonic_time() + (gint64)ms * 1000;

	while(lu < taille && g_get_monotonic_time() < fin)
	{
		struct pollfd pfd = { fd, POLLIN, 0 };

		if(poll(&pfd, 1, 10) > 0)
		{
			ssize_t n = read(fd, buf + lu, taille - lu);

			if(n > 0)
				lu += (gsize)n;
		}
		g_main_context_iteration(NULL, FALSE);
	}
	return lu;
}

/* ---- Port pty derrière un lien symbolique stable ------------------------ */

typedef struct
{
	int maitre;
	int esclave;
	gchar *dir;
	gchar *lien;
} Port;

#define PORT_INIT { -1, -1, NULL, NULL }

static void
port_brancher(Port *p)
{
	g_assert_cmpint(openpty(&p->maitre, &p->esclave, NULL, NULL, NULL), ==, 0);
	if(p->dir == NULL)
		p->dir = g_dir_make_tmp("gtkterm-link-XXXXXX", NULL);
	if(p->lien == NULL)
		p->lien = g_build_filename(p->dir, "port", NULL);
	g_unlink(p->lien);
	g_assert_cmpint(symlink(ttyname(p->esclave), p->lien), ==, 0);
}

/* Le maître et l'esclave disparaissent ; le lien reste, pendant */
static void
port_debrancher(Port *p)
{
	close(p->esclave);
	close(p->maitre);
	p->esclave = p->maitre = -1;
}

static void
port_detruire(Port *p)
{
	if(p->maitre >= 0)
		port_debrancher(p);
	g_unlink(p->lien);
	g_rmdir(p->dir);
	g_free(p->lien);
	g_free(p->dir);
}

static void
params_serie(TransportParams *tp, const gchar *chemin)
{
	memset(tp, 0, sizeof(*tp));
	tp->type = TRANSPORT_SERIAL;
	tp->port = g_strdup(chemin);
	tp->baudrate = 115200;
	tp->bits = 8;
	tp->stopbits = 1;
	tp->parity = GT_PARITY_NONE;
	tp->flow = GT_FLOW_NONE;
	tp->lock_port = FALSE;
}

static void
params_tcp(TransportParams *tp, TransportType type, guint16 port)
{
	memset(tp, 0, sizeof(*tp));
	tp->type = type;
	tp->host = g_strdup("127.0.0.1");
	tp->tcp_port = g_strdup_printf("%u", port);
}

static void
ouvrir_serie(GtkTermLink *link, Port *p)
{
	TransportParams tp;
	GError *err = NULL;

	params_serie(&tp, p->lien);
	g_assert_true(gt_link_open(link, &tp, &err));
	g_assert_no_error(err);
	transport_params_clear(&tp);
}

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

typedef struct
{
	guint rx, tx;
	GtSendOrigin origine_tx;
	GString *rx_texte;
} Bilan;

static void
faire_bilan(const GtRecord *rec, gpointer data)
{
	Bilan *b = data;

	if(rec->kind == GT_RECORD_RX)
	{
		b->rx++;
		g_string_append_len(b->rx_texte, (const gchar *)rec->data, (gssize)rec->len);
	}
	else
	{
		b->tx++;
		b->origine_tx = rec->origin;
	}
}

/* ---- Ouverture ---------------------------------------------------------- */

static void
test_ouverture_reussie(void)
{
	Observateur o;
	Port p = PORT_INIT;
	GtkTermLink *link = nouvelle_liaison(&o);
	gchar *attendu;

	port_brancher(&p);
	ouvrir_serie(link, &p);
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_OPEN);
	attendu = g_strdup_printf("%s  115200-8-N-1", p.lien);
	g_assert_cmpstr(gt_link_get_status_text(link), ==, attendu);
	g_assert_cmpint(o.open_failed, ==, 0);
	g_assert_cmpint(gt_link_get_fd(link), >=, 0);
	g_free(attendu);
	liberer(link, &o);
	port_detruire(&p);
}

static void
test_ouverture_echouee(void)
{
	Observateur o;
	GtkTermLink *link = nouvelle_liaison(&o);
	TransportParams tp;
	GError *err = NULL;

	params_serie(&tp, "/nonexistent/gtkterm-test");
	g_assert_false(gt_link_open(link, &tp, &err));
	g_assert_error(err, GT_TRANSPORT_ERROR, GT_TRANSPORT_ERROR_OPEN);
	g_assert_cmpint(o.open_failed, ==, 1);
	g_assert_nonnull(strstr(o.dernier_message, "/nonexistent/gtkterm-test"));
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_CLOSED);
	g_assert_cmpstr(gt_link_get_status_text(link), ==, "No open port");
	g_clear_error(&err);
	transport_params_clear(&tp);
	liberer(link, &o);
}

/* ---- Réception, envoi, historique -------------------------------------- */

static void
test_reception_et_historique(void)
{
	Observateur o;
	Port p = PORT_INIT;
	GtkTermLink *link = nouvelle_liaison(&o);
	Bilan b = { 0, 0, GT_SEND_KEYBOARD, g_string_new(NULL) };

	port_brancher(&p);
	ouvrir_serie(link, &p);
	g_assert_cmpint(write(p.maitre, "abc", 3), ==, 3);
	g_assert_true(attendre_octets(o.recu, 3, 2000));
	g_assert_cmpmem(o.recu->data, o.recu->len, "abc", 3);
	gt_history_foreach(gt_link_get_history(link), faire_bilan, &b);
	g_assert_cmpuint(b.tx, ==, 0);
	g_assert_cmpstr(b.rx_texte->str, ==, "abc");
	g_string_free(b.rx_texte, TRUE);
	liberer(link, &o);
	port_detruire(&p);
}

static void
test_envoi_et_historique(void)
{
	Observateur o;
	Port p = PORT_INIT;
	GtkTermLink *link = nouvelle_liaison(&o);
	Bilan b = { 0, 0, GT_SEND_KEYBOARD, g_string_new(NULL) };
	guint8 buf[3];

	port_brancher(&p);
	ouvrir_serie(link, &p);
	g_assert_cmpint(gt_link_send(link, (const guint8 *)"xyz", 3, GT_SEND_MACRO), ==, 3);
	g_assert_cmpuint(lire(p.maitre, buf, 3, 2000), ==, 3);
	g_assert_cmpmem(buf, 3, "xyz", 3);
	g_assert_cmpmem(o.envoye->data, o.envoye->len, "xyz", 3);
	g_assert_cmpint(o.derniere_origine, ==, GT_SEND_MACRO);
	gt_history_foreach(gt_link_get_history(link), faire_bilan, &b);
	g_assert_cmpuint(b.tx, ==, 1);
	g_assert_cmpint(b.origine_tx, ==, GT_SEND_MACRO);
	g_string_free(b.rx_texte, TRUE);
	liberer(link, &o);
	port_detruire(&p);
}

static void
test_effacer_historique(void)
{
	Observateur o;
	Port p = PORT_INIT;
	GtkTermLink *link = nouvelle_liaison(&o);

	port_brancher(&p);
	ouvrir_serie(link, &p);
	g_assert_cmpint(write(p.maitre, "abc", 3), ==, 3);
	g_assert_true(attendre_octets(o.recu, 3, 2000));
	gt_link_clear_history(link);
	g_assert_cmpint(o.effacements, ==, 1);
	g_assert_cmpuint(gt_history_count(gt_link_get_history(link)), ==, 0);
	liberer(link, &o);
	port_detruire(&p);
}

/* ---- Perte et reconnexion (M3) ----------------------------------------- */

static void
test_perte_sans_reconnexion(void)
{
	Observateur o;
	Port p = PORT_INIT;
	GtkTermLink *link = nouvelle_liaison(&o);

	port_brancher(&p);
	ouvrir_serie(link, &p);
	port_debrancher(&p);
	g_assert_true(attendre_etat(link, GT_LINK_LOST, 2000));
	g_assert_true(g_str_has_suffix(gt_link_get_status_text(link), " — lost"));
	g_assert_true(g_str_has_prefix(gt_link_get_status_text(link), p.lien));
	g_assert_cmpint(gt_link_send(link, (const guint8 *)"x", 1, GT_SEND_KEYBOARD), ==, 0);
	g_assert_cmpint(gt_link_get_fd(link), ==, -1);
	iterer(200);
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_LOST);
	g_assert_cmpint(o.open_failed, ==, 0);
	liberer(link, &o);
	port_detruire(&p);
}

static void
test_perte_puis_reconnexion(void)
{
	Observateur o;
	Port p = PORT_INIT;
	GtkTermLink *link = nouvelle_liaison(&o);

	g_object_set(link, "auto-reconnect", TRUE, NULL);
	port_brancher(&p);
	ouvrir_serie(link, &p);
	port_debrancher(&p);
	g_assert_true(attendre_etat(link, GT_LINK_RECONNECTING, 2000));
	g_assert_true(g_str_has_suffix(gt_link_get_status_text(link), " — reconnecting…"));
	iterer(200);	/* plusieurs essais silencieux sur un lien pendant */
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_RECONNECTING);
	port_brancher(&p);
	g_assert_true(attendre_etat(link, GT_LINK_OPEN, 2000));
	g_assert_cmpint(o.open_failed, ==, 0);
	g_assert_cmpint(write(p.maitre, "ok", 2), ==, 2);
	g_assert_true(attendre_octets(o.recu, 2, 2000));
	liberer(link, &o);
	port_detruire(&p);
}

static void
test_fermeture_annule_reconnexion(void)
{
	Observateur o;
	Port p = PORT_INIT;
	GtkTermLink *link = nouvelle_liaison(&o);

	g_object_set(link, "auto-reconnect", TRUE, NULL);
	port_brancher(&p);
	ouvrir_serie(link, &p);
	port_debrancher(&p);
	g_assert_true(attendre_etat(link, GT_LINK_RECONNECTING, 2000));
	gt_link_close(link);
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_CLOSED);
	g_assert_cmpstr(gt_link_get_status_text(link), ==, "No open port");
	port_brancher(&p);
	iterer(300);
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_CLOSED);
	liberer(link, &o);
	port_detruire(&p);
}

static void
test_bascule_auto_reconnect(void)
{
	Observateur o;
	Port p = PORT_INIT;
	GtkTermLink *link = nouvelle_liaison(&o);

	port_brancher(&p);
	ouvrir_serie(link, &p);
	port_debrancher(&p);
	g_assert_true(attendre_etat(link, GT_LINK_LOST, 2000));
	g_object_set(link, "auto-reconnect", TRUE, NULL);
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_RECONNECTING);
	g_object_set(link, "auto-reconnect", FALSE, NULL);
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_LOST);
	iterer(200);
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_LOST);
	liberer(link, &o);
	port_detruire(&p);
}

static void
test_destruction_pendant_reconnexion(void)
{
	Observateur o;
	Port p = PORT_INIT;
	GtkTermLink *link = nouvelle_liaison(&o);

	g_object_set(link, "auto-reconnect", TRUE, NULL);
	port_brancher(&p);
	ouvrir_serie(link, &p);
	port_debrancher(&p);
	g_assert_true(attendre_etat(link, GT_LINK_RECONNECTING, 2000));
	g_object_unref(link);		/* sans gt_link_close : la minuterie doit partir avec l'objet */
	iterer(200);
	g_byte_array_unref(o.recu);
	g_byte_array_unref(o.envoye);
	g_free(o.dernier_message);
	port_detruire(&p);
}

/* ---- Événements udev simulés (M2, M6) ---------------------------------- */

static void
test_udev_simule(void)
{
	Observateur o;
	Port p = PORT_INIT;
	GtkTermLink *link = nouvelle_liaison(&o);
	struct stat st;

	g_object_set(link, "auto-reconnect", TRUE, "reconnect-interval-ms", 60000, NULL);
	port_brancher(&p);
	ouvrir_serie(link, &p);
	g_assert_cmpint(stat(p.lien, &st), ==, 0);

	gt_link_handle_device_event(link, "remove", 0, "/dev/sans-rapport");
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_OPEN);

	/* Correspondance par numéro de périphérique, même sous un autre nom */
	gt_link_handle_device_event(link, "remove", st.st_rdev, "/dev/autre-nom");
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_RECONNECTING);

	gt_link_handle_device_event(link, "add", 0, "/dev/sans-rapport");
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_RECONNECTING);

	/* Retour du chemin configuré : essai immédiat, sans attendre la minuterie */
	gt_link_handle_device_event(link, "add", 0, p.lien);
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_OPEN);
	g_assert_cmpint(o.open_failed, ==, 0);
	liberer(link, &o);
	port_detruire(&p);
}

/* ---- TCP --------------------------------------------------------------- */

static void
test_tcp_client(void)
{
	Observateur o;
	GtkTermLink *link = nouvelle_liaison(&o);
	TransportParams tp;
	guint16 port;
	int ecoute = ecoute_locale(&port), pair;
	gchar *base;

	params_tcp(&tp, TRANSPORT_TCP_CLIENT, port);
	base = g_strdup_printf("tcp://127.0.0.1:%u", port);
	g_assert_true(gt_link_open(link, &tp, NULL));
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_CONNECTING);
	g_assert_true(g_str_has_suffix(gt_link_get_status_text(link), " — connecting…"));
	pair = accept(ecoute, NULL, NULL);
	g_assert_true(attendre_etat(link, GT_LINK_OPEN, 2000));
	g_assert_cmpstr(gt_link_get_status_text(link), ==, base);
	close(pair);
	g_assert_true(attendre_etat(link, GT_LINK_LOST, 2000));
	g_assert_cmpint(o.open_failed, ==, 0);
	g_free(base);
	transport_params_clear(&tp);
	liberer(link, &o);
	close(ecoute);
}

static void
test_tcp_client_refus(void)
{
	Observateur o;
	GtkTermLink *link = nouvelle_liaison(&o);
	TransportParams tp;

	params_tcp(&tp, TRANSPORT_TCP_CLIENT, port_libre());
	if(gt_link_open(link, &tp, NULL))
		g_assert_true(attendre_etat(link, GT_LINK_CLOSED, 2000));
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_CLOSED);
	g_assert_cmpint(o.open_failed, ==, 1);
	g_assert_nonnull(strstr(o.dernier_message, "Cannot connect"));
	transport_params_clear(&tp);
	liberer(link, &o);
}

static void
test_fermeture_pendant_connexion(void)
{
	Observateur o;
	GtkTermLink *link = nouvelle_liaison(&o);
	TransportParams tp;
	guint16 port;
	int ecoute = ecoute_locale(&port);

	params_tcp(&tp, TRANSPORT_TCP_CLIENT, port);
	g_assert_true(gt_link_open(link, &tp, NULL));
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_CONNECTING);
	gt_link_close(link);
	iterer(200);
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_CLOSED);
	g_assert_cmpint(o.open_failed, ==, 0);
	transport_params_clear(&tp);
	liberer(link, &o);
	close(ecoute);
}

static void
test_tcp_serveur(void)
{
	Observateur o;
	GtkTermLink *link = nouvelle_liaison(&o);
	TransportParams tp;
	guint16 port = port_libre();
	gchar *attendu;
	guint8 buf[2];
	int client;

	params_tcp(&tp, TRANSPORT_TCP_SERVER, port);
	g_assert_true(gt_link_open(link, &tp, NULL));
	g_assert_cmpint(gt_link_get_state(link), ==, GT_LINK_LISTENING);
	attendu = g_strdup_printf("tcp://127.0.0.1:%u (server, no client)", port);
	g_assert_cmpstr(gt_link_get_status_text(link), ==, attendu);
	g_free(attendu);

	client = connecter(port);
	g_assert_true(attendre_etat(link, GT_LINK_OPEN, 2000));
	attendu = g_strdup_printf("tcp://127.0.0.1:%u (server)", port);
	g_assert_cmpstr(gt_link_get_status_text(link), ==, attendu);
	g_free(attendu);
	g_assert_cmpint(gt_link_send(link, (const guint8 *)"hi", 2, GT_SEND_LUA), ==, 2);
	g_assert_cmpuint(lire(client, buf, 2, 2000), ==, 2);
	g_assert_cmpmem(buf, 2, "hi", 2);

	close(client);
	g_assert_true(attendre_etat(link, GT_LINK_LISTENING, 2000));
	transport_params_clear(&tp);
	liberer(link, &o);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/link/ouverture-reussie", test_ouverture_reussie);
	g_test_add_func("/link/ouverture-echouee", test_ouverture_echouee);
	g_test_add_func("/link/reception-et-historique", test_reception_et_historique);
	g_test_add_func("/link/envoi-et-historique", test_envoi_et_historique);
	g_test_add_func("/link/effacer-historique", test_effacer_historique);
	g_test_add_func("/link/perte-sans-reconnexion", test_perte_sans_reconnexion);
	g_test_add_func("/link/perte-puis-reconnexion", test_perte_puis_reconnexion);
	g_test_add_func("/link/fermeture-annule-reconnexion", test_fermeture_annule_reconnexion);
	g_test_add_func("/link/bascule-auto-reconnect", test_bascule_auto_reconnect);
	g_test_add_func("/link/destruction-pendant-reconnexion", test_destruction_pendant_reconnexion);
	g_test_add_func("/link/udev-simule", test_udev_simule);
	g_test_add_func("/link/tcp-client", test_tcp_client);
	g_test_add_func("/link/tcp-client-refus", test_tcp_client_refus);
	g_test_add_func("/link/fermeture-pendant-connexion", test_fermeture_pendant_connexion);
	g_test_add_func("/link/tcp-serveur", test_tcp_serveur);
	return g_test_run();
}
```

Dans `tests/meson.build`, ajouter au dictionnaire `integration_tests` :

```meson
	'test_link' : {
		'sources' : files('../src/link.c'),
		'deps' : [gudev_deps, util_dep],
		'link_args' : [],
	},
```

- [ ] **Step 2 : constater l'échec**

Run : `ninja -C build 2>&1 | grep -m3 error` — Expected : `link.h: No such file or directory`.

- [ ] **Step 3 : écrire `link.h`**

```c
/* Liaison du port principal (sous-projet 2) : machine d'état de la connexion, reconnexion, moniteur udev, historique.
 * GObject observé par l'interface (propriétés notifiées, signaux) ; ne dépend ni de GTK ni de VTE. */
#ifndef LINK_H_
#define LINK_H_

#include <glib-object.h>

#include "history.h"
#include "transport.h"

G_BEGIN_DECLS

typedef enum
{
	GT_LINK_CLOSED,
	GT_LINK_CONNECTING,		/* client TCP : connexion demandée, pas encore établie */
	GT_LINK_OPEN,
	GT_LINK_LISTENING,		/* serveur TCP sans client */
	GT_LINK_LOST,
	GT_LINK_RECONNECTING
} GtLinkState;

GType gt_link_state_get_type(void);
#define GT_TYPE_LINK_STATE (gt_link_state_get_type())

#define GT_TYPE_LINK (gt_link_get_type())
G_DECLARE_FINAL_TYPE(GtkTermLink, gt_link, GT, LINK, GObject)

GtkTermLink *gt_link_new(void);
/* Liaison du port principal, utilisée par les consommateurs (macros, Lua, fichiers) ; non possédée. */
void         gt_link_set_default(GtkTermLink *link);
GtkTermLink *gt_link_get_default(void);

/* Ouverture demandée par l'utilisateur : ferme d'abord la connexion courante. Échec : open-failed, puis FALSE. */
gboolean     gt_link_open(GtkTermLink *link, const TransportParams *params, GError **error);
/* Fermeture demandée : annule toute reconnexion. */
void         gt_link_close(GtkTermLink *link);
/* Seuls les octets réellement écrits entrent dans l'historique (TX) et sont émis par « sent ». 0 si rien n'est ouvert. */
gssize       gt_link_send(GtkTermLink *link, const guint8 *data, gsize len, GtSendOrigin origin);
gint         gt_link_get_modem_lines(GtkTermLink *link);
void         gt_link_toggle_line(GtkTermLink *link, GtModemLine line);
void         gt_link_send_break(GtkTermLink *link);
gint         gt_link_get_fd(GtkTermLink *link);
GtLinkState  gt_link_get_state(GtkTermLink *link);
const gchar *gt_link_get_status_text(GtkTermLink *link);
GtHistory   *gt_link_get_history(GtkTermLink *link);
/* Vide l'historique et émet « history-cleared » (commande « Effacer l'écran », Échap). */
void         gt_link_clear_history(GtkTermLink *link);

/* Entrée de test : simule un événement udev (« add » / « remove », numéro de périphérique, chemin du nœud). */
void         gt_link_handle_device_event(GtkTermLink *link, const gchar *action, dev_t devnum, const gchar *path);

G_END_DECLS

#endif
```

- [ ] **Step 4 : écrire `link.c`**

```c
/***********************************************************************/
/* link.c                                                              */
/*                                                                     */
/*   Purpose                                                           */
/*      Liaison du port principal : machine d'état, reconnexion,       */
/*      moniteur udev, historique (sous-projet 2)                      */
/*                                                                     */
/***********************************************************************/

#include <config.h>
#include <glib/gi18n.h>
#include <gudev/gudev.h>
#include <string.h>

#include "device_match.h"
#include "link.h"

struct _GtkTermLink
{
	GObject parent_instance;

	GtLinkState state;
	gchar *status_text;
	gboolean auto_reconnect;
	guint reconnect_interval_ms;

	TransportParams params;		/* derniers paramètres demandés (reconnexion, texte d'état) */
	gboolean have_params;
	GtTransport *transport;
	gboolean user_open_pending;	/* client TCP : ouverture demandée, connexion pas encore établie */
	guint actual_baudrate;		/* dernier débit réel (texte d'état) */
	dev_t devnum;			/* périphérique série ouvert (correspondance udev) */
	guint reconnect_timer_id;
	GUdevClient *udev;
	GtHistory *history;
};

enum
{
	PROP_0,
	PROP_STATE,
	PROP_STATUS_TEXT,
	PROP_AUTO_RECONNECT,
	PROP_RECONNECT_INTERVAL_MS,
	N_PROPS
};

enum
{
	SIG_OPEN_FAILED,
	SIG_RECEIVED,
	SIG_SENT,
	SIG_HISTORY_CLEARED,
	N_SIGNALS
};

static GParamSpec *props[N_PROPS];
static guint signals[N_SIGNALS];
static GtkTermLink *default_link = NULL;

G_DEFINE_TYPE(GtkTermLink, gt_link, G_TYPE_OBJECT)

static void link_try_reconnect(GtkTermLink *link);

GType gt_link_state_get_type(void)
{
	static gsize type_id = 0;
	static const GEnumValue values[] =
	{
		{ GT_LINK_CLOSED, "GT_LINK_CLOSED", "closed" },
		{ GT_LINK_CONNECTING, "GT_LINK_CONNECTING", "connecting" },
		{ GT_LINK_OPEN, "GT_LINK_OPEN", "open" },
		{ GT_LINK_LISTENING, "GT_LINK_LISTENING", "listening" },
		{ GT_LINK_LOST, "GT_LINK_LOST", "lost" },
		{ GT_LINK_RECONNECTING, "GT_LINK_RECONNECTING", "reconnecting" },
		{ 0, NULL, NULL }
	};

	if(g_once_init_enter(&type_id))
		g_once_init_leave(&type_id, g_enum_register_static("GtLinkState", values));
	return type_id;
}

/* ---- Texte d'état ------------------------------------------------------ */

static gchar *link_describe(GtkTermLink *link)
{
	static const gchar parity_char[] = { 'N', 'O', 'E' };
	const TransportParams *p = &link->params;
	const gchar *host = (p->host != NULL && p->host[0] != '\0') ? p->host : "*";

	switch(p->type)
	{
	case TRANSPORT_SERIAL:
		return g_strdup_printf("%s  %u-%d-%c-%d", p->port, link->actual_baudrate != 0 ? link->actual_baudrate : p->baudrate, p->bits,
		                       parity_char[p->parity <= GT_PARITY_EVEN ? p->parity : 0], p->stopbits);
	case TRANSPORT_TCP_CLIENT:
		return g_strdup_printf("tcp://%s:%s", p->host, p->tcp_port);
	default:
		if(link->state == GT_LINK_LISTENING)
			return g_strdup_printf(_("tcp://%s:%s (server, no client)"), host, p->tcp_port);
		return g_strdup_printf(_("tcp://%s:%s (server)"), host, p->tcp_port);
	}
}

static void link_update_status(GtkTermLink *link)
{
	gchar *text, *base;

	if(link->state == GT_LINK_CLOSED || !link->have_params)
		text = g_strdup(_("No open port"));
	else
	{
		base = link_describe(link);
		switch(link->state)
		{
		case GT_LINK_CONNECTING:
			text = g_strdup_printf(_("%s — connecting…"), base);
			break;
		case GT_LINK_LOST:
			text = g_strdup_printf(_("%s — lost"), base);
			break;
		case GT_LINK_RECONNECTING:
			text = g_strdup_printf(_("%s — reconnecting…"), base);
			break;
		default:
			text = g_strdup(base);
		}
		g_free(base);
	}

	if(g_strcmp0(text, link->status_text) != 0)
	{
		g_free(link->status_text);
		link->status_text = text;
		g_object_notify_by_pspec(G_OBJECT(link), props[PROP_STATUS_TEXT]);
	}
	else
		g_free(text);
}

static void link_set_state(GtkTermLink *link, GtLinkState state)
{
	if(link->state != state)
	{
		link->state = state;
		g_object_notify_by_pspec(G_OBJECT(link), props[PROP_STATE]);
	}
	link_update_status(link);
}

/* ---- Minuterie de reconnexion et moniteur udev ------------------------- */

static void link_stop_reconnect_timer(GtkTermLink *link)
{
	if(link->reconnect_timer_id != 0)
	{
		g_source_remove(link->reconnect_timer_id);
		link->reconnect_timer_id = 0;
	}
}

static gboolean link_reconnect_tick(gpointer data)
{
	link_try_reconnect(data);
	return G_SOURCE_CONTINUE;
}

static void link_start_reconnect(GtkTermLink *link)
{
	link_set_state(link, GT_LINK_RECONNECTING);
	if(link->reconnect_timer_id == 0)
		link->reconnect_timer_id = g_timeout_add(link->reconnect_interval_ms, link_reconnect_tick, link);
}

static void link_device_event(GtkTermLink *link, const gchar *action, dev_t devnum, const gchar *file, const gchar *const *links);

static void link_udev_uevent(GUdevClient *client, const gchar *action, GUdevDevice *device, gpointer user_data)
{
	if(action == NULL || device == NULL || g_udev_device_get_device_file(device) == NULL)
		return;
	link_device_event(user_data, action, g_udev_device_get_device_number(device), g_udev_device_get_device_file(device),
	                  g_udev_device_get_device_file_symlinks(device));
}

/* Armé à chaque ouverture réussie d'un port série ; n'ouvre jamais le port de lui-même (M2, M4) */
static void link_arm_udev(GtkTermLink *link)
{
	const gchar *const subsystems[] = { "tty", NULL };

	if(link->udev != NULL)
		return;
	link->udev = g_udev_client_new(subsystems);
	g_signal_connect(link->udev, "uevent", G_CALLBACK(link_udev_uevent), link);
}

static void link_disarm_udev(GtkTermLink *link)
{
	if(link->udev == NULL)
		return;
	g_signal_handlers_disconnect_by_data(link->udev, link);
	g_clear_object(&link->udev);
}

/* ---- Transport --------------------------------------------------------- */

static void link_handle_lost(GtkTermLink *link, const gchar *detail);

static void link_on_data(const guint8 *data, gsize len, gpointer user_data)
{
	GtkTermLink *link = user_data;
	gint64 now = g_get_real_time();
	GBytes *bytes;

	gt_history_append(link->history, GT_RECORD_RX, GT_SEND_KEYBOARD, now, data, len);
	bytes = g_bytes_new(data, len);
	g_signal_emit(link, signals[SIG_RECEIVED], 0, bytes, now);
	g_bytes_unref(bytes);
}

static void link_on_event(TransportEvent ev, const gchar *detail, gpointer user_data)
{
	GtkTermLink *link = user_data;

	switch(ev)
	{
	case TRANSPORT_EVENT_CONNECTED:
		link->user_open_pending = FALSE;
		link_stop_reconnect_timer(link);
		link_set_state(link, GT_LINK_OPEN);
		break;
	case TRANSPORT_EVENT_CLIENT_CONNECTED:
		link_set_state(link, GT_LINK_OPEN);
		break;
	case TRANSPORT_EVENT_CLIENT_GONE:
		link_set_state(link, GT_LINK_LISTENING);
		break;
	case TRANSPORT_EVENT_LOST:
		link_handle_lost(link, detail);
		break;
	}
}

static gboolean link_open_transport(GtkTermLink *link, GError **error)
{
	static const TransportCallbacks callbacks = { link_on_data, link_on_event };
	GtTransport *t = gt_transport_open(&link->params, &callbacks, link, error);

	if(t == NULL)
		return FALSE;
	link->transport = t;
	if(link->params.type == TRANSPORT_SERIAL)
	{
		link->actual_baudrate = gt_transport_get_actual_baudrate(t);
		link->devnum = gt_transport_get_device_number(t);
	}
	return TRUE;
}

static void link_close_transport(GtkTermLink *link)
{
	gt_transport_close(link->transport);
	link->transport = NULL;
}

/* LOST du transport (signalé hors de tout appel au transport) ou udev « remove » */
static void link_handle_lost(GtkTermLink *link, const gchar *detail)
{
	link_close_transport(link);

	if(link->user_open_pending)
	{
		/* Client TCP : la connexion demandée par l'utilisateur a échoué */
		link->user_open_pending = FALSE;
		link_set_state(link, GT_LINK_CLOSED);
		g_signal_emit(link, signals[SIG_OPEN_FAILED], 0, detail != NULL ? detail : "");
		return;
	}
	if(link->state == GT_LINK_RECONNECTING)
		return;		/* tentative de reconnexion TCP échouée : la minuterie continue, en silence */

	link_set_state(link, GT_LINK_LOST);
	if(link->auto_reconnect)
		link_start_reconnect(link);
}

/* Essai silencieux : jamais de open-failed pendant une reconnexion */
static void link_try_reconnect(GtkTermLink *link)
{
	if(link->state != GT_LINK_RECONNECTING || link->transport != NULL)
		return;
	if(!link_open_transport(link, NULL))
		return;
	if(link->params.type == TRANSPORT_TCP_CLIENT)
		return;		/* OPEN à l'événement CONNECTED */
	link_stop_reconnect_timer(link);
	link_set_state(link, link->params.type == TRANSPORT_TCP_SERVER ? GT_LINK_LISTENING : GT_LINK_OPEN);
}

static void link_device_event(GtkTermLink *link, const gchar *action, dev_t devnum, const gchar *file, const gchar *const *links)
{
	if(!link->have_params || link->params.type != TRANSPORT_SERIAL)
		return;

	if(g_strcmp0(action, "remove") == 0 && link->state == GT_LINK_OPEN)
	{
		if(device_match(link->devnum, link->params.port, devnum, file, links))
			link_handle_lost(link, _("device removed"));
	}
	else if(g_strcmp0(action, "add") == 0 && link->state == GT_LINK_RECONNECTING)
	{
		/* Au rebranchement, le numéro peut changer : seul le chemin configuré compte */
		if(device_match(0, link->params.port, devnum, file, links))
			link_try_reconnect(link);
	}
}

void gt_link_handle_device_event(GtkTermLink *link, const gchar *action, dev_t devnum, const gchar *path)
{
	g_return_if_fail(GT_IS_LINK(link));
	link_device_event(link, action, devnum, path, NULL);
}

static void link_shutdown(GtkTermLink *link)
{
	link_stop_reconnect_timer(link);
	link_close_transport(link);
	link->user_open_pending = FALSE;
	link_disarm_udev(link);
}

/* ---- Interface publique ------------------------------------------------ */

GtkTermLink *gt_link_new(void)
{
	return g_object_new(GT_TYPE_LINK, NULL);
}

void gt_link_set_default(GtkTermLink *link)
{
	default_link = link;
}

GtkTermLink *gt_link_get_default(void)
{
	return default_link;
}

gboolean gt_link_open(GtkTermLink *link, const TransportParams *params, GError **error)
{
	GError *local = NULL;

	g_return_val_if_fail(GT_IS_LINK(link), FALSE);
	g_return_val_if_fail(params != NULL, FALSE);

	link_shutdown(link);
	transport_params_clear(&link->params);
	transport_params_copy(&link->params, params);
	link->have_params = TRUE;
	link->actual_baudrate = 0;
	link->devnum = 0;

	if(!link_open_transport(link, &local))
	{
		link_set_state(link, GT_LINK_CLOSED);
		g_signal_emit(link, signals[SIG_OPEN_FAILED], 0, local->message);
		g_propagate_error(error, local);
		return FALSE;
	}

	switch(params->type)
	{
	case TRANSPORT_SERIAL:
		link_arm_udev(link);
		link_set_state(link, GT_LINK_OPEN);
		break;
	case TRANSPORT_TCP_SERVER:
		link_set_state(link, GT_LINK_LISTENING);
		break;
	default:
		link->user_open_pending = TRUE;
		link_set_state(link, GT_LINK_CONNECTING);
	}
	return TRUE;
}

void gt_link_close(GtkTermLink *link)
{
	g_return_if_fail(GT_IS_LINK(link));
	link_shutdown(link);
	link_set_state(link, GT_LINK_CLOSED);
}

gssize gt_link_send(GtkTermLink *link, const guint8 *data, gsize len, GtSendOrigin origin)
{
	gssize written;
	GBytes *bytes;

	g_return_val_if_fail(GT_IS_LINK(link), -1);
	if(link->transport == NULL || len == 0)
		return 0;

	written = gt_transport_send(link->transport, data, len);
	if(written <= 0)
		return written;

	gt_history_append(link->history, GT_RECORD_TX, origin, g_get_real_time(), data, (gsize)written);
	bytes = g_bytes_new(data, (gsize)written);
	g_signal_emit(link, signals[SIG_SENT], 0, bytes, (gint)origin);
	g_bytes_unref(bytes);
	return written;
}

gint gt_link_get_modem_lines(GtkTermLink *link)
{
	g_return_val_if_fail(GT_IS_LINK(link), -1);
	return gt_transport_get_modem_lines(link->transport);
}

void gt_link_toggle_line(GtkTermLink *link, GtModemLine line)
{
	g_return_if_fail(GT_IS_LINK(link));
	gt_transport_toggle_line(link->transport, line);
}

void gt_link_send_break(GtkTermLink *link)
{
	g_return_if_fail(GT_IS_LINK(link));
	gt_transport_send_break(link->transport);
}

gint gt_link_get_fd(GtkTermLink *link)
{
	g_return_val_if_fail(GT_IS_LINK(link), -1);
	return gt_transport_get_fd(link->transport);
}

GtLinkState gt_link_get_state(GtkTermLink *link)
{
	g_return_val_if_fail(GT_IS_LINK(link), GT_LINK_CLOSED);
	return link->state;
}

const gchar *gt_link_get_status_text(GtkTermLink *link)
{
	g_return_val_if_fail(GT_IS_LINK(link), NULL);
	return link->status_text;
}

GtHistory *gt_link_get_history(GtkTermLink *link)
{
	g_return_val_if_fail(GT_IS_LINK(link), NULL);
	return link->history;
}

void gt_link_clear_history(GtkTermLink *link)
{
	g_return_if_fail(GT_IS_LINK(link));
	gt_history_clear(link->history);
	g_signal_emit(link, signals[SIG_HISTORY_CLEARED], 0);
}

static void link_set_auto_reconnect(GtkTermLink *link, gboolean value)
{
	if(link->auto_reconnect == value)
		return;
	link->auto_reconnect = value;
	g_object_notify_by_pspec(G_OBJECT(link), props[PROP_AUTO_RECONNECT]);

	if(value && link->state == GT_LINK_LOST)
		link_start_reconnect(link);
	else if(!value && link->state == GT_LINK_RECONNECTING)
	{
		link_stop_reconnect_timer(link);
		link_close_transport(link);	/* tentative TCP en cours */
		link_set_state(link, GT_LINK_LOST);
	}
}

/* ---- GObject ----------------------------------------------------------- */

static void gt_link_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GtkTermLink *link = GT_LINK(object);

	switch(prop_id)
	{
	case PROP_STATE:
		g_value_set_enum(value, link->state);
		break;
	case PROP_STATUS_TEXT:
		g_value_set_string(value, link->status_text);
		break;
	case PROP_AUTO_RECONNECT:
		g_value_set_boolean(value, link->auto_reconnect);
		break;
	case PROP_RECONNECT_INTERVAL_MS:
		g_value_set_uint(value, link->reconnect_interval_ms);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}

static void gt_link_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GtkTermLink *link = GT_LINK(object);

	switch(prop_id)
	{
	case PROP_AUTO_RECONNECT:
		link_set_auto_reconnect(link, g_value_get_boolean(value));
		break;
	case PROP_RECONNECT_INTERVAL_MS:
		if(link->reconnect_interval_ms != g_value_get_uint(value))
		{
			link->reconnect_interval_ms = g_value_get_uint(value);
			g_object_notify_by_pspec(object, pspec);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}

static void gt_link_dispose(GObject *object)
{
	GtkTermLink *link = GT_LINK(object);

	link_shutdown(link);
	if(default_link == link)
		default_link = NULL;
	G_OBJECT_CLASS(gt_link_parent_class)->dispose(object);
}

static void gt_link_finalize(GObject *object)
{
	GtkTermLink *link = GT_LINK(object);

	g_free(link->status_text);
	transport_params_clear(&link->params);
	gt_history_free(link->history);
	G_OBJECT_CLASS(gt_link_parent_class)->finalize(object);
}

static void gt_link_class_init(GtkTermLinkClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->get_property = gt_link_get_property;
	object_class->set_property = gt_link_set_property;
	object_class->dispose = gt_link_dispose;
	object_class->finalize = gt_link_finalize;

	props[PROP_STATE] = g_param_spec_enum("state", "State", "Connection state", GT_TYPE_LINK_STATE, GT_LINK_CLOSED,
	                                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	props[PROP_STATUS_TEXT] = g_param_spec_string("status-text", "Status text", "Status bar and window title text", NULL,
	                                              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	props[PROP_AUTO_RECONNECT] = g_param_spec_boolean("auto-reconnect", "Auto reconnect", "Reconnect silently after a link loss", FALSE,
	                                                  G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
	props[PROP_RECONNECT_INTERVAL_MS] = g_param_spec_uint("reconnect-interval-ms", "Reconnect interval", "Delay between reconnection attempts (ms)",
	                                                      10, 600000, 2000, G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
	g_object_class_install_properties(object_class, N_PROPS, props);

	signals[SIG_OPEN_FAILED] = g_signal_new("open-failed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
	                                        G_TYPE_NONE, 1, G_TYPE_STRING);
	signals[SIG_RECEIVED] = g_signal_new("received", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
	                                     G_TYPE_NONE, 2, G_TYPE_BYTES, G_TYPE_INT64);
	signals[SIG_SENT] = g_signal_new("sent", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
	                                 G_TYPE_NONE, 2, G_TYPE_BYTES, G_TYPE_INT);
	signals[SIG_HISTORY_CLEARED] = g_signal_new("history-cleared", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
	                                            G_TYPE_NONE, 0);
}

static void gt_link_init(GtkTermLink *link)
{
	link->state = GT_LINK_CLOSED;
	link->status_text = g_strdup(_("No open port"));
	link->reconnect_interval_ms = 2000;
	link->history = gt_history_new(GT_HISTORY_DEFAULT_CAPACITY);
}
```

- [ ] **Step 5 : constater la réussite**

Run : `ninja -C build && meson test -C build test_link --print-errorlogs`
Expected : `test_link OK` (15 cas).

- [ ] **Step 6 : suite complète, vérification des inclusions et commit**

Run : `meson test -C build --print-errorlogs` — Expected : aucun échec.
Run : `grep -nE '#include *[<"](gtk/|vte/)|show_message|Set_status_message|Set_window_title' src/link.c` — Expected : aucune sortie.

```bash
git add src/link.c src/link.h tests/test_link.c tests/meson.build
git commit -m "liaison : GtkTermLink, machine d'état unique, reconnexion silencieuse, moniteur udev par numéro de périphérique

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7 : l'application passe par la liaison pour le cycle de vie du port

**Files :**
- Delete : `src/serial.c`, `src/serial.h`, `src/device_monitor.c`, `src/device_monitor.h`
- Modify : `src/gtkterm.c`, `src/interface.c`, `src/interface.h`, `src/term_config.c`, `src/term_config.h`, `src/files.c`, `src/files.h`, `src/terminal_display.c`, `src/buffer.c`, `src/logging.c`, `src/meson.build`, `tests/stubs/stub_ui.c`

**Interfaces :**
- Consumes : `GtkTermLink` (tâche 6), `config_to_transport_params` (tâche 5).
- Produces :
  - `void interface_connect_link(GtkTermLink *link);` (`interface.h`, qui inclut désormais `link.h`) ;
  - `void files_on_received(GtkTermLink *link, GBytes *data, gint64 wall_time_us, gpointer user_data);` (`files.h`, qui inclut `link.h`) ;
  - `configure_echo`, `configure_crlfauto`, `configure_autoreconnect_enable`, `configure_esc_clear_screen` déclarés dans `term_config.h`, définis dans `term_config.c`.
- Provisoire jusqu'à la tâche 8 : les octets reçus passent encore par `put_chars()` (`buffer.c`) via un gestionnaire de `received` ; `send_serial()` existe encore et envoie par `gt_link_send(…, GT_SEND_KEYBOARD)`.

- [ ] **Step 1 : supprimer la façade et le moniteur**

```bash
git rm src/serial.c src/serial.h src/device_monitor.c src/device_monitor.h
```

`src/meson.build`, liste `sources` : retirer `'serial.c',`, `'device_monitor.c',`, `'device_monitor.h',` ; ajouter `'link.c',` et `'link.h',`. Liste `core_sources` : retirer `'serial.h',`.

Les quelques définitions encore utiles de l'ancien `serial.h` vont chez leur seul utilisateur :
- `src/files.c` : ajouter après les inclusions `#define BUFFER_EMISSION 4096` et `#define LINE_FEED 0x0A` ;
- `src/interface.c` : ajouter après les inclusions `#define POLL_DELAY 100	/* ms, lecture des lignes modem */`.

Retirer `#include "serial.h"` de `gtkterm.c`, `interface.c`, `term_config.c`, `files.c`, `logging.c`, `terminal_display.c`, `buffer.c` ; retirer `#include "device_monitor.h"` de `gtkterm.c` et `interface.c`. Vérifier : `grep -rn 'serial\.h\|device_monitor' src tests` → aucune sortie.

- [ ] **Step 2 : `configure_*` dans `term_config.c`, écho dans `ConfigFlags`**

Dans `src/term_config.h`, après la déclaration de `config_to_transport_params`, ajouter :

```c
void configure_echo(gboolean echo);
void configure_crlfauto(gboolean crlfauto);
void configure_autoreconnect_enable(gboolean autoreconnect);
void configure_esc_clear_screen(gboolean esc_clear_screen);
```

Dans `src/term_config.c`, après `config_to_transport_params`, ajouter les quatre définitions (reprises de l'ancien `serial.c`) :

```c
void configure_echo(gboolean echo)
{
	config.echo = echo;
}

void configure_crlfauto(gboolean crlfauto)
{
	config.crlfauto = crlfauto;
}

void configure_autoreconnect_enable(gboolean autoreconnect)
{
	config.autoreconnect_enabled = autoreconnect;
}

void configure_esc_clear_screen(gboolean esc_clear_screen)
{
	config.esc_clear_screen = esc_clear_screen;
}
```

L'ancien transport rétablissait l'écho de la configuration à chaque ouverture (`Set_local_echo(config.echo)`) ; la liaison ne touche plus l'interface, donc `ConfigFlags` le fait :

```c
void ConfigFlags(void)
{
	Set_local_echo(config.echo);
	Set_crlfauto(config.crlfauto);
	Set_autoreconnect_enabled(config.autoreconnect_enabled);
	Set_esc_clear_screen(config.esc_clear_screen);
	Set_timestamp(config.timestamp);
}
```

Dans `Lis_Config` (fin de fonction, après la lecture des champs TCP), remplacer :

```c
	Config_port();
	ConfigFlags();

	message = get_port_string();
	Set_status_message(message);
	Set_window_title(message);
	g_free(message);

	return FALSE;
```

par :

```c
	interface_open_port();
	ConfigFlags();

	return FALSE;
```

Dans `load_config`, remplacer :

```c
			Load_configuration_from_file(txt);
			Verify_configuration();
			Config_port();
			ConfigFlags();
			add_shortcuts();

			message = get_port_string();
			Set_status_message(message);
			Set_window_title(message);
			g_free(message);
```

par :

```c
			Load_configuration_from_file(txt);
			Verify_configuration();
			interface_open_port();
			ConfigFlags();
			add_shortcuts();
```

et retirer `message` de la déclaration `gchar *txt, *message;` de `load_config` (→ `gchar *txt;`). Dans `Lis_Config`, garder la variable `message` si elle sert encore plus haut (elle sert à la lecture du débit).

- [ ] **Step 3 : interface abonnée à la liaison**

`src/interface.h` : ajouter `#include "link.h"` avant `#define MSG_WRN 0`, et la déclaration `void interface_connect_link(GtkTermLink *link);` après `void interface_open_port(void);`.

`src/interface.c` : ajouter `#include "link.h"` à côté de `#include "terminal_display.h"`, puis remplacer `interface_open_port` et `interface_close_port` par :

```c
static void on_link_status(GObject *object, GParamSpec *pspec, gpointer user_data)
{
	const gchar *text = gt_link_get_status_text(GT_LINK(object));

	Set_status_message((gchar *)text);
	Set_window_title((gchar *)text);
}

/* Erreur d'une ouverture demandée par l'utilisateur (jamais pendant une reconnexion) */
static void on_link_open_failed(GtkTermLink *link, const gchar *message, gpointer user_data)
{
	show_message((gchar *)message, MSG_ERR);
}

/* Provisoire (tâche 8 la remplace par le rendu depuis l'historique) : les octets reçus passent encore par buffer.c */
static void on_link_received_legacy(GtkTermLink *link, GBytes *data, gint64 wall_time_us, gpointer user_data)
{
	gsize len;
	const gchar *p = g_bytes_get_data(data, &len);

	put_chars(p, (unsigned int)len, config.crlfauto);
}

void interface_connect_link(GtkTermLink *link)
{
	g_signal_connect(link, "notify::status-text", G_CALLBACK(on_link_status), NULL);
	g_signal_connect(link, "open-failed", G_CALLBACK(on_link_open_failed), NULL);
	g_signal_connect(link, "received", G_CALLBACK(on_link_received_legacy), NULL);
	g_signal_connect(link, "received", G_CALLBACK(files_on_received), NULL);
	on_link_status(G_OBJECT(link), NULL, NULL);
}

void interface_open_port(void)
{
	TransportParams params;

	config_to_transport_params(&config, &params);
	gt_link_open(gt_link_get_default(), &params, NULL);	/* un échec émet open-failed : une boîte de dialogue */
	transport_params_clear(&params);
}

void interface_close_port(void)
{
	gt_link_close(gt_link_get_default());
}
```

Lignes modem, break, reconnexion :

```c
void signals_send_break_callback(GtkAction *action, gpointer data)
{
	gt_link_send_break(gt_link_get_default());
	Put_temp_message(_("Break signal sent!"), 800);
}

void signals_toggle_DTR_callback(GtkAction *action, gpointer data)
{
	gt_link_toggle_line(gt_link_get_default(), GT_MODEM_DTR);
}

void signals_toggle_RTS_callback(GtkAction *action, gpointer data)
{
	gt_link_toggle_line(gt_link_get_default(), GT_MODEM_RTS);
}
```

Dans `control_signals_read`, remplacer `state = lis_sig();` par `state = gt_link_get_modem_lines(gt_link_get_default());` (une erreur de périphérique y est détectée par le transport et devient `LOST`).

Dans `Set_autoreconnect_enabled` et `Autoreconnect_toggled_callback`, ajouter après la mise à jour de `autoreconnect_on` :

```c
	g_object_set(gt_link_get_default(), "auto-reconnect", autoreconnect_on, NULL);
```

- [ ] **Step 4 : démarrage avec une seule ouverture (M4)**

Dans `src/gtkterm.c`, ajouter `#include "link.h"` et remplacer le corps de `main` à partir de `gtk_init(&argc, &argv);` par :

```c
	gtk_init(&argc, &argv);

	link = gt_link_new();
	gt_link_set_default(link);

	create_buffer();

	create_main_window();
	interface_connect_link(link);

	if(read_command_line(argc, argv) < 0)
	{
		delete_buffer();
		g_object_unref(link);
		exit(1);
	}

	ConfigFlags();
	script_panel_refresh_colors();

	add_shortcuts();

	set_view(ASCII_VIEW);

	/* Une seule ouverture au démarrage, après la configuration et la ligne de commande (M4) ; échec : une boîte de dialogue */
	interface_open_port();

	user_signals_catch();

	g_idle_add(startup_update_check, NULL);

	gtk_main();

	gt_link_close(link);
	g_object_unref(link);
	delete_buffer();

	return 0;
```

et remplacer la déclaration locale `gchar *message;` par `GtkTermLink *link;`.

- [ ] **Step 5 : envoi de fichier sur la liaison, attente du caractère par la réception**

`src/files.h` : ajouter `#include "link.h"` après `#define FICHIER_H_` ; supprimer les lignes `void add_input(void);` et `extern gboolean waiting_for_char;` ; ajouter :

```c
/* Attente du caractère de fin de ligne pendant un envoi de fichier : comparé aux octets reçus. */
void files_on_received(GtkTermLink *link, GBytes *data, gint64 wall_time_us, gpointer user_data);
```

`src/files.c` : rendre l'attente locale (`gboolean waiting_for_char = FALSE;` → `static gboolean waiting_for_char = FALSE;`), ajouter `#include "link.h"`, puis remplacer `add_input` par :

```c
void add_input(void)
{
	GIOChannel *channel;
	gint fd;

	if(input_running == FALSE)
	{
		fd = gt_link_get_fd(gt_link_get_default());
		if(fd < 0)
		{
			/* Plus de port ouvert (perte de liaison) : le transfert s'arrête au lieu d'attendre indéfiniment */
			close_all();
			show_message(_("Error sending file\n"), MSG_ERR);
			return;
		}
		input_running = TRUE;
		channel = g_io_channel_unix_new(fd);
		callback_handler = g_io_add_watch_full(channel,
		                                       10,
		                                       G_IO_OUT,
		                                       ecriture,
		                                       NULL, NULL);
		/* La source garde sa propre référence sur le canal (M35) ; g_io_channel_unix_new ne ferme pas le descripteur à la libération */
		g_io_channel_unref(channel);
	}
}
```

et ajouter :

```c
/* La comparaison avec config.car a quitté le transport (sous-projet 2) : faite ici, sur les octets reçus par la liaison */
void files_on_received(GtkTermLink *link, GBytes *data, gint64 wall_time_us, gpointer user_data)
{
	gsize len;
	const guint8 *p = g_bytes_get_data(data, &len);

	if(!waiting_for_char || config.car == -1)
		return;
	for(gsize i = 0; i < len; i++)
	{
		if(p[i] == (guint8)config.car)
		{
			waiting_for_char = FALSE;
			add_input();
			return;
		}
	}
}
```

`close_all()` est appelé par `add_input()` : s'assurer qu'il est déclaré avant (`gint close_all(void);` figure déjà dans les prototypes locaux en tête de `files.c`). `close_all()` détruit `Window` : la garde `fd < 0` n'est atteinte que pendant un transfert, quand la fenêtre de progression existe.

`tests/stubs/stub_ui.c` : supprimer `gboolean waiting_for_char = FALSE;` et la fonction `add_input` (plus aucun appelant).

- [ ] **Step 6 : envoi du clavier par la liaison (provisoire)**

Dans `src/terminal_display.c`, ajouter `#include "link.h"` et remplacer dans `send_serial` la ligne `bytes_written = Send_chars (string, len);` par :

```c
  bytes_written = gt_link_send (gt_link_get_default (), (const guint8 *) string, len, GT_SEND_KEYBOARD);
```

(Le reste de `send_serial`, dont l'ancien rendu de l'écho, disparaît à la tâche 8.)

- [ ] **Step 7 : construction et suite**

Run : `ninja -C build 2>&1 | grep -E 'error|implicit declaration' ; meson test -C build --print-errorlogs`
Expected : aucune erreur de compilation ; aucun échec de test.

- [ ] **Step 8 : essai de fumée sur l'affichage isolé**

L'application doit s'ouvrir sur une pty, afficher le chemin complet dans la barre d'état, recevoir, et passer en « — lost » quand le maître pty est fermé. Écrire `$SCRATCH/fumee_t7.py` (`$SCRATCH` = répertoire bloc-notes de la session) :

```python
import sys, time
sys.path.insert(0, '/home/METRO_gabriel/git/gtkterm/.superpowers/banc-gui')
from common import *
e = Env('fumee_t7')
e.write_rc(e.profile())
e.start(); time.sleep(2)
sb = lambda: name(find1(e.app, 'status bar'))
rep('barre :', sb())
assert sb() == '%s  115200-8-N-1' % e.pty, sb()
e.write_pty(b'bonjour\r\n'); time.sleep(0.8)
assert 'bonjour' in get_text(find1(e.app, 'terminal'))
os.close(e.master); os.close(e.slave); e.master = e.slave = -1
time.sleep(1.5)
rep('barre apres perte :', sb())
assert sb().endswith(' — lost'), sb()
assert not [w for w in windows(e.app) if role(w) == 'alert'], 'boite de dialogue inattendue'
finish(e)
print('FUMEE OK')
```

Run : `cp $SCRATCH/fumee_t7.py .superpowers/banc-gui/ && EM_TIMEOUT=90 sh .superpowers/banc-gui/run.sh .superpowers/banc-gui/fumee_t7.py 2>&1 | tail -5 ; rm -rf .superpowers/banc-gui/fumee_t7.py .superpowers/banc-gui/runs/fumee_t7`
Expected : `FUMEE OK` en fin de sortie, aucune `AssertionError`.

- [ ] **Step 9 : commit**

```bash
git add -A src tests
git commit -m "interface : cycle de vie du port par GtkTermLink, une seule ouverture au démarrage

Suppression de serial.c et de device_monitor.c : ouverture, fermeture, lignes modem, break, reconnexion et texte d'état
passent par la liaison ; les erreurs d'ouverture demandée gardent leur boîte de dialogue, la perte de liaison n'en
ouvre plus (M1-M6). L'attente du caractère pendant l'envoi de fichier compare les octets reçus par la liaison.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8 : affichage depuis l'historique, un seul chemin d'envoi, consommateurs abonnés

**Files :**
- Delete : `src/buffer.c`, `src/buffer.h`, `tests/stubs/stub_display.c`
- Create : `tests/stubs/stub_link.c`
- Rewrite : `src/terminal_display.c`, `src/terminal_display.h`
- Modify : `src/interface.c`, `src/interface.h`, `src/gtkterm.c`, `src/logging.c`, `src/logging.h`, `src/files.c`, `src/macros.c`, `src/script_engine.c`, `src/meson.build`, `tests/meson.build`, `tests/stubs/stubs.h`, `tests/stubs/stub_journal.c`, `tests/test_macros_send.c`, `tests/test_script_engine.c`

**Interfaces :**
- Consumes : `GtViewRender` (tâche 4), `GtRxTransform` (tâche 2), `GtkTermLink` (tâche 6), `interface_connect_link` (tâche 7).
- Produces :
  ```c
  /* terminal_display.h */
  void terminal_display_redraw(void);
  void terminal_display_set_mode(GtViewMode mode);
  void set_hex_bytes_per_line(gint bpl);
  void set_hex_show_index(gboolean show);
  void clear_display(void);
  void terminal_display_on_received(GtkTermLink *link, GBytes *data, gint64 wall_time_us, gpointer user_data);
  void terminal_display_on_sent(GtkTermLink *link, GBytes *data, gint origin, gpointer user_data);
  void terminal_display_on_history_cleared(GtkTermLink *link, gpointer user_data);
  void Got_Input(VteTerminal *widget, gchar *text, guint length, gpointer ptr);
  /* logging.h */
  void logging_on_received(GtkTermLink *link, GBytes *data, gint64 wall_time_us, gpointer user_data);
  /* tests/stubs/stubs.h */
  GtSendOrigin stub_last_origin(void);
  ```
  Supprimés : `send_serial`, `put_text`, `put_hexadecimal`, `Envoie_car`, `initialize_hexadecimal_display`, `virt_col_pos`, `log_chars` (devient statique), `logging_set_suspended`, tout `buffer.h`.

- [ ] **Step 1 : bouchon de la liaison et tests adaptés (échec attendu)**

`tests/stubs/stub_link.c` :

```c
/* Bouchon de la liaison (link.c) pour les tests d'intégration : enregistre les envois, simule la réception. */

#include <glib-object.h>

#include "link.h"
#include "stubs.h"

struct _GtkTermLink
{
	GObject parent_instance;
};

G_DEFINE_TYPE(GtkTermLink, gt_link, G_TYPE_OBJECT)

static guint signal_received;
static GtkTermLink *lien_defaut = NULL;
static GMutex octets_mutex;
static GByteArray *envoyes = NULL;
static GThread *thread_envoi = NULL;
static GtSendOrigin derniere_origine = GT_SEND_KEYBOARD;

static void
gt_link_class_init(GtkTermLinkClass *klass)
{
	signal_received = g_signal_new("received", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
	                               G_TYPE_NONE, 2, G_TYPE_BYTES, G_TYPE_INT64);
}

static void
gt_link_init(GtkTermLink *link)
{
}

GtkTermLink *
gt_link_get_default(void)
{
	if(lien_defaut == NULL)
		lien_defaut = g_object_new(GT_TYPE_LINK, NULL);
	return lien_defaut;
}

gssize
gt_link_send(GtkTermLink *link, const guint8 *data, gsize len, GtSendOrigin origin)
{
	g_mutex_lock(&octets_mutex);
	if(envoyes == NULL)
		envoyes = g_byte_array_new();
	g_byte_array_append(envoyes, data, (guint)len);
	thread_envoi = g_thread_self();
	derniere_origine = origin;
	g_mutex_unlock(&octets_mutex);
	stub_record("gt_link_send", NULL);
	return (gssize)len;
}

void
stub_link_reset(void)
{
	g_mutex_lock(&octets_mutex);
	if(envoyes != NULL)
		g_byte_array_set_size(envoyes, 0);
	thread_envoi = NULL;
	derniere_origine = GT_SEND_KEYBOARD;
	g_mutex_unlock(&octets_mutex);
}

GByteArray *
stub_sent(void)
{
	if(envoyes == NULL)
		envoyes = g_byte_array_new();
	return envoyes;
}

GThread *
stub_send_thread(void)
{
	return thread_envoi;
}

GtSendOrigin
stub_last_origin(void)
{
	return derniere_origine;
}

/* Émet « received » sur la liaison par défaut, comme la vraie liaison à la réception */
void
stub_inject_rx(const gchar *data, guint len)
{
	GBytes *octets = g_bytes_new(data, len);

	g_signal_emit(gt_link_get_default(), signal_received, 0, octets, g_get_real_time());
	g_bytes_unref(octets);
}
```

`tests/stubs/stubs.h` : remplacer le bloc

```c
GByteArray  *stub_sent(void);
GThread     *stub_send_thread(void);
GByteArray  *stub_received(void);
void         stub_inject_rx(const gchar *data, guint len);
```

par

```c
/* Liaison (stub_link.c) */
GByteArray  *stub_sent(void);
GThread     *stub_send_thread(void);
GtSendOrigin stub_last_origin(void);
void         stub_inject_rx(const gchar *data, guint len);
```

et ajouter `#include "history.h"` après `#include <glib.h>`.

`tests/stubs/stub_journal.c` : remplacer `void stub_display_reset(void);` par `void stub_link_reset(void);` et l'appel `stub_display_reset();` par `stub_link_reset();`.

`git rm tests/stubs/stub_display.c`.

`tests/meson.build` : remplacer `stub_display = files('stubs/stub_display.c')` par `stub_link = files('stubs/stub_link.c')` ; entrée `'test_macros_send'` : `'sources' : files('../src/macros.c') + stub_journal + stub_ui + stub_link,` ; entrée `'test_script_engine'` : `'sources' : files('../src/script_engine.c', 'stubs/stub_macros.c') + stub_journal + stub_link,`.

`tests/test_macros_send.c`, dans `test_raccourci_avec_arguments_enregistres`, après `g_assert_cmpstr(s, ==, "V42\r");` ajouter :

```c
	g_assert_cmpint(stub_last_origin(), ==, GT_SEND_MACRO);
```

`tests/test_script_engine.c` : dans le commentaire d'en-tête, remplacer « bouchons pour send_serial, la fonction d'écoute du port principal et get_shortcuts » par « bouchons pour la liaison (envois, signal « received ») et get_shortcuts » ; dans `test_envois_sur_le_thread_principal_et_dans_l_ordre`, après `g_assert_true(stub_send_thread() == thread_principal);` ajouter :

```c
	g_assert_cmpint(stub_last_origin(), ==, GT_SEND_LUA);
```

Run : `ninja -C build 2>&1 | grep -m5 -E 'error|undefined reference'`
Expected : références indéfinies ou erreurs (`send_serial`, `set_tap_func`) dans `test_macros_send` / `test_script_engine` : `macros.c` et `script_engine.c` n'envoient pas encore par la liaison.

- [ ] **Step 2 : macros et Lua envoient par la liaison ; Lua écoute « received »**

`src/macros.c` : ajouter `#include "link.h"` ; dans `send_macro_with_args`, remplacer

```c
  if (buffer->len > 0)
    send_serial ((gchar *) buffer->data, buffer->len);
```

par

```c
  if (buffer->len > 0)
    gt_link_send (gt_link_get_default (), buffer->data, buffer->len, GT_SEND_MACRO);
```

`src/script_engine.c` :
- remplacer `#include "buffer.h"` par `#include "link.h"` ; supprimer le commentaire et la ligne `extern gint send_serial(gchar *string, gint len);` ;
- dans `main_send_cb` : `d->written = d->len > 0 ? (gint)gt_link_send(gt_link_get_default(), (const guint8 *)d->data, (gsize)d->len, GT_SEND_LUA) : 0;`
- dans `main_send_macro_cb` : `d->written = buf->len > 0 ? (gint)gt_link_send(gt_link_get_default(), buf->data, buf->len, GT_SEND_MACRO) : 0;`
- remplacer `lua_main_port_tap` par :

```c
/* Octets reçus sur le port principal (signal « received » de la liaison, thread principal) */
static void lua_main_port_received(GtkTermLink *link, GBytes *bytes, gint64 wall_time_us, gpointer user_data)
{
    lua_port_t *p = &ports[0];
    gsize size;
    const gchar *data = g_bytes_get_data(bytes, &size);

    g_mutex_lock(&p->mutex);
    if (p->open && p->rx_buf->len + size <= RX_BUF_MAX)
        g_string_append_len(p->rx_buf, data, (gssize)size);
    g_mutex_unlock(&p->mutex);
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
```

  Ces fonctions doivent être définies après `run_on_main_sync` et `lua_port_t ports[]` (là où se trouvait `lua_main_port_tap`).
- dans `lua_thread_func`, remplacer

```c
    /* Tap the main port */
    set_tap_func(lua_main_port_tap);
    ports[0].open = TRUE;
```

par

```c
    /* Écoute du port principal */
    g_mutex_lock(&ports[0].mutex);
    ports[0].open = TRUE;
    g_mutex_unlock(&ports[0].mutex);
    run_on_main_sync(main_port_connect_cb, NULL);
```

  et

```c
    /* Cleanup main port tap */
    unset_tap_func();
    ports[0].open = FALSE;
    g_mutex_lock(&ports[0].mutex);
    g_string_truncate(ports[0].rx_buf, 0);
    g_mutex_unlock(&ports[0].mutex);
```

par

```c
    /* Fin de l'écoute du port principal */
    run_on_main_sync(main_port_disconnect_cb, NULL);
    g_mutex_lock(&ports[0].mutex);
    ports[0].open = FALSE;
    g_string_truncate(ports[0].rx_buf, 0);
    g_mutex_unlock(&ports[0].mutex);
```

Run : `ninja -C build 2>&1 | grep -m5 -E 'error|undefined' ; meson test -C build test_macros_send test_script_engine --print-errorlogs`
Expected : les deux tests passent (l'exécutable `gtkterm` peut encore échouer à se lier tant que `send_serial` est utilisé ailleurs : c'est l'étape suivante).

- [ ] **Step 3 : réécrire `terminal_display`**

`src/terminal_display.h` :

```c
/***********************************************************************/
/* terminal_display.h                                                  */
/* ------------------                                                  */
/*           GTKTerm Software                                          */
/*                                                                     */
/*   Purpose                                                           */
/*      Alimentation du terminal VTE : rendu des vues depuis la        */
/*      liaison et son historique (sous-projet 2)                      */
/*                                                                     */
/***********************************************************************/

#ifndef TERMINAL_DISPLAY_H_
#define TERMINAL_DISPLAY_H_

#include <gtk/gtk.h>
#include <vte/vte.h>

#include "link.h"
#include "view_render.h"

/* Vide la vue et la redessine depuis l'historique avec les options courantes (vue, écho, CR/LF, horodatage, hexadécimal) */
void     terminal_display_redraw (void);
void     terminal_display_set_mode (GtViewMode mode);
void     set_hex_bytes_per_line (gint bpl);
void     set_hex_show_index (gboolean show);
void     clear_display (void);

/* Gestionnaires des signaux de la liaison */
void     terminal_display_on_received (GtkTermLink *link, GBytes *data, gint64 wall_time_us, gpointer user_data);
void     terminal_display_on_sent (GtkTermLink *link, GBytes *data, gint origin, gpointer user_data);
void     terminal_display_on_history_cleared (GtkTermLink *link, gpointer user_data);

void     Got_Input (VteTerminal *widget, gchar *text, guint length, gpointer ptr);

#endif
```

`src/terminal_display.c` :

```c
/***********************************************************************/
/* terminal_display.c                                                  */
/* ------------------                                                  */
/*           GTKTerm Software                                          */
/*                                                                     */
/*   Purpose                                                           */
/*      Alimentation du terminal VTE : les vues sont rendues par       */
/*      view_render.c depuis la liaison et son historique              */
/*                                                                     */
/***********************************************************************/

#include <string.h>
#include <vte/vte.h>

#include "link.h"
#include "view_render.h"
#include "terminal_display.h"

/* Display globals (defined in interface.c) */
extern GtkWidget *display;
extern gboolean echo_on;
extern gboolean crlfauto_on;
extern gboolean timestamp_on;
extern gboolean esc_clear_screen_on;

static GtViewOptions view_opts = { GT_VIEW_ASCII, FALSE, FALSE, FALSE, 16, FALSE };
static GtViewRender *view = NULL;

static GtViewRender *
current_view (void)
{
  if (view == NULL)
    view = gt_view_render_new (&view_opts);
  return view;
}

static void
feed (GString *s)
{
  if (display != NULL && s->len > 0)
    vte_terminal_feed (VTE_TERMINAL (display), s->str, (gssize) s->len);
}

static void
replay_record (const GtRecord *rec, gpointer user_data)
{
  gt_view_render_record (current_view (), rec, user_data);
}

void
terminal_display_redraw (void)
{
  GtkTermLink *link = gt_link_get_default ();
  GString *out = g_string_new (NULL);

  view_opts.echo = echo_on;
  view_opts.crlf_auto = crlfauto_on;
  view_opts.timestamp = timestamp_on;
  gt_view_render_reset (current_view (), &view_opts);
  clear_display ();
  if (link != NULL)
    gt_history_foreach (gt_link_get_history (link), replay_record, out);
  feed (out);
  g_string_free (out, TRUE);
}

void
terminal_display_set_mode (GtViewMode mode)
{
  view_opts.mode = mode;
  terminal_display_redraw ();
}

void
set_hex_bytes_per_line (gint bpl)
{
  view_opts.bytes_per_line = (guint) bpl;
}

void
set_hex_show_index (gboolean show)
{
  view_opts.show_index = show;
}

void
terminal_display_on_received (GtkTermLink *link, GBytes *data, gint64 wall_time_us, gpointer user_data)
{
  gsize len;
  const guint8 *p = g_bytes_get_data (data, &len);
  GString *out = g_string_new (NULL);

  gt_view_render_rx (current_view (), p, len, wall_time_us, out);
  feed (out);
  g_string_free (out, TRUE);
}

void
terminal_display_on_sent (GtkTermLink *link, GBytes *data, gint origin, gpointer user_data)
{
  gsize len;
  const guint8 *p = g_bytes_get_data (data, &len);
  GString *out = g_string_new (NULL);

  gt_view_render_tx (current_view (), p, len, (GtSendOrigin) origin, out);
  feed (out);
  g_string_free (out, TRUE);
}

void
terminal_display_on_history_cleared (GtkTermLink *link, gpointer user_data)
{
  terminal_display_redraw ();
}

void
Got_Input (VteTerminal *widget, gchar *text, guint length, gpointer ptr)
{
  /* « ESC efface l'écran » : seule la touche Échap tapée dans le terminal efface l'écran local, sans rien envoyer (M32).
     Les macros, Lua, la saisie hexadécimale et l'envoi de fichier envoient toujours 0x1B. */
  if (esc_clear_screen_on && length == 1 && (guchar) text[0] == '\x1b')
    {
      gt_link_clear_history (gt_link_get_default ());
      return;
    }

  gt_link_send (gt_link_get_default (), (const guint8 *) text, length, GT_SEND_KEYBOARD);
}

void
clear_display (void)
{
  if (display)
    {
      /* Feed clear sequences into the same VTE data stream so the erase
         is guaranteed to execute before any subsequent vte_terminal_feed
         calls (e.g. history replay). vte_terminal_reset() is a direct
         API call whose ordering relative to queued feed data is not
         guaranteed, which leaves stale hex content visible. */
      vte_terminal_feed (VTE_TERMINAL (display),
                         "\033c"    /* RIS – full terminal reset + clear screen */
                         "\033[3J", /* erase scrollback (VTE extension) */
                         7);
    }
}
```

- [ ] **Step 4 : interface, journal, fichiers**

`src/interface.h` : supprimer les déclarations `void put_text(const gchar *, guint);`, `void put_hexadecimal(const gchar *, guint);` et `gint send_serial(gchar *, gint);`.

`src/interface.c` :
- retirer `#include "buffer.h"` ;
- remplacer `set_view` par :

```c
void set_view(guint type)
{
	switch(type)
	{
	case HEXADECIMAL_VIEW:
		g_simple_action_set_state(action_view_hex, g_variant_new_boolean(TRUE));
		g_simple_action_set_enabled(action_view_index, TRUE);
		g_simple_action_set_enabled(action_view_hex_chars, TRUE);
		terminal_display_set_mode(GT_VIEW_HEX);
		break;
	default:
		g_simple_action_set_state(action_view_ascii, g_variant_new_boolean(TRUE));
		g_simple_action_set_enabled(action_view_index, FALSE);
		g_simple_action_set_enabled(action_view_hex_chars, FALSE);
		terminal_display_set_mode(GT_VIEW_ASCII);
		break;
	}
}
```

- l'écho, le CR/LF automatique et l'horodatage redessinent la vue (l'option écho est rétroactive, spec §6) : dans `Set_local_echo`, `echo_toggled_callback`, `Set_crlfauto`, `CR_LF_auto_toggled_callback`, `Set_timestamp`, `timestamp_toggled_callback`, ajouter `terminal_display_redraw();` en dernière instruction ;
- « Effacer l'écran » vide l'historique : ajouter avant `populate_file_menu`

```c
static void clear_screen_callback(GtkWidget *widget, gpointer data)
{
	gt_link_clear_history(gt_link_get_default());
}
```

  et dans `populate_file_menu`, remplacer `connect_menu_item_callback(item, G_CALLBACK(clear_buffer));` par `connect_menu_item_callback(item, G_CALLBACK(clear_screen_callback));` ;
- `Send_Hexadecimal` : remplacer `send_serial(buff, j);` par `gt_link_send(gt_link_get_default(), (const guint8 *)buff, j, GT_SEND_HEX);` ;
- `interface_connect_link` : supprimer `on_link_received_legacy` et remplacer le corps par :

```c
void interface_connect_link(GtkTermLink *link)
{
	g_signal_connect(link, "notify::status-text", G_CALLBACK(on_link_status), NULL);
	g_signal_connect(link, "open-failed", G_CALLBACK(on_link_open_failed), NULL);
	g_signal_connect(link, "received", G_CALLBACK(terminal_display_on_received), NULL);
	g_signal_connect(link, "received", G_CALLBACK(logging_on_received), NULL);
	g_signal_connect(link, "received", G_CALLBACK(files_on_received), NULL);
	g_signal_connect(link, "sent", G_CALLBACK(terminal_display_on_sent), NULL);
	g_signal_connect(link, "history-cleared", G_CALLBACK(terminal_display_on_history_cleared), NULL);
	on_link_status(G_OBJECT(link), NULL, NULL);
}
```

`src/gtkterm.c` : retirer `#include "buffer.h"`, les deux appels `create_buffer();` / `delete_buffer();` (y compris dans la branche d'erreur de `read_command_line`).

`src/logging.h` : remplacer les déclarations `void log_chars(const gchar *chars, guint size);` et `void logging_set_suspended(gboolean suspended);` par :

```c
#include "link.h"

/* Journal : texte reçu tel que la vue ASCII le montre (CR/LF automatique, horodatage), sans l'écho (spec, décision 3) */
void logging_on_received(GtkTermLink *link, GBytes *data, gint64 wall_time_us, gpointer user_data);
```

`src/logging.c` :
- retirer `#include "buffer.h"`, ajouter `#include "link.h"` et `#include "rx_transform.h"` ;
- remplacer `static gboolean   LoggingSuspended = FALSE;	/* … */` par `static GtRxTransform *log_transform = NULL;	/* état de ligne propre au journal */` et ajouter `extern gboolean crlfauto_on;` et `extern gboolean timestamp_on;` ;
- dans `OpenLogFile`, branche de réussite (après `Logging = TRUE;`), et dans `logging_clear` après le `freopen` réussi, ajouter :

```c
		if(log_transform != NULL)
			gt_rx_transform_reset(log_transform);
```

- supprimer `logging_set_suspended` ; renommer `log_chars` en `static void log_write(const gchar *chars, guint size)` et retirer `|| LoggingSuspended` de sa condition ;
- ajouter :

```c
void logging_on_received(GtkTermLink *link, GBytes *data, gint64 wall_time_us, gpointer user_data)
{
	gsize len;
	const guint8 *p = g_bytes_get_data(data, &len);
	GString *text;

	if(LoggingFile == NULL || Logging == FALSE)
		return;
	if(log_transform == NULL)
		log_transform = gt_rx_transform_new(crlfauto_on, timestamp_on);
	gt_rx_transform_set_options(log_transform, crlfauto_on, timestamp_on);
	text = g_string_new(NULL);
	gt_rx_transform_feed(log_transform, p, len, wall_time_us, text);
	log_write(text->str, (guint)text->len);
	g_string_free(text, TRUE);
}
```

  (`log_write` doit être défini avant `logging_on_received`.)

`src/files.c` :
- retirer `#include "buffer.h"`, ajouter `#include "rx_transform.h"`, `extern gboolean crlfauto_on;`, `extern gboolean timestamp_on;` ;
- dans `ecriture`, remplacer `bytes_written = send_serial(current_buffer, bytes_to_write - current_buffer_position);` par :

```c
		bytes_written = (gint)gt_link_send(gt_link_get_default(), (const guint8 *)current_buffer, (gsize)(bytes_to_write - current_buffer_position),
		                                   GT_SEND_FILE);
```

- supprimer `write_file`, `write_ascii_file` et le prototype `void write_file(const char *, unsigned int);` ; ajouter :

```c
typedef struct
{
	GtRxTransform *transform;
	GString *text;
} AsciiSave;

static void save_ascii_record(const GtRecord *rec, gpointer user_data)
{
	AsciiSave *s = user_data;

	if(rec->kind == GT_RECORD_RX)
		gt_rx_transform_feed(s->transform, rec->data, rec->len, rec->wall_time_us, s->text);
}

/* Octets reçus bruts : ni écho ni horodatage (spec §5) */
static void write_raw_history(FILE *f)
{
	GBytes *rx = gt_history_copy_rx(gt_link_get_history(gt_link_get_default()));
	gsize n;
	const guint8 *p = g_bytes_get_data(rx, &n);

	if(n > 0)
		fwrite(p, 1, n, f);
	g_bytes_unref(rx);
}

/* Texte reçu comme la vue ASCII le montre, sans caractères de contrôle autres que CR et LF (octets non signés : l'UTF-8 reste) */
static void write_ascii_history(FILE *f)
{
	AsciiSave s;

	s.transform = gt_rx_transform_new(crlfauto_on, timestamp_on);
	s.text = g_string_new(NULL);
	gt_history_foreach(gt_link_get_history(gt_link_get_default()), save_ascii_record, &s);
	for(gsize i = 0; i < s.text->len; i++)
	{
		guchar c = (guchar)s.text->str[i];

		if(c > 0x1F || c == 0x0A || c == 0x0D)
			fputc(c, f);
	}
	g_string_free(s.text, TRUE);
	gt_rx_transform_free(s.transform);
}
```

- dans `save_raw_file`, remplacer `write_buffer_with_func(write_file);` par `write_raw_history(Fic);` ; dans `save_ascii_file`, remplacer `write_buffer_with_func(write_ascii_file);` par `write_ascii_history(Fic);`.

Supprimer les fichiers et leur référence :

```bash
git rm src/buffer.c src/buffer.h
```

`src/meson.build`, `sources` : retirer `'buffer.c',` et `'buffer.h',`.

- [ ] **Step 5 : construction, suite, vérifications**

Run : `ninja -C build 2>&1 | grep -E 'error|implicit declaration|undefined' ; meson test -C build --print-errorlogs`
Expected : aucune erreur ; aucun échec.
Run : `grep -rnE 'send_serial|put_chars|set_tap_func|unset_tap_func|write_buffer|clear_buffer|buffer\.h|log_chars|logging_set_suspended|virt_col_pos|Envoie_car' src tests | grep -vE '^src/(interface\.c:(18|36)|files\.c:17):'`
Expected : aucune sortie (les trois lignes écartées sont des entrées historiques des en-têtes ChangeLog, laissées telles quelles).

- [ ] **Step 6 : essai de fumée sur l'affichage isolé**

`$SCRATCH/fumee_t8.py` (écho rétroactif et conservé au changement de vue) :

```python
import sys, time
sys.path.insert(0, '/home/METRO_gabriel/git/gtkterm/.superpowers/banc-gui')
from common import *
e = Env('fumee_t8')
ma = os.path.join(e.root, 'macros.ini')
e.write_macros(ma, [('CMD', 'None', 'CMD', '', '', 0, 1000)])
e.write_rc(e.profile(echo='True', macros_file=ma))
e.start(); time.sleep(2)
txt = lambda: get_text(find1(e.app, 'terminal'))
e.write_pty(b'RX1\r\n'); time.sleep(0.5)
act(find1(e.app, 'push button', '^CMD$', showing=True)); time.sleep(0.8)
assert e.read_pty(0.5) == b'CMD'
assert txt().index('RX1') < txt().index('> CMD'), txt()
menu(e.app, 'View', 'Hexadecimal'); time.sleep(1.0)
menu(e.app, 'View', 'ASCII'); time.sleep(1.0)
assert txt().index('RX1') < txt().index('> CMD'), txt()
finish(e)
print('FUMEE OK')
```

Run : `cp $SCRATCH/fumee_t8.py .superpowers/banc-gui/ && EM_TIMEOUT=90 sh .superpowers/banc-gui/run.sh .superpowers/banc-gui/fumee_t8.py 2>&1 | tail -5 ; rm -rf .superpowers/banc-gui/fumee_t8.py .superpowers/banc-gui/runs/fumee_t8`
Expected : `FUMEE OK`.

- [ ] **Step 7 : commit**

```bash
git add -A src tests
git commit -m "affichage : vues redessinées depuis l'historique, un seul chemin d'envoi, journal et sauvegardes depuis la liaison

buffer.c disparaît : la vue ASCII et la vue hexadécimale sont rendues par view_render.c depuis l'historique (écho à sa
place, conservé au changement de vue, index hexadécimal compté sur les seuls octets reçus, M37) ; tous les envois
passent par gt_link_send avec leur origine ; le journal transcrit le texte reçu sans l'écho ; Lua écoute le signal
« received », connecté sur le thread principal.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9 : suite graphique automatisée (`tests/gui/`)

**Files :**
- Create : `tests/gui/meson.build`, `tests/gui/run-gui-test.sh`, `tests/gui/guard.c`, `tests/gui/gtktest.py`, `tests/gui/broadway.py` (copie), `tests/gui/bin/git` (copie), et les scénarios `tests/gui/test_lien_perdu.py`, `test_echo_vues.py`, `test_hex_index.py`, `test_journal.py`, `test_horodatage.py`, `test_sauvegarde_brute.py`, `test_echo_clavier.py`, `test_envoi_fichier.py`, `test_raccourcis_macros.py`, `test_polling.py`
- Modify : `tests/meson.build`, `src/meson.build` (nommer l'exécutable), `.gitignore` si besoin (`__pycache__`)

**Interfaces :**
- Consumes : l'application complète (tâches 7 et 8), le banc d'essai du sous-projet 1 sauvegardé dans `.superpowers/banc-gui/` (ignoré par git).
- Produces : `meson test -C build --suite gui` ; la suite `gui` est exclue de `meson test -C build`.

Si un scénario échoue à cause d'un défaut du produit, corriger le produit (avec un test unitaire ou d'intégration qui échoue d'abord quand c'est possible) dans cette tâche et le dire dans le rapport ; ne jamais affaiblir l'assertion du scénario pour le faire passer.

- [ ] **Step 1 : harnais**

1. Copier tels quels : `.superpowers/banc-gui/broadway.py` → `tests/gui/broadway.py` ; `.superpowers/banc-gui/bin/git` → `tests/gui/bin/git` (garder le bit exécutable : `chmod +x`).

2. `tests/gui/guard.c` (généralisation de `.superpowers/banc-gui/guard.c` : le domicile réel vient de l'environnement) :

```c
/* Garde-fou LD_PRELOAD de la suite graphique : refuse l'ouverture des vrais ports série et de la configuration réelle de
   l'utilisateur (domicile réel transmis par GTKTERM_GUARD_REAL_HOME). */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int starts_with(const char *p, const char *prefix)
{
	return strncmp(p, prefix, strlen(prefix)) == 0;
}

static int forbidden(const char *p)
{
	static const char *devices[] = { "/dev/ttyS", "/dev/ttyACM", "/dev/ttyUSB", "/dev/serial", NULL };
	static const char *home_entries[] = { "/.config", "/.gtktermrc", "/.cache", "/.local", NULL };
	const char *home = getenv("GTKTERM_GUARD_REAL_HOME");
	char buf[4096];
	int blocked = 0;

	if(p == NULL)
		return 0;
	for(int i = 0; devices[i] != NULL && !blocked; i++)
		blocked = starts_with(p, devices[i]);
	for(int i = 0; home != NULL && home[0] != '\0' && home_entries[i] != NULL && !blocked; i++)
	{
		snprintf(buf, sizeof(buf), "%s%s", home, home_entries[i]);
		blocked = starts_with(p, buf);
	}
	if(blocked)
	{
		int n = snprintf(buf, sizeof(buf), "EMGUARD: blocked %s\n", p);

		if(write(2, buf, (size_t)n) < 0)
			return 1;
	}
	return blocked;
}

#define WRAP(name) \
int name(const char *p, int f, ...) { static int (*r)(const char *, int, ...); if(!r) r = dlsym(RTLD_NEXT, #name); \
	mode_t m = 0; if(f & (O_CREAT | O_TMPFILE)) { va_list a; va_start(a, f); m = va_arg(a, mode_t); va_end(a); } \
	if(forbidden(p)) { errno = EACCES; return -1; } return r(p, f, m); }
WRAP(open)
WRAP(open64)

#define WRAPAT(name) \
int name(int d, const char *p, int f, ...) { static int (*r)(int, const char *, int, ...); if(!r) r = dlsym(RTLD_NEXT, #name); \
	mode_t m = 0; if(f & (O_CREAT | O_TMPFILE)) { va_list a; va_start(a, f); m = va_arg(a, mode_t); va_end(a); } \
	if(forbidden(p)) { errno = EACCES; return -1; } return r(d, p, f, m); }
WRAPAT(openat)
WRAPAT(openat64)

FILE *fopen(const char *p, const char *m)
{
	static FILE *(*r)(const char *, const char *);

	if(!r)
		r = dlsym(RTLD_NEXT, "fopen");
	if(forbidden(p))
	{
		errno = EACCES;
		return NULL;
	}
	return r(p, m);
}

FILE *fopen64(const char *p, const char *m)
{
	static FILE *(*r)(const char *, const char *);

	if(!r)
		r = dlsym(RTLD_NEXT, "fopen64");
	if(forbidden(p))
	{
		errno = EACCES;
		return NULL;
	}
	return r(p, m);
}
```

3. `tests/gui/run-gui-test.sh` :

```sh
#!/bin/sh
# Lance un scénario graphique dans un environnement isolé : affichage broadway privé, bus D-Bus et AT-SPI privés,
# HOME / XDG_* dans le répertoire de travail du test, garde-fou contre les vrais ports et la vraie configuration.
# Sauté (code 77) si un prérequis manque.
command -v broadwayd >/dev/null 2>&1 || { echo "broadwayd absent"; exit 77; }
command -v dbus-run-session >/dev/null 2>&1 || { echo "dbus-run-session absent"; exit 77; }
python3 -c 'import gi; gi.require_version("Atspi", "2.0"); from gi.repository import Atspi' 2>/dev/null || { echo "python3-gi / Atspi absent"; exit 77; }
: "${GTKTERM_GUI_RUNS:?GTKTERM_GUI_RUNS non défini}"
SCENARIO=$1
WORK="$GTKTERM_GUI_RUNS/$(basename "$SCENARIO" .py)-session"
rm -rf "$WORK" && mkdir -p "$WORK/run" "$WORK/home" && chmod 700 "$WORK/run"
export GTKTERM_GUARD_REAL_HOME="$HOME"
exec env -u DISPLAY -u WAYLAND_DISPLAY -u XAUTHORITY -u DBUS_SESSION_BUS_ADDRESS -u AT_SPI_BUS_ADDRESS \
  XDG_RUNTIME_DIR="$WORK/run" HOME="$WORK/home" XDG_CONFIG_HOME="$WORK/home/.config" XDG_CACHE_HOME="$WORK/home/.cache" \
  XDG_DATA_HOME="$WORK/home/.local/share" XDG_DATA_DIRS=/usr/local/share:/usr/share GIO_USE_VFS=local GIO_USE_VOLUME_MONITOR=unix GTK_USE_PORTAL=0 \
  PYTHONPATH="$(dirname "$SCENARIO")" PYTHONDONTWRITEBYTECODE=1 \
  timeout "${GTKTERM_GUI_TIMEOUT:-240}" dbus-run-session -- python3 "$SCENARIO"
```

4. `tests/gui/gtktest.py` : partir de `.superpowers/banc-gui/emlib.py` (copie intégrale), puis :
   - remplacer l'en-tête des chemins par :

```python
# Bibliothèque de la suite graphique : environnement isolé, pty, broadway, pilotage AT-SPI, assertions.
import os, sys, time, shutil, signal, subprocess, select, tty, termios, re

SCR = os.path.dirname(os.path.abspath(__file__))
BIN = os.environ['GTKTERM_BIN']
GUARD = os.environ['GTKTERM_GUARD']
FAKEBIN = os.path.join(SCR, 'bin')
RUNS = os.environ['GTKTERM_GUI_RUNS']
```

     (supprimer `REPO`, `BIN_ASAN` et le paramètre `asan` de `Env.__init__` ; dans `start`, `binary = BIN`) ;
   - dans `Env.__init__`, `self.root = os.path.join(RUNS, name)` et ajouter `self.link = None` ;
   - dans `Env.env()`, remplacer `GIO_MODULE_DIR=os.path.join(SCR, 'giomods')` par `GIO_MODULE_DIR=self.giomods` après avoir créé, dans `__init__`, `self.giomods = os.path.join(self.root, 'giomods'); os.makedirs(self.giomods)` (répertoire vide : aucun module GIO chargé) ;
   - ajouter à `Env` les méthodes :

```python
    # ---------- port derrière un lien symbolique (perte et retour du périphérique) ----------
    def use_link(self):
        """Le port est un lien symbolique stable vers la pty : on peut la « débrancher » et la « rebrancher »."""
        self.link = os.path.join(self.root, 'port')
        os.symlink(self.pty, self.link)
        return self.link

    def unplug(self):
        for fd in (self.master, self.slave):
            try:
                os.close(fd)
            except OSError:
                pass
        self.master = self.slave = -1

    def replug(self):
        self.master, self.slave = os.openpty()
        tty.setraw(self.slave)
        self.pty = os.ttyname(self.slave)
        os.set_blocking(self.master, False)
        os.unlink(self.link)
        os.symlink(self.pty, self.link)
```

   - ajouter en fin de fichier les aides reprises de `.superpowers/banc-gui/common.py` (`rep`, `file_dialog`, `fix_path_entry`, l'import `from broadway import Broadway, GDK_CONTROL, GDK_SHIFT, GDK_MOD1`) et les assertions :

```python
from broadway import Broadway, GDK_CONTROL, GDK_SHIFT, GDK_MOD1

R = []
FAILS = []
# Bruit connu de GTK sous broadway, sans rapport avec GTKTerm
KNOWN_NOISE = ("gtk_widget_get_preferred_height_for_width: assertion 'width >= 0' failed",)


def rep(*a):
    s = ' '.join(str(x) for x in a)
    R.append(s)
    log(s)


def check(cond, label, detail=''):
    rep(('OK    ' if cond else 'ECHEC ') + label + ((' : %r' % (detail,)) if (detail != '' and not cond) else ''))
    if not cond:
        FAILS.append(label)
    return cond


def wait_until(pred, timeout=5.0, step=0.1):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            v = pred()
            if v:
                return v
        except Exception:
            pass
        time.sleep(step)
    try:
        return pred()
    except Exception:
        return False


def terminal_text(app):
    return get_text(find1(app, 'terminal')) or ''


def status_text(app):
    return name(find1(app, 'status bar'))


def alerts(app):
    return [w for w in windows(app) if role(w) == 'alert']


def macro_button(app, label):
    return find1(app, 'push button', '^%s$' % re.escape(label), showing=True)


def finish(e, quit=True):
    if quit and e.proc.poll() is None:
        try:
            menu(e.app, 'File', 'Quit')
            e.proc.wait(10)
        except Exception as ex:
            rep('quit via menu impossible:', ex)
    rep('sortie : rc=%s' % e.stop())
    reps = e.asan_reports()
    check(not reps, 'aucun rapport ASan', reps)
    st = e.stderr()
    bad = [l for l in st.splitlines()
           if any(k in l for k in ('runtime error', 'Sanitizer', 'CRITICAL', 'EMGUARD', 'Segmentation')) and not any(n in l for n in KNOWN_NOISE)]
    check(not bad, 'stderr sans erreur', bad[:5])
    e.close()
    with open(os.path.join(e.root, 'result.txt'), 'w') as f:
        f.write('\n'.join(R))


def conclude():
    rep('BILAN : %d echec(s)%s' % (len(FAILS), (' : ' + ', '.join(FAILS)) if FAILS else ''))
    return 1 if FAILS else 0
```

     (`file_dialog` et `fix_path_entry` : copiés tels quels de `common.py`.)

5. `tests/gui/meson.build` :

```meson
# Suite graphique : scénarios Python pilotant GTKTerm par AT-SPI sur un affichage broadway privé (voir tests/README.md).
# Lancée par « meson test -C build --suite gui » ; exclue de « meson test -C build » (add_test_setup dans tests/meson.build).
gui_guard = shared_module('gtkterm_guard', 'guard.c',
	name_prefix : '',
	dependencies : [cc.find_library('dl', required : false)])

gui_env = environment()
gui_env.set('GTKTERM_BIN', gtkterm_exe.full_path())
gui_env.set('GTKTERM_GUARD', gui_guard.full_path())
gui_env.set('GTKTERM_GUI_RUNS', meson.current_build_dir() / 'runs')

gui_scenarios = [
	'test_lien_perdu',
	'test_echo_vues',
	'test_hex_index',
	'test_journal',
	'test_horodatage',
	'test_sauvegarde_brute',
	'test_echo_clavier',
	'test_envoi_fichier',
	'test_raccourcis_macros',
	'test_polling',
]

foreach s : gui_scenarios
	test(s, find_program('sh'),
		args : [files('run-gui-test.sh'), files(s + '.py')],
		suite : 'gui',
		env : gui_env,
		depends : [gtkterm_exe, gui_guard],
		timeout : 300,
		is_parallel : false)
endforeach
```

6. `src/meson.build` : `executable(` → `gtkterm_exe = executable(`.
7. `tests/meson.build`, en fin de fichier :

```meson
subdir('gui')

# La suite graphique ne tourne que sur demande : meson test -C build --suite gui
add_test_setup('default', exclude_suites : ['gui'], is_default : true)
```

8. Si `git status` montre `tests/gui/__pycache__/`, ajouter `__pycache__/` à `.gitignore`.

- [ ] **Step 2 : scénarios du sous-projet 2**

`tests/gui/test_lien_perdu.py` :

```python
# Perte de liaison : barre d'état « — lost » puis « — reconnecting… », aucune boîte de dialogue, retour à l'état ouvert.
from gtktest import *

# 1. Reconnexion automatique active
e = Env('lien_perdu')
port = e.use_link()
e.write_rc(e.profile(port=port, autoreconnect_enabled='True'))
e.start(force_port=False); time.sleep(2)
base = '%s  115200-8-N-1' % port
check(wait_until(lambda: status_text(e.app) == base, 5), 'ouvert au demarrage (chemin complet)', status_text(e.app))
e.unplug()
check(wait_until(lambda: status_text(e.app).endswith(' — reconnecting…'), 5), 'reconnexion affichee', status_text(e.app))
check(alerts(e.app) == [], 'aucune boite de dialogue a la perte')
time.sleep(4.5)
check(alerts(e.app) == [], 'aucune boite de dialogue pendant les essais')
e.replug()
check(wait_until(lambda: status_text(e.app) == base, 6), 'retour a l etat ouvert', status_text(e.app))
check(wait_until(lambda: name(find1(e.app, 'frame')) == 'GTKTerm - ' + base, 3), 'titre de fenetre a jour', name(find1(e.app, 'frame')))
e.write_pty(b'retour\r\n')
check(wait_until(lambda: 'retour' in terminal_text(e.app), 3), 'reception apres reconnexion')
finish(e)

# 2. Sans reconnexion automatique
e = Env('lien_perdu_sans')
port = e.use_link()
e.write_rc(e.profile(port=port))
e.start(force_port=False); time.sleep(2)
e.unplug()
check(wait_until(lambda: status_text(e.app).endswith(' — lost'), 5), 'perte affichee', status_text(e.app))
time.sleep(2.5)
check(status_text(e.app).endswith(' — lost'), 'pas de reconnexion sans l option', status_text(e.app))
check(alerts(e.app) == [], 'aucune boite de dialogue')
finish(e)
sys.exit(conclude())
```

`tests/gui/test_echo_vues.py` :

```python
# Écho conservé à sa place au changement de vue, et rétroactif quand on rebascule l'option.
from gtktest import *

e = Env('echo_vues')
ma = os.path.join(e.root, 'macros.ini')
e.write_macros(ma, [('CMD', 'None', 'CMD', '', '', 0, 1000)])
e.write_rc(e.profile(echo='True', macros_file=ma))
e.start(); time.sleep(2)

def ordre_ok():
    t = terminal_text(e.app)
    return 'RX1' in t and '> CMD' in t and t.index('RX1') < t.index('> CMD')

e.write_pty(b'RX1\r\n'); time.sleep(0.5)
act(macro_button(e.app, 'CMD')); time.sleep(0.5)
check(e.read_pty(1.0) == b'CMD', 'macro envoyee au port')
check(wait_until(ordre_ok, 3), 'echo apres la reception', terminal_text(e.app)[:200])
menu(e.app, 'View', 'Hexadecimal'); time.sleep(1.0)
menu(e.app, 'View', 'ASCII'); time.sleep(1.0)
check(wait_until(ordre_ok, 3), 'echo conserve a sa place apres ASCII -> hexa -> ASCII', terminal_text(e.app)[:200])
e.write_pty(b'RX2\r\n')
check(wait_until(lambda: terminal_text(e.app).find('RX2') > terminal_text(e.app).find('> CMD') >= 0, 3), 'reception suivante apres l echo')
menu(e.app, 'Configuration', 'Local echo'); time.sleep(1.0)
check(wait_until(lambda: '> CMD' not in terminal_text(e.app) and 'RX1' in terminal_text(e.app), 3), 'echo desactive : envoi masque, reception gardee')
menu(e.app, 'Configuration', 'Local echo'); time.sleep(1.0)
check(wait_until(ordre_ok, 3), 'echo reactive : envoi reaffiche a sa place')
finish(e)
sys.exit(conclude())
```

`tests/gui/test_hex_index.py` :

```python
# Vue hexadécimale : l'écho a sa propre ligne et ne décale pas l'index des octets reçus.
from gtktest import *

e = Env('hex_index')
ma = os.path.join(e.root, 'macros.ini')
e.write_macros(ma, [('Z', 'None', 'Z', '', '', 0, 1000)])
e.write_rc(e.profile(echo='True', macros_file=ma))
e.start(); time.sleep(2)
menu(e.app, 'View', 'Hexadecimal'); time.sleep(0.6)
menu(e.app, 'View', 'Show index'); time.sleep(0.6)
e.write_pty(b'ABC'); time.sleep(0.6)
act(macro_button(e.app, 'Z')); time.sleep(0.6)
e.write_pty(b'DEF')
check(wait_until(lambda: '     0: 41 42 43' in terminal_text(e.app), 3), 'premiere ligne recue, index 0', terminal_text(e.app)[:300])
check(wait_until(lambda: '     >  5A' in terminal_text(e.app), 3), 'echo sur sa propre ligne')
check(wait_until(lambda: '     3: 44 45 46' in terminal_text(e.app), 3), 'index des octets recus non decale par l echo', terminal_text(e.app)[:300])
finish(e)
sys.exit(conclude())
```

`tests/gui/test_journal.py` :

```python
# Journal : texte reçu tel que la vue ASCII le montre (CR/LF automatique), sans écho.
from gtktest import *

e = Env('journal')
ma = os.path.join(e.root, 'macros.ini')
e.write_macros(ma, [('X', 'None', 'X', '', '', 0, 1000)])
e.write_rc(e.profile(echo='True', crlfauto='True', macros_file=ma))
e.start(); time.sleep(2)
bw = Broadway(e.display)
term = find1(e.app, 'terminal')
logf = os.path.join(e.root, 'journal.txt')
menu_key(bw, e.app, 'Log', 'To file...', focus_obj=term)
file_dialog(bw, e.app, 'Log file selection', logf)
e.write_pty(b'a\nb\n'); time.sleep(0.5)
act(macro_button(e.app, 'X')); time.sleep(0.5)
bw.click_obj(term); bw.warm(); bw.type('k'); time.sleep(0.5)
e.write_pty(b'c\r'); time.sleep(0.5)
check('> X' in terminal_text(e.app), 'echo visible dans la vue')
menu(e.app, 'Log', 'Stop'); time.sleep(0.5)
data = open(logf, 'rb').read() if os.path.exists(logf) else None
check(data == b'a\r\nb\r\nc\r', 'journal = texte recu transforme, sans echo', data)
finish(e)
sys.exit(conclude())
```

`tests/gui/test_horodatage.py` :

```python
# Horodatage activé après coup : affiché sur les lignes déjà reçues (rejeu de l'historique).
from gtktest import *

e = Env('horodatage')
e.write_rc(e.profile())
e.start(); time.sleep(2)
e.write_pty(b'L1\r\nL2\r\n'); time.sleep(0.8)
check('L1' in terminal_text(e.app) and '[' not in terminal_text(e.app), 'sans horodatage au depart', terminal_text(e.app)[:120])
menu(e.app, 'Configuration', 'Timestamp'); time.sleep(1.0)
for ligne in ('L1', 'L2'):
    motif = re.compile(r'^\[\d+\.\d\dh\.\d\dm\.\d\ds\.\d{3}\] %s$' % ligne, re.M)
    check(wait_until(lambda: motif.search(terminal_text(e.app)), 3), 'horodatage retroactif sur %s' % ligne, terminal_text(e.app)[:200])
finish(e)
sys.exit(conclude())
```

`tests/gui/test_sauvegarde_brute.py` :

```python
# Sauvegarde brute : octets reçus seulement, ni écho ni horodatage ni CR/LF ajouté.
from gtktest import *

e = Env('sauvegarde_brute')
ma = os.path.join(e.root, 'macros.ini')
e.write_macros(ma, [('X', 'None', 'X', '', '', 0, 1000)])
e.write_rc(e.profile(echo='True', crlfauto='True', timestamp='True', macros_file=ma))
e.start(); time.sleep(2)
bw = Broadway(e.display)
term = find1(e.app, 'terminal')
e.write_pty(b'\x01raw\r\n\xff'); time.sleep(0.5)
act(macro_button(e.app, 'X')); time.sleep(0.5)
out = os.path.join(e.root, 'brut.bin')
menu_key(bw, e.app, 'File', 'Save RAW file', focus_obj=term)
file_dialog(bw, e.app, 'Save RAW File', out)
check(wait_until(lambda: os.path.exists(out), 3), 'fichier ecrit')
data = open(out, 'rb').read() if os.path.exists(out) else None
check(data == b'\x01raw\r\n\xff', 'sauvegarde brute = octets recus seuls', data)
finish(e)
sys.exit(conclude())
```

`tests/gui/test_echo_clavier.py` :

```python
# Frappe clavier avec écho : répétée dans la ligne, sans ligne « > ».
from gtktest import *

e = Env('echo_clavier')
e.write_rc(e.profile(echo='True'))
e.start(); time.sleep(2)
bw = Broadway(e.display)
term = find1(e.app, 'terminal')
e.write_pty(b'prompt> '); time.sleep(0.5)
bw.click_obj(term); bw.warm(); e.drain(0.2)
bw.type('abc'); time.sleep(0.8)
check(e.read_pty(0.5) == b'abc', 'frappe envoyee au port')
check(wait_until(lambda: 'prompt> abc' in terminal_text(e.app), 3), 'frappe repetee dans la ligne', terminal_text(e.app)[:120])
check('> a' not in terminal_text(e.app).replace('prompt> abc', ''), 'pas de ligne > pour la frappe')
finish(e)
sys.exit(conclude())
```

- [ ] **Step 3 : scénarios de non-régression du sous-projet 1 (chemin d'envoi)**

`tests/gui/test_envoi_fichier.py` :

```python
# Envoi de fichier par la liaison : fichier intact, sans délai et avec attente d'un caractère renvoyé par l'équipement.
from gtktest import *

def envoyer(e, chemin):
    bw = Broadway(e.display)
    term = find1(e.app, 'terminal')
    e.drain(0.2)
    menu_key(bw, e.app, 'File', 'Send RAW file', focus_obj=term)
    w = focus_window(bw, e.app, 'Send RAW File')
    bw.key('l', GDK_CONTROL); time.sleep(0.4)
    bw.type(chemin); time.sleep(0.3)
    fix_path_entry(w, chemin)
    bw.key('Return')

# 1. Sans délai
e = Env('envoi_fichier')
e.write_rc(e.profile())
data = b''.join(b'Ligne %05d ABCDEFGHIJKLMNOPQRSTUVWXYZ\n' % i for i in range(1000))
txt = os.path.join(e.root, 'texte.txt')
open(txt, 'wb').write(data)
e.start(); time.sleep(2)
envoyer(e, txt)
got = e.read_pty(6.0)
check(got == data, 'fichier recu intact (%d octets)' % len(data), (len(got), got[:60]))
check(e.proc.poll() is None, 'application vivante')
finish(e)

# 2. Attente du caractère « > » après chaque ligne : l'équipement simulé le renvoie
e = Env('envoi_fichier_attente')
e.write_rc(e.profile(wait_char=62))
data = b''.join(b'L%03d\n' % i for i in range(50))
txt = os.path.join(e.root, 'texte.txt')
open(txt, 'wb').write(data)
e.start(); time.sleep(2)
envoyer(e, txt)
got = b''
t0 = time.time()
while len(got) < len(data) and time.time() - t0 < 20:
    chunk = e.read_pty(0.2)
    got += chunk
    for _ in range(chunk.count(b'\n')):
        e.write_pty(b'>')
check(got == data, 'fichier recu intact avec attente du caractere', (len(got), got[-40:]))
finish(e)
sys.exit(conclude())
```

`tests/gui/test_raccourcis_macros.py` :

```python
# Raccourcis de macros : la macro part par la liaison, la touche n'atteint pas le terminal (M43).
from gtktest import *

e = Env('raccourcis_macros')
ma = os.path.join(e.root, 'macros.ini')
e.write_macros(ma, [('M1', '<Control>F5', '<M1>', '', '', 0, 1000), ('M2', '<Control>F6', '<M2>', '', '', 0, 1000)])
e.write_rc(e.profile(macros_file=ma))
e.start(); time.sleep(2)
bw = Broadway(e.display)
term = find1(e.app, 'terminal')
for touche, attendu in (('F5', b'<M1>'), ('F6', b'<M2>')):
    bw.click_obj(term); bw.warm(); e.drain(0.2)
    bw.key(touche, GDK_CONTROL)
    d = e.read_pty(0.8)
    check(d == attendu, 'Ctrl+%s envoie %r seul' % (touche, attendu), d)
finish(e)
sys.exit(conclude())
```

`tests/gui/test_polling.py` :

```python
# Polling d'une macro : envois répétés par la liaison, arrêt au second clic.
from gtktest import *

e = Env('polling')
ma = os.path.join(e.root, 'macros.ini')
e.write_macros(ma, [('PollA', 'None', '<A1>', '', '', 1, 200)])
e.write_rc(e.profile(macros_file=ma))
e.start(); time.sleep(2)
b = macro_button(e.app, 'PollA')
act(b)
d = e.read_pty(1.5)
check(d.count(b'<A1>') >= 4, 'polling actif : au moins 4 envois en 1,5 s', d)
act(b); time.sleep(0.5); e.drain(0.3)
d = e.read_pty(1.0)
check(d == b'', 'polling arrete', d)
finish(e)
sys.exit(conclude())
```

- [ ] **Step 4 : lancer la suite graphique**

Run : `ninja -C build && meson test -C build --suite gui --print-errorlogs 2>&1 | tail -20`
Expected : 10 tests `OK` (ou `SKIP` partout si broadwayd/Atspi manquent — ils sont présents sur le poste de développement : un SKIP y est un échec à corriger).
En cas d'échec, lire `build/tests/gui/runs/<scénario>/result.txt` et `gtkterm.stderr`, corriger la cause (produit ou pilotage), relancer.

Run : `meson test -C build --print-errorlogs | tail -5`
Expected : la suite par défaut ne lance aucun test `gui` (vérifier qu'aucune ligne `gui /` n'apparaît) et n'a aucun échec.

- [ ] **Step 5 : commit**

```bash
git add tests/gui tests/meson.build src/meson.build .gitignore
git commit -m "tests : suite graphique automatisée (broadway, AT-SPI) pour la liaison, l'écho, le journal et les sauvegardes

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 10 : traductions, documentation, critères d'acceptation

**Files :**
- Modify : `po/POTFILES.in`, `po/fr.po`, `CLAUDE.md`, `tests/README.md`, `docs/superpowers/reviews/2026-10-06-revue-code.md`

**Interfaces :**
- Consumes : tout ce qui précède.
- Produces : critères d'acceptation 1 à 5 de la spec vérifiés.

- [ ] **Step 1 : liste des sources traduisibles**

`po/POTFILES.in` : retirer `src/buffer.c`, `src/device_monitor.c`, `src/serial.c` ; ajouter `src/link.c` (ordre alphabétique, après `src/interface.c`).

Run : `ninja -C build gtkterm-pot 2>&1 | tail -3 && git checkout po/gtkterm.pot`
Expected : la cible se construit sans erreur (on ne garde pas le `.pot` régénéré : sa mise à jour complète, avec les autres langues, est prévue plus tard).

- [ ] **Step 2 : traductions françaises des nouveaux textes**

Ajouter à la fin de `po/fr.po` :

```po
#: ../src/link.c
#, c-format
msgid "tcp://%s:%s (server, no client)"
msgstr "tcp://%s:%s (serveur, aucun client)"

#: ../src/link.c
#, c-format
msgid "tcp://%s:%s (server)"
msgstr "tcp://%s:%s (serveur)"

#: ../src/link.c
#, c-format
msgid "%s — connecting…"
msgstr "%s — connexion…"

#: ../src/link.c
#, c-format
msgid "%s — lost"
msgstr "%s — perdu"

#: ../src/link.c
#, c-format
msgid "%s — reconnecting…"
msgstr "%s — reconnexion…"

#: ../src/link.c
msgid "device removed"
msgstr "périphérique retiré"

#: ../src/transport.c
msgid "end of stream"
msgstr "fin du flux"
```

Si l'un de ces identifiants existe déjà dans `fr.po`, ne pas le dupliquer. Pour `"Accept error"` (nouvel identifiant sans `\n` du transport), ajouter aussi :

```po
#: ../src/transport.c
msgid "Accept error"
msgstr "Erreur d'acceptation"
```

Run : `msgfmt --check -o /dev/null po/fr.po`
Expected : aucune sortie (fichier valide, formats cohérents).

- [ ] **Step 3 : `CLAUDE.md`**

Remplacer, dans la section « Architecture », les paragraphes **Receive path**, la description de `buffer.c` (liste `display_func` / `tap_func` / `clear_func` et la phrase qui suit) et **Transport abstraction** par :

```markdown
**Layers (sub-project 2):** the GLib-only core (`gtkterm_core`: `transport.c`, `history.c`, `rx_transform.c`, `device_match.c`, `view_render.c`), the link (`link.c`, `GtkTermLink` GObject + GUdev) and the GTK 3 UI. The UI never talks to the transport directly.

**Receive path (device → screen):** `transport.c` reads bytes → `on_data` callback → **`link.c`** appends them to the `GtHistory` (RX record with its wall time) and emits `received (GBytes *data, gint64 wall_time_us)` → subscribers: `terminal_display.c` (renders through `view_render.c`, then `vte_terminal_feed()`), `logging.c` (own `GtRxTransform`, no echo), `files.c` (wait-for-char during file send), the Lua engine (port 0, connected on the main thread).

**Send path:** everything goes through `gt_link_send(link, data, len, GtSendOrigin)` (keyboard, macro, Lua, hex, file). Only bytes actually written are appended to the history (TX record) and emitted by `sent`; echo is rendered from that, so it stays in place across view switches. The history keeps **raw** bytes: CR/LF auto and timestamps are a read-time transform (`GtRxTransform`), so toggling an option, the echo or the view redraws the terminal from the history (`terminal_display_redraw()`).

**Transport (`transport.c` / `transport.h`):** one instance per opened port (`gt_transport_open(params, callbacks, user_data, GError **)`), three modes — `TRANSPORT_SERIAL`, `TRANSPORT_TCP_CLIENT`, `TRANSPORT_TCP_SERVER`; no UI calls, errors via `GError`, `LOST` reported (deferred to an idle) instead of reconnecting. Serial specifics: termios in `serial_termios.c`, baud-rate tables in `baud.c`/`baudrates.c`. Privileged TCP ports use `CAP_NET_BIND_SERVICE`.

**Link (`link.c` / `link.h`):** single state machine for the main port — `CLOSED`, `CONNECTING`, `OPEN`, `LISTENING`, `LOST`, `RECONNECTING`; properties `state`, `status-text`, `auto-reconnect`, `reconnect-interval-ms`; signals `open-failed` (user-requested open only → error dialog), `received`, `sent`, `history-cleared`. Link loss never opens a dialog: the status bar shows « — lost » / « — reconnecting… »; silent retry every 2 s plus an immediate retry when udev reports the device back. The udev monitor lives here and matches by device number, then path/links (`device_match.c`).
```

Dans le paragraphe **Lua scripting**, remplacer la mention du `tap_func` par : « the main port (0) is observed through the link's `received` signal, connected and disconnected on the main thread ». Dans **Other entry points**, remplacer `device_monitor.c (GUdev serial-device hotplug)` par `link.c (port state machine, GUdev hotplug)` et retirer `buffer.c` de la liste des fichiers au style dominant dans **Code style** (le remplacer par `link.c`). Dans **Build / Run**, ajouter la ligne :

```sh
meson test -C build --suite gui   # suite graphique (broadway + AT-SPI isolés, jamais l'écran réel)
```

et remplacer la phrase « GUI behavior still needs a manual run of the app. » par « GUI behavior is covered by the `gui` suite (`tests/gui/`, see `tests/README.md`). »

- [ ] **Step 4 : `tests/README.md`**

Ajouter une section :

```markdown
## Suite graphique (`tests/gui/`)

`meson test -C build --suite gui` lance des scénarios Python qui pilotent GTKTerm par AT-SPI sur un affichage **broadway privé** :
bus D-Bus et AT-SPI privés (`dbus-run-session`), `HOME` / `XDG_*` dans `build/tests/gui/runs/`, ports pty uniquement, faux `git`
(aucun réseau), garde-fou `LD_PRELOAD` (`guard.c`) qui refuse les vrais ports série et la configuration réelle de l'utilisateur.
La suite est exclue de `meson test -C build` ; chaque test est sauté (code 77) si `broadwayd`, `dbus-run-session` ou
`python3-gi` (Atspi 2.0) manquent. Résultats détaillés : `build/tests/gui/runs/<scénario>/result.txt` et `gtkterm.stderr`.

Pour ajouter un scénario : écrire `tests/gui/test_<nom>.py` avec `from gtktest import *`, des `check(condition, libellé, détail)`,
`finish(e)` puis `sys.exit(conclude())`, et l'ajouter à `gui_scenarios` dans `tests/gui/meson.build`.
```

Mettre à jour dans ce fichier la liste des tests unitaires et d'intégration (ajouter `test_history`, `test_rx_transform`, `test_device_match`, `test_view_render`, `test_link` ; `test_transport` ne dépend plus des bouchons ; `stub_display.c` remplacé par `stub_link.c`).

- [ ] **Step 5 : revue de code**

Dans `docs/superpowers/reviews/2026-10-06-revue-code.md`, annexe « Remarques mineures reportées par le sous-projet 1 », marquer comme traitées par le sous-projet 2 : `set_tap_func` appelé depuis le thread Lua ; journal dupliqué au rejeu (suppression de `logging_set_suspended`) ; écho journalisé via `put_hexadecimal`. Ajouter sous les défauts M1 à M6 et M37 la mention « Corrigé par le sous-projet 2 (`feature/refonte-transport`) ».

- [ ] **Step 6 : critères d'acceptation**

Run :

```bash
grep -nE '#include *[<"](gtk/|vte/|interface\.h|terminal_display\.h|buffer\.h)' src/transport.c src/history.c src/rx_transform.c src/device_match.c src/view_render.c src/link.c
grep -nE 'show_message|Set_status_message|Set_window_title' src/transport.c src/link.c
ls src/serial.c src/buffer.c src/device_monitor.c 2>&1
git diff master -- src/cmdline.c | grep -E '^[-+].*(long_options|\{ *"[a-z-]+", *(no|required|optional)_argument)'
```

Expected : les deux premiers `grep` sans sortie ; `ls` répond « No such file or directory » pour les trois ; le dernier `grep` sans sortie (aucune option de ligne de commande modifiée).

Run : `meson test -C build --print-errorlogs 2>&1 | tail -4`
Expected : `Fail: 0`.

Run (construction instrumentée, réutilisée si elle existe) :

```bash
test -d build-asan || meson setup build-asan -Db_sanitize=address,undefined
meson test -C build-asan --print-errorlogs 2>&1 | tail -4
```

Expected : `Fail: 0`.

Run : `meson test -C build --suite gui --print-errorlogs 2>&1 | tail -4`
Expected : `Ok: 10`, `Fail: 0`.

- [ ] **Step 7 : commit**

```bash
git add po/POTFILES.in po/fr.po CLAUDE.md tests/README.md docs/superpowers/reviews/2026-10-06-revue-code.md
git commit -m "docs : architecture en couches, suite graphique, traductions françaises des textes d'état de la liaison

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
