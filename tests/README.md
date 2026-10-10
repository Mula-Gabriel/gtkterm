# Tests de GTKTerm

Tests unitaires GLib GTest des modules sans GTK (bibliothèque `gtkterm_core`, voir `src/meson.build`), tests d'intégration de la couche application (sans GTK
non plus), vérification de syntaxe des exemples Lua et du fichier de bureau ; à part, la suite graphique GTK 4 (`tests/gui/`).

## Lancer

```sh
meson test -C build                 # tout sauf la suite graphique
meson test -C build --suite unit    # tests unitaires et d'intégration
meson test -C build --suite lint    # syntaxe des exemples Lua (luac -p)
meson test -C build test_config_store -v   # un test, statut de chaque sous-test (le détail est dans build/meson-logs/testlog.txt)
meson test -C build --suite gui --print-errorlogs   # suite graphique (voir plus bas)
```

`ninja -C build` construit aussi les exécutables de test (meson les rattache toujours à la cible par défaut). Pour une construction sans tests :
`meson setup build -Dtests=false` (c'est ce que fait `data/gtkterm-update.sh`).

## Bogues connus (`# TODO`)

Un cas qui correspond à un défaut de la revue (`docs/superpowers/reviews/2026-10-06-revue-code.md`, identifiants C1…, M1…) est marqué par son identifiant via
`test_conclude()` (`tests/test_util.h`) :

- tant que le défaut existe, le cas est « incomplete » : il sort en `not ok … # TODO bogue M18 - …` et **ne fait pas échouer** la suite ;
- si le comportement devient conforme alors que le marquage est encore là, le cas **échoue** (« semble corrigé : retirer le marquage »).

Le résumé de meson ne compte pas ces cas, et `meson test -v` ne les montre que comme `EXPECTEDFAIL`, sans message. La liste complète, avec le détail
attendu/obtenu, est dans le journal de meson :

```sh
meson test -C build && grep '# TODO' build/meson-logs/testlog.txt
```

Les messages des tests sont en ASCII : les tests tournent en locale C, où GLib remplace tout caractère non ASCII par `?`.

Corriger un bogue = rendre le cas conforme **et** remplacer son identifiant par `NULL` dans la table du test.

## Écrire un test

- Un exécutable par module : `tests/test_<module>.c`, déclaré dans le dictionnaire `unit_tests` de `tests/meson.build`.
- Un cas par fonction de test ; pour une table de cas, une entrée par cas avec `g_test_add_data_func`. Ne jamais appeler `test_conclude()` deux fois dans un
  même cas.
- Un cas qui peut planter le processus s'exécute dans un sous-processus (`g_test_trap_subprocess`).
- Fichiers temporaires : `g_dir_make_tmp`, `test_write_file()`, `test_rmdir()`.
- Tests unitaires du cœur GLib (dictionnaire `unit_tests`) : `test_history`, `test_rx_transform`, `test_device_match`, `test_view_render`, `test_baudrates`,
  `test_macros_format`, `test_macros_list`, `test_macro_set`, `test_macro_file`, `test_macro_poller`, `test_serial_termios`, `test_config_file`, `test_profile`
  (dont `gt_profile_name_validate` : rognage, longueur, `[`/`]`, contrôles, noms réservés), `test_config_store`, `test_rx_queue` (file de réception des ports
  Lua), `test_script_port` (port Lua sur le transport, paire pty et TCP local, décodage hexadécimal), `test_term_options` (valeurs par défaut, notification
  seulement sur changement, `changed` après les notifications et une seule fois par distribution, un seul `changed` pour un profil appliqué, nombre d'octets
  par ligne refusé, aller-retour profil), `test_hex_input` (octets
  valides, « 1FF », « -1 », « G1 », séparateurs seuls, historique borné sans doublon consécutif), `test_capture` (RAW intact ; ASCII qui garde TAB, CR, LF et
  les octets ≥ 0x80 et retire les autres contrôles, DEL compris ; CR/LF auto et horodatage ; erreur d'écriture), `test_file_sender` (fonction d'écriture
  injectée et boucle GLib : envoi intact, écritures partielles, « port plein », erreur, délai de fin de ligne et attente d'un caractère jusqu'après le dernier
  LF, fichier vide, annulation, perte de liaison), `test_rx_logger` (transformation, pause sans ligne coupée, erreur
  d'écriture), `test_update_check` (comparaison de révisions, « unknown », révision ignorée, commande en liste d'arguments), `test_serial_ports` (motifs de
  `/proc/tty/drivers`, découverte des ports, ordre semi-numérique), `test_port_form` (validation du dialogue Port, série et TCP), `test_macro_view` (règles
  d'affichage du panneau de macros : onglets, séparateurs, listes à deux valeurs), `test_action_relay` (relais d'action du sous-projet 6b : délégation
  d'activation et d'état, reflet de l'état et de la sensibilité de la cible, ajout et retrait d'action dans la cible, changement de cible qui ne notifie que
  ce qui change, sans cible = désactivé).
- Tests d'intégration (dictionnaire `integration_tests`) : `test_link`, `test_transport` (sans bouchon : le transport n'appelle pas l'interface),
  `test_macros_send` (`GtMacros` sans GTK : envoi unique par identifiant, fichier, ordonnanceur, instances : polling sur la liaison de son instance),
  `test_script_engine` (moteur Lua réel : ports sur pty et TCP local, envois complets, raisons, « Stop » sur boucle de calcul, `os.exit`, journal regroupé,
  `gt_script_engine_shutdown`, deux moteurs en parallèle et arrêt de l'un sans l'autre ; le bouchon de la liaison simule son état par `stub_link_set_state`).
  Couche application complète, sans bouchon (dictionnaire `app_tests`, bibliothèque `gtkterm_app`) : `test_session`
  (sessions sur pty : nom court, profil copié et appliqué, deux sessions indépendantes, arrêt, session occupée), `test_session_manager` (ordre, session
  active, retrait, déplacement), `test_app_config` (chargement, enregistrement et suppression de profils par session, profil `default` absent, état de
  fenêtre, `x`/`y` ignorés puis retirés, case « vérifier au démarrage », fichier de macros retenu par session), `test_cmdline` (analyse pure : options
  inconnues, valeurs invalides, `-c` appliqué avant les autres options). Logique sans affichage de `src/ui/` (dictionnaire `ui_logic_tests`) : `test_macro_row` (lignes de l'éditeur de
  macros), `test_macro_accel` (forme canonique et capture d'un raccourci ; seul test qui lie GTK 4, sans jamais ouvrir d'affichage). Les autres ne lient pas GTK : les effets d'interface passent
  par la table `GtAppUi` (`app_ui.h`), que le test remplit avec ses
  propres fonctions d'enregistrement. Les bouchons de `tests/stubs/` remplacent des modules de la couche application (`stub_link.c` la liaison,
  `stub_journal.c`, `stub_macros.c` l'état des macros) ; un bouchon enregistre l'appel (`stub_record`) et ne
  contient aucune logique métier.
- Style : celui de `.clang-format` (tabulations, Allman, `if(`).

## Sanitizers et valgrind

```sh
meson setup build-asan -Db_sanitize=address,undefined
meson test -C build-asan
```

LeakSanitizer vérifie les fuites sans aucune suppression : toute fuite fait échouer le test. Instabilité connue : `test_link` `/perte-puis-reconnexion` sous
ASan ; relancer une fois.

valgrind, pour les erreurs mémoire (les fuites sont vérifiées par ASan) :

```sh
meson test -C build --suite unit --wrap='valgrind --error-exitcode=1 --leak-check=no'
```

## Suite graphique GTK 4 (`tests/gui/`)

```sh
meson test -C build --suite gui --print-errorlogs
meson test -C build --suite gui test_journal --print-errorlogs   # un scénario
```

Exclue de `meson test -C build` (`add_test_setup` dans `tests/meson.build`). **Jamais deux suites graphiques en même temps, jamais GTKTerm sur l'écran réel.**
Elle exige le crochet de test, compilé par défaut (`-Dtest_hooks=true` ; `-Dtest_hooks=false` le retire du binaire et la suite ne peut plus tourner). Chaque
scénario est sauté (code 77) si `gtk4-broadwayd`, `dbus-run-session` ou `python3-gi` manquent.

### Principe

Sous broadway GTK 4, AT-SPI n'existe pas et VTE 0.76 n'expose pas son texte : le harnais repose sur trois briques.

1. **Actions D-Bus** : GTKTerm est piloté par `org.gtk.Actions` (`Activate`, `SetState`, `Describe`) sur le bus de session privé ; toutes ses actions sont des
   actions `app.*` (voir `CLAUDE.md`). Le démarrage est détecté par l'apparition du nom `eu.mulagabriel.gtkterm.test.p<PID>` (au plus 20 s), sans attente fixe.
2. **Crochet de test** (`src/ui/test_hooks.c`, actif seulement si `GTKTERM_TEST_HOOKS=1`, qui donne aussi à l'application l'identifiant
   `eu.mulagabriel.gtkterm.test.p<PID>`) : `app.test-dump` (état `aa{sv}` : pour chaque widget des fenêtres et popovers visibles, type, rôle accessible,
   libellé, nom de widget, visible/sensible/focus/actif/sélectionné, géométrie en coordonnées de surface, taille de surface, position de popup, compteur
   d'images `frame` ; chaque appel demande une image à chaque surface, pour qu'un clic attende une géométrie mise en page sans délai de repli),
   `app.test-terminal-text` (texte du terminal), `app.test-terminal-props` (police, historique, curseur, couleurs du terminal), `app.test-activate(s)` (clic
   programmé d'un widget désigné par son id de dump, son nom ou son libellé ; synchrone : un `GtkButton` sensible reçoit `clicked` avant la réponse D-Bus, un
   bouton insensible n'est jamais cliqué et fait échouer le scénario), `app.test-set-text((ss))` (texte d'une entrée ou d'un `GtkTextView`, choix d'un
   `GtkDropDown`, valeur d'un `GtkSpinButton`, police d'un `GtkFontDialogButton`, couleur d'un `GtkColorDialogButton`). Les classes CSS d'un widget sont
   dans la clé `css` du dump. Onglets (6b) : `test-terminal-text` et `test-terminal-props` visent l'onglet actif ; `app.test-sessions` (état `aa{sv}`
   recalculé à chaque activation, comme `test-dump`) décrit chaque onglet dans l'ordre : `index` (i), `id` (u), `name` (s, nom court), `state` (s :
   `closed`, `connecting`, `open`, `listening`, `lost`, `reconnecting`), `profile` (s), `macros_file` (s, vide si aucun), `active` (b), `busy` (b), pour
   vérifier l'indépendance des onglets sans dépendre de l'affichage. Les widgets des pages non affichées du `GtkNotebook` sont visibles mais non mappés.
3. **Faux navigateur broadway** (`broadway.py`) : protocole de `gtk4-broadwayd`, réponse immédiate aux `ROUNDTRIP`, souris (masque du bouton présent à l'appui
   et au relâchement ; un clic attend que la géométrie du widget soit stable ; `drag_in_surface` pour un glisser, par ex. réordonner les onglets) et
   clavier ; une surface est visée par son identifiant, appariée par sa taille au dump. À utiliser quand le geste est l'objet
   du test : frappe dans le terminal, raccourcis, menus par clic, sélecteurs de fichiers (Ctrl+L), menus contextuels.

### Environnement (`run-gui-test.sh`)

- **Plantage de `gtk4-broadwayd` (GTK 4.24)** : sur un poste récent, le serveur broadway peut mourir sur ses propres assertions (`Gdk:ERROR:` dans
  `/gdk/broadway/`, défaut de GTK et non de GTKTerm). Un scénario en échec dont le `broadwayd.log` contient une telle assertion, ou qui meurt d'une exception
  non rattrapée dans ce cas, sort en SKIP (code 77) au lieu de FAIL (`conclude()` et le crochet d'exception de `gtktest.py`). Un SKIP n'est pas un échec,
  un FAIL l'est ; un scénario sauté n'a rien vérifié de GTKTerm : le relancer.

- `gtk4-broadwayd` sur un affichage propre au scénario (port dérivé du numéro d'affichage), `dbus-run-session` sur un bus privé sans dossier de services
  (rien n'y est activable, ni portail ni gvfs), `GDK_DEBUG=no-portals` (GTK 4 ignore `GTK_USE_PORTAL`), `XDG_RUNTIME_DIR` court (chemin de socket
  ≤ 108 octets), `HOME` et `XDG_*` dans `build/tests/gui/runs/<scénario>/`, aucune variable AT-SPI ; processus tués par groupe à la fin (pas d'orphelin),
  y compris quand meson interrompt le scénario (Ctrl+C, délai dépassé : le signal est relayé au groupe de `timeout`, puis `run-gui-test.sh` attend sa fin).
- Garde-fou : `guard.c`, chargé par `LD_PRELOAD`, refuse les vrais ports série et la configuration réelle de l'utilisateur (message `EMGUARD` dans stderr, qui
  fait échouer le scénario). Ports : paires pty seulement (et `127.0.0.1`). Faux `git` (`bin/git`, aucun réseau) et faux `xterm` (`bin/xterm`, n'exécute
  rien) pour la fenêtre « Updates ». Si `GTKTERM_GUARD_FAKE_MODEM` est posée (bits `TIOCM_*`), `guard.c` simule aussi `TIOCMGET` sur une pty, pour les voyants des signaux
  de contrôle de la barre d'état.

### Bibliothèque (`gtktest.py`)

- `Env(name, lang='C', display=None)` : dossiers privés `runs/<name>/` (`root`), paire pty, affichage propre ; `config_path`, `macros_path`, `port` ; `general(**clés)` et
  `profile(name='default', **clés)` (texte des groupes `[general]` et `[profile:…]` des scénarios : pas de vérification de mise à jour, port = la pty) ;
  `write_config(text)`, `read_config()`, `config_group(groupe)` ; `start(*args, port=True, connect_browser=True, extra_env=None)` (GTKTerm avec `--port <pty>`
  et `GTKTERM_TEST_HOOKS=1`, attend le nom D-Bus puis la fenêtre principale) ; `stop()` ; `finish(quit=True)` (`app.quit`, code 0 attendu, stderr sans
  erreur, sans `EMGUARD` ni rapport ASan, écrit `result.txt` ; plusieurs `Env` successifs possibles dans un scénario).
- Actions : `action(name, param=None)`, `set_state(name, value)` (`GLib.Variant`), `describe(name)` (activée, type du paramètre, état), `state(name)` (valeur
  Python), `actions()`.
- Widgets : `dump()`, `find(dump=None, name=…, label=…, type=…, native=…, mapped=True, css=…)` (`mapped=None` : sans filtre), `find1(timeout=5, **critères)`,
  `windows()`, `title()`, `wait_window(title)`, `alerts()`, `close_alert(button_label='OK')`, `terminal_text()`, `terminal_props()`, `status_text()`,
  `activate(key)` (synchrone : effets visibles au retour ; un bouton insensible arrête le scénario), `set_text(key, text)`, `after_frame()` (attend une image de
  chaque surface : une vérification négative lit ensuite un état mis en page).
- Gestes : `e.bw.click(widget, button=1)`, `e.bw.type(text)`, `e.bw.key(keysym, mods=())` (nom de touche, caractère ou entier ; `mods` : `('ctrl',)`,
  `('ctrl', 'shift')`, `('alt',)`), `e.bw.focus_window(title)` ; `file_dialog(env, title, path, timeout=8)` (sélecteur de fichiers : focus, Ctrl+L, chemin,
  Entrée).
- Port : `read_pty(duration)`, `drain()`, `write_pty(data)`, `use_link()`, `unplug()`, `replug()` (port derrière un lien symbolique).
- Onglets : `e.new_pty()` (paire pty supplémentaire, objet `Pty` : `port`, `read(duration)`, `drain()`, `write(data)` ; fermée avec l'`Env`),
  `e.sessions()` (liste de dict lue dans `test-sessions`), `e.active_session()`, `open_tab(env, port)` (nouvel onglet par le vrai chemin : `app.new-tab`,
  dialogue Port, port remplacé, OK ; attend que la nouvelle session soit active). `find`/`find1` ignorent les widgets non mappés : un nom stable
  (`terminal`, `macro-panel`…) désigne celui de l'onglet affiché.
- Macros : `macros_v2_text(*libellés)`, `write_macros_v2(path, macros, lists=None)`, `write_macros_v1(path, macros, lists=None)`, `macro_config(env, fichier)`,
  `classes(w)`, `macro_buttons(env)`, `macro_id(env, label)` ; éditeur : `open_editor(env)`, `editor_rows(env)`, `editor_select(env, index)`,
  `close_by_titlebar(env, nom)`.
- Mise à jour : `local_revision()`, `e.fake_git(sha=None, delay=0)`, `e.fake_calls(prog='git')`.
- Vérifications : `check(condition, libellé, detail='')` (note l'échec et continue), `wait_until(prédicat, timeout=5)`, `conclude()` (bilan, code de sortie) ;
  jamais d'attente fixe.

### Écrire un scénario

- `tests/gui/test_<nom>.py` : `from gtktest import *`, un `Env('<nom>')`, `write_config(e.general() + e.profile(…))`, `start(…)`, des `check(…)`, puis
  `e.finish()` et `sys.exit(conclude())` ; l'ajouter à `gui_scenarios` dans `tests/gui/meson.build` (57 scénarios aujourd'hui).
- Viser les widgets par leur nom stable (`gtk_widget_set_name` dans `src/ui/`, par ex. `port-dialog`, `macro-<id>`, `script-run`) ; passer par une action
  `app.*` quand le geste n'est pas l'objet du test, par le navigateur quand il l'est.
- Échec : lire `build/tests/gui/runs/<scénario>/result.txt` (une ligne `OK` / `ECHEC` par vérification) et `gtkterm.stderr` (sortie d'erreur de GTKTerm) du même dossier.
- Scénarios qui épinglent les points délicats : `test_messages_demarrage.py` (messages émis avant la fenêtre, affichés ensuite), `test_fermeture_dialogue.py`
  (fermeture pendant un dialogue asynchrone : code 0, aucun avertissement), `test_options_profil.py` (cases de menu fidèles après un rechargement de profil),
  `test_focus_terminal.py` (focus rendu au terminal), `test_profils.py` et `test_journal.py` (texte non ASCII et espaces dans les noms et chemins),
  `test_menus_actions.py` (chaque entrée de `menus.ui` vise une action existante, types de paramètre et d'état des actions inchangés), `test_clavier_ctrl_b.py`
  (Ctrl+B part sur le port).
- Onglets (sous-projet 6b) : `test_onglets.py` (New tab par le dialogue Port, Annuler, barre visible à deux, navigation Ctrl+Maj+Page et `app.active-tab`,
  titre, Ctrl+Maj+W/T, dernier onglet non fermable, focus au terminal après un clic d'étiquette), `test_onglets_actions.py` (actions relayées : écho, vue,
  port et sensibilités propres à chaque onglet ; un « Configure Macros » par onglet), `test_onglets_fermeture.py` (confirmation si script ou envoi en cours,
  fermeture de la fenêtre avec plusieurs onglets, `[session] script_path` gardé), `test_onglets_verrou.py` (même port série refusé au second onglet),
  `test_onglets_sigusr.py` (SIGUSR1/2 sur tous les onglets), `test_onglets_terminal.py` (dialogue Terminal lié à sa session), `test_onglets_ordre.py`
  (glisser d'étiquette → ordre du gestionnaire), `test_onglets_macros.py` et `test_onglets_macros_fichier.py` (macros, raccourcis et polling par onglet ;
  fichier de macros d'un nouvel onglet), `test_onglets_lua.py` (port 0 = onglet de lancement, deux scripts en parallèle, fermeture pendant un script).
