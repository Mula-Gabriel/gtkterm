# Onglets (sous-projet 6b) — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan
> task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** un onglet = une instance complète et indépendante de GTKTerm (liaison, profil, options, terminal, macros et leur fichier, moteur Lua, journal,
envoi de fichier, barres), présentée dans un `GtkNotebook` de la fenêtre ; les actions `app.*` visent l'onglet actif.

**Architecture :** deux phases dans une seule branche, la suite verte à chaque tâche. Phase A (tâches 1 à 5, aucun changement visible) : relais d'action
`GtActionRelay` (cœur), macros et moteur Lua en instances (`GtMacros`, `GtScriptEngine`), puis `GtSession` + `GtSessionManager` (couche application) qui
remplacent la liaison par défaut, le profil global `config` et les états statiques ; les actions d'onglet passent dans le groupe d'actions de la session et
sont exposées en `app.*` par des relais. Phase B (tâches 6 à 10) : page d'onglet (`session_view.c`) et fenêtre à `GtkNotebook`, macros et script par page,
actions d'onglets, scénarios d'indépendance, documentation.

**Tech Stack :** C (gnu11), GLib/GObject/GIO, GTK 4.14, VTE 0.76 (gtk4), GtkSourceView 5.12, Lua 5.4, gudev, Meson/Ninja, GLib GTest, Python 3 (suite graphique :
`gtk4-broadwayd`, D-Bus `org.gtk.Actions`, crochet de test).

**Spec :** `docs/superpowers/specs/2026-10-10-refonte-onglets-design.md` (autorité en cas de doute ; les écarts assumés par ce plan sont listés dans « Décisions »).

## Global Constraints

- Français pour les commentaires, messages de commit et documents ; identifiants de code en anglais ; msgids de l'interface en anglais (traduits dans `po/fr.po`).
- Lignes de 200 colonnes au plus, jamais repliées à 80.
- Style : celui du fichier modifié (`macros.c`, `macros_format.c` : GNU 2 espaces ; `script_engine.c` : 4 espaces ; le reste : tabulations, accolades Allman,
  `if(` sans espace) ; nouveaux fichiers en tabulations/Allman (`.clang-format` du dépôt). Ne jamais reformater un fichier entier.
- **Sécurité absolue** : jamais de programme GTK sur l'écran réel (session Wayland) — tout lancement graphique passe par `meson test -C build --suite gui`
  (broadway privé, bus de session privé sans services), **jamais deux suites graphiques en même temps** ; jamais `~/.config/.gtktermrc`, `~/.gtktermrc`,
  `~/.config/gtkterm_macros.ini`, `~/.cache`, `~/.local` ; jamais de vrai port série (`/dev/ttyS*`, `ttyUSB*`, `ttyACM*`, `/dev/serial`) : `openpty` et
  `127.0.0.1` seulement.
- Versions minimales : GTK **4.14**, VTE **0.76**, GtkSourceView **5.12** ; `src/ui/` compilé avec `ui_c_args` (`GDK_VERSION_MAX_ALLOWED=GDK_VERSION_4_14`,
  `-Werror=deprecated-declarations`) : vérifier le « Since » de toute nouvelle fonction GTK/VTE/GtkSourceView.
- **Aucun en-tête GTK** dans le cœur (`core_sources`) ni dans la couche application (`app_layer_sources`, bibliothèque `gtkterm_app`) ;
  `grep -rn 'gtk/gtk.h' src/*.c src/*.h` doit rester vide.
- **Aucune variable statique ne porte d'état d'onglet** (spec §4.1). Statiques permises : magasin et `settings` (`app_config.c`), file des messages
  (`dialogs.c`), fil principal et file des demandes de `run_on_main_sync` (`script_engine.c`), table `GtAppUi`, instance unique des dialogues modaux Port et
  Terminal et des fenêtres À propos / Updates, l'état de l'application de `src/ui/app.c` (gestionnaire, fenêtre, relais, sources SIGUSR).
- Actions : les noms `app.*` de 6a sont **tous** conservés avec le même type de paramètre et d'état (tableau « Actions » ci-dessous) ; nouvelles actions de
  la tâche 8 seulement.
- Pas de boucle imbriquée ; règles de réentrance de `link.h` inchangées ; les rappels asynchrones retrouvent leur cible par pointeur faible ou vérifient
  qu'elle n'est pas arrêtée.
- `po/POTFILES.in` : chaque tâche y ajoute ses fichiers à chaînes traduisibles.
- Commandes : `ninja -C build` ; test ciblé `meson test -C build <nom> --print-errorlogs` ; avant chaque commit `meson test -C build --print-errorlogs` et
  `meson test -C build-asan --print-errorlogs` (instabilité tolérée : `test_link` `/perte-puis-reconnexion` sous ASan, une relance) ; toute tâche qui touche
  `src/ui/`, `src/link.c`, `src/app_config.c`, `src/cmdline.c` ou `tests/gui/` : `meson test -C build --suite gui` (poste Arch, GTK 4.24 : `test_port_dialog`
  et `test_polling` peuvent être **sautés** (SKIP, code 77) quand `gtk4-broadwayd` plante sur ses propres assertions ; un SKIP n'est pas un échec, un FAIL l'est).
  Si `build-asan` manque : `meson setup build-asan -Db_sanitize=address,undefined`.
- Un hook relance `ninja -C build` après chaque modification de `src/*.c|*.h` ; `jq` n'est pas installé (python3 pour le JSON).
- Branche `feature/refonte-onglets` déjà extraite : ne pas en changer, ne pas pousser.
- Chaque commit se termine par exactement : `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- TDD : test d'abord, échec constaté (RED), puis code (GREEN). Les scénarios graphiques sont les tests des tâches d'interface.

## Décisions prises par ce plan (interprétations de la spec)

1. **Journal et envoi de fichier** : le `GtRxLogger` et le `GtFileSender` restent dans le module d'interface `file_actions.c` (déjà le cas en 6a), rendu
   **par page** ; la session ne possède pas le journal (amende la spec §4.2). Pour la confirmation de fermeture, `file_actions.c` tient la propriété
   `file-sending` de sa session (`gt_session_set_file_sending()`), et `gt_session_is_busy()` = script en cours ou `file-sending`.
2. **Phase de transition** : les tâches 2 et 3 rendent `GtMacros` et `GtScriptEngine` instanciables avant que `GtSession` existe ; l'interface y crée une
   instance unique (dans `main.c` / `app.c`) jusqu'à la tâche 4, qui les range dans la première session. Les crochets reçoivent un `GtSession *` dès la
   tâche 2 (type opaque déclaré dans `app_ui.h`), NULL pendant la transition ; l'interface l'ignore jusqu'à la tâche 6.
3. **Ordre d'arrêt d'une session** (spec §3.2-11) : (a) la page : `gt_session_view_shutdown()` — envoi de fichier et journal (`file_actions`), panneau de
   macros, raccourcis, barre d'état détachée, actions de page retirées du groupe ; (b) la session : `gt_session_shutdown()` — moteur Lua, macros
   (ordonnanceur), liaison, puis signal `closed`. Un crochet qui vise une session dont la page est arrêtée ne fait rien.
4. **Groupe d'actions de session** : la session (couche application) y met : `local-echo`, `crlf-auto`, `timestamp`, `esc-clear-screen`, `hex-index`,
   `view-mode`, `hex-bytes` (`GPropertyAction` sur ses `GtTermOptions`), `auto-reconnect` (`GPropertyAction` sur sa liaison), `clear-screen`, `open-port`,
   `close-port`, `send-break`, `toggle-dtr`, `toggle-rts`. La page y ajoute : `clear-scrollback`, `copy`, `paste`, `select-all`, `find`, `show-hex-bar`,
   `show-macro-panel`, `show-script-panel`, `script-layout`, `send-file`, `save-raw`, `save-ascii`, `log-start`, `log-paused`, `log-stop`, `log-clear`. Restent
   des actions d'application (sans relais), qui lisent la session active au moment de l'activation : `quit`, `about`, `updates`, `port`, `terminal`, `macros`,
   `load-profile`, `save-profile`, `delete-profile`, `load-macros`, `save-macros`, et celles de la tâche 8 (`new-tab`, `close-tab`, `next-tab`,
   `previous-tab`, `active-tab`), plus les `test-*`.
5. **Ordre des onglets** : l'interface est maîtresse de l'ordre du `GtkNotebook` ; `GtSessionManager` le suit (ajout, retrait, `page-reordered` →
   `gt_session_manager_move`). L'interface n'écoute pas `items-changed` du modèle (pas de boucle de synchronisation) ; le modèle sert au crochet de test,
   aux actions `next-tab`/`previous-tab`/`active-tab` et aux arrêts dans l'ordre.
6. **Bouton de fermeture d'un onglet** : appelle `gt_app_close_session(session)` (fonction de `app.c`, même chemin que `app.close-tab` pour la session active),
   pas une action à paramètre.
7. **New tab** : `gt_port_dialog_show_new()` (mode création) prérempli d'une copie du profil actif ; OK → `gt_app_add_session(profil, nom de profil)` :
   session créée, son fichier de macros chargé (non interactif), page insérée à droite de l'onglet actif, activée, profil appliqué, port ouvert.
8. **`app.active-tab`** : action à état `i` (index de l'onglet actif dans l'ordre du notebook) ; `SetState(i)` active l'onglet `i` (hors bornes : ignoré).
9. **Crochet `app.test-sessions`** : état `aa{sv}` recalculé à chaque `Describe` (comme `test-dump`) : `index` (i), `id` (u), `name` (s, nom court), `state`
   (s, pseudo de `GtLinkState` : `closed`, `connecting`, `open`, `listening`, `lost`, `reconnecting`), `profile` (s), `macros_file` (s, vide si aucun),
   `active` (b), `busy` (b).
10. **Harnais** : `Env.new_pty()` rend une pty supplémentaire (objet `Pty` : `port`, `read(durée)`, `drain()`, `write(octets)`), fermée avec l'Env ;
    `Env.sessions()` lit `test-sessions` ; `open_tab(env, port)` (module `gtktest`) passe par le vrai dialogue Port. `find`/`find1` ignorent déjà les widgets
    non mappés (paramètre `mapped=True`, 6a) : un nom stable désigne celui de l'onglet affiché.
11. **Taille par défaut** : 1000 × 600 (`WINDOW_DEFAULT_WIDTH/HEIGHT` de `app_config.c`) ; les scénarios qui écrivent leur `[window]` ne changent pas.
12. **Couleurs du panneau de script** (`[colors]`, communes) : le dialogue « Colors… » d'une page enregistre puis rafraîchit les panneaux de **toutes** les
    pages (`gt_app_foreach_view`).
13. **Raccourcis de macros** : un `GtkShortcutController` par page, portée `GTK_SHORTCUT_SCOPE_LOCAL`, phase de capture, ajouté à la page : il ne voit que les
    touches dont la cible est dans la page affichée.

## Review Focus

1. **Aucune fuite d'un onglet vers l'autre** : historique, échos, options, macros (envoi, polling), port 0 Lua, journal. Épinglé par `test_session`
   (tâche 4) et `tests/gui/test_onglets_*.py` (tâches 8 et 9).
2. **Relais fidèles** : après un changement d'onglet, `Describe` de chaque action d'onglet donne l'état et la sensibilité de l'onglet actif, et un menu coché
   suit. Épinglé par `test_action_relay` (tâche 1) et `test_onglets_actions.py` (tâche 8).
3. **Fermeture d'un onglet occupé** (script, envoi, polling, dialogue ouvert sur cette session) : pas d'accès à la session arrêtée, pas d'avertissement, le
   reste continue. Épinglé par `test_onglets_fermeture.py` (tâche 8) et `test_onglets_lua.py` (tâche 9).
4. **Deux moteurs Lua en parallèle** : chacun son thread, son journal, son arrêt ; `run_on_main_sync` sert les deux. Épinglé par `test_script_engine`
   `/moteur/deux-en-parallele` et `/moteur/arret-independant` (tâche 3).

## Carte des fichiers

| Fichier | Tâche | Rôle |
|---|---|---|
| `src/action_relay.c/.h` (cœur) | 1 | `GtActionRelay` : `GAction` relais vers un `GActionGroup` cible |
| `tests/test_action_relay.c` | 1 | tests du relais |
| `src/macros.c/.h` (application) | 2 | `GtMacros` : jeu, chemin, ordonnanceur, liaison cible, propriétaire |
| `src/app_ui.c/.h` (cœur) | 2 | crochets avec `GtSession *` |
| `tests/test_macros_send.c`, `tests/stubs/stub_link.c`, `tests/stubs/stubs.h` | 2 | instances ; bouchon de liaison par instance |
| `src/script_engine.c/.h` (application) | 3 | `GtScriptEngine` : un moteur par onglet |
| `tests/test_script_engine.c`, `tests/stubs/stub_macros.c/.h` | 3 | moteurs en instances, deux en parallèle |
| `src/session.c/.h`, `src/session_manager.c/.h` (application) | 4 | `GtSession`, `GtSessionManager` |
| `src/link.c/.h` | 4 | liaison par défaut supprimée |
| `src/app_config.c/.h`, `src/cmdline.c/.h` | 4 | profil par session, plus de `config` global |
| `tests/test_session.c`, `tests/test_session_manager.c`, `tests/test_app_config.c`, `tests/test_cmdline.c` | 4 | tests de la couche application |
| `src/ui/main.c`, `src/ui/app.c/.h` et modules qui lisaient `config` ou la liaison par défaut | 4 | une session, aucun changement visible |
| `src/session.c` (actions), `src/ui/app.c` (relais), `src/ui/app_actions.c/.h` | 5 | actions d'onglet dans le groupe de session, `app.*` par relais |
| `src/ui/session_view.c/.h` | 6 | page d'onglet |
| `src/ui/main_window.c/.h` | 6 | barre de menus + `GtkNotebook` |
| `src/ui/status_bar.c`, `search_bar.c`, `hex_send_bar.c`, `file_actions.c`, `app_actions.c`, `dialogs.c`, `test_hooks.c` | 6 | par page, sans statique d'onglet |
| `src/ui/macro_panel.c`, `macro_shortcuts.c`, `macro_editor.c`, `script_panel.c` | 7 | par page / par session, sans statique d'onglet |
| `src/ui/tab_label.c/.h` | 8 | étiquette d'onglet |
| `src/ui/app.c`, `src/ui/menus.ui`, `src/ui/port_dialog.c/.h`, `terminal_dialog.c`, `profile_dialogs.c`, `src/app_config.c` | 8 | onglets, dialogues liés à leur session, taille par défaut |
| `tests/gui/gtktest.py`, `tests/gui/test_onglets*.py`, `tests/gui/meson.build` | 8, 9 | harnais et scénarios d'onglets |
| `CLAUDE.md`, `tests/README.md`, `docs/superpowers/reviews/2026-10-06-revue-code.md`, `po/` | 10 | documentation et traductions |

## Actions (noms exacts, inchangés depuis 6a sauf la tâche 8)

| Action | Paramètre | État | Porteur à partir de la tâche 5 |
|---|---|---|---|
| `local-echo`, `crlf-auto`, `timestamp`, `esc-clear-screen`, `hex-index`, `auto-reconnect` | — | `b` | session (relais `app.*`) |
| `view-mode` | `s` | `s` | session (relais) |
| `hex-bytes` | `u` | `u` | session (relais) |
| `clear-screen`, `open-port`, `close-port`, `send-break`, `toggle-dtr`, `toggle-rts` | — | — | session (relais) |
| `clear-scrollback`, `copy`, `paste`, `select-all`, `find`, `send-file`, `save-raw`, `save-ascii`, `log-start`, `log-stop`, `log-clear` | — | — | page (relais) |
| `log-paused`, `show-hex-bar`, `show-macro-panel`, `show-script-panel` | — | `b` | page (relais) |
| `script-layout` | `s` | `s` | page (relais) |
| `quit`, `about`, `updates`, `port`, `terminal`, `macros`, `load-profile`, `save-profile`, `delete-profile`, `load-macros`, `save-macros` | — | — | application |
| `new-tab`, `close-tab`, `next-tab`, `previous-tab` (tâche 8) | — | — | application |
| `active-tab` (tâche 8) | `i` | `i` | application |

Le type de paramètre et d'état de chaque action d'onglet est relevé par `test_menus_actions.py` avant la tâche 5 (étape 5.1) et vérifié identique après.

## Interfaces partagées (valeurs exactes, utilisées par plusieurs tâches)

### `src/action_relay.h` (tâche 1)

```c
/* Relais d'action (sous-projet 6b, spec §4.4) : GAction qui délègue à l'action de même nom d'un GActionGroup cible, changeable.
 * Nom, type de paramètre et type d'état sont fixés à la création (pris sur l'action du groupe modèle). « enabled » et « state » reflètent
 * l'action cible et sont notifiés seulement s'ils changent ; sans cible ou sans l'action dans la cible : désactivé, dernier état gardé.
 * activate et change_state ne font rien quand le relais est désactivé. Fil principal seulement. */
#ifndef ACTION_RELAY_H_
#define ACTION_RELAY_H_

#include <gio/gio.h>

G_BEGIN_DECLS

#define GT_TYPE_ACTION_RELAY (gt_action_relay_get_type())
G_DECLARE_FINAL_TYPE(GtActionRelay, gt_action_relay, GT, ACTION_RELAY, GObject)

/* NULL si model n'a pas d'action name ; sinon relais ciblant model */
GtActionRelay *gt_action_relay_new(GActionGroup *model, const gchar *name);
void           gt_action_relay_set_target(GtActionRelay *relay, GActionGroup *target);	/* NULL : aucune cible */
GActionGroup  *gt_action_relay_get_target(GtActionRelay *relay);

G_END_DECLS

#endif
```

### `src/app_ui.h` (tâche 2)

```c
/* Effets d'interface demandés par la couche application (sous-projet 6a, spec §4.3 ; session ajoutée par 6b, spec §3.2-4). Chaque interface remplit la
 * table une fois au démarrage ; table vide = aucun effet (tests), sauf message qui écrit alors sur stderr. Fil principal seulement ; aucun crochet ne
 * doit lancer de boucle imbriquée. session : l'onglet concerné (NULL : aucun, tests ou transition). */
#ifndef APP_UI_H_
#define APP_UI_H_

#include <glib.h>

typedef struct _GtSession GtSession;	/* session.h (couche application) ; opaque ici */

typedef enum { GT_APP_MSG_INFO, GT_APP_MSG_WARNING, GT_APP_MSG_ERROR } GtAppMessageKind;

typedef struct
{
	void (*message)(GtAppMessageKind kind, const gchar *text);	/* boîte non bloquante sur la fenêtre ; NULL : g_printerr */
	void (*status_flash)(GtSession *session, const gchar *text, guint ms);	/* barre d'état de la page de session ; NULL : rien */
	void (*macros_changed)(GtSession *session);	/* jeu de la session remplacé ou modifié : son panneau et ses raccourcis à reconstruire */
	void (*macro_polling_changed)(GtSession *session, guint id, gboolean running);
	void (*profile_applied)(GtSession *session);	/* profil chargé dans la session : sa page réapplique terminal et couleurs, son port est rouvert */
} GtAppUi;

void           gt_app_ui_set(const GtAppUi *ui);	/* copie la table ; NULL = table vide */
const GtAppUi *gt_app_ui_get(void);	/* jamais NULL */
void           gt_app_message(GtAppMessageKind kind, const gchar *fmt, ...) G_GNUC_PRINTF(2, 3);	/* formate puis crochet ou stderr */
void           gt_app_status_flash(GtSession *session, guint ms, const gchar *fmt, ...) G_GNUC_PRINTF(3, 4);

#endif
```

### `src/macros.h` (tâche 2)

```c
/* État des macros d'un onglet (sous-projet 4, instances en 6b) : jeu, chemin du fichier, ordonnanceur de polling sur la liaison de l'onglet. Sans GTK ;
 * fil principal seulement. Effets d'interface par les crochets GtAppUi (macros_changed, macro_polling_changed, status_flash, message), avec owner. */
#ifndef MACROS_H_
#define MACROS_H_

#include <glib.h>

#include "app_ui.h"
#include "link.h"
#include "macro_set.h"
#include "macro_poller.h"

typedef struct _GtMacros GtMacros;

/* Jeu vide, aucun fichier ; envois et polling sur link (référence gardée) ; owner : session passée aux crochets (non possédée, NULL permis) */
GtMacros      *gt_macros_new             (GtkTermLink *link, GtSession *owner);
/* Fermeture : polling arrêté sans rappel d'état, ordonnanceur libéré ; ensuite gt_macros_send rend FALSE sans rien envoyer. Idempotente. */
void           gt_macros_shutdown        (GtMacros *m);
void           gt_macros_free            (GtMacros *m);	/* shutdown puis libération ; NULL accepté */
GtSession     *gt_macros_get_owner       (GtMacros *m);
GtkTermLink   *gt_macros_get_link        (GtMacros *m);
GtMacroSet    *gt_macros_get_set         (GtMacros *m);
GtMacroPoller *gt_macros_get_poller      (GtMacros *m);	/* NULL après shutdown */
/* Prend le jeu en charge, arrête tout polling, puis crochet macros_changed(owner). Une fenêtre qui édite l'ancien jeu le reconnaît en comparant
 * gt_macros_get_set() au jeu qu'elle édite, et se ferme sans enregistrer. */
void           gt_macros_replace_set     (GtMacros *m, GtMacroSet *set);
/* Envoi de macro de l'interface (clic, raccourci, polling) sur la liaison de m ; n_args < 0 : arguments enregistrés. Macro inconnue ou non envoyable,
 * ou m arrêté : FALSE, rien n'est envoyé. TRUE si tous les octets ont été écrits. Au moins un octet écrit : « Macro "…" sent! » (status_flash(owner)). */
gboolean       gt_macros_send            (GtMacros *m, guint id, const gchar *const *args, gint n_args);
gboolean       gt_macros_send_quiet      (GtMacros *m, guint id, const gchar *const *args, gint n_args);	/* moteur Lua : sans message */
/* NULL : chemin courant (ou défaut). Contrat de macros_load_file de 6a (interactif ou non, fichier absent, illisible). */
gboolean       gt_macros_load_file       (GtMacros *m, const gchar *path, gboolean interactive);
gboolean       gt_macros_save_file       (GtMacros *m, const gchar *path, gboolean interactive);	/* NULL : chemin courant (ou défaut) */
void           gt_macros_forget_save_error(GtMacros *m);
const gchar   *gt_macros_get_path        (GtMacros *m);	/* NULL : aucun fichier chargé ni enregistré */
gchar         *macros_file_default_path  (void);
void           gt_macros_changed         (GtMacros *m);	/* crochet macros_changed(owner) */

#endif
```

### `src/script_engine.h` (tâche 3)

```c
#ifndef SCRIPT_ENGINE_H_
#define SCRIPT_ENGINE_H_

#include <glib.h>

#include "link.h"
#include "macros.h"

/* Moteur Lua (sous-projet 5) ; un moteur par onglet (6b). Toutes ces fonctions s'appellent depuis le thread principal. Le contrat d'arrêt, de journal
 * et de fermeture de 6a/SP5 vaut pour chaque moteur ; les moteurs sont indépendants (thread, ports 1-15, files, journal, drapeaux). */
typedef struct _GtScriptEngine GtScriptEngine;

void            script_engine_setup(void);	/* une fois, au démarrage : note le thread principal (commun à tous les moteurs) */
/* Port 0 = main_link (référence gardée) ; send_macro, get_macros, get_lists → macros (non possédé, doit vivre plus longtemps que le moteur) */
GtScriptEngine *gt_script_engine_new(GtkTermLink *main_link, GtMacros *macros);
void            gt_script_engine_run(GtScriptEngine *e, const char *script_text);
void            gt_script_engine_stop(GtScriptEngine *e);	/* contrat de script_engine_stop (6a) */
gboolean        gt_script_engine_is_running(GtScriptEngine *e);
/* Contrat de script_engine_shutdown (6a), pour ce moteur : fonctions de journal et de fin retirées, script arrêté, demandes de run_on_main_sync exécutées
 * par le thread principal jusqu'à la fin du thread Lua, 3 s au plus. Idempotente ; ensuite gt_script_engine_run ne fait rien. */
void            gt_script_engine_shutdown(GtScriptEngine *e);
void            gt_script_engine_free(GtScriptEngine *e);	/* shutdown puis libération ; NULL accepté */

/* Texte du journal, en UTF-8 valide, par lots (plusieurs lignes à la fois). */
typedef void (*ScriptLogFunc)(const char *text, gpointer user_data);
void            gt_script_engine_set_log_func(GtScriptEngine *e, ScriptLogFunc func, gpointer user_data);

typedef enum
{
	GT_SCRIPT_END_FINISHED,	/* fin normale, os.exit(), os.exit(true) ou os.exit(0) */
	GT_SCRIPT_END_ERROR,	/* erreur Lua ou os.exit non nul : le message est journalisé avant */
	GT_SCRIPT_END_STOPPED	/* « Stop », fermeture de l'onglet ou de GTKTerm */
} GtScriptEnd;

/* Appelée sur le thread principal après le vidage du journal, le thread Lua joint. */
typedef void (*ScriptDoneFunc)(GtScriptEnd end, gpointer user_data);
void            gt_script_engine_set_done_func(GtScriptEngine *e, ScriptDoneFunc func, gpointer user_data);

#endif
```

### `src/session.h` (tâche 4)

```c
/* Session d'onglet (sous-projet 6b, spec §4.2) : tout l'état d'une instance de GTKTerm présentée dans un onglet. Couche application, sans GTK ;
 * fil principal seulement. */
#ifndef SESSION_H_
#define SESSION_H_

#include <gio/gio.h>

#include "link.h"
#include "macros.h"
#include "profile.h"
#include "script_engine.h"
#include "term_options.h"

G_BEGIN_DECLS

#define GT_TYPE_SESSION (gt_session_get_type())
G_DECLARE_FINAL_TYPE(GtSession, gt_session, GT, SESSION, GObject)

/* Session fermée : liaison, options, profil (copie profonde de profile, ou défauts si NULL ; lock_port compris), nom de profil (NULL : « default »),
 * macros (jeu vide, aucun fichier), moteur Lua, groupe d'actions d'onglet. N'ouvre aucun port et ne charge aucun fichier. */
GtSession      *gt_session_new(const GtProfile *profile, const gchar *profile_name);
guint           gt_session_get_id(GtSession *s);	/* 1, 2, 3… dans l'ordre de création ; jamais réutilisé dans le processus */
GtkTermLink    *gt_session_get_link(GtSession *s);
GtTermOptions  *gt_session_get_options(GtSession *s);
GtProfile      *gt_session_get_profile(GtSession *s);	/* profil en cours de l'onglet (dialogues, ligne de commande, chargement) */
const gchar    *gt_session_get_profile_name(GtSession *s);	/* « default » si aucun */
void            gt_session_set_profile_name(GtSession *s, const gchar *name);	/* NULL : « default » */
GtMacros       *gt_session_get_macros(GtSession *s);
GtScriptEngine *gt_session_get_script_engine(GtSession *s);
GActionGroup   *gt_session_get_actions(GtSession *s);	/* en fait un GSimpleActionGroup (G_ACTION_MAP) : la page y ajoute les siennes */
const gchar    *gt_session_get_short_name(GtSession *s);	/* propriété « short-name » ; calculé par gt_session_short_name_for(profil) */
gchar          *gt_session_short_name_for(const GtProfile *p);	/* série : nom de base du port ; client TCP : « hôte:port » ; serveur : « :port » */
void            gt_session_set_file_sending(GtSession *s, gboolean sending);	/* tenu par l'envoi de fichier de la page */
gboolean        gt_session_is_busy(GtSession *s);	/* script en cours ou envoi de fichier en cours */
gboolean        gt_session_is_shut_down(GtSession *s);
/* Options ← profil (un seul « changed »), auto-reconnect de la liaison, nom court recalculé ; ne rouvre pas le port, n'appelle aucun crochet */
void            gt_session_apply_profile(GtSession *s);
void            gt_session_open_port(GtSession *s);	/* port du profil (nom court recalculé) ; échec : signal open-failed de la liaison */
void            gt_session_close_port(GtSession *s);
/* Arrêt ordonné (plan, décision 3, étape b) : moteur Lua, macros, actions désactivées, liaison fermée, puis signal « closed ». Idempotent ; la session
 * reste un objet valide mais inerte (ouvrir un port, envoyer une macro ou lancer un script ne fait plus rien). */
void            gt_session_shutdown(GtSession *s);

G_END_DECLS

#endif
```

### `src/session_manager.h` (tâche 4)

```c
/* Sessions ouvertes, dans l'ordre des onglets, et session active (sous-projet 6b, spec §4.3). Couche application ; fil principal seulement.
 * L'interface est maîtresse de l'ordre (plan, décision 5) : elle appelle add, remove et move ; le modèle n'est jamais modifié ailleurs. */
#ifndef SESSION_MANAGER_H_
#define SESSION_MANAGER_H_

#include <gio/gio.h>

#include "session.h"

G_BEGIN_DECLS

#define GT_TYPE_SESSION_MANAGER (gt_session_manager_get_type())
G_DECLARE_FINAL_TYPE(GtSessionManager, gt_session_manager, GT, SESSION_MANAGER, GObject)

GtSessionManager *gt_session_manager_new(void);
GListModel       *gt_session_manager_get_model(GtSessionManager *m);	/* GtSession, ordre des onglets */
guint             gt_session_manager_get_n(GtSessionManager *m);
GtSession        *gt_session_manager_get_nth(GtSessionManager *m, guint i);	/* empruntée ; NULL hors bornes */
gint              gt_session_manager_index_of(GtSessionManager *m, GtSession *s);	/* -1 si absente */
/* position < 0 ou au-delà : à la fin. Référence prise. La première session ajoutée devient active. */
void              gt_session_manager_add(GtSessionManager *m, GtSession *s, gint position);
/* Absente : rien. Active : l'active passe d'abord à la voisine de droite, sinon de gauche, sinon NULL ; puis retrait (référence rendue). */
void              gt_session_manager_remove(GtSessionManager *m, GtSession *s);
void              gt_session_manager_move(GtSessionManager *m, GtSession *s, guint position);	/* position bornée à n - 1 ; active inchangée */
GtSession        *gt_session_manager_get_active(GtSessionManager *m);	/* empruntée ; NULL seulement sans session */
void              gt_session_manager_set_active(GtSessionManager *m, GtSession *s);	/* s absente : ignoré ; propriété « active » notifiée si elle change */

G_END_DECLS

#endif
```

### `src/app_config.h` (tâche 4 — changements seulement)

```c
/* plus de « extern GtProfile config » ni de app_config_active_name() : le profil en cours et son nom sont ceux d'une session (session.h) */
gint     Check_configuration_file(GtSession *s);	/* [general] + profil « default » chargé dans s (contrat de 6a) */
gint     Load_configuration_from_file(GtSession *s, const gchar *name);	/* copie dans le profil de s (lock_port gardé), nom de profil de s, macros de s */
void     Verify_configuration(GtSession *s);	/* gt_profile_validate du profil de s + un seul gt_app_message */
gboolean app_config_load_profile(GtSession *s, const gchar *name, GError **error);	/* Load + Verify + crochet profile_applied(s) */
/* Options de s et auto-reconnect de sa liaison recopiés dans son profil (gt_term_options_store_profile), macros_file = chemin des macros de s,
 * puis relire, écrire, enregistrer ; nom de profil de s changé seulement après réussite */
gboolean app_config_save_profile(GtSession *s, const gchar *name, GError **error);
void     config_remember_macros_file(GtSession *s);	/* clé macros_file du profil actif de s seulement */
/* inchangés : config_file_init, config_get_store, config_reload_store, config_save_store, config_save_update_url, config_save_skipped_revision,
 * config_save_update_check_startup, app_config_delete_profile, app_config_list_profiles, app_config_has_profile, GtWindowState et ses deux fonctions,
 * GtkTermSettings settings */
```

### `src/cmdline.h` (tâche 4 — changement seulement)

```c
/* Analyse ; erreur ou aide : exit_now, rien d'autre n'est fait. Sinon charge dans s le profil (default, puis celui de -c), applique les autres options au
 * profil de s, quel que soit leur ordre, puis Verify_configuration(s). */
void read_command_line(GtSession *s, int argc, char **argv, GtCmdline *out);
```

### `src/ui/session_view.h` (tâche 6)

```c
/* Page d'onglet (sous-projet 6b, spec §4.6) : tout ce qui est sous la barre de menus pour une session. De haut en bas : emplacement « search-bar »,
 * GtkPaned vertical [GtkPaned horizontal (terminal | emplacement « macro-panel ») / emplacement « script-panel »], emplacements « hex-bar » et
 * « status-bar ». Ajoute au groupe d'actions de la session les actions de page (plan, décision 4). */
#ifndef GT_UI_SESSION_VIEW_H_
#define GT_UI_SESSION_VIEW_H_

#include <gtk/gtk.h>

#include "app_config.h"
#include "session.h"

G_BEGIN_DECLS

/* GtkBox verticale nommée « session-view » ; st : séparateurs et visibilités initiaux (h_paned, v_paned, *_visible ; les autres champs ignorés) */
/* layout_from : page dont le panneau de script donne sa disposition (nouvel onglet) ; NULL pour la première page ([layout] et [session] lus).
 * Paramètre ajouté à la tâche 7 (absent à la tâche 6). */
GtkWidget *gt_session_view_new(GtkApplication *app, GtSession *s, const GtWindowState *st, GtkWidget *layout_from);
GtSession *gt_session_view_get_session(GtkWidget *view);
GtkWidget *gt_session_view_lookup(GtSession *s);	/* page de la session, NULL si aucune ou arrêtée */
GtkWidget *gt_session_view_get_terminal_view(GtkWidget *view);
void       gt_session_view_focus_terminal(GtkWidget *view);
void       gt_session_view_set_slot(GtkWidget *view, const char *slot, GtkWidget *child);
GtkWidget *gt_session_view_get_slot(GtkWidget *view, const char *slot);
void       gt_session_view_get_state(GtkWidget *view, GtWindowState *out);	/* remplit h_paned, v_paned et les trois visibilités seulement */
void       gt_session_view_apply_profile(GtkWidget *view);	/* terminal ← profil de la session, couleurs du panneau de script */
void       gt_session_view_shutdown(GtkWidget *view);	/* plan, décision 3, étape a ; idempotente */
gboolean   gt_session_view_is_shut_down(GtkWidget *view);

G_END_DECLS

#endif
```

### `src/ui/app.h` (tâches 4, 6 et 8)

```c
GtkApplication   *gt_app_new(const GtCmdline *cmd, GtSessionManager *sessions);	/* tâche 4 : sessions possédé par main, vit jusqu'au retour de run */
GtSessionManager *gt_app_get_sessions(void);
GtSession        *gt_app_get_active_session(void);	/* NULL avant main ou après l'arrêt */
GtkWindow        *gt_app_get_main_window(void);	/* inchangé */
GtkWidget        *gt_app_get_active_view(void);	/* tâche 6 : page de la session active, NULL sans fenêtre */
void              gt_app_foreach_view(GFunc func, gpointer user_data);	/* tâche 6 : chaque page vivante, dans l'ordre des onglets */
GtSession        *gt_app_add_session(const GtProfile *profile, const gchar *profile_name);	/* tâche 8 : décision 7 du plan */
void              gt_app_close_session(GtSession *s);	/* tâche 8 : confirmation si occupée, puis page et session arrêtées, onglet retiré */
/* supprimés (tâche 4) : gt_app_get_link, gt_app_get_options, gt_app_open_port, gt_app_close_port, gt_app_apply_profile → fonctions de session */
```

## Tâches

### Task 1 : relais d'action `GtActionRelay` (`Core`)

**Files :**
- Create : `src/action_relay.h`, `src/action_relay.c`, `tests/test_action_relay.c`
- Modify : `src/meson.build` (`core_sources` : `'action_relay.c', 'action_relay.h'`), `tests/meson.build` (`unit_tests` : `'test_action_relay' : [gio_dep]`)

- [ ] **Step 1 : écrire le test** `tests/test_action_relay.c` :

```c
/* Tests du relais d'action (src/action_relay.c, sous-projet 6b) : délégation, reflet de l'état et de la sensibilité, changement de cible. */
#include <gio/gio.h>

#include "action_relay.h"
#include "term_options.h"

static void on_notify(GObject *obj, GParamSpec *pspec, gpointer user_data)
{
	(*(guint *)user_data)++;
}

static gulong watch(gpointer obj, const gchar *prop, guint *counter)
{
	gchar *signal = g_strconcat("notify::", prop, NULL);
	gulong id = g_signal_connect(obj, signal, G_CALLBACK(on_notify), counter);

	g_free(signal);
	return id;
}

static GSimpleActionGroup *group_with_bool(const gchar *name, gboolean value)
{
	GSimpleActionGroup *g = g_simple_action_group_new();
	GSimpleAction *a = g_simple_action_new_stateful(name, NULL, g_variant_new_boolean(value));

	g_action_map_add_action(G_ACTION_MAP(g), G_ACTION(a));
	g_object_unref(a);
	return g;
}

static gboolean state_bool(GAction *a)
{
	GVariant *v = g_action_get_state(a);
	gboolean b = g_variant_get_boolean(v);

	g_variant_unref(v);
	return b;
}

static void on_activate_count(GSimpleAction *a, GVariant *param, gpointer user_data)
{
	(*(guint *)user_data)++;
}

static void test_types(void)
{
	GSimpleActionGroup *g = group_with_bool("opt", TRUE);
	GtActionRelay *r = gt_action_relay_new(G_ACTION_GROUP(g), "opt");

	g_assert_nonnull(r);
	g_assert_cmpstr(g_action_get_name(G_ACTION(r)), ==, "opt");
	g_assert_null(g_action_get_parameter_type(G_ACTION(r)));
	g_assert_true(g_variant_type_equal(g_action_get_state_type(G_ACTION(r)), G_VARIANT_TYPE_BOOLEAN));
	g_assert_true(g_action_get_enabled(G_ACTION(r)));
	g_assert_true(state_bool(G_ACTION(r)));
	g_assert_true(gt_action_relay_get_target(r) == G_ACTION_GROUP(g));
	g_assert_null(gt_action_relay_new(G_ACTION_GROUP(g), "absente"));
	g_object_unref(r);
	g_object_unref(g);
}

static void test_activate(void)
{
	GSimpleActionGroup *g = g_simple_action_group_new();
	GSimpleAction *a = g_simple_action_new("go", NULL);
	guint n = 0;
	GtActionRelay *r;

	g_signal_connect(a, "activate", G_CALLBACK(on_activate_count), &n);
	g_action_map_add_action(G_ACTION_MAP(g), G_ACTION(a));
	r = gt_action_relay_new(G_ACTION_GROUP(g), "go");
	g_assert_null(g_action_get_state_type(G_ACTION(r)));
	g_assert_null(g_action_get_state(G_ACTION(r)));
	g_action_activate(G_ACTION(r), NULL);
	g_assert_cmpuint(n, ==, 1);
	/* Action cible désactivée : le relais l'est aussi et n'active rien */
	g_simple_action_set_enabled(a, FALSE);
	g_assert_false(g_action_get_enabled(G_ACTION(r)));
	g_action_activate(G_ACTION(r), NULL);
	g_assert_cmpuint(n, ==, 1);
	g_simple_action_set_enabled(a, TRUE);
	g_assert_true(g_action_get_enabled(G_ACTION(r)));
	g_object_unref(r);
	g_object_unref(a);
	g_object_unref(g);
}

static void test_change_state(void)
{
	GSimpleActionGroup *g = group_with_bool("opt", TRUE);
	GtActionRelay *r = gt_action_relay_new(G_ACTION_GROUP(g), "opt");
	guint n = 0;

	watch(r, "state", &n);
	g_action_change_state(G_ACTION(r), g_variant_new_boolean(FALSE));
	g_assert_false(state_bool(G_ACTION(g_action_map_lookup_action(G_ACTION_MAP(g), "opt"))));
	g_assert_false(state_bool(G_ACTION(r)));
	g_assert_cmpuint(n, ==, 1);
	/* La cible change seule : le relais suit */
	g_simple_action_set_state(G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(g), "opt")), g_variant_new_boolean(TRUE));
	g_assert_true(state_bool(G_ACTION(r)));
	g_assert_cmpuint(n, ==, 2);
	/* Même valeur : pas de notification */
	g_simple_action_set_state(G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(g), "opt")), g_variant_new_boolean(TRUE));
	g_assert_cmpuint(n, ==, 2);
	g_object_unref(r);
	g_object_unref(g);
}

static void test_retarget(void)
{
	GSimpleActionGroup *a = group_with_bool("opt", TRUE), *b = group_with_bool("opt", FALSE), *c = group_with_bool("opt", FALSE);
	GtActionRelay *r = gt_action_relay_new(G_ACTION_GROUP(a), "opt");
	guint n_state = 0, n_enabled = 0;

	watch(r, "state", &n_state);
	watch(r, "enabled", &n_enabled);
	gt_action_relay_set_target(r, G_ACTION_GROUP(b));
	g_assert_false(state_bool(G_ACTION(r)));
	g_assert_cmpuint(n_state, ==, 1);
	gt_action_relay_set_target(r, G_ACTION_GROUP(b));	/* même cible : rien */
	gt_action_relay_set_target(r, G_ACTION_GROUP(c));	/* même état : rien */
	g_assert_cmpuint(n_state, ==, 1);
	g_assert_cmpuint(n_enabled, ==, 0);
	/* L'ancienne cible ne fait plus bouger le relais */
	g_simple_action_set_state(G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(a), "opt")), g_variant_new_boolean(FALSE));
	g_simple_action_set_state(G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(b), "opt")), g_variant_new_boolean(TRUE));
	g_assert_false(state_bool(G_ACTION(r)));
	g_assert_cmpuint(n_state, ==, 1);
	/* Sans cible : désactivé, dernier état gardé */
	gt_action_relay_set_target(r, NULL);
	g_assert_false(g_action_get_enabled(G_ACTION(r)));
	g_assert_cmpuint(n_enabled, ==, 1);
	g_assert_false(state_bool(G_ACTION(r)));
	g_object_unref(r);
	g_object_unref(a);
	g_object_unref(b);
	g_object_unref(c);
}

static void test_missing_then_added(void)
{
	GSimpleActionGroup *model = group_with_bool("opt", TRUE), *empty = g_simple_action_group_new();
	GtActionRelay *r = gt_action_relay_new(G_ACTION_GROUP(model), "opt");
	GSimpleAction *later;

	gt_action_relay_set_target(r, G_ACTION_GROUP(empty));
	g_assert_false(g_action_get_enabled(G_ACTION(r)));
	g_assert_true(state_bool(G_ACTION(r)));	/* dernier état gardé */
	later = g_simple_action_new_stateful("opt", NULL, g_variant_new_boolean(FALSE));
	g_action_map_add_action(G_ACTION_MAP(empty), G_ACTION(later));
	g_assert_true(g_action_get_enabled(G_ACTION(r)));
	g_assert_false(state_bool(G_ACTION(r)));
	g_action_map_remove_action(G_ACTION_MAP(empty), "opt");
	g_assert_false(g_action_get_enabled(G_ACTION(r)));
	g_object_unref(later);
	g_object_unref(r);
	g_object_unref(model);
	g_object_unref(empty);
}

static void on_group_state(GActionGroup *g, const gchar *name, GVariant *value, gpointer user_data)
{
	(*(guint *)user_data)++;
}

/* Le relais rangé dans un groupe (l'application) : le groupe signale les changements d'état, comme l'exportateur D-Bus les voit */
static void test_in_map(void)
{
	GSimpleActionGroup *target = group_with_bool("opt", FALSE), *app = g_simple_action_group_new();
	GtActionRelay *r = gt_action_relay_new(G_ACTION_GROUP(target), "opt");
	guint n = 0;

	g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(r));
	g_signal_connect(app, "action-state-changed::opt", G_CALLBACK(on_group_state), &n);
	g_simple_action_set_state(G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(target), "opt")), g_variant_new_boolean(TRUE));
	g_assert_cmpuint(n, ==, 1);
	g_action_group_change_action_state(G_ACTION_GROUP(app), "opt", g_variant_new_boolean(FALSE));
	g_assert_false(state_bool(g_action_map_lookup_action(G_ACTION_MAP(target), "opt")));
	g_assert_cmpuint(n, ==, 2);
	g_object_unref(r);
	g_object_unref(app);
	g_object_unref(target);
}

/* GPropertyAction (actions d'options de la session) derrière le relais */
static void test_property_action(void)
{
	GtTermOptions *o = gt_term_options_new();
	GSimpleActionGroup *g = g_simple_action_group_new();
	GPropertyAction *pa = g_property_action_new("local-echo", o, "echo");
	GtActionRelay *r;
	gboolean echo = FALSE;

	g_action_map_add_action(G_ACTION_MAP(g), G_ACTION(pa));
	r = gt_action_relay_new(G_ACTION_GROUP(g), "local-echo");
	g_action_change_state(G_ACTION(r), g_variant_new_boolean(TRUE));
	g_object_get(o, "echo", &echo, NULL);
	g_assert_true(echo);
	g_object_set(o, "echo", FALSE, NULL);
	g_assert_false(state_bool(G_ACTION(r)));
	g_object_unref(r);
	g_object_unref(pa);
	g_object_unref(g);
	g_object_unref(o);
}

int main(int argc, char *argv[])
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/relais/types", test_types);
	g_test_add_func("/relais/activation", test_activate);
	g_test_add_func("/relais/changement-d-etat", test_change_state);
	g_test_add_func("/relais/changement-de-cible", test_retarget);
	g_test_add_func("/relais/action-absente-puis-ajoutee", test_missing_then_added);
	g_test_add_func("/relais/dans-un-groupe", test_in_map);
	g_test_add_func("/relais/action-de-propriete", test_property_action);
	return g_test_run();
}
```

- [ ] **Step 2 : RED** — ajouter `'test_action_relay' : [gio_dep]` à `unit_tests` et `action_relay.c/.h` à `core_sources` (fichiers vides avec l'en-tête de
  « Interfaces partagées » et un `.c` qui n'inclut que l'en-tête) ; `ninja -C build` échoue à l'édition de liens (`gt_action_relay_new` non défini). Noter
  l'erreur.

- [ ] **Step 3 : écrire `src/action_relay.c`** :

```c
/* Relais d'action (sous-projet 6b, spec §4.4) : voir action_relay.h. */
#include "action_relay.h"

struct _GtActionRelay
{
	GObject parent_instance;
	gchar *name;
	GVariantType *parameter_type;	/* NULL : sans paramètre */
	GVariantType *state_type;	/* NULL : sans état */
	GVariant *state;	/* dernier état connu (référence pleine) ; NULL : sans état */
	gboolean enabled;
	GActionGroup *target;	/* référence ; NULL : aucune */
	gulong added_id, removed_id, enabled_id, state_id;
};

enum
{
	PROP_0,
	PROP_NAME,
	PROP_PARAMETER_TYPE,
	PROP_ENABLED,
	PROP_STATE_TYPE,
	PROP_STATE
};

static void action_iface_init(GActionInterface *iface);

G_DEFINE_TYPE_WITH_CODE(GtActionRelay, gt_action_relay, G_TYPE_OBJECT, G_IMPLEMENT_INTERFACE(G_TYPE_ACTION, action_iface_init))

static void set_enabled(GtActionRelay *r, gboolean enabled)
{
	enabled = enabled != FALSE;
	if(r->enabled == enabled)
		return;
	r->enabled = enabled;
	g_object_notify(G_OBJECT(r), "enabled");
}

/* state : référence empruntée (flottante ou non) ; ignoré s'il n'est pas du type d'état du relais */
static void set_state(GtActionRelay *r, GVariant *state)
{
	if(state == NULL || r->state_type == NULL || !g_variant_is_of_type(state, r->state_type))
		return;
	if(r->state != NULL && g_variant_equal(r->state, state))
		return;
	g_clear_pointer(&r->state, g_variant_unref);
	r->state = g_variant_ref_sink(state);
	g_object_notify(G_OBJECT(r), "state");
}

static void sync_from_target(GtActionRelay *r)
{
	gboolean enabled = FALSE;
	GVariant *state = NULL;

	if(r->target != NULL && g_action_group_query_action(r->target, r->name, &enabled, NULL, NULL, NULL, &state))
	{
		if(state != NULL)
		{
			set_state(r, state);
			g_variant_unref(state);
		}
		set_enabled(r, enabled);
	}
	else
		set_enabled(r, FALSE);
}

static void on_added_or_removed(GActionGroup *group, const gchar *name, gpointer user_data)
{
	sync_from_target(GT_ACTION_RELAY(user_data));
}

static void on_enabled_changed(GActionGroup *group, const gchar *name, gboolean enabled, gpointer user_data)
{
	set_enabled(GT_ACTION_RELAY(user_data), enabled);
}

static void on_state_changed(GActionGroup *group, const gchar *name, GVariant *value, gpointer user_data)
{
	set_state(GT_ACTION_RELAY(user_data), value);
}

static gulong connect_detailed(GtActionRelay *r, const gchar *signal, GCallback cb)
{
	gchar *detailed = g_strconcat(signal, "::", r->name, NULL);
	gulong id = g_signal_connect(r->target, detailed, cb, r);

	g_free(detailed);
	return id;
}

static void disconnect_target(GtActionRelay *r)
{
	if(r->target == NULL)
		return;
	g_clear_signal_handler(&r->added_id, r->target);
	g_clear_signal_handler(&r->removed_id, r->target);
	g_clear_signal_handler(&r->enabled_id, r->target);
	g_clear_signal_handler(&r->state_id, r->target);
	g_clear_object(&r->target);
}

void gt_action_relay_set_target(GtActionRelay *r, GActionGroup *target)
{
	g_return_if_fail(GT_IS_ACTION_RELAY(r));
	g_return_if_fail(target == NULL || G_IS_ACTION_GROUP(target));

	if(target == r->target)
		return;
	disconnect_target(r);
	if(target != NULL)
	{
		r->target = g_object_ref(target);
		r->added_id = connect_detailed(r, "action-added", G_CALLBACK(on_added_or_removed));
		r->removed_id = connect_detailed(r, "action-removed", G_CALLBACK(on_added_or_removed));
		r->enabled_id = connect_detailed(r, "action-enabled-changed", G_CALLBACK(on_enabled_changed));
		r->state_id = connect_detailed(r, "action-state-changed", G_CALLBACK(on_state_changed));
	}
	sync_from_target(r);
}

GActionGroup *gt_action_relay_get_target(GtActionRelay *r)
{
	g_return_val_if_fail(GT_IS_ACTION_RELAY(r), NULL);
	return r->target;
}

GtActionRelay *gt_action_relay_new(GActionGroup *model, const gchar *name)
{
	const GVariantType *ptype = NULL, *stype = NULL;
	GtActionRelay *r;

	g_return_val_if_fail(G_IS_ACTION_GROUP(model) && name != NULL, NULL);
	if(!g_action_group_query_action(model, name, NULL, &ptype, &stype, NULL, NULL))
		return NULL;
	r = g_object_new(GT_TYPE_ACTION_RELAY, NULL);
	r->name = g_strdup(name);
	r->parameter_type = ptype != NULL ? g_variant_type_copy(ptype) : NULL;
	r->state_type = stype != NULL ? g_variant_type_copy(stype) : NULL;
	gt_action_relay_set_target(r, model);
	return r;
}

/* ---- GAction ---- */

static const gchar *relay_get_name(GAction *a)
{
	return GT_ACTION_RELAY(a)->name;
}

static const GVariantType *relay_get_parameter_type(GAction *a)
{
	return GT_ACTION_RELAY(a)->parameter_type;
}

static const GVariantType *relay_get_state_type(GAction *a)
{
	return GT_ACTION_RELAY(a)->state_type;
}

static GVariant *relay_get_state_hint(GAction *a)
{
	GtActionRelay *r = GT_ACTION_RELAY(a);
	GVariant *hint = NULL;

	if(r->target != NULL)
		g_action_group_query_action(r->target, r->name, NULL, NULL, NULL, &hint, NULL);
	return hint;
}

static gboolean relay_get_enabled(GAction *a)
{
	return GT_ACTION_RELAY(a)->enabled;
}

static GVariant *relay_get_state(GAction *a)
{
	GtActionRelay *r = GT_ACTION_RELAY(a);

	return r->state != NULL ? g_variant_ref(r->state) : NULL;
}

static void relay_change_state(GAction *a, GVariant *value)
{
	GtActionRelay *r = GT_ACTION_RELAY(a);

	if(r->enabled && r->target != NULL)
		g_action_group_change_action_state(r->target, r->name, value);
	else if(value != NULL && g_variant_is_floating(value))
		g_variant_unref(g_variant_ref_sink(value));	/* valeur flottante non consommée : libérée */
}

static void relay_activate(GAction *a, GVariant *param)
{
	GtActionRelay *r = GT_ACTION_RELAY(a);

	if(r->enabled && r->target != NULL)
		g_action_group_activate_action(r->target, r->name, param);
	else if(param != NULL && g_variant_is_floating(param))
		g_variant_unref(g_variant_ref_sink(param));
}

static void action_iface_init(GActionInterface *iface)
{
	iface->get_name = relay_get_name;
	iface->get_parameter_type = relay_get_parameter_type;
	iface->get_state_type = relay_get_state_type;
	iface->get_state_hint = relay_get_state_hint;
	iface->get_enabled = relay_get_enabled;
	iface->get_state = relay_get_state;
	iface->change_state = relay_change_state;
	iface->activate = relay_activate;
}

/* ---- GObject ---- */

static void gt_action_relay_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GtActionRelay *r = GT_ACTION_RELAY(object);

	switch(prop_id)
	{
	case PROP_NAME:
		g_value_set_string(value, r->name);
		break;
	case PROP_PARAMETER_TYPE:
		g_value_set_boxed(value, r->parameter_type);
		break;
	case PROP_ENABLED:
		g_value_set_boolean(value, r->enabled);
		break;
	case PROP_STATE_TYPE:
		g_value_set_boxed(value, r->state_type);
		break;
	case PROP_STATE:
		g_value_set_variant(value, r->state);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}

static void gt_action_relay_dispose(GObject *object)
{
	disconnect_target(GT_ACTION_RELAY(object));
	G_OBJECT_CLASS(gt_action_relay_parent_class)->dispose(object);
}

static void gt_action_relay_finalize(GObject *object)
{
	GtActionRelay *r = GT_ACTION_RELAY(object);

	g_free(r->name);
	g_clear_pointer(&r->parameter_type, g_variant_type_free);
	g_clear_pointer(&r->state_type, g_variant_type_free);
	g_clear_pointer(&r->state, g_variant_unref);
	G_OBJECT_CLASS(gt_action_relay_parent_class)->finalize(object);
}

static void gt_action_relay_class_init(GtActionRelayClass *klass)
{
	GObjectClass *oc = G_OBJECT_CLASS(klass);

	oc->get_property = gt_action_relay_get_property;
	oc->dispose = gt_action_relay_dispose;
	oc->finalize = gt_action_relay_finalize;
	g_object_class_override_property(oc, PROP_NAME, "name");
	g_object_class_override_property(oc, PROP_PARAMETER_TYPE, "parameter-type");
	g_object_class_override_property(oc, PROP_ENABLED, "enabled");
	g_object_class_override_property(oc, PROP_STATE_TYPE, "state-type");
	g_object_class_override_property(oc, PROP_STATE, "state");
}

static void gt_action_relay_init(GtActionRelay *r)
{
}
```

- [ ] **Step 4 : GREEN** — `meson test -C build test_action_relay --print-errorlogs` : 7 cas OK. Puis `meson test -C build` et `meson test -C build-asan`
  (aucune fuite signalée par LSan sur `test_action_relay`).
- [ ] **Step 5 : commit** — `git add src/action_relay.c src/action_relay.h tests/test_action_relay.c src/meson.build tests/meson.build` ;
  message : `cœur : relais d'action GtActionRelay (actions app.* qui visent l'onglet actif)` + ligne `Co-Authored-By` exacte.

---

### Task 2 : macros en instances (`GtMacros`) et crochets avec session (`Refactor`)

**Files :**
- Modify : `src/app_ui.h`, `src/app_ui.c`, `src/macros.h`, `src/macros.c`, `src/app_config.c`, `src/script_engine.c` (appels de macros seulement),
  `src/ui/main.c`, `src/ui/app.c`, `src/ui/dialogs.c`, `src/ui/status_bar.c/.h`, `src/ui/macro_panel.c`, `src/ui/macro_shortcuts.c`,
  `src/ui/macro_editor.c`, `src/ui/profile_dialogs.c`, `src/ui/app_actions.c`, `src/ui/test_hooks.c` (tout appelant de `macros_*` ou de `gt_app_status_flash`)
- Modify (tests) : `tests/test_macros_send.c`, `tests/stubs/stub_link.c`, `tests/stubs/stubs.h`, `tests/stubs/stub_macros_file.c`, `tests/stubs/stub_macros.c/.h`,
  `tests/test_app_config.c`, `tests/test_cmdline.c` (seulement ce que le changement d'API impose)

**Règles de conversion (`src/macros.c`, style GNU 2 espaces conservé) :**
- Les quatre statiques (`current_set`, `poller`, `current_path`, `last_save_error`) deviennent les champs de `struct _GtMacros`, avec `GtkTermLink *link`
  (référence), `GtSession *owner` (non possédé) et `gboolean shut_down`.
- `gt_macros_new` crée le jeu vide et l'ordonnanceur `gt_macro_poller_new(set, poller_send, poller_state, m)` ; `poller_send(id, m)` →
  `gt_macros_send(m, id, NULL, -1)` ; `poller_state(id, running, m)` → crochet `macro_polling_changed(m->owner, id, running)`.
- `send_macro(m, …)` : `FALSE` si `m->shut_down` ; envoi par `gt_link_send(m->link, …)` ; message par `gt_app_status_flash(m->owner, 800, …)`.
- `gt_macros_shutdown` = l'ancien `macros_shutdown` (`gt_macro_poller_free`, poller NULL, `shut_down = TRUE`) ; `gt_macros_free` : shutdown, jeu libéré,
  chemins libérés, `g_object_unref(link)`, `g_free(m)`.
- `gt_macros_replace_set` n'appelle plus `macros_init()` ; après shutdown, il remplace le jeu sans ordonnanceur (pas de crash) et appelle quand même le crochet.
- `gt_macros_changed(m)` → crochet `macros_changed(m->owner)`. `load_error_idle` garde son texte (message global, pas de session).
- Aucune variable `static` ne doit rester dans `macros.c`.

**Crochets (`src/app_ui.h/.c`)** : texte exact de « Interfaces partagées » ; `gt_app_status_flash(session, ms, fmt, …)` appelle
`table.status_flash(session, text, ms)`.

**Transition dans l'interface (jusqu'à la tâche 4)** : `src/ui/main.c` crée `GtMacros *macros = gt_macros_new(link, NULL)` juste après la liaison, à la place
de `macros_init()` ; une fonction d'accès temporaire `GtMacros *gt_app_get_macros(void)` (déclarée dans `src/ui/app.h`, valeur posée par
`gt_app_set_macros()` appelée dans `main()` avant `read_command_line`) sert aux modules d'interface ; `app_config.c` utilise une fonction de transition
`void app_config_set_macros(GtMacros *m)` (statique de fichier `transition_macros`, **supprimée à la tâche 4** avec un commentaire `/* transition 6b, retirée
en tâche 4 */`). Les crochets de l'interface (`dialogs.c`) prennent le nouveau prototype et ignorent `session` (barre d'état unique, panneau unique).
`main()` libère par `gt_macros_free(macros)` après `g_application_run` (au lieu de `macros_shutdown`), et `app_stop_services()` appelle
`gt_macros_shutdown(gt_app_get_macros())` à la place de `macros_shutdown()`.

**Bouchon de liaison par instance (`tests/stubs/stub_link.c`)** : les octets envoyés, l'origine, l'échec d'écriture et l'état deviennent des champs de
`struct _GtkTermLink` ; ajouter `GtkTermLink *gt_link_new(void)` et :

```c
/* Liaison (stub_link.c) : chaque GtkTermLink du bouchon enregistre ses envois. Les fonctions sans argument visent stub_link_default(). */
GtkTermLink *stub_link_default(void);	/* créée au premier appel, jamais libérée */
GByteArray  *stub_link_sent(GtkTermLink *link);
void         stub_link_inject_rx(GtkTermLink *link, const gchar *data, guint len);
```

`stub_sent()`, `stub_send_thread()`, `stub_last_origin()`, `stub_inject_rx()`, `stub_link_set_write_fails()`, `stub_link_set_state()` gardent leur sens
sur `stub_link_default()` ; `gt_link_get_default()` du bouchon rend `stub_link_default()` jusqu'à la tâche 4 (où il disparaît).

- [ ] **Step 1 : test d'indépendance (RED)** — dans `tests/test_macros_send.c`, remplacer `macros_init()` par une instance globale de fichier
  `static GtMacros *macros;` créée dans `main()` par `macros = gt_macros_new(stub_link_default(), NULL);`, remplacer chaque `macros_send(` par
  `gt_macros_send(macros, `, `macros_send_quiet(` par `gt_macros_send_quiet(macros, `, `macros_get_set()` par `gt_macros_get_set(macros)`,
  `macros_load_file(` par `gt_macros_load_file(macros, `, `macros_save_file(` par `gt_macros_save_file(macros, ` ; puis ajouter :

```c
/* Deux instances sur deux liaisons : chaque envoi et chaque polling part sur la liaison de son instance (spec §3.1-2) */
static void test_deux_instances(void)
{
	GtkTermLink *l1 = gt_link_new(), *l2 = gt_link_new();
	GtMacros *m1 = gt_macros_new(l1, NULL), *m2 = gt_macros_new(l2, NULL);
	GtMacroSet *s1 = gt_macro_set_new(), *s2 = gt_macro_set_new();
	guint id1 = gt_macro_set_append(s1, "Un", NULL, "UN", NULL);
	guint id2 = gt_macro_set_append(s2, "Deux", NULL, "DEUX", NULL);
	GMainContext *ctx = g_main_context_default();
	gint64 fin;

	gt_macros_replace_set(m1, s1);
	gt_macros_replace_set(m2, s2);
	g_assert_true(gt_macros_send(m1, id1, NULL, -1));
	g_assert_true(gt_macros_send(m2, id2, NULL, -1));
	g_assert_cmpmem(stub_link_sent(l1)->data, stub_link_sent(l1)->len, "UN", 2);
	g_assert_cmpmem(stub_link_sent(l2)->data, stub_link_sent(l2)->len, "DEUX", 4);
	/* L'identifiant de m1 n'existe pas dans m2 : rien n'est envoyé sur l2 */
	g_assert_false(gt_macros_send(m2, id1 + 1000, NULL, -1));
	/* Polling de m1 toutes les 10 ms : seul l1 reçoit */
	g_byte_array_set_size(stub_link_sent(l1), 0);
	g_byte_array_set_size(stub_link_sent(l2), 0);
	g_assert_true(gt_macro_set_set_polling(gt_macros_get_set(m1), id1, TRUE, 10));
	gt_macro_poller_start(gt_macros_get_poller(m1), id1);
	fin = g_get_monotonic_time() + 100 * G_TIME_SPAN_MILLISECOND;
	while(g_get_monotonic_time() < fin)
		g_main_context_iteration(ctx, FALSE);
	g_assert_cmpuint(stub_link_sent(l1)->len, >=, 4);
	g_assert_cmpuint(stub_link_sent(l2)->len, ==, 0);
	/* Après shutdown : plus rien n'est envoyé */
	gt_macros_shutdown(m1);
	g_assert_false(gt_macros_send(m1, id1, NULL, -1));
	gt_macros_free(m1);
	gt_macros_free(m2);
	g_object_unref(l1);
	g_object_unref(l2);
}
```

  et l'enregistrer : `g_test_add_func("/macros_send/deux-instances", test_deux_instances);`. Vérifier d'abord les noms réels de l'API du jeu et de
  l'ordonnanceur (`grep -n '^[a-z].*(' src/macro_set.h src/macro_poller.h`) : si `gt_macro_set_append` ou `gt_macro_poller_start` s'appellent autrement,
  utiliser les vrais noms (le test 6a `/macros_send/arguments-enregistres` montre comment le jeu est construit) — le comportement vérifié ne change pas.
  `ninja -C build` échoue (API inconnue) : RED constaté.
- [ ] **Step 2 : GREEN côté application** — `macros.h/.c` et `app_ui.h/.c` selon les règles ; bouchons adaptés ; `meson test -C build test_macros_send` OK.
- [ ] **Step 3 : appelants** — adapter tous les appelants
  (`grep -rn 'macros_\(init\|get_set\|get_poller\|shutdown\|send\|send_quiet\|load_file\|save_file\|forget_save_error\|file_get_path\|replace_set\|changed\)\b\|gt_app_status_flash' src tests`)
  selon la transition ci-dessus ; `ninja -C build` sans avertissement.
- [ ] **Step 4 : vérifications** — `meson test -C build`, `meson test -C build-asan`, `meson test -C build --suite gui` (aucun changement visible attendu).
  `grep -n '^static [A-Za-z_ ]*\*\?[a-z_]* *[=;]' src/macros.c` : vide.
- [ ] **Step 5 : commit** — message : `macros : GtMacros en instances, crochets GtAppUi avec la session (transition)`.

---

### Task 3 : moteur Lua en instances (`GtScriptEngine`) (`Refactor`)

**Files :**
- Modify : `src/script_engine.h` (texte de « Interfaces partagées »), `src/script_engine.c`, `src/ui/script_panel.c`, `src/ui/app.c`, `src/ui/main.c`
- Modify (tests) : `tests/test_script_engine.c`, `tests/stubs/stub_macros.c`, `tests/stubs/stub_macros.h`

**Règles de conversion (`src/script_engine.c`, style 4 espaces conservé) :**
1. `struct _GtScriptEngine` reçoit tous les anciens états de moteur : `ports[]`, `overflow_reported[]`, `main_queue`, `lua_thread`, `engine_mutex`,
   `stop_requested`, `exit_requested`, `exit_status`, `pause_mutex`, `pause_cond`, `log_func`, `log_data`, `done_func`, `done_data`, `log_mutex`,
   `log_pending`, `log_dropped`, `log_source_id`, `main_port_handler`, `done_source_id`, `thread_finished`, plus `GtkTermLink *main_link` (référence),
   `GtMacros *macros`, `gboolean shut_down`. Les `GMutex`/`GCond` sont initialisés dans `gt_script_engine_new` et nettoyés dans `gt_script_engine_free`.
2. Restent des statiques de fichier (communes, un seul thread principal) : `main_thread`, `calls_mutex`, `calls_cond`, `pending_calls`, `stop_sentinel`
   (adresse constante), `gtkterm_funcs[]`. Chaque `MainCall` porte `GtScriptEngine *engine` ; `thread_finished` devient `engine->thread_finished`
   (toujours sous `calls_mutex`).
3. Depuis l'état Lua : `static GtScriptEngine *engine_of(lua_State *L) { return *(GtScriptEngine **)lua_getextraspace(L); }`, posé juste après
   `luaL_newstate()` (`*(GtScriptEngine **)lua_getextraspace(L) = e;`). Toute fonction `l_*`, `api_guard`, `std_guard*`, `stop_hook`, `l_os_exit`,
   `l_print`, `panic_cb` obtient son moteur par `engine_of(L)` ; les fonctions internes (`pause_interruptible`, `queue_for`, `report_overflow`,
   `drain_output`, `lua_port_close`, `wait_connected`, `send_all`, `log_append_len`, `engine_log_line`, `log_flush`, …) prennent `GtScriptEngine *e` en
   premier paramètre.
4. Les structures de données des rappels du thread principal (`MainTrySend`, ouverture, fermeture, instantanés de macros et de listes, envoi de macro,
   état de la liaison, connexion du port 0) reçoivent un champ `GtScriptEngine *e` ; `main_port_connect_cb`/`main_port_disconnect_cb`/`main_link_state_cb`
   utilisent `e->main_link`, jamais `gt_link_get_default()`. `lua_main_port_received` reçoit le moteur en `user_data` et pousse dans `e->main_queue`.
5. Macros : `main_snapshot_macros_cb`/`main_snapshot_lists_cb` lisent `gt_macros_get_set(e->macros)` ; `main_send_macro_cb` appelle
   `gt_macros_send_quiet(e->macros, …)`.
6. Journal : la source de vidage reçoit `e` (`g_timeout_add_full(…, log_flush_cb, e, NULL)`).
7. `gt_script_engine_shutdown(e)` reprend l'ancien `script_engine_shutdown()` mot pour mot pour ce moteur : il attend `e->thread_finished` en exécutant
   **toutes** les demandes de `pending_calls` (quel que soit leur moteur : ce sont des demandes légitimes du thread principal), au plus 3 s, puis joint
   le thread ; `e->shut_down = TRUE` ; `gt_script_engine_run` ne fait plus rien ensuite. Fermer un moteur ne touche jamais aux ports, aux files ni au
   drapeau d'arrêt d'un autre.
8. `script_engine_setup()` reste une fonction globale (note `main_thread` une fois ; appels suivants sans effet).

- [ ] **Step 1 : tests (RED)** — dans `tests/test_script_engine.c` : instance de fichier `static GtScriptEngine *engine;` créée dans `main()` par
  `engine = gt_script_engine_new(stub_link_default(), stub_macros_default());` après `script_engine_setup()` ; remplacements mécaniques
  `script_engine_run(` → `gt_script_engine_run(engine, `, `script_engine_stop()` → `gt_script_engine_stop(engine)`, `script_engine_is_running()` →
  `gt_script_engine_is_running(engine)`, `script_engine_shutdown()` → `gt_script_engine_shutdown(engine)`, `script_engine_set_log_func(` →
  `gt_script_engine_set_log_func(engine, `, `script_engine_set_done_func(` → `gt_script_engine_set_done_func(engine, ` ; un cas qui teste l'arrêt définitif
  (shutdown) recrée ensuite `engine` pour les cas suivants. Le bouchon `stub_macros` devient une instance : `GtMacros *stub_macros_default(void);` et
  `GtMacros *stub_macros_new(void);` (chaque instance garde le jeu fourni par `stub_macros_set_set(GtMacros *, GtMacroSet *)` et note ses appels
  « gt_macros_send_quiet » dans le journal du bouchon) ; il définit `gt_macros_get_set` et `gt_macros_send_quiet` (les seules fonctions de macros appelées
  par le moteur). Puis ajouter (avec les aides existantes du fichier pour attendre la fin d'un script, à réutiliser telles quelles ; si leur nom diffère,
  adapter) :

```c
/* Deux moteurs, deux liaisons : chacun écrit sur son port 0 et finit sans toucher à l'autre (spec §3.1-3) */
static void test_deux_en_parallele(void)
{
	GtkTermLink *l1 = gt_link_new(), *l2 = gt_link_new();
	GtScriptEngine *e1 = gt_script_engine_new(l1, stub_macros_new()), *e2 = gt_script_engine_new(l2, stub_macros_new());
	EndWait w1 = { 0 }, w2 = { 0 };

	gt_script_engine_set_done_func(e1, end_wait_cb, &w1);
	gt_script_engine_set_done_func(e2, end_wait_cb, &w2);
	gt_script_engine_run(e1, "for i = 1, 20 do gtkterm.send(0, 'A') gtkterm.sleep(2) end");
	gt_script_engine_run(e2, "for i = 1, 20 do gtkterm.send(0, 'B') gtkterm.sleep(2) end");
	g_assert_true(gt_script_engine_is_running(e1) && gt_script_engine_is_running(e2));
	end_wait(&w1, 5000);
	end_wait(&w2, 5000);
	g_assert_cmpint(w1.end, ==, GT_SCRIPT_END_FINISHED);
	g_assert_cmpint(w2.end, ==, GT_SCRIPT_END_FINISHED);
	g_assert_cmpuint(stub_link_sent(l1)->len, ==, 20);
	g_assert_null(memchr(stub_link_sent(l1)->data, 'B', stub_link_sent(l1)->len));
	g_assert_cmpuint(stub_link_sent(l2)->len, ==, 20);
	g_assert_null(memchr(stub_link_sent(l2)->data, 'A', stub_link_sent(l2)->len));
	gt_script_engine_free(e1);
	gt_script_engine_free(e2);
	g_object_unref(l1);
	g_object_unref(l2);
}

/* Arrêter (ou fermer) un moteur ne touche pas l'autre ; chaque port 0 ne reçoit que sa liaison */
static void test_arret_independant(void)
{
	GtkTermLink *l1 = gt_link_new(), *l2 = gt_link_new();
	GtScriptEngine *e1 = gt_script_engine_new(l1, stub_macros_new()), *e2 = gt_script_engine_new(l2, stub_macros_new());
	EndWait w1 = { 0 }, w2 = { 0 };

	gt_script_engine_set_done_func(e1, end_wait_cb, &w1);
	gt_script_engine_set_done_func(e2, end_wait_cb, &w2);
	gt_script_engine_run(e1, "while true do end");
	gt_script_engine_run(e2, "local d = gtkterm.wait_for(0, 'X2', 3000) gtkterm.send(0, d and 'OK' or 'KO')");
	g_usleep(100 * G_TIME_SPAN_MILLISECOND);
	stub_link_inject_rx(l1, "X2", 2);	/* sur la liaison de e1 : e2 ne doit pas le voir */
	gt_script_engine_shutdown(e1);
	g_assert_false(gt_script_engine_is_running(e1));
	g_assert_true(gt_script_engine_is_running(e2));
	stub_link_inject_rx(l2, "X2", 2);
	end_wait(&w2, 5000);
	g_assert_cmpint(w2.end, ==, GT_SCRIPT_END_FINISHED);
	g_assert_cmpmem(stub_link_sent(l2)->data, stub_link_sent(l2)->len, "OK", 2);
	gt_script_engine_free(e1);
	gt_script_engine_free(e2);
	g_object_unref(l1);
	g_object_unref(l2);
}

/* Port série Lua tenu par un moteur : refusé à l'autre (verrou exclusif du transport) */
static void test_port_serie_exclusif(void)
{
	int master, slave;
	gchar *path, *script1, *script2;
	GtScriptEngine *e1 = gt_script_engine_new(stub_link_default(), stub_macros_new());
	GtScriptEngine *e2 = gt_script_engine_new(stub_link_default(), stub_macros_new());
	EndWait w1 = { 0 }, w2 = { 0 };
	GString *log2 = g_string_new(NULL);

	g_assert_cmpint(openpty(&master, &slave, NULL, NULL, NULL), ==, 0);
	path = g_strdup(ttyname(slave));
	script1 = g_strdup_printf("assert(gtkterm.open(1, {type='serial', port='%s', baud=9600})) gtkterm.sleep(1500)", path);
	script2 = g_strdup_printf("local ok, err = gtkterm.open(1, {type='serial', port='%s', baud=9600}) print(ok and 'OUVERT' or ('REFUS ' .. err))", path);
	gt_script_engine_set_done_func(e1, end_wait_cb, &w1);
	gt_script_engine_set_done_func(e2, end_wait_cb, &w2);
	gt_script_engine_set_log_func(e2, append_log_cb, log2);
	gt_script_engine_run(e1, script1);
	g_usleep(300 * G_TIME_SPAN_MILLISECOND);
	gt_script_engine_run(e2, script2);
	end_wait(&w2, 5000);
	g_assert_nonnull(strstr(log2->str, "REFUS"));
	end_wait(&w1, 5000);
	gt_script_engine_free(e1);
	gt_script_engine_free(e2);
	g_string_free(log2, TRUE);
	g_free(script1);
	g_free(script2);
	g_free(path);
	close(master);
	close(slave);
}
```

  `EndWait`, `end_wait_cb`, `end_wait` et `append_log_cb` : si le fichier n'a pas déjà ces aides, les écrire ainsi :

```c
typedef struct { gboolean done; GtScriptEnd end; } EndWait;
static void end_wait_cb(GtScriptEnd end, gpointer user_data) { EndWait *w = user_data; w->end = end; w->done = TRUE; }
static void end_wait(EndWait *w, guint timeout_ms)
{
	gint64 fin = g_get_monotonic_time() + (gint64)timeout_ms * G_TIME_SPAN_MILLISECOND;

	while(!w->done && g_get_monotonic_time() < fin)
		g_main_context_iteration(NULL, FALSE);
	g_assert_true(w->done);
}
static void append_log_cb(const char *text, gpointer user_data) { g_string_append((GString *)user_data, text); }
```

  Vérifier la syntaxe exacte de `gtkterm.open` dans les exemples (`scripts/*.lua`, `tests/test_script_engine.c`) et l'adapter si les clés diffèrent
  (`type`, `port`, `baud`) — le comportement vérifié (second `open` refusé avec une raison) ne change pas. Enregistrer :
  `/moteur/deux-en-parallele`, `/moteur/arret-independant`, `/moteur/port-serie-exclusif`. `ninja -C build` échoue : RED.
- [ ] **Step 2 : GREEN** — conversion de `script_engine.c` selon les règles ; `meson test -C build test_script_engine --print-errorlogs` : tous les cas
  existants et les trois nouveaux passent ; `meson test -C build-asan test_script_engine` aussi (ThreadSanitizer non requis).
- [ ] **Step 3 : interface (transition)** — `src/ui/main.c` crée le moteur après les macros : `GtScriptEngine *engine = gt_script_engine_new(link, macros);`
  posé par `gt_app_set_script_engine(engine)` (accès temporaire `gt_app_get_script_engine()` dans `app.h`, retiré à la tâche 4) ; `script_panel.c` utilise ce
  moteur (`gt_script_engine_set_log_func(engine, …)`, `…_set_done_func`, `…_run`, `…_stop`, `…_is_running`) ; `app_stop_services()` appelle
  `gt_script_engine_shutdown(gt_app_get_script_engine())` à la place de `script_engine_shutdown()` ; `main()` libère par `gt_script_engine_free` avant
  `gt_macros_free`. `script_engine_setup()` reste appelé une fois (au même endroit qu'en 6a).
- [ ] **Step 4 : vérifications** — `meson test -C build`, `meson test -C build-asan`, `meson test -C build --suite gui` (scénarios Lua : `test_script_arret`,
  `test_fermeture_script`, `test_fermeture_script_macro`, `test_script_couleurs` verts).
  `grep -nE '^static (GtScriptPort|gboolean|GtRxQueue|GThread|GMutex|gint|lua_Integer|GCond|ScriptLogFunc|ScriptDoneFunc|gpointer|GString|gsize|guint|gulong) ' src/script_engine.c`
  ne montre plus que `main_thread`, `calls_mutex`, `calls_cond`, `pending_calls` (et les fonctions).
- [ ] **Step 5 : commit** — message : `lua : un moteur GtScriptEngine par instance (thread, ports, journal, arrêt indépendants)`.

---

### Task 4 : sessions (`GtSession`, `GtSessionManager`), plus de liaison par défaut ni de profil global (`Refactor`)

**Files :**
- Create : `src/session.h`, `src/session.c`, `src/session_manager.h`, `src/session_manager.c`, `tests/test_session.c`, `tests/test_session_manager.c`
- Modify : `src/meson.build` (`app_layer_sources` : `session.c/.h`, `session_manager.c/.h`), `src/link.c/.h` (retrait de `gt_link_set_default`,
  `gt_link_get_default`, `default_link`), `src/app_config.c/.h`, `src/cmdline.c/.h`, `src/ui/main.c`, `src/ui/app.c/.h`, et chaque module d'interface qui lit
  `config`, `gt_app_get_link()`, `gt_app_get_options()`, `gt_app_get_macros()`, `gt_app_get_script_engine()`, `gt_app_open_port()`, `gt_app_close_port()`,
  `gt_app_apply_profile()`, `app_config_active_name()` (`grep -rn` sur ces noms : `app_actions.c`, `file_actions.c`, `terminal_dialog.c`, `port_dialog.c`,
  `profile_dialogs.c`, `script_panel.c`, `status_bar.c`, `hex_send_bar.c`, `macro_panel.c`, `macro_shortcuts.c`, `macro_editor.c`, `dialogs.c`, `test_hooks.c`)
- Modify (tests) : `tests/meson.build`, `tests/test_app_config.c`, `tests/test_cmdline.c`, `tests/test_link.c` (si `gt_link_set_default` y sert),
  `tests/stubs/stub_link.c` (retrait de `gt_link_get_default`), `tests/stubs/stub_macros_file.c` (supprimé si plus utilisé)

**`src/session.c` (couche application, tabulations/Allman) — contenu exigé :**

```c
/* Session d'onglet (sous-projet 6b, spec §4.2) : voir session.h. */
#include <config.h>
#include <glib/gi18n.h>

#include "app_ui.h"
#include "session.h"

struct _GtSession
{
	GObject parent_instance;
	guint id;
	GtkTermLink *link;
	GtTermOptions *options;
	GtProfile profile;
	gchar *profile_name;	/* NULL : « default » */
	GtMacros *macros;
	GtScriptEngine *engine;
	GSimpleActionGroup *actions;
	gchar *short_name;
	gboolean file_sending;
	gboolean shut_down;
};

enum { PROP_0, PROP_SHORT_NAME, PROP_STATUS_TEXT, N_PROPS };
enum { SIGNAL_CLOSED, N_SIGNALS };
static GParamSpec *props[N_PROPS];
static guint signals[N_SIGNALS];
static guint next_id = 1;	/* identifiants jamais réutilisés (compteur du processus, pas un état d'onglet) */

G_DEFINE_TYPE(GtSession, gt_session, G_TYPE_OBJECT)

gchar *gt_session_short_name_for(const GtProfile *p)
{
	switch(p->transport)
	{
	case TRANSPORT_TCP_CLIENT:
		return g_strdup_printf("%s:%s", p->host != NULL ? p->host : "localhost", p->tcp_port != NULL ? p->tcp_port : "");
	case TRANSPORT_TCP_SERVER:
		return g_strdup_printf(":%s", p->tcp_port != NULL ? p->tcp_port : "");
	default:
		return p->port != NULL && p->port[0] != '\0' ? g_path_get_basename(p->port) : g_strdup("?");
	}
}

static void update_short_name(GtSession *s)
{
	gchar *name = gt_session_short_name_for(&s->profile);

	if(g_strcmp0(name, s->short_name) == 0)
	{
		g_free(name);
		return;
	}
	g_free(s->short_name);
	s->short_name = name;
	g_object_notify_by_pspec(G_OBJECT(s), props[PROP_SHORT_NAME]);
}

/* notify::status-text de la liaison (peut tourner sous la surveillance du transport : ne fait que relayer la notification) */
static void on_link_status(GObject *link, GParamSpec *pspec, gpointer user_data)
{
	g_object_notify_by_pspec(G_OBJECT(user_data), props[PROP_STATUS_TEXT]);
}
```

  Puis : `gt_session_new` (id = `next_id++`, `gt_link_new()`, `gt_term_options_new()`, `gt_profile_init_defaults` ou `gt_profile_copy`, nom de profil
  copié, `gt_macros_new(link, s)`, `gt_script_engine_new(link, macros)`, `g_simple_action_group_new()`, nom court calculé, connexion
  `notify::status-text` de la liaison) ; accesseurs ; `gt_session_set_profile_name` ; `gt_session_set_file_sending` ;
  `gt_session_is_busy` = `!shut_down && (gt_script_engine_is_running(engine) || file_sending)` ; `gt_session_apply_profile` =
  `gt_term_options_apply_profile(options, &profile)` + `g_object_set(link, "auto-reconnect", profile.auto_reconnect, NULL)` + `update_short_name` (sans
  effet si arrêtée) ; `gt_session_open_port` = `update_short_name` + `gt_profile_to_transport_params` + `gt_link_open(link, &params, NULL)` +
  `transport_params_clear` (sans effet si arrêtée) ; `gt_session_close_port` = `gt_link_close` ; `gt_session_shutdown` = si déjà fait rien, sinon
  `shut_down = TRUE`, `gt_script_engine_shutdown(engine)`, `gt_macros_shutdown(macros)`, chaque action du groupe désactivée
  (`g_action_group_list_actions` puis `g_simple_action_set_enabled(…, FALSE)` sur les `GSimpleAction` ; les `GPropertyAction` sont retirées du groupe),
  `gt_link_close(link)`, `g_signal_emit(s, signals[SIGNAL_CLOSED], 0)` ; `dispose` : `gt_session_shutdown`, déconnexion de la liaison, `g_clear_object` du
  groupe ; `finalize` : `gt_script_engine_free(engine)` **avant** `gt_macros_free(macros)` (le moteur emprunte les macros), `g_object_unref(link)`,
  `g_object_unref(options)`, `gt_profile_clear(&profile)`, chaînes libérées. Propriétés : `short-name` (chaîne, lecture), `status-text` (chaîne, lecture,
  `gt_link_get_status_text(link)`) ; signal `closed` (sans argument, `G_SIGNAL_RUN_LAST`).

**`src/session_manager.c`** : `GListStore *sessions` (type `GT_TYPE_SESSION`), `GtSession *active` (empruntée : le magasin tient la référence) ; propriété
`active` (objet, lecture-écriture, `G_PARAM_EXPLICIT_NOTIFY`) ; toutes les fonctions de l'en-tête, contrats exacts de « Interfaces partagées » ; `remove` :
calcule la voisine (droite, sinon gauche), `set_active(voisine)` (NULL s'il n'y en a pas), puis `g_list_store_remove` ; `move` : retire et réinsère à
`MIN(position, n - 1)` en gardant une référence pendant l'opération, sans changer l'active.

**`src/app_config.c`** : supprime `GtProfile config`, `active_config_name`, `app_config_active_name()`, `app_config_set_macros()` (transition) ; chaque fonction
listée dans « Interfaces partagées » prend la session et lit/écrit `gt_session_get_profile(s)`, `gt_session_get_profile_name(s)`,
`gt_session_set_profile_name(s, …)`, `gt_session_get_macros(s)` ; `hard_default_configuration(s)` réinitialise le profil de `s` ; `load_profile(s, name)` garde
`lock_port` et charge `gt_macros_load_file(gt_session_get_macros(s), profile->macros_file, FALSE)` ; `app_config_load_profile` appelle le crochet
`profile_applied(s)` ; `app_config_save_profile(s, …)` fait d'abord `gt_term_options_store_profile(gt_session_get_options(s), profile)` et
`g_object_get(gt_session_get_link(s), "auto-reconnect", &profile->auto_reconnect, NULL)` (ce que faisait `profile_dialogs.c` en 6a : l'y retirer), puis
`macros_file` = `gt_macros_get_path(...)`. Les fonctions qui ne dépendent pas d'un profil (magasin, `[general]`, `[window]`, liste et suppression de profils)
ne changent pas.

**`src/cmdline.c`** : `read_command_line(s, argc, argv, out)` ; toute lecture ou écriture de `config` devient `*gt_session_get_profile(s)` ;
`Check_configuration_file(s)`, `Load_configuration_from_file(s, name)`, `Verify_configuration(s)`.

**Interface (une session, aucun changement visible)** :
- `src/ui/main.c` : `config_file_init()` → gettext → `gt_ui_install_app_hooks()` → `script_engine_setup()` → `GtSessionManager *sessions =
  gt_session_manager_new(); GtSession *first = gt_session_new(NULL, NULL); gt_session_manager_add(sessions, first, -1);` → `read_command_line(first, …)` →
  sortie anticipée (`gt_ui_messages_shutdown()`, `gt_session_shutdown(first)`, `g_object_unref(first)`, `g_object_unref(sessions)`) ou
  `gt_app_new(&cmd, sessions)` → `g_application_run` → `gt_session_shutdown` de chaque session (sans effet si déjà fait) → libérations.
- `src/ui/app.c` : `gt_app.sessions` ; `gt_app_get_active_session()` ; les anciens appels deviennent ceux de la session active (`gt_session_open_port`,
  `gt_session_apply_profile` + `gt_terminal_view_apply_profile(…, gt_session_get_profile(s))` + couleurs du panneau de script) ; `open-failed`,
  SIGUSR1/SIGUSR2 et `app_stop_services()` visent la session active (une seule pour l'instant) ; `app_stop_services()` appelle `gt_session_shutdown(s)` à la
  place de l'arrêt du moteur, des macros et de la liaison (ordre de la décision 3 : arrêts d'interface d'abord, inchangés).
- Le crochet `profile_applied(session)` de `dialogs.c` réapplique et rouvre le port de **cette** session (`gt_session_apply_profile`, terminal, couleurs,
  `gt_session_open_port`).
- Modules : `grep -rn '\bconfig\b\.' src/ui` doit être vide ; chaque module reçoit la session (ou ses objets) à sa création ou lit
  `gt_app_get_active_session()` au moment d'agir (dialogues).

- [ ] **Step 1 : tests de la couche application (RED)** — `tests/meson.build` : nouvelle table de tests qui lient la vraie couche application :

```meson
# Tests de la couche application complète (gtkterm_app : sessions, configuration, ligne de commande), sans GTK ni fenêtre ; HOME et XDG privés.
app_tests = {
	'test_session' : [util_dep],
	'test_session_manager' : [],
	'test_app_config' : [],
	'test_cmdline' : [],
}
foreach name, extra_deps : app_tests
	exe = executable(name, name + '.c', dependencies : [gtkterm_app_dep] + extra_deps)
	test(name, exe, args : ['--tap'], protocol : 'tap', suite : 'unit', env : test_env, timeout : 60)
endforeach
```

  et retirer `test_app_config` et `test_cmdline` de `integration_tests` (ils lient maintenant la vraie couche, plus `stub_macros_file.c`). Écrire
  `tests/test_session_manager.c` :

```c
/* Tests de GtSessionManager (src/session_manager.c, sous-projet 6b) : ordre, active, retrait, déplacement. */
#include "session_manager.h"
#include "test_util.h"

static void on_notify(GObject *o, GParamSpec *p, gpointer user_data)
{
	(*(guint *)user_data)++;
}

static void test_ordre_et_active(void)
{
	GtSessionManager *m = gt_session_manager_new();
	GtSession *a = gt_session_new(NULL, NULL), *b = gt_session_new(NULL, NULL), *c = gt_session_new(NULL, NULL);
	guint n = 0;

	g_signal_connect(m, "notify::active", G_CALLBACK(on_notify), &n);
	g_assert_null(gt_session_manager_get_active(m));
	gt_session_manager_add(m, a, -1);
	g_assert_true(gt_session_manager_get_active(m) == a);	/* la première devient active */
	g_assert_cmpuint(n, ==, 1);
	gt_session_manager_add(m, c, -1);
	gt_session_manager_add(m, b, 1);	/* a, b, c */
	g_assert_cmpuint(gt_session_manager_get_n(m), ==, 3);
	g_assert_true(gt_session_manager_get_nth(m, 1) == b);
	g_assert_cmpint(gt_session_manager_index_of(m, c), ==, 2);
	g_assert_null(gt_session_manager_get_nth(m, 3));
	g_assert_true(gt_session_manager_get_active(m) == a);
	gt_session_manager_set_active(m, a);	/* même : pas de notification */
	g_assert_cmpuint(n, ==, 1);
	gt_session_manager_set_active(m, c);
	g_assert_cmpuint(n, ==, 2);
	g_assert_cmpuint(gt_session_get_id(b), >, gt_session_get_id(a));
	g_object_unref(a);
	g_object_unref(b);
	g_object_unref(c);
	g_object_unref(m);	/* le gestionnaire tenait ses références */
}

static void test_retrait(void)
{
	GtSessionManager *m = gt_session_manager_new();
	GtSession *a = gt_session_new(NULL, NULL), *b = gt_session_new(NULL, NULL), *c = gt_session_new(NULL, NULL);

	gt_session_manager_add(m, a, -1);
	gt_session_manager_add(m, b, -1);
	gt_session_manager_add(m, c, -1);
	gt_session_manager_set_active(m, b);
	gt_session_manager_remove(m, b);	/* voisine de droite */
	g_assert_true(gt_session_manager_get_active(m) == c);
	gt_session_manager_remove(m, c);	/* plus de droite : gauche */
	g_assert_true(gt_session_manager_get_active(m) == a);
	gt_session_manager_remove(m, c);	/* absente : rien */
	g_assert_cmpuint(gt_session_manager_get_n(m), ==, 1);
	gt_session_manager_remove(m, a);
	g_assert_null(gt_session_manager_get_active(m));
	g_object_unref(a);
	g_object_unref(b);
	g_object_unref(c);
	g_object_unref(m);
}

static void test_deplacement(void)
{
	GtSessionManager *m = gt_session_manager_new();
	GtSession *a = gt_session_new(NULL, NULL), *b = gt_session_new(NULL, NULL), *c = gt_session_new(NULL, NULL);

	gt_session_manager_add(m, a, -1);
	gt_session_manager_add(m, b, -1);
	gt_session_manager_add(m, c, -1);
	gt_session_manager_move(m, a, 99);	/* b, c, a */
	g_assert_true(gt_session_manager_get_nth(m, 2) == a);
	g_assert_true(gt_session_manager_get_active(m) == a);
	gt_session_manager_move(m, c, 0);	/* c, b, a */
	g_assert_true(gt_session_manager_get_nth(m, 0) == c);
	g_object_unref(a);
	g_object_unref(b);
	g_object_unref(c);
	g_object_unref(m);
}

int main(int argc, char *argv[])
{
	g_test_init(&argc, &argv, NULL);
	test_util_private_dirs();	/* HOME et XDG privés (tests/test_util.h) ; adapter au nom réel de l'aide */
	g_test_add_func("/sessions/ordre-et-active", test_ordre_et_active);
	g_test_add_func("/sessions/retrait", test_retrait);
	g_test_add_func("/sessions/deplacement", test_deplacement);
	return g_test_run();
}
```

  et `tests/test_session.c` (pty par `openpty`, comme `tests/test_file_sender.c` ou `tests/test_link.c` ; boucle principale itérée par une aide
  `iterate_until(pred, ms)` écrite dans le fichier) avec ces cas :
  - `/session/nom-court` : série `/dev/pts/7` → `pts7`… exactement `g_path_get_basename` (`"7"` pour `/dev/pts/7`) ; client TCP `host=machine`,
    `tcp_port=4000` → `machine:4000` ; serveur `tcp_port=2323` → `:2323` ; port vide → `?` ; `gt_session_apply_profile` après un changement de port notifie
    `short-name` une fois, et pas de nouveau si rien ne change.
  - `/session/profil` : `gt_session_new(&p, "atelier")` copie le profil (modifier `p` ensuite ne change pas la session) et le nom ; `NULL` → « default » ;
    `apply_profile` met `echo`/`crlf-auto`/`timestamp`/`esc-clear-screen` du profil dans les options et `auto-reconnect` dans la liaison.
  - `/session/deux-sessions-independantes` : deux pty, deux sessions ouvertes (`gt_session_open_port`) ; écrire `A` sur la pty 1 et `B` sur la pty 2 ;
    l'historique de chaque liaison ne contient que son octet (`gt_history` : utiliser l'API de lecture de `history.h`) ; écho activé sur la session 1 seulement
    (`g_object_set(options1, "echo", TRUE, NULL)`) : options de la session 2 inchangées ; une macro envoyée par `gt_macros_send(gt_session_get_macros(s1), …)`
    n'arrive que sur la pty 1.
  - `/session/arret` : session ouverte sur pty ; `gt_session_shutdown` → signal `closed` émis une fois, liaison `GT_LINK_CLOSED`, `gt_session_is_shut_down`
    vrai, actions du groupe désactivées ; second `shutdown` sans effet (pas de second `closed`) ; `gt_session_open_port` ensuite ne rouvre pas ;
    `gt_session_is_busy` faux.
  - `/session/occupee` : `gt_session_set_file_sending(s, TRUE)` → occupée ; script `while true do gtkterm.sleep(10) end` lancé par
    `gt_script_engine_run(gt_session_get_script_engine(s), …)` → occupée ; `gt_session_shutdown` l'arrête (moins de 3 s) et la session n'est plus occupée.
  `test_app_config.c` et `test_cmdline.c` : chaque `config.` devient `gt_session_get_profile(s)->` d'une session de test créée en tête de cas
  (`GtSession *s = gt_session_new(NULL, NULL);`, libérée en fin de cas) ; `app_config_active_name()` → `gt_session_get_profile_name(s)` ; un nouveau cas
  `/app_config/macros-file-par-session` : deux sessions, profils « a » et « b » enregistrés, chacune charge un fichier de macros différent puis
  `config_remember_macros_file(s)` : la clé `macros_file` de `[profile:a]` et celle de `[profile:b]` reçoivent chacune le chemin de leur session.
  `ninja -C build` échoue : RED.
- [ ] **Step 2 : GREEN application** — `session.c`, `session_manager.c`, `link.c` sans liaison par défaut, `app_config.c`, `cmdline.c` ;
  `meson test -C build test_session test_session_manager test_app_config test_cmdline test_link --print-errorlogs` OK.
- [ ] **Step 3 : interface** — règles « Interface (une session) » ci-dessus ; supprimer `gt_app_get_link`, `gt_app_get_options`, `gt_app_get_macros`,
  `gt_app_set_macros`, `gt_app_get_script_engine`, `gt_app_set_script_engine`, `gt_app_open_port`, `gt_app_close_port`, `gt_app_apply_profile` ; adapter
  `test_hooks.c`. `ninja -C build` sans avertissement.
- [ ] **Step 4 : vérifications** — `grep -rn 'gt_link_get_default\|gt_link_set_default\|extern GtProfile config\|app_config_active_name' src tests` : vide ;
  `grep -rn 'gtk/gtk.h' src/*.c src/*.h` : vide ; `meson test -C build`, `meson test -C build-asan`, `meson test -C build --suite gui` : verts (SKIP tolérés).
- [ ] **Step 5 : commit** — message : `sessions : GtSession et GtSessionManager ; plus de liaison par défaut ni de profil global`.

---

### Task 5 : actions d'onglet dans le groupe de session, `app.*` par relais (`Refactor`)

**Files :**
- Modify : `src/session.c` (actions de la session), `src/ui/app_actions.c/.h`, `src/ui/file_actions.c/.h`, `src/ui/search_bar.c/.h`,
  `src/ui/hex_send_bar.c/.h`, `src/ui/macro_panel.c`, `src/ui/script_panel.c`, `src/ui/app.c`, `src/ui/test_hooks.c` si besoin
- Test : `tests/gui/test_menus_actions.py` (ajout d'une vérification de types), `tests/test_session.c` (cas `/session/actions`)

**Règles :**
- `session.c` crée dans son groupe, à `gt_session_new`, les actions de la décision 4 qui lui reviennent (code repris de `app_actions.c` de 6a : mêmes
  noms, mêmes `GPropertyAction`, mêmes conditions : `send-break`, `toggle-dtr`, `toggle-rts` actives seulement si `gt_link_is_serial_open(link)`, mises à jour
  sur `notify::state` et `notify::status-text` de la liaison ; `send-break` → `gt_link_send_break` + `gt_app_status_flash(s, 800, "%s", _("Break signal sent!"))`
  ; `clear-screen` → `gt_link_clear_history` ; `open-port`/`close-port` → `gt_session_open_port`/`gt_session_close_port`).
- `app_actions.c` ne garde que les actions de page qui dépendent du terminal (`clear-scrollback`, `copy`, `paste`, `select-all`) et le menu contextuel ;
  `gt_app_actions_install(GActionMap *tab_actions, GtkWidget *terminal_view)` les ajoute au groupe de la session (plus à l'application) ; `copy` suit la
  sélection de ce terminal. Les accélérateurs restent posés une fois par `gtk_application_set_accels_for_action(app, "app.<nom>", …)` (fonction
  `gt_app_actions_set_accels(GtkApplication *)`, appelée dans `startup`).
- `file_actions.c`, `search_bar.c` (`find`), `hex_send_bar.c` (`show-hex-bar`), `macro_panel.c` (`show-macro-panel`), `script_panel.c` (`show-script-panel`,
  `script-layout`) ajoutent leurs actions au **groupe de la session** reçu en paramètre (`GActionMap *tab_actions`), plus à `GtkApplication`. Leurs fonctions
  de retrait à l'arrêt retirent l'action de ce groupe.
- `src/ui/app.c` : dans `activate`, après la construction de la fenêtre (et donc des actions de page de la première session), pour chaque nom de la
  colonne « session » et « page » du tableau « Actions » (tableau statique `tab_action_names[]` dans `app.c`), `gt_action_relay_new(gt_session_get_actions(s),
  nom)` puis `g_action_map_add_action(G_ACTION_MAP(app), relais)` ; `g_assert` si un nom manque (oubli de module). Les relais sont gardés dans
  `gt_app.relays` (`GPtrArray`) ; `gt_app_retarget_relays(GtSession *s)` (statique) les recible ; appelé sur `notify::active` du gestionnaire. À l'arrêt,
  chaque relais est retiré de l'application puis recible NULL (plus aucune référence vers une session).
- Comportement visible inchangé : menus, raccourcis, D-Bus.

- [ ] **Step 1 : épingler les types (RED pour la régression)** — ajouter à la fin de `tests/gui/test_menus_actions.py`, avant `e.finish()` :

```python
# 6b : chaque action garde son type de paramètre et d'état (relais)
ATTENDU = {
    'local-echo': ('', 'b'), 'crlf-auto': ('', 'b'), 'timestamp': ('', 'b'), 'esc-clear-screen': ('', 'b'), 'hex-index': ('', 'b'),
    'auto-reconnect': ('', 'b'), 'view-mode': ('s', 's'), 'hex-bytes': ('u', 'u'), 'log-paused': ('', 'b'), 'show-hex-bar': ('', 'b'),
    'show-macro-panel': ('', 'b'), 'show-script-panel': ('', 'b'), 'script-layout': ('s', 's'),
    'clear-screen': ('', None), 'open-port': ('', None), 'close-port': ('', None), 'send-break': ('', None), 'toggle-dtr': ('', None),
    'toggle-rts': ('', None), 'clear-scrollback': ('', None), 'copy': ('', None), 'paste': ('', None), 'select-all': ('', None), 'find': ('', None),
    'send-file': ('', None), 'save-raw': ('', None), 'save-ascii': ('', None), 'log-start': ('', None), 'log-stop': ('', None), 'log-clear': ('', None),
}
for nom, (ptype, stype) in ATTENDU.items():
    en, pt, st = e.describe(nom)
    check(pt == ptype, 'type de parametre de %s : %r' % (nom, pt))
    check((stype is None) == (st is None), 'etat de %s present ou absent comme attendu' % nom)
```

  Lancer `meson test -C build --suite gui test_menus_actions` **avant** de modifier le code : vert (les types de 6a sont épinglés). Si un type diffère du
  tableau, c'est le tableau qui est faux : le corriger sur la valeur observée (6a fait foi), puis le garder.
  Ajouter à `tests/test_session.c` le cas `/session/actions` : le groupe d'une session nouvelle contient exactement `local-echo`, `crlf-auto`, `timestamp`,
  `esc-clear-screen`, `hex-index`, `view-mode`, `hex-bytes`, `auto-reconnect`, `clear-screen`, `open-port`, `close-port`, `send-break`, `toggle-dtr`,
  `toggle-rts` ; `send-break` désactivée port fermé ; `g_action_group_change_action_state(actions, "local-echo", TRUE)` met `echo` des options à vrai ; pour
  deux sessions, changer l'une ne change pas l'autre. RED : `ninja -C build` puis `meson test -C build test_session` échoue.
- [ ] **Step 2 : GREEN** — règles ci-dessus ; `meson test -C build test_session` OK.
- [ ] **Step 3 : vérifications** — `meson test -C build`, `meson test -C build-asan`, `meson test -C build --suite gui` (dont `test_menus_actions`,
  `test_options_profil`, `test_echo_vues`, `test_hex_index`, `test_journal`, `test_journal_pause`, `test_search`) verts.
- [ ] **Step 4 : commit** — message : `actions : groupe d'actions par session, actions app.* relayées vers la session active`.

---

### Task 6 : page d'onglet (`session_view.c`), fenêtre à `GtkNotebook`, terminal, barres et fichiers par page (`Interface`)

**Files :**
- Create : `src/ui/session_view.h` (texte de « Interfaces partagées »), `src/ui/session_view.c`
- Modify : `src/ui/main_window.c/.h`, `src/ui/app.c/.h`, `src/ui/status_bar.c/.h`, `src/ui/search_bar.c/.h`, `src/ui/hex_send_bar.c/.h`,
  `src/ui/file_actions.c/.h`, `src/ui/file_send_window.c`, `src/ui/app_actions.c/.h`, `src/ui/dialogs.c`, `src/ui/test_hooks.c`, `src/ui/meson.build`,
  `po/POTFILES.in`

**Règles :**
- **`main_window.c`** : `GtkApplicationWindow` « main-window » = barre de menus (inchangée) + `GtkNotebook` nommé « tabs » (`hexpand`/`vexpand`,
  `gtk_notebook_set_scrollable(TRUE)`, `gtk_notebook_set_show_border(FALSE)`, onglets montrés seulement si `gtk_notebook_get_n_pages() >= 2`, recalculé sur
  `page-added` et `page-removed`). API : `GtkWidget *gt_main_window_new(GtkApplication *app)` (taille et maximisation restaurées comme en 6a),
  `GtkNotebook *gt_main_window_get_notebook(GtkWidget *win)`, `void gt_main_window_insert_view(GtkWidget *win, GtkWidget *view, GtkWidget *tab_label, gint position)`
  (onglet réordonnable), `void gt_main_window_remove_view(GtkWidget *win, GtkWidget *view)`, `GtkWidget *gt_main_window_get_active_view(GtkWidget *win)`,
  `void gt_main_window_get_state(GtkWidget *win, GtWindowState *out)` (taille, maximisation ; séparateurs et visibilités de la page active par
  `gt_session_view_get_state`). Les emplacements et `gt_main_window_get_terminal_view`/`focus_terminal`/`set_slot`/`get_slot` disparaissent (passent à la page).
  Jusqu'à la tâche 8, l'étiquette d'onglet est un simple `GtkLabel` lié à `short-name` (invisible : un seul onglet).
- **`session_view.c`** : reprend de `main_window.c` (6a) la construction sous la barre de menus (emplacement « search-bar », paned vertical [paned horizontal
  (terminal | « macro-panel ») / « script-panel »], « hex-bar », « status-bar », positions et visibilités initiales depuis `st`). À la création : vue du
  terminal (`gt_terminal_view_new(gt_session_get_link(s), gt_session_get_options(s))`), barre d'état (`gt_status_bar_new(link)`), barre de recherche + action
  `find`, barre hexadécimale + `show-hex-bar`, actions de page du terminal (`gt_app_actions_install(G_ACTION_MAP(gt_session_get_actions(s)), terminal_view)`),
  actions de fichiers et journal (`gt_file_actions_new(view, s, initial_send_file)`), le tout ajouté au groupe d'actions de la session. La page se range
  sur la session (`g_object_set_data(G_OBJECT(s), "gt-session-view", view)`, retiré à la destruction de la page) : `gt_session_view_lookup(s)` la rend tant
  qu'elle n'est pas arrêtée. `gt_session_view_shutdown()` (décision 3, étape a) : envoi de fichier et journal arrêtés (`gt_file_actions_shutdown`), barre
  d'état détachée, actions de page retirées du groupe de la session (`g_action_map_remove_action` pour chaque nom de page), puis (tâche 7) panneau de macros,
  raccourcis, panneau de script. `gt_session_view_apply_profile()` = `gt_terminal_view_apply_profile(terminal, gt_session_get_profile(s))` + couleurs du
  panneau de script de la page.
- **Modules rendus par instance** (état dans une structure attachée au widget par `g_object_set_data_full`, plus aucune statique d'onglet) :
  - `status_bar.c` : `GtkWidget *gt_status_bar_new(GtkTermLink *link)` (plus de fenêtre : le titre passe à `app.c`) ; `gt_status_bar_flash(bar, text, ms)` ;
    `gt_status_bar_detach(bar)` ; `gt_status_bar_app_flash` supprimé.
  - `search_bar.c` : `gt_search_bar_install_action(GActionMap *tab_actions, GtkWidget *bar)` (action `find` dans le groupe de la session).
  - `hex_send_bar.c` : `gt_hex_send_bar_install_action(GActionMap *tab_actions, GtkWidget *bar)` (`show-hex-bar`).
  - `file_actions.c` : `void gt_file_actions_new(GtkWidget *view, GtSession *s, const gchar *initial_send_file)` ajoute `send-file`, `save-raw`,
    `save-ascii`, `log-start`, `log-paused`, `log-stop`, `log-clear` au groupe de la session ; journal alimenté par `received` de la liaison de la
    session ; envoi : `gt_session_set_file_sending(s, TRUE)` au début et `FALSE` à la fin (succès, erreur, annulation, arrêt) ; fenêtre de progression
    transitoire de la fenêtre principale, titre « <nom court>: transfer in progress... » (msgid inchangé « %s: transfer in progress... » si c'est le cas en
    6a ; sinon garder le msgid de 6a et y passer le nom court) ; `void gt_file_actions_shutdown(GtkWidget *view)` (idempotente).
  - `app_actions.c` : `gt_app_actions_install(GActionMap *tab_actions, GtkWidget *terminal_view)` (décision 4 : `clear-scrollback`, `copy`, `paste`,
    `select-all`, menu contextuel du VTE) ; `gt_app_actions_set_accels(GtkApplication *)` pose les raccourcis de 6a (inchangés).
- **`app.c`** : `activate` construit la fenêtre, puis la page de la session active (`gt_session_view_new(app, s, &st)` avec `st` lu par
  `app_config_load_window_state`), l'insère, crée les relais (tâche 5), lie le titre de la fenêtre à `status-text` de la session active
  (`g_object_bind_property_full(s, "status-text", win, "title", G_BINDING_SYNC_CREATE, to_title, …)`, « GTKTerm - %s » ; reliaison au changement de session
  active) ; `gt_app_get_active_view()` ; `gt_app_foreach_view()` ; `close-request` : état de la fenêtre (`gt_main_window_get_state`), état du panneau de
  script de la page active, une écriture, puis `gt_session_view_shutdown` de chaque page, puis l'arrêt des services (mise à jour, SIGUSR, `open-failed`, chaque
  session par `gt_session_shutdown`, dans l'ordre des onglets).
- **Crochets (`dialogs.c`)** : `status_flash(s, …)` → `gt_status_bar_flash(slot « status-bar » de gt_session_view_lookup(s), …)` ; rien si la page n'existe
  pas ou est arrêtée ; `profile_applied(s)` → `gt_session_apply_profile(s)`, `gt_session_view_apply_profile(view)`, `gt_session_open_port(s)`.
- **Transition** : le panneau de macros, ses raccourcis et le panneau de script restent des instances uniques de 6a (rangées dans les emplacements de la
  première page) jusqu'à la tâche 7.
- **`test_hooks.c`** : `test-terminal-text` et `test-terminal-props` lisent la vue du terminal de `gt_app_get_active_view()`.

- [ ] **Step 1 : épingler (RED)** — ajouter à `tests/gui/test_fumee.py`, après le démarrage :

```python
check(e.find1(name='tabs')['type'] == 'GtkNotebook', 'fenetre a onglets (GtkNotebook « tabs »)')
check(len(e.find(name='session-view')) == 1, 'une page d onglet')
check(e.find1(name='terminal') is not None, 'terminal dans la page')
```

  `meson test -C build --suite gui test_fumee` échoue (pas de « tabs ») : RED.
- [ ] **Step 2 : GREEN** — règles ci-dessus ; `ninja -C build` sans avertissement ; `meson test -C build --suite gui test_fumee` OK.
- [ ] **Step 3 : vérifications** — `meson test -C build`, `meson test -C build-asan`, `meson test -C build --suite gui` : les 47 scénarios verts (SKIP
  tolérés), en particulier `test_status_bar`, `test_search`, `test_hex_send`, `test_envoi_fichier`, `test_journal*`, `test_sauvegarde_*`, `test_quit_etat`,
  `test_focus_terminal`, `test_fermeture_*`.
- [ ] **Step 4 : commit** — message : `interface : page d'onglet, fenêtre à GtkNotebook, barres et fichiers par page`.

---

### Task 7 : macros et script par page (`Interface`)

**Files :**
- Modify : `src/ui/macro_panel.c/.h`, `src/ui/macro_shortcuts.c/.h`, `src/ui/macro_editor.c/.h`, `src/ui/script_panel.c/.h`, `src/ui/session_view.c`,
  `src/ui/dialogs.c`, `src/ui/app.c`, `src/ui/profile_dialogs.c`, `src/ui/test_hooks.c`

**Règles :**
- `macro_panel.c` : `GtkWidget *gt_macro_panel_new(GtSession *s, GActionMap *tab_actions, gboolean visible)` (panneau « macro-panel » de la session ;
  `show-macro-panel` dans le groupe de la session) ; `gt_macro_panel_rebuild(GtkWidget *panel)`, `gt_macro_panel_polling_changed(GtkWidget *panel, guint id,
  gboolean running)`, `gt_macro_panel_shutdown(GtkWidget *panel)` ; tout passe par `gt_session_get_macros(s)` (`gt_macros_send`, `gt_macros_get_set`,
  `gt_macros_get_poller`, `gt_macros_save_file`, `config_remember_macros_file(s)`). Les noms de widgets de 6a (`macro-<id>`, `macro-tab-<nom>`,
  `polling-popover`, …) sont inchangés : ils sont uniques **dans une page**, et le harnais ne voit que la page affichée.
- `macro_shortcuts.c` : `gt_macro_shortcuts_attach(GtkWidget *view, GtSession *s)` (contrôleur `GTK_SHORTCUT_SCOPE_LOCAL`, phase de capture, sur la page),
  `gt_macro_shortcuts_rebuild(GtkWidget *view)`, `gt_macro_shortcuts_detach(GtkWidget *view)`.
- `macro_editor.c` : une fenêtre par session (rangée sur la session, `g_object_set_data`), `gt_macro_editor_open(GtkWindow *parent, GtSession *s)`, titre
  « Configure Macros — %s » (nom court ; nouveau msgid traduit « Configurer les macros — %s ») ; `gt_macro_editor_macros_changed(GtSession *s)` ; la fenêtre se
  ferme sans rien appliquer si sa session est arrêtée (signal `closed`). L'action d'application `macros` ouvre l'éditeur de la session active.
- `script_panel.c` : `GtkWidget *gt_script_panel_new(GtkApplication *app, GtSession *s, GActionMap *tab_actions, GtkWidget *layout_from)` : moteur de la
  session (`gt_session_get_script_engine(s)`) ; `show-script-panel` et `script-layout` dans le groupe de la session ; `layout_from` NULL : `[layout]` et
  `[session]` lus (première page) ; sinon disposition copiée du panneau `layout_from` (page active), éditeur au modèle par défaut. `[colors]` reste lu par
  chaque panneau ; « Colors… » → Apply enregistre `[colors]` puis rafraîchit le panneau de chaque page (`gt_app_foreach_view` →
  `gt_script_panel_refresh_colors(panel)`). `gt_script_panel_store_state(GtkWidget *panel, GKeyFile *kf)` (panneau de la page active à la fermeture).
  `gt_script_panel_shutdown(GtkWidget *panel)` débranche journal et fin du moteur (le moteur lui-même est arrêté par `gt_session_shutdown`).
- `session_view.c` crée ces trois éléments par page et les arrête dans `gt_session_view_shutdown` (décision 3, étape a : panneau de macros, raccourcis,
  panneau de script, après les fichiers) ; `gt_session_view_new` reçoit le paramètre `GtkWidget *layout_from` (signature finale de « Interfaces partagées » ;
  NULL pour la première page).
- Crochets (`dialogs.c`) : `macros_changed(s)` → panneau et raccourcis de `gt_session_view_lookup(s)` reconstruits, `gt_macro_editor_macros_changed(s)` ;
  `macro_polling_changed(s, id, running)` → panneau de cette page.
- Dialogues de profils et fichiers de macros (`profile_dialogs.c`) : agissent sur la session active au moment de l'activation (décision 4) ; un rappel de
  sélecteur de fichiers retrouve sa session par référence faible (`g_weak_ref`) et ne fait rien si elle est arrêtée.

- [ ] **Step 1 : épingler (RED)** — `tests/gui/test_fumee.py` : ajouter
  `check(e.find1(name='session-view') is not None and e.find1(name='macro-panel') is not None, 'panneau de macros dans la page')`, et dans
  `tests/test_session.c` rien de nouveau (couche application inchangée). Le RED réel de cette tâche est l'absence de statique : avant de coder, lancer
  `grep -nE '^static [A-Za-z_]+ \*?[a-z_]+ *(=|;)|^static [A-Z][A-Za-z]+ [a-z_]+;' src/ui/macro_panel.c src/ui/macro_shortcuts.c src/ui/macro_editor.c src/ui/script_panel.c`
  et noter les statiques d'onglet à supprimer (liste dans le rapport).
- [ ] **Step 2 : GREEN** — règles ci-dessus ; la même commande `grep` ne rend plus que des tables constantes (`static const …`).
- [ ] **Step 3 : vérifications** — `meson test -C build`, `meson test -C build-asan`, `meson test -C build --suite gui` : tous les scénarios de macros
  (`test_macros_*`, `test_macro_*`, `test_raccourci*`, `test_polling`, `test_echo_macro`), de script (`test_script_*`, `test_fermeture_script*`) et le reste
  verts (SKIP tolérés).
- [ ] **Step 4 : commit** — message : `interface : panneau de macros, raccourcis, éditeur et panneau de script par page`.

---

### Task 8 : onglets — nouvel onglet, fermeture, navigation, dialogues liés à leur session (`Feature`)

**Files :**
- Create : `src/ui/tab_label.c/.h`, `tests/gui/test_onglets.py`, `tests/gui/test_onglets_actions.py`, `tests/gui/test_onglets_fermeture.py`,
  `tests/gui/test_onglets_verrou.py`, `tests/gui/test_onglets_sigusr.py`
- Modify : `src/ui/app.c/.h`, `src/ui/menus.ui`, `src/ui/main_window.c`, `src/ui/port_dialog.c/.h`, `src/ui/terminal_dialog.c`, `src/ui/profile_dialogs.c`,
  `src/ui/test_hooks.c/.h`, `src/app_config.c` (taille par défaut 1000 × 600), `src/ui/meson.build`, `tests/gui/gtktest.py`, `tests/gui/meson.build`,
  `po/POTFILES.in`, `po/fr.po`

**Règles :**
- **Actions d'application** (dans `app.c`, avec leurs raccourcis `gtk_application_set_accels_for_action`) : `new-tab` (`<Control><Shift>t`), `close-tab`
  (`<Control><Shift>w`, désactivée tant qu'il n'y a qu'un onglet), `next-tab` (`<Control><Shift>Page_Down`), `previous-tab` (`<Control><Shift>Page_Up`)
  (circulaires ; sans effet avec un seul onglet), `active-tab` (décision 8). `menus.ui`, menu File, en tête : « New tab » (`app.new-tab`), « Close tab »
  (`app.close-tab`), puis une section, puis les entrées de 6a. Libellés sans mnémonique. Vérifier qu'aucun de ces accélérateurs n'est déjà pris (liste de 6a).
- **New tab** (décision 7) : `gt_port_dialog_show_new(GtkWindow *parent, const GtProfile *initial, GtPortDialogNewCb cb, gpointer user_data)` — même
  dialogue que `gt_port_dialog_show` (mêmes champs, même validation, même instance unique C12 : si le dialogue est déjà ouvert, il est présenté tel quel), titre
  « New tab » ; OK applique les champs à une copie de `initial` et appelle `cb(profil, user_data)` puis ferme ; Annuler ferme sans rappel.
  `gt_app_add_session(profil, nom)` : `gt_session_new` ; `gt_macros_load_file(gt_session_get_macros(s), profil->macros_file, FALSE)` ; page
  (`gt_session_view_new(app, s, &st_page_active, vue_active)` où `st_page_active` = `gt_session_view_get_state(vue active)`) ; étiquette ; insertion à
  `index(active) + 1` dans le notebook et le gestionnaire ; session active ; `gt_session_apply_profile` ; `gt_session_view_apply_profile` ;
  `gt_session_open_port` ; focus au terminal.
- **Changement d'onglet** : `switch-page` du notebook → `gt_session_manager_set_active` ; `notify::active` → relais reciblés, titre relié, état de
  `active-tab` mis à jour, sensibilité de `close-tab`, focus au terminal de la page. `page-reordered` → `gt_session_manager_move`.
- **Fermeture** : `gt_app_close_session(s)` : un seul onglet → rien ; `gt_session_is_busy(s)` → `gt_ui_confirm(fenêtre, _("Close this tab?"),
  _("A script is running or a file is being sent in this tab."), _("Close tab"), …)` (rappel : la session peut avoir été fermée entre-temps → rien) ; puis
  `gt_session_view_shutdown(vue)`, `gt_session_shutdown(s)`, `gt_main_window_remove_view`, `gt_session_manager_remove`, `g_object_unref` de ce que l'application
  tenait. `app.close-tab` = `gt_app_close_session(active)`.
- **Étiquette** (`tab_label.c`) : `GtkWidget *gt_tab_label_new(GtSession *s)` — `GtkBox` horizontale nommée « tab-label » : `GtkLabel` « tab-name » lié à
  `short-name` (`g_object_bind_property`, `G_BINDING_SYNC_CREATE`), infobulle liée à `status-text`, bouton plat « tab-close » (icône `window-close-symbolic`,
  nom accessible « Close tab ») → `gt_app_close_session(s)`, insensible quand il n'y a qu'un onglet (suivi par `app.c`) ; classe CSS `lost` posée quand
  l'état de la liaison est `GT_LINK_LOST` ou `GT_LINK_RECONNECTING` (CSS de l'application : `.lost label { font-style: italic; }`).
- **Dialogues liés à leur session** (spec §3.2-12) : `port_dialog.c`, `terminal_dialog.c`, `profile_dialogs.c` et `macro_editor.c` (déjà, tâche 7)
  mémorisent la session active à l'ouverture (`GWeakRef`) et s'y appliquent ; s'ils la trouvent arrêtée (signal `closed` ou `gt_session_is_shut_down`), ils
  se ferment sans rien appliquer.
- **SIGUSR1/SIGUSR2** : `gt_session_open_port` / `gt_session_close_port` de **chaque** session.
- **`open-failed`** : connecté pour chaque session à sa création (déconnecté à son arrêt) ; message préfixé du nom court seulement s'il y a plus d'un onglet :
  `g_strdup_printf("%s: %s", nom_court, message)`.
- **Taille par défaut** : `WINDOW_DEFAULT_WIDTH 1000`, `WINDOW_DEFAULT_HEIGHT 600` (`app_config.c`) ; mettre à jour le commentaire de
  `app_config_load_window_state` et le cas correspondant de `tests/test_app_config.c` (valeurs par défaut attendues 1000 × 600).
- **Crochet de test** (`test_hooks.c`) : action `test-sessions` (décision 9).
- **Harnais** (`tests/gui/gtktest.py`) :

```python
class Pty:
    """Paire pty supplémentaire (onglets) : l'esclave, en mode brut, sert de port série à GTKTerm."""

    def __init__(self):
        self.master, self.slave = os.openpty()
        tty.setraw(self.slave)
        self.port = os.ttyname(self.slave)
        os.set_blocking(self.master, False)

    def read(self, duration=0.5):
        buf = b''
        t0 = time.time()
        while time.time() - t0 < duration:
            r, _, _ = select.select([self.master], [], [], 0.05)
            if r:
                try:
                    buf += os.read(self.master, 65536)
                except OSError:
                    pass
        return buf

    def drain(self, settle=0.3):
        return self.read(settle)

    def write(self, data):
        os.write(self.master, data)

    def close(self):
        for fd in (self.master, self.slave):
            try:
                os.close(fd)
            except OSError:
                pass
        self.master = self.slave = -1
```

  dans `Env.__init__` : `self.ptys = []` ; méthodes d'`Env` :

```python
    def new_pty(self):
        """Pty supplémentaire, fermée avec l'environnement."""
        p = Pty()
        self.ptys.append(p)
        return p

    def sessions(self):
        """Onglets (crochet test-sessions) : liste de dict index, id, name, state, profile, macros_file, active, busy, dans l'ordre."""
        return self.state('test-sessions') or []

    def active_session(self):
        return [s for s in self.sessions() if s['active']][0]
```

  dans `Env.close`, avant la fermeture de `self.master` : `for p in self.ptys: p.close()` ; et au niveau du module :

```python
def open_tab(env, port, timeout=8):
    """Nouvel onglet par le vrai chemin : app.new-tab, dialogue Port prérempli, port remplacé, OK ; attend la nouvelle session active."""
    avant = len(env.sessions())
    env.action('new-tab')
    env.find1(name='port-dialog', timeout=timeout)
    env.set_text('port-device', port)
    env.activate('port-ok')
    ok = wait_until(lambda: len(env.sessions()) == avant + 1 and env.active_session()['name'] == os.path.basename(port), timeout)
    check(ok, 'onglet ouvert sur %s' % port, env.sessions())
    return ok
```

- [ ] **Step 1 : scénarios (RED)** — écrire et déclarer dans `tests/gui/meson.build` (liste des scénarios) :

`tests/gui/test_onglets.py` :

```python
# Onglets (sous-projet 6b) : un seul au démarrage (barre cachée), New tab par le dialogue Port prérempli (Annuler ne crée rien), barre visible à deux,
# frappe sur le port de l'onglet affiché, navigation (Ctrl+Maj+Page, app.active-tab), titre, Ctrl+Maj+W, dernier onglet non fermable, Ctrl+Maj+T.
import os, sys, time
from gi.repository import GLib
from gtktest import *

e = Env('onglets')
e.write_config(e.general() + e.profile())
e.start()
p2 = e.new_pty()
check(len(e.sessions()) == 1, 'un seul onglet au demarrage')
check(not e.find(name='tab-label'), 'barre d onglets cachee avec un onglet')
check(not e.describe('close-tab')[0], 'close-tab desactivee avec un onglet')

e.action('new-tab')
e.find1(name='port-dialog')
check(e.find1(name='port-device').get('label') == e.port, 'dialogue prerempli avec le port de l onglet actif')
e.activate('port-cancel')
check(wait_until(lambda: not e.find(name='port-dialog')), 'Annuler ferme le dialogue')
check(len(e.sessions()) == 1, 'Annuler ne cree pas d onglet')

open_tab(e, p2.port)
check(wait_until(lambda: len(e.find(name='tab-label')) == 2), 'barre d onglets visible avec deux onglets')
check(e.describe('close-tab')[0], 'close-tab active avec deux onglets')
check(e.state('active-tab') == 1, 'le nouvel onglet est actif, a droite')
check(wait_until(lambda: e.active_session()['state'] == 'open'), 'port du nouvel onglet ouvert')
check(wait_until(lambda: os.path.basename(p2.port) in (e.title() or '')), 'titre de la fenetre : onglet actif (%r)' % e.title())

e.drain()
p2.drain()
e.bw.click(e.find1(name='terminal'))
e.bw.type('xy')
check(p2.read(1.0) == b'xy', 'frappe envoyee sur le port de l onglet affiche')
check(e.read_pty(0.3) == b'', 'rien sur le port de l autre onglet')

e.bw.key('Page_Up', mods=('ctrl', 'shift'))
check(wait_until(lambda: e.state('active-tab') == 0), 'Ctrl+Maj+Page precedente : premier onglet')
check(wait_until(lambda: os.path.basename(e.port) in (e.title() or '')), 'titre suit l onglet actif')
e.bw.key('Page_Down', mods=('ctrl', 'shift'))
check(wait_until(lambda: e.state('active-tab') == 1), 'Ctrl+Maj+Page suivante : second onglet')
e.set_state('active-tab', GLib.Variant('i', 0))
check(wait_until(lambda: e.active_session()['index'] == 0), 'app.active-tab change d onglet')

e.bw.key('w', mods=('ctrl', 'shift'))
check(wait_until(lambda: len(e.sessions()) == 1), 'Ctrl+Maj+W ferme l onglet actif')
check(e.sessions()[0]['name'] == os.path.basename(p2.port), 'l onglet restant est le second')
check(wait_until(lambda: not e.find(name='tab-label')), 'barre cachee de nouveau')
check(not e.describe('close-tab')[0], 'close-tab desactivee de nouveau')
e.action('close-tab')
time.sleep(0.5)
check(len(e.sessions()) == 1, 'le dernier onglet ne se ferme pas')

e.bw.key('t', mods=('ctrl', 'shift'))
check(bool(e.find1(name='port-dialog')), 'Ctrl+Maj+T ouvre le dialogue Port')
e.activate('port-cancel')
e.finish()
sys.exit(conclude())
```

`tests/gui/test_onglets_actions.py` :

```python
# Actions app.* relayées vers l'onglet actif (spec §3.2-3) : écho, vue, état du port et sensibilités propres à chaque onglet ; Close port sur l'onglet
# actif seulement ; chaque terminal n'affiche que son port.
import os, sys, time
from gi.repository import GLib
from gtktest import *

e = Env('onglets_actions')
e.write_config(e.general() + e.profile())
e.start()
p2 = e.new_pty()
open_tab(e, p2.port)
e.set_state('local-echo', GLib.Variant('b', True))
e.set_state('view-mode', GLib.Variant('s', 'hex'))
check(e.state('local-echo') is True and e.state('view-mode') == 'hex', 'second onglet : echo et vue hexadecimale')
e.set_state('active-tab', GLib.Variant('i', 0))
check(wait_until(lambda: e.state('local-echo') is False), 'premier onglet : echo inchange')
check(e.state('view-mode') == 'ascii', 'premier onglet : vue ASCII inchangee')
e.set_state('active-tab', GLib.Variant('i', 1))
check(wait_until(lambda: e.state('local-echo') is True and e.state('view-mode') == 'hex'), 'second onglet : options retrouvees')

e.write_pty(b'UN\r\n')
p2.write(b'DEUX\r\n')
check(wait_until(lambda: '44 45 55 58' in e.terminal_text()), 'second onglet affiche son port (hexa)')
check('55 4e' not in e.terminal_text().lower(), 'second onglet n affiche pas le port du premier')
e.set_state('active-tab', GLib.Variant('i', 0))
check(wait_until(lambda: 'UN' in e.terminal_text() and 'DEUX' not in e.terminal_text()), 'premier onglet : seulement son port')

e.set_state('active-tab', GLib.Variant('i', 1))
e.action('close-port')
check(wait_until(lambda: e.sessions()[1]['state'] == 'closed'), 'Close port ferme le port de l onglet actif')
check(e.sessions()[0]['state'] == 'open', 'le port de l autre onglet reste ouvert')
check(wait_until(lambda: not e.describe('send-break')[0]), 'send-break inactive : onglet actif ferme')
e.set_state('active-tab', GLib.Variant('i', 0))
check(wait_until(lambda: e.describe('send-break')[0]), 'send-break active : onglet actif ouvert en serie')
e.finish()
sys.exit(conclude())
```

`tests/gui/test_onglets_fermeture.py` :

```python
# Fermeture d'un onglet occupé (spec §3.2-11) : confirmation si un script tourne, Annuler garde l'onglet, « Close tab » l'arrête proprement ;
# fermeture de la fenêtre avec plusieurs onglets dont un script en cours : code 0, aucun avertissement, [window] écrit.
import os, sys, time
from gi.repository import GLib
from gtktest import *

BOUCLE = 'while true do gtkterm.sleep(20) end\n'

e = Env('onglets_fermeture')
e.write_config(e.general() + e.profile())
e.start()
p2 = e.new_pty()
open_tab(e, p2.port)
e.set_state('show-script-panel', GLib.Variant('b', True))
check(wait_until(lambda: e.find(name='script-editor')), 'panneau de script du second onglet')
e.set_text('script-editor', BOUCLE)
e.activate('script-run')
check(wait_until(lambda: e.active_session()['busy']), 'second onglet occupe (script en cours)')
e.action('close-tab')
check(wait_until(lambda: e.alerts(), 3), 'confirmation demandee')
e.close_alert('Cancel')
check(len(e.sessions()) == 2, 'Annuler garde l onglet')
check(e.active_session()['busy'], 'le script tourne toujours')
e.action('close-tab')
check(wait_until(lambda: e.alerts(), 3), 'confirmation demandee de nouveau')
e.close_alert('Close tab')
check(wait_until(lambda: len(e.sessions()) == 1, 6), 'onglet ferme apres confirmation')
check(not e.sessions()[0]['busy'], 'onglet restant libre')

# Sans script : pas de confirmation
p3 = e.new_pty()
open_tab(e, p3.port)
e.action('close-tab')
check(wait_until(lambda: len(e.sessions()) == 1, 3), 'onglet libre ferme sans confirmation')
check(not e.alerts(), 'aucune boite')

# Quitter avec deux onglets dont un script en cours
open_tab(e, p3.port)
e.set_state('show-script-panel', GLib.Variant('b', True))
e.set_text('script-editor', BOUCLE)
e.activate('script-run')
check(wait_until(lambda: e.active_session()['busy']), 'script en cours avant de quitter')
e.finish()	# app.quit : code 0, stderr sans erreur, aucun rapport ASan
check('[window]' in e.read_config(), '[window] enregistre a la fermeture')
sys.exit(conclude())
```

`tests/gui/test_onglets_verrou.py` :

```python
# Même port série dans un second onglet : refusé par le verrou du transport (boîte d'erreur open-failed), l'onglet reste, port fermé.
import os, sys
from gtktest import *

e = Env('onglets_verrou')
e.write_config(e.general() + e.profile())
e.start()
e.action('new-tab')
e.find1(name='port-dialog')
e.activate('port-ok')	# prérempli : même port que l'onglet actif
check(wait_until(lambda: len(e.sessions()) == 2), 'second onglet cree')
check(wait_until(lambda: e.alerts(), 5), 'boite d erreur : port deja tenu')
check(any(e.port in (a.get('text') or '') or 'lock' in (a.get('text') or '').lower() for a in e.alerts()), 'message cite le port ou le verrou', e.alerts())
e.close_alert('OK')
check(e.sessions()[1]['state'] == 'closed', 'second onglet : port ferme')
check(e.sessions()[0]['state'] == 'open', 'premier onglet : port toujours ouvert')
e.finish()
sys.exit(conclude())
```

  (Lire `Env.alerts()` dans `gtktest.py` pour la forme exacte d'une boîte — clés du dict — et adapter `a.get('text')` à la clé réelle du texte.)

`tests/gui/test_onglets_sigusr.py` :

```python
# SIGUSR2 ferme le port de tous les onglets, SIGUSR1 les rouvre (spec §3.2-15).
import os, signal, sys
from gtktest import *

e = Env('onglets_sigusr')
e.write_config(e.general() + e.profile())
e.start()
p2 = e.new_pty()
open_tab(e, p2.port)
check(wait_until(lambda: all(s['state'] == 'open' for s in e.sessions())), 'deux ports ouverts')
os.kill(e.proc.pid, signal.SIGUSR2)
check(wait_until(lambda: all(s['state'] == 'closed' for s in e.sessions())), 'SIGUSR2 : tous les ports fermes')
os.kill(e.proc.pid, signal.SIGUSR1)
check(wait_until(lambda: all(s['state'] == 'open' for s in e.sessions())), 'SIGUSR1 : tous les ports rouverts')
e.finish()
sys.exit(conclude())
```

  `meson test -C build --suite gui test_onglets test_onglets_actions test_onglets_fermeture test_onglets_verrou test_onglets_sigusr` : échecs (actions
  absentes) : RED.
- [ ] **Step 2 : GREEN** — règles ci-dessus ; les cinq scénarios passent.
- [ ] **Step 3 : traductions** — `po/POTFILES.in` (`src/ui/tab_label.c`) ; `ninja -C build gtkterm-pot gtkterm-update-po` ; `po/fr.po` : « New tab » →
  « Nouvel onglet », « Close tab » → « Fermer l'onglet », « Close this tab? » → « Fermer cet onglet ? », « A script is running or a file is being sent in
  this tab. » → « Un script tourne ou un fichier est en cours d'envoi dans cet onglet. », « Configure Macros — %s » → « Configurer les macros — %s ».
- [ ] **Step 4 : vérifications** — `meson test -C build`, `meson test -C build-asan`, `meson test -C build --suite gui` : 47 + 5 scénarios verts (SKIP tolérés).
- [ ] **Step 5 : commit** — message : `onglets : nouvel onglet, fermeture, navigation, dialogues liés à leur session`.

---

### Task 9 : indépendance des macros et des scripts entre onglets (`Test`)

**Files :**
- Create : `tests/gui/test_onglets_macros.py`, `tests/gui/test_onglets_lua.py`
- Modify : `tests/gui/meson.build` ; code de `src/` seulement si un scénario révèle un défaut (corriger dans la même tâche, cas en échec d'abord)

- [ ] **Step 1 : scénarios** —

`tests/gui/test_onglets_macros.py` :

```python
# Macros par onglet (spec §3.1-1, §3.1-2) : un fichier de macros différent par onglet, panneaux différents, clic et raccourci sur le port de l'onglet,
# raccourci d'un onglet sans effet dans l'autre, polling qui continue sur son port quand on change d'onglet et s'arrête à la fermeture de son onglet.
import os, sys, time
from gi.repository import GLib
from gtktest import *

e = Env('onglets_macros')
ma = e.macros_path
mb = os.path.join(e.cfg, 'macros_b.ini')
write_macros_v2(ma, [dict(label='MA', action='<A>', shortcut='F2', polling=True, period=200)])
write_macros_v2(mb, [dict(label='MB', action='<B>')])
macro_config(e, ma)
e.start()
p2 = e.new_pty()
open_tab(e, p2.port)
check(e.sessions()[1]['macros_file'] == ma, 'nouvel onglet : fichier de macros de l onglet actif')
e.action('load-macros')
file_dialog(e, 'Load macros file', mb)
check(wait_until(lambda: e.sessions()[1]['macros_file'] == mb), 'second onglet : fichier B')
check(e.sessions()[0]['macros_file'] == ma, 'premier onglet : fichier A inchange')
check(wait_until(lambda: macro_buttons(e) and all('MB' in (b.get('label') or '') for b in macro_buttons(e))), 'panneau du second onglet : MB seulement')

e.drain()
p2.drain()
e.activate('macro-%d' % macro_id(e, 'MB'))
check(p2.read(1.0) == b'<B>', 'MB envoyee sur le port du second onglet')
check(e.read_pty(0.3) == b'', 'rien sur le port du premier')
e.bw.click(e.find1(name='terminal'))
e.bw.key('F2')
check(b'<A>' not in p2.read(0.8), 'F2 (raccourci de A) sans effet de macro dans le second onglet')
check(e.read_pty(0.3) == b'', 'F2 n envoie rien sur le premier port')

e.set_state('active-tab', GLib.Variant('i', 0))
check(wait_until(lambda: any('MA' in (b.get('label') or '') for b in macro_buttons(e))), 'panneau du premier onglet : MA')
e.drain()
e.bw.click(e.find1(name='terminal'))
e.bw.key('F2')
check(b'<A>' in e.read_pty(0.8), 'F2 envoie MA dans le premier onglet')

# Polling de MA (premier onglet) : continue quand le second est affiché, s'arrête à la fermeture du premier
e.activate('macro-%d' % macro_id(e, '⏱ MA'))
check(e.read_pty(1.0).count(b'<A>') >= 3, 'polling de MA actif')
e.set_state('active-tab', GLib.Variant('i', 1))
e.drain()
p2.drain()
check(e.read_pty(1.0).count(b'<A>') >= 3, 'second onglet affiche : le polling continue sur le premier port')
check(b'<A>' not in p2.read(0.3), 'le polling ne part jamais sur le second port')
e.set_state('active-tab', GLib.Variant('i', 0))
e.action('close-tab')
check(wait_until(lambda: len(e.sessions()) == 1, 5), 'premier onglet ferme')
e.drain()
check(e.read_pty(1.0) == b'', 'polling arrete avec son onglet')
e.finish()
sys.exit(conclude())
```

`tests/gui/test_onglets_lua.py` :

```python
# Lua par onglet (spec §3.1-3) : le port 0 d'un script est la liaison de son onglet, pour toute l'exécution ; deux scripts en parallèle ;
# fermer un onglet pendant son script ne touche pas l'autre.
import os, sys, time
from gi.repository import GLib
from gtktest import *

ECHO = 'while true do local d = gtkterm.wait_for(0, "PING", 100) if d then gtkterm.send(0, "%s") end end\n'

e = Env('onglets_lua')
e.write_config(e.general() + e.profile())
e.start()
e.set_state('show-script-panel', GLib.Variant('b', True))
check(wait_until(lambda: e.find(name='script-editor')), 'panneau de script du premier onglet')
e.set_text('script-editor', ECHO % 'PONG1')
e.activate('script-run')
p2 = e.new_pty()
open_tab(e, p2.port)
check(wait_until(lambda: e.find(name='script-editor')), 'panneau de script du second onglet (visibilite copiee)')
check((e.find1(name='script-editor').get('label') or '') != ECHO % 'PONG1', 'editeur du second onglet independant')
e.set_text('script-editor', ECHO % 'PONG2')
e.activate('script-run')
check(wait_until(lambda: all(s['busy'] for s in e.sessions())), 'deux scripts en cours')

e.drain()
p2.drain()
e.write_pty(b'PING')
check(b'PONG1' in e.read_pty(1.5), 'script du premier onglet repond sur son port, second onglet affiche')
check(b'PONG' not in p2.read(0.3), 'rien sur le second port')
p2.write(b'PING')
check(b'PONG2' in p2.read(1.5), 'script du second onglet repond sur son port')

e.action('close-tab')	# second onglet, occupé : confirmation
check(wait_until(lambda: e.alerts(), 3), 'confirmation de fermeture')
e.close_alert('Close tab')
check(wait_until(lambda: len(e.sessions()) == 1, 6), 'second onglet ferme pendant son script')
e.drain()
e.write_pty(b'PING')
check(b'PONG1' in e.read_pty(1.5), 'le script du premier onglet tourne toujours')
e.finish()
sys.exit(conclude())
```

  Lancer `meson test -C build --suite gui test_onglets_macros test_onglets_lua --print-errorlogs`. Un échec révèle un défaut d'indépendance : le corriger
  (test d'abord au niveau le plus bas possible : `test_session`, `test_macros_send` ou `test_script_engine`), jamais en affaiblissant le scénario.
- [ ] **Step 2 : vérifications** — `meson test -C build`, `meson test -C build-asan`, `meson test -C build --suite gui` : 54 scénarios verts (SKIP tolérés).
- [ ] **Step 3 : commit** — message : `tests : indépendance des macros et des scripts Lua entre onglets`.

---

### Task 10 : documentation et traductions (`Docs`)

**Files :**
- Modify : `CLAUDE.md`, `tests/README.md`, `README.md` (section d'utilisation : onglets, raccourcis), `docs/superpowers/reviews/2026-10-06-revue-code.md`,
  `docs/superpowers/specs/2026-10-10-refonte-onglets-design.md` (section « Amendements issus du plan »), `po/fr.po` (vérification)

- [ ] **Step 1 : `CLAUDE.md`** — architecture : couche application avec `GtSession`/`GtSessionManager` (tout l'état d'un onglet, plus de liaison par défaut ni
  de `config` global), `GtMacros` et `GtScriptEngine` par session (contrat Lua inchangé, par moteur ; `run_on_main_sync` commun), `GtActionRelay` (cœur) et
  actions d'onglet dans le groupe de session exposées en `app.*`, crochets `GtAppUi` avec la session, page d'onglet `session_view.c`, `GtkNotebook` « tabs »
  (barre cachée avec un onglet), nouvelles actions et raccourcis (Ctrl+Maj+T/W/Page), règle « aucune statique d'onglet », ordre d'arrêt (page puis session),
  `[window]`/`[layout]`/`[session]` venant de la page active, taille par défaut 1000 × 600, harnais (`Pty`, `new_pty`, `sessions`, `open_tab`,
  `test-sessions`, SKIP sur plantage de `gtk4-broadwayd`). Mettre à jour le nombre de scénarios (54).
- [ ] **Step 2 : `tests/README.md`** — nouveaux scénarios, aides du harnais, crochet `test-sessions`, règle SKIP broadway (GTK 4.24).
- [ ] **Step 3 : revue** — annexe « Remarques mineures reportées par le sous-projet 6b » (remplie à la relecture finale) ; la remarque « Disposition par
  défaut » de l'annexe 6 : « Traité par le sous-projet 6b (taille par défaut 1000 × 600) ».
- [ ] **Step 4 : spec** — section « Amendements issus du plan » : décisions 1 (journal dans la page), 3 (ordre d'arrêt), 4 (répartition des actions), 5
  (ordre maître : l'interface), 6, 7, 12, 13 ; paramètre `layout_from` de `gt_session_view_new`.
- [ ] **Step 5 : vérifications** — `ninja -C build gtkterm-pot gtkterm-update-po` puis `msgfmt -c --statistics po/fr.po` : aucune chaîne non traduite ni floue
  pour les msgids de 6b ; `meson test -C build`.
- [ ] **Step 6 : commit** — message : `docs : onglets (CLAUDE.md, tests/README.md, README, revue, spec)`.
