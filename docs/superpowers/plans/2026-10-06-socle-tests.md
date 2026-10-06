# Socle de tests et hygiène du dépôt — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** obtenir une suite `meson test` (GLib GTest) couvrant les modules de GTKTerm indépendants de GTK, signaler les bogues connus sans casser la suite, et assainir la construction et le dépôt sans changer le comportement de l'application.

**Architecture :** les modules qui ne dépendent que de GLib/termios (`macros_format.c`, `macros_list.c`, `baudrates.c`, `baud.c`, `parsecfg.c`, `i18n.c`) sont compilés une fois dans une `static_library('gtkterm_core')`, liée à la fois par l'exécutable `gtkterm` et par les exécutables de test de `tests/`. Chaque cas de test décrit le comportement attendu ; un cas lié à un défaut de la revue est marqué par son identifiant et sort en `# TODO` (incomplete) tant que le défaut existe.

**Tech Stack :** C (gnu11), Meson ≥ 1.0 / Ninja, GLib 2.80 (GTest, sortie TAP), openpty (libutil), luac 5.4.

**Spec :** `docs/superpowers/specs/2026-10-06-socle-tests-design.md` (revue d'origine : `docs/superpowers/reviews/2026-10-06-revue-code.md`).

## Global Constraints

- Branche : `feature/socle-tests` (déjà créée, contient la spec et la revue). Ne jamais pousser sur `origin` ; le remote de publication est `Maison`. Ne pas pousser ni fusionner dans ce plan : la fusion suit la règle de l'utilisateur (écrasement en commits thématiques) et se fait à part.
- Messages de commit en français, terminés par la ligne `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Longueur de ligne : 200 caractères maximum, ne pas replier à 80.
- Style des **nouveaux** fichiers C : tabulations, accolades Allman (`{` seule sur sa ligne), pas d'espace avant `(` (`if(x)`), comme `.clang-format`. Ne reformater **aucun** fichier existant.
- Aucun changement de comportement de l'application, sauf la révision git (Task 6).
- Ne corriger aucun bogue du logiciel : un bogue révélé par un test est marqué (`test_conclude(..., "<ID>", ...)`), pas corrigé.
- Options de compilation exactes : `default_options : ['c_std=gnu11', 'warning_level=2']` et `'-Wno-unused-parameter', '-Werror=format-security', '-Werror=implicit-function-declaration'`.
- Les exécutables de test sont en `build_by_default : false` et déclarés `test(<nom>, exe, args : ['--tap'], protocol : 'tap', suite : 'unit')`.
- `jq` n'est pas installé : utiliser `python3` pour tout traitement JSON.
- Un hook de l'outil relance `ninja -C build` après chaque modification de `src/*.c|*.h` : une erreur de compilation apparaît immédiatement, la corriger avant de continuer.

## Review Focus

1. **Installation en root (`pkexec ninja install` de `gtkterm-update.sh`, `sudo ninja install`)** : la révision embarquée doit rester le SHA réel, pas `unknown`. Test ajouté à la Task 6 (simulation `GIT_TEST_ASSUME_DIFFERENT_OWNER=1`).
2. **Construction depuis une archive sans `.git`** (`git archive`) : la construction doit réussir et la révision valoir `unknown`. Test ajouté à la Task 6.
3. **Configuration neuve** (`meson setup` dans un répertoire vide) avec les nouvelles options et la bibliothèque cœur : doit réussir sans erreur. Vérifié aux Tasks 1 et 9 sur un répertoire neuf.
4. **Symboles manquants à l'exécution** après le passage en bibliothèque statique (GtkBuilder et `export_dynamic`) : l'application doit démarrer et fonctionner comme avant. Vérification manuelle par l'utilisateur à la Task 9 (étape dédiée), plus `gtkterm --help` aux Tasks 1 et 6.
5. **Machine sans la locale `C.UTF-8` ni `openpty`** : les tests concernés doivent être sautés (`g_test_skip`), jamais en échec. Couvert aux Tasks 3 et 4.

---

### Task 1 : bibliothèque cœur, options de compilation, aide de test et premiers tests (`macros_list`)

**Files :**
- Modify : `meson.build` (racine)
- Modify : `src/meson.build` (réécrit en entier)
- Create : `tests/meson.build`
- Create : `tests/test_util.h`
- Create : `tests/test_macros_list.c`

**Interfaces :**
- Consumes : rien.
- Produces :
  - variable meson `gtkterm_core_dep` (bibliothèque cœur + en-têtes de `src/` + `config.h` + glib), définie dans `src/meson.build`, utilisée par `tests/meson.build` ;
  - variable meson `unit_tests` (dictionnaire `nom → [dépendances supplémentaires]`) dans `tests/meson.build`, que les tâches suivantes complètent ;
  - `tests/test_util.h` :
    - `void test_conclude(gboolean conforme, const gchar *bug, const gchar *detail)` : conclut un cas ; `bug == NULL` → échec si non conforme ; `bug != NULL` → `incomplete` si non conforme, échec « semble corrigé » si conforme ;
    - `gchar *test_hex(const guint8 *data, gsize len)` : `"61 5C 62"` (à libérer) ;
    - `gchar *test_write_file(const gchar *dir, const gchar *name, const gchar *content)` : écrit le fichier, renvoie son chemin (à libérer) ;
    - `void test_rmdir(const gchar *dir)` : supprime un répertoire temporaire plat.

- [ ] **Step 1 : écrire le test `macros_list` (il ne peut pas encore être construit)**

Créer `tests/test_macros_list.c` :

```c
/* Tests de macros_list.c : modèle des listes nommées utilisées par %#NomListe. */

#include <glib.h>

#include "macros_list.h"

static void
test_vide(void)
{
	macro_lists_free();
	g_assert_cmpint(macro_list_count(), ==, 0);
	g_assert_cmpint(macro_list_find("Mode"), ==, -1);
	g_assert_cmpint(macro_list_find(NULL), ==, -1);
	g_assert_null(macro_list_name(0));
	g_assert_cmpint(macro_list_entry_count(0), ==, 0);
	g_assert_null(macro_list_entry_display(0, 0));
	g_assert_null(macro_list_entry_value(0, 0));
}

static void
test_ajout_cree_liste(void)
{
	macro_lists_free();
	macro_list_add("Mode", "Run", "R");
	g_assert_cmpint(macro_list_count(), ==, 1);
	g_assert_cmpint(macro_list_find("Mode"), ==, 0);
	g_assert_cmpstr(macro_list_name(0), ==, "Mode");
	g_assert_cmpint(macro_list_entry_count(0), ==, 1);
	g_assert_cmpstr(macro_list_entry_display(0, 0), ==, "Run");
	g_assert_cmpstr(macro_list_entry_value(0, 0), ==, "R");
	macro_lists_free();
}

static void
test_ajout_liste_existante(void)
{
	macro_lists_free();
	macro_list_add("Mode", "Run", "R");
	macro_list_add("Mode", "Stop", "S");
	g_assert_cmpint(macro_list_count(), ==, 1);
	g_assert_cmpint(macro_list_entry_count(0), ==, 2);
	g_assert_cmpstr(macro_list_entry_display(0, 1), ==, "Stop");
	g_assert_cmpstr(macro_list_entry_value(0, 1), ==, "S");
	macro_lists_free();
}

static void
test_valeur_par_defaut(void)
{
	macro_lists_free();
	macro_list_add("Mode", "Idle", NULL);
	g_assert_cmpstr(macro_list_entry_value(0, 0), ==, "Idle");
	macro_lists_free();
}

static void
test_arguments_nuls_ignores(void)
{
	macro_lists_free();
	macro_list_add(NULL, "x", "y");
	macro_list_add("Mode", NULL, "y");
	g_assert_cmpint(macro_list_count(), ==, 0);
	macro_lists_free();
}

static void
test_recherche_sensible_casse(void)
{
	/* Comportement actuel : la recherche distingue la casse. */
	macro_lists_free();
	macro_list_add("Mode", "Run", "R");
	g_assert_cmpint(macro_list_find("mode"), ==, -1);
	macro_lists_free();
}

static void
test_ordre_des_listes(void)
{
	macro_lists_free();
	macro_list_add("A", "a1", NULL);
	macro_list_add("B", "b1", NULL);
	macro_list_add("A", "a2", NULL);
	g_assert_cmpint(macro_list_count(), ==, 2);
	g_assert_cmpstr(macro_list_name(0), ==, "A");
	g_assert_cmpstr(macro_list_name(1), ==, "B");
	g_assert_cmpint(macro_list_entry_count(0), ==, 2);
	g_assert_cmpint(macro_list_entry_count(1), ==, 1);
	macro_lists_free();
}

static void
test_suppression_entree(void)
{
	macro_lists_free();
	macro_list_add("L", "1", NULL);
	macro_list_add("L", "2", NULL);
	macro_list_add("L", "3", NULL);
	macro_list_remove_entry(0, 1);
	g_assert_cmpint(macro_list_entry_count(0), ==, 2);
	g_assert_cmpstr(macro_list_entry_display(0, 0), ==, "1");
	g_assert_cmpstr(macro_list_entry_display(0, 1), ==, "3");
	macro_list_remove_entry(0, 5);
	macro_list_remove_entry(0, -1);
	macro_list_remove_entry(3, 0);
	g_assert_cmpint(macro_list_entry_count(0), ==, 2);
	macro_lists_free();
}

static void
test_indices_hors_bornes(void)
{
	macro_lists_free();
	macro_list_add("L", "x", NULL);
	g_assert_null(macro_list_name(-1));
	g_assert_null(macro_list_name(1));
	g_assert_cmpint(macro_list_entry_count(-1), ==, 0);
	g_assert_null(macro_list_entry_display(0, -1));
	g_assert_null(macro_list_entry_display(0, 1));
	g_assert_null(macro_list_entry_value(1, 0));
	macro_lists_free();
}

static void
test_liberation_puis_reutilisation(void)
{
	macro_lists_free();
	macro_list_add("L", "x", NULL);
	macro_lists_free();
	g_assert_cmpint(macro_list_count(), ==, 0);
	g_assert_cmpint(macro_list_find("L"), ==, -1);
	macro_list_add("M", "y", NULL);
	g_assert_cmpint(macro_list_count(), ==, 1);
	g_assert_cmpint(macro_list_find("M"), ==, 0);
	macro_lists_free();
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
	g_test_add_func("/macros_list/suppression-entree", test_suppression_entree);
	g_test_add_func("/macros_list/indices-hors-bornes", test_indices_hors_bornes);
	g_test_add_func("/macros_list/liberation-puis-reutilisation", test_liberation_puis_reutilisation);
	return g_test_run();
}
```

- [ ] **Step 2 : constater que la suite n'existe pas encore**

Run : `meson test -C build --list`
Expected : seul `desktop file test` apparaît (aucun test `unit`).

- [ ] **Step 3 : créer l'aide commune `tests/test_util.h`**

```c
/* Aides communes aux tests unitaires GTKTerm (GLib GTest, sortie TAP).
 * Identifiants de bogues : docs/superpowers/reviews/2026-10-06-revue-code.md */

#ifndef TEST_UTIL_H_
#define TEST_UTIL_H_

#include <glib.h>
#include <glib/gstdio.h>

/* Conclut un cas de test.
 *   bug == NULL : cas ordinaire, échec s'il n'est pas conforme.
 *   bug != NULL : défaut connu de la revue (ex. "M18").
 *                 Non conforme -> « incomplete » (# TODO en TAP) : la suite reste verte.
 *                 Conforme     -> échec : le bogue semble corrigé, il faut retirer le marquage. */
static inline void
test_conclude(gboolean conforme, const gchar *bug, const gchar *detail)
{
	if(bug == NULL)
	{
		if(!conforme)
			g_test_fail_printf("%s", detail);
	}
	else if(conforme)
		g_test_fail_printf("bogue %s semble corrigé (%s) : retirer le marquage", bug, detail);
	else
		g_test_incomplete_printf("bogue %s — sous-projet 1 : %s", bug, detail);
}

/* Représentation hexadécimale « 61 5C 62 » ; à libérer avec g_free. */
static inline gchar *
test_hex(const guint8 *data, gsize len)
{
	GString *s = g_string_new(NULL);

	for(gsize i = 0; i < len; i++)
		g_string_append_printf(s, i ? " %02X" : "%02X", data[i]);
	return g_string_free(s, FALSE);
}

/* Écrit content dans dir/name ; renvoie le chemin, à libérer avec g_free. */
static inline gchar *
test_write_file(const gchar *dir, const gchar *name, const gchar *content)
{
	gchar *path = g_build_filename(dir, name, NULL);

	g_assert_true(g_file_set_contents(path, content, -1, NULL));
	return path;
}

/* Supprime un répertoire temporaire plat (créé par g_dir_make_tmp). */
static inline void
test_rmdir(const gchar *dir)
{
	GDir *d = g_dir_open(dir, 0, NULL);
	const gchar *name;

	if(d == NULL)
		return;
	while((name = g_dir_read_name(d)) != NULL)
	{
		gchar *path = g_build_filename(dir, name, NULL);
		g_unlink(path);
		g_free(path);
	}
	g_dir_close(d);
	g_rmdir(dir);
}

#endif
```

Note : un cas par fonction de test. Ne jamais appeler `test_conclude` deux fois dans le même cas : GLib ne garde que le dernier état (un `incomplete` masquerait un échec précédent). Les tests de table utilisent `g_test_add_data_func`, avec un cas par ligne.

- [ ] **Step 4 : modifier le `meson.build` racine**

Remplacer l'appel `project(...)` par :

```meson
project(
	'gtkterm',
	'c',
	version: '1.3.2',
	meson_version : '>= 1.0.0',
	default_options : ['c_std=gnu11', 'warning_level=2']
)
```

Juste après `gsv_dep = dependency('gtksourceview-4', required: true)`, ajouter :

```meson
glib_dep = dependency('glib-2.0')
```

Remplacer le bloc `add_project_arguments(...)` par :

```meson
# Needed on some platforms for <termios.h>
add_project_arguments('-D_GNU_SOURCE=1',
		      '-D_XOPEN_SOURCE=700',
		      # Les rappels GTK ont des paramètres imposés : les 151 avertissements « unused parameter » seraient du bruit.
		      '-Wno-unused-parameter',
		      '-Werror=format-security',
		      '-Werror=implicit-function-declaration',
                      language : ['c'])
```

À la fin du fichier, après `subdir('po')`, ajouter :

```meson
subdir('tests')
```

- [ ] **Step 5 : réécrire `src/meson.build`**

Contenu complet :

```meson
prog_sh = find_program('sh')
baudrates_h = custom_target(
	    'baudrates.h',
	    output : 'baudrates.h',
	    input : 'baudrates.sh',
	    command : [prog_sh, '@INPUT@', '@OUTPUT@'])

# Cœur sans GTK : compilé une fois, lié par l'exécutable et par les tests (tests/).
core_sources = [
	'baud.c',
	'baudrates.c',
	baudrates_h,
	'i18n.c',
	'i18n.h',
	'macros_format.c',
	'macros_format.h',
	'macros_list.c',
	'macros_list.h',
	'parsecfg.c',
	'parsecfg.h',
	'serial.h',
]

gtkterm_core = static_library(
	'gtkterm_core', core_sources,
	dependencies : [glib_dep, config]
)

gtkterm_core_dep = declare_dependency(
	link_with : gtkterm_core,
	include_directories : include_directories('.'),
	dependencies : [glib_dep, config]
)

sources = [
	'buffer.c',
	'buffer.h',
	'cmdline.c',
	'cmdline.h',
	'device_monitor.c',
	'device_monitor.h',
	'files.c',
	'files.h',
	'gtkterm.c',
	'interface.c',
	'interface.h',
	'logging.c',
	'logging.h',
	'macros.c',
	'macros.h',
	'terminal_display.c',
	'terminal_display.h',
	'macro_panel.c',
	'macro_panel.h',
	'search.c',
	'search.h',
	'serial.c',
	'term_config.c',
	'term_config.h',
	'transport.c',
	'transport.h',
	'update.c',
	'update.h',
	'user_signals.c',
	'user_signals.h',
	'script_engine.c',
	'script_engine.h',
	'script_panel.c',
	'script_panel.h',
	gresources
]

executable(
	'gtkterm', sources,
	export_dynamic : true,
	dependencies : [
		gtk_deps,
		vte_deps,
		config,
		gudev_deps,
		lua_dep,
		gsv_dep,
		gtkterm_core_dep
	],
	install : true
)
```

- [ ] **Step 6 : créer `tests/meson.build`**

```meson
# Tests unitaires : un exécutable GTest par module du cœur (src/ : gtkterm_core).
# Construits seulement par « meson test » (build_by_default : false) pour garder « ninja » rapide.

# Dépendances supplémentaires par test ; les tâches suivantes complètent ce dictionnaire.
unit_tests = {
	'test_macros_list' : [],
}

foreach name, extra_deps : unit_tests
	exe = executable(name, name + '.c',
		dependencies : [gtkterm_core_dep] + extra_deps,
		build_by_default : false)
	test(name, exe, args : ['--tap'], protocol : 'tap', suite : 'unit')
endforeach
```

- [ ] **Step 7 : reconfigurer, construire, lancer les tests**

Run : `meson setup --wipe build >/dev/null && meson configure build | grep -E '^ +(c_std|warning_level) ' && ninja -C build 2>&1 | tail -3 && meson test -C build --suite unit`

(`--wipe` est nécessaire : les `default_options` de `project()` ne s'appliquent qu'à une configuration neuve, `--reconfigure` garderait `c_std=none` et `warning_level=1`.)
Expected : `c_std gnu11` et `warning_level 2` affichés ; la construction réussit (des avertissements `sign-compare` et `cast-function-type` sont normaux, environ 14) ; `meson test` affiche `1/1 test_macros_list OK` et `Fail: 0`.

- [ ] **Step 8 : vérifier sur une configuration neuve et contrôler l'exécutable**

Run :
```bash
rm -rf /tmp/claude-1315401155/-home-METRO-gabriel-git-gtkterm/e7fc7d21-128e-4797-b30d-8c870b9c4643/scratchpad/build-neuf
meson setup /tmp/claude-1315401155/-home-METRO-gabriel-git-gtkterm/e7fc7d21-128e-4797-b30d-8c870b9c4643/scratchpad/build-neuf >/dev/null
ninja -C /tmp/claude-1315401155/-home-METRO-gabriel-git-gtkterm/e7fc7d21-128e-4797-b30d-8c870b9c4643/scratchpad/build-neuf 2>&1 | grep -c "error" ; ./build/src/gtkterm --help >/dev/null 2>&1; echo "rc=$?"
```
Expected : `0` erreur, puis `rc=1` (`--help` sort en 1 depuis toujours, bogue mineur noté dans la revue) ; aucun « undefined symbol ».

- [ ] **Step 9 : commit**

```bash
git add meson.build src/meson.build tests/meson.build tests/test_util.h tests/test_macros_list.c
git commit -m "tests : bibliothèque gtkterm_core, options de compilation et premiers tests (macros_list)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2 : tests de `macros_format` (échappements, expansion, validation, arguments)

**Files :**
- Create : `tests/test_macros_format.c`
- Modify : `tests/meson.build` (ajout au dictionnaire `unit_tests`)

**Interfaces :**
- Consumes : `gtkterm_core_dep`, `unit_tests`, `test_conclude`, `test_hex` (Task 1). API testée (`src/macros_format.h`) : `GByteArray *parse_macro_string(const gchar *)`, `gchar *format_action_with_args(const gchar *action, const gchar **args, gint n_args)`, `gboolean macro_type_value_valid(const gchar *type, const gchar *value)`, `gboolean macro_type_is_float(const gchar *type)`, `gint macro_count_format_args(const gchar *)`, `macro_arg_info_t *macro_get_arg_infos(const gchar *, gint *count_out)` (champs `type`, `list_name`, `label`), `void macro_arg_infos_free(macro_arg_info_t *, gint)`.
- Produces : rien pour les autres tâches.

Les octets réellement envoyés sur le port valent `parse_macro_string(format_action_with_args(action, args, n))` (chemin de `macros.c:77-78` et de `script_engine.c:661-664`) : c'est cette composition que l'on teste. Toutes les valeurs attendues non marquées ont été observées sur le code actuel ; les cas marqués décrivent le comportement correct.

- [ ] **Step 1 : écrire le test**

Créer `tests/test_macros_format.c` :

```c
/* Tests de macros_format.c : séquences d'échappement, expansion des spécificateurs,
 * validation des valeurs, description des arguments. Les octets attendus sont ceux
 * envoyés sur le port : parse_macro_string(format_action_with_args(...)). */

#include <glib.h>
#include <locale.h>
#include <string.h>

#include "macros_format.h"
#include "macros_list.h"
#include "test_util.h"

/* ---- Échappements ---------------------------------------------------- */

typedef struct
{
	const gchar *entree;
	const gchar *octets;	/* attendu, en hexadécimal */
	const gchar *bug;
} cas_echappement_t;

static const cas_echappement_t cas_echappements[] =
{
	{ "ABC", "41 42 43", NULL },
	{ "\\x41", "41", NULL },
	{ "\\X7f", "7F", NULL },
	{ "\\x4", "04", NULL },
	{ "\\x4G", "04 47", NULL },
	{ "\\x414", "41 34", NULL },		/* comportement actuel, à décider : deux chiffres au plus */
	{ "\\x", "5C 78", NULL },
	{ "\\xZZ", "5C 78 5A 5A", NULL },
	{ "\\x0x", "00 78", NULL },
	{ "\\30", "5C 33 30", NULL },		/* correctif 7a697b2 : plus d'octal */
	{ "\\306", "5C 33 30 36", NULL },
	{ "\\0", "5C 30", NULL },		/* comportement actuel, à décider */
	{ "\\r\\n", "0D 0A", NULL },
	{ "\\t", "09", NULL },
	{ "\\\\", "5C", NULL },
	{ "\\\\x41", "5C 78 34 31", NULL },
	{ "a\\", "61 5C", NULL },
	{ "\\e", "5C 65", NULL },		/* comportement actuel, à décider */
	{ "\\\"", "5C 22", NULL },
};

static void
test_echappement(gconstpointer data)
{
	const cas_echappement_t *c = data;
	GByteArray *octets = parse_macro_string(c->entree);
	gchar *obtenu = test_hex(octets->data, octets->len);
	gchar *detail = g_strdup_printf("« %s » : attendu %s, obtenu %s", c->entree, c->octets, obtenu);

	test_conclude(g_strcmp0(obtenu, c->octets) == 0, c->bug, detail);
	g_free(detail);
	g_free(obtenu);
	g_byte_array_free(octets, TRUE);
}

/* ---- Expansion -------------------------------------------------------- */

typedef struct
{
	const gchar *action;
	const gchar *arg1;	/* NULL : aucun argument */
	const gchar *arg2;	/* NULL : au plus un argument */
	const gchar *attendu;	/* octets attendus, sous forme de chaîne C */
	const gchar *bug;
} cas_expansion_t;

static const cas_expansion_t cas_expansions[] =
{
	{ "V%d\\r", "42", NULL, "V42\r", NULL },
	{ "%hhd", "200", NULL, "-56", NULL },		/* tronqué ; la validation refuse 200 en amont */
	{ "%hhu", "300", NULL, "44", NULL },
	{ "%hd", "70000", NULL, "4464", NULL },
	{ "%u", "-1", NULL, "4294967295", NULL },
	{ "%lu", "-1", NULL, "18446744073709551615", NULL },
	{ "%d", "-5", NULL, "-5", NULL },
	{ "%+d", "5", NULL, "+5", NULL },
	{ "%-5d|", "5", NULL, "5    |", NULL },
	{ "%i", "42", NULL, "42", NULL },
	{ "%d", "12abc", NULL, "12", NULL },
	{ "%d", "", NULL, "0", NULL },
	{ "%x", "255", NULL, "ff", NULL },
	{ "%X", "255", NULL, "FF", NULL },
	{ "%02X", "10", NULL, "0A", NULL },
	{ "%x", "0x1f", NULL, "1f", NULL },
	{ "%x", "ff", NULL, "ff", "M23" },		/* argument d'un %x saisi en hexadécimal */
	{ "%x", "010", NULL, "10", "M23" },
	{ "%o", "8", NULL, "10", NULL },
	{ "%#x", "255", NULL, "0xff", "M18" },		/* aucune liste « x » : drapeau # de printf */
	{ "%#08x", "255", NULL, "0x0000ff", "M18" },
	{ "%#o", "8", NULL, "010", "M18" },
	{ "%f", "2.00000", NULL, "2.00000", NULL },	/* 9f48921 : zéros de fin conservés */
	{ "%f", "1e3", NULL, "1e3", NULL },		/* comportement actuel : valeur brute */
	{ "%f", "abc", NULL, "abc", NULL },		/* comportement actuel : la validation refuse « abc » en amont */
	{ "%.2f", "3.14159", NULL, "3.14", NULL },
	{ "%5.1f", "2", NULL, "  2.0", NULL },
	{ "%lf", "2.50", NULL, "2.50", NULL },
	{ "%e", "1234.5", NULL, "1.234500e+03", NULL },
	{ "%hf", "2.0", NULL, "2.0", NULL },
	{ "%5s", "abc", NULL, "  abc", NULL },
	{ "%s%s", "a", "b", "ab", NULL },
	{ "A\\r%sB", "x", NULL, "A\rxB", NULL },
	{ "%s", "a\\b", NULL, "a\\b", NULL },
	{ "%s", "\\x41", NULL, "\\x41", NULL },		/* f26847f : pas d'interprétation dans %s */
	{ "%s", "\\r", NULL, "\\r", NULL },
	{ "%s", "50%", NULL, "50%", NULL },
	{ "%5s", "a\\b", NULL, "  a\\b", "M19" },	/* largeur : 5 octets attendus */
	{ "%cn", "\\", NULL, "\\n", "M19" },		/* « \ » puis « n », pas un saut de ligne */
	{ "%c", "A", NULL, "A", NULL },
	{ "%c", "", NULL, " ", NULL },			/* comportement actuel, à décider */
	{ "100%%", "1", NULL, "100%", NULL },
	{ "100%%", NULL, NULL, "100%", NULL },
	{ "%d %d", "1", NULL, "1 %d", NULL },		/* argument manquant : spécificateur laissé tel quel */
	{ "%d", NULL, NULL, "%d", NULL },
	{ "plain", NULL, NULL, "plain", NULL },
	{ "[Speed] %d", "5", NULL, "5", NULL },
	{ "[a] %d [b] %d", "1", "2", "1 2", NULL },
	{ "XY[a]%d", "5", NULL, "XY5", NULL },
	{ "XY[a%%]%d", "5", NULL, "XY5", "M20" },	/* le « Y » disparaît */
	{ "arr[0] %d", "5", NULL, "arr5", NULL },	/* à décider : « [0] » pris pour un libellé */
	{ "50% off", "9", NULL, "5011ff", NULL },	/* à décider : « % o » est un spécificateur valide */
	{ "%#Mode", "S", NULL, "S", NULL },
	{ "%#Mode", "\\x02", NULL, "\\x02", NULL },	/* à décider : valeurs de liste sans échappements */
	{ "%d%&", "1", NULL, "1%&", NULL },		/* le séparateur %& est traité par le panneau */
};

static void
test_expansion(gconstpointer data)
{
	const cas_expansion_t *c = data;
	const gchar *args[2] = { c->arg1, c->arg2 };
	gint n_args = c->arg2 ? 2 : (c->arg1 ? 1 : 0);
	gchar *texte = format_action_with_args(c->action, args, n_args);
	GByteArray *octets = parse_macro_string(texte);
	gchar *attendu = test_hex((const guint8 *)c->attendu, strlen(c->attendu));
	gchar *obtenu = test_hex(octets->data, octets->len);
	gchar *detail = g_strdup_printf("« %s » [%s][%s] : attendu %s, obtenu %s", c->action, c->arg1 ? c->arg1 : "-", c->arg2 ? c->arg2 : "-", attendu, obtenu);

	test_conclude(g_strcmp0(obtenu, attendu) == 0, c->bug, detail);
	g_free(detail);
	g_free(obtenu);
	g_free(attendu);
	g_byte_array_free(octets, TRUE);
	g_free(texte);
}

/* ---- Validation ------------------------------------------------------- */

typedef struct
{
	const gchar *type;
	const gchar *valeur;
	gboolean attendu;
	const gchar *bug;
} cas_validation_t;

static const cas_validation_t cas_validations[] =
{
	{ "hhd", "127", TRUE, NULL },
	{ "hhd", "128", FALSE, NULL },
	{ "hhd", "-128", TRUE, NULL },
	{ "hhd", "-129", FALSE, NULL },
	{ "hhu", "255", TRUE, NULL },
	{ "hhu", "256", FALSE, NULL },
	{ "d", "2147483647", TRUE, NULL },
	{ "d", "2147483648", FALSE, NULL },
	{ "u", "-1", FALSE, NULL },
	{ "u", "4294967295", TRUE, NULL },
	{ "x", "0x10", TRUE, NULL },
	{ "x", "ff", TRUE, "M23" },
	{ "x", "09", TRUE, "M23" },
	{ "f", "2.5", TRUE, NULL },
	{ "f", "2,5", FALSE, NULL },
	{ "f", "1234567890", FALSE, NULL },	/* comportement actuel, à décider : limite de chiffres */
	{ "lf", "1234567890", TRUE, NULL },
	{ "d", " 12", TRUE, NULL },
	{ "d", "12 ", FALSE, NULL },
	{ "d", "abc", FALSE, NULL },
	{ "d", "", TRUE, NULL },		/* comportement actuel, à décider : vide accepté */
	{ "s", "anything", TRUE, NULL },
};

static void
test_validation(gconstpointer data)
{
	const cas_validation_t *c = data;
	gboolean obtenu = macro_type_value_valid(c->type, c->valeur);
	gchar *detail = g_strdup_printf("%s « %s » : attendu %s, obtenu %s", c->type, c->valeur, c->attendu ? "valide" : "invalide", obtenu ? "valide" : "invalide");

	test_conclude((obtenu != FALSE) == (c->attendu != FALSE), c->bug, detail);
	g_free(detail);
}

static void
test_types_flottants(void)
{
	g_assert_true(macro_type_is_float("f"));
	g_assert_true(macro_type_is_float("lf"));
	g_assert_true(macro_type_is_float("e"));
	g_assert_false(macro_type_is_float("d"));
}

/* ---- Description des arguments --------------------------------------- */

typedef struct
{
	const gchar *action;
	gint nombre;
	const gchar *infos;	/* « type:liste:libellé » séparés par « | », « - » si absent */
} cas_arguments_t;

static const cas_arguments_t cas_arguments[] =
{
	{ "%#Mode %d", 2, "l:Mode:-|d:-:-" },
	{ "%%d", 0, "" },
	{ "%&", 0, "" },
	{ "[Speed] %d", 1, "d:-:Speed" },
	{ "%d %s %f", 3, "d:-:-|s:-:-|f:-:-" },
	{ "plain", 0, "" },
};

static void
test_arguments(gconstpointer data)
{
	const cas_arguments_t *c = data;
	gint n = -1;
	macro_arg_info_t *infos = macro_get_arg_infos(c->action, &n);
	GString *texte = g_string_new(NULL);

	for(gint i = 0; i < n; i++)
		g_string_append_printf(texte, "%s%s:%s:%s", i ? "|" : "", infos[i].type, infos[i].list_name ? infos[i].list_name : "-", infos[i].label ? infos[i].label : "-");
	g_assert_cmpint(macro_count_format_args(c->action), ==, c->nombre);
	g_assert_cmpint(n, ==, c->nombre);
	g_assert_cmpstr(texte->str, ==, c->infos);
	g_string_free(texte, TRUE);
	macro_arg_infos_free(infos, n);
}

/* ---- Enregistrement --------------------------------------------------- */

#define AJOUTER_TABLE(prefixe, table, fonction) \
	for(guint i = 0; i < G_N_ELEMENTS(table); i++) \
	{ \
		gchar *chemin = g_strdup_printf("/macros_format/" prefixe "/%02u", i); \
		g_test_add_data_func(chemin, &table[i], fonction); \
		g_free(chemin); \
	}

int
main(int argc, char **argv)
{
	int resultat;

	g_test_init(&argc, &argv, NULL);
	/* strtod dépend aujourd'hui de la locale « C » imposée en douce par parsecfg (bogue M9). */
	setlocale(LC_NUMERIC, "C");
	/* Liste « Mode » pour %#Mode ; aucune liste « x » ni « o » (cas M18). */
	macro_list_add("Mode", "Run", "R");
	macro_list_add("Mode", "Stop", "S");

	AJOUTER_TABLE("echappement", cas_echappements, test_echappement);
	AJOUTER_TABLE("expansion", cas_expansions, test_expansion);
	AJOUTER_TABLE("validation", cas_validations, test_validation);
	AJOUTER_TABLE("arguments", cas_arguments, test_arguments);
	g_test_add_func("/macros_format/types-flottants", test_types_flottants);

	resultat = g_test_run();
	macro_lists_free();
	return resultat;
}
```

- [ ] **Step 2 : vérifier que le test n'est pas encore déclaré**

Run : `meson test -C build --list | grep -c test_macros_format`
Expected : `0`.

- [ ] **Step 3 : déclarer le test**

Dans `tests/meson.build`, compléter le dictionnaire :

```meson
unit_tests = {
	'test_macros_list' : [],
	'test_macros_format' : [],
}
```

- [ ] **Step 4 : lancer et contrôler les bogues connus**

Run : `meson test -C build --suite unit && meson test -C build test_macros_format -v 2>&1 | grep -c "# TODO"`
Expected : `Fail: 0` ; puis `10` lignes `# TODO` : M23 ×4 (2 expansions, 2 validations), M18 ×3, M19 ×2, M20 ×1.
Si le compte diffère : lire les lignes `not ok` (`meson test -C build test_macros_format -v`). Une ligne non marquée en échec signifie qu'une valeur attendue ne correspond pas au code actuel : vérifier la valeur par un petit essai avant de la changer. Ne jamais corriger `src/`.

- [ ] **Step 5 : commit**

```bash
git add tests/test_macros_format.c tests/meson.build
git commit -m "tests : macros_format (échappements, expansion, validation, arguments), bogues M18 M19 M20 M23 marqués

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3 : tests des débits (`baudrates.c`, `baud.c`) avec une paire pty

**Files :**
- Create : `tests/test_baudrates.c`
- Modify : `tests/meson.build`

**Interfaces :**
- Consumes : `gtkterm_core_dep`, `unit_tests` (Task 1). API testée (`src/serial.h`) : `const struct baudrate baudrate_list[]` (champs `baud`, `speed`, terminé par `{0, B0}`), `const int baudrate_count`, `speed_t find_standard_baudrate(unsigned int)`, `unsigned int speed_t_to_baud(speed_t)`, `unsigned int set_port_baudrate(unsigned int baud, int port_fd)` (renvoie 0 en cas d'erreur, sinon le débit relu).
- Produces : rien.

- [ ] **Step 1 : écrire le test**

Créer `tests/test_baudrates.c` :

```c
/* Tests des débits : table générée (baudrates.sh), correspondances débit <-> speed_t,
 * et réglage réel sur une paire pseudo-terminal (TCGETS2/BOTHER pour les débits non standard). */

#include <glib.h>
#include <limits.h>
#include <pty.h>
#include <termios.h>
#include <unistd.h>

#include "serial.h"

static void
test_table_triee(void)
{
	g_assert_cmpint(baudrate_count, >, 0);
	for(int i = 1; i < baudrate_count; i++)
		g_assert_cmpuint(baudrate_list[i - 1].baud, <, baudrate_list[i].baud);
	g_assert_cmpuint(baudrate_list[baudrate_count].baud, ==, 0);
}

static void
test_correspondances(void)
{
	for(int i = 0; i < baudrate_count; i++)
	{
		g_assert_cmpuint(find_standard_baudrate(baudrate_list[i].baud), ==, baudrate_list[i].speed);
		g_assert_cmpuint(speed_t_to_baud(baudrate_list[i].speed), ==, baudrate_list[i].baud);
	}
	g_assert_cmpuint(find_standard_baudrate(9600), ==, B9600);
	g_assert_cmpuint(find_standard_baudrate(115200), ==, B115200);
}

static void
test_hors_table(void)
{
	g_assert_cmpuint(find_standard_baudrate(0), ==, B0);
	g_assert_cmpuint(find_standard_baudrate(1), ==, B0);
	g_assert_cmpuint(find_standard_baudrate(250000), ==, B0);
	g_assert_cmpuint(find_standard_baudrate(UINT_MAX), ==, B0);
	g_assert_cmpuint(speed_t_to_baud(B0), ==, 0);
	g_assert_cmpuint(speed_t_to_baud(0xdead), ==, 0);
}

static gboolean
ouvrir_pty(int *maitre, int *esclave)
{
	return openpty(maitre, esclave, NULL, NULL, NULL) == 0;
}

static void
test_pty_debit_standard(void)
{
	int maitre, esclave;
	struct termios tio;

	if(!ouvrir_pty(&maitre, &esclave))
	{
		g_test_skip("openpty indisponible");
		return;
	}
	g_assert_cmpuint(set_port_baudrate(115200, esclave), ==, 115200);
	g_assert_cmpint(tcgetattr(esclave, &tio), ==, 0);
	g_assert_cmpuint(cfgetospeed(&tio), ==, B115200);
	close(esclave);
	close(maitre);
}

static void
test_pty_debit_non_standard(void)
{
	int maitre, esclave;

	if(!ouvrir_pty(&maitre, &esclave))
	{
		g_test_skip("openpty indisponible");
		return;
	}
	g_assert_cmpuint(set_port_baudrate(250000, esclave), ==, 250000);
	g_assert_cmpuint(set_port_baudrate(9600, esclave), ==, 9600);
	close(esclave);
	close(maitre);
}

static void
test_descripteur_invalide(void)
{
	g_assert_cmpuint(set_port_baudrate(9600, -1), ==, 0);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/baudrates/table-triee", test_table_triee);
	g_test_add_func("/baudrates/correspondances", test_correspondances);
	g_test_add_func("/baudrates/hors-table", test_hors_table);
	g_test_add_func("/baudrates/pty-debit-standard", test_pty_debit_standard);
	g_test_add_func("/baudrates/pty-debit-non-standard", test_pty_debit_non_standard);
	g_test_add_func("/baudrates/descripteur-invalide", test_descripteur_invalide);
	return g_test_run();
}
```

- [ ] **Step 2 : vérifier que le test n'est pas encore déclaré**

Run : `meson test -C build --list | grep -c test_baudrates`
Expected : `0`.

- [ ] **Step 3 : déclarer le test avec libutil (openpty)**

Dans `tests/meson.build`, au-dessus du dictionnaire, ajouter :

```meson
# openpty() : dans la libc depuis glibc 2.34, dans libutil avant.
util_dep = cc.find_library('util', required : false)
```

et compléter le dictionnaire :

```meson
unit_tests = {
	'test_macros_list' : [],
	'test_macros_format' : [],
	'test_baudrates' : [util_dep],
}
```

- [ ] **Step 4 : lancer**

Run : `meson test -C build --suite unit`
Expected : `test_baudrates OK`, `Fail: 0`.

- [ ] **Step 5 : commit**

```bash
git add tests/test_baudrates.c tests/meson.build
git commit -m "tests : table des débits et réglage sur une paire pty (débits standard et BOTHER)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4 : tests de `parsecfg` et suppressions LeakSanitizer

**Files :**
- Create : `tests/test_parsecfg.c`
- Create : `tests/lsan.supp`
- Modify : `tests/meson.build`

**Interfaces :**
- Consumes : `gtkterm_core_dep`, `unit_tests`, `test_conclude`, `test_write_file`, `test_rmdir` (Task 1). API testée (`src/parsecfg.h`) : `int cfgParse(const char *file, cfgStruct cfg[], cfgFileType type)` (nombre de sections, -1 en cas d'erreur), `int cfgDump(const char *file, cfgStruct cfg[], cfgFileType type, int max_section)` (0 si succès), `char *cfgSectionNumberToName(int)`, `int cfgAllocForNewSection(cfgStruct cfg[], const char *name)` (renvoie le **nombre** de sections ; l'indice de la nouvelle vaut ce nombre moins un), `int cfgStoreValue(cfgStruct cfg[], const char *parameter, const char *value, cfgFileType type, int section)` (0 = `CFG_NO_ERROR`). En mode `CFG_INI`, chaque entrée de `cfg[]` pointe vers un tableau indexé par section (`gchar **`, `guint *`, `gint *`, `gfloat *`).
- Produces : `tests/lsan.supp`, référencé par la variable d'environnement des tests (utile pour la Task 9).

parsecfg garde un état global (noms de sections, nombre de sections) et abandonne ses anciens tableaux à chaque lecture : chaque cas relit son propre fichier. Ces fuites sont connues (revue §5) et parsecfg disparaît au sous-projet 3 ; elles sont donc neutralisées par `tests/lsan.supp`, en se limitant aux fonctions de `parsecfg.c`.

- [ ] **Step 1 : écrire le test**

Créer `tests/test_parsecfg.c` :

```c
/* Tests de parsecfg.c (format INI multi-profils de .gtktermrc) : lecture, aller-retour,
 * locale, fichier vide, cas limites. Les fichiers sont écrits dans un répertoire temporaire. */

#include <glib.h>
#include <glib/gstdio.h>
#include <locale.h>
#include <string.h>

#include "parsecfg.h"
#include "test_util.h"

static gchar **port;
static guint *speed;
static gint *bits;
static gint *echo;
static gfloat *red;

static cfgStruct cfg[] =
{
	{"port", CFG_STRING, &port},
	{"speed", CFG_UINT, &speed},
	{"bits", CFG_INT, &bits},
	{"echo", CFG_BOOL, &echo},
	{"red", CFG_FLOAT, &red},
	{NULL, CFG_END, NULL}
};

static const gchar *fichier_deux_profils =
	"[default]\n"
	"port\t= /dev/ttyS0\n"
	"speed\t= 115200\n"
	"bits\t= 8\n"
	"echo\t= True\n"
	"red\t= 0.5\n"
	"\n"
	"[usb]\n"
	"port = \"/dev/my port\"\n"
	"speed = 9600\n"
	"bits = 7\n"
	"echo = False\n"
	"inconnue = 3\n";

static gchar *
nouveau_repertoire(void)
{
	gchar *dir = g_dir_make_tmp("gtkterm-test-XXXXXX", NULL);

	g_assert_nonnull(dir);
	return dir;
}

static void
test_lecture(void)
{
	gchar *dir = nouveau_repertoire();
	gchar *chemin = test_write_file(dir, "a.ini", fichier_deux_profils);

	g_assert_cmpint(cfgParse(chemin, cfg, CFG_INI), ==, 2);
	g_assert_cmpstr(cfgSectionNumberToName(0), ==, "default");
	g_assert_cmpstr(cfgSectionNumberToName(1), ==, "usb");
	g_assert_cmpstr(port[0], ==, "/dev/ttyS0");
	g_assert_cmpstr(port[1], ==, "/dev/my port");
	g_assert_cmpuint(speed[0], ==, 115200);
	g_assert_cmpuint(speed[1], ==, 9600);
	g_assert_cmpint(bits[0], ==, 8);
	g_assert_cmpint(bits[1], ==, 7);
	g_assert_cmpint(echo[0], ==, 1);
	g_assert_cmpint(echo[1], ==, 0);
	g_assert_cmpfloat_with_epsilon(red[0], 0.5, 1e-6);

	g_free(chemin);
	test_rmdir(dir);
	g_free(dir);
}

static void
test_aller_retour(void)
{
	gchar *dir = nouveau_repertoire();
	gchar *source = test_write_file(dir, "a.ini", fichier_deux_profils);
	gchar *copie = g_build_filename(dir, "b.ini", NULL);
	gint idx;

	g_assert_cmpint(cfgParse(source, cfg, CFG_INI), ==, 2);
	idx = cfgAllocForNewSection(cfg, "nouveau") - 1;
	g_assert_cmpint(idx, ==, 2);
	g_assert_cmpint(cfgStoreValue(cfg, "port", "/dev/x y", CFG_INI, idx), ==, CFG_NO_ERROR);
	g_assert_cmpint(cfgStoreValue(cfg, "speed", "57600", CFG_INI, idx), ==, CFG_NO_ERROR);
	g_assert_cmpint(cfgStoreValue(cfg, "bits", "5", CFG_INI, idx), ==, CFG_NO_ERROR);
	g_assert_cmpint(cfgStoreValue(cfg, "echo", "yes", CFG_INI, idx), ==, CFG_NO_ERROR);
	g_assert_cmpint(cfgStoreValue(cfg, "red", "0.25", CFG_INI, idx), ==, CFG_NO_ERROR);
	g_assert_cmpint(cfgDump(copie, cfg, CFG_INI, idx + 1), ==, 0);

	g_assert_cmpint(cfgParse(copie, cfg, CFG_INI), ==, 3);
	g_assert_cmpstr(cfgSectionNumberToName(2), ==, "nouveau");
	g_assert_cmpstr(port[1], ==, "/dev/my port");
	g_assert_cmpint(echo[1], ==, 0);
	g_assert_cmpstr(port[2], ==, "/dev/x y");
	g_assert_cmpuint(speed[2], ==, 57600);
	g_assert_cmpint(bits[2], ==, 5);
	g_assert_cmpint(echo[2], ==, 1);
	g_assert_cmpfloat_with_epsilon(red[2], 0.25, 1e-6);

	g_free(copie);
	g_free(source);
	test_rmdir(dir);
	g_free(dir);
}

static void
test_booleen_non_renseigne(void)
{
	/* Comportement actuel, à décider : un booléen jamais renseigné vaut -1 et s'écrit « True ». */
	gchar *dir = nouveau_repertoire();
	gchar *source = test_write_file(dir, "a.ini", fichier_deux_profils);
	gchar *copie = g_build_filename(dir, "b.ini", NULL);
	gint idx;

	g_assert_cmpint(cfgParse(source, cfg, CFG_INI), ==, 2);
	idx = cfgAllocForNewSection(cfg, "nouveau") - 1;
	g_assert_cmpint(cfgStoreValue(cfg, "port", "/dev/ttyUSB0", CFG_INI, idx), ==, CFG_NO_ERROR);
	g_assert_cmpint(cfgDump(copie, cfg, CFG_INI, idx + 1), ==, 0);
	g_assert_cmpint(cfgParse(copie, cfg, CFG_INI), ==, 3);
	g_assert_cmpint(echo[idx], ==, 1);
	g_assert_cmpuint(speed[idx], ==, 0);

	g_free(copie);
	g_free(source);
	test_rmdir(dir);
	g_free(dir);
}

static void
test_locale_preservee(void)
{
	gchar *dir;
	gchar *source;
	gchar *copie;
	const gchar *apres;
	gchar *detail;

	if(setlocale(LC_ALL, "C.UTF-8") == NULL)
	{
		g_test_skip("locale C.UTF-8 indisponible");
		return;
	}
	dir = nouveau_repertoire();
	source = test_write_file(dir, "a.ini", fichier_deux_profils);
	copie = g_build_filename(dir, "b.ini", NULL);
	g_assert_cmpint(cfgParse(source, cfg, CFG_INI), ==, 2);
	g_assert_cmpint(cfgDump(copie, cfg, CFG_INI, 2), ==, 0);

	apres = setlocale(LC_ALL, NULL);
	detail = g_strdup_printf("locale après cfgParse/cfgDump : attendu C.UTF-8, obtenu %s", apres);
	test_conclude(g_strcmp0(apres, "C.UTF-8") == 0, "M9", detail);

	g_free(detail);
	setlocale(LC_ALL, "C");
	g_free(copie);
	g_free(source);
	test_rmdir(dir);
	g_free(dir);
}

#define VAR_FICHIER_VIDE "GTKTERM_TEST_FICHIER_VIDE"

static void
test_fichier_vide_puis_ecriture(void)
{
	gchar *dir;
	gchar *chemin;

	if(g_test_subprocess())
	{
		/* Processus neuf : les tableaux de cfg[] sont encore NULL, comme au premier démarrage de GTKTerm. */
		g_assert_cmpint(cfgParse(g_getenv(VAR_FICHIER_VIDE), cfg, CFG_INI), ==, 0);
		cfgStoreValue(cfg, "port", "/dev/ttyS0", CFG_INI, 0);
		return;
	}
	dir = nouveau_repertoire();
	chemin = test_write_file(dir, "vide.ini", "");
	g_setenv(VAR_FICHIER_VIDE, chemin, TRUE);
	g_test_trap_subprocess(NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
	test_conclude(g_test_trap_has_passed(), "C7", "cfgStoreValue après la lecture d'un fichier vide ne doit pas planter");
	g_unsetenv(VAR_FICHIER_VIDE);
	g_free(chemin);
	test_rmdir(dir);
	g_free(dir);
}

static void
test_sections_differant_par_la_casse(void)
{
	/* Comportement actuel conservé : parsecfg refuse deux sections qui ne diffèrent que par la casse.
	 * Le défaut C6 vient de term_config.c (comparaison par strcmp) ; son test arrive au sous-projet 1. */
	gchar *dir = nouveau_repertoire();
	gchar *chemin = test_write_file(dir, "c.ini", "[default]\nport = /dev/a\n[Default]\nport = /dev/b\n");

	g_assert_cmpint(cfgParse(chemin, cfg, CFG_INI), ==, -1);
	g_free(chemin);
	test_rmdir(dir);
	g_free(dir);
}

static void
test_espace_sans_guillemets(void)
{
	/* Comportement actuel conservé : une valeur non entre guillemets contenant une espace est refusée.
	 * C'est la cause de C5 : GKeyFile écrit [session] script_path sans guillemets, et term_config.c
	 * oublie de retirer ce groupe avant certains appels à cfgParse. Test de C5 : sous-projet 1. */
	gchar *dir = nouveau_repertoire();
	gchar *chemin = test_write_file(dir, "d.ini", "[session]\nscript_path=/home/x/my script.lua\n");

	g_assert_cmpint(cfgParse(chemin, cfg, CFG_INI), ==, -1);
	g_free(chemin);
	test_rmdir(dir);
	g_free(dir);
}

static void
test_fichier_absent(void)
{
	gchar *dir = nouveau_repertoire();
	gchar *chemin = g_build_filename(dir, "absent.ini", NULL);

	g_assert_cmpint(cfgParse(chemin, cfg, CFG_INI), ==, -1);
	g_free(chemin);
	test_rmdir(dir);
	g_free(dir);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/parsecfg/lecture", test_lecture);
	g_test_add_func("/parsecfg/aller-retour", test_aller_retour);
	g_test_add_func("/parsecfg/booleen-non-renseigne", test_booleen_non_renseigne);
	g_test_add_func("/parsecfg/locale-preservee", test_locale_preservee);
	g_test_add_func("/parsecfg/fichier-vide-puis-ecriture", test_fichier_vide_puis_ecriture);
	g_test_add_func("/parsecfg/sections-differant-par-la-casse", test_sections_differant_par_la_casse);
	g_test_add_func("/parsecfg/espace-sans-guillemets", test_espace_sans_guillemets);
	g_test_add_func("/parsecfg/fichier-absent", test_fichier_absent);
	return g_test_run();
}
```

- [ ] **Step 2 : vérifier que le test n'est pas encore déclaré**

Run : `meson test -C build --list | grep -c test_parsecfg`
Expected : `0`.

- [ ] **Step 3 : créer `tests/lsan.supp`**

```
# Fuites connues de parsecfg.c (revue §5 : chaque cfgParse abandonne les tableaux précédents).
# parsecfg est remplacé par GKeyFile au sous-projet 3 : supprimer ce fichier à ce moment-là.
# Ne jamais ajouter ici une fonction qui n'est pas dans parsecfg.c.
leak:alloc_for_new_section
leak:store_value
leak:parse_word
leak:parse_ini
leak:cfgAllocForNewSection
```

- [ ] **Step 4 : déclarer le test et l'environnement commun**

Dans `tests/meson.build`, compléter le dictionnaire, puis remplacer la boucle `foreach` par la version qui passe l'environnement LeakSanitizer à tous les tests :

```meson
unit_tests = {
	'test_macros_list' : [],
	'test_macros_format' : [],
	'test_baudrates' : [util_dep],
	'test_parsecfg' : [],
}

# Sans effet hors construction avec -Db_sanitize=address ; voir tests/README.md.
test_env = environment()
test_env.set('LSAN_OPTIONS', 'suppressions=' + meson.current_source_dir() / 'lsan.supp')
# UBSan ne fait qu'afficher par défaut : on veut qu'un comportement indéfini fasse échouer le test.
test_env.set('UBSAN_OPTIONS', 'halt_on_error=1:print_stacktrace=1')

foreach name, extra_deps : unit_tests
	exe = executable(name, name + '.c',
		dependencies : [gtkterm_core_dep] + extra_deps,
		build_by_default : false)
	test(name, exe, args : ['--tap'], protocol : 'tap', suite : 'unit', env : test_env)
endforeach
```

- [ ] **Step 5 : lancer et contrôler les bogues connus**

Run : `meson test -C build --suite unit && meson test -C build test_parsecfg -v 2>&1 | grep "# TODO"`
Expected : `Fail: 0` ; deux lignes `# TODO`, l'une avec `bogue C7`, l'autre avec `bogue M9`. Les messages d'erreur de parsecfg sur stderr (« Syntax error », « The section name is already used », « Cannot open configuration file ») sont attendus.

- [ ] **Step 6 : commit**

```bash
git add tests/test_parsecfg.c tests/lsan.supp tests/meson.build
git commit -m "tests : parsecfg (lecture, aller-retour, locale, fichier vide), bogues C7 et M9 marqués

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5 : vérification de syntaxe des exemples Lua

**Files :**
- Modify : `tests/meson.build`

**Interfaces :**
- Consumes : `tests/meson.build` (Task 1).
- Produces : test meson `lua-syntax` (suite `lint`).

- [ ] **Step 1 : constater l'absence du test**

Run : `meson test -C build --list | grep -c lua-syntax`
Expected : `0`.

- [ ] **Step 2 : déclarer le test**

Ajouter à la fin de `tests/meson.build` :

```meson
# Syntaxe des exemples Lua (luac -p : analyse seule, aucun fichier produit).
luac = find_program('luac5.4', 'luac', required : false)
if luac.found()
	lua_examples = files(
		'../scripts/01_hello.lua',
		'../scripts/02_at_commands.lua',
		'../scripts/03_wait_for_demo.lua',
		'../scripts/04_csv_logger.lua',
		'../scripts/05_macros_demo.lua',
		'../scripts/06_hex_protocol.lua',
		'../scripts/07_tcp_client.lua',
		'../scripts/08_stress_loop.lua',
		'../scripts/09_multi_port.lua',
		'../scripts/10_firmware_update.lua',
	)
	test('lua-syntax', luac, args : ['-p'] + lua_examples, suite : 'lint')
endif
```

- [ ] **Step 3 : lancer, puis vérifier qu'une erreur de syntaxe est bien détectée**

Run : `meson test -C build lua-syntax`
Expected : `lua-syntax OK`.

Puis, pour vérifier que le test détecte bien une erreur (sans rien commiter) :
```bash
printf 'if then\n' >> scripts/01_hello.lua && meson test -C build lua-syntax; git checkout scripts/01_hello.lua
```
Expected : `lua-syntax FAIL`, puis le fichier est restauré (`git status --short scripts/` vide).

- [ ] **Step 4 : commit**

```bash
git add tests/meson.build
git commit -m "tests : vérification de syntaxe des exemples Lua (luac -p)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6 : révision git par `vcs_tag()` et suppression de `RELEASE_DATE`

**Files :**
- Create : `src/git_revision.h.in`
- Modify : `meson.build` (racine) : retirer le calcul `run_command` de `GIT_REVISION` et `RELEASE_DATE`
- Modify : `src/meson.build` : `vcs_tag`, ajout aux sources de l'exécutable
- Modify : `src/update.c:1-10` (inclusion)
- Modify : `src/interface.c:83` (inclusion) et `src/interface.c:888` (commentaire de « À propos »)

**Interfaces :**
- Consumes : `src/meson.build` de la Task 1.
- Produces : en-tête généré `git_revision.h` définissant `GIT_REVISION` (chaîne : SHA complet, ou `"unknown"`). `GIT_REVISION` et `RELEASE_DATE` disparaissent de `config.h`.

- [ ] **Step 1 : constater le défaut (révision figée)**

Run :
```bash
grep GIT_REVISION build/config.h
git log -1 --format=%H
```
Expected : la valeur de `config.h` (`0a013c2…`, figée à la dernière configuration) diffère du `HEAD` courant de la branche : c'est le bogue M33.

- [ ] **Step 2 : créer `src/git_revision.h.in`**

```c
/* Généré par vcs_tag() (src/meson.build) à chaque construction : ne pas modifier git_revision.h. */
#ifndef GIT_REVISION_H_
#define GIT_REVISION_H_

#define GIT_REVISION "@GIT_REVISION@"

#endif
```

- [ ] **Step 3 : modifier le `meson.build` racine**

Supprimer la ligne `conf.set_quoted('RELEASE_DATE', 'May 2024')` et tout le bloc suivant (du commentaire `# Bake the current git commit…` jusqu'à `conf.set_quoted('GIT_REVISION', git_revision)` inclus).

- [ ] **Step 4 : ajouter `vcs_tag` dans `src/meson.build`**

Juste après la définition de `gtkterm_core_dep`, ajouter :

```meson
# Révision git recalculée à chaque construction (le fichier n'est réécrit que si elle change).
# safe.directory=* : l'installation (pkexec/sudo ninja install) relance ceci en root dans un dépôt
# appartenant à l'utilisateur ; sans cette option, git refuse et la révision deviendrait « unknown ».
git_revision_h = vcs_tag(
	command : ['git', '-c', 'safe.directory=*', 'rev-parse', 'HEAD'],
	fallback : 'unknown',
	input : 'git_revision.h.in',
	output : 'git_revision.h',
	replace_string : '@GIT_REVISION@'
)
```

et ajouter `git_revision_h,` dans la liste `sources` de l'exécutable, juste avant `gresources`.

- [ ] **Step 5 : inclure l'en-tête là où `GIT_REVISION` sert**

Dans `src/update.c`, après `#include "term_config.h"` (ligne 10), ajouter :

```c
#include "git_revision.h"
```

Dans `src/interface.c`, après `#include "update.h"` (ligne 83), ajouter :

```c
#include "git_revision.h"
```

Dans `src/interface.c`, remplacer la ligne :

```c
	g_snprintf(comments, sizeof(comments), "%s\nGit: %s\n\n%s", RELEASE_DATE, GIT_REVISION, comments_program);
```

par :

```c
	g_snprintf(comments, sizeof(comments), "Git: %s\n\n%s", GIT_REVISION, comments_program);
```

(La version est déjà affichée par `gtk_about_dialog_set_version`.)

- [ ] **Step 6 : construire et vérifier la révision embarquée**

Run :
```bash
meson setup --reconfigure build >/dev/null && ninja -C build 2>&1 | grep -E "error|Error" ; grep -c "GIT_REVISION\|RELEASE_DATE" build/config.h; cat build/src/git_revision.h | grep define; git rev-parse HEAD
```
Expected : aucune erreur ; `0` (plus rien dans `config.h`) ; la ligne `#define GIT_REVISION "<sha>"` porte le même SHA que `git rev-parse HEAD`.

- [ ] **Step 7 : vérifier la mise à jour sans reconfiguration, et le cas root (Review Focus 1)**

Run :
```bash
git commit --allow-empty -q -m "essai vcs_tag" && ninja -C build >/dev/null && grep define build/src/git_revision.h && git rev-parse HEAD
GIT_TEST_ASSUME_DIFFERENT_OWNER=1 ninja -C build >/dev/null && grep define build/src/git_revision.h
git reset -q --soft HEAD~1 && ninja -C build >/dev/null
```
Expected : après le commit vide, un simple `ninja` met à jour le SHA (égal au nouveau `HEAD`) ; avec `GIT_TEST_ASSUME_DIFFERENT_OWNER=1` (simulation d'un dépôt appartenant à un autre utilisateur), le SHA reste le vrai et non `unknown` ; le commit d'essai est ensuite annulé.

- [ ] **Step 8 : vérifier une construction sans `.git` (Review Focus 2)**

Run :
```bash
A=/tmp/claude-1315401155/-home-METRO-gabriel-git-gtkterm/e7fc7d21-128e-4797-b30d-8c870b9c4643/scratchpad/archive
rm -rf $A && mkdir -p $A && git archive HEAD | tar -x -C $A && cp src/git_revision.h.in $A/src/ && cp meson.build $A/ && cp src/meson.build $A/src/ && cp src/update.c src/interface.c $A/src/
cd $A && meson setup b >/dev/null && ninja -C b 2>&1 | grep -cE "error" ; grep define b/src/git_revision.h; cd -
```
Expected : `0` erreur ; `#define GIT_REVISION "unknown"`. (La copie des fichiers modifiés remplace un commit, l'archive partant de `HEAD`.)

- [ ] **Step 9 : contrôler l'exécutable et commiter**

Run : `./build/src/gtkterm --help >/dev/null 2>&1; echo "rc=$?"`
Expected : `rc=1`.

```bash
git add src/git_revision.h.in meson.build src/meson.build src/update.c src/interface.c
git commit -m "build : révision git par vcs_tag (plus de révision figée à la configuration), suppression de RELEASE_DATE

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7 : nettoyage du dépôt

**Files :**
- Delete : `.circleci/config.yml`, `screenshot.png`, `src/auto_config.h`, `graphify-out/` (suivi et disque)
- Move : `Capture1.png` … `Capture6.png` → `docs/images/`
- Modify : `README.md:135-150`, `.gitignore`, `po/POTFILES.in`, `src/cmdline.c:27`, `src/interface.c:80`, `src/gtkterm.c:31`

**Interfaces :**
- Consumes : rien.
- Produces : rien.

- [ ] **Step 1 : constater l'état initial**

Run : `git ls-files .circleci graphify-out src/auto_config.h screenshot.png 'Capture*.png' | wc -l`
Expected : `134` (1 + 125 + 1 + 1 + 6).

- [ ] **Step 2 : supprimer CircleCI, `screenshot.png`, `graphify-out/`**

```bash
git rm -q -r .circleci screenshot.png
git rm -q -r --cached graphify-out
rm -rf graphify-out
```

- [ ] **Step 3 : déplacer les captures et corriger le README**

```bash
mkdir -p docs/images
git mv Capture1.png Capture2.png Capture3.png Capture4.png Capture5.png Capture6.png docs/images/
sed -i 's|<img src="Capture\([1-6]\)\.png"|<img src="docs/images/Capture\1.png"|' README.md
grep -n 'img src' README.md
```
Expected : les six lignes `<img src="docs/images/CaptureN.png" …>`.

- [ ] **Step 4 : supprimer `auto_config.h`**

Les trois fichiers qui l'incluent incluent déjà `config.h` (`cmdline.c:30`, `interface.c:57`, `gtkterm.c:36`) : il suffit de retirer l'inclusion.

```bash
sed -i '/#include "auto_config.h"/d' src/cmdline.c src/interface.c src/gtkterm.c
git rm -q src/auto_config.h
grep -rn auto_config src/ ; ninja -C build 2>&1 | grep -cE "error"
```
Expected : aucune occurrence, `0` erreur.

- [ ] **Step 5 : `.gitignore`**

Remplacer le contenu de `.gitignore` par :

```
*~
build*/
graphify-out/
```

- [ ] **Step 6 : compléter `po/POTFILES.in`**

Remplacer la liste des sources (après le commentaire `# Package source files`) par la liste triée complète :

```
src/buffer.c
src/cmdline.c
src/device_monitor.c
src/files.c
src/gtkterm.c
src/i18n.c
src/interface.c
src/logging.c
src/macro_panel.c
src/macros.c
src/macros_format.c
src/macros_list.c
src/parsecfg.c
src/script_panel.c
src/search.c
src/serial.c
src/term_config.c
src/terminal_display.c
src/transport.c
src/update.c
src/user_signals.c
```

Run : `grep '^src/' po/POTFILES.in | while read f; do test -f "$f" || echo "absent: $f"; done`
Expected : aucune sortie.

- [ ] **Step 7 : vérifier la construction et les tests, puis commiter**

Run : `ninja -C build 2>&1 | grep -cE "error"; meson test -C build --suite unit --suite lint | grep -E "^(Ok|Fail)"`
Expected : `0` ; `Ok: 5`, `Fail: 0`.

```bash
git add -A .gitignore README.md docs/images po/POTFILES.in src/cmdline.c src/interface.c src/gtkterm.c
git status --short
git commit -m "dépôt : suppression de CircleCI, graphify-out, screenshot.png et auto_config.h ; captures dans docs/images ; POTFILES.in complet

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
Expected avant le commit : `git status --short` ne liste que des suppressions (`D`), des renommages (`R`) et les fichiers modifiés ci-dessus ; aucun fichier non suivi (`??`).

---

### Task 8 : documentation des tests et mise à jour de `CLAUDE.md`

**Files :**
- Create : `tests/README.md`
- Modify : `CLAUDE.md` (section Build / Run, paragraphe des tests, paragraphe Config)

**Interfaces :**
- Consumes : tout ce qui précède (noms des suites, `lsan.supp`, `test_util.h`).
- Produces : rien.

- [ ] **Step 1 : créer `tests/README.md`**

````markdown
# Tests de GTKTerm

Tests unitaires GLib GTest des modules sans GTK (bibliothèque `gtkterm_core`, voir `src/meson.build`), plus une vérification de syntaxe des exemples Lua.

## Lancer

```sh
meson test -C build                 # tout
meson test -C build --suite unit    # tests unitaires seulement
meson test -C build --suite lint    # syntaxe des exemples Lua (luac -p)
meson test -C build test_parsecfg -v   # un test, sortie TAP détaillée
```

Les exécutables de test ne sont pas construits par `ninja -C build` : `meson test` les construit à la demande.

## Bogues connus (`# TODO`)

Un cas qui correspond à un défaut de la revue (`docs/superpowers/reviews/2026-10-06-revue-code.md`, identifiants C1…, M1…) est marqué par son identifiant via `test_conclude()` (`tests/test_util.h`) :

- tant que le défaut existe, le cas est « incomplete » : il sort en `not ok … # TODO bogue M18 — …` et **ne fait pas échouer** la suite ;
- si le comportement devient conforme alors que le marquage est encore là, le cas **échoue** (« semble corrigé : retirer le marquage »).

Le résumé de meson ne compte pas ces cas ; pour en voir la liste :

```sh
meson test -C build -v 2>&1 | grep '# TODO'
```

Corriger un bogue = rendre le cas conforme **et** remplacer son identifiant par `NULL` dans la table du test.

## Écrire un test

- Un exécutable par module : `tests/test_<module>.c`, déclaré dans le dictionnaire `unit_tests` de `tests/meson.build`.
- Un cas par fonction de test ; pour une table de cas, une entrée par cas avec `g_test_add_data_func`. Ne jamais appeler `test_conclude()` deux fois dans un même cas.
- Un cas qui peut planter le processus s'exécute dans un sous-processus (`g_test_trap_subprocess`), voir `test_fichier_vide_puis_ecriture` dans `test_parsecfg.c`.
- Fichiers temporaires : `g_dir_make_tmp`, `test_write_file()`, `test_rmdir()`.
- Style : celui de `.clang-format` (tabulations, Allman, `if(`).

## Sanitizers et valgrind

```sh
meson setup build-asan -Db_sanitize=address,undefined
meson test -C build-asan
```

Les fuites connues de `parsecfg.c` sont neutralisées par `tests/lsan.supp` (variable `LSAN_OPTIONS` posée par `tests/meson.build`) ; n'y ajouter que des fonctions de `parsecfg.c`.

valgrind, pour les erreurs mémoire (les fuites sont vérifiées par ASan) :

```sh
meson test -C build --suite unit --wrap='valgrind --error-exitcode=1 --leak-check=no'
```
````

- [ ] **Step 2 : mettre à jour `CLAUDE.md`, section Build / Run**

Dans le bloc de commandes, après la ligne `./build/src/gtkterm      # run`, ajouter :

```sh
meson test -C build      # tests unitaires (GLib GTest) + syntaxe des exemples Lua
```

Remplacer le paragraphe qui commence par `**No test suite exists.**` (jusqu'à `and by running the Lua examples in \`scripts/\`.`) par :

```markdown
**Tests:** `meson test -C build` runs GLib GTest unit tests (`tests/`) against `gtkterm_core`, a static library of the GTK-free modules (`macros_format.c`, `macros_list.c`, `baudrates.c`, `baud.c`, `parsecfg.c`, `i18n.c`) linked by both the app and the tests; plus `luac -p` on `scripts/*.lua`. Test executables are only built by `meson test`. Cases tied to a known bug from `docs/superpowers/reviews/2026-10-06-revue-code.md` are marked with its ID and reported as `# TODO` (see `meson test -C build -v`) without failing the suite; a marked case that starts passing fails, so the marker gets removed with the fix. See `tests/README.md` (sanitizers, valgrind, how to add a test). There is no remote CI: tests run locally. GUI behavior still needs a manual run of the app.
```

- [ ] **Step 3 : mettre à jour `CLAUDE.md`, paragraphe Config**

Remplacer :

```markdown
**Config (`term_config.c` + `parsecfg.c`):** the port/window config is `.gtktermrc` under `$XDG_CONFIG_HOME` (migrated from `$HOME`). `parsecfg.c` is the generic parser; `term_config.c` extracts macro/list keyfile sections out before parsing and re-appends them after. Script-editor colors are a separate file: `~/.config/gtkterm_script_colors.ini`.
```

par :

```markdown
**Config (`term_config.c` + `parsecfg.c`):** the port/window config is `.gtktermrc` under `$XDG_CONFIG_HOME` (migrated from `$HOME`). Profiles are INI sections handled by `parsecfg.c` (old third-party parser). The same file also holds GKeyFile groups `[window]`, `[colors]`, `[layout]`, `[session]` (window state in `term_config.c`; script-editor colors, layout and last script in `script_panel.c`); `term_config.c` strips those groups before calling parsecfg and restores them after (`cfg_strip_kf_sections` / `cfg_restore_kf_sections`). Two mechanisms on one file is fragile (review defects C5, C13); sub-project 3 replaces parsecfg with a single GKeyFile store.
```

- [ ] **Step 4 : mettre à jour `CLAUDE.md`, mention de la révision git**

Dans le paragraphe **Self-update**, remplacer `because \`meson.build\` bakes \`git rev-parse HEAD\` into \`GIT_REVISION\`` par `because \`src/meson.build\` generates \`git_revision.h\` (\`GIT_REVISION\`) with \`vcs_tag()\` on every build (with \`safe.directory=*\` so a root \`ninja install\` keeps the real SHA)`.

Run : `grep -n "No test suite\|circleci\|gtkterm_script_colors\|bakes" CLAUDE.md`
Expected : aucune sortie.

- [ ] **Step 5 : commit**

```bash
git add tests/README.md CLAUDE.md
git commit -m "docs : guide des tests (bogues connus, sanitizers, valgrind) et CLAUDE.md à jour

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9 : vérification d'acceptation (ASan/UBSan, valgrind, configuration neuve, essai manuel)

**Files :**
- Modify (seulement si un problème est révélé) : `docs/superpowers/reviews/2026-10-06-revue-code.md`, tests concernés.

**Interfaces :**
- Consumes : tout ce qui précède.
- Produces : rien.

- [ ] **Step 1 : configuration neuve et suite complète (critères 1 et 3)**

Run :
```bash
N=/tmp/claude-1315401155/-home-METRO-gabriel-git-gtkterm/e7fc7d21-128e-4797-b30d-8c870b9c4643/scratchpad/build-accept
rm -rf $N && meson setup $N >/dev/null && ninja -C $N 2>&1 | grep -cE "error" ; meson test -C $N | grep -E "^(Ok|Expected Fail|Fail|Skipped)"
```
Expected : `0` erreur ; `Ok` ≥ 5 (4 tests unitaires + `lua-syntax`, plus `desktop file test` si `desktop-file-validate` est installé), `Fail: 0`.

- [ ] **Step 2 : liste des bogues connus (critère 4)**

Run : `meson test -C build -v 2>&1 | grep -o "bogue [A-Z][0-9]*" | sort | uniq -c`
Expected : exactement `C7` ×1, `M9` ×1, `M18` ×3, `M19` ×2, `M20` ×1, `M23` ×4.

- [ ] **Step 3 : ASan + UBSan (critère 5)**

Run :
```bash
S=/tmp/claude-1315401155/-home-METRO-gabriel-git-gtkterm/e7fc7d21-128e-4797-b30d-8c870b9c4643/scratchpad/build-asan
rm -rf $S && meson setup $S -Db_sanitize=address,undefined >/dev/null && ninja -C $S >/dev/null 2>&1; meson test -C $S --suite unit | grep -E "^(Ok|Fail)"
```
Expected : `Fail: 0`.
Si un test échoue : lire `$S/meson-logs/testlog.txt`.
- Une fuite **dans `parsecfg.c`** non couverte : ajouter la fonction de `parsecfg.c` concernée à `tests/lsan.supp`.
- Tout autre rapport ASan/UBSan dans un module du cœur : l'ajouter à la revue (§3 ou §4, nouvel identifiant à la suite des existants, ex. `M41`) et marquer le cas concerné avec cet identifiant s'il ne se corrige pas sans changer de comportement.
- Ne jamais corriger `src/` dans ce plan.
Relancer jusqu'à `Fail: 0`.

- [ ] **Step 4 : valgrind (erreurs mémoire)**

Run : `meson test -C build --suite unit --wrap='valgrind --error-exitcode=1 --leak-check=no' | grep -E "^(Ok|Fail)"`
Expected : `Fail: 0` (le cas C7 plante volontairement dans son sous-processus ; il reste « incomplete »). Même traitement qu'au Step 3 si un rapport apparaît.

- [ ] **Step 5 : état du dépôt (critère 6)**

Run : `git ls-files graphify-out .circleci src/auto_config.h screenshot.png | wc -l; ls graphify-out 2>&1 | head -1; git status --short`
Expected : `0` ; `ls: impossible d'accéder à 'graphify-out'…` ; arbre propre (sauf corrections éventuelles des Steps 3-4).

- [ ] **Step 6 : commiter les éventuelles corrections**

Seulement si les Steps 3-4 ont modifié des fichiers :
```bash
git add tests/lsan.supp docs/superpowers/reviews/2026-10-06-revue-code.md tests/
git commit -m "tests : constats ASan/valgrind consignés dans la revue

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 7 : essai manuel par l'utilisateur (critères 1, 2 et 7, Review Focus 4)**

Ne pas lancer l'application soi-même : elle ouvre une fenêtre sur le bureau de l'utilisateur et peut ouvrir un port réel. Demander à l'utilisateur de lancer `./build/src/gtkterm` et de vérifier :
1. la fenêtre s'ouvre, le port configuré s'ouvre comme avant ;
2. « À propos » affiche la version 1.3.2 et la ligne `Git: <sha>` égale à `git rev-parse HEAD` (sans ligne « May 2024 ») ;
3. macros, panneau de script et changement de vue ASCII/hexa fonctionnent comme avant.
Consigner sa réponse ; tout écart est un défaut à traiter avant la fusion.
