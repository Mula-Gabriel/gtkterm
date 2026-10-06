# Tests de GTKTerm

Tests unitaires GLib GTest des modules sans GTK (bibliothèque `gtkterm_core`, voir `src/meson.build`), plus une vérification de syntaxe des exemples Lua.

## Lancer

```sh
meson test -C build                 # tout
meson test -C build --suite unit    # tests unitaires seulement
meson test -C build --suite lint    # syntaxe des exemples Lua (luac -p)
meson test -C build test_parsecfg -v   # un test, sortie TAP détaillée
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
