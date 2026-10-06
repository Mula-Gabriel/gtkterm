# Sous-projet 0 — Socle de tests et hygiène du dépôt

- Date : 2026-10-06
- Branche : `feature/socle-tests` (fusion dans `master` selon la règle habituelle : écrasement en commits thématiques, puis suppression de la branche)
- Entrée : revue de code `docs/superpowers/reviews/2026-10-06-revue-code.md`

## Contexte : le chantier complet

La revue a conduit à une refonte en sept sous-projets, traités dans cet ordre, chacun avec son propre cycle spec → plan → réalisation et sa propre branche :

| # | Sous-projet | Branche prévue |
|---|---|---|
| **0** | **Socle de tests et hygiène (ce document)** | `feature/socle-tests` |
| 1 | Correctifs critiques (C1–C13 + majeurs simples), chacun avec test de non-régression | `fix/correctifs-critiques` |
| 2 | Cœur sans GTK : transport par rappels, termios pur, perte de liaison unifiée, tampon brut, envoi séparé de l'écho | `feature/refonte-transport` |
| 3 | Configuration : GKeyFile unique + migration, modèle pur, dialogues découpés | `feature/refonte-config` |
| 4 | Macros : envoi unique, polling par identifiant, analyseur à jetons, stockage GKeyFile | `feature/refonte-macros` |
| 5 | Lua : contexte moteur, ports sur le fil Lua via le transport, vrai arrêt, exécuteur de tests sans interface | `feature/refonte-lua` |
| 6 | Interface : découpage d'`interface.c`, `GActionMap` réel, clang-format des fichiers refondus | `feature/refonte-interface` |

Le sous-projet 0 pose le filet de sécurité sur lequel reposent tous les autres.

## Objectif

1. Pouvoir lancer `meson test -C build` et obtenir une suite de tests automatisés couvrant les modules déjà indépendants de GTK.
2. Rendre visibles, dans la sortie des tests, les bogues connus de ces modules, pour que le sous-projet 1 les corrige un par un.
3. Mettre la construction et le dépôt au propre, sans changer le comportement du logiciel.

## Hors périmètre

- Aucun correctif de bogue du logiciel, à une exception près : `GIT_REVISION` (voir §1.3). Les bogues que les tests mettent en évidence sont seulement signalés.
- Aucune refonte, aucun reformatage de fichier existant.
- Pas d'intégration continue distante : les tests se lancent en local. Le `.circleci/` hérité du projet d'origine est supprimé.
- Pas de tests d'interface graphique, ni de tests d'intégration du transport ou de Lua : ils arrivent avec les sous-projets 2 et 5, quand les points d'injection existeront.

## 1. Construction

### 1.1 Bibliothèque cœur

`src/meson.build` déclare une `static_library('gtkterm_core', …)` avec les modules qui ne dépendent que de GLib ou de termios :

- `macros_format.c`, `macros_list.c`
- `baudrates.c` (avec l'en-tête généré `baudrates.h`), `baud.c`
- `parsecfg.c`, `i18n.c`

L'exécutable `gtkterm` ne compile plus ces fichiers lui-même : il se lie à `gtkterm_core`. Une `declare_dependency` (`gtkterm_core_dep`) expose la bibliothèque, ses en-têtes et l'en-tête généré aux tests.
Les fichiers de la bibliothèque ne doivent inclure aucun en-tête GTK, VTE ou interface. Si l'un d'eux en inclut un sans en avoir besoin, l'inclusion est retirée. S'il en a vraiment besoin, il reste dans l'exécutable et la spec est amendée.

### 1.2 Options de compilation

Dans `project()` :
- `default_options : ['c_std=gnu11', 'warning_level=2']`.
- `add_project_arguments('-Wno-unused-parameter', '-Werror=format-security', '-Werror=implicit-function-declaration', language : 'c')`.

Les avertissements réels qui restent (`sign-compare`, `cast-function-type`, une quinzaine) ne sont pas corrigés ici : ils relèvent du sous-projet 1. L'un d'eux est le bogue M35 de `files.c`.

### 1.3 Révision git et date de publication

- `GIT_REVISION` n'est plus calculée par `run_command` à la configuration : avec cette méthode, la valeur restait figée après de nouveaux commits suivis d'un simple `ninja`, et la mise à jour était proposée en boucle (bogue M33, première moitié).
- À la place : `vcs_tag(command : ['git', '-c', 'safe.directory=*', 'rev-parse', 'HEAD'], fallback : 'unknown', input : 'git_revision.h.in', output : 'git_revision.h', replace_string : '@GIT_REVISION@')`, régénéré à chaque construction (le fichier n'est réécrit que si la révision change).
- `safe.directory=*` est indispensable : l'installation (`pkexec ninja install` dans `gtkterm-update.sh`, ou `sudo ninja install`) relance `vcs_tag` en root dans un dépôt appartenant à l'utilisateur ; sans cette option, git refuse (« dubious ownership »), la révision devient `unknown`, et le binaire installé ne sait plus se comparer au dépôt distant. Vérifiable sans root avec `GIT_TEST_ASSUME_DIFFERENT_OWNER=1`.
- `update.c` et `interface.c` incluent `git_revision.h`. `GIT_REVISION` disparaît de `config.h`.
- `RELEASE_DATE` (figée à « May 2024 ») est supprimée de `meson.build`. La boîte « À propos » affiche déjà la version (`gtk_about_dialog_set_version`) ; la ligne de commentaire ne garde que `Git: <révision>`.
- La seconde moitié de M33 (le test vérifie que la révision est « différente » et non « plus récente ») reste au sous-projet 1.

## 2. Suite de tests

### 2.1 Organisation

- `tests/meson.build`, inclus par le `meson.build` racine.
- Un exécutable GTest par module : `test_macros_format`, `test_macros_list`, `test_baudrates`, `test_parsecfg`. Chacun se lie à `gtkterm_core_dep` et à `glib-2.0`, et est déclaré avec `test('<nom>', exe, args : ['--tap'], protocol : 'tap', suite : 'unit')`. En TAP, un cas « incomplete » sort en `not ok … # TODO`, que meson compte comme un échec attendu et non comme un échec. Ce comportement est vérifié dès la première tâche du plan.
- meson construit toujours les prérequis des tests avec `ninja -C build` (`build_by_default : false` est sans effet sur eux). Une option `tests` (`meson_options.txt`, vraie par défaut) permet de les écarter : `data/gtkterm-update.sh` configure avec `-Dtests=false`, pour qu'une erreur dans un test ne bloque jamais la mise à jour d'un utilisateur. (Amendement après la relecture finale.)
- `tests/stubs.c` fournit, au besoin, les symboles de l'exécutable que les modules du cœur référencent encore. Le fichier part vide et ne grossit que si l'édition de liens l'exige.
- Style des nouveaux fichiers : celui du `.clang-format` (tabulations, Allman, `if(`).

### 2.2 Règle des bogues connus

- Chaque test décrit le comportement **attendu**.
- Un cas qui correspond à un défaut de la revue est marqué par son identifiant (ex. `"M18"`) au moyen d'une aide commune (`tests/test_util.h`) :
  - si le résultat obtenu diffère de l'attendu, l'aide appelle `g_test_incomplete_printf("bogue M18 — sous-projet 1 : obtenu …")` ; `meson test` reste vert et le cas sort en `# TODO` dans la sortie TAP ;
  - si le résultat est **conforme** alors que le cas est encore marqué, l'aide fait échouer le test (« bogue M18 semble corrigé : retirer le marquage »). Un correctif accidentel ne passe donc pas inaperçu, et chaque correctif du sous-projet 1 retire explicitement son marquage.
- La liste des bogues connus s'affiche avec `meson test -C build -v` (lignes `# TODO`) ; le résumé par défaut de meson ne les compte pas.
- Un comportement simplement discutable (`\e` et `\0` non reconnus, `\x414` → `41 34`, valeurs de liste sans échappements) est figé tel quel, avec un commentaire « comportement actuel, à décider ». Il n'est pas marqué incomplete.
- Pour les cas qui planteraient le processus (C7), le test s'exécute dans un sous-processus (`g_test_trap_subprocess`) afin que le plantage n'interrompe pas la suite.

### 2.3 Contenu

**`test_macros_format`** (tableaux de cas, une fonction de test par famille)
- Échappements `parse_macro_string`, avec les entrées et octets attendus de la revue §5 : `\x41`, `\X7f`, `\x4`, `\x4G`, `\x414`, `\x`, `\xZZ`, `\30`, `\306`, `\0`, `\r\n`, `\\`, `\\x41`, `a\`, `\e`, `\"`, `\x0x`.
- Expansion `format_action_with_args` : entiers et modificateurs de longueur (`%hhd`, `%hhu`, `%hd`, `%u`, `%lu`), hexa et octal (`%x`, `%02X`, `%o`), flottants (`%f` avec zéros de fin conservés, `%.2f`, `%5.1f`, `%lf`, `%e`, `%hf`), chaînes (`%s` avec antislash, `%s` avec `\x41` non interprété), `%%`, argument manquant, libellés `[…]`, listes `%#Nom`, séparateur `%&`.
- Bogues connus marqués incomplete :
  - M18 : `%#x`, `%#08x`, `%#o` lus comme une liste alors qu'aucune liste de ce nom n'existe (attendu : `0xff`, `0x0000ff`, `010`) ;
  - M19 : `%5s` avec `a\b`, `%c` avec `\` ;
  - M20 : `XY[a%%]%d` (le « Y » disparaît) ; `arr[0] %d` → `arr5` et `50% off` → `5011ff` sont figés comme « comportement actuel, à décider », car la syntaxe des libellés et un `% o` valide les rendent ambigus ;
  - M23 : l'argument d'un `%x`/`%X` est saisi en hexadécimal : `ff` → `ff`, `010` → `10` ; côté validation, `ff` et `09` sont acceptés.
- Comptage et description des arguments (`macro_count_format_args`, `macro_get_arg_infos`) : `%#L1 %d` → 2 arguments, `%%d` → 0, `%&` → 0.
- Validation `macro_type_value_valid` : bornes `hhd` (127/128), `hhu` (255/256), `d` (2147483647/2147483648), `u` (-1), `x` (`0x10`, `ff`, `09`), `f` (`2,5` refusé), espace initiale.
- `strtod` dépend aujourd'hui de la locale « C » imposée par parsecfg (M9). Le test fixe explicitement `setlocale(LC_NUMERIC, "C")` et documente cette dépendance.

**`test_macros_list`**
- Création, ajout d'entrées, recherche par nom (casse), énumération, libération ; interface complète de `macros_list.h`, y compris les cas limites (nom absent, liste vide).

**`test_baudrates`**
- `find_standard_baudrate` et `speed_t_to_baud` sur toute la table générée, plus des valeurs hors table.
- `set_port_baudrate` sur une paire `openpty` : débit standard (115200), puis débit non standard via `BOTHER` (250000), relu par `TCGETS2`. Si `openpty` est indisponible dans l'environnement, le test appelle `g_test_skip`.

**`test_parsecfg`**
- Aller-retour : écrire plusieurs profils avec `cfgDump` dans un fichier temporaire, relire avec `cfgParse`, comparer chaînes, entiers et booléens.
- Locale : après une lecture, `setlocale(LC_ALL, NULL)` doit être inchangée. Bogue M9, marqué incomplete.
- Fichier vide, puis `cfgStoreValue` : ne doit pas planter (bogue C7, sous-processus, marqué incomplete).
- Deux sections dont le nom ne diffère que par la casse : parsecfg les refuse (`CFG_USED_SECTION`). Ce refus est figé comme comportement actuel : le défaut C6 vient de `term_config.c`, qui compare les noms de profils avec `strcmp`. Son test de non-régression sera écrit au sous-projet 1, au niveau de `term_config`, comme pour C5.
- Section ajoutée par `cfgAllocForNewSection` : la valeur renvoyée est le *nombre* de sections, l'indice de la nouvelle section est cette valeur moins un. Un booléen jamais renseigné vaut -1 et s'écrit `True` (comportement actuel, à décider).
- Valeur non entre guillemets contenant une espace : refusée aujourd'hui. Le test documente la cause de C5 (c'est `term_config.c` qui oublie de retirer les groupes GKeyFile, pas parsecfg qui est fautif) ; le test de non-régression de C5 lui-même sera écrit au sous-projet 1, au niveau de `term_config`.

**Syntaxe des exemples Lua**
- `test('lua-syntax', luac, args : ['-p', <scripts/*.lua>], suite : 'lint')`, avec `luac5.4` trouvé par `find_program(['luac5.4', 'luac'], required : false)`. Le test est sauté si `luac` est absent.

### 2.4 Sanitizers et valgrind

`tests/README.md` explique :
- `meson setup build-asan -Db_sanitize=address,undefined && meson test -C build-asan` ;
- `meson test -C build --wrap='valgrind --error-exitcode=1 --leak-check=full'` ;
- comment lire les cas « incomplete ».

Le socle doit passer sans erreur sous ASan/UBSan. Un problème mémoire révélé dans un module du cœur est consigné dans la revue (nouvel identifiant) et marqué incomplete s'il ne se corrige pas trivialement sans changer de comportement.

## 3. Nettoyage du dépôt

- Supprimer `.circleci/`.
- `graphify-out/` : `git rm -r --cached`, suppression du dossier sur le disque, ajout à `.gitignore`.
- `Capture1.png` … `Capture6.png` → `docs/images/`, liens du `README.md` corrigés. Suppression de `screenshot.png`, qui n'est référencée nulle part.
- Suppression de `src/auto_config.h` ; ses trois inclusions (`cmdline.c`, `interface.c`, `gtkterm.c`) deviennent `#include <config.h>`.
- `po/POTFILES.in` : ajout de `src/terminal_display.c`, `src/update.c`, `src/transport.c`, `src/macro_panel.c`, `src/script_panel.c`, `src/macros_format.c`, `src/macros_list.c`.
- `.gitignore` : `build*/` au lieu de `build` (couvre `build-asan`).
- `CLAUDE.md` :
  - remplacer « No test suite exists » par une section Tests (`meson test -C build`, règle des bogues connus, sanitizers, `gtkterm_core`) ;
  - retirer la mention de la CI CircleCI ;
  - corriger les passages périmés (extraction des sections de macros du `.gtktermrc`, fichier `gtkterm_script_colors.ini`).
- Commiter la revue `docs/superpowers/reviews/2026-10-06-revue-code.md`.

## 4. Critères d'acceptation

1. `meson setup build --wipe && ninja -C build` compile sans erreur ; l'application démarre et affiche dans « À propos » la version et la révision git courante.
2. Après un nouveau commit, un simple `ninja -C build` met à jour la révision affichée.
3. `meson test -C build` : tous les tests passent ou sont « incomplete » / « skip » ; aucun échec.
4. La sortie de `meson test -C build -v` liste en `# TODO` les bogues connus attendus : C7, M9, M18, M19, M20, M23.
5. `meson test -C build-asan` (address, undefined) : aucun échec.
6. `git ls-files graphify-out .circleci src/auto_config.h` est vide ; le README affiche les captures.
7. Aucun changement de comportement de l'application en dehors de la boîte « À propos ».
