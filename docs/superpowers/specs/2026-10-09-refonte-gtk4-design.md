# Sous-projet 6a — Migration GTK 4 : bascule à fonctionnalités égales, état d'application unique, dialogues et panneaux réécrits

- **Date** : 2026-10-09
- **Branche** : `feature/refonte-gtk4` (fusionnée `--no-ff` dans `master`, écrasée en 3 commits thématiques, puis supprimée en local et sur Maison)
- **Entrées** : revue `docs/superpowers/reviews/2026-10-06-revue-code.md` (M33 2e moitié, M34 reste, M35 reste, M36, §5, §6, annexes des sous-projets 1 à 5) ;
  sous-projets 0 à 5 fusionnés ; inventaires et essai de faisabilité du 2026-10-09 (résumés en annexe A).
- **Décisions** : celles de l'utilisateur au cadrage (§3.1) ; les autres prises en autonomie (consigne de l'utilisateur pour le chantier) et notées « autonomie » (§3.2).

## Contexte : la suite du chantier

Les sous-projets 2 à 5 ont sorti de l'interface tout ce qui n'était pas graphique : le cœur GLib (`gtkterm_core`), la liaison `GtkTermLink` (GObject à
propriétés et signaux), le magasin de configuration et le profil (`GtConfigStore`, `GtProfile`), le modèle de macros (`GtMacroSet`, `GtMacroPoller`) et le moteur
Lua (aucun appel GTK). Il reste une interface GTK 3 d'environ 5 500 lignes (`interface.c`, `term_config.c`, `macros.c`, `macro_panel.c`, `script_panel.c`,
`files.c`, `logging.c`, `search.c`, `update.c`, `terminal_display.c`) qui garde les défauts que les specs précédentes ont renvoyés ici : drapeaux d'option en
quatre à cinq copies, actions jamais ajoutées à un `GActionMap` (M36), 23 boucles imbriquées `gtk_dialog_run`, dialogues seulement rebranchés.

Le sous-projet 6 passe GTKTerm à GTK 4. Il est découpé (décision de l'utilisateur) en trois branches successives : **6a** (cette spec) bascule à fonctionnalités
égales et réécrit l'interface sur un état unique ; **6b** ajoutera les onglets, un par port ouvert ; **6c** ajoutera les projets (`[project:…]`).

## Objectif

1. Une seule pile graphique : **GTK 4 ≥ 4.14, VTE 0.76 (gtk4), GtkSourceView 5.12**, sans libadwaita ni GTK 3. Le minimum vaut à la compilation **et** à l'exécution
   (Ubuntu 24.04) ; le compilateur le fait respecter (§7).
2. **GtkApplication** : actions dans un vrai `GActionMap` (M36), menus en `GMenuModel`, raccourcis par `gtk_application_set_accels_for_action`, une seule fermeture.
3. **État d'application unique** : les options de comportement et de vue vivent dans un seul objet ; menus, affichage, journal et sauvegardes en sont des vues.
4. **Dialogues non bloquants** : plus aucune boucle imbriquée ; Port, Terminal, profils, éditeur de macros, panneau de macros et panneau de script réécrits.
5. **Interface découpée** en modules d'un seul rôle (`src/ui/`), sans état global partagé par `extern` ; la logique non graphique (envoi de fichier, captures,
   journal, saisie hexadécimale) descend dans des modules GLib testés unitairement.
6. **Suite graphique portée** sur GTK 4 (broadway GTK 4 + crochet de test + actions D-Bus) : les 23 scénarios existants passent, plus ceux des nouveaux comportements.
7. Les défauts de la revue encore ouverts qui touchent l'interface sont corrigés (§10).

## Hors périmètre

- Onglets, plusieurs liaisons ouvertes (6b) ; projets, contenu de `[project:…]`, plusieurs instances sur un même fichier de macros (6c).
- Nouvelles fonctions : pas de nouvelle option de profil, pas d'éditeur de colonnes/lignes/sonnette visuelle (champs `columns`, `rows`, `visual_bell` sans UI, comme aujourd'hui).
- Les remarques sur le magasin et la migration de configuration (revue, annexe SP3 : causes d'erreur, courses entre instances) et sur `macro_file.c` (annexe SP4) : 6c.
- Accessibilité réelle des widgets au-delà des noms accessibles posés au passage (aucun test possible sous broadway, §9).

## 3. Décisions

### 3.1 Prises par l'utilisateur au cadrage (2026-10-09)

1. Découpage **6a → 6b → 6c**, une branche chacun, fusionnée avant la suivante.
2. **Bascule franche** : pas de construction GTK 3 conservée après la fusion de 6a.
3. **Pas de libadwaita**.
4. Minimum **GTK 4.14 / VTE 0.76 / GtkSourceView 5.12** : des postes en Ubuntu 24.04 doivent continuer à exécuter GTKTerm.

### 3.2 Prises en autonomie

1. **Double construction pendant la branche** (pour que chaque tâche garde une suite verte) : la nouvelle interface est écrite dans `src/ui/` et produit l'exécutable
   `gtkterm-gtk4` (non installé). L'ancienne interface GTK 3 reste construite et testée, **sans être modifiée** sauf ce que les extractions de §4.2 imposent. La
   dernière tâche d'implémentation fait la bascule (§11) : suppression des fichiers GTK 3, `gtkterm-gtk4` renommé `gtkterm`, suite graphique GTK 3 retirée.
2. **Couche « application » GLib partagée** : ce qui n'est pas graphique dans `macros.c` et `term_config.c` est extrait **sur place** dans des modules GLib
   (`macros.c` sans GTK, `app_config.c`), utilisés par les deux interfaces pendant la transition. Les rappels vers l'interface passent par une table de crochets
   (`GtAppUi`, §4.3), plus jamais par des fonctions d'interface appelées en dur.
3. **Identifiant d'application** `eu.mulagabriel.gtkterm`, `G_APPLICATION_NON_UNIQUE` (plusieurs instances, chacune son port). Le fichier de bureau devient
   `eu.mulagabriel.gtkterm.desktop` (Wayland associe fenêtre et lanceur par l'identifiant) ; l'icône reste `gtkterm`. Le script de mise à jour retire l'ancien
   `<prefix>/share/applications/gtkterm.desktop` s'il existe. En mode test (§9), l'identifiant est `eu.mulagabriel.gtkterm.test.p<PID>` et l'application est unique
   sur le bus privé, pour que le harnais la joigne par D-Bus.
4. **Toutes les actions en `app.*`** : sous broadway, seules les actions de l'application sont exportées sur D-Bus (essai, annexe A) ; en 6b elles viseront l'onglet actif.
5. **État unique** : un GObject du cœur, `GtTermOptions` (§5), porte écho, CR/LF auto, horodatage, ESC efface l'écran, mode de vue, octets par ligne, index ;
   l'auto-reconnexion reste la propriété `auto-reconnect` de la liaison. Les actions à état sont des `GPropertyAction` sur ces propriétés : il n'existe plus
   de copie. Les champs correspondants de `config` ne servent qu'à appliquer un profil et à l'enregistrer.
6. **Dialogues** : `GtkWindow` modales transitoires (GtkDialog est déprécié en 4.10), `GtkAlertDialog`, `GtkFileDialog`, `GtkFontDialogButton`, `GtkColorDialogButton`.
   `show_message()` disparaît ; les erreurs passent par le crochet `GtAppUi.message` (boîte non bloquante). Aucun appelant ne doit plus dépendre d'un blocage.
7. **Position de fenêtre** : impossible en GTK 4 (et sans effet sous Wayland). `[window] x`/`y` ne sont plus lus ; ils sont retirés au prochain enregistrement.
   Restent : largeur, hauteur, maximisée, positions des deux séparateurs, visibilité des panneaux de macros et de script et de la barre d'envoi hexadécimal.
8. **Raccourcis** : ceux d'aujourd'hui sont conservés (Ctrl+Maj+L/K/R/Q/C/V/F/A/S/B, F5-F8), sauf l'alias **Ctrl+B retiré** (STX, 0x02, doit pouvoir partir sur le port).
   Les raccourcis de macros passent avant le terminal (comportement M43 conservé) ; l'éditeur refuse un raccourci déjà pris par une action de l'application.
9. **Menu Journal** : « Pause/Resume » devient une case à cocher « Pause » (action à état `app.log-paused`) ; un `GMenuModel` ne change pas de libellé.
10. **Mise à jour** : l'URL de la source quitte la boîte À propos (GTK 4 n'y permet plus d'ajouter des champs) pour une fenêtre « Updates » (menu Aide › « Check for
    updates… ») : URL, vérification au démarrage, bouton « Check now » qui utilise **l'URL affichée** ; la boîte À propos redevient standard.
11. **« Clear scrollback »** (annexe SP2, « à trancher ») : comportement conservé et documenté — il ne vide que le défilement du terminal ; un réaffichage (vue, écho,
    CR/LF) le fait revenir depuis l'historique, qui reste la référence des captures.
12. **Onglets masqués du panneau de macros** : toujours non persistés (comme aujourd'hui) ; masquer un onglet arrête ses pollings (comportement actuel).
13. **Noms de profil** (annexe SP3) : nom rogné, 1 à 64 caractères, tout caractère imprimable sauf `[`, `]` et les caractères de contrôle, noms réservés
    (`window`, `colors`, `layout`, `session`, sans casse) refusés ; doublon sans casse = confirmation d'écrasement. Règle dans le cœur (`gt_profile_name_validate`).
14. **Démarrage sans profil `default`** alors que d'autres existent (annexe SP3) : démarrage sur les valeurs par défaut, sans boîte ; « default » n'est créé que
    par un enregistrement explicite.
15. **Panneau de script** : msgids français remplacés par des msgids anglais (traduits en `fr.po`) ; schéma de couleurs GtkSourceView écrit dans
    `$XDG_DATA_HOME/gtkterm/styles/` (dossier propre à GTKTerm, ajouté au chemin de recherche) au lieu de `gtksourceview-4/styles/` ; `[colors] bg`/`fg` écrits
    seulement si l'utilisateur les a choisis (les couleurs suivent sinon celles du terminal, y compris après le dialogue Terminal ou un changement de profil).
16. **Captures** : « Save RAW/ASCII » proposent leur propre dernier fichier (plus `fic_defaut` partagé avec l'envoi) ; « Save ASCII » garde TAB (fin de M35).

## 4. Architecture

### 4.1 Couches et fichiers

| Couche | Fichiers | Dépendances | Rôle |
|---|---|---|---|
| Cœur (`gtkterm_core`) | existants + **`term_options.c`**, **`file_sender.c`**, **`capture.c`**, **`rx_logger.c`**, **`hex_input.c`** ; `profile.c` (+ `gt_profile_name_validate`) | GLib, GObject | état et logique testables sans affichage |
| Application (GLib) | `link.c`, `script_engine.c`, **`macros.c`** (refondu sans GTK), **`app_config.c`** (extrait de `term_config.c`), **`app_ui.h`**, `cmdline.c`, `update_check.c` (extrait de `update.c`) | cœur, gudev, Lua | état de l'application, chemins de fichiers, liaison par défaut ; aucun en-tête GTK |
| Interface GTK 4 (`src/ui/`) | `app.c`, `main_window.c`, `menus.ui`, `terminal_view.c`, `search_bar.c`, `status_bar.c`, `hex_send_bar.c`, `dialogs.c`, `port_dialog.c`, `terminal_dialog.c` + `terminal_dialog.ui`, `profile_dialogs.c`, `macro_panel.c`, `macro_editor.c`, `macro_shortcuts.c`, `script_panel.c`, `file_send_window.c`, `update_window.c`, `test_hooks.c` | GTK 4, VTE gtk4, GtkSourceView 5 | vues et contrôleurs ; lit l'état, ne le copie pas |
| Interface GTK 3 (temporaire) | `interface.c`, `term_config.c` (dialogues), `macro_panel.c`, `macros_gtk3.c` (raccourcis et fenêtre d'édition sortis de `macros.c`), `script_panel.c`, `files.c`, `logging.c`, `search.c`, `update.c`, `terminal_display.c`, `gtkterm.c` | GTK 3 | supprimée à la bascule (§11) |

Règles : rien dans le cœur ni dans la couche application n'inclut `<gtk/gtk.h>` (vérifié par grep, §12) ; `src/ui/` n'utilise aucune API marquée dépréciée ni
postérieure à 4.14 (vérifié par le compilateur, §7) ; aucun `extern` de variable d'interface hors de son module.

### 4.2 Extractions sur place (communes aux deux interfaces)

- **`macros.c`** devient GLib : état du jeu, chemin, ordonnanceur, `macros_send`, chargement/enregistrement. Les raccourcis (`GtkAccelGroup`) et la fenêtre d'édition
  GTK 3 partent dans `macros_gtk3.c` (déplacement sans réécriture). Les appels `show_message`, `Put_temp_message`, `rebuild_macro_buttons`,
  `macro_panel_polling_changed`, `config_remember_macros_file` deviennent des crochets `GtAppUi` (§4.3). `macros.h` n'inclut plus GTK ; `Config_macros` passe dans
  `macros_gtk3.h`. `script_engine.c` et son test ne dépendent alors plus de GTK.
- **`app_config.c`** reprend de `term_config.c` tout ce qui n'est pas un widget : façade du magasin (`config_file_init`, `config_get_store`, `config_reload_store`,
  `config_save_store`), écritures partielles (`config_remember_macros_file`, `config_save_update_url`), profil courant (`config`, nom actif, `app_config_load_profile`,
  `app_config_save_profile`, `app_config_delete_profile`, `Check_configuration_file`, `Verify_configuration`), état de fenêtre en valeur (`GtWindowState`, lu et
  écrit dans `[window]`). `term_config.c` (GTK 3) garde ses dialogues et appelle ces fonctions.
- **`update_check.c`** reprend de `update.c` le lancement asynchrone de `git ls-remote`, la comparaison de révisions et la construction de la commande du script ;
  les deux interfaces gardent leurs boîtes.

### 4.3 Crochets de l'interface (`app_ui.h`)

```c
/* Effets d'interface demandés par la couche application. Chaque interface remplit la table une fois au démarrage ;
 * NULL partout = aucun effet (tests). Fil principal seulement ; aucun crochet ne doit lancer de boucle imbriquée. */
typedef enum { GT_APP_MSG_INFO, GT_APP_MSG_WARNING, GT_APP_MSG_ERROR } GtAppMessageKind;
typedef struct
{
	void (*message)(GtAppMessageKind kind, const gchar *text);	/* boîte non bloquante */
	void (*status_flash)(const gchar *text, guint ms);	/* message temporaire de la barre d'état */
	void (*macros_changed)(void);	/* jeu remplacé ou modifié : panneau et raccourcis à reconstruire */
	void (*macro_polling_changed)(guint id, gboolean running);
	void (*profile_applied)(void);	/* profil chargé : terminal, options, couleurs à réappliquer */
} GtAppUi;
void           gt_app_ui_set(const GtAppUi *ui);
const GtAppUi *gt_app_ui_get(void);
```

Les tests d'intégration (`test_macros_send`, `test_app_config`) installent leur propre table au lieu des bouchons `stub_ui.c` d'aujourd'hui.

### 4.4 Démarrage et fermeture (`src/ui/app.c`)

`main()` : SIGPIPE ignoré → `config_file_init()` → gettext → lecture de la ligne de commande (`cmdline.c`, avant `g_application_run`, qui reçoit seulement
`argv[0]` ; `--help` et option inconnue sortent **avant** toute fenêtre) → `gtk_application_new()` → `g_application_run()`.
`startup` : crochets `GtAppUi`, actions et raccourcis, liaison par défaut, `macros_init()`, profil (`Check_configuration_file`, options de ligne de commande, validation).
`activate` : fenêtre principale, application du profil (`GtTermOptions`, terminal, couleurs), ouverture du port, signaux Unix (SIGUSR1/2), vérification de mise à jour
différée. **Une seule fermeture** : `app.quit` appelle `gtk_window_close()` sur la fenêtre principale ; `close-request` enregistre l'état de fenêtre et du panneau de
script, puis arrête dans l'ordre : moteur Lua, ordonnanceur et panneau de macros, liaison ; `shutdown` libère le reste. Fermer par la barre de titre et par
`app.quit` suivent donc le même chemin (annexe SP3 « File/Quit »).

## 5. État unique : `GtTermOptions` (cœur, nouveau)

```c
/* GtViewMode vient de view_render.h ; term_options.c l'enregistre comme GEnum (gt_view_mode_get_type(), nicks « ascii » / « hex »). */
#define GT_TYPE_TERM_OPTIONS (gt_term_options_get_type())
G_DECLARE_FINAL_TYPE(GtTermOptions, gt_term_options, GT, TERM_OPTIONS, GObject)

/* Propriétés (lecture-écriture, notify seulement si la valeur change) :
 *   "echo", "crlf-auto", "timestamp", "esc-clear-screen", "hex-show-index"  gboolean, FALSE
 *   "view-mode"           GtViewMode, GT_VIEW_ASCII
 *   "hex-bytes-per-line"  guint ∈ {8, 10, 16, 24, 32}, 16 ; une autre valeur est refusée (g_warning, valeur inchangée) */
GtTermOptions *gt_term_options_new(void);
void gt_term_options_apply_profile(GtTermOptions *o, const GtProfile *p);	/* les 4 drapeaux du profil ; un seul « changed » */
void gt_term_options_store_profile(GtTermOptions *o, GtProfile *p);	/* recopie inverse, pour l'enregistrement */
void gt_term_options_get_view(GtTermOptions *o, GtViewOptions *out);	/* pour view_render.c (mode, octets, index, écho) */
void gt_term_options_get_rx(GtTermOptions *o, gboolean *crlf_auto, gboolean *timestamp);	/* pour GtRxTransform */
/* Signal "changed" (sans argument) : émis une fois après toute modification, y compris un apply_profile qui en change plusieurs. */
```

- Lecteurs : la vue du terminal se redessine sur « changed » (un seul redessin par changement, fin des trois redessins au rechargement, annexe SP2) ; le journal et
  les captures lisent `gt_term_options_get_rx()` au moment voulu ; ESC efface l'écran est lu à la frappe.
- Menus : `g_property_action_new("local-echo", opts, "echo")`, `"crlf-auto"`, `"timestamp"`, `"esc-clear-screen"`, `"view-mode"` (état chaîne), `"hex-bytes"`
  (état `u`), `"hex-index"` ; `"auto-reconnect"` sur la propriété de la liaison ; `"show-macro-panel"`, `"show-script-panel"`, `"show-hex-bar"` sur la propriété
  `visible` des widgets. « Show index » et « Hexadecimal chars » restent toujours actifs : en vue ASCII ils changent l'option sans basculer la vue, et prennent effet au
  passage en hexadécimal (aujourd'hui ils basculaient l'affichage en hexa sans que le menu suive).
- Profil : `gt_term_options_apply_profile()` à l'application d'un profil (chargement, démarrage, ligne de commande) ; `gt_term_options_store_profile()` juste
  avant `app_config_save_profile()`. Le mode de vue, les octets par ligne et l'index ne sont pas enregistrés (comme aujourd'hui) ; au démarrage : ASCII, 16, sans index.

## 6. Interface GTK 4 (`src/ui/`)

### 6.1 Fenêtre principale (`main_window.c`)

`GtkApplicationWindow` ; barre de menus `GtkPopoverMenuBar` depuis `menus.ui` (ressource) ; de haut en bas : barre de menus, barre de recherche, `GtkPaned` vertical
[`GtkPaned` horizontal (terminal dans un `GtkScrolledWindow` | panneau de macros) / panneau de script], barre d'envoi hexadécimal, barre d'état. Titre
« GTKTerm - <texte d'état> » (plus « GTKTerm - GTKTerm » au démarrage). Taille par défaut 750×550, état restauré depuis `GtWindowState`.

### 6.2 Menus et actions (`app.c`, `menus.ui`)

| Menu | Entrées (msgid) → action | Raccourci |
|---|---|---|
| File | Clear screen → `app.clear-screen` ; Clear scrollback → `app.clear-scrollback` ; Send RAW file → `app.send-file` ; Save RAW file → `app.save-raw` ; Save ASCII file → `app.save-ascii` ; Quit → `app.quit` | Ctrl+Maj+L ; Ctrl+Maj+K ; Ctrl+Maj+R ; — ; — ; Ctrl+Maj+Q |
| Edit | Copy, Paste, Find, Select All → `app.copy`, `app.paste`, `app.find`, `app.select-all` | Ctrl+Maj+C/V/F/A |
| Log | To file... → `app.log-start` ; Pause (case) → `app.log-paused` ; Stop → `app.log-stop` ; Clear → `app.log-clear` | — |
| Configuration | Port → `app.port` ; Main window → `app.terminal` ; Local echo, Autoreconnect, CR LF auto, ESC clear screen, Timestamp (cases) ; Macros → `app.macros` ; Load macros file... ; Save macros file ; Load/Save/Delete configuration → `app.load-profile`, `app.save-profile`, `app.delete-profile` | Ctrl+Maj+S (Port) |
| Control signals | Send break → `app.send-break` ; Open Port → `app.open-port` ; Close Port → `app.close-port` ; Toggle DTR/RTS → `app.toggle-dtr`/`app.toggle-rts` | Ctrl+Maj+B ; F5 ; F6 ; F7 ; F8 |
| View | ASCII / Hexadecimal (radios `app.view-mode`) ; Hexadecimal chars › 8/10/16/24/32 (radios `app.hex-bytes`) ; Show index ; Send hexadecimal data ; Macro panel ; Script panel | — |
| Help | Check for updates… → `app.updates` ; About → `app.about` | — |

Sensibilités : actions de journal selon l'état du journal ; Send break et Toggle DTR/RTS seulement si la liaison est ouverte **en série** (d'après la liaison, plus
d'après `config.transport`) ; Copy selon la sélection du terminal. Menu contextuel du terminal : `vte_terminal_set_context_menu_model` (Copy, Paste, Find, Select All).

### 6.3 Terminal, recherche, barre d'état, envoi hexadécimal

- **`terminal_view.c`** reprend `terminal_display.c` : rendu par `view_render.c` depuis l'historique de la liaison et `GtTermOptions` ; `vte_terminal_feed` ;
  frappe (`commit`) → `gt_link_send(GT_SEND_KEYBOARD)` ; effacement sans l'octet NUL en trop ; copier par `vte_terminal_copy_clipboard_format`.
- **`search_bar.c`** : `GtkSearchBar` + `GtkSearchEntry` ; Entrée = suivant, Maj+Entrée = précédent, Échap ferme et retire le surlignage ; erreur d'expression
  affichée dans la barre (plus de boîte modale). `app.find` ouvre ou ferme selon l'état réel de la barre (`search-mode-enabled`).
- **`status_bar.c`** : `GtkBox` horizontal = libellé lié à `status-text` de la liaison (`g_object_bind_property`), message temporaire qui le remplace le temps voulu
  puis le rend (plus de pile), voyants DTR RTS CTS CD DSR RI relevés toutes les 100 ms **seulement** pendant qu'une liaison série est ouverte.
- **`hex_send_bar.c`** : saisie analysée par `hex_input.c` (cœur) : octets de 1 ou 2 chiffres hexadécimaux séparés par espace ou `;` ; « 1FF », « -1 », « G1 » refusés
  avec message dans la barre d'état, rien n'est envoyé ; une saisie vide n'entre pas dans l'historique ; historique de 50 entrées, ↑/↓, sans doublon consécutif.

### 6.4 Dialogues (`dialogs.c` et suivants)

- `dialogs.c` : `gt_ui_message(parent, kind, text)` (`GtkAlertDialog`, texte jamais interprété comme balisage), `gt_ui_confirm(parent, question, détail, libellé du bouton,
  rappel)`, `gt_ui_open_file`/`gt_ui_save_file` (`GtkFileDialog`, dossier et nom initiaux, confirmation d'écrasement native, rappel avec `GFile` ou NULL si annulé).
- **Port (`port_dialog.c`)** : instance unique, modale, transitoire (C12 conservé : une seconde demande la ramène au premier plan). Champs : Type (`GtkDropDown`
  Serial / TCP Client / TCP Server, correspondance explicite avec l'énumération) ; série : Port (entrée + bouton de liste des ports détectés, liens udev en gras),
  Baud Rate (entrée numérique + liste des débits standard), Parity, Bits, Stopbits, Flow control (`GtkDropDown`, libellés traduits, correspondance explicite) ;
  TCP : Host (défaut « localhost »), Port (1 à 65535, défaut « 2323 ») ; avancé (`GtkExpander`) : délai de fin de ligne 0-500 ms, attente d'un caractère (case +
  entrée d'un caractère ASCII, case cochée et entrée vide = erreur), RS-485 avant/après 0-500 ms. « OK » valide (`gt_profile_validate` + contrôles du dialogue) :
  erreur = message dans le dialogue, rien n'est appliqué ; sinon copie dans `config`, réouverture du port. Aucun port détecté : indication dans le dialogue (mode série),
  plus de boîte avant son ouverture. Annuler ne modifie rien (plus de `baudrate = 115200` posé à l'ouverture).
- **Terminal (`terminal_dialog.c` + `.ui` GTK 4)**, titre « Terminal » : police (`GtkFontDialogButton`, polices à chasse fixe), lignes de défilement (100-99 999),
  curseur bloc (`GtkSwitch`), couleurs du texte et du fond (`GtkColorDialogButton`) ; effet immédiat (comme aujourd'hui) par une seule fonction
  `terminal_view_apply_profile()` ; « Close ». Fichier `.ui` traduit (retiré de `POTFILES.skip`), plus de couleurs de libellés codées en dur.
- **Profils (`profile_dialogs.c`)** : Load / Delete = fenêtre modale avec `GtkListView` des profils (double-clic ou Entrée = OK) ; Delete demande confirmation.
  Save = nom prérempli avec **le profil actif**, validé par `gt_profile_name_validate` (message dans la fenêtre), doublon → `gt_ui_confirm` d'écrasement.
- **Fichiers de macros** : Load/Save par `GtkFileDialog` (dossier du fichier courant, nom « gtkterm_macros.ini »).

### 6.5 Macros

- **`macro_shortcuts.c`** : un `GtkShortcutController` de portée globale sur la fenêtre principale, phase de capture, reconstruit au crochet `macros_changed` ;
  raccourci d'une macro envoyable = touche consommée (M43) ; sinon la touche continue vers le terminal.
- **`macro_panel.c`** (réécrit) : onglets = boutons bascule groupés (`gtk_toggle_button_set_group`) dans un `GtkFlowBox` + `GtkStack` de pages ; mêmes règles
  d'affichage qu'aujourd'hui (macros avec libellé et action, séparateurs, liste de deux valeurs = deux boutons, arguments en entrées ou `GtkDropDown`, validation
  avec classe `error`, ⏱ et clignotement du polling). Clic droit sur un bouton (`GtkGestureClick`) → `GtkPopover` : case « Polling Mode » + champ « Period (ms) »
  appliqué à Entrée et à la fermeture (M22), période invalide = classe `error` + message dans le popover. Clic droit sur la barre d'onglets → popover de cases par
  onglet. Couleurs de thème (plus de couleurs codées en dur hors du clignotement). Une frappe dans un argument enregistre le fichier à la validation (Entrée ou perte
  du focus) seulement si la valeur a changé.
- **`macro_editor.c`** (réécrit, « Configure Macros ») : fenêtre non modale à instance unique, deux pages (`GtkStack`). Macros : `GtkColumnView` sur un `GListStore`
  de lignes (objet portant l'id stable, libellé, raccourci, action, onglet), cellules éditables (`GtkEditableLabel`), sélection multiple, **pas de tri par en-tête**
  (l'ordre est celui de l'utilisateur) ; boutons Add, Delete, Move Up, Move Down, Capture Shortcut (prochaine touche avec modificateur ; Échap annule ; raccourci
  déjà pris par une macro ou une action de l'application = refus expliqué), Delete Shortcut. Lists : même principe (List, Display, Value). Help (texte complet :
  échappements, `%d`/`%s`/`%#Liste`, séparateurs, polling), Cancel, OK (= `gt_macro_set_apply_edits` comme aujourd'hui). Fermer par la barre de titre = OK (comportement
  conservé). Un changement de jeu pendant l'édition ferme la fenêtre sans enregistrer (comportement conservé).

### 6.6 Panneau de script (`script_panel.c`, GtkSourceView 5)

Même fonctions qu'aujourd'hui (Open, Save, Run, Stop, Colors…, disposition haut/bas ou gauche/droite par menu contextuel, console bornée à 10 000 lignes, messages de fin
de script) sur `GtkSourceView` 5 ; barre d'outils = `GtkBox` de boutons ; dialogue des couleurs en `GtkWindow` modale avec `GtkColorDialogButton` (Reset sans fermer,
Cancel, Apply) ; état `[colors]`, `[layout]`, `[session]` inchangé dans son format (§3.2-15 pour `bg`/`fg`).

### 6.7 Fichiers, journal, mise à jour

- **`file_sender.c`** (cœur) : `GtFileSender` (GObject) envoie un fichier par la liaison (fonction d'écriture injectée), avec délai de fin de ligne ou attente d'un
  caractère reçu, progression (`progress`, octets envoyés / total en `guint64`), fin (`done` avec `GError`), annulation ; fichier vide = fin immédiate sans division
  par zéro ; minuterie gardée et retirée à l'annulation ; perte de liaison pendant l'attente = erreur « Error sending file ». **`file_send_window.c`** : fenêtre de
  progression transitoire de la fenêtre principale, Cancel.
- **`capture.c`** (cœur) : `gt_capture_save_raw(history, path, GError**)` et `gt_capture_save_ascii(history, crlf_auto, timestamp, path, GError**)` ; écriture
  vérifiée (`fwrite`/`fclose`) ; TAB, CR, LF et octets ≥ 0x80 conservés, autres contrôles retirés.
- **`rx_logger.c`** (cœur) : `GtRxLogger` (démarrer vers un fichier, pause, reprise, arrêt, vider), alimenté par le signal `received` ; la transformation CR/LF et
  horodatage est **nourrie aussi en pause** (annexe SP2) pour que la reprise ne coupe pas une ligne ; erreur d'écriture = arrêt + crochet `message`.
- **`update_check.c`** + **`update_window.c`** : vérification asynchrone annulable (`GCancellable`, annulée à la fermeture) ; « plus récente » = différente **et**,
  si la copie de travail du cache (`$XDG_CACHE_HOME/gtkterm/src`) existe, `git merge-base --is-ancestor <remote> <local>` n'indique pas que la locale est plus
  récente (M33) ; révision locale « unknown » : pas de proposition au démarrage ; « Skip this version » enregistré dans `[general] update_skipped_revision` (M34) ;
  arguments du script passés en liste (`g_spawn_async` avec `argv`, plus de `sh -c` construit par chaîne, M34).

## 7. Construction

- `meson.build` : dépendances `gtk4 >= 4.14`, `vte-2.91-gtk4 >= 0.76`, `gtksourceview-5 >= 5.12` pour `src/ui/` ; pendant la branche, les dépendances GTK 3 restent
  pour l'ancienne interface et disparaissent à la bascule.
- `src/ui/` est compilé avec `-DGDK_VERSION_MIN_REQUIRED=GDK_VERSION_4_14 -DGDK_VERSION_MAX_ALLOWED=GDK_VERSION_4_14` et
  `-Werror=deprecated-declarations` : toute API dépréciée ou postérieure à 4.14 casse la construction, même sur une machine plus récente (poste Arch).
  Équivalent pour GtkSourceView si ses en-têtes le permettent (sinon contrôle par la construction sur le poste Ubuntu 24.04).
- Ressources : `menus.ui`, `terminal_dialog.ui`, logo ; fichier de bureau renommé (§3.2-3) ; option meson `-Dtest_hooks=true|false` (défaut `true` : le
  crochet n'est actif que sous la variable d'environnement ; `false` le retire du binaire).

## 8. Erreurs et cas limites

- Aucun crochet `GtAppUi` ni rappel de signal de la liaison n'ouvre de boucle imbriquée ; les règles de réentrance de `link.h` restent vraies (ouvrir/fermer la liaison
  depuis un rappel de signal reste différé par `g_idle_add`).
- Dialogue fermé pendant une opération asynchrone (sélecteur de fichiers, vérification de mise à jour) : le rappel retrouve un objet détruit → il ne fait rien (pointeur
  faible ou `GCancellable`).
- Fenêtre principale fermée pendant un envoi de fichier, un script ou un dialogue modal : même ordre d'arrêt (§4.4), code de sortie 0, aucun avertissement.
- Configuration illisible : magasin en lecture seule comme aujourd'hui ; aucune écriture, l'état de fenêtre n'est pas enregistré.
- Valeurs aberrantes de `[window]` (tailles ≤ 0 ou > 10 000, positions de séparateurs négatives) : ignorées.

## 9. Harnais de la suite graphique GTK 4

D'après l'essai (annexe A), sous broadway GTK 4 **AT-SPI n'existe pas** et VTE 0.76 n'expose pas son texte. Le harnais GTK 4 (`tests/gui4/`, suite meson `gui4`
pendant la branche, renommée `gui` à la bascule) repose sur trois briques :

1. **Actions D-Bus** (`org.gtk.Actions` sur le bus de session privé) : `Activate`, `SetState`, `Describe`/`DescribeAll` sur les actions `app.*` ; démarrage détecté
   par l'apparition du nom `eu.mulagabriel.gtkterm.test.p<PID>` (plus d'attente fixe de 2 s).
2. **Crochet de test** (`src/ui/test_hooks.c`), actif seulement si `GTKTERM_TEST_HOOKS=1` : actions `app.test-dump` (état `aa{sv}` : pour chaque widget des fenêtres
   et popovers visibles, type, rôle accessible, libellé, nom de widget, visible/sensible/focus/actif/sélectionné, géométrie en coordonnées de surface, taille de
   surface, position de popup), `app.test-terminal-text` (texte du VTE, `vte_terminal_get_text_format`), `app.test-activate(s)` (clic programmé d'un widget par nom ou
   libellé), `app.test-set-text((ss))`. Les widgets visés par les tests portent un nom stable (`gtk_widget_set_name`) et un nom accessible.
3. **Faux navigateur broadway GTK 4** (`tests/gui4/broadway4.py`, d'après l'essai) : protocole numérique de `gtk4-broadwayd`, réponse immédiate aux `ROUNDTRIP`,
   souris (masque du bouton présent à l'appui **et** au relâchement) et clavier ; une surface est visée par son identifiant, appariée par sa taille au dump.
   Utilisé quand le geste est l'objet du test : frappe dans le terminal, raccourcis, menus par F10 et par clic, sélecteurs de fichiers (Ctrl+L), menus contextuels.

Environnement (`run-gui4-test.sh`) : `gtk4-broadwayd`, `dbus-run-session`, `XDG_RUNTIME_DIR` court (chemin de socket ≤ 108 octets), HOME et XDG_* privés,
`LD_PRELOAD` du garde-fou fichiers/ports de `guard.c` (sans le correctif clavier GTK 3, devenu inutile), aucune variable AT-SPI. Le fichier de mise à jour factice
(`bin/git`) est conservé.

## 10. Défauts et remarques traités

| Identifiant (revue) | Traitement |
|---|---|
| Arch. 1 (couches inversées), Arch. 3 (drapeaux dupliqués, globales de `files.c`) | §4, §5 ; globales supprimées |
| M33 (2e moitié), M34 (reste) | §6.7 |
| M35 (reste : TAB) | `capture.c` |
| M36 | GtkApplication + `GActionMap` (§6.2) |
| §5 `sscanf("%02X")`, `fic_defaut` partagé et fuites, `logfile_default`, titre « Main Window », chaînes non traduites | `hex_input.c` ; §3.2-16 ; `rx_logger.c` ; §6.4 ; §13 |
| §6 code mort (`Text`, `buffer`, `iter`, `adj`), doublons (`save_raw`/`save_ascii`, `clear_display`), motif `g_strdup_printf`+`show_message` | supprimés avec l'interface GTK 3 ; `capture.c` ; `GtAppUi.message` |
| Annexe SP1 : `Set_status_message` dépile le sommet ; nom de profil dans du balisage Pango ; `fwrite` non vérifié ; minuterie de `close_all` | §6.3 ; §6.4 (pas de balisage) ; `capture.c`, `file_sender.c` |
| Annexe SP2 Interface (3 redessins, Clear scrollback, `clear_display` ×2), Journal (pause), Fichiers, Suite graphique (port broadway fixe, orphelins) | §5 ; §3.2-11 ; §6.7 ; §9 (port dérivé du numéro d'affichage par scénario, processus tués par groupe) |
| Annexe SP3 : noms de profil, « No section default », File/Quit, double réécriture à la fermeture, `update_url` non relu, `.po` de `parsecfg.c` | §3.2-13/14 ; §4.4 (une seule écriture) ; §6.7 ; §13 |
| Annexe SP4 Interface et Tests (boîtes en double, « sent! », période invalide, code mort, chaînes, `try/finally`, attentes fixes) | §6.5 ; « Macro … sent! » seulement pour clic, raccourci et polling (pas pour Lua, spec Lua §1) ; §9 |
| Annexe SP5 : `run_on_main_sync` hors itération (boucle imbriquée) | disparaît : plus de boucle imbriquée |
| Ligne de commande : `-T serial`, `-a none`, `-w none` ignorés ; `--help` après la création de la fenêtre et code 1 | `cmdline.c` (§4.4) ; `--help` = code 0 |
| Cosmétique annexe SP4 (en-têtes de `macros_list.c/.h`, ChangeLog de `gtkterm.c`) | corrigés ; nouveaux fichiers formatés par `.clang-format` |

## 11. Bascule (dernière tâche d'implémentation)

Suppression de `interface.c/.h`, `term_config.c/.h`, `macros_gtk3.c/.h`, `macro_panel.c/.h`, `script_panel.c/.h`, `files.c/.h`, `logging.c/.h`, `search.c/.h`,
`update.c/.h`, `terminal_display.c/.h`, `gtkterm.c`, `data/config_terminal_dialog.ui` (GTK 3), `tests/gui/` (GTK 3), `tests/stubs/stub_ui.c` ; `gtkterm-gtk4`
renommé `gtkterm` et installé ; `tests/gui4/` renommé `tests/gui/` et la suite `gui4` renommée `gui` ; dépendances GTK 3 retirées de `meson.build` ;
`data/gtkterm-update.sh` construit avec les dépendances GTK 4 ; `CLAUDE.md`, `tests/README.md`, `po/POTFILES.in` à jour.

## 12. Tests

Tous automatiques, sans écran réel ni vrai port série (pty et 127.0.0.1).

**Unitaires (cœur)** : `test_term_options` (valeurs par défaut, notify seulement sur changement, `changed` unique pour `apply_profile`, valeur d'octets refusée,
aller-retour profil) ; `test_hex_input` (cas valides, « 1FF », « -1 », « G1 », séparateurs seuls, historique borné sans doublon) ; `test_capture` (RAW intact, ASCII
avec TAB/CR/LF/≥0x80 gardés, contrôles retirés, CR/LF auto et horodatage, erreur d'écriture sur un chemin interdit) ; `test_file_sender` (pty : envoi intact, délai de
fin de ligne, attente de caractère, fichier vide, annulation, perte de liaison) ; `test_rx_logger` (transformation, pause sans coupure de ligne, erreur d'écriture) ;
`test_profile` étendu (`gt_profile_name_validate`) ; `test_update_check` (comparaison de révisions, « unknown », révision ignorée, commande en liste).

**Intégration (couche application, sans GTK)** : `test_macros_send` adapté (table `GtAppUi` au lieu de `stub_ui.c` ; les 3 cas GTK 3 remplacés par des cas
`macro_shortcuts` dans la suite graphique) ; `test_app_config` (chargement, enregistrement, suppression, profil `default` absent, état de fenêtre, `x`/`y` ignorés et retirés) ;
`test_script_engine` sans GTK.

**Suite graphique GTK 4** : les 23 scénarios d'aujourd'hui portés (même vérification, nouveaux moyens) ; nouveaux scénarios : `test_fumee` (démarrage, dump, D-Bus,
fermeture), `test_menus_actions` (chaque élément de `menus.ui` vise une action existante ; cases du menu = état des options ; Ctrl+B part sur le port),
`test_port_dialog` (validation : port TCP 0, attente de caractère vide ; Annuler ne change rien ; C12), `test_terminal_dialog` (police, défilement, couleurs appliquées),
`test_hex_send` (refus de « 1FF », historique), `test_search` (ouvrir, Échap, rouvrir), `test_quit_menu` (`app.quit` enregistre `[window]`), `test_update_window`
(l'URL affichée est celle vérifiée, faux `git`), `test_macro_editor_raccourci` (conflit avec une action refusé, Échap annule), `test_status_bar` (message temporaire
rendu, voyants seulement en série).

## 13. Documentation et traductions

`CLAUDE.md` (architecture : couches, `src/ui/`, `GtTermOptions`, `GtAppUi`, actions `app.*`, harnais GTK 4, dépendances, commandes), `tests/README.md` (harnais GTK 4,
crochet de test, broadway4), revue (« Corrigé par le sous-projet 6 (`feature/refonte-gtk4`) » sur les identifiants du §10 ; annexe des remarques reportées),
`po/POTFILES.in` (nouveaux fichiers, `.ui`), `po/fr.po` régénéré et complété (msgids anglais du panneau de script, nouveaux libellés), `.pot` régénéré ; références
`#: ../src/parsecfg.c` disparues.

## 14. Critères d'acceptation

1. `meson setup` + `ninja` réussissent avec les seules dépendances GTK 4 : `grep -E "gtk\+-3.0|vte-2.91'|gtksourceview-4" meson.build src/meson.build` est vide.
2. `meson test -C build`, `meson test -C build-asan` et `meson test -C build --suite gui` passent (instabilité connue tolérée : `test_link` sous ASan, une relance).
3. `grep -rn 'gtk/gtk.h' src/*.c src/*.h` est vide (le cœur et la couche application sont sans GTK ; l'interface est dans `src/ui/`).
4. `grep -rnE 'gtk_dialog_run|GtkAccelGroup|GtkStatusbar|GtkTreeView|GtkComboBox|gtk_widget_show_all|GtkAction\b' src/` est vide.
5. `src/ui/` se construit avec `GDK_VERSION_MAX_ALLOWED=GDK_VERSION_4_14` et `-Werror=deprecated-declarations`.
6. Les globales `echo_on`, `crlfauto_on`, `timestamp_on`, `esc_clear_screen_on`, `fic_defaut` n'existent plus ; `config.echo`/`crlf_auto`/`timestamp`/
   `esc_clear_screen` ne sont lus que dans `gt_term_options_apply_profile` et écrits que dans `gt_term_options_store_profile` (et par la ligne de commande et le chargement de profil).
7. Le binaire installé s'appelle `gtkterm`, le fichier de bureau `eu.mulagabriel.gtkterm.desktop` ; `gtkterm --help` sort avec le code 0 sans ouvrir de fenêtre.

## Annexe A — Constats des inventaires et de l'essai (2026-10-09)

- Interface GTK 3 : 7 menus, 40 entrées, 15 accélérateurs ; 23 `gtk_dialog_run` ; drapeaux en 4 à 5 copies, gestionnaires `change-state` qui n'enregistrent pas
  l'état (cases désynchronisées) ; radios de vue non reliées (le menu coche « 8 » octets quand l'affichage en utilise 16) ; `Quit` sans enregistrement ;
  `Set_status_message` dépile le mauvais message ; `-T serial`, `-a none`, `-w none` ignorés sur la ligne de commande (corrigés au passage dans `cmdline.c`).
- Essai GTK 4 sous `gtk4-broadwayd` (programme jetable) : application absente d'AT-SPI (« Unrecognized accessibility backend » sous broadway) ; actions `app.*`
  joignables par `org.gtk.Actions` en 0,1 s avec ou sans navigateur (pas d'objet `win.*` exporté) ; crochet de dump et texte VTE fonctionnels ; faux navigateur :
  clics, frappe, Ctrl+L/Ctrl+F5/F5, F10, menus par clic et au clavier, dialogue modal repéré comme surface transitoire ; « b » ne déclenche plus Ctrl+B (le correctif
  clavier de `guard.c` est inutile en GTK 4) ; pièges : masque du bouton à garder au relâchement, toutes les surfaces en (0, 0), `XDG_RUNTIME_DIR` court.
- VTE 0.76 gtk4 n'importe aucun symbole `gtk_accessible_*` : le texte du terminal n'est lisible que par le crochet de test.
- Constat sur la suite GTK 3 : `run-gui-test.sh` cherche `/usr/lib/at-spi2-registryd`, absent d'Ubuntu 24.04 (`/usr/libexec/`) ; sans conséquence après la bascule.

## 15. Amendements issus du plan (2026-10-09)

Le plan `docs/superpowers/plans/2026-10-09-refonte-gtk4.md` précise ou corrige la spec sur ces points ; ils font autorité sur le texte des sections citées.

- §4.1 : `update_check.c`, `serial_ports.c` (détection des ports), `port_form.c` (validation du dialogue Port) et `macro_view.c` (règles d'affichage du panneau de
  macros) sont dans le cœur ; la couche application est une bibliothèque statique `gtkterm_app` sans GTK.
- §4.4 : la liaison par défaut et `macros_init()` sont créées dans `main()` avant la lecture de la ligne de commande (le chargement du profil charge son fichier de
  macros) ; SIGUSR1/SIGUSR2 sont gérés par `src/ui/app.c` et `user_signals.c` disparaît à la bascule ; `-c` est appliqué avant les autres options ; `-f` ne fait que
  proposer le fichier dans le sélecteur d'envoi.
- §5 : `changed` est émis **après** les `notify` des propriétés.
- §6.2 : menus sans mnémoniques (comme aujourd'hui). §6.3 : le titre de la fenêtre est tenu par la barre d'état.
- §6.4 Port : délai de fin de ligne et attente de caractère disponibles aussi en TCP (seul RS-485 est propre au série) ; débit non standard = avertissement non
  bloquant après OK ; titre « Port configuration ». Terminal : `po/POTFILES.skip` supprimé.
- §6.5 : « Configure Macros » n'est pas rattachée à l'application (sa capture de raccourci reçoit Ctrl+Maj+Q au lieu de quitter) ; sans Ctrl, Alt ni Super, seules
  F1-F35 sont acceptées comme raccourci ; `app.show-macro-panel` (comme `show-hex-bar`) est retirée à l'arrêt.
- §6.7 : la capture ASCII retire aussi DEL (0x7F) ; l'envoi de fichier attend aussi son délai ou son caractère après le dernier LF (comme aujourd'hui) ; la fonction
  d'écriture rend -1 si la liaison n'est pas ouverte ; `config_save_update_check_startup()` enregistre la case « au démarrage » de la fenêtre Updates.
- §9 : le harnais pose `GDK_DEBUG=no-portals` (GTK 4.14 ignore `GTK_USE_PORTAL`) et lance un bus de session privé sans dossiers de services.
- §12 : 46 scénarios `gui4` (liste dans le plan) ; `test_quit_menu` s'appelle `test_quit_etat`, `test_config_profils` devient `test_profils` ; Ctrl+B vérifié par
  `test_clavier_ctrl_b` ; `test_macros_send_affichage` n'est pas porté (ses cas sont couverts par les scénarios de raccourcis) ; nouveaux tests unitaires
  `test_serial_ports`, `test_port_form`, `test_macro_view`, `test_cmdline`, `test_app_config`.
- §13 : chaque tâche tient `po/POTFILES.in` pour ses fichiers ; la tâche de documentation vérifie seulement.
- §3.2-6 / §6.4 (amendé à l'exécution, tâche 13) : les boîtes de message et de confirmation (`gt_ui_message`, `gt_ui_confirm`) ne sont pas des
  `GtkAlertDialog` mais des fenêtres construites par GTKTerm (modale, transitoire, détruite avec son parent, texte brut, rappel unique différé sur sa
  destruction) : avec `GtkAlertDialog`, fermer l'application pendant une boîte produisait une erreur GTK, et la seule parade dépendait de détails internes
  de GTK 4.14.5 qui pouvaient virer à l'accès mémoire invalide sur un GTK plus récent. `GtkFileDialog` reste utilisé pour les sélecteurs de fichiers.
