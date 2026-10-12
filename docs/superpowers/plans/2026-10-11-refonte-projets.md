# Projets (sous-projet 6c) — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan
> task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** un projet est un fichier `.gtkterm` unique qui contient tous les onglets (réglages, état de page, copie des macros, copie du script Lua) ;
l'ouvrir rouvre tous les onglets, GTKTerm rouvre le dernier projet au démarrage ; les arguments saisis dans le panneau de macros ne sont plus enregistrés
(valeurs d'usage), leurs valeurs par défaut se règlent dans l'éditeur de macros ; menus réorganisés (Port › New port…).

**Architecture :** trois phases, la suite verte à chaque tâche. Phase A (tâches 1 à 5, cœur, aucun changement visible) : sérialisations réutilisables
(profil ↔ groupe, jeu de macros ↔ groupes préfixés), remarques SP3/SP4, `GtFileStamp`, format projet `GtProjectData`. Phase B (tâches 6 à 8, couche
application) : `GtMacros` (valeurs d'usage, mode intégré, garde d'identité), `GtSession` (`in-project`, `script-text`, signal `edited`), `app_config`
(`last_project`, profils dans un onglet de projet), `--project`, puis `GtProject` (capture, instantané, « modifié », enregistrement). Phase C (tâches 9 à
13, interface) : panneau et éditeur de macros, panneau de script, actions `project-*` et menus, démarrage, scénarios graphiques, documentation.

**Tech Stack :** C (gnu11), GLib/GObject/GIO, GTK 4.14, VTE 0.76, GtkSourceView 5.12, Lua 5.4, Meson/Ninja, GLib GTest, Python 3 (suite graphique :
`gtk4-broadwayd`, D-Bus `org.gtk.Actions`, crochet de test).

**Spec :** `docs/superpowers/specs/2026-10-10-refonte-projets-design.md` (autorité en cas de doute ; les écarts assumés par ce plan sont listés dans
« Décisions »).

## Global Constraints

- Français pour les commentaires, messages de commit et documents ; identifiants de code en anglais ; msgids de l'interface en anglais (traduits dans `po/fr.po`).
- Lignes de 200 colonnes au plus, jamais repliées à 80.
- Style : celui du fichier modifié (`macros.c`, `macros_format.c`, `macros_list.c` : GNU 2 espaces ; `script_engine.c` : 4 espaces ; `config_store.c` :
  tabulations, accolades Allman, signature sur une ligne `static void f(…)` ; le reste : tabulations, Allman, `if(` sans espace) ; nouveaux fichiers en
  tabulations/Allman (`.clang-format`). Ne jamais reformater un fichier entier.
- **Sécurité absolue** : jamais de programme GTK sur l'écran réel (session Wayland) — tout lancement graphique passe par `meson test -C build --suite gui`
  (broadway privé, bus privé), **jamais deux suites graphiques en même temps** ; jamais `~/.config/.gtktermrc`, `~/.gtktermrc`, `~/.config/gtkterm_macros.ini`,
  `~/.cache`, `~/.local` ; jamais de vrai port série : `openpty` et `127.0.0.1` seulement.
- Versions minimales : GTK **4.14**, VTE **0.76**, GtkSourceView **5.12** ; `src/ui/` compilé avec `GDK_VERSION_MAX_ALLOWED=GDK_VERSION_4_14` et
  `-Werror=deprecated-declarations` : vérifier le « Since » de toute nouvelle fonction.
- **Aucun en-tête GTK** dans le cœur ni dans `gtkterm_app` : `grep -rn 'gtk/gtk.h' src/*.c src/*.h` reste vide.
- **Aucune variable statique ne porte d'état d'onglet.** Le projet (`GtProject`) est unique par fenêtre : il vit dans l'état de l'application de
  `src/ui/app.c` (statique permise).
- Noms d'actions `app.*` et raccourcis de 6b **tous** conservés (même type de paramètre et d'état) ; nouvelles actions : `project-new`, `project-open`,
  `project-save`, `project-save-as`, `project-close` (application, sans paramètre ni état), `test-project` (crochet de test).
- Pas de boucle imbriquée ; règles de réentrance de `link.h` inchangées ; rappels asynchrones protégés par pointeur faible ou vérification d'arrêt.
- `po/POTFILES.in` : chaque tâche y ajoute ses nouveaux fichiers à chaînes traduisibles.
- Commandes : `ninja -C build` ; test ciblé `meson test -C build <nom> --print-errorlogs` ; avant chaque commit `meson test -C build --print-errorlogs` et
  `meson test -C build-asan --print-errorlogs` (instabilité tolérée : `test_link` `/perte-puis-reconnexion` sous ASan, une relance). Toute tâche qui touche
  `src/ui/`, `src/link.c`, `src/app_config.c`, `src/cmdline.c`, `src/session.c`, `src/macros.c` ou `tests/gui/` : `meson test -C build --suite gui`
  (poste Arch, GTK 4.24 : un scénario peut être SKIP (77) quand `gtk4-broadwayd` plante sur ses propres assertions ; un SKIP n'est pas un échec, un FAIL l'est).
- Un hook relance `ninja -C build` après chaque modification de `src/*.c|*.h` ; `jq` n'est pas installé (python3 pour le JSON).
- Branche `feature/refonte-projets` déjà extraite : ne pas en changer, ne pas pousser (la fusion et l'envoi sur Maison suivent la relecture finale).
- Chaque commit se termine par exactement : `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`.
- TDD : test d'abord, échec constaté (RED), puis code (GREEN). Les scénarios graphiques sont les tests des tâches d'interface.

## Décisions prises par ce plan (interprétations de la spec)

1. **Identité de fichier** : `GtFileStamp` (cœur) retient la date de modification à la **nanoseconde** (la spec dit µs ; la ns est strictement plus
   sûre) et **remplace** le `FileIdentity` privé de `config_store.c` (pas de deux implémentations).
2. **Suivi « modifié »** : la spec liste des événements ; les dialogues Port et Terminal modifient le profil de la session directement. Un seul canal :
   le signal `edited` de `GtSession`, émis après `changed` de ses `GtTermOptions`, `notify::auto-reconnect` de sa liaison, un changement de
   `script-text`, une modification de son jeu de macros (`gt_macros_changed`, `gt_macros_edited`), `gt_session_apply_profile`, `gt_session_open_port` et
   `gt_session_edited()` (appelée par le dialogue Terminal à chaque changement). `GtProject` écoute `edited` de chaque session et `items-changed` du
   gestionnaire (ajout, retrait, déplacement), et regroupe à l'inactivité (au plus une fois par 300 ms). Avant toute question « Enregistrer ? » et à la
   fermeture, le calcul est refait d'office (`gt_project_check_now`).
3. **Texte du script** : la session garde le texte de l'éditeur (`script-text`), poussé par le panneau de script à chaque modification du tampon. La capture
   du projet ne dépend donc que de la couche application, sauf l'état de page fourni par un rappel de l'interface (`GtProjectPageFunc`).
4. **Champs des valeurs par défaut** dans l'éditeur : une entrée validée (classe `error`, `macro_type_value_valid`) pour tout argument numérique ou texte,
   une liste déroulante pour `%#Liste` (la spec demandait un bouton numérique pour `%d` : une entrée validée sert aussi `%x`, `%f`, `%c`, comme le
   panneau).
5. **Format** : `[tab:N]` porte aussi `profile_name` (nom du profil de l'onglet, pour « Save profile ») ; jamais `macros_file`. La clé `script` est écrite
   même vide. Groupes dans l'ordre : `[gtkterm-project]`, puis pour chaque onglet `[tab:N]`, ses `[tab:N:macro:M]` (M = 1…), ses `[tab:N:list:K]`.
6. **Profil chargé dans un onglet de projet** : le profil s'applique (port, terminal, options) mais **ne charge pas** son fichier de macros (les macros
   de l'onglet sont celles du projet). « Save profile » d'un onglet de projet garde la clé `macros_file` que ce profil avait déjà dans `.gtktermrc`.
   `config_remember_macros_file` ne fait rien pour un onglet de projet.
7. **`--project`** : option longue seule (`--project FICHIER`, chemin rendu absolu). Refusée (code 1) avec `-c`/`--config` ou toute option de profil
   (toutes les `GtCmdlineField` sauf `GT_CMDLINE_NO_LOCK`) ; `-f` et `-L` restent permis. `GtCmdline.profile_given` = `-c` ou une option de profil
   (hors `-L`) : alors le dernier projet n'est pas rouvert.
8. **Enregistrement des `.ini` d'onglet libre** : `gt_macros_save_file()` rend un `GtMacrosSaveResult` (`SAVED`, `FAILED`, `CONFLICT`). `CONFLICT` seulement
   quand la cible est le fichier courant et qu'il a changé sur le disque depuis la dernière lecture ou écriture par cette instance, et que `force` est faux.
   L'interface passe par `gt_ui_macros_save()` (nouveau `src/ui/macros_save.c`) qui pose la question **Overwrite / Reload / Cancel**.
   Un enregistrement non interactif en conflit n'écrit rien et émet un avertissement (`g_warning`).
9. **Ordre d'une ouverture** (spec §3.2-11) : choix du fichier → lecture et validation complètes (refus : message, rien ne change) → confirmation si un
   onglet est occupé → question Enregistrer / Ne pas enregistrer / Annuler si le projet courant est modifié → tous les onglets fermés (page puis session,
   ordre 6b) → onglets du projet créés dans l'ordre → onglet `active` affiché → `last_project` écrit. New project et Close project suivent les mêmes deux
   confirmations. La fermeture de la fenêtre pose la seule question « modifié » (Enregistrer / Ne pas enregistrer / Annuler) ; sans projet ouvert, aucune
   question (comportement 6b).
10. **Titre** : sans projet, « GTKTerm - <status-text de l'onglet actif> » (6b). Avec un projet : « <nom> - <nom court de l'onglet actif> », où <nom> est le
    nom de base du fichier sans `.gtkterm` (« Untitled project » sans fichier), suivi de « • » (` •`) quand il est modifié : « banc • - ttyUSB0 ».
11. **Save project as** : dialogue d'enregistrement (`gt_ui_save_file`, nom proposé « project.gtkterm ») ; `.gtkterm` ajouté si le nom ne finit pas par
    `.gtkterm` ; pas de vérification de conflit (le sélecteur a déjà demandé l'écrasement). Save sur le fichier courant : vérification de conflit,
    Overwrite / Reload / Cancel (Reload = rouvrir le projet depuis le disque, sans autre question).
12. **Script d'un onglet de projet** : Open… charge un fichier dans l'éditeur (le chemin n'est pas retenu dans `[session]`) ; Save ouvre **toujours** le
    dialogue d'enregistrement (export) ; `[session] script_path` n'est ni lu ni écrit pour un onglet de projet.
13. **Crochet de test** : `test-sessions` gagne `in_project` (b) et `script` (s) ; nouvelle action `app.test-project` (état `a{sv}` recalculé à chaque
    activation : `open` (b), `path` (s, vide sans fichier), `modified` (b), `name` (s)).
14. **Libellés** : « Select All » et « Open Port »/« Close Port » gardent leur casse actuelle (traductions existantes) ; les autres libellés suivent la spec §3.2-10.

## Review Focus

1. **Formats inchangés** : `.gtktermrc` et `.ini` écrits après 6c identiques octet pour octet à ceux de 6b à contenu égal. Épinglé par
   `test_profile_keyfile` `/profile_keyfile/identique-6b` et `test_macro_file` `/macro_file/identique-6b` (tâches 1 et 3).
2. **Ouverture tout ou rien** : un projet abîmé ne ferme aucun onglet. Épinglé par `test_project_file` (tâche 5) et `test_projet.py` (tâche 11).
3. **Arguments jamais enregistrés** : aucun fichier écrit quand un argument change dans le panneau. Épinglé par `test_macros_send` (tâche 6) et
   `test_macros_arguments.py` (tâche 9).
4. **Aucun mélange** : dès qu'un projet est ouvert ou enregistré, toutes les sessions ont `in-project` et leurs macros sont intégrées (aucun `.ini` écrit).
   Épinglé par `test_project` (tâche 8) et `test_projet_macros.py` (tâche 12).

## Carte des fichiers

| Fichier | Tâche | Rôle |
|---|---|---|
| `src/window_state.h` (cœur, nouveau) | 1 | `GtWindowState` partagé (déplacé de `app_config.h`) |
| `src/profile_keyfile.c/.h` (cœur, nouveau) | 1 | profil ↔ groupe GKeyFile (extrait de `config_store.c`) |
| `src/config_store.c` | 1, 2, 4 | utilise `profile_keyfile` ; remarques SP3 ; `GtFileStamp` |
| `tests/test_profile_keyfile.c` | 1 | aller-retour, identique à 6b |
| `tests/test_config_store.c` | 2 | cause réelle, annulation après échec, migration stricte |
| `src/macro_file.c/.h`, `src/macro_set.c/.h`, `src/macros_list.c/.h` | 3 | jeu ↔ groupes préfixés, remarques SP4, copie, `GtMacroEdit.args`, liste vide |
| `tests/test_macro_file.c`, `tests/test_macro_set.c`, `tests/test_macros_list.c` | 3 | préfixes, identique à 6b, SP4 |
| `src/file_stamp.c/.h` (cœur, nouveau) | 4 | `GtFileStamp` |
| `tests/test_file_stamp.c` | 4 | identité |
| `src/project_file.c/.h` (cœur, nouveau) | 5 | `GtProjectData` ↔ fichier `.gtkterm`, instantané |
| `tests/test_project_file.c` | 5 | aller-retour, refus, instantané |
| `src/macros.c/.h` | 6 | valeurs d'usage, mode intégré, `GtFileStamp`, `GtMacrosSaveResult`, `edited` |
| `tests/test_macros_send.c` | 6 | valeurs d'usage, intégré, conflit |
| `src/session.c/.h`, `src/app_config.c/.h`, `src/cmdline.c/.h` | 7 | `in-project`, `script-text`, `edited` ; `last_project` ; `--project` |
| `tests/test_session.c`, `tests/test_app_config.c`, `tests/test_cmdline.c` | 7 | idem |
| `src/project.c/.h` (application, nouveau) | 8 | `GtProject` |
| `tests/test_project.c` | 8 | capture, modifié, enregistrement |
| `src/ui/macros_save.c/.h` (nouveau), `src/ui/macro_panel.c`, `src/ui/macro_editor.c`, `src/ui/macro_row.c/.h`, `src/ui/profile_dialogs.c` | 9 | arguments, valeurs par défaut, conflit |
| `tests/test_macro_row.c`, `tests/gui/test_macros_arguments.py`, `tests/gui/test_macros_conflit.py` | 9 | idem |
| `src/ui/script_panel.c/.h`, `src/ui/session_view.c/.h`, `src/ui/terminal_dialog.c` | 10 | texte du script dans la session, mode projet, état de page |
| `src/ui/app.c/.h`, `src/ui/menus.ui`, `src/ui/main.c`, `src/ui/test_hooks.c` | 11 | actions `project-*`, menus, titre, démarrage, fermeture |
| `tests/gui/gtktest.py`, `tests/gui/test_projet*.py`, `tests/gui/test_menus_actions.py`, `tests/gui/meson.build` | 11, 12 | scénarios |
| `CLAUDE.md`, `README.md`, `tests/README.md`, revue, `po/` | 13 | documentation, traductions |

## Interfaces partagées (valeurs exactes, utilisées par plusieurs tâches)

### `src/window_state.h` (tâche 1)

```c
/* État de fenêtre et de page (6a, partagé par le projet en 6c) : taille et maximisation de la fenêtre, séparateurs et visibilités d'une page. */
#ifndef WINDOW_STATE_H_
#define WINDOW_STATE_H_

#include <glib.h>

typedef struct
{
	gint width, height;	/* ≤ 0 : taille par défaut */
	gboolean maximized;
	gint h_paned, v_paned;	/* < 0 : position par défaut */
	gboolean macro_panel_visible, script_panel_visible, hex_bar_visible;
} GtWindowState;

#endif
```

### `src/profile_keyfile.h` (tâche 1)

```c
/* Profil ↔ groupe GKeyFile (6c, extrait de config_store.c) : clés de [profile:…] de .gtktermrc et de [tab:N] d'un projet. */
#ifndef PROFILE_KEYFILE_H_
#define PROFILE_KEYFILE_H_

#include <glib.h>

#include "profile.h"

/* Écrit toutes les clés du profil dans group (macros_file : écrite si non NULL, sinon retirée) */
void gt_profile_to_key_file(const GtProfile *p, GKeyFile *kf, const gchar *group);
/* Lecture tolérante : p déjà rempli (défauts) ; une clé absente ou illisible garde la valeur de p */
void gt_profile_from_key_file(GKeyFile *kf, const gchar *group, GtProfile *p);
/* Booléen tolérant (true/yes/on/1, false/no/off/0) ; *field inchangé si absent ou illisible */
void gt_key_file_read_bool(GKeyFile *kf, const gchar *group, const gchar *key, gboolean *field);
/* Entier tolérant (blancs rognés, base 10, dans gint) ; *field inchangé sinon */
void gt_key_file_read_int(GKeyFile *kf, const gchar *group, const gchar *key, gint *field);

#endif
```

### `src/file_stamp.h` (tâche 4)

```c
/* Identité d'un fichier (6c, spec §3.2-15) : périphérique, inode, taille, date de modification à la nanoseconde. L'écriture atomique (fichier
 * temporaire renommé) change l'inode ; une modification sur place change la taille ou la date. */
#ifndef FILE_STAMP_H_
#define FILE_STAMP_H_

#include <glib.h>

typedef struct
{
	gboolean exists;	/* FALSE : fichier absent ou stat impossible (tous les autres champs à 0) */
	guint64 dev, ino;
	gint64 size;
	gint64 mtime_sec, mtime_nsec;
} GtFileStamp;

void     gt_file_stamp_take(const gchar *path, GtFileStamp *out);
gboolean gt_file_stamp_equal(const GtFileStamp *a, const GtFileStamp *b);	/* deux absents : égaux */
gboolean gt_file_stamp_changed(const gchar *path, const GtFileStamp *known);	/* identité actuelle de path ≠ known */

#endif
```

### `src/macro_file.h` (tâche 3, ajouts)

```c
/* Jeu ↔ groupes de kf préfixés (6c, spec §3.2-2) : prefix "" → [macro:N] et [list:K] (fichier .ini) ; "tab:0:" → [tab:0:macro:N]… Sans en-tête.
 * Écriture : groupes ajoutés dans l'ordre (macros 1…n, puis listes 1…k) ; groupes existants de même nom remplacés. */
void        gt_macro_set_to_key_file(const GtMacroSet *set, GKeyFile *kf, const gchar *prefix);
/* Lecture tolérante, jamais NULL : groupes « prefix macro:* » et « prefix list:* » dans l'ordre du fichier ; un échappement invalide garde la
 * valeur brute (g_warning citant origin et le groupe) ; une liste sans entrée existe quand même. */
GtMacroSet *gt_macro_set_from_key_file(GKeyFile *kf, const gchar *prefix, const gchar *origin);
```

### `src/project_file.h` (tâche 5)

```c
/* Fichier projet .gtkterm (6c, spec §3.2-1 et §4.2) : GKeyFile, [gtkterm-project] version=1 active=N, [tab:N] (profil + profile_name + état de page +
 * script_layout + script), [tab:N:macro:M], [tab:N:list:K]. Sans GTK. */
#ifndef PROJECT_FILE_H_
#define PROJECT_FILE_H_

#include <glib.h>

#include "file_stamp.h"
#include "macro_set.h"
#include "profile.h"
#include "window_state.h"

#define GT_PROJECT_FILE_VERSION 1

#define GT_PROJECT_ERROR (gt_project_error_quark())
typedef enum
{
	GT_PROJECT_ERROR_NOT_PROJECT,	/* syntaxe invalide ou pas de [gtkterm-project] */
	GT_PROJECT_ERROR_VERSION,	/* version absente ou ≠ 1 */
	GT_PROJECT_ERROR_NO_TAB,	/* aucun [tab:0] */
	GT_PROJECT_ERROR_BAD_TAB	/* transport absent ou inconnu dans un [tab:N] */
} GtProjectError;
GQuark gt_project_error_quark(void);

typedef struct
{
	GtProfile profile;	/* macros_file toujours NULL ; lock_port TRUE */
	gchar *profile_name;	/* jamais NULL (« default ») */
	GtWindowState page;	/* h_paned, v_paned, visibilités ; width/height/maximized ignorés (0) */
	gchar *script_layout;	/* "topbottom" | "leftright" */
	GtMacroSet *macros;	/* jamais NULL */
	gchar *script;		/* jamais NULL ("" : aucun) */
} GtProjectTab;

typedef struct
{
	GPtrArray *tabs;	/* GtProjectTab *, possédés */
	guint active;		/* < tabs->len dès qu'il y a un onglet */
} GtProjectData;

GtProjectTab  *gt_project_tab_new(void);	/* profil par défaut, « default », page par défaut (-1, -1, TRUE, FALSE, FALSE), "topbottom", jeu vide, "" */
void           gt_project_tab_free(GtProjectTab *t);
GtProjectData *gt_project_data_new(void);	/* aucun onglet, active 0 */
void           gt_project_data_free(GtProjectData *d);	/* NULL accepté */
/* Tout ou rien : FALSE + GError (G_FILE_ERROR, G_KEY_FILE_ERROR ou GT_PROJECT_ERROR, message traduit préfixé du chemin), *out non écrit.
 * stamp (peut être NULL) : identité du fichier lu. active hors bornes → 0. */
gboolean       gt_project_data_load(const gchar *path, GtProjectData **out, GtFileStamp *stamp, GError **error);
/* Écriture atomique (0644) ; stamp (peut être NULL) : identité du fichier écrit */
gboolean       gt_project_data_save(const GtProjectData *d, const gchar *path, GtFileStamp *stamp, GError **error);
gchar         *gt_project_data_to_text(const GtProjectData *d);	/* contenu exact du fichier */
gchar         *gt_project_data_snapshot(const GtProjectData *d);	/* même sérialisation sans état de page, sans script_layout ni active */

#endif
```

### `src/macros.h` (tâche 6, ajouts et changements)

```c
typedef enum { GT_MACROS_SAVED, GT_MACROS_FAILED, GT_MACROS_CONFLICT } GtMacrosSaveResult;

/* Valeurs d'usage (spec §3.2-6) : jamais enregistrées ; effacées par gt_macros_replace_set (donc par tout chargement). */
void                gt_macros_set_usage_arg(GtMacros *m, guint id, gint arg_index, const gchar *value);	/* initialisées depuis les défauts */
void                gt_macros_reset_usage(GtMacros *m, guint id);	/* id 0 : toutes */
gboolean            gt_macros_has_usage(GtMacros *m, guint id);
/* Arguments d'envoi : valeurs d'usage si elles existent et ont le bon nombre, sinon GtMacro.args (défauts), sinon NULL. Empruntés. */
const gchar *const *gt_macros_get_args(GtMacros *m, guint id);
/* Mode intégré (onglet de projet, spec §3.2-4) : chemin oublié ; load/save visent alors un fichier sans le retenir (import/export). */
void                gt_macros_set_embedded(GtMacros *m, gboolean embedded);
gboolean            gt_macros_is_embedded(GtMacros *m);
/* Rappel « jeu modifié » (session : signal edited) ; appelé par gt_macros_changed et gt_macros_edited */
void                gt_macros_set_edited_func(GtMacros *m, void (*func)(gpointer data), gpointer data);
void                gt_macros_edited(GtMacros *m);	/* réglage enregistrable changé sans reconstruction (popover de polling) */
/* path NULL : chemin courant (ou défaut). CONFLICT : cible = fichier courant changé sur le disque depuis la dernière lecture/écriture, et !force ;
 * rien n'est écrit (non interactif : g_warning). FAILED : message (interactif) ou g_warning. Intégré : le chemin n'est pas retenu. */
GtMacrosSaveResult  gt_macros_save_file(GtMacros *m, const gchar *path, gboolean interactive, gboolean force);
gboolean            gt_macros_reload(GtMacros *m);	/* relit le fichier courant (interactif) ; FALSE sans fichier courant */
```

### `src/session.h` (tâche 7, ajouts)

```c
/* Projet (6c) : propriété « in-project » (onglet d'un projet : macros intégrées) ; propriété « script-text » (texte de l'éditeur de script, jamais NULL,
 * notifiée seulement si elle change) ; signal « edited » (aucun argument) : un réglage enregistrable a pu changer (plan, décision 2). */
gboolean     gt_session_get_in_project(GtSession *s);
void         gt_session_set_in_project(GtSession *s, gboolean in_project);	/* gt_macros_set_embedded ; edited */
const gchar *gt_session_get_script_text(GtSession *s);
void         gt_session_set_script_text(GtSession *s, const gchar *text);	/* NULL : "" */
void         gt_session_edited(GtSession *s);	/* émet « edited » (rien après shutdown) */
```

### `src/app_config.h` (tâche 7, ajouts)

```c
#include "window_state.h"	/* GtWindowState n'est plus défini ici */

gchar *app_config_get_last_project(void);	/* relit ; [general] last_project ; NULL si absente ou vide */
void   app_config_set_last_project(const gchar *path);	/* NULL efface ; relit, écrit, enregistre (non interactif) ; rien si inchangé */
```

### `src/cmdline.h` (tâche 7, ajouts)

```c
/* GtCmdline */
gchar *project;		/* --project, chemin absolu ; NULL : aucun */
gboolean profile_given;	/* -c ou une option de profil autre que -L : le dernier projet n'est pas rouvert */
/* GtCmdlineOptions */
gchar *project;		/* --project tel que donné */
```

### `src/project.h` (tâche 8)

```c
/* Projet de la fenêtre (6c, spec §4.3) : chemin, instantané enregistré, état « modifié ». Couche application ; ne crée ni ne ferme d'onglet
 * (l'interface le fait). Fil principal seulement. */
#ifndef PROJECT_H_
#define PROJECT_H_

#include <gio/gio.h>

#include "file_stamp.h"
#include "project_file.h"
#include "session_manager.h"

G_BEGIN_DECLS

#define GT_TYPE_PROJECT (gt_project_get_type())
G_DECLARE_FINAL_TYPE(GtProject, gt_project, GT, PROJECT, GObject)

typedef enum { GT_PROJECT_SAVED, GT_PROJECT_FAILED, GT_PROJECT_CONFLICT } GtProjectSaveResult;
/* Rappel de l'interface : remplit tab->page (h_paned, v_paned, visibilités) et tab->script_layout pour la page de s */
typedef void (*GtProjectPageFunc)(GtSession *s, GtProjectTab *tab, gpointer user_data);

/* Propriétés : « open » (b), « path » (s, NULL : sans fichier), « modified » (b), notifiées seulement si elles changent */
GtProject     *gt_project_new(GtSessionManager *sessions);	/* aucun projet ouvert */
void           gt_project_set_page_func(GtProject *p, GtProjectPageFunc func, gpointer user_data);
gboolean       gt_project_is_open(GtProject *p);
const gchar   *gt_project_get_path(GtProject *p);
gchar         *gt_project_get_name(GtProject *p);	/* nom de base sans .gtkterm, « Untitled project » sans fichier ; "" si fermé */
gboolean       gt_project_is_modified(GtProject *p);
/* Sessions du gestionnaire dans l'ordre : profil (options et auto-reconnect recopiés, macros_file NULL), nom de profil, copie du jeu, texte du script,
 * état de page (rappel) ; active = index de la session active */
GtProjectData *gt_project_capture(GtProject *p);
/* Projet ouvert : chaque session (et chaque session ajoutée ensuite) passe en in-project ; instantané de référence = celui de saved (données lues
 * du fichier), ou de la capture actuelle si saved est NULL ; path NULL : sans fichier ; stamp NULL : inconnu. Puis calcul de « modified ». */
void           gt_project_begin(GtProject *p, const gchar *path, const GtFileStamp *stamp, const GtProjectData *saved);
void           gt_project_end(GtProject *p);	/* plus de projet : open FALSE, path NULL, modified FALSE (les sessions restent comme elles sont) */
/* path NULL : chemin courant (obligatoire). CONFLICT : même chemin, fichier changé sur le disque, !force ; rien n'est écrit. SAVED : chemin et
 * identité retenus, instantané = capture enregistrée, modified FALSE. FAILED : GError. */
GtProjectSaveResult gt_project_save(GtProject *p, const gchar *path, gboolean force, GError **error);
void           gt_project_check_now(GtProject *p);	/* recalcule « modified » tout de suite */

G_END_DECLS

#endif
```

## Tâches

### Task 1 : profil ↔ groupe GKeyFile, `GtWindowState` partagé (`Refactor`)

**Files:**
- Create: `src/window_state.h`, `src/profile_keyfile.h`, `src/profile_keyfile.c`, `tests/test_profile_keyfile.c`
- Modify: `src/config_store.c` (lignes 118-257 : lecture/écriture du profil retirées), `src/app_config.h` (typedef retiré, `#include "window_state.h"`),
  `src/meson.build` (`core_sources` : `window_state.h`, `profile_keyfile.c`, `profile_keyfile.h`), `tests/meson.build` (`'test_profile_keyfile' : []`)

- [ ] **Step 1 : écrire le test**

`tests/test_profile_keyfile.c` (tabulations, Allman) :

```c
/* Tests de profile_keyfile.c : profil ↔ groupe GKeyFile, texte identique à celui de 6b (config_store.c avant 6c). */

#include <glib.h>

#include "profile_keyfile.h"

/* Texte exact écrit par config_store.c de 6b pour un profil par défaut avec macros_file=/tmp/m.ini (référence figée) */
static const gchar *const reference_6b =
	"[profile:default]\n"
	"transport=serial\n"
	"port=/dev/ttyS0\n"
	"speed=115200\n"
	"bits=8\n"
	"stopbits=1\n"
	"parity=none\n"
	"flow=none\n"
	"wait_delay=0\n"
	"wait_char=-1\n"
	"rs485_rts_time_before_tx=30\n"
	"rs485_rts_time_after_tx=30\n"
	"socket_host=\n"
	"socket_port=\n"
	"echo=false\n"
	"crlfauto=false\n"
	"autoreconnect_enabled=false\n"
	"esc_clear_screen=false\n"
	"timestamp=false\n"
	"font=Monospace 12\n"
	"term_block_cursor=true\n"
	"term_columns=80\n"
	"term_rows=25\n"
	"term_scrollback=10000\n"
	"term_visual_bell=false\n"
	"term_foreground=0.66666666666666663;0.66666666666666663;0.66666666666666663;1;\n"
	"term_background=0;0;0;1;\n"
	"macros_file=/tmp/m.ini\n";

static void test_identique_6b(void)
{
	GtProfile p;
	GKeyFile *kf = g_key_file_new();
	gchar *text;

	gt_profile_init_defaults(&p);
	gt_profile_set_string(&p.macros_file, "/tmp/m.ini");
	gt_profile_to_key_file(&p, kf, "profile:default");
	text = g_key_file_to_data(kf, NULL, NULL);
	g_assert_cmpstr(text, ==, reference_6b);
	g_free(text);
	g_key_file_free(kf);
	gt_profile_clear(&p);
}

static void test_aller_retour(void)
{
	GtProfile a, b;
	GKeyFile *kf = g_key_file_new();

	gt_profile_init_defaults(&a);
	a.transport = TRANSPORT_TCP_CLIENT;
	gt_profile_set_string(&a.host, "h\303\264te;=[x]");
	gt_profile_set_string(&a.tcp_port, "4000");
	a.baudrate = 9600;
	a.echo = TRUE;
	a.wait_char = 13;
	a.foreground = (GtColor){ 0.25, 0.5, 0.75, 1.0 };
	gt_profile_to_key_file(&a, kf, "tab:0");
	gt_profile_init_defaults(&b);
	gt_profile_from_key_file(kf, "tab:0", &b);
	g_assert_cmpint(b.transport, ==, TRANSPORT_TCP_CLIENT);
	g_assert_cmpstr(b.host, ==, a.host);
	g_assert_cmpstr(b.tcp_port, ==, "4000");
	g_assert_cmpuint(b.baudrate, ==, 9600);
	g_assert_true(b.echo);
	g_assert_cmpint(b.wait_char, ==, 13);
	g_assert_cmpfloat(b.foreground.green, ==, 0.5);
	g_assert_null(b.macros_file);
	gt_profile_clear(&a);
	gt_profile_clear(&b);
	g_key_file_free(kf);
}

static void test_cles_absentes_gardees(void)
{
	GtProfile p;
	GKeyFile *kf = g_key_file_new();

	g_key_file_set_string(kf, "g", "speed", "abc");	/* illisible : valeur gardée */
	g_key_file_set_string(kf, "g", "echo", "peut-etre");
	gt_profile_init_defaults(&p);
	p.baudrate = 57600;
	gt_profile_from_key_file(kf, "g", &p);
	g_assert_cmpuint(p.baudrate, ==, 57600);
	g_assert_false(p.echo);
	gt_profile_clear(&p);
	g_key_file_free(kf);
}

int main(int argc, char *argv[])
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/profile_keyfile/identique-6b", test_identique_6b);
	g_test_add_func("/profile_keyfile/aller-retour", test_aller_retour);
	g_test_add_func("/profile_keyfile/cles-absentes-gardees", test_cles_absentes_gardees);
	return g_test_run();
}
```

Avant d'écrire `reference_6b` en dur, la produire avec le code **actuel** (6b) pour qu'elle soit exacte : petit programme jetable dans le scratchpad lié à
`gtkterm_core` (`gt_config_store_load` d'un chemin vide, `gt_config_store_put_profile(s, "default", &p)`, `g_key_file_to_data(gt_config_store_get_key_file(s))`),
coller sa sortie dans le test. Ne pas deviner les valeurs par défaut (débit, couleurs, rs485).

- [ ] **Step 2 : RED** — `ninja -C build` échoue (`profile_keyfile.h` absent) : attendu.

- [ ] **Step 3 : implémenter**

`src/profile_keyfile.c` : déplacer **à l'identique** de `config_store.c` `read_string`, `read_int`, `read_uint`, `read_bool`, `read_color`, `read_profile`,
`write_color`, `write_profile` (corps inchangés), en `static` sauf :
`gt_key_file_read_bool` (= `read_bool`), `gt_key_file_read_int` (= `read_int`), `gt_profile_from_key_file(kf, group, p)` (= `read_profile(kf, group, p)`),
`gt_profile_to_key_file(p, kf, group)` (= `write_profile(kf, group, p)`). En-têtes : `<config.h>`, `"config_file.h"` (pour `config_wait_char_from_file`),
`"config_store.h"` (pour `gt_config_parse_boolean`), `"profile_keyfile.h"`.
Dans `config_store.c` : supprimer ces fonctions, `#include "profile_keyfile.h"`, remplacer `read_profile(s->kf, group, out)` par
`gt_profile_from_key_file(s->kf, group, out)`, `write_profile(s->kf, group, p)` par `gt_profile_to_key_file(p, s->kf, group)`, `read_bool(s->kf, GENERAL_GROUP, key, &v)`
par `gt_key_file_read_bool(...)`.
`src/window_state.h` : contenu de « Interfaces partagées » ; dans `app_config.h`, retirer le `typedef struct {…} GtWindowState;` et ajouter
`#include "window_state.h"`.

- [ ] **Step 4 : GREEN** — `meson test -C build test_profile_keyfile test_config_store test_app_config --print-errorlogs` : tout passe.

- [ ] **Step 5 : suite complète + ASan, commit**

```bash
git add src/window_state.h src/profile_keyfile.[ch] src/config_store.c src/app_config.h src/meson.build tests/test_profile_keyfile.c tests/meson.build
git commit -m "cœur : profil ↔ groupe GKeyFile (profile_keyfile.c) et GtWindowState partagé, format inchangé"
```

### Task 2 : magasin de configuration — remarques SP3 (`Fix`)

**Files:**
- Modify: `src/config_store.c`, `src/app_config.c` (`load_profile`)
- Test: `tests/test_config_store.c`, `tests/test_app_config.c`

Trois remarques reportées (spec §3.2-17) :

1. **Cause réelle** : un fichier devenu illisible en cours de session (droits retirés) donne, au chargement d'un profil, l'erreur de lecture (« Permission
   denied ») au lieu de « No section ». `config_reload_store()` garde l'erreur de `gt_config_store_reload_if_changed` ; ajouter à `config_store.h` :
   `const GError *gt_config_store_get_read_error(GtConfigStore *s); /* dernière erreur de lecture (magasin en lecture seule), NULL sinon */` (erreur
   retenue par `store_read`, effacée à la lecture réussie suivante). Dans `app_config.c` `load_profile`, si le profil est absent **et** que le magasin a une
   erreur de lecture : `g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_PERM, _("Cannot read the configuration file: %s"), err->message)`.
2. **Annulation** : `put_profile`, `delete_profile`, `set_profile_string`, `set_general_*` restent en mémoire ; si `gt_config_store_save` échoue, le contenu
   en mémoire revient à celui d'avant la dernière série de changements. Mise en œuvre : `store_read` et `gt_config_store_save` (réussi) gardent
   `s->saved_text` (`g_key_file_to_data` de l'état lu/écrit) ; un `save` qui échoue recharge `s->kf` depuis `saved_text` (`g_key_file_load_from_data`,
   `G_KEY_FILE_KEEP_COMMENTS`) avant de rendre FALSE.
3. **Migration stricte** : `transport_type` et `wait_char` non numériques (`g_ascii_string_to_signed(val, 10, …)` échoue) → clé ignorée (valeur par défaut au
   chargement) et `g_warning("%s: [%s] %s=%s ignored (not a number)", path, old, k, val)` au lieu d'`atoi`.

- [ ] **Step 1 : tests** — dans `tests/test_config_store.c`, ajouter (style du fichier, répertoires temporaires `g_dir_make_tmp`) :
  - `/config_store/annulation-apres-echec` : magasin chargé d'un fichier avec `[profile:a]` ; `gt_config_store_put_profile(s, "b", &p)` ; rendre le dossier
    non inscriptible (`g_chmod(dir, 0500)`) ; `gt_config_store_save` → FALSE ; `gt_config_store_has_profile(s, "b")` → FALSE ; `has_profile("a")` → TRUE ;
    remettre `0700`. (Sauter le cas avec `g_test_skip` si `getuid() == 0`.)
  - `/config_store/erreur-de-lecture-retenue` : fichier valide chargé ; `g_chmod(path, 0)` puis modification de la date (`g_utime`) pour forcer la
    relecture ; `gt_config_store_reload_if_changed` → FALSE ; `gt_config_store_get_read_error(s)` non NULL, domaine `G_FILE_ERROR` ; remettre `0600`,
    relire → NULL. (`getuid() == 0` : skip.)
  - `/config_store/migration-non-numerique` : ancien fichier parsecfg `[default]\nport = /dev/ttyS0\ntransport_type = tcp\nwait_char = x\n` → profil
    `default` migré sans clé `transport` ni `wait_char` (`g_key_file_has_key` FALSE) ; `g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*not a number*")`
    deux fois.
  Dans `tests/test_app_config.c` : `/app_config/profil-cause-reelle` : profil `p` enregistré ; fichier rendu illisible (chmod 0 + `g_utime`) ;
  `app_config_load_profile(s, "p", &error)` → FALSE, `error->domain == G_FILE_ERROR`, message contenant « Cannot read the configuration file ».

- [ ] **Step 2 : RED** — `meson test -C build test_config_store test_app_config` : nouveaux cas en échec (ou ne compilent pas).

- [ ] **Step 3 : implémenter** (fichiers ci-dessus ; `saved_text` libéré dans `gt_config_store_free` ; `read_error` : `GError *` libéré à chaque relecture).

- [ ] **Step 4 : GREEN** puis suite complète + ASan.

- [ ] **Step 5 : commit** `magasin : cause réelle d'un fichier devenu illisible, changement annulé si l'enregistrement échoue, migration stricte (SP3)`

### Task 3 : jeu de macros ↔ groupes préfixés, remarques SP4, copie de jeu, valeurs par défaut éditables (`Core`)

**Files:**
- Modify: `src/macro_file.c/.h`, `src/macro_set.c/.h`, `src/macros_list.c/.h`
- Test: `tests/test_macro_file.c`, `tests/test_macro_set.c`, `tests/test_macros_list.c`

- [ ] **Step 1 : tests**
  - `test_macros_list.c` `/macros_list/liste-vide` : `gint i = gt_macro_lists_add_list(l, "Vide")` → 0 ; `gt_macro_lists_find(l, "Vide") == 0` ;
    `entry_count == 0` ; second appel → même index, `count == 1`.
  - `test_macro_set.c` `/macro_set/copie` : jeu de 2 macros (args, polling 250 ms) + une liste ; `gt_macro_set_copy` → mêmes champs dans le même ordre,
    identifiants **différents** de l'original, listes égales, la copie survit à `gt_macro_set_free(original)`.
  - `test_macro_set.c` `/macro_set/edits-avec-args` : macro `A%d;%s` avec args `{"1","x"}` ; `GtMacroEdit` de même id avec `args = {"2","y",NULL}` →
    `m->args` = `{"2","y"}` ; avec `args = {"3",NULL}` (mauvais nombre) → `m->args == NULL` ; ligne nouvelle (id 0) avec `args = {"7",NULL}` et action `N%d`
    → `{"7"}` ; ligne avec `args == NULL` → `m->args == NULL`.
  - `test_macro_file.c` :
    - `/macro_file/identique-6b` : jeu de référence (celui de `test_aller_retour`), `gt_macro_file_save` → texte exact attendu, figé en le produisant avec
      le code 6b (même méthode que la tâche 1, step 1) avant de modifier `macro_file.c`.
    - `/macro_file/prefixe` : `gt_macro_set_to_key_file(set, kf, "tab:3:")` → groupes `tab:3:macro:1`… `tab:3:list:1` ; `gt_macro_set_from_key_file(kf,
      "tab:3:", "x")` relit le même jeu (`comparer_macro`) ; `from_key_file(kf, "tab:1:", …)` → jeu vide (« tab:3: » n'est pas « tab:1: ») ; un groupe
      `tab:30:macro:1` n'est pas lu avec le préfixe `tab:3:`.
    - `/macro_file/echappement-invalide-garde` : fichier v2 écrit à la main avec `args=a\q;b` et `displays=x\q;y` → la macro existe, `args` = `{"a\\q","b"}`
      (brut), la liste a ses deux entrées ; deux avertissements (`g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*invalid escape*")`).
    - `/macro_file/liste-vide-relue` : `[list:1]\nname=Vide\ndisplays=\nvalues=\n` → `gt_macro_lists_find(lists, "Vide") >= 0`, 0 entrée ; réécrit, le
      groupe existe toujours.
    - `/macro_file/illisible-cause` : fichier v2 en `0000` (skip si root) → `gt_macro_file_load` FALSE, `G_FILE_ERROR_ACCES`, message contenant le chemin et
      `g_strerror(EACCES)`.
    - `/macro_file/refus-copie-remede` : cas de `test_refus_si_copie_differente` ; le message d'erreur contient `.avant-migration` et « rename or delete ».

- [ ] **Step 2 : RED** — les nouveaux cas échouent ou ne compilent pas.

- [ ] **Step 3 : implémenter**

`macros_list` (style GNU 2 espaces) : `gint gt_macro_lists_add_list (GtMacroLists *lists, const gchar *name);` — liste nommée créée vide si absente,
index rendu ; `gt_macro_lists_add` l'utilise.

`macro_set` : `GtMacroSet *gt_macro_set_copy(const GtMacroSet *set);` (appends dans l'ordre, `gt_macro_set_set_args`, `set_polling`, listes par
`gt_macro_lists_copy`, prises avant les macros). `GtMacroEdit` gagne `const gchar *const *args; /* valeurs par défaut ; NULL : aucune */` ;
`gt_macro_set_apply_edits` : après `set_fields`/`macro_new`, `g_clear_pointer(&m->args, g_strfreev)` puis, si `rows[i].args != NULL` et
`g_strv_length(rows[i].args) == macro_count_format_args(set->lists, m->action)` et ce nombre > 0, `m->args = g_strdupv((gchar **)rows[i].args)`.
**Attention** : les appelants existants (`macro_row.c` `gt_macro_rows_to_edits`) mettent `args` à NULL tant que la tâche 9 ne remplit pas la ligne — ce
qui effacerait les arguments à chaque OK de l'éditeur. Donc, dans cette tâche, `gt_macro_rows_to_edits` remplit `args` avec ceux de la macro d'origine :
`GtMacroRow` gagne une propriété `args` (`G_TYPE_STRV`, NULL permis) recopiée par `gt_macro_rows_from_set` (`m->args`) et rendue par
`gt_macro_row_get_args()` ; `gt_macro_rows_to_edits` met `edits[i].args = gt_macro_row_get_args(row)`. Ajouter à `test_macro_row.c`
`/macro_row/args-recopies` (ligne issue d'un jeu avec args → edit.args égal ; nouvelle ligne → NULL).

`macro_file.c` : extraire de `load_v2` la lecture des groupes dans `gt_macro_set_from_key_file(kf, prefix, origin)` (préfixe : `g_str_has_prefix(group,
prefix)` puis `g_str_has_prefix(group + strlen(prefix), "list:"/"macro:")`) et de `gt_macro_file_save` l'écriture dans `gt_macro_set_to_key_file(set, kf,
prefix)` (noms `"%smacro:%u"`, `"%slist:%d"`). `load_v2` = vérification de version + `gt_macro_set_from_key_file(kf, "", path)` ; `gt_macro_file_save` =
en-tête + `gt_macro_set_to_key_file(set, kf, "")` (ordre des clés et groupes inchangé : `identique-6b`).
Lecture tolérante des listes de chaînes :

```c
/* Liste « a;b;c » : g_key_file_get_string_list, sinon (échappement inconnu) éléments bruts coupés aux « ; » non échappés, « \; » → « ; » */
static gchar **get_list_tolerant(GKeyFile *kf, const gchar *group, const gchar *key, const gchar *origin, gsize *n_out)
{
	GError *local = NULL;
	gchar **v = g_key_file_get_string_list(kf, group, key, n_out, &local);
	gchar *raw;
	GPtrArray *items;
	GString *cur;

	if(local == NULL || !g_error_matches(local, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE))
	{
		g_clear_error(&local);
		return v;
	}
	g_clear_error(&local);
	raw = g_key_file_get_value(kf, group, key, NULL);
	g_warning(_("%s: [%s] %s: invalid escape, value kept as written"), origin, group, key);
	items = g_ptr_array_new();
	cur = g_string_new(NULL);
	for(const gchar *c = raw != NULL ? raw : ""; *c != '\0'; c++)
	{
		if(c[0] == '\\' && c[1] == ';')
		{
			g_string_append_c(cur, ';');
			c++;
		}
		else if(c[0] == ';')
		{
			g_ptr_array_add(items, g_string_free(cur, FALSE));
			cur = g_string_new(NULL);
		}
		else
			g_string_append_c(cur, c[0]);
	}
	if(cur->len > 0)
		g_ptr_array_add(items, g_string_free(cur, FALSE));
	else
		g_string_free(cur, TRUE);
	*n_out = items->len;
	g_ptr_array_add(items, NULL);
	g_free(raw);
	return (gchar **)g_ptr_array_free(items, FALSE);
}
```

(`g_warning` reçoit un texte formaté traduit : `g_warning("%s", msg)` avec `msg = g_strdup_printf(_("…"), …)` pour rester compatible `-Wformat-security`.)
Le motif de `g_test_expect_message` dans les tests vise « *invalid escape* » (texte anglais : les tests tournent sans catalogue). Liste vide : appeler
`gt_macro_lists_add_list(lists, name)` avant la boucle des entrées quand `name` n'est pas vide. Fichier illisible : dans `gt_macro_file_load`, si
`g_file_get_contents` échoue avec `G_FILE_ERROR_ACCES`/`PERM`, remplacer le message par `"%s: %s", path, g_strerror(errno_correspondant)` (EACCES/EPERM).
Message du refus de copie : `_("%s was not overwritten: it is not in the current macros format and its backup %s already exists with a different content. Rename or delete %s, then save again.")`
(msgid nouveau, `po/fr.po` à la tâche 13).

- [ ] **Step 4 : GREEN** — `meson test -C build test_macro_file test_macro_set test_macros_list test_macro_row test_macros_send --print-errorlogs`.

- [ ] **Step 5 : suite + ASan, commit** `macros : jeu ↔ groupes préfixés, copie de jeu, valeurs par défaut dans les lignes d'édition, remarques SP4`

### Task 4 : `GtFileStamp` (`Core`)

**Files:**
- Create: `src/file_stamp.c/.h`, `tests/test_file_stamp.c`
- Modify: `src/config_store.c` (`FileIdentity` → `GtFileStamp`), `src/meson.build`, `tests/meson.build` (`'test_file_stamp' : []`)

- [ ] **Step 1 : test** — `tests/test_file_stamp.c` :

```c
/* Tests de file_stamp.c : identité d'un fichier (absent, réécrit atomiquement, modifié sur place, inchangé). */

#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>

#include "file_stamp.h"

static void test_identite(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-stamp-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "f", NULL);
	GtFileStamp absent, a, b;
	FILE *f;

	gt_file_stamp_take(path, &absent);
	g_assert_false(absent.exists);
	g_assert_false(gt_file_stamp_changed(path, &absent));
	g_assert_true(g_file_set_contents(path, "un", -1, NULL));
	g_assert_true(gt_file_stamp_changed(path, &absent));
	gt_file_stamp_take(path, &a);
	g_assert_true(a.exists);
	g_assert_false(gt_file_stamp_changed(path, &a));
	g_assert_true(g_file_set_contents(path, "un", -1, NULL));	/* atomique : nouvel inode, même taille */
	g_assert_true(gt_file_stamp_changed(path, &a));
	gt_file_stamp_take(path, &b);
	f = fopen(path, "a");
	fputs("deux", f);
	fclose(f);
	g_assert_true(gt_file_stamp_changed(path, &b));	/* sur place : taille */
	g_assert_true(gt_file_stamp_equal(&absent, &absent));
	g_assert_false(gt_file_stamp_equal(&a, &absent));
	g_unlink(path);
	g_rmdir(dir);
	g_free(path);
	g_free(dir);
}

int main(int argc, char *argv[])
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/file_stamp/identite", test_identite);
	return g_test_run();
}
```

- [ ] **Step 2 : RED.**

- [ ] **Step 3 : implémenter** `src/file_stamp.c` :

```c
/* Identité d'un fichier (voir file_stamp.h). */
#include <sys/stat.h>

#include "file_stamp.h"

void gt_file_stamp_take(const gchar *path, GtFileStamp *out)
{
	struct stat st;

	*out = (GtFileStamp){ 0 };
	if(path == NULL || stat(path, &st) != 0)
		return;
	out->exists = TRUE;
	out->dev = (guint64)st.st_dev;
	out->ino = (guint64)st.st_ino;
	out->size = (gint64)st.st_size;
	out->mtime_sec = (gint64)st.st_mtim.tv_sec;
	out->mtime_nsec = (gint64)st.st_mtim.tv_nsec;
}

gboolean gt_file_stamp_equal(const GtFileStamp *a, const GtFileStamp *b)
{
	if(a->exists != b->exists)
		return FALSE;
	if(!a->exists)
		return TRUE;
	return a->dev == b->dev && a->ino == b->ino && a->size == b->size && a->mtime_sec == b->mtime_sec && a->mtime_nsec == b->mtime_nsec;
}

gboolean gt_file_stamp_changed(const gchar *path, const GtFileStamp *known)
{
	GtFileStamp now;

	gt_file_stamp_take(path, &now);
	return !gt_file_stamp_equal(&now, known);
}
```

`config_store.c` : supprimer `FileIdentity`, `identity_take`, `identity_equal` ; `GtFileStamp identity;` ; `gt_file_stamp_take(s->path, &s->identity)` ;
`gt_config_store_reload_if_changed` : `if(!gt_file_stamp_changed(s->path, &s->identity)) return TRUE;`.

- [ ] **Step 4 : GREEN** (`test_file_stamp`, `test_config_store`, `test_app_config`), suite + ASan.

- [ ] **Step 5 : commit** `cœur : GtFileStamp, identité de fichier partagée (magasin compris)`

### Task 5 : fichier projet `GtProjectData` (`Core`)

**Files:**
- Create: `src/project_file.c/.h`, `tests/test_project_file.c`
- Modify: `src/meson.build`, `tests/meson.build` (`'test_project_file' : []`), `po/POTFILES.in` (`src/project_file.c`)

- [ ] **Step 1 : tests** — `tests/test_project_file.c` (fonctions `deux_onglets()` qui construit un `GtProjectData` : onglet 0 série `/dev/pts/9`, profil
  `banc`, page `h_paned=420`, `macro_panel_visible=FALSE`, layout `leftright`, une macro `PING%d\r` args `{"5"}` polling 250 ms, une liste `Modes`
  (`Alpha=a`), script `"-- é\nprint(\"a;b\\\\c\")\n\n"` ; onglet 1 client TCP `127.0.0.1:4000`, jeu vide, script `""` ; `active = 1`) :
  - `/project_file/aller-retour` : save puis load → mêmes onglets (profil : transport, port, host, tcp_port, débit ; `profile_name`, page, layout, macros
    par `comparer_macro` recopié de `test_macro_file.c`, listes, script octet pour octet), `active == 1`, `stamp.exists`.
  - `/project_file/aucun-chemin` : le texte (`gt_project_data_to_text`) ne contient ni `macros_file` ni `.ini` ni `.lua` même si le profil source avait
    `macros_file = "/tmp/x.ini"`.
  - `/project_file/format` : le texte commence par `"[gtkterm-project]\nversion=1\nactive=1\n"` et contient `"[tab:0]\n"`, `"[tab:0:macro:1]\n"`,
    `"[tab:0:list:1]\n"`, `"[tab:1]\n"`, `"script_layout=leftright\n"`, `"profile_name=banc\n"`.
  - `/project_file/refus` (chaque cas : FALSE, bon code d'erreur, `out` resté NULL) : fichier absent (`G_FILE_ERROR_NOENT`) ; syntaxe invalide
    (`"[x"` → `GT_PROJECT_ERROR_NOT_PROJECT`) ; `.ini` de macros (`NOT_PROJECT`) ; `version=2` (`VERSION`) ; en-tête sans `[tab:0]` (`NO_TAB`) ; `[tab:0]`
    sans `transport` ou `transport=modem` (`BAD_TAB`, message citant l'onglet 1 en numérotation humaine).
  - `/project_file/active-hors-bornes` : `active=7` avec un onglet → `active == 0`.
  - `/project_file/instantane` : `snapshot(d)` ; changer `page.h_paned`, `page.hex_bar_visible`, `script_layout` et `active` → instantané **égal** ; changer
    un argument par défaut, la période de polling, une lettre du script, le débit, l'ordre des onglets → instantané **différent**.

- [ ] **Step 2 : RED.**

- [ ] **Step 3 : implémenter** `src/project_file.c` (tabulations, Allman). Points précis :
  - Écriture (`write_key_file(d, with_page)`) : `[gtkterm-project]` `version` (entier), `active` (si `with_page`) ; pour chaque onglet `i` : groupe
    `g_strdup_printf("tab:%u", i)` ; `profile_name` (`g_key_file_set_string`), puis profil par `gt_profile_to_key_file` d'une **copie** dont `macros_file`
    est NULL ; si `with_page` : `h_paned`, `v_paned` (entiers), `macro_panel_visible`, `script_panel_visible`, `hex_bar_visible` (booléens),
    `script_layout` ; `script` (`g_key_file_set_string`, échappe `\n`, `\\`, etc.) ; puis `gt_macro_set_to_key_file(t->macros, kf, "tab:%u:")`.
    `gt_project_data_to_text` = `write_key_file(d, TRUE)` → `g_key_file_to_data` ; `gt_project_data_snapshot` = `write_key_file(d, FALSE)`.
  - `profile_name` est écrit avant les clés du profil : `g_key_file_set_string` ajoute dans l'ordre d'appel (format figé par `/project_file/format`).
  - Lecture : `g_file_get_contents` (erreur : `g_prefix_error(error, "%s: ", path)` sauf si le message cite déjà le chemin) ; `g_key_file_load_from_data`
    (échec → `GT_PROJECT_ERROR_NOT_PROJECT`, `_("%s is not a GTKTerm project")`) ; pas de groupe `gtkterm-project` → idem ; `version` (valeur rognée) ≠ "1" →
    `GT_PROJECT_ERROR_VERSION`, `_("%s: unsupported project version (written by a newer GTKTerm?)")` ; onglets lus pour `n = 0, 1, …` tant que `tab:n`
    existe ; aucun → `GT_PROJECT_ERROR_NO_TAB` `_("%s: the project has no tab")` ; pour chacun, `transport` doit exister et `gt_transport_from_string`
    réussir, sinon `GT_PROJECT_ERROR_BAD_TAB` `_("%s: tab %u has no valid port settings")` (n + 1) ; profil = `gt_profile_init_defaults` +
    `gt_profile_from_key_file` + `macros_file` NULL ; `profile_name` absent ou vide → « default » ; page : défauts de `gt_project_tab_new` puis
    `gt_key_file_read_int`/`gt_key_file_read_bool` ; `script_layout` ∉ {topbottom, leftright} → « topbottom » ; `script` absent → "" (lecture par
    `g_key_file_get_string`, échappement invalide → `g_key_file_get_value` brut) ; macros par `gt_macro_set_from_key_file(kf, "tab:%u:", path)`. Tout
    est construit dans un `GtProjectData` local libéré en cas d'échec ; `*out` n'est écrit qu'au succès.
  - Enregistrement : `g_file_set_contents_full(path, text, -1, G_FILE_SET_CONTENTS_CONSISTENT, 0644, error)` puis `gt_file_stamp_take(path, stamp)`.

- [ ] **Step 4 : GREEN**, suite + ASan.

- [ ] **Step 5 : commit** `cœur : fichier projet .gtkterm (GtProjectData), lecture tout ou rien, instantané de comparaison`

### Task 6 : `GtMacros` — valeurs d'usage, mode intégré, garde d'identité (`Feature`)

**Files:**
- Modify: `src/macros.c/.h` (GNU 2 espaces), appelants de `gt_macros_save_file` (`src/ui/macro_panel.c`, `src/ui/macro_editor.c`,
  `src/ui/profile_dialogs.c` : adaptation mécanique à la nouvelle signature, `TRUE`/`FALSE` → `!= GT_MACROS_FAILED`… comportement inchangé, `force` FALSE)
- Test: `tests/test_macros_send.c`

- [ ] **Step 1 : tests** (`test_macros_send.c`, style du fichier ; la liaison bouchon enregistre les octets envoyés) :
  - `/macros/usage-prioritaire` : macro `N%d;` args par défaut `{"5"}` ; `gt_macros_send(m, id, NULL, -1)` → `N5;` ; `gt_macros_set_usage_arg(m, id, 0, "9")`
    → `N9;` ; `gt_macro_set_find(set, id)->args[0]` toujours `"5"` ; `gt_macros_reset_usage(m, id)` → `N5;`.
  - `/macros/usage-jamais-enregistre` : fichier `.ini` chargé ; usage changé ; `gt_macros_save_file(m, NULL, FALSE, FALSE)` → le fichier contient
    `args=5;` (pas `9`).
  - `/macros/usage-efface-au-remplacement` : usage posé ; `gt_macros_replace_set(m, gt_macro_set_copy(set))` → `has_usage` FALSE pour tout id.
  - `/macros/usage-nombre-change` : usage `{"9"}` puis action passée à `N%d %d` par `apply_edits` → `gt_macros_get_args` rend les défauts (NULL ici), pas
    l'usage d'un seul élément.
  - `/macros/integre-aucune-ecriture` : `gt_macros_set_embedded(m, TRUE)` après un chargement de fichier → `gt_macros_get_path(m) == NULL` ;
    `gt_macros_save_file(m, NULL, FALSE, FALSE)` → `GT_MACROS_FAILED` et aucun fichier créé (`g_test_expect_message` WARNING « *no macros file* ») ;
    `gt_macros_save_file(m, autre, FALSE, FALSE)` → `SAVED`, `autre` écrit, `get_path` toujours NULL ; `gt_macros_load_file(m, autre, FALSE)` → jeu
    importé, `get_path` NULL.
  - `/macros/conflit` : fichier chargé ; réécrit par un tiers (`g_file_set_contents`) ; `gt_macros_save_file(m, NULL, FALSE, FALSE)` → `CONFLICT`,
    contenu du tiers intact, un `g_warning` attendu ; `force` TRUE → `SAVED` ; nouveau tiers puis `gt_macros_reload(m)` → jeu relu, puis `save` →
    `SAVED` (identité reprise à la relecture).
  - `/macros/edited` : `gt_macros_set_edited_func` compte les appels ; `gt_macros_changed`, `gt_macros_edited`, `gt_macros_replace_set` → +1 chacun.

- [ ] **Step 2 : RED.**

- [ ] **Step 3 : implémenter** (`macros.c`) :
  - Champs : `GHashTable *usage; /* id → gchar ** (possédés) */`, `gboolean embedded;`, `GtFileStamp stamp;`, `void (*edited)(gpointer); gpointer edited_data;`.
  - `gt_macros_set_usage_arg` : macro introuvable ou `arg_index` hors `[0, macro_count_format_args(lists, action))` → rien ; sinon vecteur créé depuis les
    défauts (ou `""`) s'il manque ou n'a pas le bon nombre, élément remplacé. `gt_macros_get_args` : usage de bon nombre, sinon `macro->args`.
  - `send_macro` : si `n_args < 0`, `args = gt_macros_get_args(m, id)` et `n_args = args ? g_strv_length(args) : 0`, puis `gt_macro_expand(…, args, n_args)`
    (jamais -1 transmis : les défauts sont déjà résolus).
  - `gt_macros_replace_set` : `g_hash_table_remove_all(m->usage)` avant `gt_macros_changed`.
  - `gt_macros_changed` et `gt_macros_edited` appellent `m->edited(m->edited_data)` s'il est posé ; `gt_macros_changed` garde le crochet `macros_changed`.
  - `gt_macros_set_embedded(m, TRUE)` : `g_clear_pointer(&m->path, g_free)`, `stamp` remis à zéro.
  - `gt_macros_load_file` : en mode intégré, `m->path` n'est pas remplacé ; sinon identité prise **après** la lecture réussie (`gt_file_stamp_take`), et
    remise à « absent » pour un fichier absent.
  - `gt_macros_save_file(m, path, interactive, force)` : cible = `path` ou chemin courant ou (si non intégré) défaut ; en mode intégré sans `path` :
    `report_error(interactive, _("This tab has no macros file: its macros are saved with the project."))` → `FAILED` (le test attend « *no macros file* ») ;
    conflit si `!force`, cible == `m->path` (`g_strcmp0`) et `gt_file_stamp_changed(cible, &m->stamp)` → non interactif : `g_warning(_("%s changed on disk: not overwritten"), cible)` ;
    rend `CONFLICT` sans écrire. Succès : identité reprise ; `m->path` remplacé seulement hors mode intégré. `gt_macros_reload` :
    `m->path ? gt_macros_load_file(m, m->path, TRUE) : FALSE`.
  - Appelants : `save_macros()` du panneau et `apply_edits()` de l'éditeur : `gt_macros_save_file(…, NULL, TRUE, FALSE)` (le conflit sera traité à la
    tâche 9) ; `on_macros_save_chosen` : `gt_macros_save_file(…, path, TRUE, FALSE) == GT_MACROS_SAVED`.

- [ ] **Step 4 : GREEN** (`test_macros_send`, `test_script_engine`, `test_session`, `test_app_config`), suite + ASan + **suite graphique** (appelants UI).

- [ ] **Step 5 : commit** `macros : valeurs d'usage jamais enregistrées, mode intégré, garde d'identité du fichier (conflit)`

### Task 7 : session de projet, `last_project`, profils dans un onglet de projet, `--project` (`Feature`)

**Files:**
- Modify: `src/session.c/.h`, `src/app_config.c/.h`, `src/cmdline.c/.h`
- Test: `tests/test_session.c`, `tests/test_app_config.c`, `tests/test_cmdline.c`

- [ ] **Step 1 : tests**
  - `test_session.c` : `/session/in-project` (défaut FALSE ; `set_in_project(TRUE)` → `gt_macros_is_embedded`, notify une fois, `edited` émis) ;
    `/session/script-text` (défaut `""` ; même texte → pas de notify ; autre → notify + `edited`) ; `/session/edited` (compte de `edited` : +1 pour
    `g_object_set(options, "local-echo", TRUE)`, +1 pour `auto-reconnect` de la liaison, +1 pour `gt_session_apply_profile`, +1 pour `gt_macros_changed`,
    0 après `gt_session_shutdown` sur `gt_session_edited`).
  - `test_app_config.c` : `/app_config/last-project` (absent → NULL ; `set("/a/b.gtkterm")` → écrit dans `[general] last_project` du fichier ; relu ; `set(NULL)`
    → clé retirée) ; `/app_config/profil-dans-projet` (profil `p` avec `macros_file=m.ini` existant ; session `in-project` avec un jeu d'une macro ;
    `app_config_load_profile(s, "p")` → débit du profil appliqué, **jeu inchangé** (une macro), `gt_macros_get_path` NULL ; `config_remember_macros_file(s)`
    ne change pas le fichier ; `app_config_save_profile(s, "p")` garde `macros_file=…m.ini` dans le fichier).
  - `test_cmdline.c` : `/cmdline/project` (`--project rel.gtkterm` → `options.project == "rel.gtkterm"` ; `read_command_line` → `cmd.project` absolu
    = `g_canonicalize_filename("rel.gtkterm", NULL)`, `profile_given` FALSE) ; `/cmdline/project-refuse` (`--project x -p /dev/pts/1` et `--project x -c a`
    → `gt_cmdline_parse` FALSE, `GT_CMDLINE_ERROR_INVALID_VALUE`, message contenant `--project`) ; `/cmdline/profile-given` (`-s 9600` → TRUE ; `-L` seul →
    FALSE ; `-c a` → TRUE).

- [ ] **Step 2 : RED.**

- [ ] **Step 3 : implémenter**
  - `session.c` : propriétés `PROP_IN_PROJECT` (lecture/écriture) et `PROP_SCRIPT_TEXT` (lecture/écriture), signal `edited` (`G_SIGNAL_RUN_LAST`, aucun
    argument). Dans `gt_session_new` : `g_signal_connect_swapped(options, "changed", G_CALLBACK(gt_session_edited), s)`,
    `g_signal_connect_swapped(link, "notify::auto-reconnect", …)`, `gt_macros_set_edited_func(macros, (void (*)(gpointer))gt_session_edited, s)` ;
    `gt_session_apply_profile` et `gt_session_open_port` finissent par `gt_session_edited(s)` ; `gt_session_edited` ne fait rien après `shutdown`.
    Ces gestionnaires sont déconnectés dans `gt_session_shutdown` (`g_signal_handlers_disconnect_by_data`), `gt_macros_set_edited_func(macros, NULL, NULL)`.
  - `app_config.c` : `app_config_get_last_project` / `app_config_set_last_project` (clé `last_project` de `[general]`, via
    `gt_config_store_get_general_string` / `set_general_string`, retrait par `g_key_file_remove_key(gt_config_store_get_key_file(store), "general",
    "last_project", NULL)` ; relire avant, `config_save_store(FALSE)` après ; ne rien écrire si la valeur est déjà celle du fichier).
    `load_profile` : si `gt_session_get_in_project(s)`, ne pas appeler `gt_macros_load_file` et vider `profile->macros_file`.
    `config_remember_macros_file` : `return` d'entrée si `in-project`. `app_config_save_profile` : si `in-project`, `macros_file` écrit = celui que le profil
    `name` a déjà dans le magasin (lu par `gt_config_store_get_profile` dans un `GtProfile` temporaire après la relecture ; NULL si le profil n'existe pas).
  - `cmdline.c` : option longue `{"project", 1, 0, 'j'}` (valeur interne `'j'`, pas d'option courte dans la chaîne `getopt`) → `gt_profile_set_string(&out->project,
    optarg)` ; après la boucle, si `out->project != NULL && (out->config_name != NULL || (out->set & ~GT_CMDLINE_NO_LOCK) != 0)` → erreur
    `_("--project cannot be combined with --config or port settings")`. `display_help` : ligne `_("--project <file>: open this project\n")`.
    `read_command_line` : `out->project = project ? g_canonicalize_filename(project, NULL) : NULL`,
    `out->profile_given = config_name != NULL || (set & ~GT_CMDLINE_NO_LOCK) != 0` ; `gt_cmdline_clear` libère `project`.

- [ ] **Step 4 : GREEN**, suite + ASan + suite graphique (`app_config.c`, `cmdline.c` touchés).

- [ ] **Step 5 : commit** `sessions : onglet de projet (in-project, script-text, signal edited), last_project, profils sans macros dans un projet, --project`

### Task 8 : `GtProject` (`Feature`)

**Files:**
- Create: `src/project.c/.h`, `tests/test_project.c`
- Modify: `src/meson.build` (`app_layer_sources`), `tests/meson.build` (`app_tests` : `'test_project' : [util_dep]`), `po/POTFILES.in` (`src/project.c`)

- [ ] **Step 1 : tests** — `tests/test_project.c` (HOME/XDG privés comme `test_session.c` ; deux sessions sur des ptys `openpty`, ajoutées à un
  `GtSessionManager` ; boucle principale itérée par `wait_until` maison, `g_main_context_iteration`) :
  - `/project/ferme` : `gt_project_new` → `is_open` FALSE, `get_name` `""`, `modified` FALSE.
  - `/project/capture` : session A (profil `banc`, écho vrai par `g_object_set(options, "local-echo", TRUE)`, jeu d'une macro args `{"5"}`, usage `{"9"}`,
    `script-text` « x »), session B ; B active ; page func qui met `h_paned = 100 + id` → `tabs->len == 2`, `active == 1`, onglet 0 : `profile.echo` TRUE,
    `profile_name == "banc"`, `macros` copie (args `{"5"}`, pas `9`), `script == "x"`, `page.h_paned == 101` ; `profile.macros_file == NULL`.
  - `/project/begin-non-modifie` : `gt_project_begin(p, NULL, NULL, NULL)` → `open`, `path` NULL, `get_name == "Untitled project"`, `modified` FALSE ;
    sessions `in-project`.
  - `/project/modifie` : après `begin`, chacun des changements suivants rend `modified` TRUE après itération (≤ 1 s), puis `gt_project_save` le remet à FALSE :
    option (`local-echo`), macros (`gt_macros_changed` après `gt_macro_set_set_polling`), script (`set_script_text`), ajout d'une session au gestionnaire
    (elle passe `in-project`), déplacement (`gt_session_manager_move`). Ne rend **pas** modifié : changer le `h_paned` renvoyé par la page func, changer
    la session active, une valeur d'usage.
  - `/project/enregistrer` : `gt_project_save(p, path, FALSE, &err)` → `SAVED`, `path` retenu, fichier lisible par `gt_project_data_load` (2 onglets) ;
    tiers réécrit le fichier → `gt_project_save(p, NULL, FALSE, …)` → `CONFLICT`, fichier du tiers intact ; `force` → `SAVED`. Dossier non inscriptible →
    `FAILED` + GError (skip si root).
  - `/project/begin-avec-donnees` : `saved` = capture dont on change le débit de l'onglet 0 → après `begin(p, path, &stamp, saved)`, `modified` TRUE
    (profil corrigé à l'ouverture) ; `saved` = capture exacte → FALSE.
  - `/project/end` : `gt_project_end` → `open` FALSE, notifications `open`, `path`, `modified` émises seulement si elles changent.

- [ ] **Step 2 : RED.**

- [ ] **Step 3 : implémenter** `src/project.c` (GObject final, tabulations) :
  - Champs : `GtSessionManager *sessions` (référence), `gboolean open`, `gchar *path`, `GtFileStamp stamp`, `gchar *saved_snapshot`, `gboolean modified`,
    `GtProjectPageFunc page_func; gpointer page_data;`, `guint check_id` (source), `gint64 last_check_us`, `gulong items_id`, `GHashTable *watched`
    (session → id du gestionnaire `edited`).
  - `items-changed` du gestionnaire : relier `edited` des nouvelles sessions (et `in-project` TRUE si le projet est ouvert), oublier celles qui sont parties
    (`g_hash_table` ; déconnexion par l'identifiant) ; puis `queue_check`. Une session arrêtée (`gt_session_is_shut_down`) est ignorée par la capture.
  - `queue_check` : si `!open` rien ; si `check_id == 0`, `g_timeout_add_full(G_PRIORITY_DEFAULT_IDLE, délai, …)` avec délai = `MAX(0, 300 ms - (maintenant
    - last_check))` ; le rappel appelle `gt_project_check_now`.
  - `gt_project_check_now` : `open` ? capture → `gt_project_data_snapshot` → `modified = g_strcmp0(snap, saved_snapshot) != 0` ; notifier si changé ;
    `last_check_us = g_get_monotonic_time()`.
  - `gt_project_capture` : pour chaque session non arrêtée : `gt_profile_copy(&t->profile, gt_session_get_profile(s))`,
    `gt_term_options_store_profile(gt_session_get_options(s), &t->profile)`, `auto_reconnect` lu sur la liaison (`g_object_get`), `macros_file` NULL ;
    `profile_name` ; `gt_macro_set_copy(gt_macros_get_set(macros))` ; `script` = `gt_session_get_script_text` ; page func si posée ; `active` = index de la
    session active (0 si aucune).
  - `gt_project_save` : chemin = `path` ou `p->path` (NULL des deux : `g_set_error(G_FILE_ERROR_INVAL, _("The project has no file"))`, `FAILED`) ; conflit
    si `!force`, chemin == `p->path` et `gt_file_stamp_changed(chemin, &p->stamp)` ; capture → `gt_project_data_save(d, chemin, &p->stamp, error)` ; succès :
    `path` notifié s'il change, `saved_snapshot` = `gt_project_data_snapshot(d)`, `modified` FALSE.
  - `gt_project_begin` : `open` TRUE, `path`/`stamp` copiés, chaque session `gt_session_set_in_project(s, TRUE)`, `saved_snapshot` = snapshot de `saved` ou
    de la capture, puis `gt_project_check_now`.
  - `gt_project_get_name` : `_("Untitled project")` ou `g_path_get_basename` sans suffixe `.gtkterm`.
  - `dispose` : sources retirées, gestionnaires déconnectés, références rendues.

- [ ] **Step 4 : GREEN**, suite + ASan.

- [ ] **Step 5 : commit** `projets : GtProject (capture des onglets, instantané, état modifié, enregistrement avec garde d'identité)`

### Task 9 : panneau et éditeur de macros — arguments d'usage, valeurs par défaut, conflit Overwrite/Reload/Cancel (`Interface`)

**Files:**
- Create: `src/ui/macros_save.c/.h`, `tests/gui/test_macros_conflit.py`
- Modify: `src/ui/macro_panel.c`, `src/ui/macro_editor.c`, `src/ui/macro_row.c/.h`, `src/ui/profile_dialogs.c`, `src/ui/meson.build`,
  `tests/gui/test_macros_arguments.py` (réécrit), `tests/gui/meson.build`, `tests/test_macro_row.c`, `po/POTFILES.in`

`src/ui/macros_save.h` :

```c
/* Enregistrement des macros d'une session depuis l'interface (6c, spec §3.2-15) : onglet de projet → rien d'écrit (gt_macros_edited) ; onglet
 * libre → fichier courant (ou path), et en cas de conflit question Overwrite / Reload / Cancel sur parent ; le profil actif retient le fichier. */
#ifndef GT_UI_MACROS_SAVE_H
#define GT_UI_MACROS_SAVE_H

#include <gtk/gtk.h>

#include "session.h"

void gt_ui_macros_save(GtkWindow *parent, GtSession *s, const gchar *path);	/* path NULL : fichier courant */

#endif
```

Comportement : session `in-project` et `path == NULL` → `gt_macros_edited(m)` ; `in-project` et `path` → export
`gt_macros_save_file(m, path, TRUE, TRUE)` ; sinon `gt_macros_save_file(m, path, TRUE, FALSE)` : `SAVED` → `config_remember_macros_file(s)` ;
`CONFLICT` → `gt_ui_choose(parent, _("The macros file changed on disk."), chemin, { _("Overwrite"), _("Reload"), _("Cancel"), NULL }, …)` avec une
`GWeakRef` sur la session : 0 → `gt_macros_save_file(m, NULL, TRUE, TRUE)` + remember ; 1 → `gt_macros_reload(m)` ; 2 ou -1 → rien.

- [ ] **Step 1 : tests graphiques et unitaires**
  - `test_macro_row.c` `/macro_row/set-args` : `gt_macro_row_set_args(row, {"1",NULL})` → `notify::args` une fois, `get_args` égal ; même valeur → pas de
    notify.
  - `tests/gui/test_macros_arguments.py` réécrit (même jeu de macros que l'actuel) :
    1. Choix « Gamma » dans la liste de `Choix` → envoi `Cg;` ; **le `.ini` n'a pas changé** (contenu comparé à celui du départ, `st_mtime_ns` identique).
    2. Entrée `abc` → classe `error` ; `12` + Entrée → envoi `N12;` ; fichier inchangé.
    3. Clic droit sur le bouton `Num` → popover ; bouton `macro-reset-args` (« Reset arguments ») → champ `macro-<id>-arg-0` revient à `5`, envoi `N5;`.
    4. Configure Macros (`app.macros`) : sélectionner la ligne `Num` (clic sur sa cellule label) → zone `macro-defaults` visible avec `macro-default-0`
       = `5` ; `set_text('macro-default-0', '7')` ; OK → `args=7;` dans le `.ini` ; le champ du panneau affiche `7` ; envoi `N7;`.
    5. Redémarrage (`app.quit` puis `e.start()`) : champ `7` (défaut enregistré), `Choix` présélectionné sur `b` (défaut du fichier, pas `g`).
    6. Liste de deux valeurs : deux boutons `L Lent` / `L Vif` (inchangé).
  - `tests/gui/test_macros_conflit.py` : `.ini` commun à deux onglets libres (profil `default` avec `macros_file`) ; onglet 2 par `open_tab` ; polling d'une
    macro activé dans l'onglet 1 (popover, case `polling-mode`) → fichier écrit ; le test réécrit le fichier (macro `TIERS`) ; dans l'onglet 2, changement de
    période (popover, `polling-period` = `500`, Entrée) → boîte avec « Overwrite », « Reload », « Cancel » ; Cancel → fichier = version du tiers ;
    recommencer, Reload → le panneau de l'onglet 2 montre `TIERS` ; recommencer après une nouvelle écriture du tiers, Overwrite → fichier contient
    `polling_period_ms=500`.
  Enregistrer les deux scénarios dans `tests/gui/meson.build` (section « raccourcis et panneau de macros » : `test_macros_conflit`).

- [ ] **Step 2 : RED** — `meson test -C build --suite gui test_macros_arguments test_macros_conflit` échouent.

- [ ] **Step 3 : implémenter**
  - `macro_panel.c` : `save_macros()` → `gt_ui_macros_save(gtk_widget_get_root(panel->box) as GtkWindow, panel->session, NULL)` (polling seulement) ;
    `commit_arg` : valeur comparée à `gt_macros_get_args(m, id)[k]` et écrite par `gt_macros_set_usage_arg` ; **aucun** appel d'enregistrement dans
    `on_entry_activate`, `on_entry_focus_leave`, `on_dropdown_selected`, `on_args_button_clicked` (renommer `commit_arg` → bool inutile : garder le
    retour pour savoir s'il faut rien de plus). `make_arg_widget` : valeur initiale = `gt_macros_get_args(macros, m->id)` (signature : passer `GtMacros *`).
    Popover de polling : sous la période, bouton « Reset arguments » nommé `macro-reset-args`, sensible si la macro a au moins un argument de format ;
    clic → `gt_macros_reset_usage(m, id)`, popover fermé, `gt_macro_panel_rebuild(panel->box)`.
  - `macro_row.c/.h` : `gt_macro_row_set_args(GtMacroRow *row, const gchar *const *args)` (notify seulement si différent ; NULL permis).
  - `macro_editor.c` : sous la vue des macros (page Macros), une `GtkBox` horizontale nommée `macro-defaults` : étiquette « Default values: », puis un champ
    par argument de format de la **seule** ligne sélectionnée (masquée si 0 ou plusieurs lignes, ou macro sans argument). Infos d'arguments :
    `macro_get_arg_infos(lists, gt_macro_row_get_action(row), &n)` avec `lists = gt_macro_list_rows_to_lists(editor->list_rows)` (libérées après). Liste
    (`type "l"`) → `GtkDropDown` des affichages de la liste (valeur choisie = `values[sel]`) ; autre → `GtkEntry` validée par `macro_type_value_valid` (classe
    `error`). Champs nommés `macro-default-%d`. Valeur initiale : `get_args(row)[k]` si le nombre correspond, sinon `""`. Chaque changement → vecteur complet
    des valeurs → `gt_macro_row_set_args(row, v)`. Reconstruction sur `selection-changed`, sur `notify::action` de la ligne affichée (gestionnaire
    déplacé quand la ligne change) et sur `items-changed` de `list_rows`. `apply_edits` : `gt_ui_macros_save(GTK_WINDOW(editor->window) parent principal,
    s, NULL)` remplace `gt_macros_save_file` + `config_remember_macros_file`. Texte d'aide (`help_text`) : la phrase « its value is remembered in the
    macros file » devient « the value typed there is used for sending but never saved; set default values below the table ».
  - `profile_dialogs.c` : « Save macros file » → `gt_ui_macros_save(parent, s, path)` ; « Load macros file » inchangé (en mode intégré,
    `gt_macros_load_file` importe déjà sans retenir le chemin, et `config_remember_macros_file` ne fait rien).

- [ ] **Step 4 : GREEN** — scénarios ci-dessus, puis `meson test -C build --suite gui` complet (les scénarios de macros existants `test_macros_edition`,
  `test_polling`, `test_onglets_macros_fichier` doivent rester verts : ils vérifient l'écriture du polling et de l'éditeur, inchangée pour un onglet libre).

- [ ] **Step 5 : suite + ASan, commit** `macros (interface) : arguments d'usage non enregistrés, Reset arguments, valeurs par défaut dans l'éditeur, conflit Overwrite/Reload/Cancel`

### Task 10 : panneau de script et page en mode projet (`Interface`)

**Files:**
- Modify: `src/ui/script_panel.c/.h`, `src/ui/session_view.c/.h`, `src/ui/terminal_dialog.c`

- [ ] **Step 1 : ce qui doit changer** (vérifié par les scénarios de la tâche 11 ; ici `test_script_*`, `test_onglets_lua` restent verts) :
  - `script_panel.c` : le tampon de l'éditeur pousse son texte dans la session à chaque `changed` (`gt_session_set_script_text(s, texte)`), et une fois à
    la création après le chargement du fichier. À la création, si `gt_session_get_in_project(s)` : texte = `gt_session_get_script_text(s)`, `script_path`
    NULL, `[session]` non lu. `store_state` : `[session] script_path` écrit seulement si la session n'est pas `in-project`. « Open… » : en projet, le
    chemin choisi n'est pas retenu (sert seulement de dossier initial) ; « Save » : en projet, toujours le dialogue d'enregistrement, chemin non retenu.
    Nouvelle fonction : `void gt_script_panel_set_layout(GtkWidget *panel, const gchar *layout); /* "topbottom" | "leftright" : comme app.script-layout */`
    et `const gchar *gt_script_panel_get_layout(GtkWidget *panel);`.
  - `session_view.c/.h` : `void gt_session_view_fill_project_tab(GtkWidget *view, GtProjectTab *tab);` (`gt_session_view_get_state(view, &tab->page)`,
    `g_free(tab->script_layout); tab->script_layout = g_strdup(gt_script_panel_get_layout(slot script-panel))`) et
    `void gt_session_view_apply_project_tab(GtkWidget *view, const GtProjectTab *tab);` (disposition du panneau de script ; séparateurs et visibilités sont
    déjà passés à `gt_session_view_new` par `st`).
  - `terminal_dialog.c` : après chaque modification du profil (police, défilement, curseur, couleurs), `gt_session_edited(s)`.

- [ ] **Step 2 : implémenter**, `ninja -C build`, `meson test -C build --suite gui test_script_couleurs test_script_arret test_onglets_lua test_terminal_dialog`.

- [ ] **Step 3 : suite + ASan, commit** `script : texte de l'éditeur tenu par la session, mode projet (sans script_path), état de page pour le projet`

### Task 11 : actions de projet, menus réorganisés, titre, démarrage, fermeture (`Feature`)

**Files:**
- Modify: `src/ui/app.c/.h`, `src/ui/menus.ui`, `src/ui/test_hooks.c`, `src/ui/app_actions.c` (si un raccourci y est déclaré), `tests/gui/gtktest.py`,
  `tests/gui/test_menus_actions.py`, `tests/gui/meson.build`
- Create: `tests/gui/test_projet.py`, `tests/gui/test_projet_fermeture.py`

- [ ] **Step 1 : harnais et scénarios**
  - `gtktest.py` : `Env.project()` (lit `app.test-project` comme `sessions()` lit `test-sessions` : `Activate` puis `Describe`, dict python) ;
    `Env.project_path(name)` = chemin dans le HOME privé ; `save_project_as(env, path)` (active `project-save-as`, puis `file_dialog(env, 'Save project', path)`) ;
    `open_project(env, path)` (active `project-open`, `file_dialog(env, 'Open project', path)`).
  - `test_menus_actions.py` : l'étape 3 ouvre le menu « Terminal » (au lieu de « Configuration ») ; ajouter : chaque `submenu` de premier niveau de
    `menus.ui` a, dans l'ordre, les libellés `File, Edit, View, Port, Terminal, Macros, Log, Help` ; `Describe` de `project-new`, `project-open`,
    `project-save`, `project-save-as`, `project-close` (sans paramètre ni état) ; raccourci Ctrl+Maj+O ouvre une fenêtre de titre « Open project »
    (puis Échap).
  - `tests/gui/test_projet.py` :
    1. Démarrage sur pty A, profil `default` ; macros chargées depuis un `.ini` (`macro_config`) : `PING%d\r` défaut `5` ; `set_text` du script
       (champ `script-editor` via `app.test-set-text`) `print("a")` ; onglet 2 sur pty B par `open_tab` ; `project()['open']` FALSE ; titre `GTKTerm - …`.
    2. Save project as `banc.gtkterm` → `project()` : `open`, `path` se termine par `banc.gtkterm`, `modified` FALSE ; titre `banc - <nom court de B>` ;
       `sessions()` : `in_project` partout ; `[general] last_project` = ce chemin dans `.gtktermrc` ; le fichier contient `[tab:1]`, `PING%d`, `print(\"a\")`
       et aucune de ces chaînes : `.ini`, `macros_file`.
    3. `set_state('local-echo', True)` → titre `banc • - …`, `modified` TRUE ; `project-save` → `•` disparaît.
    4. Changer l'onglet actif (`active-tab` 0) : `modified` reste FALSE.
    5. `project-close` → un seul onglet, `open` FALSE, titre `GTKTerm - …`, `last_project` absent du fichier.
    6. `open_project(banc.gtkterm)` → deux onglets (ports A et B, état `open`), onglet actif 1, macros de l'onglet 0 (`macro_id(e, 'PING%d')` trouvé après
       `active-tab` 0), `script` de l'onglet 0 == `print("a")`, `modified` FALSE ; envoi de la macro → `PING5\r` sur A.
    7. Ouverture d'un fichier abîmé (`[gtkterm-project]\nversion=9\n`) → boîte d'erreur, les deux onglets sont toujours là.
  - `tests/gui/test_projet_fermeture.py` : projet enregistré puis modifié (écho) ; `app.quit` → boîte avec « Save », « Don't save », « Cancel » ; Cancel →
    fenêtre toujours là ; `app.quit` puis « Don't save » → processus terminé (code 0), fichier projet inchangé ; nouveau lancement (dernier projet rouvert)
    modifié puis `app.quit` → « Save » → fichier contient `echo=true`.
  Ajouter `test_projet`, `test_projet_fermeture` dans une section « projets (sous-projet 6c) » de `tests/gui/meson.build`.

- [ ] **Step 2 : RED.**

- [ ] **Step 3 : implémenter**
  - `menus.ui` : structure exacte de la spec §3.2-10 (libellés : File › « New project », « Open project…», « Save project », « Save project as… »,
    « Close project » | « Send RAW file… », « Save RAW file… », « Save ASCII file… » | « Quit » ; Edit › « Copy », « Paste », « Select All », « Find » |
    « Clear screen », « Clear scrollback » ; View inchangé ; Port › « New port… » (`app.new-tab`), « Close tab » | « Settings… » (`app.port`),
    « Open Port », « Close Port », « Autoreconnect » | « Send break », « Toggle DTR », « Toggle RTS » | « Load profile… », « Save profile… »,
    « Delete profile… » ; Terminal › « Settings… » (`app.terminal`), « Local echo », « CR LF auto », « ESC clear screen », « Timestamp » ; Macros ›
    « Configure macros… », « Load macros file… », « Save macros file… » ; Log et Help inchangés). Le menu contextuel ne change pas.
  - `app.c` : état `GtProject *project` (créé dans `gt_app_new` sur le gestionnaire, rendu à la finalisation), page func =
    `gt_session_view_fill_project_tab(gt_session_view_lookup(s), tab)` ; actions `project-new|open|save|save-as|close` (`app_entries`), raccourci
    `{ "app.project-open", "<Control><Shift>o" }`.
    Opération en attente (`typedef enum { OP_NONE, OP_NEW, OP_OPEN, OP_CLOSE, OP_QUIT } PendingOp;` + `GtProjectData *pending_data; gchar *pending_path;
    GtFileStamp pending_stamp;` dans `GtApp`) et enchaînement : `request(op)` → si une session `gt_session_is_busy` : `gt_ui_confirm(_("Close all tabs?"),
    _("A script is running or a file is being sent."), _("Close tabs"))` → `gt_project_check_now` ; si `open && modified` : `gt_ui_choose(_("Save changes to
    the project?"), nom, { _("Save"), _("Don't save"), _("Cancel") })` → Save : `project_save_then(op)` (sans chemin : dialogue Save as d'abord ; échec ou
    annulation : l'opération est abandonnée) → `perform(op)`.
    `replace_all_tabs(data, path, stamp)` : toutes les pages arrêtées puis sessions arrêtées et retirées (`close_session_now` sans la condition « plus d'un
    onglet », factorisée en `remove_session(s)`), puis pour chaque onglet `i` : `s = gt_session_new(&t->profile, t->profile_name)`,
    `gt_session_set_in_project(s, TRUE)`, `gt_macros_replace_set(gt_session_get_macros(s), gt_macro_set_copy(t->macros))`,
    `gt_session_set_script_text(s, t->script)`, `Verify_configuration(s)` (profil corrigé et averti comme un profil chargé), `gt_session_manager_add`,
    page (`session_page_new(s, &t->page, NULL, -1)` — la première page lit `[layout]` pour les positions ; `gt_session_view_apply_project_tab`), profil
    appliqué, port ouvert ; puis onglet `data->active` affiché, `gt_project_begin(project, path, stamp, data)`, `app_config_set_last_project(path)` (si
    `path`). Les relais (`install_relays`) existent depuis `activate` : seul le reciblage suit `notify::active`.
    `perform(OP_NEW)` : données d'un onglet (`gt_project_tab_new`, profil `default` lu par `app_config_has_profile("default")` puis
    `gt_config_store_get_profile`, sinon défauts) → `replace_all_tabs(d, NULL, NULL)` ; `OP_CLOSE` : `replace_all_tabs` d'un onglet libre (même profil,
    mais **sans** `set_in_project` ni `begin` : `gt_project_end`, `app_config_set_last_project(NULL)`, et le fichier de macros du profil chargé par
    `gt_macros_load_file(…, FALSE)`) ; `OP_OPEN` : `replace_all_tabs(pending_data, pending_path, &pending_stamp)` ; `OP_QUIT` :
    `gtk_window_close` avec le drapeau `close_confirmed`.
    `project-open` : `gt_ui_open_file(_("Open project"), filtre « GTKTerm projects » `*.gtkterm` + « All files »)` → `gt_project_data_load` (échec :
    `gt_ui_message(error)`, rien d'autre) → `request(OP_OPEN)`. `project-save` : sans projet ou sans chemin → save as ; sinon `gt_project_save(p, NULL,
    FALSE)` : `CONFLICT` → `gt_ui_choose(_("The project file changed on disk."), chemin, { _("Overwrite"), _("Reload"), _("Cancel") })` (Overwrite :
    `force` ; Reload : lecture puis `replace_all_tabs` sans autre question) ; `FAILED` → message ; `SAVED` → `app_config_set_last_project`.
    `project-save-as` : `gt_ui_save_file(_("Save project"), dossier du projet ou NULL, nom actuel + `.gtkterm` ou `project.gtkterm`)` → suffixe ajouté si
    absent → si le projet n'est pas ouvert : `gt_project_begin(p, NULL, NULL, NULL)` d'abord (les onglets deviennent de projet : macros intégrées, script
    pris de la session) → `gt_project_save(p, chemin, TRUE)` → `last_project`.
    `gt_app_add_session` : si `gt_project_is_open` : pas de chargement de fichier ; `gt_macros_replace_set(macros, gt_macro_set_copy(jeu de l'onglet actif))`
    après `gt_session_new` (le gestionnaire la passera `in-project` à l'ajout).
    Titre : remplacer la liaison `status-text` → `title` par `title_update()` connecté à `notify::status-text` et `notify::short-name` de la session active et
    à `notify::open|path|modified` du projet (plan, décision 10).
    `close-request` : si `project open` et `!close_confirmed` : `gt_project_check_now` ; `modified` → `request(OP_QUIT)` et rendre TRUE (fermeture arrêtée) ;
    sinon chemin actuel.
    Démarrage (`on_activate`) : chemin = `cmd->project` ou, si `!cmd->profile_given`, `app_config_get_last_project()` ; s'il y en a un :
    `gt_project_data_load` ; succès → la première session (créée par `main.c`) est retirée et arrêtée avant toute page, puis `replace_all_tabs` (pas de
    question) ; échec → `gt_app_message(GT_APP_MSG_ERROR, _("Cannot open the project: %s"), error->message)` (mis en file, affiché après la présentation) et
    démarrage 6b. Les étapes « page de la première session » d'`on_activate` sont factorisées pour servir aux deux cas ; `install_relays` utilise la
    session active.
  - `test_hooks.c` : `test-sessions` gagne `in_project` et `script` ; `test-project` (état `a{sv}` : `open`, `path`, `modified`, `name`), installée comme
    `test-sessions` ; accès au projet par `GtProject *gt_app_get_project(void)` (`app.h`).

- [ ] **Step 4 : GREEN** — `meson test -C build --suite gui test_projet test_projet_fermeture test_menus_actions test_journal_pause test_onglets`, puis la suite
  graphique complète (tous les scénarios qui démarrent sans projet doivent rester inchangés : sans `last_project`, aucun projet n'est rouvert).

- [ ] **Step 5 : suite + ASan, commit** `projets (interface) : New/Open/Save/Save as/Close project, menus réorganisés (Port › New port…), titre, dernier projet, question à la fermeture`

### Task 12 : démarrage, macros d'un projet, colonnes de l'éditeur (`Test`)

**Files:**
- Create: `tests/gui/test_projet_demarrage.py`, `tests/gui/test_projet_macros.py`, `tests/gui/test_macro_editor_colonnes.py`
- Modify: `tests/gui/meson.build`

- [ ] **Step 1 : scénarios**
  - `test_projet_demarrage.py` : (1) projet écrit à la main (deux onglets sur deux ptys, `active=1`) et `[general] last_project` → démarrage sans argument :
    deux onglets, actif 1, `project()['open']` ; (2) `last_project` vers un fichier absent → boîte d'erreur après la présentation (titre de boîte, texte
    contenant le chemin), un onglet, `open` FALSE ; (3) `-p <pty>` avec `last_project` valide → un onglet, pas de projet ; (4) `--project <fichier>` →
    projet ouvert ; (5) `--project x -p y` → code 1 sans fenêtre (`e.start(..., connect_browser=False)` puis `e.proc.wait`).
  - `test_projet_macros.py` : projet d'un onglet ; Configure Macros : ajout d'une macro `AJOUT`, OK → aucun fichier `.ini` créé ou modifié dans le HOME
    privé (liste et `st_mtime_ns` avant/après), `modified` TRUE ; Load macros file… (`file_dialog`) d'un `.ini` → panneau montre ses macros, le `.ini` n'est
    pas réécrit après un changement de polling ; Save macros file… vers `export.ini` → fichier écrit, `sessions()[0]['macros_file'] == ''` ; New port… →
    nouvel onglet avec une copie du jeu (même libellés) ; changer la période dans le nouvel onglet ne change pas celle du premier.
  - `test_macro_editor_colonnes.py` : Configure Macros ; dump des en-têtes de colonnes (`GtkColumnViewTitle` ou équivalent du dump) : chaque largeur ≥
    largeur de son titre ; glisser (`drag_in_surface`) le bord droit de la première colonne de 40 px vers la droite → seule la largeur de la première change
    de ≈ 40 px (±4), la dernière rétrécit, les autres sont inchangées. Si, après mise au point, le glisser sur un en-tête n'est pas reproductible sous
    broadway (trois essais consécutifs instables), retirer ce scénario et le noter dans `tests/README.md` (« colonnes de l'éditeur : vérification manuelle »).
  Section « projets (sous-projet 6c) » de `tests/gui/meson.build`.

- [ ] **Step 2 : exécuter** — les scénarios doivent passer sans changement de code ; un échec révèle un défaut à corriger dans le module concerné (tâches
  7, 9 à 11), test d'abord.

- [ ] **Step 3 : suite graphique complète, commit** `tests : projets (démarrage, macros intégrées, nouvel onglet), colonnes de l'éditeur de macros`

### Task 13 : documentation et traductions (`Docs`)

**Files:**
- Modify: `CLAUDE.md`, `README.md`, `tests/README.md`, `docs/superpowers/reviews/2026-10-06-revue-code.md`, `po/POTFILES.in`, `po/*.po`, `po/gtkterm.pot`

- [ ] **Step 1 : `CLAUDE.md`** — modules du cœur (`profile_keyfile.c`, `file_stamp.c`, `project_file.c`, `window_state.h`), couche application
  (`project.c`), section « Projets (sous-projet 6c) » : format `.gtkterm`, onglets libres et onglets de projet (jamais de mélange), macros intégrées,
  valeurs d'usage / par défaut, signal `edited` et « modifié », `GtFileStamp` et Overwrite/Reload/Cancel, `last_project`, `--project`, menus réorganisés
  (liste de la spec), nouvelles actions, `test-project` et champs ajoutés à `test-sessions`, nombre de scénarios graphiques mis à jour ; paragraphe
  « Macros subsystem » : le panneau n'enregistre plus les arguments.
- [ ] **Step 2 : `README.md`** (section Projets : créer, ouvrir, dernier projet, `--project`) ; `tests/README.md` (scénarios `test_projet*`,
  `test_macros_conflit`, aide `Env.project()`).
- [ ] **Step 3 : revue** — annexe « Sous-projet 6c » ; remarques SP3/SP4 traitées marquées (référence aux tâches 2 et 3).
- [ ] **Step 4 : traductions** — `po/POTFILES.in` vérifié (`src/project_file.c`, `src/project.c`, `src/ui/macros_save.c`, nouveaux fichiers à chaînes) ;
  `ninja -C build gtkterm-pot gtkterm-update-po` ; `po/fr.po` : toutes les nouvelles chaînes traduites (aucune entrée `fuzzy` ni vide parmi les nouvelles :
  `msgattrib --only-fuzzy po/fr.po` et `--untranslated` vides pour elles) ; `ninja -C build` ; suite complète + ASan + graphique (`test_messages_demarrage`
  en français).
- [ ] **Step 5 : commit** `docs, traductions : projets (sous-projet 6c)`

## Après les tâches

Relecture finale de la branche (`superpowers:requesting-code-review`), correctifs, puis fusion (`superpowers:finishing-a-development-branch`) selon
la spec (« fusionnée `--no-ff` dans `master`, écrasée en commits thématiques ») et envoi sur Maison.
