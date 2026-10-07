# Tests de GTKTerm

Tests unitaires GLib GTest des modules sans GTK (bibliothèque `gtkterm_core`, voir `src/meson.build`), plus une vérification de syntaxe des exemples Lua.

## Lancer

```sh
meson test -C build                 # tout
meson test -C build --suite unit    # tests unitaires seulement
meson test -C build --suite lint    # syntaxe des exemples Lua (luac -p)
meson test -C build test_config_store -v   # un test, statut de chaque sous-test (le détail est dans build/meson-logs/testlog.txt)
```

`ninja -C build` construit aussi les exécutables de test (meson les rattache toujours à la cible par défaut). Pour une construction sans tests : `meson setup build -Dtests=false` (c'est ce que fait `data/gtkterm-update.sh`).

## Bogues connus (`# TODO`)

Un cas qui correspond à un défaut de la revue (`docs/superpowers/reviews/2026-10-06-revue-code.md`, identifiants C1…, M1…) est marqué par son identifiant via `test_conclude()` (`tests/test_util.h`) :

- tant que le défaut existe, le cas est « incomplete » : il sort en `not ok … # TODO bogue M18 - …` et **ne fait pas échouer** la suite ;
- si le comportement devient conforme alors que le marquage est encore là, le cas **échoue** (« semble corrigé : retirer le marquage »).

Le résumé de meson ne compte pas ces cas, et `meson test -v` ne les montre que comme `EXPECTEDFAIL`, sans message. La liste complète, avec le détail attendu/obtenu, est dans le journal de meson :

```sh
meson test -C build && grep '# TODO' build/meson-logs/testlog.txt
```

Les messages des tests sont en ASCII : les tests tournent en locale C, où GLib remplace tout caractère non ASCII par `?`.

Corriger un bogue = rendre le cas conforme **et** remplacer son identifiant par `NULL` dans la table du test.

## Écrire un test

- Un exécutable par module : `tests/test_<module>.c`, déclaré dans le dictionnaire `unit_tests` de `tests/meson.build`.
- Un cas par fonction de test ; pour une table de cas, une entrée par cas avec `g_test_add_data_func`. Ne jamais appeler `test_conclude()` deux fois dans un même cas.
- Un cas qui peut planter le processus s'exécute dans un sous-processus (`g_test_trap_subprocess`).
- Fichiers temporaires : `g_dir_make_tmp`, `test_write_file()`, `test_rmdir()`.
- Tests unitaires du cœur GLib (dictionnaire `unit_tests` de `tests/meson.build`) : `test_history`, `test_rx_transform`, `test_device_match`, `test_view_render`, `test_baudrates`, `test_macros_format`, `test_macros_list`, `test_macro_set`, `test_macro_file`, `test_macro_poller`, `test_serial_termios`, `test_config_file`, `test_profile`, `test_config_store`.
- Tests d'intégration (dictionnaire `integration_tests` de `tests/meson.build`) : `test_link`, `test_transport` (n'a plus besoin de bouchons : le transport n'appelle plus l'interface), `test_macros_send`, `test_script_engine`. Un module de src/ qui dépend de l'interface se compile avec les bouchons de tests/stubs/ (`stub_link.c` remplace l'ancien `stub_display.c`, `stub_journal.c`, `stub_macros.c` (bouchon de l'état des macros, jeu fourni par le test, pour `test_script_engine`), `stub_ui.c`) ; un bouchon enregistre l'appel (stub_record) et ne contient aucune logique métier.
- Style : celui de `.clang-format` (tabulations, Allman, `if(`).

## Sanitizers et valgrind

```sh
meson setup build-asan -Db_sanitize=address,undefined
meson test -C build-asan
```

LeakSanitizer vérifie les fuites sans aucune suppression : toute fuite fait échouer le test.

valgrind, pour les erreurs mémoire (les fuites sont vérifiées par ASan) :

```sh
meson test -C build --suite unit --wrap='valgrind --error-exitcode=1 --leak-check=no'
```

Les fuites ne sont pas vérifiées par valgrind ici (--leak-check=no) : elles le sont par LeakSanitizer (build-asan).

## Suite graphique (`tests/gui/`)

```sh
meson test -C build --suite gui --print-errorlogs
```

Exclue de `meson test -C build` (`add_test_setup` dans `tests/meson.build`). Chaque scénario `test_*.py` lance GTKTerm par AT-SPI sur un affichage **broadway privé** (et un faux navigateur broadway, `broadway.py`) : bus D-Bus et AT-SPI privés (`dbus-run-session`, `run-gui-test.sh`), `HOME` / `XDG_*` dans `build/tests/gui/runs/`, ports pty uniquement, faux `git` (aucun réseau). Chaque test est sauté (code 77) si `broadwayd`, `dbus-run-session` ou `python3-gi` (Atspi 2.0) manquent. Résultats détaillés : `build/tests/gui/runs/<scénario>/result.txt` et `gtkterm.stderr`.

- Garde-fou : `guard.c`, chargé par LD_PRELOAD, refuse les vrais ports série et la configuration réelle de l'utilisateur (message `EMGUARD` dans stderr, qui fait échouer le scénario). Il corrige aussi le clavier de GDK broadway, qui ne renseigne pas les modificateurs consommés.
- Échec : lire `build/tests/gui/runs/<scénario>/result.txt` (une ligne `OK` / `ECHEC` par vérification) et `gtkterm.stderr`.
- Un scénario finit par `finish(e)` (quitte par le menu, vérifie stderr) puis `sys.exit(conclude())`. Bruit GTK connu sous broadway, sans rapport avec GTKTerm : `KNOWN_NOISE` dans `gtktest.py` ; n'y ajouter qu'une assertion GTK dont la pile ne passe pas par GTKTerm.
- `test_macros_send_affichage.py` n'ouvre pas GTKTerm : il exécute `test_macros_send` (chemin dans `GTKTERM_TEST_MACROS_SEND`) sur le broadway privé, pour que ses cas qui exigent un affichage ne soient pas sautés (ils le sont dans `meson test` ordinaire).
- Pour ajouter un scénario : écrire `tests/gui/test_<nom>.py` avec `from gtktest import *`, des `check(condition, libellé, détail)`, `finish(e)` puis `sys.exit(conclude())`, et l'ajouter à `gui_scenarios` dans `tests/gui/meson.build`.
