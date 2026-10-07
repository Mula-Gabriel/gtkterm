# Macros : modèle sans GTK, identité stable, fichier échappé, polling par liaison — plan d'implémentation (sous-projet 4)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** remplacer le tableau global `macro_t *macros` et ses trois copies d'état (polling, arguments) par un modèle sans GTK (`GtMacroSet` à identifiants stables, `GtMacroLists`), un fichier `.ini` échappé avec migration de l'ancien format, un ordonnanceur de polling indépendant des boutons, et un seul chemin d'envoi (`macros_send()`).

**Architecture :** trois modules nouveaux dans `gtkterm_core` — `macro_set.c` (modèle, édition par identifiant, expansion unique), `macro_file.c` (lecture/écriture v2, lecture de l'ancien format, copie avant migration), `macro_poller.c` (une minuterie GLib par macro en cours) — plus `macros_list.c` transformé en objet `GtMacroLists` et `macros_format.c` qui reçoit les listes en paramètre. L'interface (`macros.c`) tient le jeu courant, le chemin du fichier et l'ordonnanceur de la liaison par défaut ; `macro_panel.c`, la fenêtre d'édition, les raccourcis, le Lua et `term_config.c` passent tous par elle.

**Tech Stack :** C (gnu11), GLib ≥ 2.70 (GKeyFile, `g_ascii_string_to_unsigned`, `g_byte_array_free_to_bytes`), GTK 3 (interface seulement), Lua 5.4, Meson ≥ 1.0, GLib GTest (TAP), suite graphique `tests/gui/` (Python + AT-SPI + broadway isolé).

**Spec :** `docs/superpowers/specs/2026-10-07-refonte-macros-design.md`

## Global Constraints

- `macros_list.c`, `macros_format.c`, `macro_set.c`, `macro_file.c`, `macro_poller.c` sont dans `gtkterm_core` et n'incluent aucun en-tête GTK, VTE, Lua ni d'interface (`interface.h`, `link.h`, `term_config.h`…).
- Le jeu, le fichier et l'ordonnanceur ne sont utilisés que sur le fil principal ; le fil Lua n'y accède que par `run_on_main_sync`.
- À la fin (tâche 5), plus aucune référence dans `src/` et `tests/` à `macro_t`, `get_shortcuts`, `create_shortcuts`, `add_shortcuts`, `remove_shortcuts`, `shortcut_callback`, `send_macro_with_arg`, `send_macro_with_args`, `macro_set_arg`, `macro_set_polling`, `macro_polling_t`, `macro_list_find`, `macro_list_add`, `macro_lists_free`, `macro_lists_init`, `macros_app_lists`, `macros_file_load`, `macros_file_save`, `macros_file_set_path`, `macros_file_get_default_path`.
- Format écrit : exactement celui du §5.1 de la spec (`[gtkterm-macros]` `version=2`, `[macro:N]`, `[list:N]`, clés `label`, `shortcut`, `action`, `tab`, `args`, `polling`, `polling_period_ms`, `name`, `displays`, `values`). Un ancien fichier reste lisible sans perte ; copie `<fichier>.avant-migration` au premier enregistrement.
- Bornes de période : `GT_MACRO_PERIOD_MIN_MS` = 1, `GT_MACRO_PERIOD_MAX_MS` = 86400000, `GT_MACRO_PERIOD_DEFAULT_MS` = 1000.
- Chaînes d'interface : identifiants gettext en anglais ; traductions françaises dans `po/fr.po` (tâche 7). Les messages existants (« Macro \"%s\" sent! », « Polling Mode », « Period (ms) », « None »…) gardent leur identifiant.
- Style : celui du fichier modifié (GNU 2 espaces pour `macros.c`, `macros_list.c`, `macros_format.c` ; tabulations + `if (` pour `macro_panel.c` ; K&R 4 espaces pour `script_engine.c`) ; nouveaux fichiers C dans le style dominant (tabulations, Allman, `if(`, type de retour sur sa propre ligne) ; lignes ≤ 200 colonnes ; commentaires en français ; messages des tests en ASCII (caractères non ASCII des données de test écrits en échappements octaux, ex. `"G\303\251n\303\251ral"`).
- Tests : jamais la vraie configuration de l'utilisateur ni de vrai port série ; fichiers temporaires dans un répertoire créé par `g_dir_make_tmp` ; la suite graphique ne tourne que par `meson test -C build --suite gui` (broadway isolé), jamais sur l'écran de l'utilisateur.
- Chaque commit en français, terminé par `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Branche `feature/refonte-macros`.
- Construction : `ninja -C build` ; tests : `meson test -C build --print-errorlogs` ; ASan (créer s'il manque : `meson setup build-asan -Db_sanitize=address,undefined`) : `meson test -C build-asan --print-errorlogs` ; interface : `meson test -C build --suite gui --print-errorlogs`.

## Décisions prises par ce plan

1. **Listes transitoires (tâches 1 à 4)** : tant que `macros.c` n'a pas son jeu (tâche 5), les listes de l'application sont une instance `GtMacroLists` tenue par `macros.c` (`macros_app_lists()`, `macros_app_lists_replace()`), que les appelants passent aux fonctions de format. Ces deux fonctions disparaissent à la tâche 5.
2. **`macros_replace_set()`** (ajout à l'API du §6.1 de la spec) : installe un jeu (pris en charge), bascule l'ordonnanceur (`gt_macro_poller_set_set`, qui arrête tout pendant que l'ancien jeu est encore le jeu courant), libère l'ancien, puis `macros_changed()`. Utilisée par `macros_load_file` et par les tests.
3. **`gt_macro_period_parse()`** (ajout à l'API du §4) : analyse d'une période saisie (entier décimal dans les bornes, sans signe ni blanc) ; testée en unitaire, utilisée par le menu de polling.
4. **Rappel d'état de l'ordonnanceur** : `macro_panel_polling_changed(guint id, gboolean running)` (déclarée dans `macro_panel.h`) ; elle tolère un identifiant sans bouton ou absent du jeu.
5. **M44 (nouveau défaut, consigné dans la revue à la tâche 7)** : « Capture Shortcut » et « Delete Shortcut » de la fenêtre d'édition utilisent `gtk_tree_selection_get_selected`, inopérant en sélection multiple (mode de la liste) : les deux boutons ne font rien. Corrigé à la tâche 5 (première ligne sélectionnée par `gtk_tree_selection_get_selected_rows`) ; essai graphique à la tâche 7.
6. **Période « 0 » dans l'interface** : refusée par `gt_macro_period_parse` (unitaire) ; la suite graphique vérifie seulement qu'une saisie touche par touche n'est pas appliquée avant Entrée (la couleur d'erreur n'est pas observable par AT-SPI).
7. **Lua et message temporaire** : `send_macro` passant par `macros_send()`, il affiche aussi « Macro "…" sent! » (comme les autres envois de macro).

## Review Focus

1. **Fichier de macros modifié à la main ou abîmé** (clé manquante, période négative ou énorme, nombre d'arguments faux, groupe inconnu, ancien format incomplet) : valeurs fautives retombant sur le défaut, sans plantage ni perte du reste. Épinglé par `test_macro_file` (`/macro_file/cles-manquantes-ou-invalides`, `/macro_file/ancien-format`).
2. **Ré-entrance de l'ordonnanceur** : la fonction d'envoi (ou un rappel d'état) arrête la macro, la redémarre, ou recharge le jeu pendant l'envoi immédiat ou l'échéance. Épinglé par `test_macro_poller` (`/macro_poller/arret-depuis-envoi`, `/macro_poller/arret-pendant-envoi-immediat`).
3. **Édition qui réordonne, supprime ou duplique** (tri par colonne puis suppression, ligne copiée avec le même identifiant) : arguments et polling restent sur la bonne macro, jamais hérités par position. Épinglé par `test_macro_set` (`/macro_set/edition-*`) et `test_macros_edition.py`.
4. **Échec d'enregistrement répété** (répertoire supprimé, disque plein) alors que chaque frappe dans un argument enregistre : une seule boîte par message, pas de boucle de boîtes. Épinglé par `test_macros_send` (`/macros_send/enregistrement-echec-une-seule-boite`).
5. **Chargement d'un autre fichier pendant un polling** : l'ancien polling s'arrête, aucune macro du nouveau fichier ne part seule, aucun accès à un bouton détruit. Épinglé par `test_macros_send` (`/macros_send/polling-arrete-au-chargement`) et ASan sur la suite graphique.

## Carte des fichiers

| Fichier | Tâche | Rôle |
|---|---|---|
| `src/macros_list.c/.h` | 1 | `GtMacroLists` (instance) |
| `src/macros_format.c/.h` | 1 | listes en paramètre ; code mort retiré |
| `src/macro_set.c/.h` (créés) | 2 | `GtMacro`, `GtMacroSet`, édition, expansion, séparateurs, période |
| `src/macro_file.c/.h` (créés) | 3 | `.ini` v2, ancien format, copie avant migration |
| `src/macro_poller.c/.h` (créés) | 4 | ordonnanceur de polling |
| `src/macros.c/.h` | 1 (transitoire), 5 | état applicatif, `macros_send`, raccourcis, fichier, fenêtre d'édition |
| `src/macro_panel.c/.h` | 1 (mécanique), 5, 6 | panneau vue du modèle ; comportement du polling (tâche 6) |
| `src/script_engine.c` | 1 (mécanique), 5 | `send_macro`/`get_macros`/`get_lists` sur le jeu courant |
| `src/term_config.c`, `src/gtkterm.c` | 5 | chargement/enregistrement via `macros.c`, `macros_init` |
| `src/meson.build`, `tests/meson.build` | 2, 3, 4 | sources du cœur, nouveaux tests |
| `tests/test_macros_list.c`, `tests/test_macros_format.c` | 1 | adaptés |
| `tests/test_macro_set.c`, `tests/test_macro_file.c`, `tests/test_macro_poller.c` (créés) | 2, 3, 4 | unitaires |
| `tests/test_macros_send.c`, `tests/test_script_engine.c`, `tests/stubs/stub_macros.c/.h`, `tests/stubs/stub_ui.c` | 1, 5 | intégration |
| `tests/gui/test_polling.py`, `tests/gui/gtktest.py` | 6, 7 | polling, aide d'écriture v2 |
| `tests/gui/test_macros_edition.py`, `tests/gui/test_macros_migration.py` (créés), `tests/gui/meson.build` | 7 | suite graphique |
| `po/POTFILES.in`, `po/fr.po`, `CLAUDE.md`, `tests/README.md`, revue | 7 | documentation, traductions |

---

### Task 1 : listes nommées en instance, format avec listes en paramètre

**Files :**
- Modify : `src/macros_list.h`, `src/macros_list.c` (réécrits), `src/macros_format.h`, `src/macros_format.c`
- Modify (mécanique) : `src/macros.c`, `src/macros.h`, `src/macro_panel.c`, `src/script_engine.c`, `tests/stubs/stub_macros.c`, `tests/stubs/stub_macros.h`
- Test : `tests/test_macros_list.c` (réécrit), `tests/test_macros_format.c`, `tests/test_script_engine.c`, `tests/test_macros_send.c`

**Interfaces :**
- Produces : `GtMacroLists` et les fonctions `gt_macro_lists_*` du §2 de la spec ; `macro_get_arg_infos(lists, action, &n)`, `macro_count_format_args(lists, action)`, `format_action_with_args(lists, action, args, n)` ; transitoire : `GtMacroLists *macros_app_lists(void)` et `void macros_app_lists_replace(GtMacroLists *lists)` dans `macros.h`.
- Retire : `macro_lists_init`, `macro_lists_free`, `macro_list_find`, `macro_list_add`, `macro_list_remove_entry`, `macro_list_entry_count`, `macro_list_entry_display`, `macro_list_entry_value`, `macro_list_count`, `macro_list_name`, `list_entry_t`, `macro_list_t`, `macro_get_format_type`, `macro_has_format_arg`, `macro_get_format_types`.

- [ ] **Step 1 : réécrire le test des listes (échoue à la compilation)**

Remplacer tout `tests/test_macros_list.c` par :

```c
/* Tests de macros_list.c : listes nommées (instance GtMacroLists) utilisées par %#NomListe. */

#include <glib.h>

#include "macros_list.h"

static void
test_vide(void)
{
	GtMacroLists *l = gt_macro_lists_new();

	g_assert_cmpint(gt_macro_lists_count(l), ==, 0);
	g_assert_cmpint(gt_macro_lists_find(l, "Mode"), ==, -1);
	g_assert_cmpint(gt_macro_lists_find(l, NULL), ==, -1);
	g_assert_cmpint(gt_macro_lists_find(NULL, "Mode"), ==, -1);
	g_assert_null(gt_macro_lists_name(l, 0));
	g_assert_cmpint(gt_macro_lists_entry_count(l, 0), ==, 0);
	g_assert_null(gt_macro_lists_entry_display(l, 0, 0));
	g_assert_null(gt_macro_lists_entry_value(l, 0, 0));
	g_assert_cmpint(gt_macro_lists_count(NULL), ==, 0);
	gt_macro_lists_free(l);
	gt_macro_lists_free(NULL);
}

static void
test_ajout_cree_liste(void)
{
	GtMacroLists *l = gt_macro_lists_new();

	gt_macro_lists_add(l, "Mode", "Run", "R");
	g_assert_cmpint(gt_macro_lists_count(l), ==, 1);
	g_assert_cmpint(gt_macro_lists_find(l, "Mode"), ==, 0);
	g_assert_cmpstr(gt_macro_lists_name(l, 0), ==, "Mode");
	g_assert_cmpint(gt_macro_lists_entry_count(l, 0), ==, 1);
	g_assert_cmpstr(gt_macro_lists_entry_display(l, 0, 0), ==, "Run");
	g_assert_cmpstr(gt_macro_lists_entry_value(l, 0, 0), ==, "R");
	gt_macro_lists_free(l);
}

static void
test_ajout_liste_existante(void)
{
	GtMacroLists *l = gt_macro_lists_new();

	gt_macro_lists_add(l, "Mode", "Run", "R");
	gt_macro_lists_add(l, "Mode", "Stop", "S");
	g_assert_cmpint(gt_macro_lists_count(l), ==, 1);
	g_assert_cmpint(gt_macro_lists_entry_count(l, 0), ==, 2);
	g_assert_cmpstr(gt_macro_lists_entry_display(l, 0, 1), ==, "Stop");
	g_assert_cmpstr(gt_macro_lists_entry_value(l, 0, 1), ==, "S");
	gt_macro_lists_free(l);
}

static void
test_valeur_par_defaut(void)
{
	GtMacroLists *l = gt_macro_lists_new();

	gt_macro_lists_add(l, "Mode", "Idle", NULL);
	g_assert_cmpstr(gt_macro_lists_entry_value(l, 0, 0), ==, "Idle");
	gt_macro_lists_free(l);
}

static void
test_arguments_nuls_ignores(void)
{
	GtMacroLists *l = gt_macro_lists_new();

	gt_macro_lists_add(l, NULL, "x", "y");
	gt_macro_lists_add(l, "Mode", NULL, "y");
	gt_macro_lists_add(NULL, "Mode", "x", "y");
	g_assert_cmpint(gt_macro_lists_count(l), ==, 0);
	gt_macro_lists_free(l);
}

static void
test_recherche_sensible_casse(void)
{
	GtMacroLists *l = gt_macro_lists_new();

	gt_macro_lists_add(l, "Mode", "Run", "R");
	g_assert_cmpint(gt_macro_lists_find(l, "mode"), ==, -1);
	g_assert_cmpint(gt_macro_lists_find(l, "Mode"), ==, 0);
	gt_macro_lists_free(l);
}

static void
test_ordre_des_listes(void)
{
	GtMacroLists *l = gt_macro_lists_new();

	gt_macro_lists_add(l, "B", "b", "1");
	gt_macro_lists_add(l, "A", "a", "2");
	gt_macro_lists_add(l, "B", "c", "3");
	g_assert_cmpint(gt_macro_lists_count(l), ==, 2);
	g_assert_cmpstr(gt_macro_lists_name(l, 0), ==, "B");
	g_assert_cmpstr(gt_macro_lists_name(l, 1), ==, "A");
	g_assert_cmpint(gt_macro_lists_entry_count(l, 0), ==, 2);
	gt_macro_lists_free(l);
}

static void
test_indices_hors_bornes(void)
{
	GtMacroLists *l = gt_macro_lists_new();

	gt_macro_lists_add(l, "Mode", "Run", "R");
	g_assert_null(gt_macro_lists_name(l, -1));
	g_assert_null(gt_macro_lists_name(l, 1));
	g_assert_cmpint(gt_macro_lists_entry_count(l, -1), ==, 0);
	g_assert_cmpint(gt_macro_lists_entry_count(l, 1), ==, 0);
	g_assert_null(gt_macro_lists_entry_display(l, 0, 1));
	g_assert_null(gt_macro_lists_entry_display(l, 0, -1));
	g_assert_null(gt_macro_lists_entry_value(l, 1, 0));
	gt_macro_lists_free(l);
}

static void
test_copie_independante(void)
{
	GtMacroLists *l = gt_macro_lists_new(), *c;

	gt_macro_lists_add(l, "Mode", "Run", "R");
	c = gt_macro_lists_copy(l);
	gt_macro_lists_add(l, "Mode", "Stop", "S");
	gt_macro_lists_add(c, "Autre", "x", "1");
	g_assert_cmpint(gt_macro_lists_entry_count(c, 0), ==, 1);
	g_assert_cmpint(gt_macro_lists_count(c), ==, 2);
	g_assert_cmpint(gt_macro_lists_count(l), ==, 1);
	gt_macro_lists_free(l);
	g_assert_cmpstr(gt_macro_lists_entry_value(c, 0, 0), ==, "R");
	gt_macro_lists_free(c);
	c = gt_macro_lists_copy(NULL);
	g_assert_nonnull(c);
	g_assert_cmpint(gt_macro_lists_count(c), ==, 0);
	gt_macro_lists_free(c);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/macros_list/vide", test_vide);
	g_test_add_func("/macros_list/ajout-cree-liste", test_ajout_cree_liste);
	g_test_add_func("/macros_list/ajout-liste-existante", test_ajout_liste_existante);
	g_test_add_func("/macros_list/valeur-par-defaut", test_valeur_par_defaut);
	g_test_add_func("/macros_list/arguments-nuls-ignores", test_arguments_nuls_ignores);
	g_test_add_func("/macros_list/recherche-sensible-casse", test_recherche_sensible_casse);
	g_test_add_func("/macros_list/ordre-des-listes", test_ordre_des_listes);
	g_test_add_func("/macros_list/indices-hors-bornes", test_indices_hors_bornes);
	g_test_add_func("/macros_list/copie-independante", test_copie_independante);
	return g_test_run();
}
```

- [ ] **Step 2 : adapter le test du format (échoue à la compilation)**

Dans `tests/test_macros_format.c` :
- ajouter après les `#include` : `static GtMacroLists *listes = NULL;	/* « Mode » : Run=R, Stop=S ; aucune liste « x » ni « o » (cas M18) */` ;
- `format_action_with_args(c->action, args, n_args)` → `format_action_with_args(listes, c->action, args, n_args)` ; `format_action_with_args("%.2f", args, 1)` → `format_action_with_args(listes, "%.2f", args, 1)` ;
- `macro_get_arg_infos(c->action, &n)` → `macro_get_arg_infos(listes, c->action, &n)` ; `macro_count_format_args(c->action)` → `macro_count_format_args(listes, c->action)` ;
- dans `main`, remplacer les deux `macro_list_add("Mode", …)` par :

```c
	listes = gt_macro_lists_new();
	gt_macro_lists_add(listes, "Mode", "Run", "R");
	gt_macro_lists_add(listes, "Mode", "Stop", "S");
```

  et `macro_lists_free();` (après `g_test_run`) par `gt_macro_lists_free(listes);` ;
- ajouter ce cas (et son enregistrement `g_test_add_func("/macros_format/listes-en-parametre", test_listes_en_parametre);` dans `main`) :

```c
/* « %#x » : liste si une liste « x » existe dans les listes passées, drapeau printf sinon (listes NULL comprises). */
static void
test_listes_en_parametre(void)
{
	GtMacroLists *avec_x = gt_macro_lists_new();
	const gchar *args[] = { "255" };
	gchar *texte;

	gt_macro_lists_add(avec_x, "x", "Huit", "8");
	texte = format_action_with_args(NULL, "%#x", args, 1);
	g_assert_cmpstr(texte, ==, "0xff");
	g_free(texte);
	texte = format_action_with_args(avec_x, "%#x", args, 1);
	g_assert_cmpstr(texte, ==, "255");
	g_free(texte);
	g_assert_cmpint(macro_count_format_args(NULL, "%#Mode %d"), ==, 1);
	g_assert_cmpint(macro_count_format_args(listes, "%#Mode %d"), ==, 2);
	gt_macro_lists_free(avec_x);
}
```

- [ ] **Step 3 : vérifier l'échec**

Run : `ninja -C build 2>&1 | tail -5`
Expected : erreurs de compilation dans `test_macros_list.c`/`test_macros_format.c` (`gt_macro_lists_new` non déclarée, nombre d'arguments).

- [ ] **Step 4 : réécrire `macros_list.h`**

```c
/***********************************************************************/
/* macros_list.h                                                       */
/* ------------                                                        */
/*           GTKTerm Software                                          */
/*                                                                     */
/*   Purpose                                                           */
/*      Named value lists used by %#ListName in macros (one instance  */
/*      per macro set, no global state)                               */
/*                                                                     */
/***********************************************************************/

#ifndef MACROS_LIST_H_
#define MACROS_LIST_H_

#include <glib.h>

typedef struct _GtMacroLists GtMacroLists;

GtMacroLists *gt_macro_lists_new           (void);
void          gt_macro_lists_free          (GtMacroLists *lists);
GtMacroLists *gt_macro_lists_copy          (const GtMacroLists *lists);
gint          gt_macro_lists_find          (const GtMacroLists *lists, const gchar *name);
void          gt_macro_lists_add           (GtMacroLists *lists, const gchar *name, const gchar *display, const gchar *value);
gint          gt_macro_lists_count         (const GtMacroLists *lists);
const gchar  *gt_macro_lists_name          (const GtMacroLists *lists, gint list_idx);
gint          gt_macro_lists_entry_count   (const GtMacroLists *lists, gint list_idx);
const gchar  *gt_macro_lists_entry_display (const GtMacroLists *lists, gint list_idx, gint entry_idx);
const gchar  *gt_macro_lists_entry_value   (const GtMacroLists *lists, gint list_idx, gint entry_idx);

#endif
```

- [ ] **Step 5 : réécrire `macros_list.c`** (garder l'en-tête de commentaire existant du fichier, style GNU)

```c
#include <glib.h>

#include "macros_list.h"

typedef struct
{
  gchar *display;
  gchar *value;
} ListEntry;

typedef struct
{
  gchar *name;
  GPtrArray *entries;   /* ListEntry * */
} NamedList;

struct _GtMacroLists
{
  GPtrArray *lists;     /* NamedList *, dans l'ordre d'apparition */
};

static void
list_entry_free (gpointer data)
{
  ListEntry *e = data;

  g_free (e->display);
  g_free (e->value);
  g_free (e);
}

static void
named_list_free (gpointer data)
{
  NamedList *nl = data;

  g_free (nl->name);
  g_ptr_array_unref (nl->entries);
  g_free (nl);
}

static NamedList *
list_at (const GtMacroLists *lists, gint list_idx)
{
  if (lists == NULL || list_idx < 0 || (guint) list_idx >= lists->lists->len)
    return NULL;
  return g_ptr_array_index (lists->lists, list_idx);
}

static ListEntry *
entry_at (const GtMacroLists *lists, gint list_idx, gint entry_idx)
{
  NamedList *nl = list_at (lists, list_idx);

  if (nl == NULL || entry_idx < 0 || (guint) entry_idx >= nl->entries->len)
    return NULL;
  return g_ptr_array_index (nl->entries, entry_idx);
}

GtMacroLists *
gt_macro_lists_new (void)
{
  GtMacroLists *lists = g_new0 (GtMacroLists, 1);

  lists->lists = g_ptr_array_new_with_free_func (named_list_free);
  return lists;
}

void
gt_macro_lists_free (GtMacroLists *lists)
{
  if (lists == NULL)
    return;
  g_ptr_array_unref (lists->lists);
  g_free (lists);
}

GtMacroLists *
gt_macro_lists_copy (const GtMacroLists *lists)
{
  GtMacroLists *copy = gt_macro_lists_new ();

  for (gint li = 0; li < gt_macro_lists_count (lists); li++)
    for (gint ei = 0; ei < gt_macro_lists_entry_count (lists, li); ei++)
      gt_macro_lists_add (copy, gt_macro_lists_name (lists, li), gt_macro_lists_entry_display (lists, li, ei), gt_macro_lists_entry_value (lists, li, ei));
  return copy;
}

gint
gt_macro_lists_find (const GtMacroLists *lists, const gchar *name)
{
  if (lists == NULL || name == NULL)
    return -1;
  for (guint i = 0; i < lists->lists->len; i++)
    {
      NamedList *nl = g_ptr_array_index (lists->lists, i);
      if (g_strcmp0 (nl->name, name) == 0)
        return (gint) i;
    }
  return -1;
}

void
gt_macro_lists_add (GtMacroLists *lists, const gchar *name, const gchar *display, const gchar *value)
{
  gint idx;
  NamedList *nl;
  ListEntry *entry;

  if (lists == NULL || name == NULL || display == NULL)
    return;
  idx = gt_macro_lists_find (lists, name);
  if (idx < 0)
    {
      nl = g_new0 (NamedList, 1);
      nl->name = g_strdup (name);
      nl->entries = g_ptr_array_new_with_free_func (list_entry_free);
      g_ptr_array_add (lists->lists, nl);
    }
  else
    nl = g_ptr_array_index (lists->lists, idx);

  entry = g_new0 (ListEntry, 1);
  entry->display = g_strdup (display);
  entry->value = g_strdup (value != NULL ? value : display);
  g_ptr_array_add (nl->entries, entry);
}

gint
gt_macro_lists_count (const GtMacroLists *lists)
{
  return lists != NULL ? (gint) lists->lists->len : 0;
}

const gchar *
gt_macro_lists_name (const GtMacroLists *lists, gint list_idx)
{
  NamedList *nl = list_at (lists, list_idx);

  return nl != NULL ? nl->name : NULL;
}

gint
gt_macro_lists_entry_count (const GtMacroLists *lists, gint list_idx)
{
  NamedList *nl = list_at (lists, list_idx);

  return nl != NULL ? (gint) nl->entries->len : 0;
}

const gchar *
gt_macro_lists_entry_display (const GtMacroLists *lists, gint list_idx, gint entry_idx)
{
  ListEntry *e = entry_at (lists, list_idx, entry_idx);

  return e != NULL ? e->display : NULL;
}

const gchar *
gt_macro_lists_entry_value (const GtMacroLists *lists, gint list_idx, gint entry_idx)
{
  ListEntry *e = entry_at (lists, list_idx, entry_idx);

  return e != NULL ? e->value : NULL;
}
```

- [ ] **Step 6 : listes en paramètre dans `macros_format.c/.h`**

Dans `macros_format.h` : `#include "macros_list.h"` ; supprimer les prototypes de `macro_get_format_type`, `macro_has_format_arg`, `macro_get_format_types` ; nouvelles signatures :

```c
macro_arg_info_t *macro_get_arg_infos    (const GtMacroLists *lists, const gchar *action, gint *count_out);
gint              macro_count_format_args(const GtMacroLists *lists, const gchar *action);
gchar            *format_action_with_args(const GtMacroLists *lists, const gchar *action, const gchar **args, gint n_args);
```

Dans `macros_format.c` :
- supprimer `#include "macros_list.h"` s'il est inclus autrement que par l'en-tête (l'en-tête l'apporte) ;
- `try_parse_list_spec (const gchar *action, gint start, gchar **name_out, gint *end_out)` → `try_parse_list_spec (const GtMacroLists *lists, const gchar *action, gint start, gchar **name_out, gint *end_out)` et `macro_list_find (name) < 0` → `gt_macro_lists_find (lists, name) < 0` ;
- passer `lists` à chaque appel de `try_parse_list_spec` (dans `macro_count_format_args`, `macro_get_arg_infos`, `format_action_with_args`) et dans l'appel `macro_count_format_args (lists, action)` de `macro_get_arg_infos` ;
- supprimer les fonctions `macro_get_format_type`, `macro_has_format_arg`, `macro_get_format_types` (code mort, revue).

- [ ] **Step 7 : listes transitoires de l'application et appelants**

Dans `src/macros.h`, ajouter :

```c
/* Transitoire (tâches 1 à 4 du sous-projet 4) : listes de l'application, en attendant le jeu de macros. */
GtMacroLists *macros_app_lists         (void);
void          macros_app_lists_replace (GtMacroLists *lists);
```

Dans `src/macros.c`, ajouter (près des variables statiques) :

```c
static GtMacroLists *app_lists = NULL;

GtMacroLists *
macros_app_lists (void)
{
  if (app_lists == NULL)
    app_lists = gt_macro_lists_new ();
  return app_lists;
}

void
macros_app_lists_replace (GtMacroLists *lists)
{
  gt_macro_lists_free (app_lists);
  app_lists = lists;
}
```

Remplacements mécaniques dans `src/macros.c`, `src/macro_panel.c`, `src/script_engine.c` :
- `macro_count_format_args (X)` → `macro_count_format_args (macros_app_lists (), X)` ; idem `macro_get_arg_infos (X, Y)` → `(macros_app_lists (), X, Y)` et `format_action_with_args (A, B, C)` → `(macros_app_lists (), A, B, C)` ;
- `macro_list_find (n)` → `gt_macro_lists_find (macros_app_lists (), n)` ; `macro_list_count ()` → `gt_macro_lists_count (macros_app_lists ())` ; `macro_list_name (i)` → `gt_macro_lists_name (macros_app_lists (), i)` ; `macro_list_entry_count (i)` / `_display (i, e)` / `_value (i, e)` → `gt_macro_lists_entry_count (macros_app_lists (), i)` / … ;
- dans `macros_file_load` : `macro_lists_free (); macro_lists_init ();` puis les `macro_list_add (…)` → construire `GtMacroLists *lists = gt_macro_lists_new ();`, `gt_macro_lists_add (lists, …)`, puis `macros_app_lists_replace (lists);` **avant** de lire les macros (le décompte des arguments en dépend) ;
- dans `save_lists_from_model` : même principe (nouvelle instance remplie depuis le modèle, puis `macros_app_lists_replace`).

Bouchon du moteur Lua : dans `tests/stubs/stub_macros.h` ajouter `#include "macros_list.h"` (déjà apporté par `macros.h`) et dans `tests/stubs/stub_macros.c` ajouter une copie des deux fonctions transitoires ci-dessus (même code). Dans `tests/test_script_engine.c`, `test_get_lists` devient :

```c
static void
test_get_lists(void)
{
	GtMacroLists *l = gt_macro_lists_new();

	gt_macro_lists_add(l, "Mode", "Marche", "M");
	macros_app_lists_replace(l);
	g_assert_true(executer("local l = gtkterm.get_lists()\n"
	                       "gtkterm.log(l[1].name .. ' ' .. l[1].entries[1].value)\n", 5000));
	g_assert_nonnull(strstr(journal->str, "Mode M"));
	macros_app_lists_replace(NULL);
}
```

Dans `tests/test_macros_send.c`, `test_chargement_arguments_liste_conserves` : remplacer les paires `macro_lists_free(); macro_lists_init();` par `macros_app_lists_replace(NULL);`.

- [ ] **Step 8 : construire et tester**

Run : `ninja -C build 2>&1 | tail -3 && meson test -C build --print-errorlogs 2>&1 | tail -8`
Expected : construction sans avertissement nouveau ; tous les tests OK (dont `test_macros_list` 9 cas, `test_macros_format` avec `/macros_format/listes-en-parametre`).

Run : `grep -rn -E 'macro_list_(find|add|count|name|entry|remove)|macro_lists_(init|free)|macro_get_format_type|macro_has_format_arg' src tests`
Expected : aucune sortie.

- [ ] **Step 9 : commit**

```bash
git add src/macros_list.c src/macros_list.h src/macros_format.c src/macros_format.h src/macros.c src/macros.h src/macro_panel.c src/script_engine.c \
        tests/test_macros_list.c tests/test_macros_format.c tests/test_script_engine.c tests/test_macros_send.c tests/stubs/stub_macros.c tests/stubs/stub_macros.h
git commit -m "macros : listes nommées en instance (GtMacroLists), format avec listes en paramètre

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 2 : modèle de macros (`GtMacroSet`)

**Files :**
- Create : `src/macro_set.h`, `src/macro_set.c`, `tests/test_macro_set.c`
- Modify : `src/meson.build` (`core_sources`), `tests/meson.build` (`unit_tests`)

**Interfaces :**
- Consumes : `GtMacroLists`, `macro_count_format_args(lists, action)`, `format_action_with_args(lists, …)`, `parse_macro_string` (tâche 1).
- Produces : tout le §4 de la spec (`GtMacro`, `GtMacroSet`, `GtMacroEdit`, `gt_macro_set_*`, `gt_macro_action_is_separator`, `gt_macro_is_sendable`, `gt_macro_expand`, constantes de période) plus `gboolean gt_macro_period_parse(const gchar *text, guint *period_ms)`.

- [ ] **Step 1 : écrire le test**

Créer `tests/test_macro_set.c` :

```c
/* Tests de macro_set.c : jeu de macros sans GTK (identifiants stables, arguments, polling, édition, expansion). */

#include <glib.h>
#include <string.h>

#include "macro_set.h"
#include "test_util.h"

static gchar *
expansion(const GtMacroSet *set, const GtMacro *m, const gchar *const *args, gint n)
{
	GBytes *b = gt_macro_expand(set, m, args, n);
	gsize len = 0;
	const gchar *data = g_bytes_get_data(b, &len);
	gchar *s = g_strndup(data != NULL ? data : "", len);

	g_bytes_unref(b);
	return s;
}

static void
test_normalisation(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint id = gt_macro_set_append(set, NULL, "None", NULL, NULL);
	const GtMacro *m = gt_macro_set_find(set, id);

	g_assert_nonnull(m);
	g_assert_cmpstr(m->label, ==, "");
	g_assert_cmpstr(m->shortcut, ==, "");
	g_assert_cmpstr(m->action, ==, "");
	g_assert_cmpstr(m->tab, ==, "");
	g_assert_null(m->args);
	g_assert_false(m->polling);
	g_assert_cmpuint(m->polling_period_ms, ==, GT_MACRO_PERIOD_DEFAULT_MS);
	gt_macro_set_free(set);
}

static void
test_identifiants(void)
{
	GtMacroSet *a = gt_macro_set_new(), *b;
	guint id1 = gt_macro_set_append(a, "A", "", "a", "");
	guint id2 = gt_macro_set_append(a, "B", "", "b", "");
	guint id3;

	g_assert_cmpuint(id1, !=, 0);
	g_assert_cmpuint(id2, !=, id1);
	g_assert_cmpuint(gt_macro_set_count(a), ==, 2);
	g_assert_cmpint(gt_macro_set_index_of(a, id2), ==, 1);
	g_assert_true(gt_macro_set_get(a, 0) == gt_macro_set_find(a, id1));
	g_assert_null(gt_macro_set_get(a, 2));
	g_assert_null(gt_macro_set_find(a, 0));
	g_assert_cmpint(gt_macro_set_index_of(a, 9999999), ==, -1);
	g_assert_true(gt_macro_set_find_label(a, "B") == gt_macro_set_find(a, id2));
	g_assert_null(gt_macro_set_find_label(a, "C"));
	g_assert_null(gt_macro_set_find_label(a, NULL));
	gt_macro_set_free(a);
	/* Un nouveau jeu ne réutilise jamais un identifiant d'un ancien jeu */
	b = gt_macro_set_new();
	id3 = gt_macro_set_append(b, "C", "", "c", "");
	g_assert_cmpuint(id3, !=, id1);
	g_assert_cmpuint(id3, !=, id2);
	gt_macro_set_free(b);
	gt_macro_set_free(NULL);
	g_assert_cmpuint(gt_macro_set_count(NULL), ==, 0);
}

static void
test_arguments(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint id = gt_macro_set_append(set, "V", "", "V%d,%s", "");
	guint sans = gt_macro_set_append(set, "P", "", "plain", "");
	const gchar *deux[] = { "1", "x" };
	const gchar *un[] = { "1" };
	const GtMacro *m;

	g_assert_true(gt_macro_set_set_arg(set, id, 1, "abc"));
	m = gt_macro_set_find(set, id);
	g_assert_nonnull(m->args);
	g_assert_cmpuint(g_strv_length(m->args), ==, 2);
	g_assert_cmpstr(m->args[0], ==, "");
	g_assert_cmpstr(m->args[1], ==, "abc");
	g_assert_false(gt_macro_set_set_arg(set, id, 2, "z"));
	g_assert_false(gt_macro_set_set_arg(set, id, -1, "z"));
	g_assert_false(gt_macro_set_set_arg(set, sans, 0, "z"));
	g_assert_false(gt_macro_set_set_arg(set, 9999999, 0, "z"));
	g_assert_true(gt_macro_set_set_arg(set, id, 0, NULL));
	g_assert_cmpstr(gt_macro_set_find(set, id)->args[0], ==, "");
	g_assert_true(gt_macro_set_set_args(set, id, deux, 2));
	g_assert_cmpstr(gt_macro_set_find(set, id)->args[1], ==, "x");
	g_assert_false(gt_macro_set_set_args(set, id, un, 1));
	g_assert_cmpstr(gt_macro_set_find(set, id)->args[0], ==, "1");
	g_assert_true(gt_macro_set_set_args(set, sans, NULL, 0));
	g_assert_null(gt_macro_set_find(set, sans)->args);
	g_assert_false(gt_macro_set_set_args(set, 9999999, NULL, 0));
	gt_macro_set_free(set);
}

static void
test_polling_bornes(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint id = gt_macro_set_append(set, "P", "", "p", "");

	g_assert_true(gt_macro_set_set_polling(set, id, TRUE, 0));
	g_assert_true(gt_macro_set_find(set, id)->polling);
	g_assert_cmpuint(gt_macro_set_find(set, id)->polling_period_ms, ==, GT_MACRO_PERIOD_DEFAULT_MS);
	g_assert_true(gt_macro_set_set_polling(set, id, TRUE, 1));
	g_assert_cmpuint(gt_macro_set_find(set, id)->polling_period_ms, ==, 1);
	g_assert_true(gt_macro_set_set_polling(set, id, FALSE, 86400000));
	g_assert_false(gt_macro_set_find(set, id)->polling);
	g_assert_cmpuint(gt_macro_set_find(set, id)->polling_period_ms, ==, 86400000);
	g_assert_true(gt_macro_set_set_polling(set, id, TRUE, 86400001));
	g_assert_cmpuint(gt_macro_set_find(set, id)->polling_period_ms, ==, GT_MACRO_PERIOD_MAX_MS);
	g_assert_false(gt_macro_set_set_polling(set, 9999999, TRUE, 10));
	gt_macro_set_free(set);
}

static void
test_periode_saisie(void)
{
	guint p = 77;

	g_assert_true(gt_macro_period_parse("2000", &p));
	g_assert_cmpuint(p, ==, 2000);
	g_assert_true(gt_macro_period_parse("1", &p));
	g_assert_cmpuint(p, ==, 1);
	g_assert_true(gt_macro_period_parse("86400000", &p));
	g_assert_cmpuint(p, ==, 86400000);
	p = 77;
	g_assert_false(gt_macro_period_parse("0", &p));
	g_assert_false(gt_macro_period_parse("86400001", &p));
	g_assert_false(gt_macro_period_parse("", &p));
	g_assert_false(gt_macro_period_parse("-5", &p));
	g_assert_false(gt_macro_period_parse("+5", &p));
	g_assert_false(gt_macro_period_parse(" 5", &p));
	g_assert_false(gt_macro_period_parse("5ms", &p));
	g_assert_false(gt_macro_period_parse("99999999999999999999", &p));
	g_assert_false(gt_macro_period_parse(NULL, &p));
	g_assert_cmpuint(p, ==, 77);
}

/* A (argument « 1 », raccourci), B (polling 250 ms), C (argument « 3 »). */
static GtMacroSet *
jeu_abc(guint *a, guint *b, guint *c)
{
	GtMacroSet *set = gt_macro_set_new();

	*a = gt_macro_set_append(set, "A", "<Control>F1", "A%d", "");
	*b = gt_macro_set_append(set, "B", "", "B", "");
	*c = gt_macro_set_append(set, "C", "", "C%d", "");
	g_assert_true(gt_macro_set_set_arg(set, *a, 0, "1"));
	g_assert_true(gt_macro_set_set_polling(set, *b, TRUE, 250));
	g_assert_true(gt_macro_set_set_arg(set, *c, 0, "3"));
	return set;
}

/* M16 : B supprimée, C (3e) ne devient pas la 2e macro « en polling » ; ses arguments la suivent. */
static void
test_edition_suppression(void)
{
	guint a, b, c;
	GtMacroSet *set = jeu_abc(&a, &b, &c);
	GtMacroEdit rows[] = { { a, "A", "<Control>F1", "A%d", "" }, { c, "C", "", "C%d", "" } };

	gt_macro_set_apply_edits(set, rows, G_N_ELEMENTS(rows), NULL);
	g_assert_cmpuint(gt_macro_set_count(set), ==, 2);
	g_assert_null(gt_macro_set_find(set, b));
	g_assert_cmpint(gt_macro_set_index_of(set, c), ==, 1);
	g_assert_cmpstr(gt_macro_set_find(set, c)->args[0], ==, "3");
	g_assert_false(gt_macro_set_find(set, c)->polling);
	g_assert_false(gt_macro_set_find(set, a)->polling);
	g_assert_cmpstr(gt_macro_set_find(set, a)->args[0], ==, "1");
	g_assert_cmpstr(gt_macro_set_find(set, a)->shortcut, ==, "<Control>F1");
	gt_macro_set_free(set);
}

static void
test_edition_deplacement_et_tri(void)
{
	guint a, b, c;
	GtMacroSet *set = jeu_abc(&a, &b, &c);
	GtMacroEdit rows[] = { { c, "C", "", "C%d", "" }, { b, "B", "", "B", "" }, { a, "A", "<Control>F1", "A%d", "" } };

	gt_macro_set_apply_edits(set, rows, G_N_ELEMENTS(rows), NULL);
	g_assert_cmpuint(gt_macro_set_count(set), ==, 3);
	g_assert_cmpuint(gt_macro_set_get(set, 0)->id, ==, c);
	g_assert_cmpuint(gt_macro_set_get(set, 1)->id, ==, b);
	g_assert_cmpuint(gt_macro_set_get(set, 2)->id, ==, a);
	g_assert_true(gt_macro_set_find(set, b)->polling);
	g_assert_cmpuint(gt_macro_set_find(set, b)->polling_period_ms, ==, 250);
	g_assert_false(gt_macro_set_find(set, a)->polling);
	g_assert_cmpstr(gt_macro_set_find(set, a)->args[0], ==, "1");
	g_assert_cmpstr(gt_macro_set_find(set, c)->args[0], ==, "3");
	gt_macro_set_free(set);
}

static void
test_edition_ajout_et_doublon(void)
{
	guint a, b, c, nouveau, doublon;
	GtMacroSet *set = jeu_abc(&a, &b, &c);
	GtMacroEdit rows[] = { { 0, "N", "None", "N%d", "T" }, { a, "A+", "<Control>F1", "A%d", "" }, { a, "A2", "", "A%d", "" } };

	gt_macro_set_apply_edits(set, rows, G_N_ELEMENTS(rows), NULL);
	g_assert_cmpuint(gt_macro_set_count(set), ==, 3);
	nouveau = gt_macro_set_get(set, 0)->id;
	doublon = gt_macro_set_get(set, 2)->id;
	g_assert_cmpuint(nouveau, !=, 0);
	g_assert_cmpuint(nouveau, !=, a);
	g_assert_cmpuint(nouveau, !=, b);
	g_assert_cmpuint(nouveau, !=, c);
	g_assert_cmpstr(gt_macro_set_get(set, 0)->shortcut, ==, "");
	g_assert_cmpstr(gt_macro_set_get(set, 0)->tab, ==, "T");
	g_assert_null(gt_macro_set_get(set, 0)->args);
	g_assert_false(gt_macro_set_get(set, 0)->polling);
	g_assert_cmpuint(gt_macro_set_get(set, 1)->id, ==, a);
	g_assert_cmpstr(gt_macro_set_get(set, 1)->label, ==, "A+");
	g_assert_cmpstr(gt_macro_set_get(set, 1)->args[0], ==, "1");
	/* Même identifiant sur deux lignes : la première le garde, la seconde est une macro nouvelle */
	g_assert_cmpuint(doublon, !=, a);
	g_assert_cmpstr(gt_macro_set_get(set, 2)->label, ==, "A2");
	g_assert_null(gt_macro_set_get(set, 2)->args);
	g_assert_null(gt_macro_set_find(set, b));
	g_assert_null(gt_macro_set_find(set, c));
	gt_macro_set_free(set);
}

static void
test_edition_change_arguments(void)
{
	guint a, b, c;
	GtMacroSet *set = jeu_abc(&a, &b, &c);
	GtMacroEdit rows[] = { { a, "A", "", "A%d %d", "" }, { b, "B", "", "B", "" }, { c, "C", "", "C%s", "" } };

	gt_macro_set_apply_edits(set, rows, G_N_ELEMENTS(rows), NULL);
	g_assert_null(gt_macro_set_find(set, a)->args);			/* 1 -> 2 spécificateurs */
	g_assert_cmpstr(gt_macro_set_find(set, c)->args[0], ==, "3");	/* toujours 1 spécificateur */
	g_assert_cmpstr(gt_macro_set_find(set, a)->shortcut, ==, "");
	gt_macro_set_free(set);
}

static void
test_listes_et_arguments(void)
{
	GtMacroSet *set = gt_macro_set_new();
	GtMacroLists *mode = gt_macro_lists_new(), *vide = gt_macro_lists_new();
	guint id = gt_macro_set_append(set, "M", "", "M %#Mode", "");
	GtMacroEdit rows[] = { { id, "M", "", "M %#Mode", "" } };

	/* Sans liste « Mode », « %#M » n'est pas un spécificateur complet : aucun argument */
	g_assert_false(gt_macro_set_set_arg(set, id, 0, "R"));
	gt_macro_lists_add(mode, "Mode", "Run", "R");
	gt_macro_set_take_lists(set, mode);
	g_assert_true(gt_macro_set_get_lists(set) == mode);
	g_assert_true(gt_macro_set_set_arg(set, id, 0, "R"));
	gt_macro_set_take_lists(set, vide);
	g_assert_null(gt_macro_set_find(set, id)->args);
	/* apply_edits prend les listes avant de recompter les arguments */
	mode = gt_macro_lists_new();
	gt_macro_lists_add(mode, "Mode", "Run", "R");
	gt_macro_set_take_lists(set, mode);
	g_assert_true(gt_macro_set_set_arg(set, id, 0, "R"));
	vide = gt_macro_lists_new();
	gt_macro_set_apply_edits(set, rows, 1, vide);
	g_assert_true(gt_macro_set_get_lists(set) == vide);
	g_assert_null(gt_macro_set_find(set, id)->args);
	gt_macro_set_take_lists(set, NULL);
	g_assert_nonnull(gt_macro_set_get_lists(set));
	gt_macro_set_free(set);
}

static void
test_separateurs(void)
{
	gchar *label = (gchar *)"x";
	GtMacroSet *set = gt_macro_set_new();
	guint vide = gt_macro_set_append(set, "V", "", "", "");
	guint sep = gt_macro_set_append(set, "S", "", "[Titre]%&", "");
	guint ok = gt_macro_set_append(set, "O", "", "x", "");

	g_assert_true(gt_macro_action_is_separator("%&", &label));
	g_assert_null(label);
	g_assert_true(gt_macro_action_is_separator("[Titre]%&", &label));
	g_assert_cmpstr(label, ==, "Titre");
	g_free(label);
	g_assert_true(gt_macro_action_is_separator("[Titre]  %&", NULL));
	g_assert_false(gt_macro_action_is_separator("[Titre]x%&", NULL));
	g_assert_false(gt_macro_action_is_separator("%&x", NULL));
	g_assert_false(gt_macro_action_is_separator("[sans fin%&", NULL));
	g_assert_false(gt_macro_action_is_separator(NULL, NULL));
	g_assert_false(gt_macro_is_sendable(gt_macro_set_find(set, vide)));
	g_assert_false(gt_macro_is_sendable(gt_macro_set_find(set, sep)));
	g_assert_true(gt_macro_is_sendable(gt_macro_set_find(set, ok)));
	g_assert_false(gt_macro_is_sendable(NULL));
	gt_macro_set_free(set);
}

static void
test_expansion(void)
{
	GtMacroSet *set = gt_macro_set_new();
	GtMacroLists *mode = gt_macro_lists_new();
	guint v, nu, m, p;
	const gchar *sept[] = { "7" };
	gchar *s;

	gt_macro_lists_add(mode, "Mode", "Run", "R");
	gt_macro_set_take_lists(set, mode);
	v = gt_macro_set_append(set, "V", "", "V%d\\r", "");
	nu = gt_macro_set_append(set, "N", "", "N%d", "");
	m = gt_macro_set_append(set, "M", "", "M %#Mode", "");
	p = gt_macro_set_append(set, "P", "", "100%%", "");
	gt_macro_set_set_arg(set, v, 0, "42");
	gt_macro_set_set_arg(set, m, 0, "S");

	s = expansion(set, gt_macro_set_find(set, v), NULL, -1);
	g_assert_cmpstr(s, ==, "V42\r");
	g_free(s);
	s = expansion(set, gt_macro_set_find(set, v), sept, 1);
	g_assert_cmpstr(s, ==, "V7\r");
	g_free(s);
	s = expansion(set, gt_macro_set_find(set, v), NULL, 0);
	g_assert_cmpstr(s, ==, "V%d\r");
	g_free(s);
	s = expansion(set, gt_macro_set_find(set, nu), NULL, -1);
	g_assert_cmpstr(s, ==, "N%d");
	g_free(s);
	s = expansion(set, gt_macro_set_find(set, m), NULL, -1);
	g_assert_cmpstr(s, ==, "M S");
	g_free(s);
	s = expansion(set, gt_macro_set_find(set, p), NULL, -1);
	g_assert_cmpstr(s, ==, "100%");
	g_free(s);
	g_assert_null(gt_macro_expand(set, NULL, NULL, -1));
	gt_macro_set_free(set);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/macro_set/normalisation", test_normalisation);
	g_test_add_func("/macro_set/identifiants", test_identifiants);
	g_test_add_func("/macro_set/arguments", test_arguments);
	g_test_add_func("/macro_set/polling-bornes", test_polling_bornes);
	g_test_add_func("/macro_set/periode-saisie", test_periode_saisie);
	g_test_add_func("/macro_set/edition-suppression", test_edition_suppression);
	g_test_add_func("/macro_set/edition-deplacement-et-tri", test_edition_deplacement_et_tri);
	g_test_add_func("/macro_set/edition-ajout-et-doublon", test_edition_ajout_et_doublon);
	g_test_add_func("/macro_set/edition-change-arguments", test_edition_change_arguments);
	g_test_add_func("/macro_set/listes-et-arguments", test_listes_et_arguments);
	g_test_add_func("/macro_set/separateurs", test_separateurs);
	g_test_add_func("/macro_set/expansion", test_expansion);
	return g_test_run();
}
```

Ajouter `'test_macro_set' : [],` au dictionnaire `unit_tests` de `tests/meson.build`.

- [ ] **Step 2 : vérifier l'échec**

Run : `ninja -C build 2>&1 | tail -3`
Expected : échec de compilation (`macro_set.h` introuvable).

- [ ] **Step 3 : écrire `src/macro_set.h`**

```c
/* macro_set.h — jeu de macros sans GTK : identifiants stables, arguments enregistrés, réglages de polling, expansion unique.
 * Utilisé uniquement sur le fil principal. */

#ifndef MACRO_SET_H_
#define MACRO_SET_H_

#include <glib.h>

#include "macros_list.h"

#define GT_MACRO_PERIOD_MIN_MS     1u
#define GT_MACRO_PERIOD_MAX_MS     86400000u	/* 24 h */
#define GT_MACRO_PERIOD_DEFAULT_MS 1000u

typedef struct
{
	guint    id;			/* stable pendant la vie du jeu, jamais 0, jamais réutilisé dans le processus */
	gchar   *label;			/* jamais NULL ("" possible) */
	gchar   *shortcut;		/* jamais NULL ; "" = aucun raccourci */
	gchar   *action;		/* jamais NULL */
	gchar   *tab;			/* jamais NULL ; "" = onglet par défaut (nom décidé par l'interface) */
	gchar  **args;			/* NULL, ou exactement macro_count_format_args() éléments */
	gboolean polling;		/* mode polling activé (persistant) ; la marche n'est jamais persistée */
	guint    polling_period_ms;	/* toujours dans [GT_MACRO_PERIOD_MIN_MS, GT_MACRO_PERIOD_MAX_MS] */
} GtMacro;

typedef struct _GtMacroSet GtMacroSet;

/* Une ligne de la fenêtre d'édition : id 0 = nouvelle macro. */
typedef struct
{
	guint        id;
	const gchar *label;
	const gchar *shortcut;
	const gchar *action;
	const gchar *tab;
} GtMacroEdit;

GtMacroSet         *gt_macro_set_new          (void);
void                gt_macro_set_free         (GtMacroSet *set);
guint               gt_macro_set_count        (const GtMacroSet *set);
const GtMacro      *gt_macro_set_get          (const GtMacroSet *set, guint index);
const GtMacro      *gt_macro_set_find         (const GtMacroSet *set, guint id);
gint                gt_macro_set_index_of     (const GtMacroSet *set, guint id);
const GtMacro      *gt_macro_set_find_label   (const GtMacroSet *set, const gchar *label);
guint               gt_macro_set_append       (GtMacroSet *set, const gchar *label, const gchar *shortcut, const gchar *action, const gchar *tab);
const GtMacroLists *gt_macro_set_get_lists    (const GtMacroSet *set);
void                gt_macro_set_take_lists   (GtMacroSet *set, GtMacroLists *lists);
gboolean            gt_macro_set_set_arg      (GtMacroSet *set, guint id, gint arg_index, const gchar *value);
gboolean            gt_macro_set_set_args     (GtMacroSet *set, guint id, const gchar *const *args, gint n_args);
gboolean            gt_macro_set_set_polling  (GtMacroSet *set, guint id, gboolean polling, guint period_ms);
void                gt_macro_set_apply_edits  (GtMacroSet *set, const GtMacroEdit *rows, guint n_rows, GtMacroLists *lists);

gboolean            gt_macro_action_is_separator(const gchar *action, gchar **label_out);
gboolean            gt_macro_is_sendable      (const GtMacro *macro);
GBytes             *gt_macro_expand           (const GtMacroSet *set, const GtMacro *macro, const gchar *const *args, gint n_args);
gboolean            gt_macro_period_parse     (const gchar *text, guint *period_ms);

#endif
```

- [ ] **Step 4 : écrire `src/macro_set.c`**

```c
/* macro_set.c — jeu de macros sans GTK (voir macro_set.h). */

#include <string.h>

#include "macro_set.h"
#include "macros_format.h"

struct _GtMacroSet
{
	GPtrArray    *macros;	/* GtMacro *, possédées, dans l'ordre d'affichage */
	GtMacroLists *lists;	/* jamais NULL */
};

/* Compteur du processus : un identifiant n'est jamais réutilisé, même par un autre jeu (fil principal seulement) */
static guint next_id = 1;

static void
macro_free(gpointer data)
{
	GtMacro *m = data;

	/* apply_edits laisse des trous (NULL) dans l'ancien tableau pour les macros reprises */
	if(m == NULL)
		return;
	g_free(m->label);
	g_free(m->shortcut);
	g_free(m->action);
	g_free(m->tab);
	g_strfreev(m->args);
	g_free(m);
}

static gchar *
normalized(const gchar *s)
{
	return g_strdup(s != NULL ? s : "");
}

/* « None » était la valeur « pas de raccourci » de l'ancienne fenêtre d'édition */
static gchar *
normalized_shortcut(const gchar *s)
{
	if(s == NULL || strcmp(s, "None") == 0)
		return g_strdup("");
	return g_strdup(s);
}

static guint
clamp_period(guint period_ms)
{
	if(period_ms == 0)
		return GT_MACRO_PERIOD_DEFAULT_MS;
	return CLAMP(period_ms, GT_MACRO_PERIOD_MIN_MS, GT_MACRO_PERIOD_MAX_MS);
}

static void
set_fields(GtMacro *m, const gchar *label, const gchar *shortcut, const gchar *action, const gchar *tab)
{
	g_free(m->label);
	g_free(m->shortcut);
	g_free(m->action);
	g_free(m->tab);
	m->label = normalized(label);
	m->shortcut = normalized_shortcut(shortcut);
	m->action = normalized(action);
	m->tab = normalized(tab);
}

static GtMacro *
macro_new(const gchar *label, const gchar *shortcut, const gchar *action, const gchar *tab)
{
	GtMacro *m = g_new0(GtMacro, 1);

	m->id = next_id++;
	set_fields(m, label, shortcut, action, tab);
	m->polling_period_ms = GT_MACRO_PERIOD_DEFAULT_MS;
	return m;
}

/* Les arguments enregistrés ne restent que s'ils correspondent encore au nombre de spécificateurs */
static void
check_args(const GtMacroSet *set, GtMacro *m)
{
	if(m->args != NULL && g_strv_length(m->args) != (guint)macro_count_format_args(set->lists, m->action))
		g_clear_pointer(&m->args, g_strfreev);
}

static GtMacro *
find_mutable(const GtMacroSet *set, guint id)
{
	if(set == NULL || id == 0)
		return NULL;
	for(guint i = 0; i < set->macros->len; i++)
	{
		GtMacro *m = g_ptr_array_index(set->macros, i);

		if(m->id == id)
			return m;
	}
	return NULL;
}

GtMacroSet *
gt_macro_set_new(void)
{
	GtMacroSet *set = g_new0(GtMacroSet, 1);

	set->macros = g_ptr_array_new_with_free_func(macro_free);
	set->lists = gt_macro_lists_new();
	return set;
}

void
gt_macro_set_free(GtMacroSet *set)
{
	if(set == NULL)
		return;
	g_ptr_array_unref(set->macros);
	gt_macro_lists_free(set->lists);
	g_free(set);
}

guint
gt_macro_set_count(const GtMacroSet *set)
{
	return set != NULL ? set->macros->len : 0;
}

const GtMacro *
gt_macro_set_get(const GtMacroSet *set, guint index)
{
	if(set == NULL || index >= set->macros->len)
		return NULL;
	return g_ptr_array_index(set->macros, index);
}

const GtMacro *
gt_macro_set_find(const GtMacroSet *set, guint id)
{
	return find_mutable(set, id);
}

gint
gt_macro_set_index_of(const GtMacroSet *set, guint id)
{
	if(set == NULL || id == 0)
		return -1;
	for(guint i = 0; i < set->macros->len; i++)
		if(((GtMacro *)g_ptr_array_index(set->macros, i))->id == id)
			return (gint)i;
	return -1;
}

const GtMacro *
gt_macro_set_find_label(const GtMacroSet *set, const gchar *label)
{
	if(set == NULL || label == NULL)
		return NULL;
	for(guint i = 0; i < set->macros->len; i++)
	{
		GtMacro *m = g_ptr_array_index(set->macros, i);

		if(strcmp(m->label, label) == 0)
			return m;
	}
	return NULL;
}

guint
gt_macro_set_append(GtMacroSet *set, const gchar *label, const gchar *shortcut, const gchar *action, const gchar *tab)
{
	GtMacro *m = macro_new(label, shortcut, action, tab);

	g_ptr_array_add(set->macros, m);
	return m->id;
}

const GtMacroLists *
gt_macro_set_get_lists(const GtMacroSet *set)
{
	return set != NULL ? set->lists : NULL;
}

void
gt_macro_set_take_lists(GtMacroSet *set, GtMacroLists *lists)
{
	gt_macro_lists_free(set->lists);
	set->lists = lists != NULL ? lists : gt_macro_lists_new();
	for(guint i = 0; i < set->macros->len; i++)
		check_args(set, g_ptr_array_index(set->macros, i));
}

gboolean
gt_macro_set_set_arg(GtMacroSet *set, guint id, gint arg_index, const gchar *value)
{
	GtMacro *m = find_mutable(set, id);
	gint n;

	if(m == NULL)
		return FALSE;
	n = macro_count_format_args(set->lists, m->action);
	if(arg_index < 0 || arg_index >= n)
		return FALSE;
	if(m->args == NULL)
	{
		m->args = g_new0(gchar *, n + 1);
		for(gint k = 0; k < n; k++)
			m->args[k] = g_strdup("");
	}
	g_free(m->args[arg_index]);
	m->args[arg_index] = g_strdup(value != NULL ? value : "");
	return TRUE;
}

gboolean
gt_macro_set_set_args(GtMacroSet *set, guint id, const gchar *const *args, gint n_args)
{
	GtMacro *m = find_mutable(set, id);
	gint n;

	if(m == NULL)
		return FALSE;
	n = macro_count_format_args(set->lists, m->action);
	if(n_args != n)
		return FALSE;
	g_clear_pointer(&m->args, g_strfreev);
	if(n == 0)
		return TRUE;
	m->args = g_new0(gchar *, n + 1);
	for(gint k = 0; k < n; k++)
		m->args[k] = g_strdup(args != NULL && args[k] != NULL ? args[k] : "");
	return TRUE;
}

gboolean
gt_macro_set_set_polling(GtMacroSet *set, guint id, gboolean polling, guint period_ms)
{
	GtMacro *m = find_mutable(set, id);

	if(m == NULL)
		return FALSE;
	m->polling = polling;
	m->polling_period_ms = clamp_period(period_ms);
	return TRUE;
}

void
gt_macro_set_apply_edits(GtMacroSet *set, const GtMacroEdit *rows, guint n_rows, GtMacroLists *lists)
{
	GPtrArray *old = set->macros;
	GPtrArray *fresh = g_ptr_array_new_with_free_func(macro_free);

	/* Les listes d'abord : « %#Nom » n'est un argument que si la liste existe */
	if(lists != NULL)
	{
		gt_macro_lists_free(set->lists);
		set->lists = lists;
	}
	for(guint i = 0; i < n_rows; i++)
	{
		GtMacro *m = NULL;

		/* Identifiant connu et pas encore repris : la macro garde son identité, ses arguments et son polling (M16) */
		for(guint k = 0; rows[i].id != 0 && k < old->len; k++)
		{
			GtMacro *o = g_ptr_array_index(old, k);

			if(o != NULL && o->id == rows[i].id)
			{
				m = o;
				old->pdata[k] = NULL;
				break;
			}
		}
		if(m == NULL)
			m = macro_new(rows[i].label, rows[i].shortcut, rows[i].action, rows[i].tab);
		else
			set_fields(m, rows[i].label, rows[i].shortcut, rows[i].action, rows[i].tab);
		g_ptr_array_add(fresh, m);
	}
	set->macros = fresh;
	g_ptr_array_unref(old);
	for(guint i = 0; i < set->macros->len; i++)
		check_args(set, g_ptr_array_index(set->macros, i));
}

gboolean
gt_macro_action_is_separator(const gchar *action, gchar **label_out)
{
	const gchar *end, *rest;

	if(label_out != NULL)
		*label_out = NULL;
	if(action == NULL)
		return FALSE;
	if(action[0] != '[')
		return strcmp(action, "%&") == 0;
	end = strchr(action + 1, ']');
	if(end == NULL)
		return FALSE;
	for(rest = end + 1; *rest == ' '; rest++)
		;
	if(strcmp(rest, "%&") != 0)
		return FALSE;
	if(label_out != NULL)
		*label_out = g_strndup(action + 1, end - action - 1);
	return TRUE;
}

gboolean
gt_macro_is_sendable(const GtMacro *macro)
{
	return macro != NULL && macro->action[0] != '\0' && !gt_macro_action_is_separator(macro->action, NULL);
}

GBytes *
gt_macro_expand(const GtMacroSet *set, const GtMacro *macro, const gchar *const *args, gint n_args)
{
	const gchar *const *use = args;
	gint n = n_args;
	gchar *formatted;
	GByteArray *bytes;

	if(macro == NULL)
		return NULL;
	if(n_args < 0)
	{
		use = (const gchar *const *)macro->args;
		n = macro->args != NULL ? (gint)g_strv_length(macro->args) : 0;
	}
	else if(args == NULL)
		n = 0;
	formatted = format_action_with_args(gt_macro_set_get_lists(set), macro->action, (const gchar **)use, n);
	bytes = parse_macro_string(formatted);
	g_free(formatted);
	return g_byte_array_free_to_bytes(bytes);
}

gboolean
gt_macro_period_parse(const gchar *text, guint *period_ms)
{
	guint64 value;

	/* g_ascii_string_to_unsigned refuse signe, blancs, texte en trop et dépassement */
	if(text == NULL || !g_ascii_string_to_unsigned(text, 10, GT_MACRO_PERIOD_MIN_MS, GT_MACRO_PERIOD_MAX_MS, &value, NULL))
		return FALSE;
	*period_ms = (guint)value;
	return TRUE;
}
```

Ajouter `'macro_set.c',` et `'macro_set.h',` à `core_sources` dans `src/meson.build` (après `'macros_list.h',`).

Note : `g_ascii_string_to_unsigned` accepte-t-il « +5 » ? Non : la documentation GLib indique que seuls les chiffres sont admis (pas de signe) ; si le test « +5 » échoue malgré tout, ajouter avant l'appel `if(!g_ascii_isdigit(text[0])) return FALSE;`.

- [ ] **Step 5 : construire et tester**

Run : `ninja -C build 2>&1 | tail -3 && meson test -C build test_macro_set --print-errorlogs 2>&1 | tail -5`
Expected : `test_macro_set` OK (12 cas).

Run : `meson test -C build --print-errorlogs 2>&1 | tail -4`
Expected : tout OK.

- [ ] **Step 6 : commit**

```bash
git add src/macro_set.c src/macro_set.h src/meson.build tests/test_macro_set.c tests/meson.build
git commit -m "macros : modèle sans GTK GtMacroSet (identifiants stables, édition par identifiant, expansion unique)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 3 : fichier de macros v2, ancien format, copie avant migration

**Files :**
- Create : `src/macro_file.h`, `src/macro_file.c`, `tests/test_macro_file.c`
- Modify : `src/meson.build` (`core_sources`), `tests/meson.build` (`unit_tests`)

**Interfaces :**
- Consumes : `GtMacroSet` et ses fonctions (tâche 2), `GtMacroLists` (tâche 1).
- Produces : `gboolean gt_macro_file_load(const gchar *path, GtMacroSet **set_out, GError **error)` ; `gboolean gt_macro_file_save(const GtMacroSet *set, const gchar *path, GError **error)`. Fichier absent → `FALSE` + `G_FILE_ERROR_NOENT`. `*set_out` n'est écrit qu'en cas de succès.

- [ ] **Step 1 : écrire le test**

Créer `tests/test_macro_file.c` :

```c
/* Tests de macro_file.c : fichier de macros v2 (valeurs échappées), lecture de l'ancien format, copie avant migration. */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "macro_file.h"
#include "test_util.h"

#define GENERAL "G\303\251n\303\251ral"	/* « Général » en UTF-8 */

static GtMacroSet *
charger(const gchar *path)
{
	GtMacroSet *set = NULL;
	GError *error = NULL;

	g_assert_true(gt_macro_file_load(path, &set, &error));
	g_assert_no_error(error);
	g_assert_nonnull(set);
	return set;
}

static gchar *
lire(const gchar *path)
{
	gchar *texte = NULL;

	g_assert_true(g_file_get_contents(path, &texte, NULL, NULL));
	return texte;
}

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

static void
test_aller_retour(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "m.ini", NULL);
	GtMacroSet *set = gt_macro_set_new(), *relu;
	GtMacroLists *lists = gt_macro_lists_new();
	const GtMacroLists *l2;
	const gchar *deux[] = { "", "a;b\\c" };
	GError *error = NULL;
	guint id1, id2, id3;
	gchar *texte;

	gt_macro_lists_add(lists, "Vitesses", "Lent", "0");
	gt_macro_lists_add(lists, "Vitesses", "Rapide", "1");
	gt_macro_lists_add(lists, "S\303\251p;ar\303\251e", "a;b", "x\\y");
	gt_macro_set_take_lists(set, lists);
	id1 = gt_macro_set_append(set, "  Lecture", "<Control>F5", "R%d::x|y;z=w\\r", GENERAL);
	gt_macro_set_set_arg(set, id1, 0, "42");
	gt_macro_set_set_polling(set, id1, TRUE, 250);
	id2 = gt_macro_set_append(set, "Vide", "", "V%s %s", "");
	g_assert_true(gt_macro_set_set_args(set, id2, deux, 2));
	id3 = gt_macro_set_append(set, "Liste", "", "S %#Vitesses", "Onglet 2");
	gt_macro_set_set_arg(set, id3, 0, "1");
	gt_macro_set_append(set, "", "", "%&", "");

	g_assert_true(gt_macro_file_save(set, path, &error));
	g_assert_no_error(error);
	texte = lire(path);
	g_assert_nonnull(strstr(texte, "[gtkterm-macros]\nversion=2\n"));
	g_assert_nonnull(strstr(texte, "[macro:1]"));
	g_assert_nonnull(strstr(texte, "[list:1]"));
	g_free(texte);

	relu = charger(path);
	g_assert_cmpuint(gt_macro_set_count(relu), ==, 4);
	for(guint i = 0; i < 4; i++)
		comparer_macro(gt_macro_set_get(set, i), gt_macro_set_get(relu, i));
	l2 = gt_macro_set_get_lists(relu);
	g_assert_cmpint(gt_macro_lists_count(l2), ==, 2);
	g_assert_cmpstr(gt_macro_lists_name(l2, 1), ==, "S\303\251p;ar\303\251e");
	g_assert_cmpstr(gt_macro_lists_entry_display(l2, 1, 0), ==, "a;b");
	g_assert_cmpstr(gt_macro_lists_entry_value(l2, 1, 0), ==, "x\\y");
	g_assert_cmpstr(gt_macro_lists_entry_value(l2, 0, 1), ==, "1");
	gt_macro_set_free(relu);
	gt_macro_set_free(set);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

static void
test_ordre_conserve(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "m.ini", NULL);
	GtMacroSet *set = gt_macro_set_new(), *relu;

	for(guint i = 0; i < 12; i++)
	{
		gchar *label = g_strdup_printf("M%u", i);
		gt_macro_set_append(set, label, "", label, "");
		g_free(label);
	}
	g_assert_true(gt_macro_file_save(set, path, NULL));
	relu = charger(path);
	g_assert_cmpuint(gt_macro_set_count(relu), ==, 12);
	for(guint i = 0; i < 12; i++)
		g_assert_cmpstr(gt_macro_set_get(relu, i)->label, ==, gt_macro_set_get(set, i)->label);
	gt_macro_set_free(relu);
	gt_macro_set_free(set);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

static void
test_version_refusee(void)
{
	const gchar *contenus[] = {
		"[gtkterm-macros]\nversion=3\n[macro:1]\naction=x\n",
		"[gtkterm-macros]\n[macro:1]\naction=x\n",
		"[gtkterm-macros]\nversion=abc\n",
		"[gtkterm-macros]\nversion=1\n",
	};
	gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);

	for(guint i = 0; i < G_N_ELEMENTS(contenus); i++)
	{
		gchar *path = test_write_file(dir, "m.ini", contenus[i]);
		GtMacroSet *set = NULL;
		GError *error = NULL;

		g_assert_false(gt_macro_file_load(path, &set, &error));
		g_assert_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE);
		g_assert_null(set);
		g_clear_error(&error);
		g_free(path);
	}
	test_rmdir(dir);
	g_free(dir);
}

static void
test_cles_manquantes_ou_invalides(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);
	gchar *path = test_write_file(dir, "m.ini",
		"[gtkterm-macros]\nversion=2\n"
		"[macro:1]\naction=A%d\nargs=1;2;\npolling=peut-etre\npolling_period_ms=-5\n"
		"[macro:2]\nlabel=B\npolling=true\npolling_period_ms=99999999999\n"
		"[macro:3]\nlabel=C\npolling=true\npolling_period_ms=0\n"
		"[list:1]\ndisplays=x;\n"
		"[list:2]\nname=L\ndisplays=a;b;c;\nvalues=1;\n"
		"[list:3]\nname=L\ndisplays=d;\nvalues=4;5;\n"
		"[autre]\ncle=valeur\n");
	GtMacroSet *set = charger(path);
	const GtMacro *m1 = gt_macro_set_get(set, 0), *m2 = gt_macro_set_get(set, 1), *m3 = gt_macro_set_get(set, 2);
	const GtMacroLists *l = gt_macro_set_get_lists(set);

	g_assert_cmpuint(gt_macro_set_count(set), ==, 3);
	g_assert_cmpstr(m1->label, ==, "");
	g_assert_cmpstr(m1->shortcut, ==, "");
	g_assert_cmpstr(m1->tab, ==, "");
	g_assert_cmpstr(m1->action, ==, "A%d");
	g_assert_null(m1->args);
	g_assert_false(m1->polling);
	g_assert_cmpuint(m1->polling_period_ms, ==, GT_MACRO_PERIOD_DEFAULT_MS);
	g_assert_cmpstr(m2->label, ==, "B");
	g_assert_cmpstr(m2->action, ==, "");
	g_assert_true(m2->polling);
	g_assert_cmpuint(m2->polling_period_ms, ==, GT_MACRO_PERIOD_MAX_MS);
	g_assert_cmpuint(m3->polling_period_ms, ==, GT_MACRO_PERIOD_DEFAULT_MS);
	g_assert_cmpint(gt_macro_lists_count(l), ==, 1);
	g_assert_cmpstr(gt_macro_lists_name(l, 0), ==, "L");
	g_assert_cmpint(gt_macro_lists_entry_count(l, 0), ==, 4);
	g_assert_cmpstr(gt_macro_lists_entry_value(l, 0, 0), ==, "1");
	g_assert_cmpstr(gt_macro_lists_entry_value(l, 0, 1), ==, "b");
	g_assert_cmpstr(gt_macro_lists_entry_value(l, 0, 2), ==, "c");
	g_assert_cmpstr(gt_macro_lists_entry_display(l, 0, 3), ==, "d");
	g_assert_cmpstr(gt_macro_lists_entry_value(l, 0, 3), ==, "4");
	gt_macro_set_free(set);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

#define ANCIEN \
	"[macros]\n" \
	"macro_0=Lecture::<Control>F5::R%d\\\\r::" GENERAL "::42::1::250\n" \
	"macro_1=\n" \
	"macro_2=SansTab::None::X\n" \
	"macro_3=Deux::None\n" \
	"macro_4=Liste::None::S %#Vitesses::::1::0\n" \
	"macro_5=Args::None::A%d %d::T::1|2\n" \
	"macro_6=Pol::None::P::T::::1\n" \
	"macro_7=Faux::None::F%d::::1|2::0::1000\n" \
	"[lists]\n" \
	"list_0=Vitesses:Lent=0|Rapide=1\n" \
	"list_1=Sans:a|b\n" \
	"list_2=pasdedeuxpoints\n"

static void
test_ancien_format(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);
	gchar *path = test_write_file(dir, "m.ini", ANCIEN);
	GtMacroSet *set = charger(path);
	const GtMacroLists *l = gt_macro_set_get_lists(set);
	const GtMacro *m;

	/* macro_1 (vide) et macro_3 (moins de deux « :: ») ignorées */
	g_assert_cmpuint(gt_macro_set_count(set), ==, 6);
	m = gt_macro_set_get(set, 0);
	g_assert_cmpstr(m->label, ==, "Lecture");
	g_assert_cmpstr(m->shortcut, ==, "<Control>F5");
	g_assert_cmpstr(m->action, ==, "R%d\\r");
	g_assert_cmpstr(m->tab, ==, GENERAL);
	g_assert_cmpstr(m->args[0], ==, "42");
	g_assert_true(m->polling);
	g_assert_cmpuint(m->polling_period_ms, ==, 250);
	m = gt_macro_set_get(set, 1);
	g_assert_cmpstr(m->label, ==, "SansTab");
	g_assert_cmpstr(m->shortcut, ==, "");
	g_assert_cmpstr(m->action, ==, "X");
	g_assert_cmpstr(m->tab, ==, "");
	m = gt_macro_set_get(set, 2);
	g_assert_cmpstr(m->args[0], ==, "1");
	g_assert_false(m->polling);
	g_assert_cmpuint(m->polling_period_ms, ==, GT_MACRO_PERIOD_DEFAULT_MS);
	m = gt_macro_set_get(set, 3);
	g_assert_cmpstr(m->tab, ==, "T");
	g_assert_cmpstr(m->args[0], ==, "1");
	g_assert_cmpstr(m->args[1], ==, "2");
	m = gt_macro_set_get(set, 4);
	g_assert_cmpstr(m->label, ==, "Pol");
	g_assert_null(m->args);
	g_assert_true(m->polling);
	m = gt_macro_set_get(set, 5);
	g_assert_null(m->args);			/* 2 arguments enregistrés pour 1 spécificateur */
	g_assert_cmpint(gt_macro_lists_count(l), ==, 2);
	g_assert_cmpstr(gt_macro_lists_entry_value(l, 0, 1), ==, "1");
	g_assert_cmpstr(gt_macro_lists_name(l, 1), ==, "Sans");
	g_assert_cmpstr(gt_macro_lists_entry_value(l, 1, 1), ==, "b");
	gt_macro_set_free(set);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

static void
test_ancien_sans_macros(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);
	gchar *path = test_write_file(dir, "m.ini", "[lists]\nlist_0=L:a=1\n");
	GtMacroSet *set = charger(path);

	g_assert_cmpuint(gt_macro_set_count(set), ==, 0);
	g_assert_cmpint(gt_macro_lists_count(gt_macro_set_get_lists(set)), ==, 1);
	gt_macro_set_free(set);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

static void
test_fichier_vide_ou_inconnu(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);
	gchar *vide = test_write_file(dir, "vide.ini", "");
	gchar *inconnu = test_write_file(dir, "inconnu.ini", "[autre]\nx=1\n");
	GtMacroSet *set = charger(vide);

	g_assert_cmpuint(gt_macro_set_count(set), ==, 0);
	gt_macro_set_free(set);
	set = charger(inconnu);
	g_assert_cmpuint(gt_macro_set_count(set), ==, 0);
	gt_macro_set_free(set);
	test_rmdir(dir);
	g_free(vide);
	g_free(inconnu);
	g_free(dir);
}

static void
test_fichier_absent_ou_corrompu(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);
	gchar *absent = g_build_filename(dir, "absent.ini", NULL);
	gchar *corrompu = test_write_file(dir, "corrompu.ini", "pas un fichier ini\n[[[\n");
	GtMacroSet *set = NULL;
	GError *error = NULL;

	g_assert_false(gt_macro_file_load(absent, &set, &error));
	g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_NOENT);
	g_assert_null(set);
	g_clear_error(&error);
	g_assert_false(gt_macro_file_load(corrompu, &set, &error));
	g_assert_nonnull(error);
	g_assert_nonnull(strstr(error->message, "corrompu.ini"));
	g_assert_null(set);
	g_clear_error(&error);
	test_rmdir(dir);
	g_free(absent);
	g_free(corrompu);
	g_free(dir);
}

static void
test_copie_avant_migration(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);
	gchar *path = test_write_file(dir, "m.ini", ANCIEN);
	gchar *copie = g_strconcat(path, ".avant-migration", NULL);
	GtMacroSet *set = charger(path);
	gchar *texte;

	g_assert_false(g_file_test(copie, G_FILE_TEST_EXISTS));
	g_assert_true(gt_macro_file_save(set, path, NULL));
	texte = lire(copie);
	g_assert_cmpstr(texte, ==, ANCIEN);
	g_free(texte);
	texte = lire(path);
	g_assert_nonnull(strstr(texte, "[gtkterm-macros]"));
	g_free(texte);
	/* Second enregistrement : le fichier est v2, la copie ne bouge pas */
	gt_macro_set_append(set, "Nouvelle", "", "N", "");
	g_assert_true(gt_macro_file_save(set, path, NULL));
	texte = lire(copie);
	g_assert_cmpstr(texte, ==, ANCIEN);
	g_free(texte);
	/* Un autre ancien fichier au même endroit : la copie existante n'est jamais écrasée */
	g_free(test_write_file(dir, "m.ini", "[macros]\nmacro_0=Autre::None::O\n"));
	g_assert_true(gt_macro_file_save(set, path, NULL));
	texte = lire(copie);
	g_assert_cmpstr(texte, ==, ANCIEN);
	g_free(texte);
	gt_macro_set_free(set);
	test_rmdir(dir);
	g_free(copie);
	g_free(path);
	g_free(dir);
}

static void
test_pas_de_copie_pour_v2(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "m.ini", NULL);
	gchar *copie = g_strconcat(path, ".avant-migration", NULL);
	GtMacroSet *set = gt_macro_set_new();

	gt_macro_set_append(set, "A", "", "a", "");
	g_assert_true(gt_macro_file_save(set, path, NULL));
	g_assert_true(gt_macro_file_save(set, path, NULL));
	g_assert_false(g_file_test(copie, G_FILE_TEST_EXISTS));
	gt_macro_set_free(set);
	test_rmdir(dir);
	g_free(copie);
	g_free(path);
	g_free(dir);
}

static void
test_echec_ecriture(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-macro-file-XXXXXX", NULL);
	gchar *path = g_build_filename(dir, "absent", "m.ini", NULL);
	GtMacroSet *set = gt_macro_set_new();
	GError *error = NULL;

	g_assert_false(gt_macro_file_save(set, path, &error));
	g_assert_nonnull(error);
	g_assert_nonnull(strstr(error->message, "absent"));
	g_clear_error(&error);
	gt_macro_set_free(set);
	test_rmdir(dir);
	g_free(path);
	g_free(dir);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/macro_file/aller-retour", test_aller_retour);
	g_test_add_func("/macro_file/ordre-conserve", test_ordre_conserve);
	g_test_add_func("/macro_file/version-refusee", test_version_refusee);
	g_test_add_func("/macro_file/cles-manquantes-ou-invalides", test_cles_manquantes_ou_invalides);
	g_test_add_func("/macro_file/ancien-format", test_ancien_format);
	g_test_add_func("/macro_file/ancien-sans-macros", test_ancien_sans_macros);
	g_test_add_func("/macro_file/fichier-vide-ou-inconnu", test_fichier_vide_ou_inconnu);
	g_test_add_func("/macro_file/fichier-absent-ou-corrompu", test_fichier_absent_ou_corrompu);
	g_test_add_func("/macro_file/copie-avant-migration", test_copie_avant_migration);
	g_test_add_func("/macro_file/pas-de-copie-pour-v2", test_pas_de_copie_pour_v2);
	g_test_add_func("/macro_file/echec-ecriture", test_echec_ecriture);
	return g_test_run();
}
```

Ajouter `'test_macro_file' : [],` à `unit_tests` dans `tests/meson.build`.

- [ ] **Step 2 : vérifier l'échec**

Run : `ninja -C build 2>&1 | tail -3`
Expected : échec de compilation (`macro_file.h` introuvable).

- [ ] **Step 3 : écrire `src/macro_file.h`**

```c
/* macro_file.h — lecture et écriture du fichier de macros (format v2 échappé par GKeyFile, lecture de l'ancien format,
 * copie <fichier>.avant-migration au premier enregistrement d'un ancien fichier). */

#ifndef MACRO_FILE_H_
#define MACRO_FILE_H_

#include <glib.h>

#include "macro_set.h"

/* Fichier absent : FALSE + G_FILE_ERROR_NOENT. *set_out n'est écrit qu'en cas de succès. */
gboolean gt_macro_file_load(const gchar *path, GtMacroSet **set_out, GError **error);
gboolean gt_macro_file_save(const GtMacroSet *set, const gchar *path, GError **error);

#endif
```

- [ ] **Step 4 : écrire `src/macro_file.c`**

```c
/* macro_file.c — fichier de macros (voir macro_file.h et le §5 de la spec du sous-projet 4). */

#include <config.h>
#include <glib/gi18n.h>
#include <stdlib.h>
#include <string.h>

#include "macro_file.h"

#define HEADER_GROUP "gtkterm-macros"
#define FILE_VERSION 2
#define BACKUP_SUFFIX ".avant-migration"

static gchar *
get_string_or_empty(GKeyFile *kf, const gchar *group, const gchar *key)
{
	gchar *s = g_key_file_get_string(kf, group, key, NULL);

	return s != NULL ? s : g_strdup("");
}

/* Période lue comme texte : g_key_file_get_uint64 accepterait « -5 » (g_ascii_strtoull) */
static guint
get_period(GKeyFile *kf, const gchar *group)
{
	gchar *s = g_key_file_get_value(kf, group, "polling_period_ms", NULL);
	guint64 v = 0;
	guint period = GT_MACRO_PERIOD_DEFAULT_MS;

	if(s != NULL && g_ascii_string_to_unsigned(g_strstrip(s), 10, 0, G_MAXUINT64, &v, NULL) && v > 0)
		period = (guint)MIN(v, (guint64)GT_MACRO_PERIOD_MAX_MS);
	g_free(s);
	return period;
}

static GtMacroSet *
load_v2(GKeyFile *kf, GError **error)
{
	gchar *version = g_key_file_get_value(kf, HEADER_GROUP, "version", NULL);
	gboolean known = version != NULL && strcmp(g_strstrip(version), "2") == 0;
	gchar **groups;
	GtMacroLists *lists;
	GtMacroSet *set;

	g_free(version);
	if(!known)
	{
		g_set_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE, _("Unsupported macros file version (written by a newer GTKTerm?)"));
		return NULL;
	}
	groups = g_key_file_get_groups(kf, NULL);
	/* Les listes d'abord : « %#Nom » n'est un argument que si la liste existe (décompte des arguments enregistrés) */
	lists = gt_macro_lists_new();
	for(guint g = 0; groups[g] != NULL; g++)
	{
		gchar *name;
		gchar **displays, **values;
		gsize n_displays = 0, n_values = 0;

		if(!g_str_has_prefix(groups[g], "list:"))
			continue;
		name = get_string_or_empty(kf, groups[g], "name");
		displays = g_key_file_get_string_list(kf, groups[g], "displays", &n_displays, NULL);
		values = g_key_file_get_string_list(kf, groups[g], "values", &n_values, NULL);
		for(gsize k = 0; name[0] != '\0' && k < n_displays; k++)
			gt_macro_lists_add(lists, name, displays[k], k < n_values ? values[k] : NULL);
		g_strfreev(displays);
		g_strfreev(values);
		g_free(name);
	}
	set = gt_macro_set_new();
	gt_macro_set_take_lists(set, lists);
	for(guint g = 0; groups[g] != NULL; g++)
	{
		gchar *label, *shortcut, *action, *tab;
		gchar **args;
		gsize n_args = 0;
		GError *local = NULL;
		gboolean polling;
		guint id;

		if(!g_str_has_prefix(groups[g], "macro:"))
			continue;
		label = get_string_or_empty(kf, groups[g], "label");
		shortcut = get_string_or_empty(kf, groups[g], "shortcut");
		action = get_string_or_empty(kf, groups[g], "action");
		tab = get_string_or_empty(kf, groups[g], "tab");
		id = gt_macro_set_append(set, label, shortcut, action, tab);
		args = g_key_file_get_string_list(kf, groups[g], "args", &n_args, NULL);
		/* Refusé (arguments abandonnés) si leur nombre ne correspond pas aux spécificateurs */
		if(args != NULL)
			gt_macro_set_set_args(set, id, (const gchar *const *)args, (gint)n_args);
		polling = g_key_file_get_boolean(kf, groups[g], "polling", &local);
		if(local != NULL)
		{
			polling = FALSE;
			g_clear_error(&local);
		}
		gt_macro_set_set_polling(set, id, polling, get_period(kf, groups[g]));
		g_strfreev(args);
		g_free(label);
		g_free(shortcut);
		g_free(action);
		g_free(tab);
	}
	g_strfreev(groups);
	return set;
}

/* Ancien format, ligne de [macros] : « libellé::raccourci::action::onglet::arg1|arg2::polling(0/1)::période », les champs de
 * queue sont facultatifs et le dernier prend le reste de la ligne (M21 : séparateurs jamais échappés, lus tels quels). */
static void
legacy_add_macro(GtMacroSet *set, const gchar *value)
{
	gchar **f = g_strsplit(value, "::", 7);
	guint n = g_strv_length(f);
	guint id;

	/* Valeur vide ou sans au moins deux « :: » : pas une macro */
	if(n < 3)
	{
		g_strfreev(f);
		return;
	}
	id = gt_macro_set_append(set, f[0], f[1], f[2], n > 3 ? f[3] : "");
	if(n > 4)
	{
		gchar **args = g_strsplit(f[4], "|", -1);

		gt_macro_set_set_args(set, id, (const gchar *const *)args, (gint)g_strv_length(args));
		g_strfreev(args);
	}
	gt_macro_set_set_polling(set, id, n > 5 && strcmp(f[5], "1") == 0, n > 6 ? (guint)MIN(strtoul(f[6], NULL, 10), (gulong)GT_MACRO_PERIOD_MAX_MS) : 0);
	g_strfreev(f);
}

/* Ancien format, ligne de [lists] : « Nom:affichage=valeur|affichage » */
static void
legacy_add_list(GtMacroLists *lists, const gchar *value)
{
	const gchar *colon = strchr(value, ':');
	gchar *name;
	gchar **entries;

	if(colon == NULL || colon == value)
		return;
	name = g_strndup(value, colon - value);
	entries = g_strsplit(colon + 1, "|", -1);
	for(guint i = 0; entries[i] != NULL; i++)
	{
		gchar *eq = strchr(entries[i], '=');

		if(eq == NULL)
			gt_macro_lists_add(lists, name, entries[i], entries[i]);
		else
		{
			*eq = '\0';
			gt_macro_lists_add(lists, name, entries[i], eq + 1);
		}
	}
	g_strfreev(entries);
	g_free(name);
}

static void
legacy_read_group(GKeyFile *kf, const gchar *group, void (*add)(gpointer target, const gchar *value), gpointer target)
{
	gchar **keys = g_key_file_get_keys(kf, group, NULL, NULL);

	for(guint i = 0; keys != NULL && keys[i] != NULL; i++)
	{
		gchar *value = g_key_file_get_string(kf, group, keys[i], NULL);

		if(value != NULL && value[0] != '\0')
			add(target, value);
		g_free(value);
	}
	g_strfreev(keys);
}

static void
legacy_add_list_cb(gpointer target, const gchar *value)
{
	legacy_add_list(target, value);
}

static void
legacy_add_macro_cb(gpointer target, const gchar *value)
{
	legacy_add_macro(target, value);
}

static GtMacroSet *
load_legacy(GKeyFile *kf)
{
	GtMacroSet *set = gt_macro_set_new();
	GtMacroLists *lists = gt_macro_lists_new();

	/* Sans [macros], le jeu n'a aucune macro : plus de mélange « anciennes macros, nouvelles listes » (M21) */
	legacy_read_group(kf, "lists", legacy_add_list_cb, lists);
	gt_macro_set_take_lists(set, lists);
	legacy_read_group(kf, "macros", legacy_add_macro_cb, set);
	return set;
}

gboolean
gt_macro_file_load(const gchar *path, GtMacroSet **set_out, GError **error)
{
	gchar *data = NULL;
	gsize len = 0;
	GKeyFile *kf;
	GtMacroSet *set;
	GError *local = NULL;

	if(!g_file_get_contents(path, &data, &len, error))
		return FALSE;
	kf = g_key_file_new();
	if(len > 0 && !g_key_file_load_from_data(kf, data, len, G_KEY_FILE_NONE, &local))
	{
		g_propagate_prefixed_error(error, local, "%s: ", path);
		g_key_file_free(kf);
		g_free(data);
		return FALSE;
	}
	g_free(data);
	if(g_key_file_has_group(kf, HEADER_GROUP))
		set = load_v2(kf, error);
	else
		set = load_legacy(kf);
	g_key_file_free(kf);
	if(set == NULL)
		return FALSE;
	*set_out = set;
	return TRUE;
}

/* Copie à l'identique un ancien fichier avant sa réécriture au format v2 ; une copie existante n'est jamais écrasée */
static gboolean
backup_legacy(const gchar *path, GError **error)
{
	gchar *data = NULL, *backup;
	gsize len = 0;
	GKeyFile *kf;
	gboolean legacy, ok = TRUE;

	if(!g_file_get_contents(path, &data, &len, NULL))
		return TRUE;
	kf = g_key_file_new();
	legacy = len > 0 && g_key_file_load_from_data(kf, data, len, G_KEY_FILE_NONE, NULL) && !g_key_file_has_group(kf, HEADER_GROUP)
	         && (g_key_file_has_group(kf, "macros") || g_key_file_has_group(kf, "lists"));
	g_key_file_free(kf);
	backup = g_strconcat(path, BACKUP_SUFFIX, NULL);
	if(legacy && !g_file_test(backup, G_FILE_TEST_EXISTS))
		ok = g_file_set_contents(backup, data, len, error);
	g_free(backup);
	g_free(data);
	return ok;
}

gboolean
gt_macro_file_save(const GtMacroSet *set, const gchar *path, GError **error)
{
	GKeyFile *kf;
	const GtMacroLists *lists = gt_macro_set_get_lists(set);
	gchar *data;
	gsize len = 0;
	gboolean ok;

	if(!backup_legacy(path, error))
		return FALSE;
	kf = g_key_file_new();
	g_key_file_set_integer(kf, HEADER_GROUP, "version", FILE_VERSION);
	for(guint i = 0; i < gt_macro_set_count(set); i++)
	{
		const GtMacro *m = gt_macro_set_get(set, i);
		gchar *group = g_strdup_printf("macro:%u", i + 1);

		g_key_file_set_string(kf, group, "label", m->label);
		g_key_file_set_string(kf, group, "shortcut", m->shortcut);
		g_key_file_set_string(kf, group, "action", m->action);
		g_key_file_set_string(kf, group, "tab", m->tab);
		if(m->args != NULL)
			g_key_file_set_string_list(kf, group, "args", (const gchar *const *)m->args, g_strv_length(m->args));
		g_key_file_set_boolean(kf, group, "polling", m->polling);
		g_key_file_set_uint64(kf, group, "polling_period_ms", m->polling_period_ms);
		g_free(group);
	}
	for(gint li = 0; li < gt_macro_lists_count(lists); li++)
	{
		gint n = gt_macro_lists_entry_count(lists, li);
		const gchar **displays = g_new0(const gchar *, n + 1);
		const gchar **values = g_new0(const gchar *, n + 1);
		gchar *group = g_strdup_printf("list:%d", li + 1);

		for(gint ei = 0; ei < n; ei++)
		{
			displays[ei] = gt_macro_lists_entry_display(lists, li, ei);
			values[ei] = gt_macro_lists_entry_value(lists, li, ei);
		}
		g_key_file_set_string(kf, group, "name", gt_macro_lists_name(lists, li));
		g_key_file_set_string_list(kf, group, "displays", displays, n);
		g_key_file_set_string_list(kf, group, "values", values, n);
		g_free(group);
		g_free(displays);
		g_free(values);
	}
	data = g_key_file_to_data(kf, &len, NULL);
	g_key_file_free(kf);
	/* Écriture atomique (fichier temporaire puis renommage) ; le message d'erreur de GLib cite le chemin */
	ok = g_file_set_contents(path, data, len, error);
	g_free(data);
	return ok;
}
```

Ajouter `'macro_file.c',` et `'macro_file.h',` à `core_sources` (après `'macro_set.h',`).

- [ ] **Step 5 : construire et tester**

Run : `ninja -C build 2>&1 | tail -3 && meson test -C build test_macro_file --print-errorlogs 2>&1 | tail -5`
Expected : `test_macro_file` OK (11 cas).

Run : `meson test -C build --print-errorlogs 2>&1 | tail -4`
Expected : tout OK.

- [ ] **Step 6 : commit**

```bash
git add src/macro_file.c src/macro_file.h src/meson.build tests/test_macro_file.c tests/meson.build
git commit -m "macros : fichier v2 échappé par GKeyFile, lecture de l'ancien format, copie avant migration (M21)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 4 : ordonnanceur de polling (`GtMacroPoller`)

**Files :**
- Create : `src/macro_poller.h`, `src/macro_poller.c`, `tests/test_macro_poller.c`
- Modify : `src/meson.build` (`core_sources`), `tests/meson.build` (`unit_tests`)

**Interfaces :**
- Consumes : `GtMacroSet`, `gt_macro_set_find`, `gt_macro_is_sendable`, `GtMacro.polling`/`polling_period_ms` (tâche 2).
- Produces : tout le §7 de la spec (`GtMacroPoller`, `GtMacroPollerSendFunc`, `GtMacroPollerStateFunc`, `gt_macro_poller_*`).

- [ ] **Step 1 : écrire le test**

Créer `tests/test_macro_poller.c` :

```c
/* Tests de macro_poller.c : une minuterie GLib par macro en cours, envoi immédiat puis périodique, rappels d'état. */

#include <glib.h>

#include "macro_poller.h"

typedef struct
{
	GArray        *envois;		/* guint : identifiants envoyés, dans l'ordre */
	GArray        *etats;		/* guint : id * 2 + (running ? 1 : 0) */
	GtMacroPoller *poller;
	guint          arreter_au;	/* > 0 : la fonction d'envoi arrête la macro au n-ième envoi */
} Suivi;

static gboolean
envoyer(guint id, gpointer data)
{
	Suivi *s = data;

	g_array_append_val(s->envois, id);
	if(s->arreter_au != 0 && s->envois->len == s->arreter_au)
		gt_macro_poller_stop(s->poller, id);
	return TRUE;
}

static void
etat(guint id, gboolean running, gpointer data)
{
	Suivi *s = data;
	guint v = id * 2 + (running ? 1 : 0);

	g_array_append_val(s->etats, v);
}

static gboolean
quitter(gpointer data)
{
	g_main_loop_quit(data);
	return G_SOURCE_REMOVE;
}

static void
attendre(guint ms)
{
	GMainLoop *boucle = g_main_loop_new(NULL, FALSE);

	g_timeout_add(ms, quitter, boucle);
	g_main_loop_run(boucle);
	g_main_loop_unref(boucle);
}

static guint
compter(const Suivi *s, guint id)
{
	guint n = 0;

	for(guint i = 0; i < s->envois->len; i++)
		if(g_array_index(s->envois, guint, i) == id)
			n++;
	return n;
}

static guint
dernier_etat(const Suivi *s)
{
	g_assert_cmpuint(s->etats->len, >, 0);
	return g_array_index(s->etats, guint, s->etats->len - 1);
}

static void
suivi_init(Suivi *s, GtMacroSet *set)
{
	s->envois = g_array_new(FALSE, FALSE, sizeof(guint));
	s->etats = g_array_new(FALSE, FALSE, sizeof(guint));
	s->arreter_au = 0;
	s->poller = gt_macro_poller_new(set, envoyer, etat, s);
}

static void
suivi_fin(Suivi *s)
{
	gt_macro_poller_free(s->poller);
	g_array_unref(s->envois);
	g_array_unref(s->etats);
}

static guint
macro_en_polling(GtMacroSet *set, const gchar *label, const gchar *action, guint period)
{
	guint id = gt_macro_set_append(set, label, "", action, "");

	gt_macro_set_set_polling(set, id, TRUE, period);
	return id;
}

static void
test_envoi_immediat_puis_periodique(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint id = macro_en_polling(set, "A", "a", 30);
	Suivi s;
	guint n;

	suivi_init(&s, set);
	g_assert_true(gt_macro_poller_start(s.poller, id));
	g_assert_cmpuint(compter(&s, id), ==, 1);
	g_assert_true(gt_macro_poller_is_running(s.poller, id));
	g_assert_cmpuint(gt_macro_poller_running_count(s.poller), ==, 1);
	g_assert_cmpuint(dernier_etat(&s), ==, id * 2 + 1);
	attendre(110);
	n = compter(&s, id);
	g_assert_cmpuint(n, >=, 3);
	g_assert_cmpuint(n, <=, 6);
	gt_macro_poller_stop(s.poller, id);
	g_assert_false(gt_macro_poller_is_running(s.poller, id));
	g_assert_cmpuint(dernier_etat(&s), ==, id * 2);
	attendre(80);
	g_assert_cmpuint(compter(&s, id), ==, n);
	g_assert_cmpuint(gt_macro_poller_running_count(s.poller), ==, 0);
	suivi_fin(&s);
	gt_macro_set_free(set);
}

static void
test_demarrage_refuse(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint sans_mode = gt_macro_set_append(set, "S", "", "s", "");
	guint vide = macro_en_polling(set, "V", "", 30);
	guint sep = macro_en_polling(set, "P", "%&", 30);
	Suivi s;

	suivi_init(&s, set);
	g_assert_false(gt_macro_poller_start(s.poller, 9999999));
	g_assert_false(gt_macro_poller_start(s.poller, sans_mode));
	g_assert_false(gt_macro_poller_start(s.poller, vide));
	g_assert_false(gt_macro_poller_start(s.poller, sep));
	g_assert_cmpuint(s.envois->len, ==, 0);
	g_assert_cmpuint(s.etats->len, ==, 0);
	gt_macro_poller_stop(s.poller, sans_mode);		/* arrêter une macro qui ne tourne pas : sans effet */
	g_assert_cmpuint(s.etats->len, ==, 0);
	suivi_fin(&s);
	gt_macro_set_free(set);
}

static void
test_deja_en_cours_et_bascule(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint id = macro_en_polling(set, "A", "a", 1000);
	Suivi s;

	suivi_init(&s, set);
	g_assert_true(gt_macro_poller_start(s.poller, id));
	g_assert_true(gt_macro_poller_start(s.poller, id));
	g_assert_cmpuint(compter(&s, id), ==, 1);
	g_assert_cmpuint(s.etats->len, ==, 1);
	g_assert_false(gt_macro_poller_toggle(s.poller, id));
	g_assert_false(gt_macro_poller_is_running(s.poller, id));
	g_assert_true(gt_macro_poller_toggle(s.poller, id));
	g_assert_cmpuint(compter(&s, id), ==, 2);
	g_assert_cmpuint(s.etats->len, ==, 3);
	suivi_fin(&s);
	gt_macro_set_free(set);
}

static void
test_reset(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint a = macro_en_polling(set, "A", "a", 1000), b = macro_en_polling(set, "B", "b", 1000);
	Suivi s;

	suivi_init(&s, set);
	gt_macro_poller_start(s.poller, a);
	gt_macro_poller_start(s.poller, b);
	gt_macro_poller_reset(s.poller);
	g_assert_cmpuint(gt_macro_poller_running_count(s.poller), ==, 0);
	g_assert_cmpuint(s.etats->len, ==, 4);
	suivi_fin(&s);
	gt_macro_set_free(set);
}

static void
test_sync_suppression_et_mode(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint a = macro_en_polling(set, "A", "a", 1000), b = macro_en_polling(set, "B", "b", 1000), c = macro_en_polling(set, "C", "c", 1000);
	GtMacroEdit rows[] = { { b, "B", "", "b", "" }, { c, "C", "", "c", "" } };
	Suivi s;

	suivi_init(&s, set);
	gt_macro_poller_start(s.poller, a);
	gt_macro_poller_start(s.poller, b);
	gt_macro_poller_start(s.poller, c);
	gt_macro_set_apply_edits(set, rows, G_N_ELEMENTS(rows), NULL);
	gt_macro_set_set_polling(set, c, FALSE, 1000);
	gt_macro_poller_sync(s.poller);
	g_assert_false(gt_macro_poller_is_running(s.poller, a));
	g_assert_true(gt_macro_poller_is_running(s.poller, b));
	g_assert_false(gt_macro_poller_is_running(s.poller, c));
	g_assert_cmpuint(gt_macro_poller_running_count(s.poller), ==, 1);
	suivi_fin(&s);
	gt_macro_set_free(set);
}

static void
test_sync_action_videe(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint a = macro_en_polling(set, "A", "a", 1000);
	GtMacroEdit rows[] = { { a, "A", "", "", "" } };
	Suivi s;

	suivi_init(&s, set);
	gt_macro_poller_start(s.poller, a);
	gt_macro_set_apply_edits(set, rows, 1, NULL);
	gt_macro_poller_sync(s.poller);
	g_assert_false(gt_macro_poller_is_running(s.poller, a));
	suivi_fin(&s);
	gt_macro_set_free(set);
}

static void
test_changement_de_periode(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint a = macro_en_polling(set, "A", "a", 1000), b = macro_en_polling(set, "B", "b", 1000);
	Suivi s;

	suivi_init(&s, set);
	gt_macro_poller_start(s.poller, a);
	gt_macro_poller_start(s.poller, b);
	gt_macro_set_set_polling(set, a, TRUE, 30);
	gt_macro_set_set_polling(set, b, TRUE, 30);
	gt_macro_poller_sync(s.poller);
	gt_macro_poller_period_changed(s.poller, b);		/* déjà relancée par sync : sans effet supplémentaire */
	/* Pas d'envoi immédiat au changement de période */
	g_assert_cmpuint(compter(&s, a), ==, 1);
	g_assert_cmpuint(compter(&s, b), ==, 1);
	attendre(110);
	g_assert_cmpuint(compter(&s, a), >=, 3);
	g_assert_cmpuint(compter(&s, b), >=, 3);
	gt_macro_set_set_polling(set, a, TRUE, 1000);
	gt_macro_poller_period_changed(s.poller, a);
	{
		guint n = compter(&s, a);

		attendre(80);
		g_assert_cmpuint(compter(&s, a), ==, n);
	}
	suivi_fin(&s);
	gt_macro_set_free(set);
}

static void
test_periode_maximale(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint a = macro_en_polling(set, "A", "a", GT_MACRO_PERIOD_MAX_MS);
	Suivi s;

	suivi_init(&s, set);
	g_assert_true(gt_macro_poller_start(s.poller, a));
	attendre(50);
	g_assert_cmpuint(compter(&s, a), ==, 1);
	g_assert_true(gt_macro_poller_is_running(s.poller, a));
	suivi_fin(&s);
	gt_macro_set_free(set);
}

static void
test_arret_depuis_envoi(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint a = macro_en_polling(set, "A", "a", 20);
	Suivi s;

	suivi_init(&s, set);
	s.arreter_au = 2;
	gt_macro_poller_start(s.poller, a);
	attendre(120);
	g_assert_cmpuint(compter(&s, a), ==, 2);
	g_assert_false(gt_macro_poller_is_running(s.poller, a));
	g_assert_cmpuint(s.etats->len, ==, 2);
	suivi_fin(&s);
	gt_macro_set_free(set);
}

static void
test_arret_pendant_envoi_immediat(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint a = macro_en_polling(set, "A", "a", 20);
	Suivi s;

	suivi_init(&s, set);
	s.arreter_au = 1;
	g_assert_false(gt_macro_poller_start(s.poller, a));
	attendre(60);
	g_assert_cmpuint(compter(&s, a), ==, 1);
	g_assert_false(gt_macro_poller_is_running(s.poller, a));
	suivi_fin(&s);
	gt_macro_set_free(set);
}

static void
test_free_sans_rappel(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint a = macro_en_polling(set, "A", "a", 20);
	Suivi s;

	suivi_init(&s, set);
	gt_macro_poller_start(s.poller, a);
	gt_macro_poller_free(s.poller);
	s.poller = NULL;
	g_assert_cmpuint(s.etats->len, ==, 1);
	attendre(60);
	g_assert_cmpuint(compter(&s, a), ==, 1);
	g_array_unref(s.envois);
	g_array_unref(s.etats);
	gt_macro_set_free(set);
	gt_macro_poller_free(NULL);
}

static void
test_nouveau_jeu(void)
{
	GtMacroSet *set = gt_macro_set_new(), *autre = gt_macro_set_new();
	guint a = macro_en_polling(set, "A", "a", 1000);
	Suivi s;

	suivi_init(&s, set);
	gt_macro_poller_start(s.poller, a);
	gt_macro_poller_set_set(s.poller, autre);
	g_assert_cmpuint(gt_macro_poller_running_count(s.poller), ==, 0);
	g_assert_cmpuint(dernier_etat(&s), ==, a * 2);
	g_assert_false(gt_macro_poller_start(s.poller, a));
	suivi_fin(&s);
	gt_macro_set_free(set);
	gt_macro_set_free(autre);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/macro_poller/envoi-immediat-puis-periodique", test_envoi_immediat_puis_periodique);
	g_test_add_func("/macro_poller/demarrage-refuse", test_demarrage_refuse);
	g_test_add_func("/macro_poller/deja-en-cours-et-bascule", test_deja_en_cours_et_bascule);
	g_test_add_func("/macro_poller/reset", test_reset);
	g_test_add_func("/macro_poller/sync-suppression-et-mode", test_sync_suppression_et_mode);
	g_test_add_func("/macro_poller/sync-action-videe", test_sync_action_videe);
	g_test_add_func("/macro_poller/changement-de-periode", test_changement_de_periode);
	g_test_add_func("/macro_poller/periode-maximale", test_periode_maximale);
	g_test_add_func("/macro_poller/arret-depuis-envoi", test_arret_depuis_envoi);
	g_test_add_func("/macro_poller/arret-pendant-envoi-immediat", test_arret_pendant_envoi_immediat);
	g_test_add_func("/macro_poller/free-sans-rappel", test_free_sans_rappel);
	g_test_add_func("/macro_poller/nouveau-jeu", test_nouveau_jeu);
	return g_test_run();
}
```

Ajouter `'test_macro_poller' : [],` à `unit_tests` dans `tests/meson.build`.

- [ ] **Step 2 : vérifier l'échec**

Run : `ninja -C build 2>&1 | tail -3`
Expected : échec de compilation (`macro_poller.h` introuvable).

- [ ] **Step 3 : écrire `src/macro_poller.h`**

```c
/* macro_poller.h — ordonnanceur de polling des macros : une minuterie GLib (contexte par défaut) par macro en cours.
 * Les macros sont envoyées par la fonction fournie (dans l'application : macros_send, arguments enregistrés, vers la liaison
 * de l'application). Utilisé uniquement sur le fil principal. */

#ifndef MACRO_POLLER_H_
#define MACRO_POLLER_H_

#include <glib.h>

#include "macro_set.h"

typedef struct _GtMacroPoller GtMacroPoller;

typedef gboolean (*GtMacroPollerSendFunc)  (guint id, gpointer user_data);
typedef void     (*GtMacroPollerStateFunc) (guint id, gboolean running, gpointer user_data);

GtMacroPoller *gt_macro_poller_new           (GtMacroSet *set, GtMacroPollerSendFunc send, GtMacroPollerStateFunc state_changed, gpointer user_data);
void           gt_macro_poller_free          (GtMacroPoller *poller);	/* arrête tout, sans rappel d'état */
void           gt_macro_poller_set_set       (GtMacroPoller *poller, GtMacroSet *set);	/* arrête tout (rappels émis) puis change de jeu */
gboolean       gt_macro_poller_start         (GtMacroPoller *poller, guint id);	/* envoi immédiat puis à chaque période */
void           gt_macro_poller_stop          (GtMacroPoller *poller, guint id);
gboolean       gt_macro_poller_toggle        (GtMacroPoller *poller, guint id);	/* renvoie le nouvel état */
gboolean       gt_macro_poller_is_running    (const GtMacroPoller *poller, guint id);
guint          gt_macro_poller_running_count (const GtMacroPoller *poller);
void           gt_macro_poller_reset         (GtMacroPoller *poller);
void           gt_macro_poller_sync          (GtMacroPoller *poller);	/* après une modification du jeu */
void           gt_macro_poller_period_changed(GtMacroPoller *poller, guint id);

#endif
```

- [ ] **Step 4 : écrire `src/macro_poller.c`**

```c
/* macro_poller.c — ordonnanceur de polling (voir macro_poller.h et le §7 de la spec du sous-projet 4). */

#include "macro_poller.h"

typedef struct
{
	GtMacroPoller *poller;
	guint          id;
	guint          period_ms;
	guint          source;
} Running;

struct _GtMacroPoller
{
	GtMacroSet            *set;
	GtMacroPollerSendFunc  send;
	GtMacroPollerStateFunc state_changed;
	gpointer               user_data;
	GHashTable            *running;	/* id → Running * */
};

static void
running_free(gpointer data)
{
	Running *r = data;

	/* Retirer une source pendant sa propre exécution est permis : GLib l'ignore au retour */
	if(r->source != 0)
		g_source_remove(r->source);
	g_free(r);
}

static gboolean
on_tick(gpointer data)
{
	Running *r = data;
	GtMacroPoller *poller = r->poller;

	/* r peut être libéré par l'envoi (macro arrêtée depuis la fonction d'envoi) : ne plus y toucher après */
	poller->send(r->id, poller->user_data);
	return G_SOURCE_CONTINUE;
}

static void
notify(GtMacroPoller *poller, guint id, gboolean running)
{
	if(poller->state_changed != NULL)
		poller->state_changed(id, running, poller->user_data);
}

static void
restart_timer(Running *r, guint period_ms)
{
	if(r->source != 0)
		g_source_remove(r->source);
	r->period_ms = period_ms;
	r->source = g_timeout_add(period_ms, on_tick, r);
}

GtMacroPoller *
gt_macro_poller_new(GtMacroSet *set, GtMacroPollerSendFunc send, GtMacroPollerStateFunc state_changed, gpointer user_data)
{
	GtMacroPoller *poller = g_new0(GtMacroPoller, 1);

	poller->set = set;
	poller->send = send;
	poller->state_changed = state_changed;
	poller->user_data = user_data;
	poller->running = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, running_free);
	return poller;
}

void
gt_macro_poller_free(GtMacroPoller *poller)
{
	if(poller == NULL)
		return;
	poller->state_changed = NULL;
	g_hash_table_destroy(poller->running);
	g_free(poller);
}

void
gt_macro_poller_set_set(GtMacroPoller *poller, GtMacroSet *set)
{
	gt_macro_poller_reset(poller);
	poller->set = set;
}

gboolean
gt_macro_poller_start(GtMacroPoller *poller, guint id)
{
	const GtMacro *m = gt_macro_set_find(poller->set, id);
	Running *r;

	if(m == NULL || !m->polling || !gt_macro_is_sendable(m))
		return FALSE;
	if(gt_macro_poller_is_running(poller, id))
		return TRUE;
	r = g_new0(Running, 1);
	r->poller = poller;
	r->id = id;
	restart_timer(r, m->polling_period_ms);
	g_hash_table_insert(poller->running, GUINT_TO_POINTER(id), r);
	notify(poller, id, TRUE);
	/* Envoi immédiat ; il peut arrêter la macro (rappel d'envoi ré-entrant) */
	poller->send(id, poller->user_data);
	return gt_macro_poller_is_running(poller, id);
}

void
gt_macro_poller_stop(GtMacroPoller *poller, guint id)
{
	if(g_hash_table_remove(poller->running, GUINT_TO_POINTER(id)))
		notify(poller, id, FALSE);
}

gboolean
gt_macro_poller_toggle(GtMacroPoller *poller, guint id)
{
	if(gt_macro_poller_is_running(poller, id))
	{
		gt_macro_poller_stop(poller, id);
		return FALSE;
	}
	return gt_macro_poller_start(poller, id);
}

gboolean
gt_macro_poller_is_running(const GtMacroPoller *poller, guint id)
{
	return poller != NULL && g_hash_table_contains(poller->running, GUINT_TO_POINTER(id));
}

guint
gt_macro_poller_running_count(const GtMacroPoller *poller)
{
	return poller != NULL ? g_hash_table_size(poller->running) : 0;
}

void
gt_macro_poller_reset(GtMacroPoller *poller)
{
	/* Copie des clés : les rappels d'état peuvent toucher à l'ordonnanceur */
	GList *ids = g_hash_table_get_keys(poller->running);

	for(GList *l = ids; l != NULL; l = l->next)
		gt_macro_poller_stop(poller, GPOINTER_TO_UINT(l->data));
	g_list_free(ids);
}

void
gt_macro_poller_sync(GtMacroPoller *poller)
{
	GList *ids = g_hash_table_get_keys(poller->running);

	for(GList *l = ids; l != NULL; l = l->next)
	{
		guint id = GPOINTER_TO_UINT(l->data);
		const GtMacro *m = gt_macro_set_find(poller->set, id);

		if(m == NULL || !m->polling || !gt_macro_is_sendable(m))
			gt_macro_poller_stop(poller, id);
		else
			gt_macro_poller_period_changed(poller, id);
	}
	g_list_free(ids);
}

void
gt_macro_poller_period_changed(GtMacroPoller *poller, guint id)
{
	Running *r = g_hash_table_lookup(poller->running, GUINT_TO_POINTER(id));
	const GtMacro *m = gt_macro_set_find(poller->set, id);

	if(r != NULL && m != NULL && m->polling_period_ms != r->period_ms)
		restart_timer(r, m->polling_period_ms);
}
```

Ajouter `'macro_poller.c',` et `'macro_poller.h',` à `core_sources` (après `'macro_file.h',`).

- [ ] **Step 5 : construire et tester (normal et ASan)**

Run : `ninja -C build 2>&1 | tail -3 && meson test -C build test_macro_poller --print-errorlogs 2>&1 | tail -5`
Expected : `test_macro_poller` OK (12 cas).

Run : `test -d build-asan || meson setup build-asan -Db_sanitize=address,undefined >/dev/null; ninja -C build-asan >/dev/null && meson test -C build-asan test_macro_poller test_macro_set test_macro_file test_macros_list test_macros_format --print-errorlogs 2>&1 | tail -8`
Expected : tous OK, aucun rapport ASan/LSan.

- [ ] **Step 6 : commit**

```bash
git add src/macro_poller.c src/macro_poller.h src/meson.build tests/test_macro_poller.c tests/meson.build
git commit -m "macros : ordonnanceur de polling GtMacroPoller, une minuterie par macro en cours (M22)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 5 : l'interface passe au modèle (état applicatif, envoi unique, raccourcis, fenêtre d'édition, panneau, Lua)

**Files :**
- Modify : `src/macros.h`, `src/macros.c`, `src/macro_panel.h`, `src/macro_panel.c`, `src/script_engine.c`, `src/term_config.c`, `src/gtkterm.c`
- Modify (tests) : `tests/test_macros_send.c` (réécrit), `tests/test_script_engine.c`, `tests/stubs/stub_macros.c`, `tests/stubs/stub_macros.h`, `tests/stubs/stub_ui.c`

**Interfaces :**
- Consumes : `GtMacroSet` (tâche 2), `gt_macro_file_load/save` (tâche 3), `GtMacroPoller` (tâche 4), `gt_link_send` (`gssize gt_link_send(GtkTermLink *, const guint8 *, gsize, GtSendOrigin)`), `gt_link_get_default()`, `Put_temp_message`, `show_message(gchar *, gint)` + `MSG_ERR` (`interface.h`), `config_remember_macros_file()` (`term_config.h`), `GtkAccelGroup *shortcuts` (`interface.h`).
- Produces (`macros.h`) :

```c
void           macros_init             (void);
GtMacroSet    *macros_get_set          (void);
GtMacroPoller *macros_get_poller       (void);
void           macros_replace_set      (GtMacroSet *set);
gboolean       macros_send             (guint id, const gchar *const *args, gint n_args);
gboolean       macros_load_file        (const gchar *path, gboolean interactive);
gboolean       macros_save_file        (const gchar *path, gboolean interactive);
const gchar   *macros_file_get_path    (void);
gchar         *macros_file_default_path(void);
void           macros_changed          (void);
void           Config_macros           (GtkAction *action, gpointer data);
```

- Produces (`macro_panel.h`) : `void macro_panel_polling_changed(guint id, gboolean running);` (rappel d'état de l'ordonnanceur ; tolère un identifiant inconnu ou sans bouton).
- Retire : tout ce que liste la contrainte globale « plus aucune référence » (dont `macros_app_lists`/`macros_app_lists_replace` de la tâche 1).

- [ ] **Step 1 : réécrire le test d'intégration de `macros.c` (échoue à la compilation)**

Remplacer tout `tests/test_macros_send.c` par :

```c
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
	desinstaller();
	gtk_widget_destroy(fenetre);
	g_clear_object(&shortcuts);
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
	g_test_add_func("/macros_send/raccourci-connecte-une-seule-fois", test_raccourci_connecte_une_seule_fois);
	g_test_add_func("/macros_send/raccourci-declare-traite", test_raccourci_declare_traite);
	g_test_add_func("/macros_send/chargement-ancien-format-listes", test_chargement_ancien_format_listes);
	g_test_add_func("/macros_send/chargement-fichier-absent", test_chargement_fichier_absent);
	g_test_add_func("/macros_send/chargement-corrompu-garde-le-jeu", test_chargement_corrompu_garde_le_jeu);
	g_test_add_func("/macros_send/enregistrement-echec-une-seule-boite", test_enregistrement_echec_une_seule_boite);
	g_test_add_func("/macros_send/enregistrement-puis-relecture", test_enregistrement_puis_relecture);
	g_test_add_func("/macros_send/polling-arrete-au-chargement", test_polling_arrete_au_chargement);
	return g_test_run();
}
```

Dans `tests/stubs/stub_ui.c`, ajouter (avec `#include "macro_panel.h"` déjà présent) :

```c
void
macro_panel_polling_changed(guint id, gboolean running)
{
	stub_record("macro_panel_polling_changed", running ? "1" : "0");
}

void
config_remember_macros_file(void)
{
	stub_record("config_remember_macros_file", NULL);
}
```

(si `config_remember_macros_file` est déjà défini ailleurs dans les bouchons liés à `test_macros_send`, ne pas le dupliquer.)

- [ ] **Step 2 : adapter le test du moteur Lua et son bouchon**

`tests/stubs/stub_macros.h` devient :

```c
/* Bouchon de l'état des macros pour test_script_engine : jeu fourni par le test, envoi par la liaison bouchonnée. */
#ifndef STUB_MACROS_H_
#define STUB_MACROS_H_

#include "macros.h"

void     stub_set_macro_set(GtMacroSet *set);	/* prend le jeu en charge */
GThread *stub_macros_thread(void);		/* fil du dernier appel à macros_get_set */

#endif
```

`tests/stubs/stub_macros.c` devient :

```c
/* Bouchon de l'état des macros (macros.c) pour test_script_engine : jeu fourni par le test, fil appelant mémorisé. */

#include "link.h"
#include "stub_macros.h"

static GtMacroSet *jeu = NULL;
static GThread *thread_acces = NULL;

void
stub_set_macro_set(GtMacroSet *set)
{
	gt_macro_set_free(jeu);
	jeu = set;
}

GThread *
stub_macros_thread(void)
{
	return thread_acces;
}

GtMacroSet *
macros_get_set(void)
{
	thread_acces = g_thread_self();
	if(jeu == NULL)
		jeu = gt_macro_set_new();
	return jeu;
}

/* Même contrat que macros.c : macro inconnue ou non envoyable → FALSE ; n_args < 0 → arguments enregistrés */
gboolean
macros_send(guint id, const gchar *const *args, gint n_args)
{
	const GtMacro *m = gt_macro_set_find(macros_get_set(), id);
	GBytes *bytes;
	const guint8 *data;
	gsize len = 0;
	gssize written = 0;

	if(!gt_macro_is_sendable(m))
		return FALSE;
	bytes = gt_macro_expand(jeu, m, args, n_args);
	data = g_bytes_get_data(bytes, &len);
	if(len > 0)
		written = gt_link_send(gt_link_get_default(), data, len, GT_SEND_MACRO);
	g_bytes_unref(bytes);
	return len == 0 || (written >= 0 && (gsize)written == len);
}
```

Dans `tests/test_script_engine.c` :
- supprimer `static macro_t macros_test[1];` ; `installer_macro_a` devient :

```c
static void
installer_macro_a(void)
{
	GtMacroSet *set = gt_macro_set_new();
	guint v;

	gt_macro_set_append(set, "A", "", "A", "");
	v = gt_macro_set_append(set, "V", "", "V%d", "");
	gt_macro_set_set_arg(set, v, 0, "42");
	stub_set_macro_set(set);
}
```

- `stub_get_shortcuts_thread()` → `stub_macros_thread()` ;
- `test_get_lists` :

```c
static void
test_get_lists(void)
{
	GtMacroSet *set = gt_macro_set_new();
	GtMacroLists *l = gt_macro_lists_new();

	gt_macro_lists_add(l, "Mode", "Marche", "M");
	gt_macro_set_take_lists(set, l);
	stub_set_macro_set(set);
	g_assert_true(executer("local l = gtkterm.get_lists()\n"
	                       "gtkterm.log(l[1].name .. ' ' .. l[1].entries[1].value)\n", 5000));
	g_assert_nonnull(strstr(journal->str, "Mode M"));
	g_assert_true(stub_macros_thread() == thread_principal);
}
```

- ajouter, et enregistrer sous `/script_engine/send-macro-arguments` :

```c
/* Sans argument Lua, les arguments enregistrés ; avec, ceux fournis ; par indice (0-based) comme par libellé. */
static void
test_send_macro_arguments(void)
{
	GByteArray *octets;

	stub_reset();
	installer_macro_a();
	g_assert_true(executer("assert(gtkterm.send_macro('V'))\n"
	                       "assert(gtkterm.send_macro('V', 7))\n"
	                       "assert(gtkterm.send_macro(1))\n", 5000));
	octets = stub_sent();
	g_assert_cmpmem(octets->data, octets->len, "V42V7V42", 8);
	g_assert_true(stub_send_thread() == thread_principal);
}
```

- [ ] **Step 3 : vérifier l'échec**

Run : `ninja -C build 2>&1 | grep -E 'error' | head -5`
Expected : erreurs de compilation (`macros_replace_set`, `macros_send`… non déclarées).

- [ ] **Step 4 : `macros.h`**

Remplacer le contenu (après l'en-tête de commentaire) par :

```c
#ifndef MACROS_H_
#define MACROS_H_

#include <gtk/gtk.h>

#include "macro_set.h"
#include "macro_poller.h"

/* État des macros de l'application (un seul jeu aujourd'hui, un par projet demain) ; fil principal seulement. */
void           macros_init             (void);	/* jeu vide, ordonnanceur sur la liaison par défaut ; avant create_main_window() */
GtMacroSet    *macros_get_set          (void);
GtMacroPoller *macros_get_poller       (void);
void           macros_replace_set      (GtMacroSet *set);	/* prend le jeu en charge, arrête tout polling, raccourcis + panneau */
/* Seul envoi de macro de l'interface (clic, raccourci, polling, Lua) ; n_args < 0 : arguments enregistrés.
 * Macro inconnue ou non envoyable : FALSE, rien n'est envoyé. TRUE si tous les octets ont été écrits. */
gboolean       macros_send             (guint id, const gchar *const *args, gint n_args);
gboolean       macros_load_file        (const gchar *path, gboolean interactive);	/* NULL : chemin courant (ou défaut) */
gboolean       macros_save_file        (const gchar *path, gboolean interactive);	/* NULL : chemin courant (ou défaut) */
const gchar   *macros_file_get_path    (void);
gchar         *macros_file_default_path(void);
void           macros_changed          (void);	/* raccourcis reconnectés + panneau reconstruit */
void           Config_macros           (GtkAction *action, gpointer data);

#endif
```

- [ ] **Step 5 : état applicatif, envoi, raccourcis et fichier dans `macros.c`**

Supprimer de `macros.c` : `macro_t *macros`, `get_shortcuts`, `macro_display_name` (ancienne), `send_macro_with_args`, `send_macro_with_arg`, `shortcut_callback`, `create_shortcuts`, `add_shortcuts`, `macros_destroy`, `remove_shortcuts`, `macro_set_arg`, `macro_set_polling`, toute la section « Fichier macros séparé » (`macros_file_path`, `macros_file_get_default_path`, `macros_file_set_path`, `macros_file_get_path`, `get_macros_file_path`, `write_macros_to_file`, `macros_file_save`, `macros_file_load`), `app_lists`/`macros_app_lists`/`macros_app_lists_replace`. Ajouter `#include "macro_file.h"` et `#include "macro_panel.h"`, puis (style GNU du fichier) :

```c
static GtMacroSet *current_set = NULL;
static GtMacroPoller *poller = NULL;
static gchar *current_path = NULL;
static GPtrArray *closures = NULL;		/* GClosure * connectées à connected_group */
static GtkAccelGroup *connected_group = NULL;
static gchar *last_save_error = NULL;		/* dernier échec d'enregistrement signalé : une seule boîte par message */

static gboolean
poller_send (guint id, gpointer user_data)
{
  return macros_send (id, NULL, -1);
}

static void
poller_state (guint id, gboolean running, gpointer user_data)
{
  macro_panel_polling_changed (id, running);
}

void
macros_init (void)
{
  if (current_set != NULL)
    return;
  current_set = gt_macro_set_new ();
  poller = gt_macro_poller_new (current_set, poller_send, poller_state, NULL);
}

GtMacroSet *
macros_get_set (void)
{
  return current_set;
}

GtMacroPoller *
macros_get_poller (void)
{
  return poller;
}

gboolean
macros_send (guint id, const gchar *const *args, gint n_args)
{
  const GtMacro *m = gt_macro_set_find (current_set, id);
  GBytes *bytes;
  const guint8 *data;
  gsize len = 0;
  gssize written = 0;
  gchar *message;

  if (!gt_macro_is_sendable (m))
    return FALSE;
  /* Nom affiché : le libellé, à défaut le raccourci (C8) ; pris avant l'envoi, dont les gestionnaires ne doivent pas toucher au jeu */
  message = g_strdup_printf (_ ("Macro \"%s\" sent!"), m->label[0] != '\0' ? m->label : m->shortcut);
  bytes = gt_macro_expand (current_set, m, args, n_args);
  data = g_bytes_get_data (bytes, &len);
  if (len > 0)
    written = gt_link_send (gt_link_get_default (), data, len, GT_SEND_MACRO);
  g_bytes_unref (bytes);
  Put_temp_message (message, 800);
  g_free (message);
  return len == 0 || (written >= 0 && (gsize) written == len);
}

/* Rappel d'un raccourci (g_cclosure_new_swap : la donnée arrive en premier). TRUE : la touche est traitée et n'atteint pas
   le terminal (M43) ; macro disparue ou non envoyable : FALSE, la touche suit son cours. */
static gboolean
shortcut_cb (gpointer data)
{
  return macros_send (GPOINTER_TO_UINT (data), NULL, -1);
}

static void
shortcuts_disconnect (void)
{
  if (closures == NULL || connected_group == NULL)
    return;
  for (guint i = 0; i < closures->len; i++)
    gtk_accel_group_disconnect (connected_group, g_ptr_array_index (closures, i));
  g_ptr_array_set_size (closures, 0);
  connected_group = NULL;
}

/* Une seule connexion par macro (M17), par identifiant : un raccourci suit sa macro quand l'ordre change */
static void
shortcuts_connect (void)
{
  shortcuts_disconnect ();
  if (shortcuts == NULL || current_set == NULL)
    return;
  if (closures == NULL)
    closures = g_ptr_array_new ();
  connected_group = shortcuts;
  for (guint i = 0; i < gt_macro_set_count (current_set); i++)
    {
      const GtMacro *m = gt_macro_set_get (current_set, i);
      guint key = 0;
      GdkModifierType mods = 0;
      GClosure *closure;

      if (m->shortcut[0] == '\0')
        continue;
      gtk_accelerator_parse (m->shortcut, &key, &mods);
      if (key == 0)
        continue;
      closure = g_cclosure_new_swap (G_CALLBACK (shortcut_cb), GUINT_TO_POINTER (m->id), NULL);
      gtk_accel_group_connect (connected_group, key, mods, GTK_ACCEL_MASK, closure);
      g_ptr_array_add (closures, closure);
    }
}

void
macros_changed (void)
{
  shortcuts_connect ();
  rebuild_macro_buttons ();
}

void
macros_replace_set (GtMacroSet *set)
{
  GtMacroSet *old;

  macros_init ();
  /* L'ordonnanceur s'arrête pendant que l'ancien jeu est encore le jeu courant : les rappels d'état voient des boutons cohérents */
  gt_macro_poller_set_set (poller, set);
  old = current_set;
  current_set = set;
  gt_macro_set_free (old);
  macros_changed ();
}

const gchar *
macros_file_get_path (void)
{
  return current_path;
}

gchar *
macros_file_default_path (void)
{
  return g_build_filename (g_get_user_config_dir (), "gtkterm_macros.ini", NULL);
}

static gchar *
resolve_path (const gchar *path)
{
  if (path != NULL)
    return g_strdup (path);
  if (current_path != NULL)
    return g_strdup (current_path);
  return macros_file_default_path ();
}

static void
report_error (gboolean interactive, const gchar *text)
{
  if (interactive)
    show_message ((gchar *) text, MSG_ERR);
  else
    g_warning ("%s", text);
}

gboolean
macros_load_file (const gchar *path, gboolean interactive)
{
  gchar *load_path = resolve_path (path);
  GtMacroSet *set = NULL;
  GError *error = NULL;

  if (!gt_macro_file_load (load_path, &set, &error))
    {
      if (!g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        {
          gchar *text = g_strdup_printf (_ ("Cannot load macros file: %s"), error->message);

          /* Jeu et chemin courants inchangés : rien n'est perdu */
          report_error (interactive, text);
          g_free (text);
          g_error_free (error);
          g_free (load_path);
          return FALSE;
        }
      /* Fichier absent (premier lancement, profil qui pointe un fichier pas encore créé) : jeu vide, créé au premier enregistrement */
      g_clear_error (&error);
      set = gt_macro_set_new ();
    }
  g_free (current_path);
  current_path = load_path;
  g_clear_pointer (&last_save_error, g_free);
  macros_replace_set (set);
  return TRUE;
}

gboolean
macros_save_file (const gchar *path, gboolean interactive)
{
  gchar *save_path = resolve_path (path);
  GError *error = NULL;

  if (!gt_macro_file_save (current_set, save_path, &error))
    {
      gchar *text = g_strdup_printf (_ ("Cannot save macros file: %s"), error->message);

      /* Chaque frappe dans un argument enregistre : un même échec n'ouvre qu'une boîte */
      if (g_strcmp0 (text, last_save_error) != 0)
        report_error (interactive, text);
      g_free (last_save_error);
      last_save_error = text;
      g_error_free (error);
      g_free (save_path);
      return FALSE;
    }
  g_clear_pointer (&last_save_error, g_free);
  g_free (current_path);
  current_path = save_path;
  return TRUE;
}
```

- [ ] **Step 6 : fenêtre d'édition sur identifiants (M16, M44, fuites, duplications)**

Dans `macros.c` :

1. Colonnes : ajouter `COLUMN_ID` à la fin de l'énumération (avant `NUM_COLUMNS`) ; `create_model` crée `gtk_list_store_new (NUM_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT)` et remplit depuis le jeu :

```c
static GtkTreeModel *
create_model (void)
{
  GtkListStore *store = gtk_list_store_new (NUM_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT);
  GtkTreeIter iter;

  for (guint i = 0; i < gt_macro_set_count (current_set); i++)
    {
      const GtMacro *m = gt_macro_set_get (current_set, i);

      gtk_list_store_append (store, &iter);
      gtk_list_store_set (store, &iter,
                          COLUMN_LABEL, m->label,
                          COLUMN_SHORTCUT, m->shortcut[0] != '\0' ? m->shortcut : _ ("None"),
                          COLUMN_ACTION, m->action,
                          COLUMN_TAB, m->tab,
                          COLUMN_ID, m->id,
                          -1);
    }
  return GTK_TREE_MODEL (store);
}
```

2. Un seul gestionnaire d'édition pour les colonnes texte des deux onglets (remplace `label_edited`, `action_edited`, `tab_edited`, `list_name_edited`, `list_display_edited`, `list_value_edited`) ; la colonne est portée par le rendu :

```c
static void
cell_edited (GtkCellRendererText *cell, const gchar *path_string, const gchar *new_text, gpointer data)
{
  GtkTreeModel *model = data;
  gint column = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (cell), "column"));
  GtkTreePath *path = gtk_tree_path_new_from_string (path_string);
  GtkTreeIter iter;

  if (gtk_tree_model_get_iter (model, &iter, path))
    gtk_list_store_set (GTK_LIST_STORE (model), &iter, column, new_text, -1);
  gtk_tree_path_free (path);
}

/* Colonne texte ; editable : rendu relié à cell_edited sur le modèle de la vue */
static void
append_text_column (GtkTreeView *treeview, const gchar *title, gint column, gboolean editable, gboolean sortable, gint min_width)
{
  GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();
  GtkTreeViewColumn *col;

  if (editable)
    {
      g_object_set_data (G_OBJECT (renderer), "column", GINT_TO_POINTER (column));
      g_object_set (G_OBJECT (renderer), "editable", TRUE, NULL);
      g_signal_connect (renderer, "edited", G_CALLBACK (cell_edited), gtk_tree_view_get_model (treeview));
      g_signal_connect (renderer, "editing-started", G_CALLBACK (cell_editing_started), NULL);
    }
  col = gtk_tree_view_column_new_with_attributes (title, renderer, "text", column, NULL);
  if (sortable)
    gtk_tree_view_column_set_sort_column_id (col, column);
  gtk_tree_view_column_set_resizable (col, TRUE);
  gtk_tree_view_column_set_min_width (col, min_width);
  gtk_tree_view_append_column (treeview, col);
}
```

   `add_columns` devient quatre appels : `(_ ("Label"), COLUMN_LABEL, TRUE, TRUE, 80)`, `(_ ("Shortcut"), COLUMN_SHORTCUT, FALSE, TRUE, 80)`, `(_ ("Action"), COLUMN_ACTION, TRUE, TRUE, 120)`, `(_ ("Tab"), COLUMN_TAB, TRUE, TRUE, 60)` ; `add_list_columns` : `(_ ("List"), LIST_COLUMN_NAME, TRUE, FALSE, 80)`, `(_ ("Display"), LIST_COLUMN_DISPLAY, TRUE, FALSE, 80)`, `(_ ("Value"), LIST_COLUMN_VALUE, TRUE, FALSE, 120)`.

3. `Add_shortcut` : la ligne nouvelle reçoit `COLUMN_SHORTCUT, _ ("None")`, `COLUMN_TAB, ""` (onglet par défaut, nommé par le panneau) et `COLUMN_ID, 0u`.

4. M44 — première ligne sélectionnée en sélection multiple :

```c
/* La liste est en sélection multiple : gtk_tree_selection_get_selected n'y fonctionne pas (M44) */
static gboolean
first_selected (GtkTreeView *treeview, GtkTreeIter *iter)
{
  GtkTreeModel *model = NULL;
  GList *rows = gtk_tree_selection_get_selected_rows (gtk_tree_view_get_selection (treeview), &model);
  gboolean found = rows != NULL && gtk_tree_model_get_iter (model, iter, rows->data);

  g_list_free_full (rows, (GDestroyNotify) gtk_tree_path_free);
  return found;
}
```

   Dans `Delete_shortcut` et `key_pressed`, remplacer `gtk_tree_selection_get_selected (selection, NULL, &iter)` par `first_selected (treeview, &iter)` ; `Delete_shortcut` met `_ ("None")` (au lieu de `"None"`).

5. `Save_shortcuts` est remplacé par :

```c
static GtMacroLists *
lists_from_model (GtkTreeModel *model)
{
  GtMacroLists *lists = gt_macro_lists_new ();
  GtkTreeIter iter;
  gboolean valid;

  for (valid = gtk_tree_model_get_iter_first (model, &iter); valid; valid = gtk_tree_model_iter_next (model, &iter))
    {
      gchar *name = NULL, *display = NULL, *value = NULL;

      gtk_tree_model_get (model, &iter, LIST_COLUMN_NAME, &name, LIST_COLUMN_DISPLAY, &display, LIST_COLUMN_VALUE, &value, -1);
      gt_macro_lists_add (lists, name, display, value);
      g_free (name);
      g_free (display);
      g_free (value);
    }
  return lists;
}

/* Validation de la fenêtre : les lignes, dans l'ordre affiché, avec leur identifiant ; arguments et polling suivent
   l'identifiant, jamais la position (M16). */
static gboolean
Save_shortcuts (GtkWidget *button, gpointer pointer)
{
  GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (pointer));
  GArray *rows = g_array_new (FALSE, TRUE, sizeof (GtMacroEdit));
  GPtrArray *strings = g_ptr_array_new_with_free_func (g_free);
  GtkTreeIter iter;
  gboolean valid;

  for (valid = gtk_tree_model_get_iter_first (model, &iter); valid; valid = gtk_tree_model_iter_next (model, &iter))
    {
      GtMacroEdit row = { 0 };
      gchar *label = NULL, *shortcut = NULL, *action = NULL, *tab = NULL;

      gtk_tree_model_get (model, &iter, COLUMN_ID, &row.id, COLUMN_LABEL, &label, COLUMN_SHORTCUT, &shortcut,
                          COLUMN_ACTION, &action, COLUMN_TAB, &tab, -1);
      /* « None » affiché (traduit) = pas de raccourci ; « None » non traduit est normalisé par le modèle */
      if (g_strcmp0 (shortcut, _ ("None")) == 0)
        {
          g_free (shortcut);
          shortcut = g_strdup ("");
        }
      row.label = label;
      row.shortcut = shortcut;
      row.action = action;
      row.tab = tab;
      g_ptr_array_add (strings, label);
      g_ptr_array_add (strings, shortcut);
      g_ptr_array_add (strings, action);
      g_ptr_array_add (strings, tab);
      g_array_append_val (rows, row);
    }
  gt_macro_set_apply_edits (current_set, (const GtMacroEdit *) rows->data, rows->len, lists_model != NULL ? lists_from_model (lists_model) : NULL);
  gt_macro_poller_sync (poller);
  macros_changed ();
  macros_save_file (NULL, TRUE);
  config_remember_macros_file ();
  g_ptr_array_unref (strings);
  g_array_unref (rows);
  return FALSE;
}
```

   Supprimer `save_lists_from_model` et `sort_indices_asc` s'il ne sert plus qu'à lui (il sert aussi à `Delete_macro`/`Delete_list_entry` : le garder dans ce cas).

6. `create_lists_model` lit `gt_macro_set_get_lists (current_set)` avec les fonctions `gt_macro_lists_*`.

7. `Config_macros` : supprimer `g_print ("Config_macros called\n")` ; si la fenêtre est déjà ouverte, `gtk_window_present (GTK_WINDOW (window)); return;` en tête ; à la destruction de la fenêtre, libérer le modèle des listes (fuite à chaque ouverture) — dans `build_lists_page`, le modèle est confié à la vue (`g_object_unref (lists_model)` juste après `gtk_tree_view_new_with_model`, la vue garde sa référence) et un gestionnaire `destroy` de la fenêtre remet `lists_model` à `NULL` :

```c
static void
on_window_destroy (GtkWidget *widget, gpointer data)
{
  window = NULL;
  lists_model = NULL;		/* libéré avec sa vue */
}
```

   (remplace la connexion `gtk_widget_destroyed` sur `window`).

- [ ] **Step 7 : panneau, vue du modèle (`macro_panel.c`, `macro_panel.h`)**

Dans `macro_panel.h`, ajouter : `void macro_panel_polling_changed(guint id, gboolean running);`.

Dans `macro_panel.c` (style du fichier : tabulations, `if (`) :

1. Supprimer : `macro_polling_t`, `macro_polling_table`, `free_polling_args`, `get_polling_state`, `set_polling_state`, `toggle_polling_run`, `restore_macro_polling`, `polling_timer_callback` (et son `g_timeout_add(1, …)`), `send_macro_by_index`, `macro_is_separator` (remplacé par `gt_macro_action_is_separator`), `macro_stack` ; la boucle « Restore polling state from macro_t » en fin de `rebuild_macro_buttons`.

2. `macro_button_table` : clé = identifiant de macro (`GUINT_TO_POINTER(m->id)`), valeur = bouton principal (non possédé), vidée au début de chaque `rebuild_macro_buttons()` ; **les boutons de liste à deux valeurs n'y sont pas inscrits** (pas de polling pour eux). Chaque bouton et chaque champ d'argument porte `g_object_set_data(…, "macro-id", GUINT_TO_POINTER(m->id))` (au lieu de `"macro-index"`). `MacroArgData` : `guint macro_id; GtkWidget **entries; gint n_entries;`. `rebuild_macro_buttons` parcourt `macros_get_set()` (`gt_macro_set_count`/`gt_macro_set_get`) ; libellés, listes et arguments viennent du `GtMacro` et de `gt_macro_set_get_lists(set)` ; `macro_count_format_args(gt_macro_set_get_lists(set), m->action)` et `macro_get_arg_infos(gt_macro_set_get_lists(set), m->action, NULL)`.

3. Nouvelles fonctions (remplacent les anciennes du même rôle) :

```c
static guint widget_macro_id(GtkWidget *widget)
{
	return GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget), "macro-id"));
}

static void save_macros(void)
{
	macros_save_file(NULL, TRUE);
	config_remember_macros_file();
}

/* Valeur d'un champ d'argument écrite dans le modèle (le fichier n'est pas enregistré ici) */
static void commit_arg_widget(GtkWidget *widget)
{
	gint arg_index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "arg-index"));
	gchar *val = get_arg_value_from_widget(widget);

	gt_macro_set_set_arg(macros_get_set(), widget_macro_id(widget), arg_index, val);
	g_free(val);
}

static void save_arg_from_widget(GtkWidget *widget)
{
	commit_arg_widget(widget);
	save_macros();
}

/* Libellé « ⏱ Libellé » en mode polling ; plus de clignotement quand la macro ne tourne pas */
static void update_button_appearance(guint id)
{
	GtkWidget *button = g_hash_table_lookup(macro_button_table, GUINT_TO_POINTER(id));
	const GtMacro *m = gt_macro_set_find(macros_get_set(), id);

	if (button == NULL || m == NULL)
		return;
	if (!gt_macro_poller_is_running(macros_get_poller(), id))
	{
		gtk_style_context_remove_class(gtk_widget_get_style_context(button), "polling-blink");
		gtk_widget_queue_draw(button);
	}
	if (m->polling)
	{
		gchar *label = g_strdup_printf("⏱ %s", m->label);

		gtk_button_set_label(GTK_BUTTON(button), label);
		g_free(label);
	}
	else
		gtk_button_set_label(GTK_BUTTON(button), m->label);
}

void macro_panel_polling_changed(guint id, gboolean running)
{
	if (macro_button_table != NULL)
		update_button_appearance(id);
}

/* Clic sur une macro : bascule son polling si le mode est activé, sinon un envoi (arguments enregistrés) */
static void activate_macro(guint id)
{
	const GtMacro *m = gt_macro_set_find(macros_get_set(), id);

	if (m == NULL)
		return;
	if (m->polling)
		gt_macro_poller_toggle(macros_get_poller(), id);
	else
		macros_send(id, NULL, -1);
}
```

4. Gestionnaires :
   - `on_macro_button_clicked_with_polling` (macro sans argument) : `activate_macro(GPOINTER_TO_UINT(data));` (la donnée connectée devient `GUINT_TO_POINTER(m->id)`).
   - `on_macro_arg_button_clicked` : écrire tous les champs dans le modèle, enregistrer, puis activer — plus de copie d'arguments propre au polling :

```c
static void on_macro_arg_button_clicked(GtkWidget *widget, gpointer data)
{
	MacroArgData *d = (MacroArgData *)g_object_get_data(G_OBJECT(widget), "macro-data");

	if (d == NULL)
		return;
	for (gint k = 0; k < d->n_entries; k++)
		commit_arg_widget(d->entries[k]);
	save_macros();
	activate_macro(d->macro_id);
}
```

   - `on_list_action_button_clicked` : `const gchar *args[] = { list_value }; macros_send(widget_macro_id(widget), args, 1);`.
   - `on_polling_mode_toggled` :

```c
static void on_polling_mode_toggled(GtkCheckMenuItem *check_item, gpointer user_data)
{
	guint id = widget_macro_id(GTK_WIDGET(check_item));
	const GtMacro *m = gt_macro_set_find(macros_get_set(), id);
	gboolean active = gtk_check_menu_item_get_active(check_item);

	if (m == NULL)
		return;
	gt_macro_set_set_polling(macros_get_set(), id, active, m->polling_period_ms);
	if (!active)
		gt_macro_poller_stop(macros_get_poller(), id);
	update_button_appearance(id);
	save_macros();
}
```

   - `on_polling_period_changed` (comportement provisoire, remplacé à la tâche 6) : lire l'entier (`strtoul`), appliquer par `gt_macro_set_set_polling(set, id, m->polling, period)`, `gt_macro_poller_period_changed(macros_get_poller(), id)`, `save_macros()`.
   - `on_macro_button_right_click` : l'identifiant vient de `user_data` (`GUINT_TO_POINTER(m->id)`) ; la case « Polling Mode » est cochée selon `m->polling` ; le champ période affiche `m->polling_period_ms` ; les éléments portent `"macro-id"`.
   - `update_button_label` disparaît dans `update_button_appearance` (fuite du libellé alloué corrigée).
   - `polling_blink_callback` (500 ms, provisoirement permanent) : parcourt `macro_button_table` et fait clignoter les boutons dont la macro tourne (`gt_macro_poller_is_running`).
   - À la création de chaque bouton principal : `update_button_appearance(m->id)` après son inscription dans la table (remplace la restauration du polling).
   - Les gestionnaires des onglets (`on_macro_notebook_button_press`, `rebuild_macro_buttons`) lisent les onglets depuis le jeu (`m->tab`, vide → `_("General")`) ; les macros affichables restent celles à libellé et action non vides.

5. Inclure `macros.h` (déjà) ; plus aucun appel à `get_shortcuts`, `shortcut_callback`, `send_macro_with_args`, `macro_set_arg`, `macro_set_polling`, `macros_file_save`, `macros_app_lists`.

- [ ] **Step 8 : Lua (`script_engine.c`)**

- `MainMacroData` : remplacer `expected`/`written` par `gboolean ok;` ; `main_send_macro_cb` devient :

```c
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
```

- `l_send_macro` : initialisation `MainMacroData d = { -1, NULL, 0, NULL, FALSE, FALSE };` ; en fin, `lua_pushboolean(L, d.ok);`.
- `main_snapshot_macros_cb` : parcourir `macros_get_set()` (`gt_macro_set_count`/`gt_macro_set_get`), `s->infos = macro_get_arg_infos(gt_macro_set_get_lists(set), m->action, &s->n_args);`.
- `main_snapshot_lists_cb` : `const GtMacroLists *lists = gt_macro_set_get_lists(macros_get_set());` puis `gt_macro_lists_*` (au lieu de `macros_app_lists()`).

- [ ] **Step 9 : `term_config.c` et `gtkterm.c`**

- `load_macros_file_callback` : `if (macros_load_file (path, TRUE)) config_remember_macros_file ();` (supprimer `macros_file_load`, `add_shortcuts`, `rebuild_macro_buttons`) ; dossier initial sans fuite :

```c
		if (cur && g_file_test (cur, G_FILE_TEST_EXISTS))
		{
			gchar *dir = g_path_get_dirname (cur);

			gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER(dialog), dir);
			g_free (dir);
		}
```

  (même correction dans `save_macros_file_callback`).
- `save_macros_file_callback` : `if (macros_save_file (path, TRUE)) config_remember_macros_file ();`.
- `Load_configuration_from_file` : remplacer les quatre lignes `macros_file_set_path`/`macros_file_load`/`add_shortcuts`/`rebuild_macro_buttons` par :

```c
	/* Fichier de macros du profil ; sans fichier, celui en cours est relu (non interactif : avertissement seulement) */
	macros_load_file(config.macros_file, FALSE);
```

- callback de chargement de profil (`Select_config` → chargement) : supprimer l'appel `add_shortcuts();`.
- `gtkterm.c` : `#include "macros.h"` ; appeler `macros_init();` juste avant `create_main_window();` ; supprimer `add_shortcuts();` (les raccourcis sont connectés à chaque chargement de fichier de macros).

- [ ] **Step 10 : construire, tester, vérifier les références**

Run : `ninja -C build 2>&1 | grep -E 'warning|error' | head -10`
Expected : aucune sortie.

Run : `meson test -C build --print-errorlogs 2>&1 | tail -6`
Expected : tout OK (dont `test_macros_send` 13 cas, `test_script_engine` avec `/script_engine/send-macro-arguments`).

Run : `grep -rn -E '\bmacro_t\b|get_shortcuts|create_shortcuts|add_shortcuts|remove_shortcuts|shortcut_callback|send_macro_with_arg|macro_set_arg\b|macro_set_polling|macro_polling_t|macros_app_lists|macros_file_load|macros_file_save|macros_file_set_path|macros_file_get_default_path' src tests`
Expected : aucune sortie (hors commentaires d'en-tête « ChangeLog » de `gtkterm.c`, à reformuler si présents).

Run : `ninja -C build-asan >/dev/null && meson test -C build-asan --print-errorlogs 2>&1 | tail -6`
Expected : tout OK.

Run : `meson test -C build --suite gui --print-errorlogs 2>&1 | tail -8`
Expected : tout OK (les scénarios existants `test_polling`, `test_raccourcis_macros` écrivent des fichiers à l'ancien format : ils vérifient la lecture de l'ancien format et l'envoi par le nouveau chemin).

- [ ] **Step 11 : commit**

```bash
git add src/macros.c src/macros.h src/macro_panel.c src/macro_panel.h src/script_engine.c src/term_config.c src/gtkterm.c \
        tests/test_macros_send.c tests/test_script_engine.c tests/stubs/stub_macros.c tests/stubs/stub_macros.h tests/stubs/stub_ui.c
git commit -m "macros : l'interface passe au modèle (envoi unique, raccourcis et édition par identifiant, panneau vue, Lua) — M16, M44

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 6 : comportement du polling dans le panneau (période validée, onglets masqués, clignotement à la demande)

**Files :**
- Modify : `src/macro_panel.c`
- Test : `tests/gui/test_polling.py` (étendu)

**Interfaces :**
- Consumes : `gt_macro_period_parse` (tâche 2), `gt_macro_poller_*` (tâche 4), `macros_get_set`/`macros_get_poller`/`macros_save_file` (tâche 5), `widget_macro_id`, `save_macros`, `update_button_appearance`, `macro_panel_polling_changed`, `polling_blink_callback` (tâche 5, `macro_panel.c`).

- [ ] **Step 1 : étendre le scénario graphique (échoue sur la période)**

Remplacer `tests/gui/test_polling.py` par :

```python
# Polling d'une macro : envois répétés par la liaison, arrêt au second clic ; masquer un autre onglet ne l'arrête pas (note EM-1) ;
# la période saisie n'est appliquée qu'à la validation, jamais touche par touche (M22).
from gtktest import *

e = Env('polling')
ma = os.path.join(e.root, 'macros.ini')
e.write_macros(ma, [('PollA', 'None', '<A1>', 'T1', '', 1, 200), ('Autre', 'None', '<B>', 'T2', '', 0, 1000)])
e.write_rc(e.profile(macros_file=ma))
e.start(); time.sleep(2)
bw = Broadway(e.display)
# Une macro en mode polling porte le libellé « ⏱ <nom> » (macro_panel.c, update_button_appearance)
b = macro_button(e.app, '⏱ PollA')
act(b)
d = e.read_pty(1.5)
check(d.count(b'<A1>') >= 4, 'polling actif : au moins 4 envois en 1,5 s', d)

# Masquer l'onglet T2 par le menu contextuel de la barre d'onglets : le polling de T1 continue
bw.click_obj(find1(e.app, 'toggle button', '^T2$', showing=True), button=3); time.sleep(0.8)
act(find1(e.app, 'check menu item', '^T2$', showing=True)); time.sleep(0.8)
e.drain(0.2)
d = e.read_pty(1.0)
check(d.count(b'<A1>') >= 3, 'onglet T2 masque : le polling de T1 continue', d)

# Période : « 2000 » tapé touche par touche n'est pas appliqué avant la validation (sinon « 2 » donnerait une rafale à 2 ms)
bw.click_obj(macro_button(e.app, '⏱ PollA'), button=3); time.sleep(0.8)
bw.key('a', GDK_CONTROL); time.sleep(0.2)
e.drain(0.1)
bw.type('2000')
d = e.read_pty(0.6)
check(d.count(b'<A1>') <= 5, 'saisie en cours non appliquee', len(d))
bw.key('Return'); time.sleep(0.8)
e.drain(0.3)
d = e.read_pty(1.5)
check(d.count(b'<A1>') <= 1, 'periode 2000 ms appliquee a la validation', d)
check('polling_period_ms=2000' in open(ma).read(), 'periode enregistree', open(ma).read()[:400])

act(macro_button(e.app, '⏱ PollA')); time.sleep(0.5); e.drain(0.3)
d = e.read_pty(1.0)
check(d == b'', 'polling arrete', d)
finish(e)
sys.exit(conclude())
```

- [ ] **Step 2 : vérifier l'échec**

Run : `meson test -C build --suite gui test_polling --print-errorlogs 2>&1 | tail -5; grep -E 'ECHEC' build/tests/gui/runs/polling/result.txt`
Expected : échec de « saisie en cours non appliquee » (comportement provisoire de la tâche 5 : période appliquée à chaque frappe) ; les autres vérifications peuvent passer.

- [ ] **Step 3 : période appliquée à la validation (M22)**

Dans `macro_panel.c`, supprimer `on_polling_period_changed` et sa connexion `"changed"` ; ajouter :

```c
/* Période saisie appliquée si elle est valide (entier de 1 à 86 400 000 ms) ; sinon champ en erreur, ancienne période gardée (M22) */
static gboolean apply_period_entry(GtkWidget *entry)
{
	guint id = widget_macro_id(entry);
	const GtMacro *m = gt_macro_set_find(macros_get_set(), id);
	GtkStyleContext *ctx = gtk_widget_get_style_context(entry);
	guint period;

	if (m == NULL)
		return FALSE;
	if (!gt_macro_period_parse(gtk_entry_get_text(GTK_ENTRY(entry)), &period))
	{
		gtk_style_context_add_class(ctx, "error");
		return FALSE;
	}
	gtk_style_context_remove_class(ctx, "error");
	if (period != m->polling_period_ms)
	{
		gt_macro_set_set_polling(macros_get_set(), id, m->polling, period);
		gt_macro_poller_period_changed(macros_get_poller(), id);
		save_macros();
	}
	return TRUE;
}

static void on_polling_period_activate(GtkEntry *entry, gpointer menu)
{
	if (apply_period_entry(GTK_WIDGET(entry)))
		gtk_menu_shell_deactivate(GTK_MENU_SHELL(menu));
}

static gboolean destroy_widget_idle(gpointer widget)
{
	gtk_widget_destroy(GTK_WIDGET(widget));
	return G_SOURCE_REMOVE;
}

/* Fermeture du menu (Entrée, clic ailleurs, Échap) : la période valide est appliquée, puis le menu est détruit (il fuyait) */
static void on_polling_menu_hide(GtkWidget *menu, gpointer entry)
{
	apply_period_entry(GTK_WIDGET(entry));
	g_idle_add(destroy_widget_idle, menu);
}
```

Dans `on_macro_button_right_click`, après la création du champ période : `g_signal_connect(period_entry, "activate", G_CALLBACK(on_polling_period_activate), menu);` et `g_signal_connect(menu, "hide", G_CALLBACK(on_polling_menu_hide), period_entry);`. Si, sous broadway, Entrée active l'élément de menu au lieu du champ, le menu se ferme et `on_polling_menu_hide` applique la période : les deux chemins mènent au même résultat. Le menu des onglets (`on_macro_notebook_button_press`) est détruit de la même façon à sa fermeture : `g_signal_connect(menu, "hide", G_CALLBACK(on_tab_menu_hide), NULL);` avec `on_tab_menu_hide` qui fait `g_idle_add(destroy_widget_idle, menu)`.

- [ ] **Step 4 : masquer un onglet n'arrête que ses pollings (note EM-1)**

```c
/* Les pollings des macros d'un onglet masqué s'arrêtent (rien ne tournerait sans bouton visible) ; les autres continuent */
static void stop_tab_pollings(const gchar *tab_name)
{
	GtMacroSet *set = macros_get_set();

	for (guint i = 0; i < gt_macro_set_count(set); i++)
	{
		const GtMacro *m = gt_macro_set_get(set, i);
		const gchar *tab = m->tab[0] != '\0' ? m->tab : _("General");

		if (g_strcmp0(tab, tab_name) == 0)
			gt_macro_poller_stop(macros_get_poller(), m->id);
	}
}
```

Dans `on_macro_tab_visibility_toggled`, quand l'onglet est masqué (`!active`) : appeler `stop_tab_pollings(tab_name)` avant d'insérer le nom dans `hidden_macro_tabs` et de reconstruire.

- [ ] **Step 5 : clignotement seulement quand une macro tourne**

Remplacer le `g_timeout_add(500, polling_blink_callback, NULL)` permanent de `create_macro_panel` par une minuterie gérée depuis le rappel d'état (cette définition de `macro_panel_polling_changed` remplace celle de la tâche 5) :

```c
static guint blink_source = 0;

/* Minuterie de clignotement : n'existe que tant qu'au moins une macro tourne */
static void update_blink_timer(void)
{
	guint running = gt_macro_poller_running_count(macros_get_poller());

	if (running > 0 && blink_source == 0)
		blink_source = g_timeout_add(500, polling_blink_callback, NULL);
	else if (running == 0 && blink_source != 0)
	{
		g_source_remove(blink_source);
		blink_source = 0;
	}
}

void macro_panel_polling_changed(guint id, gboolean running)
{
	if (macro_button_table == NULL)
		return;
	update_button_appearance(id);
	update_blink_timer();
}
```

- [ ] **Step 6 : info-bulle sans tampon fixe**

Dans `rebuild_macro_buttons`, remplacer `gchar tooltip[256]; g_snprintf(...)` par `gchar *tooltip = g_strdup_printf(_("Shortcut: %s\nAction: %s"), m->shortcut, m->action);` et libérer `tooltip` (`g_free`) sur **chaque** chemin de sortie de l'itération (y compris les `continue` du bouton de liste à deux valeurs).

- [ ] **Step 7 : construire et tester**

Run : `ninja -C build 2>&1 | grep -E 'warning|error' | head; meson test -C build --print-errorlogs 2>&1 | tail -4`
Expected : aucune alerte ; tout OK.

Run : `meson test -C build --suite gui test_polling test_raccourcis_macros --print-errorlogs 2>&1 | tail -5`
Expected : OK (toutes les lignes de `build/tests/gui/runs/polling/result.txt` à `OK`).

- [ ] **Step 8 : commit**

```bash
git add src/macro_panel.c tests/gui/test_polling.py
git commit -m "macros : période de polling appliquée à la validation, onglet masqué n'arrête que ses pollings, clignotement à la demande (M22)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7 : scénarios graphiques d'édition et de migration, traductions, documentation, acceptation

**Files :**
- Create : `tests/gui/test_macros_edition.py`, `tests/gui/test_macros_migration.py`
- Modify : `tests/gui/gtktest.py` (aide `write_macros_v2`), `tests/gui/meson.build`, `po/POTFILES.in`, `po/fr.po`, `CLAUDE.md`, `tests/README.md`, `docs/superpowers/reviews/2026-10-06-revue-code.md`

**Interfaces :**
- Consumes : toute l'application (tâches 1 à 6) ; harnais `tests/gui/gtktest.py` (`Env`, `find1`, `find`, `act`, `menu_key`, `wait_window`, `macro_button`, `file_dialog`, `check`, `finish`, `conclude`, `Broadway`).

- [ ] **Step 1 : aide d'écriture d'un fichier v2 dans le harnais**

Dans `tests/gui/gtktest.py`, classe `Env`, après `write_macros` :

```python
    def write_macros_v2(self, path, macros, lists=None):
        """Fichier de macros au format v2 (macro_file.c). macros : dicts (label, shortcut, action, tab, args [liste], polling, period) ;
        lists : [(nom, [(affichage, valeur), ...])]. Échappements de GKeyFile : \\ \\n \\t \\r, espace de tête \\s, « ; » dans les listes."""
        def s(v):
            v = v.replace('\\', '\\\\').replace('\n', '\\n').replace('\t', '\\t').replace('\r', '\\r')
            return '\\s' + v[1:] if v.startswith(' ') else v

        def l(items):
            return ''.join(s(x).replace(';', '\;') + ';' for x in items)
        out = ['[gtkterm-macros]', 'version=2', '']
        for i, m in enumerate(macros):
            out.append('[macro:%d]' % (i + 1))
            for k in ('label', 'shortcut', 'action', 'tab'):
                out.append('%s=%s' % (k, s(m.get(k, ''))))
            if m.get('args') is not None:
                out.append('args=%s' % l(m['args']))
            out.append('polling=%s' % ('true' if m.get('polling') else 'false'))
            out.append('polling_period_ms=%d' % m.get('period', 1000))
            out.append('')
        for i, (nom, entrees) in enumerate(lists or []):
            out += ['[list:%d]' % (i + 1), 'name=%s' % s(nom), 'displays=%s' % l(a for a, _ in entrees), 'values=%s' % l(v for _, v in entrees), '']
        with open(path, 'w') as f:
            f.write('\n'.join(out) + '\n')
```

- [ ] **Step 2 : scénario d'édition**

Créer `tests/gui/test_macros_edition.py` :

```python
# Fenêtre d'édition des macros : supprimer, déplacer, capturer un raccourci ; arguments et polling suivent leur macro, jamais la
# position (M16) ; « Capture Shortcut » fonctionne en sélection multiple (M44) ; une action contenant « :: » et « | » survit à
# l'enregistrement et au rechargement du fichier (M21).
from gtktest import *


def ouvrir_edition(e, bw, term):
    menu_key(bw, e.app, 'Configuration', 'Macros', focus_obj=term)
    w = wait_window(e.app, 'Configure Macros')
    time.sleep(0.5)
    return w


def table(w):
    return [t for t in find(w, None) if role(t) in ('tree table', 'table')][0]


def selectionner(bw, w, ligne):
    """Sélectionne une seule ligne de la liste des macros (AT-SPI ; à défaut, clic sur la cellule « Label »)."""
    tbl = table(w)
    try:
        Atspi.Selection.clear_selection(tbl)
    except Exception:
        pass
    if not Atspi.Table.add_row_selection(tbl, ligne):
        bw.click_obj(Atspi.Table.get_accessible_at(tbl, ligne, 0))
    time.sleep(0.3)


def libelles(w):
    tbl = table(w)
    return [get_text(Atspi.Table.get_accessible_at(tbl, r, 0)) or name(Atspi.Table.get_accessible_at(tbl, r, 0)) for r in range(Atspi.Table.get_n_rows(tbl))]


def bouton(w, nom):
    return find1(w, 'push button', '^_?%s$' % nom, showing=True)


def envoi(e, lib, attendu):
    e.drain(0.2)
    act(macro_button(e.app, lib))
    d = e.read_pty(0.6)
    check(d == attendu, 'clic %s envoie %r' % (lib, attendu), d)


e = Env('macros_edition')
ma = os.path.join(e.root, 'macros.ini')
e.write_macros_v2(ma, [
    dict(label='A', action='<A>', polling=True, period=200),
    dict(label='B', action='<B%d::x|y>', args=['5']),
    dict(label='C', action='<C%d>', args=['7']),
])
e.write_rc(e.profile(macros_file=ma))
e.start(); time.sleep(2)
bw = Broadway(e.display)
term = find1(e.app, 'terminal')

envoi(e, 'B', b'<B5::x|y>')
envoi(e, 'C', b'<C7>')
act(macro_button(e.app, '⏱ A')); time.sleep(0.5)
check(e.read_pty(0.8).count(b'<A>') >= 2, 'polling de A actif')

# 1. supprimer A (1re ligne) : son polling s'arrête, aucune macro n'en hérite, C garde son argument (M16)
w = ouvrir_edition(e, bw, term)
check(libelles(w) == ['A', 'B', 'C'], 'liste initiale', libelles(w))
selectionner(bw, w, 0)
bw.click_obj(bouton(w, 'Delete')); time.sleep(0.3)
bw.click_obj(bouton(w, 'OK')); time.sleep(1.0)
e.drain(0.3)
check(e.read_pty(0.8) == b'', 'polling de la macro supprimee arrete')
check(not [b for b in find(e.app, 'push button', showing=True) if (name(b) or '').startswith('⏱')], 'aucune macro en mode polling')
envoi(e, 'C', b'<C7>')
envoi(e, 'B', b'<B5::x|y>')

# 2. remonter C au-dessus de B : l'ordre change dans le fichier, les arguments suivent
w = ouvrir_edition(e, bw, term)
selectionner(bw, w, 1)
bw.click_obj(find1(w, 'push button', '^Move _?Up$', showing=True))
time.sleep(0.3)
bw.click_obj(bouton(w, 'OK')); time.sleep(1.0)
texte = open(ma).read()
check('[gtkterm-macros]' in texte and texte.find('label=C') < texte.find('label=B'), 'ordre C puis B enregistre au format v2', texte[:500])
check('action=<B%d::x|y>' in texte and 'args=5;' in texte, 'action avec :: et | enregistree telle quelle', texte[:500])
envoi(e, 'C', b'<C7>')
envoi(e, 'B', b'<B5::x|y>')

# 3. capturer un raccourci pour B (M44 : la liste est en sélection multiple)
w = ouvrir_edition(e, bw, term)
selectionner(bw, w, 1)
bw.click_obj(find1(w, 'push button', '^_?Capture Shortcut$', showing=True)); time.sleep(0.3)
bw.key('F8', GDK_CONTROL); time.sleep(0.5)
bw.click_obj(bouton(w, 'OK')); time.sleep(1.0)
check('shortcut=<Primary>F8' in open(ma).read() or 'shortcut=<Control>F8' in open(ma).read(), 'raccourci capture enregistre', open(ma).read()[:500])
bw.click_obj(term); bw.warm(); e.drain(0.2)
bw.key('F8', GDK_CONTROL)
d = e.read_pty(0.8)
check(d == b'<B5::x|y>', 'Ctrl+F8 envoie la macro B', d)

# 4. recharger le fichier par le menu : tout est relu à l'identique
menu_key(bw, e.app, 'Configuration', 'Load macros file...', focus_obj=term)
file_dialog(bw, e.app, 'Load macros file', ma)
envoi(e, 'B', b'<B5::x|y>')
envoi(e, 'C', b'<C7>')
finish(e)
sys.exit(conclude())
```

Le libellé GTK du bouton est « Move _Up » ; AT-SPI expose « Move Up ». Si `Atspi.Table.add_row_selection` n'est pas pris en charge par la vue, le repli par clic sur la cellule suffit ; ajuster le nom accessible des boutons d'après `dump(w)` si une recherche échoue, sans modifier l'application pour le test.

- [ ] **Step 3 : scénario de migration**

Créer `tests/gui/test_macros_migration.py` :

```python
# Ancien fichier de macros : lu au démarrage (macros, arguments, listes), inchangé tant que rien n'est enregistré, réécrit au format
# v2 au premier enregistrement, avec une copie .avant-migration identique à l'original (M21).
from gtktest import *

e = Env('macros_migration')
ma = os.path.join(e.root, 'macros.ini')
copie = ma + '.avant-migration'
e.write_macros(ma, [('Liste', 'None', 'S %#Vitesses', '', '1', 0, 1000), ('Arg', 'None', 'A%d', '', '42', 0, 1000)],
               lists=['Vitesses:Lent=0|Rapide=1'])
original = open(ma).read()
e.write_rc(e.profile(macros_file=ma))
e.start(); time.sleep(2)
bw = Broadway(e.display)
term = find1(e.app, 'terminal')

# Une liste de deux valeurs donne deux boutons « Liste Lent » / « Liste Rapide »
e.drain(0.2)
act(macro_button(e.app, 'Liste Rapide'))
d = e.read_pty(0.6)
check(d == b'S 1', 'liste lue : Liste Rapide envoie S 1', d)
e.drain(0.2)
act(macro_button(e.app, 'Arg'))
d = e.read_pty(0.6)
check(d == b'A42', 'argument lu : Arg envoie A42', d)
check(open(ma).read() == original and not os.path.exists(copie), 'fichier inchange avant tout enregistrement')

# Premier enregistrement : fenêtre des macros validée sans modification
menu_key(bw, e.app, 'Configuration', 'Macros', focus_obj=term)
w = wait_window(e.app, 'Configure Macros'); time.sleep(0.5)
bw.click_obj(find1(w, 'push button', '^_?OK$', showing=True)); time.sleep(1.0)
texte = open(ma).read()
check('[gtkterm-macros]' in texte and 'version=2' in texte, 'fichier reecrit au format v2', texte[:300])
check(os.path.exists(copie) and open(copie).read() == original, 'copie .avant-migration identique a l\'original')
e.drain(0.2)
act(macro_button(e.app, 'Arg'))
d = e.read_pty(0.6)
check(d == b'A42', 'apres migration : Arg envoie A42', d)
finish(e)
sys.exit(conclude())
```

Ajouter `'test_macros_edition',` et `'test_macros_migration',` à `gui_scenarios` dans `tests/gui/meson.build`.

Run : `meson test -C build --suite gui test_macros_edition test_macros_migration --print-errorlogs 2>&1 | tail -6`
Expected : OK. En cas d'échec, lire `build/tests/gui/runs/macros_edition/result.txt` et `gtkterm.stderr` ; un défaut de l'application se corrige dans `src/` (avec un test unitaire ou d'intégration qui le reproduit), un défaut du scénario dans le scénario.

- [ ] **Step 4 : traductions**

- `po/POTFILES.in` : ajouter `src/macro_file.c` (ordre alphabétique de la liste existante).
- `po/fr.po` : ajouter les entrées (et vérifier qu'aucune entrée utilisée n'est marquée `fuzzy`) :

```po
msgid "Cannot load macros file: %s"
msgstr "Impossible de charger le fichier de macros : %s"

msgid "Cannot save macros file: %s"
msgstr "Impossible d'enregistrer le fichier de macros : %s"

msgid "Unsupported macros file version (written by a newer GTKTerm?)"
msgstr "Version de fichier de macros non prise en charge (écrit par une version plus récente de GTKTerm ?)"
```

Run : `msgfmt --check -o /dev/null po/fr.po && echo ok`
Expected : `ok`.

- [ ] **Step 5 : documentation**

- `CLAUDE.md` :
  - liste `core_sources` du paragraphe **Tests** : ajouter `macro_set.c`, `macro_file.c`, `macro_poller.c` ;
  - paragraphe **Macros subsystem** remplacé par :

```markdown
**Macros subsystem (sub-project 4):**
- `macro_set.c` (core) — `GtMacroSet`: ordered macros with a **stable in-memory id** (never reused), label/shortcut/action/tab, stored arguments, polling mode + period (1 ms – 24 h), its own named lists (`GtMacroLists`, `macros_list.c`); `gt_macro_expand()` is the only expansion (format then escapes; `n_args < 0` = stored arguments); `gt_macro_set_apply_edits()` applies the edit window rows by id, so arguments and polling follow the macro, never the position.
- `macros_format.c` (core) — printf-style arguments (`%d`, `%s`, `%#ListName`, length modifiers); the lists are passed in (`%#x` is a list only if list `x` exists).
- `macro_file.c` (core) — the standalone `.ini`: v2 format (`[gtkterm-macros] version=2`, `[macro:N]`, `[list:N]`, values escaped by GKeyFile); old `[macros]`/`[lists]` files are read and rewritten as v2 on the first save, after a one-time copy `<file>.avant-migration`.
- `macro_poller.c` (core) — polling scheduler: one GLib timeout per running macro, sends through a callback (the app's `macros_send`), state callback to the panel. The running state is never saved.
- `macros.c` (UI) — the app's macro state (current set, file path, poller on the default link), `macros_send(id, args, n)` — the **only** macro send path (panel click, shortcut, polling, Lua) —, shortcuts connected by id, the edit window.
- `macro_panel.c` (UI) — the button panel, a view of the set and the poller (argument fields write to the model; rebuilding never touches polling).

Macros and lists live in that standalone `.ini`, separate from the port config; the last-used path is remembered in the active profile of `.gtktermrc`.
```

  - phrase d'intégration du paragraphe **Tests** : `macros.c` reste testé avec les bouchons d'interface (inchangé).
- `tests/README.md` : ajouter `test_macro_set`, `test_macro_file`, `test_macro_poller` à la liste des tests unitaires du cœur ; préciser pour `stub_macros.c` : « bouchon de l'état des macros (jeu fourni par le test) pour `test_script_engine` ».

- [ ] **Step 6 : revue de code**

Dans `docs/superpowers/reviews/2026-10-06-revue-code.md` :
- à la suite de M16, M21, M22 : ligne `  Corrigé par le sous-projet 4 (\`feature/refonte-macros\`).` (même forme que les autres) ;
- après M43, ajouter : `- M44. « Capture Shortcut » et « Delete Shortcut » de la fenêtre des macros appellent \`gtk_tree_selection_get_selected\`, inopérant quand la liste est en sélection multiple (son mode) : les deux boutons ne font rien. Trouvé à la conception du sous-projet 4. Corrigé par le sous-projet 4 (\`feature/refonte-macros\`) : première ligne sélectionnée par \`gtk_tree_selection_get_selected_rows\` ; essai graphique \`test_macros_edition.py\`.` ;
- note EM-1 (sous-projet 1) : ajouter « — traité par le sous-projet 4 : masquer un onglet n'arrête que ses pollings ; la reconstruction du panneau ne touche plus à l'ordonnanceur. » ;
- remarque reportée « Relecture finale » du sous-projet 1 : annoter « lists_model fuit », « clé .ini sans valeur », « fuite g_path_get_dirname » par « (traité par le sous-projet 4) » ; remarque « Macros (`macro_panel.c`) : `update_button_label` … » du sous-projet 2 : idem ;
- point 3 de l'architecture (« polling des macros en trois exemplaires… arguments aussi ») : ajouter « Traité pour les macros par le sous-projet 4 (modèle `GtMacroSet` + ordonnanceur). ».

- [ ] **Step 7 : acceptation**

Run : `ninja -C build 2>&1 | grep -E 'warning|error' | head; meson test -C build --print-errorlogs 2>&1 | tail -5; grep -E '# TODO.*(M16|M21|M22)' build/meson-logs/testlog.txt`
Expected : aucune alerte ; tout OK ; aucune ligne `# TODO` pour M16/M21/M22.

Run : `ninja -C build-asan >/dev/null && meson test -C build-asan --print-errorlogs 2>&1 | tail -5`
Expected : tout OK, aucun rapport LeakSanitizer.

Run : `meson test -C build --suite gui --print-errorlogs 2>&1 | tail -25`
Expected : tous les scénarios OK (dont `test_polling`, `test_raccourcis_macros`, `test_macros_edition`, `test_macros_migration`).

Run : `grep -rn -E '\bmacro_t\b|get_shortcuts|create_shortcuts|add_shortcuts|remove_shortcuts|shortcut_callback|send_macro_with_arg|macro_polling_t|macro_list_find|macros_app_lists' src tests`
Expected : aucune sortie.

- [ ] **Step 8 : commit**

```bash
git add tests/gui/gtktest.py tests/gui/test_macros_edition.py tests/gui/test_macros_migration.py tests/gui/meson.build po/POTFILES.in po/fr.po CLAUDE.md tests/README.md docs/superpowers/reviews/2026-10-06-revue-code.md
git commit -m "macros : scénarios graphiques d'édition et de migration, traductions, documentation, revue (M16, M21, M22, M44)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
