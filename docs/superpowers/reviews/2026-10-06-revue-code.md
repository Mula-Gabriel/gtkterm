# Revue de code GTKTerm — 2026-10-06

Revue en lecture seule de l'ensemble du dépôt (≈ 14 200 lignes de C, 324 commits), menée par sous-système : transport/série, configuration, macros, moteur Lua, interface/tampon/divers.
Les constats marqués **vérifié** ont été reproduits (petit banc d'essai compilé hors dépôt, ou lecture du code confirmée à la main) ; les autres portent un niveau de confiance.

## 1. Synthèse

Le logiciel fonctionne au quotidien, mais il repose sur une architecture où **les couches basses appellent l'interface**, où l'**état est global et souvent dupliqué**, et où **aucune bibliothèque testable** n'existe (un seul `executable()` meson, pas de `meson test`, CI cassée).
La revue relève une quinzaine de défauts graves (plantages, fichier de configuration rendu illisible, comportement matériel faux en RS485, accès mémoire après libération), une trentaine de défauts fonctionnels, et beaucoup de code mort / dupliqué.

Bonne nouvelle : plusieurs modules sont déjà **purs ou presque** (`macros_format.c`, `macros_list.c`, `baudrates.c`, `parsecfg.c`) et peuvent être couverts par des tests immédiatement. Le transport peut être testé avec des paires pty (`openpty`/`socat`) et la boucle locale TCP, moyennant des bouchons d'édition de liens.

## 2. Problèmes d'architecture transverses

1. **Couches inversées.** Le transport, `files.c`, `logging.c`, `term_config.c` (54 appels), `macros.c`, `device_monitor.c`, `user_signals.c` appellent directement l'interface (`show_message` — boîte modale `gtk_dialog_run` dans le chemin d'E/S, donc réentrance —, `Set_status_message`, `Set_window_title`, `interface_open/close_port`…). Dix fichiers incluent `interface.h`.
   — Corrigé par le sous-projet 6 (`feature/refonte-gtk4`) : le cœur et la couche application n'incluent plus GTK ni n'appellent l'interface ; leurs effets
   d'interface passent par la table de crochets `GtAppUi` (`app_ui.h`), l'interface GTK 4 est isolée dans `src/ui/`.
2. **`send_serial` (terminal_display.c) pilote le port** : envoi sur le transport + écho VTE + effacement d'écran mêlés dans un composant d'affichage. Il est appelé depuis le fil Lua.
3. **État global partout, sources de vérité dupliquées** : `echo_on/crlfauto_on/timestamp_on` (interface.c) doublonnent `config.echo/…` ; polling des macros en trois exemplaires (`macro_t.polling_*`, `macro_polling_t`, widgets), arguments aussi (traité pour les macros par le sous-projet 4 : modèle `GtMacroSet` + ordonnanceur) ; 37 globales non statiques liées à parsecfg aux noms courts (`port`, `speed`, `font`…) ; `files.c` exporte 15 globales.
   — Corrigé par le sous-projet 6 (`feature/refonte-gtk4`) : options d'affichage et de comportement dans un seul objet (`GtTermOptions`), menus en
   `GPropertyAction` sur ses propriétés ; globales de `files.c` supprimées (`GtFileSender`, `capture.c`, `GtRxLogger`).
4. **Deux mécanismes de persistance de la configuration** : parsecfg (bibliothèque de 2001) pour les profils, GKeyFile pour `[window]`, `[colors]`, `[layout]`, `[session]` — ces groupes sont retirés du fichier avant chaque passage de parsecfg puis remis. Plusieurs chemins oublient ce retrait (voir C5).
5. **Le tampon stocke les données transformées** (CR/LF ajoutés, horodatages, troncature) : « Save RAW », vue hexa, journal et données vues par Lua (`tap_func`) sont faux dès que l'horodatage ou le CR/LF automatique sont actifs.
6. **Fil Lua** : la règle « jamais de GTK hors du fil principal » est violée (`send_serial` → `vte_terminal_feed`) ; le fil lit `macros` sans verrou ; l'état des ports Lua est partagé sans verrou avec les rappels de la boucle principale ; la configuration termios/TCP du moteur Lua duplique `transport.c`/`baud.c`. — Traité : `send_serial` par le sous-projet 2, macros par le sous-projet 4 ; ports Lua sur `GtScriptPort`/`GtTransport` (état atomique, files verrouillées, plus de termios/TCP dans le moteur) par le sous-projet 5 (`feature/refonte-lua`).
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
| C5 | term_config.c:1548,1278,1457 | `[session] script_path` contenant une espace : écrit sans guillemets par GKeyFile, rejeté par parsecfg ; trois chemins lancent `cfgParse` sans retirer les groupes GKeyFile. | **vérifié** | Au démarrage « Cannot read configuration file », valeurs par défaut ; charger/enregistrer échouent ensuite. Corrigé par le sous-projet 3 (`feature/refonte-configuration`). |
| C6 | term_config.c / parsecfg.c:913 | `strcmp` côté term_config, `strcasecmp` côté parsecfg. | **vérifié** | Enregistrer « Default » alors que « default » existe → deux sections → `CFG_USED_SECTION` → fichier illisible définitivement. Corrigé par le sous-projet 3 (`feature/refonte-configuration`). |
| C7 | term_config.c:2209-2241 | `.gtktermrc` vide → tableaux parsecfg NULL → `cfgStoreValue(...,0)`. | **vérifié** | Premier changement de couleur → erreur de segmentation. Corrigé par le sous-projet 3 (`feature/refonte-configuration`). |
| C8 | macros.c:86,120 | `strlen(NULL)` sur une macro sans libellé. | haute | « Ajouter », capturer un raccourci, OK, presser le raccourci → plantage. |
| C9 | macro_panel.c:565,911 | `rebuild_macro_buttons` détruit les pages sans vider `macro_button_table`/`macro_polling_table`. | haute | Masquer un onglet contenant une macro en polling, ou recharger un `.ini` → accès après libération ; l'ancien polling envoie la macro *i* du nouveau fichier avec les anciens arguments. |
| C10 | script_engine.c:575-718 | Le fil Lua parcourt `macros` sans verrou pendant que le fil principal la libère/réalloue. | haute | Modifier les macros ou recharger le `.ini` pendant un script → accès après libération. |
| C11 | script_engine.c:379,419 | `send_serial` appelé depuis le fil Lua → `vte_terminal_feed` (écho) et `clear_buffer()` hors fil GTK. | **vérifié** | Scripts 08/09 avec écho actif. |
| C12 | term_config.c:654 | Dialogue Port non modal avec `static Combos[]`. | haute | Ouvrir A, ouvrir B, annuler B, valider A → lecture de widgets détruits. |
| C13 | term_config.c:1334 | Les groupes `window`/`colors`/`layout` apparaissent dans « Charger »/« Supprimer ». | haute | Charger « window » puis enregistrer → `[window]` en double → fichier illisible. Corrigé par le sous-projet 3 (`feature/refonte-configuration`). |

## 4. Défauts fonctionnels majeurs

**Transport / série / udev**
- M1. `transport_close` décide du type de fermeture avec la *nouvelle* configuration (term_config.c:1082 modifie `config` puis rouvre) : `tcsetattr` sur descripteur fermé, termios non restauré ; double `close` en mode serveur (`transport_fd == listen_fd`).
  Corrigé par le sous-projet 2 (`feature/refonte-transport`).
- M2. Le moniteur udev ignore le mode de transport (débrancher l'ancien USB ferme une connexion TCP) et n'est démarré qu'une fois (passage TCP → série sans surveillance).
  Corrigé par le sous-projet 2 (`feature/refonte-transport`).
- M3. Reconnexion automatique : boîte modale toutes les 2 s ; le minuteur se réarme même après `transport_close` ; en RS485 sur pty, `perror` toutes les 100 ms.
  Corrigé par le sous-projet 2 (`feature/refonte-transport`).
- M4. Double ouverture au démarrage avec reconnexion automatique (deux boîtes d'erreur si le port est verrouillé).
  Corrigé par le sous-projet 2 (`feature/refonte-transport`).
- M5. TCP client : fin de flux → port fermé sans mise à jour de l'état ; état « tcp://… » affiché pendant la connexion.
  Corrigé par le sous-projet 2 (`feature/refonte-transport`).
- M6. Lien utilisateur hors udev (`~/x -> /dev/ttyACM0`) : les événements ne correspondent jamais (comparer `st_rdev` serait robuste).
  Corrigé par le sous-projet 2 (`feature/refonte-transport`).
- M7. `gchar car` comparé à -1 (term_config.c:949, files.c:198, transport.c:98) : sur ARM (char non signé), « attendre un caractère » toujours actif ; au-delà de 127 jamais reconnu.
- M8. `gtkterm-update.sh` : `disown` n'existe pas sous dash ; `pkexec ninja install` laisse des fichiers root dans `build/` ; `gtkterm &` relance celui du PATH.

**Configuration**
- M9. parsecfg fait `setlocale(LC_ALL,"C")` (parsecfg.c:595,1258) sans restaurer : interface plus traduite après la première lecture ; `strtod` des macros fonctionne *grâce* à ce couplage caché.
  Corrigé par le sous-projet 3 (`feature/refonte-configuration`).
- M10. `save_config_silent` (chargement d'un `.ini` de macros, URL de mise à jour) persiste les options de ligne de commande (`-p`, `-s`) dans le profil actif.
  Corrigé par le sous-projet 3 (`feature/refonte-configuration`).
- M11. `remove_section` : `FILE*` jamais fermé, recherche de `[nom]` non ancrée en début de ligne (`socket_host = [::1]` tronque), décalage d'un octet en fin de fichier, réécriture non atomique.
  Corrigé par le sous-projet 3 (`feature/refonte-configuration`).
- M12. Erreur de `g_key_file_load_from_file` ignorée → risque de réécrire le fichier avec seulement `[window]` (perte des profils) — non vérifié.
  Corrigé par le sous-projet 3 (`feature/refonte-configuration`).
- M13. `~/.config` absent : `g_file_move`/`cfgDump` échouent en silence alors que « a été créé » s'affiche ; `getenv("HOME")` NULL non géré.
  Corrigé par le sous-projet 3 (`feature/refonte-configuration`).
- M14. `rows`/`columns` inversés (rows=80, columns=25), compensé par hasard ; `visual_bell` défaut TRUE ou FALSE selon le chemin, jamais appliqué.
  Corrigé par le sous-projet 3 (`feature/refonte-configuration`).

**Macros**
- M15. Raccourci clavier et polling envoient l'action **sans expansion** (`%d` littéral, `%%` reste `%%`) — trois chemins d'envoi divergents (`macros.c:69`, `macros.c:99`, `script_engine.c:656`).
- M16. `Save_shortcuts` restaure arguments et polling **par position** : supprimer, déplacer ou trier transfère le polling à une autre commande.
  Corrigé par le sous-projet 4 (`feature/refonte-macros`).
- M17. Raccourcis connectés deux fois (`term_config.c:1667` puis `gtkterm.c:78`) → double envoi probable ; pas de contrôle de bornes dans `shortcut_callback`.
- M18. `%#x`, `%#o`, `%#08x` lus comme la liste « x ».
- M19. Antislash échappé *avant* printf : `%5s` avec `a\b` donne 4 octets ; `%c` avec `\` donne 0A ; `%.1s` coupe `\\`.
- M20. Libellés : `arr[0] %d` → `arr5` ; `XY[a%%]%d` → `X5` ; `50% off` lu comme `% o` → `5011ff`.
- M21. Lecture `.ini` : séparateurs `::`, `|`, `:`, `=` non échappés ; `GError` réutilisé ; fuite de `macro_keys` ; sans `[macros]`, anciennes macros conservées mais listes remplacées.
  Corrigé par le sous-projet 4 (`feature/refonte-macros`).
- M22. Période de polling appliquée et sauvegardée à chaque frappe (taper 2000 passe par 2 ms) ; minuteur à 1 ms permanent ; `period_ms*1000` déborde au-delà de ~71 min.
  Corrigé par le sous-projet 4 (`feature/refonte-macros`).
- M23. `%x` analysé en base 0 : `ff` invalide envoie `0`, `010` donne `8`.
- M24. Aide de la fenêtre des macros obsolète depuis 7a697b2 (annonce encore `\0A`, `\0FF`).

**Lua**
- M25. « Stop » n'arrête pas le script (seuls `wait_for`/`sleep` reviennent) ; `while true do end` ne s'arrête jamais (aucun `lua_sethook`) ; `os.exit` ferme GTKTerm ; pas de `lua_atpanic`.
  Corrigé par le sous-projet 5 (`feature/refonte-lua`) : crochet Lua (`lua_sethook`) relevé après un `pcall`, `sleep` et attentes du moteur interruptibles, `os.exit` termine le script, `lua_atpanic` installé, exécution sous `lua_pcall` ; essais `test_script_engine` et `test_script_arret.py`.
- M26. `wait_for` s'arrête au premier octet nul (`g_strstr_len`, `lua_pushstring`) : protocoles binaires impossibles.
- M27. Ordre non garanti : `send_macro` est différé (idle), `send` direct ; `send_macro` renvoie vrai avant l'envoi.
- M28. Tampon de réception plein : bloc jeté en silence ; débits limités à 9 valeurs avec repli silencieux sur 9600 ; écritures partielles/EAGAIN non reprises ; `send_hex` ignore les caractères invalides.
  Corrigé par le sous-projet 5 (`feature/refonte-lua`) : file de 1 Mio (`GtRxQueue`) qui jette les plus anciens octets et le signale une fois par port et par exécution ; débits du transport (non standard compris), débit impossible refusé ; écritures partielles et `EAGAIN` repris jusqu'au délai ; `send_hex` refuse un texte invalide sans rien envoyer.
- M29. Exemples faux : `10_firmware_update.lua:71,93` (`wait_for` renvoie les données motif inclus, priorité de `..` sur `or`) → plante via C1 ; `08` délai trop court ; commentaire faux dans `09`. `SKILL.md` de `/lua-script` ne correspond pas à l'API réelle (`log`, `send`, `open tcp_server`, `clear`, `save_csv`).
- M30. Le `GThread` n'est jamais joint (fuite à chaque exécution).

**Interface / tampon / fichiers / mise à jour**
- M31. Journal dupliqué : `set_view()` rejoue le tampon et l'affichage appelle `log_chars` → changer de vue réécrit jusqu'à 128 Ko dans le journal ; en vue hexa le journal contient le texte « XX ».
- M32. Avec « ESC efface l'écran », tout envoi d'un octet unique 0x1B est avalé (terminal_display.c:116) — hexa, macro, Lua, fichier.
- M33. `GIT_REVISION` figé par `run_command` à la configuration meson : après de nouveaux commits + `ninja`, mise à jour proposée en boucle ; le test est « différent », pas « plus récent ».
  Corrigé par le sous-projet 6 (`feature/refonte-gtk4`) : révision recalculée à chaque construction (`vcs_tag`, sous-projet 0) ; une révision distante
  n'est proposée que si elle diffère et n'est pas plus ancienne que la locale (`git merge-base --is-ancestor` dans la copie du cache) ; « unknown » jamais au démarrage.
- M34. update.c : pointeur pendant si on ferme « À propos » pendant `git ls-remote` ; pas de `GCancellable` ni `GIT_TERMINAL_PROMPT=0` ; URL passée sans `--` ; `'%s'` non échappé dans la commande ; « Ignorer cette version » oublié au redémarrage.
  Corrigé par le sous-projet 6 (`feature/refonte-gtk4`) : vérification annulable (`GCancellable`, annulée à la fermeture de la fenêtre « Updates »), script lancé
  par une liste d'arguments (`g_spawn_async`, plus de `sh -c`), « Skip this version » gardé dans `[general] update_skipped_revision`.
- M35. files.c : rappel `ecriture` typé `void` enregistré comme `GIOFunc` → valeur de retour aléatoire, transfert bloqué ; `read()` -1/0 → boucle à 100 % ; lecture `buffer[bytes_read]` hors données ; `GIOChannel` jamais libéré ; « Save ASCII » supprime ≥ 0x80 et TAB.
  Corrigé par le sous-projet 6 (`feature/refonte-gtk4`) : envoi de fichier dans le cœur (`GtFileSender` : fichier vide, minuterie retirée à l'annulation, perte
  de liaison) ; « Save ASCII » garde TAB, CR, LF et les octets ≥ 0x80 (`capture.c`) ; le reste l'était au sous-projet 1.
- M36. Les `GAction` ne sont jamais ajoutées à un `GActionMap` : le bouton radio ASCII/Hexa se désynchronise.
  Corrigé par le sous-projet 6 (`feature/refonte-gtk4`) : `GtkApplication`, toutes les actions `app.*` dans son `GActionMap`, cases et radios en `GPropertyAction`
  sur l'état réel ; essais `test_menus_actions.py` et `test_options_profil.py`.
- M37. Écho : en vue texte une ligne `> a\r\n` par touche ; en vue hexa l'index dérive ; l'écho n'est pas dans le tampon et disparaît au changement de vue.
  Corrigé par le sous-projet 2 (`feature/refonte-transport`).
- M38. Chaîne de format non maîtrisée (`show_message`, `search.c:75`) : un `%` dans un chemin ou `strerror` → comportement indéfini.
  Corrigé par le sous-projet 6 (`feature/refonte-gtk4`) : `show_message` supprimé ; messages par `gt_app_message` (format vérifié par `G_GNUC_PRINTF`), erreur
  de recherche affichée dans la barre de recherche.
- M39. Journal : le dialogue demande « écraser ? » mais ouvre en `"a"` ; menus bloqués après un échec ; boîte modale dans le chemin de réception.
  Corrigé par le sous-projet 6 (`feature/refonte-gtk4`) : le journal tronque le fichier choisi ; une erreur d'écriture l'arrête et passe par une boîte non
  bloquante, hors du chemin de réception (`GtRxLogger`).
- M40. `Put_temp_message` retire le sommet de la pile de la barre d'état → efface le nom du port.
  Corrigé par le sous-projet 6 (`feature/refonte-gtk4`) : barre d'état sans pile (`status_bar.c`) : le message temporaire remplace le texte d'état le temps voulu
  puis le rend ; essai `test_status_bar.py`.

**Constats du socle de tests (sous-projet 0, sous ASan)**
- M41. `try_parse_label` (macros_format.c) allouait le libellé même quand l'appelant passe `label_out = NULL` (`format_action_with_args`) : fuite à chaque expansion d'une macro à libellé `[…]`. **Corrigé au sous-projet 0** (correctif trivial sans changement de comportement, spec §2.4), détecté par LeakSanitizer sur `test_macros_format`.
- M42. Une ligne ajoutée dans la fenêtre des macros sans libellé ni raccourci donne une entrée `label`/`shortcut` NULL : `get_shortcuts` (s'arrête sur `shortcut||label`) tronque le tableau à cette ligne, `add_shortcuts` et `macros_destroy` (s'arrêtent sur `shortcut`) ne connectent pas les raccourcis suivants et fuient. Racine de C8. Traité au sous-projet 1.
- M43. Un raccourci clavier de macro envoie la macro **puis** la touche elle-même au terminal (Ctrl+F5 → `<macro>` suivi de `ESC[15;5~`) : `shortcut_callback` est connecté par `g_cclosure_new_swap` et renvoie `void`, donc GTK ne considère pas l'accélérateur comme traité et propage la touche à VTE. Antérieur à la branche (présent sur master), révélé par les essais automatisés EM-5 du sous-projet 1. Correctif : rappel renvoyant `gboolean` TRUE. À traiter au sous-projet 4 (macros) ou en correctif isolé.
- M44. « Capture Shortcut » et « Delete Shortcut » de la fenêtre des macros appellent `gtk_tree_selection_get_selected`, inopérant quand la liste est en sélection multiple (son mode) : les deux boutons ne font rien. Trouvé à la conception du sous-projet 4. Corrigé par le sous-projet 4 (`feature/refonte-macros`) : première ligne sélectionnée par `gtk_tree_selection_get_selected_rows` ; essai graphique `test_macros_edition.py`.
- Note (essais EM-1, sous-projet 1) : masquer un onglet arrête tous les pollings, y compris ceux des onglets restés visibles (`rebuild_macro_buttons` vide la table de polling — correctif C9) ; aucun ne reprend seul, un clic relance le bon bouton. À revoir avec la refonte du polling (sous-projet 4) — traité par le sous-projet 4 : masquer un onglet n'arrête que ses pollings ; la reconstruction du panneau ne touche plus à l'ordonnanceur.
- Note (relecture du sous-projet 0) : `git -c safe.directory=*` n'est respecté qu'à partir de git 2.38 ; accepté, puisque l'installation se fait par `meson install --no-rebuild` et ne relance plus git en root.

## 5. Défauts mineurs (sélection)

`prctl(PR_CAP_AMBIENT_RAISE)` inopérant pour `bind` ; `-T serial` non reconnu, `none` impossible pour `--parity/--flow`, `--help` sort en 1 ; `%.15s` tronque les chemins longs ; branche non-Linux de baud.c ne compile pas ; `metro-tty-index` peut laisser un lien orphelin vers un autre appareil ; fuites (`get_config_file_path` déclarée `const` mais allouée, tableaux parsecfg, police, `out_buffer`, `logfile_default`, `lists_model`, fermetures de raccourcis) ; `tooltip[256]` coupe l'UTF-8 ; `sscanf("%02X")` accepte « 123 » ; `fic_defaut` partagé entre envoi et sauvegarde ; titre « Main Window » pour le dialogue terminal ; « Close », « Delete Shortcut » non traduits ; icône 48×48 installée deux fois ; `g_print("Config_macros called")` oublié.
— Corrigés par le sous-projet 6 (`feature/refonte-gtk4`) : `-T serial`, `none` pour `--parity`/`--flow` et `--help` qui sort en 0 (`cmdline.c`) ;
`sscanf("%02X")` (`hex_input.c`) ; `fic_defaut` partagé (dernier fichier propre à chaque action) ; fuite de `logfile_default` (`rx_logger.c`) ; titre
« Main Window » (dialogue « Terminal ») ; « Close » et « Delete Shortcut » traduits.

## 6. Code mort et duplication

- **Code mort** : ~500 lignes de parsecfg (mode `CFG_SIMPLE`, `fetchVarFromCfgFile`, types inutilisés) ; `serial_port_fd`, `HAVE_LINUX_SERIAL_H` ; `Envoie_car`, `blank_data`, `Text/buffer/iter` (interface.c), `adj` (files.c) (supprimés avec l'interface GTK 3, sous-projet 6) ; `send_macro_with_arg`, `macro_get_format_type`, `macro_has_format_arg`, `macro_get_format_types`, `macro_list_remove_entry`, `macro_stack` ; `L_global`, `chan_err` (Lua) (retirés par le sous-projet 5) ; clé `show_rxtx_compat` ; `i18n.c` largement redondant avec `g_print`.
- **Duplication** : `save_config_silent` ≈ `really_save_config` ; `save_raw_file` ≈ `save_ascii_file` (remplacés par capture.c, sous-projet 6) ; `transport_setup_watches` ≈ `transport_setup_client_watches` ; quatre boucles de balayage dans `macros_format.c` ; gestionnaires Add/Delete/Move et `*_edited` recopiés dans le dialogue des macros ; termios/TCP du moteur Lua ≈ transport.c (retiré par le sous-projet 5) ; prototypes d'affichage dans `interface.h` et `terminal_display.h` (supprimés, sous-projet 6) ; motif `g_strdup_printf` + `show_message` + `g_free` ×10 (remplacé par gt_app_message, sous-projet 6).

## 7. Testabilité

- **Testable tout de suite** : `macros_format.c` (échappements, expansion, validation, infos d'arguments), `macros_list.c`, `baudrates.c` (+ en-tête généré), `set_port_baudrate` sur `openpty`, `parsecfg.c` (avec bouchon `i18n_fprintf`).
- **Après petite extraction** : construction termios pure (régression C4), correspondance udev, texte d'état, analyse de la ligne de commande, formatage hexa d'une ligne, décodage de la saisie hexa, décision de mise à jour, tampon (avec horloge et puits d'affichage injectés), sérialisation `.ini` des macros, décision de tir du polling.
- **Intégration** : transport + serial + baud compilés avec des bouchons (`show_message`, `Set_*`, `put_chars`…) contre une paire pty et la boucle locale TCP ; moteur Lua en exécutable sans interface avec un faux port principal, scripts de test `tests/lua/*.lua`.
- **Cadre** : GLib GTest + `meson test` (GLib 2.80, meson 1.3.2 présents). `G_TEST_OPTION_ISOLATE_DIRS` pour les tests de configuration. Sanitizers `-Db_sanitize=address,undefined` et `thread`. `valgrind` et `socat` sont installés ; ni `xvfb-run` ni `weston` (broadwayd présent).
- **Prérequis meson** : une `static_library` cœur partagée par l'exécutable et les tests.

## Remarques mineures reportées par le sous-projet 1 (relectures par tâche et finale)

À reprendre dans les sous-projets 2 à 6 selon le module concerné. Origine : registre d'exécution du plan `2026-10-06-correctifs-critiques`.

- **Tâche 1** : test exhaustif ne contrôle pas IXOFF/CLOCAL/CREAD/c_oflag/c_lflag par combinaison ; ordre alphabétique core_sources ; CLOCAL désormais posé aussi en flux 1/2 et bits hors 5-8 → CS8 (conforme spec, à mentionner)
- **Tâche 2** : stub_display accesseurs stub_sent/stub_received/stub_send_thread sans verrou (initialisation paresseuse) ; stub_last pointe dans le journal libéré par stub_reset ; prototype stub_display_reset local au .c ; entrée vide dans Lis_Config → car=0 (préexistant, incohérent avec wait_char 0 = aucun) ; test TCP fondé sur des temporisations ; commentaire sur la perte du 0xFF hérité
- **Tâche 3** : newlocale peut renvoyer (locale_t)0 (ENOMEM) → freelocale(0) ; newlocale/freelocale par valeur lue (surcoût) — même motif à reprendre dans macros_format (Task 4) et script_engine (Task 8)
- **Tâche 4** : %Lf lu par g_ascii_strtod (double) puis converti en long double → précision au-delà de ~16 chiffres perdue alors que la validation autorise 19 chiffres pour L (choix du plan) ; %Lf/%e/%f nu non testés sous fr
- **Tâche 5** : libellé contenant un spécificateur ([%d] %d) → longueur retirée fausse (préexistant) ; ligne %x/ff fige le repli 0 ; pas de test d'une liste nommée « x » masquant le drapeau, ni de -0x10 / blancs avant préfixe
- **Tâche 6** : (1) Load_configuration_from_file affecte active_config_name avant de trouver le profil → `gtkterm -c session` puis save_config_silent crée/empile des [session] parsecfg (nom réservé non refusé sur ce chemin) ; (2) enregistrer « Default » sur [default] renomme la section à la casse saisie (réutiliser cfgSectionNumberToName) ; (3) copie temporaire de .gtktermrc en 0666&umask (g_file_set_contents) au lieu d'écrire dans le fd 0600 ; (4) erreurs parsecfg citent le chemin temporaire, lignes décalées, échec tmp mal libellé ; (5) anciens profils nommés [Session]/[Window]/[Colors] désormais inaccessibles (à mentionner) ; (6) strip ignore l'échec d'écriture (doublon possible) ; (7) préexistant : `if(max == cfgParse(...))` ne détecte pas -1 (term_config.c:1085/1342) — points 1 à 7 traités par le sous-projet 3 (`feature/refonte-configuration`) : nom actif retenu seulement après avoir trouvé le profil, remplacement sans changement de casse, enregistrement atomique en 0600 par GKeyFile, plus de fichier temporaire ni de parsecfg ; (5) documenté : un ancien profil devient `[profile:…]`, sauf s'il porte le nom d'un groupe réservé (`window`, `colors`, `layout`, `session`)
- **Tâche 7** : test M17 sauté sans affichage (piste broadway/xvfb) ; arguments enregistrés moins nombreux que les spécificateurs non testés ; taille de tab[4] superflue
- **Tâche 8** : test « envois… » ne vérifie pas stub_get_shortcuts_thread (send_macro synchrone sur le thread Lua passerait) ; set_tap_func/unset_tap_func encore appelés depuis le thread Lua (préexistant ; traité par le sous-projet 2 : le moteur Lua se connecte au signal `received` sur le fil principal) ; courses préexistantes lua_port_close vs port_read_cb/tcp_accept_cb, g_source_remove sur source déjà retirée (traité par le sous-projet 5 : ports sur GtScriptPort, transport fermé sur le thread principal) ; famine possible de l'idle sous flux continu (priorité DEFAULT_IDLE) ; pas de garde contre run_on_main_sync appelé du thread principal (traité par le sous-projet 5) ; fermeture de l'appli pendant run_on_main_sync → thread abandonné (pas d'interblocage) (traité par le sous-projet 5 : script_engine_shutdown) ; fuites sur erreur mémoire Lua (longjmp) (reporté, hors périmètre du sous-projet 5)
- **Tâche 9** : descripteurs non fermés si un g_assert échoue après la création du répondeur ; ttyname (tampon statique) utilisé directement
- **Tâche 10/11** : Set_status_message dépile encore le sommet (préexistant ; si un message temporaire est au sommet, l'ancien nom de port reste dessous, affichage correct) (traité par le sous-projet 6 : barre d'état sans pile) ; transfer_msg_id ne suit pas deux envois simultanés (inatteignable, modal) ; config_name non échappé dans un balisage Pango (term_config.c:1390, noms alphanumériques) (traité par le sous-projet 6 : plus aucun balisage dans les messages) ; fuite de logfile_default (préexistant) (traité par le sous-projet 6 : rx_logger.c)
- **Tâche 12/13** : polling_timer_callback itère une copie des valeurs ; un rebuild déclenché par l'envoi libérerait les ps restants (préexistant, improbable) ; on_polling_mode_toggled avec btn NULL appelle update_button_appearance (tolérance à ps->button NULL non vérifiée, préexistant)
- **Tâche 14/15** : autres écrivains de .gtktermrc (term_config.c save_config_silent/really_save_config via config_file_strip/restore) à vérifier pour le motif M12 (traité par le sous-projet 3 : seul `GtConfigStore` lit et écrit `.gtktermrc`, un fichier illisible le met en lecture seule) ; écho local journalisé via put_hexadecimal (traité par le sous-projet 2 : l'écho vient de l'historique TX, le journal a son propre `GtRxTransform`) ; get_config_file_path fuit (préexistant) (traité par le sous-projet 3 : `get_config_file_path` a disparu, le chemin vient de `config_file_prepare` et appartient au magasin) ; message g_printerr non traduit
- **Tâche 16** : files.c chemins « current_buffer == NULL » et erreur d'écriture série affichent avant close_all (ré-entrée possible, préexistant) ; écriture partielle avec lf_found déclenche le délai trop tôt (préexistant) (traité par le sous-projet 6 : files.c remplacé par GtFileSender)
- **Tâche 17** : lua-syntax.sh sans .lua → glob littéral (échec franc) ; code 77 sans message
- **Relecture finale** : journal Lua à G_PRIORITY_DEFAULT — un script qui journalise en boucle serrée peut figer le rafraîchissement (Stop reste opérant) (traité par le sous-projet 5 : journal regroupé à G_PRIORITY_DEFAULT_IDLE, plafonné à 1 Mio) ; gtkterm -c <inconnu> n'enregistre plus dans [inconnu] mais dans le profil précédent (voulu) ; lists_model fuit à chaque ouverture de la fenêtre des macros (traité par le sous-projet 4) ; clé .ini sans valeur → entrée à libellé NULL (sentinelle) (traité par le sous-projet 4) ; fuite g_path_get_dirname (load_macros_file_callback) (traité par le sous-projet 4) ; traductions po/ à régénérer (msgid de l'aide modifié)
- **Sous-projet 2** : journal dupliqué au rejeu de `set_view()` (suppression de `logging_set_suspended`) — traité par le sous-projet 2 (le journal est alimenté par le signal `received`, jamais par le rejeu de l'affichage).


## Remarques mineures reportées par le sous-projet 2 (relectures par tâche et finale)

Non bloquantes à la fusion (triage de la relecture finale de branche) ; à reprendre dans le sous-projet concerné.

- **Historique** (`history.c`) : la capacité compte les octets utiles seulement ; en réception octet par octet le surcoût (en-tête + nœud de liste) porte le pire cas à ~10 Mo. Envisager une coalescence des enregistrements RX consécutifs. Cas `len == 0` et éviction multiple en un ajout non testés.
- **Transformation** (`rx_transform.c`) : `cr_received` reste vrai si le CR/LF automatique est désactivé avec un CR en attente (parité avec l'ancien `put_chars`) ; cas de test supplémentaires possibles (CRCR + horodatage).
- **Rendu** (`view_render.c`) : après une ligne d'écho « > », la `GtRxTransform` n'apprend pas le retour à la ligne : la ligne reçue suivante peut manquer son horodatage. Pas de test direct d'un envoi hexadécimal sur plusieurs lignes.
- **Correspondance udev** (`device_match.c`) : pas de test « numéro différent mais chemin égal ».
- **Transport** (`transport.c`) : `transport_accept_cb` cesse d'accepter sans événement sur EINVAL/EBADF et boucle sur `i18n_perror` en cas d'erreur persistante (EMFILE) — préexistant.
- **Liaison** (`link.c`) : changer `reconnect-interval-ms` pendant la minuterie n'agit qu'au prochain démarrage ; « device removed » n'est jamais affiché ; après une ouverture échouée passée en reconnexion, décocher l'option affiche « — lost » pour un port jamais ouvert ; cocher l'option après un échec fait option décochée ne relance rien. Tests absents : reconnexion du client TCP, bascule de l'option pendant une tentative TCP, reconnexion du serveur. Fragilités des tests (`port_libre`, `g_dir_make_tmp` non vérifié, répertoire temporaire laissé en cas d'échec).
- **Interface** : trois redessins de suite au rechargement d'une configuration (traité par le sous-projet 6 : un seul signal « changed » de GtTermOptions) ; « Clear scrollback » n'efface que VTE et un redessin ultérieur fait réapparaître le contenu (à trancher) (tranché par le sous-projet 6 : comportement conservé et documenté) ; `clear_display` déclarée deux fois (`interface.h`, `terminal_display.h`) (supprimée avec l'interface GTK 3, sous-projet 6).
- **Journal** (`logging.c`) : en pause, la transformation n'est pas nourrie ; l'état de ligne est périmé à la reprise. (traité par le sous-projet 6 : GtRxLogger nourrit la transformation pendant la pause)
- **Fichiers** (`files.c`) : retour de `fwrite` non vérifié dans les sauvegardes (échec silencieux sur disque plein) (traité par le sous-projet 6 : capture.c) ; écriture ASCII octet par octet ; chemin `current_buffer == NULL` de `ecriture` affiche avant de fermer (inatteignable) ; la minuterie de délai n'est pas retirée par `close_all` (traité par le sous-projet 6 : GtFileSender) — préexistant.
- **Macros** (`macro_panel.c`) : `update_button_label` ne libère pas le libellé alloué — préexistant (traité par le sous-projet 4).
- **Suite graphique** (`tests/gui/`) : lente (~3 à 4 min, chemins tapés touche par touche) ; `GTKTERM_GUARD_REAL_HOME` non vérifié non vide ; garde par préfixes littéraux ; processus orphelins possibles après un dépassement de délai (port broadway fixe 8117) (traité par le sous-projet 6 : port broadway propre à chaque scénario, processus tués par groupe) ; `timeout` et `pgrep` absents ne donnent pas un saut.

## Remarques mineures reportées par le sous-projet 3 (relectures par tâche et finale)

Non bloquantes à la fusion ; à reprendre avec les dialogues (sous-projet 6) ou les projets.

- **Noms de profil** : un nom fait d'espaces, ou contenant `[`, `]` ou un caractère de contrôle, est accepté par « Save configuration » (seul le nom vide est refusé) ; GKeyFile refuse alors le groupe. Doublon de casse écrit à la main : seul le premier profil est atteignable ; ordre indéterminé entre deux noms égaux à la casse près. (traité par le sous-projet 6 : gt_profile_name_validate, doublon sans casse = écrasement confirmé)
- **Fichier sans profil `default`** (mais avec d'autres profils) : « No section "default" » à chaque démarrage (comportement d'avant). (traité par le sous-projet 6 : valeurs par défaut, sans boîte)
- **Migration** : un ancien profil sans aucune clé repère (ex. seulement `bits`) est laissé tel quel ; `atoi` tolérant sur `transport_type`/`wait_char` non numériques ; un ancien groupe réécrit par une ancienne version de GTKTerm après la migration devient `nom (2)` sans nouvelle copie de sauvegarde.
- **Magasin** : fichier devenu illisible en cours de session → le chargement d'un profil affiche « No section » au lieu de la vraie cause (sur stderr seulement) ; `settings.update_url` n'est pas relu avec le magasin (traité par le sous-projet 6) ; deux fenêtres de course de quelques millisecondes entre instances (écriture entre le renommage et le `stat`, inode réutilisé dans le même tic) acceptées faute de verrou de fichier ; `put`/`delete` en mémoire conservés après un enregistrement échoué.
- **Interface** : File/Quit n'enregistre pas l'état de la fenêtre (préexistant) ; deux réécritures du fichier à la fermeture par la barre de titre. (traités par le sous-projet 6 : une seule fermeture, une seule écriture)
- **Tests** : `e.proc.wait(10)` muet en cas de délai dans `test_config_profils.py` ; références `#: ../src/parsecfg.c` des `.po`/`.pot` à régénérer (traité par le sous-projet 6 : .pot et .po régénérés).

## Remarques mineures reportées par le sous-projet 4 (relectures par tâche et finale)

Non bloquantes à la fusion ; à reprendre avec les dialogues (sous-projet 6), le moteur Lua (sous-projet 5) ou les projets.

- **Modèle** (`macro_set.c`) : branches non testées (`gt_macro_expand` avec `args == NULL` et `n_args > 0`, jeu `NULL` ; `apply_edits` sans ligne) ; compteur d'identifiants sans garde de dépassement (2³² macros, irréaliste).
- **Fichier** (`macro_file.c`) : un échappement invalide écrit à la main dans `args`, `displays` ou `values` d'un fichier v2 fait perdre la liste entière au prochain enregistrement, sans copie (le fichier reste un v2 lisible) ; le refus « une copie différente existe déjà » n'indique pas le remède (renommer ou supprimer la copie) ; une période « -5 » d'un ancien fichier est lue comme le maximum (comme l'ancien code) ; une liste sans entrée est écrite puis perdue à la relecture ; un fichier présent mais illisible (droits) fait échouer l'enregistrement avec le message de lecture de GLib.
- **Ordonnanceur** (`macro_poller.c`) : si le rappel d'état « en marche » arrête la macro, l'envoi immédiat a quand même lieu ; une macro en cours déplacée par la fenêtre d'édition dans un onglet masqué, ou dont le libellé est vidé, continue sans bouton pour l'arrêter (réafficher l'onglet ou remettre un libellé) ; assertions temporelles de `test_macro_poller` sensibles à une machine très chargée.
- **Interface** (`macros.c`, `macro_panel.c`) : `connected_group` et les fermetures des raccourcis ne sont pas référencés (le groupe vit tout le processus) (traité par le sous-projet 6 : plus de GtkAccelGroup) ; au démarrage, deux boîtes « Cannot load macros file » identiques possibles (profil `default` abîmé puis `--config`) (traité par le sous-projet 6) ; une action développée en 0 octet n'affiche plus « sent! » ; chaque `send_macro` Lua affiche « Macro "…" sent! » (une boucle serrée en empile) (traité par le sous-projet 6 : macros_send_quiet) ; clic ailleurs avec une période invalide : l'ancienne période est gardée sans retour visuel (traité par le sous-projet 6 : erreur affichée dans le popover) ; `on_polling_period_activate` presque mort à côté du gestionnaire de touches du menu ; déclaration anticipée en double de `destroy_widget_idle` (code mort supprimé avec le panneau GTK 3, sous-projet 6) ; pointeurs de noms d'onglets du menu des onglets empruntés au jeu (préexistant) ; « None », l'info-bulle « Shortcut: %s\nAction: %s » et « Delete Shortcut » non traduits (préexistant) (traité par le sous-projet 6) ; plusieurs instances sur le même fichier de macros : le dernier qui écrit gagne (préexistant).
- **Tests** : `stub_macros.c` recopie le contrat de `macros_send` pour `test_script_engine` ; ordre de `macros_replace_set` et réarmement de la boîte d'échec après un succès non testés ; appel de `macros_forget_save_error` dans `term_config.c` non testé ; `test_macros_send_affichage.py` sans `try/finally` (broadwayd orphelin en cas de dépassement) ni nombre minimal de cas (supprimé avec la suite GTK 3, sous-projet 6) ; attentes fixes dans `test_macros_edition.py` (traité par le sous-projet 6 : harnais GTK 4 sans attente fixe) ; `test_macros_migration.py` vérifie un seul des deux boutons de liste et l'argument après la migration ; `test_echec_ecriture_copie` peut laisser un répertoire en 0500 si une assertion échoue ; `test_link /perte-puis-reconnexion` instable sous ASan (préexistant, fichiers non touchés).
- **Cosmétique** : en-tête de `macros_list.c` (« Named value-list data model ») et `*/` mal aligné de `macros_list.h` ; ligne de ChangeLog de `gtkterm.c` reformulée. (traité par le sous-projet 6)

## Remarques mineures reportées par le sous-projet 5 (relectures par tâche et finale)

Non bloquantes à la fusion ; à reprendre avec les projets ou après un essai matériel. Les défauts M1, M4 à M6 et les points du registre corrigés par les vagues 1 et 2 de correctifs ne sont pas repris ici.

- **File de réception** (`rx_queue.c`, tâche 1) : dans `test_rx_queue.c`, les tests de fermeture et d'annulation pendant l'attente (`test_fermeture_pendant_attente`, annulation) laissent passer une pause fixe de 50 ms au lieu de garantir que l'attente a commencé (le cas « réveillé pendant l'attente » peut alors n'être pas exercé) ; leurs bornes temporelles (retour en moins de 5 s sans données, 500 ms après la fermeture, 100 ms après l'annulation) sont sensibles à une machine très chargée ; un `push` sur une file fermée n'est pas compté dans `dropped` (conforme à la spec).
- **`wait_for(0, …)` sur liaison perdue** : la file du port 0 n'est jamais fermée (décision 5 de la spec) : l'appel attend tout le délai au lieu de rendre `"closed"`. Conforme, à surveiller si l'usage montre le contraire.
- **Délai 0 de connexion TCP** : `open(h, "tcp_client", …, 0)` (ou un délai négatif) rend `false, "timeout"` avant toute résolution ou tentative ; comportement documenté, mais un délai nul n'a donc jamais « essayer une fois sans attendre » comme sens.
- **Avertissements de compilation** : `-Wcast-function-type` de `interface.c` (`control_signals_read`, rappel de minuterie) et quelques `-Wsign-compare` (`baudrates.c`, `term_config.c`) vus sous `build-asan`, corrigés par la vague 2 ; `-Wunused-function` de `iterer` dans `test_transport.c` retiré.
- **Message de perte après le lot** : le message d'un port perdu est journalisé après les octets déjà reçus du lot en cours, pas à l'instant de la perte.
- **Famine du vidage du journal** : la source de vidage est à `G_PRIORITY_DEFAULT_IDLE` (20 ms) ; une interface qui reste occupée à priorité supérieure (rafales de réception) retarde l'affichage du journal d'autant (le plafond de 1 Mio borne la mémoire).
- **`run_on_main_sync` hors itération** : le thread principal exécute les demandes en attente depuis une source d'inactivité à `G_PRIORITY_DEFAULT` ; une boucle d'événements qui n'itère pas (dialogue modal imbriqué sans itération, thread principal bloqué) les retarde. À l'arrêt de GTKTerm, `script_engine_shutdown` les exécute lui-même. (traité par le sous-projet 6 : plus aucune boucle imbriquée dans l'interface)
- **Journal plafonné** : `test_journal_plafond` dépend d'un délai (vidage à 20 ms) pour observer la ligne « [N bytes of log output dropped] » ; sensible à une machine très chargée.
- **Style** : l'énumération de `script_engine.c` en tabulations au milieu d'un fichier en 4 espaces ; couverture de `print` avec un `__tostring` qui lève, ajoutée par la vague 2.
- **Finaliseurs** : une boucle Lua pure dans un `__gc` n'est pas interruptible (Lua coupe les crochets pendant un finaliseur) ; GTKTerm abandonne alors le thread au bout de 3 s à la fermeture.
- **Durée de fin de script** : la vidange série avant fermeture peut durer jusqu'à 2 s par port série encombré (échéance unique, marge de FIFO comprise) ; un script qui ouvre plusieurs ports série encombrés cumule ces durées.
- **Liaison principale** : le transport du port 0 résout encore son nom (`getaddrinfo`) sur le thread principal ; seule la résolution des clients TCP des ports Lua a été déplacée sur le thread Lua.
- **Arguments des méthodes de fichier** : une erreur d'argument sur `f:read('x')` rend `bad argument #2` (l'objet fichier compte) alors que Lua standard dirait `#1` ; seul le numéro diffère, le nom et la position sont ceux du script.
- **Tests non déterministes** : `test_client_parti_pendant_envoi` (départ d'un client TCP pendant un envoi du serveur) passe aussi sans la correction quand le transport détecte la fermeture avant l'envoi ; la garde d'arrêt de `main_try_send_cb` (dernier `gtkterm.send` en file après « Stop ») n'a pas de test déterministe (course entre la demande d'arrêt et l'exécution de la demande en file).
- **Essai matériel recommandé de la vidange** : avec un adaptateur USB à puce FTDI ou CP210x puis avec un port 8250 natif, à 9600 bauds, un script dont la dernière ligne est `gtkterm.send(1, "RESET\r\n")` (sans `close` ni `wait_for`) : vérifier que l'équipement reçoit la trame entière (la vidange ne voit que `TIOCOUTQ` et une marge de 64 octets pour la FIFO). Les adaptateurs à gros tampon interne peuvent émettre la fin au débit restauré ou la perdre.
- **Tests, `getaddrinfo` simulé** : `tests/test_util.h` chaîne deux résultats `getaddrinfo` libérés par un seul `freeaddrinfo`, ce qui n'est valable qu'avec la glibc (libération nœud par nœud) ; à revoir pour musl.
- **`std_guard` et erreur mémoire** : une `LUA_ERRMEM` de la fonction d'origine est relancée comme erreur d'exécution, et l'ajout de la position alloue de nouveau ; sans conséquence pratique.
- **`send_hex`** : la distinction « caractère invalide » / « nombre impair » s'appuie sur le texte du décodeur (`strncmp(err->message, "invalid character", 17)`), non traduit mais couplé ; un code d'erreur dédié serait plus sûr.

## Remarques mineures reportées par le sous-projet 6 (relectures par tâche et finale)

Non bloquantes à la fusion ; à reprendre avec les onglets (sous-projet 6b), les projets (6c) ou au fil de l'eau. Les points corrigés par la vague finale
(messages d'erreur doublés, FIFO à l'envoi de fichier, `test-activate` synchrone, image forcée du crochet de test, processus orphelins du harnais, garde du faux
terminal) ne sont pas repris ici.

- **Disposition par défaut** : à 750 px avec le panneau de macros visible, le terminal n'a que 63 colonnes, une ligne hexadécimale de 16 octets (64 colonnes) se
  replie ; `test_hex_index` tourne en 1000 px. Comme en GTK 3 ; à revoir avec les onglets (taille par défaut ou panneau replié au premier lancement).
  **Traité par le sous-projet 6b (taille par défaut 1000 × 600).**
- **Sélecteur de fichiers (GTK 4.14)** : `GtkFileDialog` ouvre le dossier du fichier proposé mais ne le présélectionne pas (constaté sous broadway ; à confirmer
  sur un vrai bureau).
- **Configuration et fenêtre** (`app_config.c`, `app.c`) : profil resté en mémoire après un enregistrement échoué ; `[window]` lu au démarrage sans relecture
  préalable ; deux relectures successives à la fermeture (une autre instance qui écrit entre les deux, fenêtre de quelques µs, ferait perdre `[colors]`/`[layout]`
  /`[session]` posés en mémoire) ; aucun scénario ne compte les écritures de `.gtktermrc` à la fermeture (le code n'en fait qu'une).
- **Cœur** : capture écrite sur place (troncature avant écriture) ; envoi de fichier : `read` non borné au total annoncé (fichier qui grandit), fichier raccourci
  annoncé comme succès, `EINTR` non réessayé, conversion EAGAIN → réessai non testée ; journal ouvert sans `O_CLOEXEC` (sans effet : les lancements ferment les
  descripteurs) et `fflush` à chaque réception ; vérification de mise à jour sans délai maximal sur `git ls-remote` (la fenêtre reste « Checking… » jusqu'à sa
  fermeture).
- **Ligne de commande** : bornes de `-x`/`-y` (0-500) non alignées sur la validation du profil ; arguments positionnels ignorés sans message.
- **Interface** : capture de raccourci non annulée par un changement de page de « Configure Macros » ; conflits de mnémoniques du dialogue Port (`_Baud`/`_before`,
  `P_arity`/`_Advanced`, `_Flow`/`a_fter`) ; « No macros defined » quand tous les onglets sont masqués ; focus après un choix de liste déroulante ; menu des onglets
  qui garde l'ancien jeu s'il reste ouvert pendant un changement de jeu ; recherche sans occurrence sans retour visuel ; panneau de script : position du séparateur
  tronquée au changement d'orientation, schéma réécrit à chaque rafraîchissement, Ctrl+Z qui vide l'éditeur au démarrage ; `goto` par-dessus des déclarations
  initialisées dans `update_window.c`.
- **Harnais** : faux navigateur (`sendall` sans délai sous verrou, `close()` sans `join`, keysyms hors Latin-1) ; garde-fou sans `__open_2`/`creat` ; après un
  Ctrl+C sur meson, le nettoyage d'un scénario (quelques secondes) peut encore chevaucher une relance immédiate ; aucun scénario ne vérifie qu'aucune action
  `test-*` n'existe sans `GTKTERM_TEST_HOOKS`.
- **Couverture graphique manquante** : fermeture pendant un envoi de fichier ou un polling actif ; macro envoyable consommée port fermé ; avertissement de débit non
  standard ; dans « Configure Macros » : Add, édition de cellule, page Lists ; panneau de script : Save, menu de disposition, Annuler/Échap des couleurs ; URL vide
  et échec de la vérification manuelle de mise à jour.
- **Installation** : un `sudo meson install` manuel par-dessus une installation GTK 3 laisse l'ancien lanceur `gtkterm.desktop` (le script de mise à jour le
  retire ; le README le signale).
- **Hors périmètre, relevé** : licence incohérente (À propos : LGPL 3, `--help` : « GPL V.2 », `COPYING` : GPL 3), antérieure à la branche.

## Remarques mineures reportées par le sous-projet 6b (relectures par tâche et finale)

Non bloquantes à la fusion ; à reprendre avec les projets (6c) ou au fil de l'eau. Les points marqués « traité » l'ont été par la vague finale de
correctifs de la branche `feature/refonte-onglets`.

- **Relais d'actions** (`action_relay.c`) : branches « valeur flottante » mortes dans `relay_activate`/`relay_change_state` ; `set_target` ne vérifie pas que
  le type de paramètre et d'état de l'action cible est celui du relais ; `test_action_relay` ne couvre ni un relais désactivé ni une valeur flottante.
- **Macros et Lua** : `test_macros_send` n'essaie pas `replace_set` après `gt_macros_shutdown` ; le cas `/moteur/arret-sert-l-autre` ne prouve pas que l'arrêt
  d'un moteur exécute les demandes de l'autre (nom trop fort) ; le chevauchement réel de deux scripts n'est pas démontré ; `stub_macros` envoie sur la liaison
  par défaut du bouchon.
- **Moteur Lua abandonné à la fermeture d'un onglet** (`script_engine.c`, `session.c`) : après 3 s d'attente (interface figée pendant ce temps), le thread et
  ses ports Lua 1-15 restent vivants jusqu'au retour de l'appel C long qui le bloque, l'application continue, le moteur n'est jamais libéré.
  **Traité en partie : commentaires corrigés et cas décrit dans `CLAUDE.md` ; comportement inchangé.**
- **Fermeture avec N scripts bloqués** (`app.c`) : moteurs arrêtés et attendus l'un après l'autre. **Traité : l'arrêt est demandé à tous les moteurs avant le
  premier `gt_session_shutdown` ; un script bloqué plus de 3 s dans un appel C coûte encore ses 3 s d'attente (échéance propre à chaque moteur).**
- **Interface** : `[window]` lu deux fois au démarrage et à chaque nouvel onglet ; couplage ad hoc `gt_app_take_initial_send_file` ; un échec de chargement
  du fichier de macros se répète à chaque nouvel onglet qui hérite du chemin.
- **Nouvel onglet : nom de profil et fichier de macros de repli** pris dans l'onglet actif au OK du dialogue Port. **Traité : commenté (le dialogue est modal,
  l'onglet actif au OK est celui qui l'a prérempli).**
- **Ctrl+Page sans Maj** : `GtkNotebook` a ses propres liaisons Ctrl+Page_Up/Down. **Traité : scénario dans `test_onglets.py` (focus au terminal, les
  séquences partent sur le port de l'onglet actif, l'onglet actif ne change pas).**
- **Suite graphique** : `drag_in_surface` arrondit vers le bas et la cible du glisser est mesurée sur `tab-name` ; dialogue Terminal modal fermé par
  `test-activate`, pas par un geste ; focus après un changement d'onglet vérifié de façon non discriminante ; arrêt du polling à la fermeture d'un onglet
  faiblement prouvé ; Stop Lua d'un onglet sans toucher l'autre non couvert en graphique ; attentes après `open_tab` qui supposent l'activation faite.
  `time.sleep` non commentés de `test_onglets_terminal.py` et commentaire d'état divergent de `test_onglets.py` (panneau de script) : **traités.**
- **Traductions et documents** : préfixe `"%s: %s"` du message `open-failed` non traduisible, ponctuation française mêlée dans `fr.po`.
  « Configure Macros — %s » flou en allemand, hongrois, néerlandais, portugais du Brésil, russe et chinois : **traité** (ancienne traduction suivie de
  « — %s »). Ligne de 272 colonnes dans la spec (§3.2-17) : **traité.**
