# Ouverture silencieuse, éditeur de macros agrandissable, arguments drapeaux

Demande de l'utilisateur (2026-10-15) :

1. « À l'ouverture, les ports qu'on n'arrive pas à ouvrir, on les passe directement en tête de mort, la popup est trop envahissante. »
2. « Autorise la page config macro à être en plein écran. »
3. « Les arguments optionnels qui n'ont que le préfixe (typiquement `config DHCP -save`) transformés en case à cocher : décocher on n'envoie pas
   `-save`, cocher on envoie `-save`. »
4. « Quand on ouvre un fichier de macro, propose de créer un projet, et de l'importer sous forme de template, ne l'ouvre pas forcément en direct. »

Décisions prises en autonomie (consigne du chantier), consignées ici.

## 1. Échec d'ouverture : tête de mort, sans boîte

### 1.1 Modèle (`port_activity.c`, cœur)

- `gt_port_activity_open_failed(a, now_us)` : l'ouverture vient d'échouer. Tant que la liaison est `CLOSED`, `LOST` ou `RECONNECTING`, l'état est
  `dead` tout de suite (sans attendre le délai sans données ; même avec ce délai à 0, l'échec est un fait, pas une attente).
- La marque d'échec est effacée par `gt_port_activity_reset()` (toute ouverture ou fermeture demandée) et par l'entrée en `OPEN` ou `LISTENING`
  (reconnexion réussie). `CONNECTING` ne l'efface pas mais a priorité (le port essaie : roue), comme aujourd'hui.

### 1.2 Session

- La session écoute `open-failed` de sa liaison et appelle `gt_port_activity_open_failed()` : **tout** échec d'ouverture donne la tête de mort, demandé
  ou non.
- `gt_session_open_port_quiet(s)` : ouverture **automatique** ; `gt_session_open_port(s)` : ouverture demandée. La session retient le mode de la
  dernière ouverture (`gt_session_get_open_quiet()`), lu au moment de `open-failed` (un client TCP échoue après coup, depuis une source inactive).
- `gt_session_close_port()` appelle `gt_port_activity_reset()` après la fermeture : un port fermé à la main n'est plus une tête de mort.

### 1.3 Interface (`app.c`)

- Ouvertures automatiques (`gt_session_open_port_quiet`) : premier onglet au démarrage, onglets recréés à l'ouverture d'un projet (démarrage,
  Open project, Reload après un conflit), SIGUSR1. Ouvertures demandées (boîte gardée) : Open Port, OK du dialogue Port, nouvel onglet (l'utilisateur
  vient de régler le port), chargement d'un profil.
- `on_open_failed` d'une ouverture silencieuse : pas de boîte ; la raison s'affiche 8 s dans la barre d'état de l'onglet
  (`gt_app_status_flash`, préfixée du nom court quand il y a plusieurs onglets, comme la boîte) ; la bulle de l'onglet garde `status-text`.
- Le profil chargé au démarrage n'ouvre pas le port lui-même (`activate` l'ouvre après) : rien à changer pour lui.

## 2. « Configure Macros » agrandissable

- GTK 4 n'offre les boutons Réduire / Agrandir qu'à une fenêtre souveraine (ni modale, ni `transient-for`) ; l'éditeur, non modal, n'est plus
  `transient-for` la fenêtre principale : bouton Agrandir, double-clic sur le titre, et F11 bascule le plein écran (`gtk_window_fullscreen` /
  `unfullscreen`, capturé par le contrôleur de touches existant de l'éditeur, hors capture d'un raccourci : F11 seul reste capturable pendant
  « Capture Shortcut »).
- Ce que donnait `transient-for` est gardé à la main : la fenêtre principale est retenue (pointeur faible) comme parent des questions
  (`gt_ui_macros_save`), et l'éditeur est détruit avec elle (`destroy` de la fenêtre principale).

## 3. Arguments drapeaux

### 3.1 Modèle

- `arg_optional[k]` vaut `""` (requis), `"1"` (facultatif) ou **`"flag"`** (drapeau) — clé `arg_optional` du `.ini` v2 et du projet, lecture tolérante
  (toute autre valeur devient `""`). Un drapeau n'est permis que sur un `%s` (ailleurs, lu `"1"`).
- Un drapeau est facultatif (`gt_macro_arg_is_optional` vrai) ; `gt_macro_arg_is_flag(m, k)`.
- **Valeur** : présent si la valeur résolue n'est ni vide ni `"0"`. Défaut enregistré `"1"` (coché) ou `""` (décoché). Valeur d'usage du panneau :
  `"1"` ou `"0"` (`"0"` est non vide : il l'emporte sur un défaut coché, la règle « vide = défaut » reste vraie). Lua `send_macro` : `"1"` / `"0"`
  / `""`.
- **Expansion** : présent → le préfixe seul (la valeur n'est jamais envoyée) ; absent → rien, comme un facultatif omis. Aperçu : préfixe en bleu,
  « ∅ » quand absent.
- **Vérification** : un drapeau n'a ni limites ni jeu de caractères (ignorés, jamais une cause de refus) ; valeur propre d'un modèle lié toujours
  acceptée.

### 3.2 Éditeur

- Colonne `Flag` (après Prefix) : case `macro-flag-<k>`, seulement pour un `%s`, sensible quand Optional est cochée. La cocher écrit `"flag"`, la
  décocher `"1"` ; décocher Optional décoche aussi Flag.
- Pour un drapeau, la cellule Default est une case (`macro-default-<k>`, cochée = `"1"`), Min / Max / Charset sont insensibles.

### 3.3 Panneau

- Un drapeau est une case à cocher `macro-<id>-arg-<k>` dont le libellé est le préfixe sans espaces de tête (`-save`), cochée selon la valeur
  résolue ; basculer écrit la valeur d'usage `"1"` / `"0"`. « Reset arguments » la ramène au défaut.

### 3.4 Compatibilité

- `"flag"` n'existe que depuis la v2.2.0, comme `arg_optional` ; la ligne `!` de NEWS sur les versions plus anciennes couvre déjà la perte des
  préfixes.

## 4. « Load macros file… » d'un onglet libre

- Le fichier choisi n'est plus chargé d'office : question « Open this macros file » (le chemin en détail), **Create a project with this template**
  (par défaut) / Open directly / Cancel, sans bouton destructeur.
- Create : les onglets libres deviennent un projet sans fichier (« Untitled project », `gt_project_begin(project, NULL, NULL, NULL)` comme Save as
  depuis des onglets libres), puis l'onglet actif lie le fichier (`gt_macros_link_template`). Le projet devient modifié : quitter pose la question
  Save / Don't save / Cancel habituelle. Un modèle illisible ouvre la boîte « Cannot load macros file », le projet reste ouvert (macros intégrées).
- Open directly : l'ancien comportement (chargement, chemin retenu dans le profil).
- La réponse ne fait rien si la session est fermée, est passée en projet ou si sa génération de macros a changé (comme la question d'un onglet de
  projet). Un onglet de projet garde sa question « Use this macros file in the project ».

## 5. Tests

- Unitaires : `test_port_activity` (échec → dead, effacé par reset et par OPEN, CONNECTING prioritaire), `test_session` (open-failed → dead, mode
  silencieux retenu, fermeture → closed), `test_macro_set` / `test_macros_format` (drapeau : présent → préfixe seul, `"0"` et `""` → rien, `"flag"`
  hors `%s` → `"1"`), `test_macro_file` (aller-retour `flag`), `gt_macro_check_args` (drapeau jamais refusé).
- GUI : `test_ouverture_silencieuse` (démarrage sur un port absent : aucune boîte, onglet `dead`, flash dans la barre d'état ; Open Port
  garde la boîte), `test_macros_drapeau` (case du panneau, envoi avec et sans `-save`, colonne Flag de l'éditeur), éditeur non `transient-for`
  (`test-dump` : `transient` faux ; F11 n'est pas vérifiable sous broadway, qui ne gère pas le plein écran), `test_macros_ouvrir_modele` (Cancel,
  Create : projet sans fichier, onglet lié, macro envoyée, question en quittant). Les scénarios d'onglets libres qui chargent un `.ini` répondent
  « Open directly » (`load_macros_free` de `gtktest.py`).
