# Sous-projet 1 — Correctifs critiques

- Date : 2026-10-06
- Branche : `fix/correctifs-critiques` (fusion dans `master` comme une feature : écrasement en commits thématiques, fusion, suppression de la branche, publication sur `Maison`)
- Entrées : revue `docs/superpowers/reviews/2026-10-06-revue-code.md` (identifiants C*, M*), socle de tests du sous-projet 0 (`tests/`, `gtkterm_core`), remarques mineures reportées de la relecture finale du sous-projet 0

## Objectif

Corriger les défauts critiques C1–C13 et les défauts majeurs à fort impact dont la correction est locale, chacun prouvé par un test qui échoue avant le correctif (ou, pour les correctifs purement graphiques, par un essai manuel listé ici). Poser au passage le harnais de test à bouchons qui resservira aux sous-projets 2 (transport) et 5 (Lua).

## Hors périmètre

- Toute refonte au-delà des deux extractions décrites au §1.3. Le découpage d'`interface.c`, de `term_config.c`, de `macros.c`, le remplacement de parsecfg, etc. restent aux sous-projets 2 à 6.
- Les défauts majeurs dont le code sera réécrit par un sous-projet ultérieur : M1–M6 (transport, udev, reconnexion → 2), M10, M14 (configuration → 3), M16, M21, M22 (polling et persistance des macros → 4), M25, M28 (arrêt et tampon Lua → 5), M33 seconde moitié, M36, M37 (interface, écho → 2/6).
- Les avertissements `sign-compare` et `cast-function-type` ne sont corrigés que là où un correctif de ce document touche la ligne (M35 corrige `files.c:268`).

## Décisions de comportement (validées avec l'utilisateur)

1. **ESC et « ESC efface l'écran »** : seule la touche Échap tapée au clavier dans le terminal efface l'écran local, sans être envoyée. Les macros, Lua, la saisie hexadécimale et l'envoi de fichier envoient toujours 0x1B.
2. **Arguments entiers des macros** (`%d`, `%i`, `%u`, `%x`, `%X`, `%o`, avec ou sans modificateur de longueur) : saisis en décimal, ou en hexadécimal avec le préfixe `0x`/`0X`. Il n'y a plus d'interprétation octale d'un zéro initial (`010` vaut dix). `ff` sans préfixe reste invalide. Les macros existantes qui stockent du décimal ou du `0x…` ne changent pas de sortie.
3. **`%#Nom`** : c'est une liste si une liste de ce nom existe ; sinon `#` est le drapeau printf (`%#x` → `0xff`, `%#o` → `010`).
4. **Antislash dans les arguments `%s`/`%c`** : la valeur est mise en forme par printf (largeur, précision) **puis** ses antislashs sont protégés. `%5s` avec `a\b` donne 5 octets ; `%cn` avec `\` donne `\` puis `n`.
5. **Libellés `[…]`** : la troncature ne retire que le libellé et les espaces qui le séparent du spécificateur (`XY[a%%]%d` → `XY5`). `arr[0] %d` → `arr5` et `50% off` restent tels quels (comportements figés « à décider » au sous-projet 0, non modifiés).
6. **Noms de profils** : comparés sans tenir compte de la casse, comme le fait parsecfg. Enregistrer « Default » alors que « default » existe propose d'écraser « default ». Les noms `window`, `colors`, `layout` et `session` sont réservés : ils n'apparaissent pas dans les listes Charger/Supprimer et sont refusés à l'enregistrement.
7. **RS485** : RTS est activé avant chaque émission et relâché après (comportement d'envoi actuel, inchangé). La lecture périodique des signaux ne touche plus jamais RTS.

## 1. Harnais de test et extractions

### 1.1 Bouchons (`tests/stubs/`)

Des fichiers C qui fournissent, pour les tests, les symboles de l'application dont dépendent les modules testés, sans GTK ni VTE :
- `stub_ui.c` :
  - `show_message`, `Set_status_message`, `Set_window_title`, `Set_local_echo`, `Put_temp_message`, `rebuild_macro_buttons`, `add_input` ;
  - `waiting_for_char` ;
  - `struct configuration_port config` et `display_config_t term_conf`.
  Chaque appel est enregistré (nom, texte, thread appelant) dans un journal consultable par le test.
- `stub_display.c` : `send_serial` (enregistre les octets et le thread appelant, renvoie la longueur), `put_chars`, `set_tap_func`, `unset_tap_func`, `clear_buffer`.
- `stub_macros.c` : `get_shortcuts` sur un tableau de macros fourni par le test.

Un bouchon ne contient aucune logique métier. Si un test a besoin d'un comportement, le bouchon expose une variable ou un rappel que le test règle.

### 1.2 Nouveaux exécutables de test

- **`test_transport`** : le vrai `transport.c` + `serial.c` + bouchons, lié avec `-Wl,--wrap=ioctl`. `__wrap_ioctl` simule les lignes modem (`TIOCMGET`, `TIOCMSET`, `TIOCMBIS`, `TIOCMBIC`) pour un descripteur déclaré par le test, enregistre chaque changement de RTS/DTR, et délègue les autres requêtes à `__real_ioctl`. Ports : paire `openpty` (le test règle `config.port` sur le nom de l'esclave) et socket TCP local (le test écoute sur 127.0.0.1, port 0).
- **`test_script_engine`** : le vrai `script_engine.c` + Lua 5.4 + bouchons. Le test fixe une fonction de journal et une fonction de fin (`script_engine_set_log_func`, `script_engine_set_done_func`), lance un script (`script_engine_run`) et fait tourner le contexte principal GLib jusqu'à la fonction de fin ou un délai de 5 s (échec).
- **`test_macros_send`** : le chemin d'envoi de `macros.c` (envoi par index, par raccourci, avec arguments stockés), lié à GTK sans fenêtre (aucun `gtk_init`), avec bouchons.
- **`test_config_file`** : le nouveau module `config_file.c` (§1.3) sur des fichiers temporaires.
- **`test_serial_termios`** : la nouvelle fonction pure de construction termios (§1.3).

Tous sont déclarés comme les tests du sous-projet 0 (TAP, suite `unit`, environnement LSan/UBSan), derrière l'option `tests`.

### 1.3 Extractions dans `gtkterm_core`

- **`src/serial_termios.c` / `.h`** :
  - `void serial_termios_apply(struct termios *tio, gint bits, gint parity, gint stopbits, gint flow)` ;
  - avant d'appliquer, la fonction efface `CSIZE`, `PARENB`, `PARODD`, `CSTOPB`, `CRTSCTS`, et les bits `IXON`/`IXOFF` de `c_iflag` ; elle positionne `CLOCAL | CREAD` ;
  - le codage parité/flux reste celui de `config` (parité 0/1/2, flux 0/1/2/3). Un enum nommé n'arrive qu'au sous-projet 2 ;
  - `transport.c` l'appelle à la place de son `switch` actuel.
- **`src/config_file.c` / `.h`** (GLib + parsecfg, sans GTK) :
  - `gint config_file_parse_profiles(const gchar *path, cfgStruct cfg[])` : seul point d'entrée de lecture des profils. Retire les groupes GKeyFile réservés, lance `cfgParse`, remet les groupes en place, renvoie le nombre de profils (-1 en cas d'erreur). Les six appels directs à `cfgParse` de `term_config.c` passent par lui (C5).
  - `gint config_file_find_profile(const gchar *name, gint max)` : comparaison avec `g_ascii_strcasecmp` (C6).
  - `gboolean config_file_is_reserved_name(const gchar *name)` : vrai pour `window`, `colors`, `layout`, `session`, quelle que soit la casse (C13).
  - `gint config_file_remove_section(const gchar *path, const gchar *section)` : remplace `remove_section`. En-têtes de section reconnus seulement en début de ligne (après d'éventuels blancs), comparaison sans casse, fichier toujours fermé, fichier vide accepté, réécriture atomique (`g_file_set_contents`) (M11).
  - `gchar *config_file_strip_reserved_groups(const gchar *path)` et `gboolean config_file_restore_reserved_groups(const gchar *path, const gchar *extras)` : versions avec chemin explicite des fonctions statiques actuelles.

## 2. Correctifs

Chaque ligne indique le défaut, le correctif et sa preuve (test automatique ou essai manuel `EM-n`, §3).

### Transport et série
- **C2 SIGPIPE** :
  - `signal(SIGPIPE, SIG_IGN)` dans `main()` ;
  - en TCP, `transport_send` écrit avec `send(fd, buf, len, MSG_NOSIGNAL)` ;
  - côté Lua, `l_send`/`l_send_hex` vers un port TCP font de même.
  - Preuve : `test_transport`, pair TCP fermé puis deux envois → valeur négative renvoyée, processus vivant ; `test_script_engine`, même chose avec un port Lua TCP.
- **C3 RS485** : retrait de l'appel `transport_set_signal(1)` dans `transport_get_signals`. Preuve : `test_transport`, flux 3, dix lectures de signaux → aucun changement de RTS enregistré par `__wrap_ioctl` ; un envoi → RTS activé puis relâché.
- **C4 termios** : `serial_termios_apply`. Preuve :
  - `test_serial_termios` : départ CS8 + PARENB + CRTSCTS, réglage 7 bits sans parité ni flux → CS7, ni PARENB ni CRTSCTS ;
  - toutes les combinaisons bits × parité × stop × flux ;
  - `test_transport` sur pty : `tcgetattr` après ouverture.
- **M7 caractère d'attente signé** : `car`/`wait_char` manipulés en `gint` (0–255, -1 = aucun) dans `term_config.c`, `files.c` et `transport.c`. Preuve : `test_transport`, caractère 0xE9 attendu → `add_input` appelé à sa réception.

### Moteur Lua
- **C1 fin de script** :
  - les ports Lua ouverts sont fermés par une fonction C `lua_port_close(gint handle)`, appelée pour chaque port ouvert (`l_close` l'appelle aussi) ;
  - la pile Lua n'est plus utilisée pour ce nettoyage.
  - Preuve : `test_script_engine`, script qui ouvre deux ports TCP puis lève une erreur → pas d'abandon du processus, fonction de fin appelée, les deux descripteurs fermés (`fcntl(fd, F_GETFD)` → `EBADF`).
- **C10, C11, M27 thread principal** :
  - une aide `run_on_main_sync(GSourceFunc func, gpointer data)` exécute `func` sur le contexte principal (`g_main_context_invoke`) et bloque le thread Lua jusqu'à la fin (`GMutex` + `GCond`) ;
  - `send(0, …)`, `send_hex(0, …)`, `send_macro(…)`, `get_macros()` et `get_lists()` passent par elle ;
  - `send_macro` résout la macro, met en forme et envoie sur le thread principal, et renvoie le vrai résultat de l'envoi ;
  - l'arrêt du moteur ne joint jamais le thread Lua depuis le thread principal tant que celui-ci peut attendre le thread principal : la jointure a lieu dans le rappel de fin (M30).
  - Preuve : `test_script_engine`, les bouchons `send_serial`/`get_shortcuts` enregistrent le thread appelant, qui doit être le thread principal ; `send_macro("A"); send(0, "B")` → octets dans l'ordre A puis B.
- **M26 `wait_for` binaire** : recherche par `memmem`, résultat par `lua_pushlstring`. Le motif peut contenir `\0`. Preuve : `test_script_engine`, réception injectée `"\x01\x00\x02OK"`, `wait_for(0, "\x00\x02")` → trouvé, données de longueur 3.
- **M30 thread joint** : `g_thread_join` dans le rappel de fin. Preuve : `test_script_engine`, deux exécutions successives sans fuite (ASan).

### Configuration et parsecfg
- **C5** : `config_file_parse_profiles` partout. Preuve : `test_config_file`, fichier avec un profil plus `[session] script_path=/home/x/mon script.lua` → 1 profil lu, groupe `[session]` intact octet pour octet.
- **C6** : `config_file_find_profile`. Preuve : `test_config_file`, profils `default` et `usb`, recherche « Default » → indice de `default`.
- **C7** :
  - `cfgStoreValue` refuse un indice de section hors bornes (`CFG_INTERNAL_ERROR`) ;
  - suppression des appels `cfgStoreValue` inutiles de `config_fg_color`/`config_bg_color` (term_config.c:2209-2241).
  - Preuve : le cas `test_parsecfg` marqué C7 devient conforme (marquage retiré) ; le sous-processus du test ne produit plus de core dump (`setrlimit(RLIMIT_CORE, 0)` dans l'enfant).
- **C13** : `config_file_is_reserved_name`. La liste Charger/Supprimer les écarte et l'enregistrement les refuse. Preuve : `test_config_file` pour la fonction, essai manuel EM-3 pour les listes.
- **M9 locale** : parsecfg lit et écrit sous une locale « C » propre au thread (`newlocale` + `uselocale`, restaurée à la sortie) et n'appelle plus `setlocale`. Aujourd'hui, ce `setlocale(LC_ALL, "C")` caché remet *toute* l'application en locale C dès la première lecture de la configuration, après que `gtk_init` a appliqué la locale de l'utilisateur. Le supprimer rendrait la virgule décimale active partout sous `fr_FR`. Le correctif couvre donc aussi tous les endroits qui dépendaient de ce couplage :
  - `macros_format.c` : lecture des flottants par `g_ascii_strtod` (validation et expansion), mise en forme des flottants (`%.2f`, `%e`…) sous locale C propre au thread. Le séparateur décimal des macros est le point, quelle que soit la langue : « 2.5 » est accepté, « 2,5 » refusé comme aujourd'hui, et `%.2f` produit « 3.14 » ;
  - `term_config.c` (`Copy_configuration`) : couleurs écrites par `g_ascii_formatd` au lieu de `g_strdup_printf("%f")` ;
  - `script_engine.c` : le thread Lua s'exécute sous locale C propre au thread, pour que `tostring(3.5)` donne « 3.5 ».
  - Preuve :
    - le cas `test_parsecfg` marqué M9 devient conforme ;
    - nouveaux cas `test_macros_format` sous `fr_FR.UTF-8` (sautés si la locale est absente) : validation de `%f` avec « 2.5 » → valide, expansion de `%.2f` avec « 3.14159 » → `3.14` ;
    - `test_script_engine` sous `fr_FR.UTF-8` : `gtkterm.log(tostring(3.5))` → « 3.5 » ;
    - essai manuel EM-16 (couleurs enregistrées puis relues sous `fr_FR`).
- **M11** : `config_file_remove_section`. Preuve : `test_config_file`, retrait de la section du milieu, de la dernière sans saut de ligne final, avec une valeur `socket_host = [::1]` dans la section précédente, sur un fichier vide.
- **M12** : si `g_key_file_load_from_file` échoue sur un fichier qui existe (autre erreur que « fichier absent »), `save_window_state` et la sauvegarde de `script_panel.c` n'écrivent rien et journalisent l'erreur. Preuve : essai manuel EM-4.
- **M13** : `config_file_init` crée le répertoire de configuration (`g_mkdir_with_parents`) et utilise `g_get_home_dir()`. Preuve : `test_config_file`, `XDG_CONFIG_HOME` pointant vers un répertoire inexistant (sous-processus, `G_TEST_OPTION_ISOLATE_DIRS`) → répertoire créé.

### Macros
- **C8 et M42** (nouveau défaut, consigné dans la revue) :
  - une ligne ajoutée dans la fenêtre des macros sans libellé ni raccourci produit une entrée dont `label` et `shortcut` valent NULL. Le tableau des macros est terminé par une entrée entièrement NULL : `get_shortcuts` s'arrête donc à cette ligne et tronque le tableau, et `add_shortcuts` et `macros_destroy` s'arrêtent au premier raccourci NULL (raccourcis suivants jamais connectés, fuite) ;
  - correctif : `Save_shortcuts` normalise un libellé absent en `""` et un raccourci absent en `"None"`, comme le fait déjà le chargement du `.ini` ; `strlen(NULL)` est aussi évité dans `macros.c:86,120` ;
  - `create_shortcuts` alloue avec `g_malloc0`.
  - Preuve : `test_macros_send`, macro de libellé NULL envoyée par raccourci → pas de plantage, octets envoyés.
- **C9** :
  - `rebuild_macro_buttons` vide `macro_button_table` et `macro_polling_table` avant de détruire les pages ;
  - le polling d'une macro qui n'existe plus est arrêté.
  - Preuve : essais manuels EM-1 et EM-2, plus une exécution sous ASan (`build-asan`) pendant ces essais.
- **M15** : l'envoi par raccourci et par polling passe par le même chemin que le panneau, `format_action_with_args` avec les arguments stockés de la macro, puis `parse_macro_string`. Preuve : `test_macros_send`, macro `V%d\r` avec argument stocké `42` envoyée par raccourci → `V42\r` ; macro `100%%` → `100%`.
- **M17** : les raccourcis ne sont connectés qu'une fois. `add_shortcuts` déconnecte les fermetures précédentes avant d'en connecter de nouvelles, et `shortcut_callback` vérifie que l'indice est dans les bornes. Preuve : `test_macros_send`, groupe de raccourcis réel (`gtk_accel_group_new`, sans fenêtre), `add_shortcuts` appelé deux fois → une seule entrée par touche (`gtk_accel_group_query`) ; indice hors bornes → rien d'envoyé ; essai manuel EM-5.
- **M18, M19, M20** : selon les décisions 3, 4 et 5. Preuve : les cas marqués de `test_macros_format` deviennent conformes, marquages retirés.
- **M23** : selon la décision 2. Les cas M23 de `test_macros_format` sont réécrits :
  - `%x [010]` → `a` ;
  - `%x [ff]` → comportement inchangé (refusé à la validation), non marqué ;
  - validation `x 09` → valide ;
  - nouveaux cas `%d [010]` → `10`, `%o [010]` → `12`.
- **M24** : l'aide de la fenêtre des macros décrit les échappements réels (`\xHH`, `\r`, `\n`, `\t`, `\\` ; plus de `\0A` ni `\0FF`). Preuve : essai manuel EM-6.

### Interface, affichage, journal
- **C12** : le dialogue Port est modal et transitoire pour la fenêtre principale. Une seconde demande d'ouverture pendant qu'il est ouvert le ramène au premier plan au lieu d'en créer un autre. Preuve : essai manuel EM-7.
- **M31 journal dupliqué** : `set_view`, seul appelant qui rejoue le tampon vers l'affichage, suspend la journalisation autour de `write_buffer()` par une nouvelle fonction `logging_set_suspended(gboolean)` de `logging.c`. `buffer.c` reste sans dépendance GTK. Preuve : essai manuel EM-8.
- **M32 ESC** : la règle d'effacement passe de `send_serial` au gestionnaire de saisie clavier (`commit` de VTE). Preuve : essai manuel EM-9. `send_serial` dépend de VTE et n'a pas de test automatique dans ce sous-projet.
- **M38 chaînes de format** : `show_message` et `search.c:75` passent le message en argument d'un format `"%s"`. Preuve : essai manuel EM-10 (message contenant `%s%n`).
- **M39 journal** : quand l'utilisateur choisit d'écraser, le fichier est ouvert en `"w"`. Si `logging_clear` échoue (`freopen`), les menus sont remis dans leur état initial ; c'est là qu'ils restaient bloqués, `logging_start` les remettant déjà. Preuve : essai manuel EM-11.
- **M40 barre d'état** : `Put_temp_message` mémorise l'identifiant du message poussé et le retire par `gtk_statusbar_remove`. Le message « transfert en cours » de `files.c` (`send_raw_file`, `close_all`) est lui aussi retiré par identifiant ; sinon un message temporaire arrivé pendant un envoi de fichier le laisserait affiché pour toujours. Preuve : essai manuel EM-12.

### Fichiers et mise à jour
- **M34 update.c** :
  - `git ls-remote -- <url> HEAD` ;
  - `GIT_TERMINAL_PROMPT=0` dans l'environnement du sous-processus ;
  - le dialogue parent est suivi par un pointeur faible : si l'utilisateur ferme « À propos » pendant la vérification, le résultat s'affiche sans parent.
  - Preuve : essai manuel EM-13.
- **M35 files.c** :
  - le rappel d'envoi de fichier a la signature `GIOFunc` (renvoie `gboolean`) ;
  - `read` renvoyant 0 termine le transfert, -1 l'interrompt avec un message ;
  - la recherche de fin de ligne ne lit plus au-delà des données ;
  - le `GIOChannel` est libéré.
  - Preuve : avertissement `cast-function-type` disparu à cet endroit, essai manuel EM-14.
- **M8 gtkterm-update.sh** : `disown` retiré (le script est en `/bin/sh`), et l'application est relancée par `"$PREFIX/bin/gtkterm"` (détaché par `nohup … &`). Preuve : `sh -n` et `checkbashisms` s'il est installé ; essai manuel EM-15 lors de la prochaine mise à jour réelle.

### Exemples et documentation
- **M29** :
  - `scripts/10_firmware_update.lua` : extraction de version et de CRC corrigée, puisque `wait_for` renvoie les données motif inclus, et parenthèses autour de `..`/`or` ;
  - `08` : délai suffisant ;
  - `09` : commentaire corrigé ;
  - `.claude/skills/lua-script/SKILL.md` aligné sur l'API réelle (`log` un seul argument, `send` → booléen, `open(h, "tcp_server", port)`, `clear(h)` vide le tampon de réception, `save_csv` → `ok, err`).
  - Preuve : `lua-syntax` ; `test_script_engine` exécute `10_firmware_update.lua` contre un répondeur simulé jusqu'à la vérification de version.

### Outillage (remarques reportées du sous-projet 0)
1. `tests/lsan.supp` : une seule ligne `leak:parsecfg.c` à la place des noms de fonctions.
2. Cas C7 : `setrlimit(RLIMIT_CORE, 0)` dans le sous-processus ; C7 testé aussi côté configuration (`test_config_file`, fichier vide puis enregistrement d'un profil).
3. `.gitignore` : `/build*/` et `/graphify-out/` (ancrés).
4. `lua-syntax` : liste des exemples obtenue par `run_command(find…)` à la configuration ou par un petit script, au lieu d'une liste écrite en dur. Si `luac` est absent, un test qui se déclare sauté (code de sortie 77) est déclaré, au lieu de rien.
5. `glib_dep = dependency('glib-2.0', version : '>= 2.70')`.
6. `tests/README.md` : « sortie TAP détaillée » corrigé, valgrind documenté avec `--leak-check=full --errors-for-leak-kinds=definite` et les suppressions, ou l'écart justifié.
7. `test_macros_list` : cas explicite pour `macro_lists_init` (idempotent, ne vide pas une liste existante).
8. Noms des cas de table : le chemin GTest inclut une étiquette lisible en plus de l'indice (ex. `/expansion/16-x-ff`).
9. `-c safe.directory=*` : rien à faire au-delà de `--no-rebuild` (déjà fait) ; noté dans la revue comme accepté.

## 3. Essais manuels

À dérouler par l'utilisateur sur l'application construite, une fois tous les correctifs faits. `build-asan` est utilisé pour EM-1 et EM-2.

| N° | Défaut | Essai | Attendu |
|---|---|---|---|
| EM-1 | C9 | Macro en polling dans un onglet, masquer cet onglet, le réafficher | Pas de plantage ni de rapport ASan ; le polling reprend sur le bon bouton |
| EM-2 | C9 | Macro en polling, charger un autre fichier `.ini` de macros | Le polling de l'ancien fichier s'arrête ; aucune macro du nouveau fichier ne part seule |
| EM-3 | C13 | Configuration → Charger, puis Supprimer | `window`, `colors`, `layout` et `session` n'apparaissent pas ; enregistrer un profil nommé « window » est refusé |
| EM-4 | M12 | Copier `.gtktermrc`, puis mettre la ligne `ligne invalide` en **première** ligne ; lancer (« Cannot read configuration file! » et configuration par défaut sont attendus), quitter | Le fichier n'est pas modifié : profils et première ligne toujours là (avant le correctif, il était réduit aux groupes `[window]`, `[colors]`, …) ; remettre ensuite la copie |
| EM-5 | M17 | Raccourci clavier d'une macro, appuyer une fois | Un seul envoi (écho local ou moniteur série) |
| EM-6 | M24 | Ouvrir l'aide de la fenêtre des macros | Échappements décrits : `\xHH`, `\r`, `\n`, `\t`, `\\` |
| EM-7 | C12 | Ouvrir Configuration → Port deux fois | Un seul dialogue, modal |
| EM-8 | M31 | Journaliser, recevoir des données, basculer ASCII → hexa → ASCII | Le journal ne contient les données qu'une fois |
| EM-9 | M32 | Option « ESC efface l'écran » active : touche Échap, puis macro `\x1b` | Échap efface sans rien envoyer ; la macro envoie 0x1B |
| EM-10 | M38 | Ouvrir un port inexistant nommé `/dev/%s%n` | Message d'erreur affiché tel quel, pas de plantage |
| EM-11 | M39 | Journaliser dans un fichier existant et choisir « écraser » | Le fichier ne contient que la nouvelle session |
| EM-12 | M40 | Affichage → « Envoyer des données hexadécimales », Entrée dans le champ vide (message temporaire « 0 byte(s) sent! »), puis aussitôt Configuration → Port, changer le débit et valider | Après l'expiration du message, la barre d'état montre le **nouveau** débit (avant le correctif : l'ancien) |
| EM-13 | M34 | « À propos » → Vérifier, refermer « À propos » aussitôt | Pas de plantage ; le résultat s'affiche |
| EM-14 | M35 | Envoyer un fichier texte, puis un fichier vide | Envoi complet, fin propre, pas de blocage |
| EM-15 | M8 | Lors de la prochaine mise à jour réelle | Le script se termine sans erreur et relance GTKTerm |
| EM-16 | M9 | Sous `LANG=fr_FR.UTF-8` : changer la couleur du texte, enregistrer la configuration, relancer | La couleur est conservée ; `.gtktermrc` contient des flottants avec un point |
| EM-17 | M9 | Sous `LANG=fr_FR.UTF-8` : macro `%.2f` avec l'argument 3.14159 | `3.14` envoyé (point décimal) ; les menus restent traduits en français |
| EM-18 | M42 | Fenêtre des macros : « Ajouter » une ligne laissée vide entre deux macros à raccourci, valider | Les macros et raccourcis situés après la ligne vide fonctionnent toujours |

## 4. Règles d'exécution

- Chaque correctif commence par un test qui échoue : un nouveau test, ou un cas marqué `# TODO` dont on retire le marquage. À défaut, l'essai manuel correspondant est indiqué dans le commit.
- Un commit par correctif ou par petit groupe cohérent (même module, même cause).
- Style du fichier touché respecté ; nouveaux fichiers au style de `.clang-format`.
- Aucun reformatage, aucune refonte hors §1.3.
- Tout nouveau défaut découvert en chemin est consigné dans la revue (identifiant suivant, M42…). Il n'est corrigé que s'il est critique ou s'il bloque un correctif de ce document.

## 5. Critères d'acceptation

1. `meson test -C build` : aucun échec ; `grep '# TODO' build/meson-logs/testlog.txt` ne contient plus aucun des identifiants traités ici (C7, M9, M18, M19, M20, M23).
2. `meson test -C build-asan` (address, undefined) : aucun échec.
3. Chaque défaut de §2 a son test automatique ou son essai manuel, et chaque test nommé au §2 existe.
4. Les essais manuels EM-1 à EM-14, EM-16 à EM-18 sont validés par l'utilisateur (EM-15 à la prochaine mise à jour).
5. Aucun changement de comportement en dehors des défauts listés et des décisions ci-dessus.
