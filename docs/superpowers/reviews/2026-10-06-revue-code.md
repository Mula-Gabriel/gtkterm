# Revue de code GTKTerm — 2026-10-06

Revue en lecture seule de l'ensemble du dépôt (≈ 14 200 lignes de C, 324 commits), menée par sous-système : transport/série, configuration, macros, moteur Lua, interface/tampon/divers.
Les constats marqués **vérifié** ont été reproduits (petit banc d'essai compilé hors dépôt, ou lecture du code confirmée à la main) ; les autres portent un niveau de confiance.

## 1. Synthèse

Le logiciel fonctionne au quotidien, mais il repose sur une architecture où **les couches basses appellent l'interface**, où l'**état est global et souvent dupliqué**, et où **aucune bibliothèque testable** n'existe (un seul `executable()` meson, pas de `meson test`, CI cassée).
La revue relève une quinzaine de défauts graves (plantages, fichier de configuration rendu illisible, comportement matériel faux en RS485, accès mémoire après libération), une trentaine de défauts fonctionnels, et beaucoup de code mort / dupliqué.

Bonne nouvelle : plusieurs modules sont déjà **purs ou presque** (`macros_format.c`, `macros_list.c`, `baudrates.c`, `parsecfg.c`) et peuvent être couverts par des tests immédiatement. Le transport peut être testé avec des paires pty (`openpty`/`socat`) et la boucle locale TCP, moyennant des bouchons d'édition de liens.

## 2. Problèmes d'architecture transverses

1. **Couches inversées.** Le transport, `files.c`, `logging.c`, `term_config.c` (54 appels), `macros.c`, `device_monitor.c`, `user_signals.c` appellent directement l'interface (`show_message` — boîte modale `gtk_dialog_run` dans le chemin d'E/S, donc réentrance —, `Set_status_message`, `Set_window_title`, `interface_open/close_port`…). Dix fichiers incluent `interface.h`.
2. **`send_serial` (terminal_display.c) pilote le port** : envoi sur le transport + écho VTE + effacement d'écran mêlés dans un composant d'affichage. Il est appelé depuis le fil Lua.
3. **État global partout, sources de vérité dupliquées** : `echo_on/crlfauto_on/timestamp_on` (interface.c) doublonnent `config.echo/…` ; polling des macros en trois exemplaires (`macro_t.polling_*`, `macro_polling_t`, widgets), arguments aussi ; 37 globales non statiques liées à parsecfg aux noms courts (`port`, `speed`, `font`…) ; `files.c` exporte 15 globales.
4. **Deux mécanismes de persistance de la configuration** : parsecfg (bibliothèque de 2001) pour les profils, GKeyFile pour `[window]`, `[colors]`, `[layout]`, `[session]` — ces groupes sont retirés du fichier avant chaque passage de parsecfg puis remis. Plusieurs chemins oublient ce retrait (voir C5).
5. **Le tampon stocke les données transformées** (CR/LF ajoutés, horodatages, troncature) : « Save RAW », vue hexa, journal et données vues par Lua (`tap_func`) sont faux dès que l'horodatage ou le CR/LF automatique sont actifs.
6. **Fil Lua** : la règle « jamais de GTK hors du fil principal » est violée (`send_serial` → `vte_terminal_feed`) ; le fil lit `macros` sans verrou ; l'état des ports Lua est partagé sans verrou avec les rappels de la boucle principale ; la configuration termios/TCP du moteur Lua duplique `transport.c`/`baud.c`.
7. **Perte de liaison gérée par trois chemins concurrents** (erreur sur le descripteur, `EIO` de `TIOCMGET`, événement udev « remove ») qui ne mettent pas tous l'interface à jour.
8. **Construction et dépôt** : pas de `warning_level` ni de `c_std` ; `RELEASE_DATE 'May 2024'` figé ; `GIT_REVISION` figé à la configuration meson ; CI sur `debian:buster` (fin de vie) sans lua5.4 ni gtksourceview-4 — elle ne peut pas compiler aujourd'hui ; `graphify-out/` (125 fichiers, 3,5 Mo) versionné ; `Capture1-6.png` à la racine ; `screenshot.png` non référencée ; `auto_config.h` inutile ; `POTFILES.in` incomplet ; `CLAUDE.md` en partie périmé.
9. **Styles mêlés** : tabulations + `if(` (buffer, cmdline, files, gtkterm, logging, transport, update, term_config en partie) ; tabulations + `if (` (macro_panel, parsecfg, search, device_monitor, baud*) ; GNU 2 espaces (terminal_display, macros, macros_format, macros_list) ; K&R 4 espaces (script_engine, script_panel, fin d'interface.c). Noms français/anglais mêlés (`Lis_Config`, `vitesse`, `parite`, `flux`, `car`…) ; nombres magiques pour parité (0-2), flux (0-3), signal (0/1).

Avertissements `-Wall -Wextra` : 165 (151 paramètres inutilisés, 11 `sign-compare`, 3 `cast-function-type` — `files.c:268`, `interface.c:865,1134`).

## 3. Défauts critiques (plantage, corruption, comportement matériel faux)

| # | Module | Défaut | Confiance | Scénario |
|---|---|---|---|---|
| C1 | script_engine.c:793 | Fermeture des ports en fin de script via `l_close(L)` qui lit `luaL_checkinteger(L,1)` ; `lua_pop(L,0)` ne retire rien. Après une erreur Lua, l'indice 1 contient le message → erreur hors pcall → `abort()` de GTKTerm. Avec deux ports ouverts, seul le premier est fermé (fuite de descripteur). | **vérifié** | Script qui lève une erreur avec un port 1..15 ouvert (ex. `scripts/10_firmware_update.lua`). |
| C2 | transport.c:737, script_engine.c:392 | SIGPIPE ni ignoré ni évité (`MSG_NOSIGNAL`). | **vérifié** (aucune occurrence) | `write` vers un pair TCP fermé → processus tué. |
| C3 | transport.c:776 | En RS485 (`flux == 3`), `transport_get_signals` appelle `transport_set_signal(1)` qui **inverse** RTS, toutes les 100 ms. | **vérifié** | Émetteur RS485 actif la moitié du temps au repos → collisions sur le bus. |
| C4 | transport.c:290-330 | `c_cflag \|= CS7` sans `&= ~CSIZE` ; PARENB/PARODD/CSTOPB/CRTSCTS/CLOCAL hérités jamais effacés. | **vérifié** | 7 bits demandé → 8 bits réels ; CRTSCTS laissé par un autre outil bloque l'émission. |
| C5 | term_config.c:1548,1278,1457 | `[session] script_path` contenant une espace : écrit sans guillemets par GKeyFile, rejeté par parsecfg ; trois chemins lancent `cfgParse` sans retirer les groupes GKeyFile. | **vérifié** | Au démarrage « Cannot read configuration file », valeurs par défaut ; charger/enregistrer échouent ensuite. |
| C6 | term_config.c / parsecfg.c:913 | `strcmp` côté term_config, `strcasecmp` côté parsecfg. | **vérifié** | Enregistrer « Default » alors que « default » existe → deux sections → `CFG_USED_SECTION` → fichier illisible définitivement. |
| C7 | term_config.c:2209-2241 | `.gtktermrc` vide → tableaux parsecfg NULL → `cfgStoreValue(...,0)`. | **vérifié** | Premier changement de couleur → erreur de segmentation. |
| C8 | macros.c:86,120 | `strlen(NULL)` sur une macro sans libellé. | haute | « Ajouter », capturer un raccourci, OK, presser le raccourci → plantage. |
| C9 | macro_panel.c:565,911 | `rebuild_macro_buttons` détruit les pages sans vider `macro_button_table`/`macro_polling_table`. | haute | Masquer un onglet contenant une macro en polling, ou recharger un `.ini` → accès après libération ; l'ancien polling envoie la macro *i* du nouveau fichier avec les anciens arguments. |
| C10 | script_engine.c:575-718 | Le fil Lua parcourt `macros` sans verrou pendant que le fil principal la libère/réalloue. | haute | Modifier les macros ou recharger le `.ini` pendant un script → accès après libération. |
| C11 | script_engine.c:379,419 | `send_serial` appelé depuis le fil Lua → `vte_terminal_feed` (écho) et `clear_buffer()` hors fil GTK. | **vérifié** | Scripts 08/09 avec écho actif. |
| C12 | term_config.c:654 | Dialogue Port non modal avec `static Combos[]`. | haute | Ouvrir A, ouvrir B, annuler B, valider A → lecture de widgets détruits. |
| C13 | term_config.c:1334 | Les groupes `window`/`colors`/`layout` apparaissent dans « Charger »/« Supprimer ». | haute | Charger « window » puis enregistrer → `[window]` en double → fichier illisible. |

## 4. Défauts fonctionnels majeurs

**Transport / série / udev**
- M1. `transport_close` décide du type de fermeture avec la *nouvelle* configuration (term_config.c:1082 modifie `config` puis rouvre) : `tcsetattr` sur descripteur fermé, termios non restauré ; double `close` en mode serveur (`transport_fd == listen_fd`).
- M2. Le moniteur udev ignore le mode de transport (débrancher l'ancien USB ferme une connexion TCP) et n'est démarré qu'une fois (passage TCP → série sans surveillance).
- M3. Reconnexion automatique : boîte modale toutes les 2 s ; le minuteur se réarme même après `transport_close` ; en RS485 sur pty, `perror` toutes les 100 ms.
- M4. Double ouverture au démarrage avec reconnexion automatique (deux boîtes d'erreur si le port est verrouillé).
- M5. TCP client : fin de flux → port fermé sans mise à jour de l'état ; état « tcp://… » affiché pendant la connexion.
- M6. Lien utilisateur hors udev (`~/x -> /dev/ttyACM0`) : les événements ne correspondent jamais (comparer `st_rdev` serait robuste).
- M7. `gchar car` comparé à -1 (term_config.c:949, files.c:198, transport.c:98) : sur ARM (char non signé), « attendre un caractère » toujours actif ; au-delà de 127 jamais reconnu.
- M8. `gtkterm-update.sh` : `disown` n'existe pas sous dash ; `pkexec ninja install` laisse des fichiers root dans `build/` ; `gtkterm &` relance celui du PATH.

**Configuration**
- M9. parsecfg fait `setlocale(LC_ALL,"C")` (parsecfg.c:595,1258) sans restaurer : interface plus traduite après la première lecture ; `strtod` des macros fonctionne *grâce* à ce couplage caché.
- M10. `save_config_silent` (chargement d'un `.ini` de macros, URL de mise à jour) persiste les options de ligne de commande (`-p`, `-s`) dans le profil actif.
- M11. `remove_section` : `FILE*` jamais fermé, recherche de `[nom]` non ancrée en début de ligne (`socket_host = [::1]` tronque), décalage d'un octet en fin de fichier, réécriture non atomique.
- M12. Erreur de `g_key_file_load_from_file` ignorée → risque de réécrire le fichier avec seulement `[window]` (perte des profils) — non vérifié.
- M13. `~/.config` absent : `g_file_move`/`cfgDump` échouent en silence alors que « a été créé » s'affiche ; `getenv("HOME")` NULL non géré.
- M14. `rows`/`columns` inversés (rows=80, columns=25), compensé par hasard ; `visual_bell` défaut TRUE ou FALSE selon le chemin, jamais appliqué.

**Macros**
- M15. Raccourci clavier et polling envoient l'action **sans expansion** (`%d` littéral, `%%` reste `%%`) — trois chemins d'envoi divergents (`macros.c:69`, `macros.c:99`, `script_engine.c:656`).
- M16. `Save_shortcuts` restaure arguments et polling **par position** : supprimer, déplacer ou trier transfère le polling à une autre commande.
- M17. Raccourcis connectés deux fois (`term_config.c:1667` puis `gtkterm.c:78`) → double envoi probable ; pas de contrôle de bornes dans `shortcut_callback`.
- M18. `%#x`, `%#o`, `%#08x` lus comme la liste « x ».
- M19. Antislash échappé *avant* printf : `%5s` avec `a\b` donne 4 octets ; `%c` avec `\` donne 0A ; `%.1s` coupe `\\`.
- M20. Libellés : `arr[0] %d` → `arr5` ; `XY[a%%]%d` → `X5` ; `50% off` lu comme `% o` → `5011ff`.
- M21. Lecture `.ini` : séparateurs `::`, `|`, `:`, `=` non échappés ; `GError` réutilisé ; fuite de `macro_keys` ; sans `[macros]`, anciennes macros conservées mais listes remplacées.
- M22. Période de polling appliquée et sauvegardée à chaque frappe (taper 2000 passe par 2 ms) ; minuteur à 1 ms permanent ; `period_ms*1000` déborde au-delà de ~71 min.
- M23. `%x` analysé en base 0 : `ff` invalide envoie `0`, `010` donne `8`.
- M24. Aide de la fenêtre des macros obsolète depuis 7a697b2 (annonce encore `\0A`, `\0FF`).

**Lua**
- M25. « Stop » n'arrête pas le script (seuls `wait_for`/`sleep` reviennent) ; `while true do end` ne s'arrête jamais (aucun `lua_sethook`) ; `os.exit` ferme GTKTerm ; pas de `lua_atpanic`.
- M26. `wait_for` s'arrête au premier octet nul (`g_strstr_len`, `lua_pushstring`) : protocoles binaires impossibles.
- M27. Ordre non garanti : `send_macro` est différé (idle), `send` direct ; `send_macro` renvoie vrai avant l'envoi.
- M28. Tampon de réception plein : bloc jeté en silence ; débits limités à 9 valeurs avec repli silencieux sur 9600 ; écritures partielles/EAGAIN non reprises ; `send_hex` ignore les caractères invalides.
- M29. Exemples faux : `10_firmware_update.lua:71,93` (`wait_for` renvoie les données motif inclus, priorité de `..` sur `or`) → plante via C1 ; `08` délai trop court ; commentaire faux dans `09`. `SKILL.md` de `/lua-script` ne correspond pas à l'API réelle (`log`, `send`, `open tcp_server`, `clear`, `save_csv`).
- M30. Le `GThread` n'est jamais joint (fuite à chaque exécution).

**Interface / tampon / fichiers / mise à jour**
- M31. Journal dupliqué : `set_view()` rejoue le tampon et l'affichage appelle `log_chars` → changer de vue réécrit jusqu'à 128 Ko dans le journal ; en vue hexa le journal contient le texte « XX ».
- M32. Avec « ESC efface l'écran », tout envoi d'un octet unique 0x1B est avalé (terminal_display.c:116) — hexa, macro, Lua, fichier.
- M33. `GIT_REVISION` figé par `run_command` à la configuration meson : après de nouveaux commits + `ninja`, mise à jour proposée en boucle ; le test est « différent », pas « plus récent ».
- M34. update.c : pointeur pendant si on ferme « À propos » pendant `git ls-remote` ; pas de `GCancellable` ni `GIT_TERMINAL_PROMPT=0` ; URL passée sans `--` ; `'%s'` non échappé dans la commande ; « Ignorer cette version » oublié au redémarrage.
- M35. files.c : rappel `ecriture` typé `void` enregistré comme `GIOFunc` → valeur de retour aléatoire, transfert bloqué ; `read()` -1/0 → boucle à 100 % ; lecture `buffer[bytes_read]` hors données ; `GIOChannel` jamais libéré ; « Save ASCII » supprime ≥ 0x80 et TAB.
- M36. Les `GAction` ne sont jamais ajoutées à un `GActionMap` : le bouton radio ASCII/Hexa se désynchronise.
- M37. Écho : en vue texte une ligne `> a\r\n` par touche ; en vue hexa l'index dérive ; l'écho n'est pas dans le tampon et disparaît au changement de vue.
- M38. Chaîne de format non maîtrisée (`show_message`, `search.c:75`) : un `%` dans un chemin ou `strerror` → comportement indéfini.
- M39. Journal : le dialogue demande « écraser ? » mais ouvre en `"a"` ; menus bloqués après un échec ; boîte modale dans le chemin de réception.
- M40. `Put_temp_message` retire le sommet de la pile de la barre d'état → efface le nom du port.

**Constats du socle de tests (sous-projet 0, sous ASan)**
- M41. `try_parse_label` (macros_format.c) allouait le libellé même quand l'appelant passe `label_out = NULL` (`format_action_with_args`) : fuite à chaque expansion d'une macro à libellé `[…]`. **Corrigé au sous-projet 0** (correctif trivial sans changement de comportement, spec §2.4), détecté par LeakSanitizer sur `test_macros_format`.

## 5. Défauts mineurs (sélection)

`prctl(PR_CAP_AMBIENT_RAISE)` inopérant pour `bind` ; `-T serial` non reconnu, `none` impossible pour `--parity/--flow`, `--help` sort en 1 ; `%.15s` tronque les chemins longs ; branche non-Linux de baud.c ne compile pas ; `metro-tty-index` peut laisser un lien orphelin vers un autre appareil ; fuites (`get_config_file_path` déclarée `const` mais allouée, tableaux parsecfg, police, `out_buffer`, `logfile_default`, `lists_model`, fermetures de raccourcis) ; `tooltip[256]` coupe l'UTF-8 ; `sscanf("%02X")` accepte « 123 » ; `fic_defaut` partagé entre envoi et sauvegarde ; titre « Main Window » pour le dialogue terminal ; « Close », « Delete Shortcut » non traduits ; icône 48×48 installée deux fois ; `g_print("Config_macros called")` oublié.

## 6. Code mort et duplication

- **Code mort** : ~500 lignes de parsecfg (mode `CFG_SIMPLE`, `fetchVarFromCfgFile`, types inutilisés) ; `serial_port_fd`, `HAVE_LINUX_SERIAL_H` ; `Envoie_car`, `blank_data`, `Text/buffer/iter` (interface.c), `adj` (files.c) ; `send_macro_with_arg`, `macro_get_format_type`, `macro_has_format_arg`, `macro_get_format_types`, `macro_list_remove_entry`, `macro_stack` ; `L_global`, `chan_err` (Lua) ; clé `show_rxtx_compat` ; `i18n.c` largement redondant avec `g_print`.
- **Duplication** : `save_config_silent` ≈ `really_save_config` ; `save_raw_file` ≈ `save_ascii_file` ; `transport_setup_watches` ≈ `transport_setup_client_watches` ; quatre boucles de balayage dans `macros_format.c` ; gestionnaires Add/Delete/Move et `*_edited` recopiés dans le dialogue des macros ; termios/TCP du moteur Lua ≈ transport.c ; prototypes d'affichage dans `interface.h` et `terminal_display.h` ; motif `g_strdup_printf` + `show_message` + `g_free` ×10.

## 7. Testabilité

- **Testable tout de suite** : `macros_format.c` (échappements, expansion, validation, infos d'arguments), `macros_list.c`, `baudrates.c` (+ en-tête généré), `set_port_baudrate` sur `openpty`, `parsecfg.c` (avec bouchon `i18n_fprintf`).
- **Après petite extraction** : construction termios pure (régression C4), correspondance udev, texte d'état, analyse de la ligne de commande, formatage hexa d'une ligne, décodage de la saisie hexa, décision de mise à jour, tampon (avec horloge et puits d'affichage injectés), sérialisation `.ini` des macros, décision de tir du polling.
- **Intégration** : transport + serial + baud compilés avec des bouchons (`show_message`, `Set_*`, `put_chars`…) contre une paire pty et la boucle locale TCP ; moteur Lua en exécutable sans interface avec un faux port principal, scripts de test `tests/lua/*.lua`.
- **Cadre** : GLib GTest + `meson test` (GLib 2.80, meson 1.3.2 présents). `G_TEST_OPTION_ISOLATE_DIRS` pour les tests de configuration. Sanitizers `-Db_sanitize=address,undefined` et `thread`. `valgrind` et `socat` sont installés ; ni `xvfb-run` ni `weston` (broadwayd présent).
- **Prérequis meson** : une `static_library` cœur partagée par l'exécutable et les tests.
