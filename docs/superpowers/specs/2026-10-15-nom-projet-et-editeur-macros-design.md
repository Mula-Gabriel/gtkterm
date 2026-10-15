# Nom de projet, éditeur de macros (regroupement, filtres, rechargement, mise en page) et arguments facultatifs — spec

Date : 2026-10-15. Suite de 6c (projets), des arguments de macros et des modèles liés. Travail en direct sur `master` (dépôt poussé sur `Maison`).
Six parties validées par l'utilisateur :
1. un projet a un **nom** stocké dans son fichier ; « Rename project… » le change et propose de renommer aussi le fichier ;
2. les macros sont **regroupées par onglet du panneau**, les entrées de listes **par liste** (tri stable, automatique) ;
3. Configure Macros gagne un **filtre** « Tab: » (page Macros) et « List: » (page Lists) ;
4. une ligne en haut de l'éditeur montre **le fichier visé** (modèle, `.ini` ou « intégré au projet ») avec un bouton **Reload** ;
5. un argument peut être **facultatif** : vide, il n'envoie ni sa valeur ni son **préfixe** (` -password=`) ;
6. **mise en page** de l'éditeur : boutons sous le tableau, séparateur et espace avant les réglages d'argument.

## 1. Nom du projet

### 1.1 Format et modèle

- Clé facultative `name=` dans `[gtkterm-project]` (chaîne GKeyFile échappée ; `version` reste 1 ; un GTKTerm plus ancien l'ignore). Absente ou vide après
  `g_strstrip` : pas de nom. Un nom qui n'est pas de l'UTF-8 valide est rendu valide avec avertissement, comme le texte d'un script.
- `GtProjectData` gagne `gchar *name` (NULL : pas de nom). `gt_project_data_load/save` le lisent et l'écrivent ; `gt_project_data_snapshot()`
  **l'inclut** : renommer rend le projet « modifié ».
- `GtProject` gagne la propriété `name` (chaîne, NULL permise, notifiée seulement si elle change) : `gt_project_set_name(p, name)` (texte rogné,
  vide → NULL ; programme le recalcul de « modifié »), `gt_project_get_name()` renvoie dans l'ordre : le nom stocké, le nom du fichier sans `.gtkterm`
  (comportement actuel), « Untitled project » ; chaîne vide pour un projet fermé. `gt_project_capture()` copie le nom dans les données ; l'ouverture
  le reprend ; New project et Close project le remettent à NULL. Le titre de la fenêtre suit (`notify::name` déclenche la même mise à jour que `path`).
- Validation du nom : rogné, 1 à 128 caractères, aucun caractère de contrôle ; sinon le champ passe en style `error` et OK reste insensible.

### 1.2 Action « Rename project… »

- `app.project-rename`, menu File après « Save project as… », désactivée sans projet ouvert (même règle que `project-close`), sans raccourci.
- Fenêtre modale GTKTerm (pas de `GtkDialog`), transiente, `rename-project` : libellé « Project name: », `GtkEntry` `project-name` pré-rempli avec
  `gt_project_get_name()` (texte sélectionné), case `project-rename-file` « Also rename the file » affichée seulement si le projet a un fichier, cochée
  par défaut ; boutons Cancel / OK (`suggested-action`, Entrée). Une requête de projet en cours (dialogue de projet déjà ouvert) fait ignorer l'action,
  comme les autres actions de projet ; fermer le projet pendant que la fenêtre est ouverte la ferme sans rien appliquer.
- OK :
  1. si la case est cochée : nom de fichier cible = nom avec `/`, `\` et les caractères de contrôle remplacés par `_`, suffixe `.gtkterm`, dans le dossier
     du fichier actuel. Identique au chemin actuel : rien à déplacer. La cible existe : message « A file named %s already exists. », rien ne change
     (ni nom, ni fichier), la fenêtre reste ouverte. Sinon `g_rename()` ; échec : message « Cannot rename the project file: %s », rien ne change.
     Succès : `path` du projet mis à jour, tampon (`GtFileStamp`) repris sur le nouveau fichier, `last_project` réécrit (ordre relire → modifier →
     enregistrer de `app_config`). Le contenu du fichier n'est **pas** réécrit : le nom n'y entre qu'au prochain Save ;
  2. le nom est appliqué (`gt_project_set_name`), le projet devient modifié (sauf nom identique à celui stocké).
- Save as d'un projet sans fichier propose `<nom>.gtkterm` (même nettoyage) comme nom par défaut quand le projet a un nom stocké.
- `app.test-project` expose aussi `name` (le nom stocké, vide si aucun).

## 2. Regroupement

### 2.1 Règle

Tri **stable** : les groupes apparaissent dans l'ordre de la **première apparition** de leur clé ; à l'intérieur d'un groupe, l'ordre relatif est
conservé. Clé d'une macro : son `tab` (vide = « General », un groupe comme un autre) ; clé d'une entrée de liste : son nom de liste. Les identifiants
des macros sont conservés.

### 2.2 Cœur

- `gboolean gt_macro_set_group_by_tab(GtMacroSet *set)` (`macro_set.c`) : réordonne, TRUE si l'ordre a changé.
- Les listes d'un `GtMacroLists` sont déjà stockées par liste (une liste = un tableau d'entrées) : le regroupement est structurel côté cœur.
- Points d'appel du regroupement des macros (dans `macros.c`, couche application) : après toute lecture d'un `.ini` (`gt_macros_load_file`,
  `gt_macros_reload`, rechargement après conflit), à la lecture d'un modèle (`gt_macros_link_template`, `gt_macros_link_from_project` pour la base relue
  **et** la base en cache, `gt_macros_refresh_template` / `gt_macros_reload_template`), à l'ouverture d'un projet pour le jeu intégré d'un onglet non lié.
- **Modèle lié** : la base et le jeu effectif sont tous deux regroupés (le jeu effectif est une application des valeurs propres à la base regroupée),
  donc l'invariant « même structure » tient. La détection `UPDATED` de `gt_macros_link_from_project` compare les textes **après** regroupement des deux
  jeux : un modèle seulement mélangé n'est pas « mis à jour ». Les occurrences (§2.3 de la spec des modèles) sont calculées sur le jeu regroupé ;
  cas limite assumé et documenté : deux macros de même libellé dans des onglets différents peuvent échanger leurs occurrences pour un projet enregistré
  avant cette version (leurs valeurs propres passent alors de l'une à l'autre).
- Un fichier n'est jamais réécrit par le seul regroupement : le nouvel ordre n'y entre qu'à la prochaine sauvegarde (`.ini`, modèle ou projet).
  Le snapshot d'un projet ouvert est pris après regroupement, si bien qu'un projet ancien mélangé ne s'ouvre pas « modifié ».

### 2.3 Éditeur

- `gboolean gt_macro_rows_group_by_tab(GListStore *store)` et `gboolean gt_macro_list_rows_group(GListStore *store)` (`src/ui/macro_row.c`, sans
  affichage, testées par `test_macro_row`) : même règle, déplacements par `g_list_store_splice`, TRUE si l'ordre a changé.
- Appliqués à la construction des lignes, après Add / Add Entry, après la modification d'une cellule Tab / List (à la validation de la cellule, pas à
  chaque frappe), après Reload. La sélection suit les lignes déplacées (positions recalculées par identité d'objet).
- `gt_macro_rows_move()` ne fait plus franchir à une ligne le bord de son groupe : un Move Up sur la première ligne d'un groupe (ou Down sur la
  dernière) ne fait rien. Une sélection qui couvre plusieurs groupes : chaque ligne bouge dans son groupe, une ligne bloquée bloque les suivantes du
  même groupe (règle actuelle de blocage appliquée par groupe).
- Comme le jeu édité est regroupé, `gt_macro_set_apply_edits()` reçoit des lignes déjà dans l'ordre regroupé : aucun changement côté cœur.

## 3. Filtres de l'éditeur

### 3.1 Page Macros : « Tab: »

- `GtkDropDown` `macro-tab-filter` (nom accessible « Tab filter »), précédé du libellé « Tab: », au-dessus du tableau : « All », puis chaque clé
  dans l'ordre des groupes (le groupe vide affiché « General »). Recalculé quand les lignes changent (`items-changed`, `notify::tab`) ; si la clé
  choisie disparaît, le filtre revient à « All ». Le choix n'est pas enregistré (ouverture : « All »).
- Le `GtkColumnView` montre un `GtkFilterListModel` (`GtkCustomFilter` sur le `tab` de la ligne) au-dessus du `GListStore` ; la `GtkMultiSelection`
  porte sur le modèle filtré. Une fonction unique traduit les positions sélectionnées (filtrées) en positions du store, utilisée par Delete,
  Move Up/Down, Capture Shortcut, Delete Shortcut, la table des arguments et l'aperçu.
- Add sous un filtre : la nouvelle macro a le `tab` du filtre (« General » → vide) ; elle est insérée après la dernière ligne sélectionnée si elle
  appartient au groupe, sinon à la fin du groupe ; elle est sélectionnée.
- Move Up/Down sous un filtre : comme sans filtre (le groupe entier est visible, les bords de groupe arrêtent le déplacement).

### 3.2 Page Lists : « List: »

- `GtkDropDown` `lists-filter` (« List filter »), libellé « List: » : « All » puis chaque liste dans l'ordre ; mêmes règles (recalcul, retour à
  « All », non enregistré).
- Add Entry sous un filtre : l'entrée est créée dans la liste filtrée (`display`, `value` par défaut comme aujourd'hui) ; sans filtre : comportement
  actuel (« NewList »).

## 4. Ligne du fichier visé et Reload

- La ligne `macro-template-info` est remplacée par une boîte `macro-source` : libellé `macro-source-label` (sélectionnable, coupé aux mots/caractères)
  et bouton `macro-reload` « Reload ».
  - onglet lié : « Template: <chemin absolu> », bouton visible ;
  - onglet libre avec un fichier courant : « File: <chemin> », bouton visible ;
  - onglet de projet non lié : « Embedded in the project », bouton caché ;
  - onglet libre sans fichier : « No macros file », bouton caché.
  Mise à jour sur `edited` de la session et sur `gt_macro_editor_macros_changed()` (comme `template_info_update` aujourd'hui).
- Reload :
  1. si les lignes de l'éditeur diffèrent du jeu courant (même test que l'OK : `gt_macros_classify_edits()` ≠ `NONE`, ou listes différentes), question
     `gt_ui_choose` « Discard your changes and reload the macros file? » Reload / Cancel, Cancel par défaut, Reload `destructive-action` ; la réponse
     tient un `GWeakRef` sur la fenêtre et la génération (`gt_macros_get_generation`), et n'applique rien si l'une a changé ;
  2. onglet lié : `gt_macros_reload_template()` ; onglet libre : `gt_macros_reload()` ;
  3. l'éditeur **reste ouvert** : pendant l'appel, un drapeau `reloading` de l'état de l'éditeur fait que `gt_macro_editor_macros_changed()`
     reconstruit les lignes (macros et listes, regroupées), la table des arguments, l'aperçu et les filtres (filtre conservé si sa clé existe
     encore) et reprend `editor->set`, au lieu de détruire la fenêtre. Hors de ce drapeau, la règle actuelle (jeu remplacé ailleurs → fenêtre
     fermée sans rien appliquer) demeure ;
  4. échec (fichier illisible) : le message de `GtMacros` est aussi affiché dans `macro-editor-error`, rien ne change.
- Pendant une question en attente (Reload ou OK), le bouton Reload est insensible.

## 5. Arguments facultatifs

### 5.1 Modèle (cœur)

- `GtMacro` gagne `gchar **arg_optional` (`"1"` ou `""`) et `gchar **arg_prefix` (texte brut de l'action, échappements non traités) : NULL ou
  exactement autant d'éléments que d'arguments de format, jamais entièrement vides (ils deviennent NULL), comme `arg_min`.
- Quand le nombre d'arguments d'une macro change (`gt_macro_set_apply_edits`), ces deux vecteurs ne sont **pas** effacés comme les limites : ils sont
  gardés rang par rang, tronqués ou complétés par `""`. L'éditeur signale un préfixe non vide perdu par la troncature dans `macro-editor-error`
  (« Prefix of argument %d removed »), avant l'OK (à la modification de l'action).
- `GtMacroEdit` et `GtMacroRow` portent les deux vecteurs (`gt_macro_row_get_arg_optional/prefix`, `gt_macro_row_set_optional`) ;
  `gt_macro_set_copy/clone` les copient ; `gt_macro_set_same_structure()` les compare (NULL ≡ entièrement vide) : changer Optional ou Prefix change la
  structure (onglet lié : « Save to template / Detach »).
- `macro_arg.c` : `gt_macro_arg_describe()` ajoute « optional » et, s'il existe, « prefix "<préfixe>" ».

### 5.2 Expansion et envoi

- `gt_macro_expand()` et `gt_macro_expand_segments()` : pour un argument facultatif dont la valeur est **vide** (chaîne vide après résolution par
  `gt_macros_get_args` : champ vide sans valeur par défaut, entrée « (none) » d'une liste, `""` ou `nil` depuis Lua), rien n'est envoyé pour lui, ni
  préfixe ni valeur. Sinon : préfixe (échappements `\r`, `\n`, `\xHH`… traités comme dans l'action) puis la valeur formatée.
- Un argument non facultatif est inchangé (vide → `""` formaté comme aujourd'hui, `%d` vide → 0).
- `gt_macro_check_args()` / `gt_macros_check_args()` ne vérifient pas un argument facultatif vide (ni type ni limites).
- Segments de l'aperçu : le préfixe appartient au segment de l'argument ; un argument omis donne un segment vide de type argument.
- Lua `send_macro(id, args)` : `nil` pour un rang est équivalent à `""`.

### 5.3 Format

- Clés `arg_optional` et `arg_prefix` dans `[macro:N]` du `.ini` v2 et dans `[tab:N:macro:M]` du projet : listes GKeyFile (valeurs échappées, espaces de
  tête préservés), écrites seulement quand une valeur n'est pas vide ; `version` reste 2 / 1. Lecture tolérante : longueur différente du nombre
  d'arguments → tronquée ou complétée, avertissement nommant fichier, groupe et clé ; valeur d'`arg_optional` autre que `""`/`"1"` → `""` avec
  avertissement.
- Un GTKTerm plus ancien ignore ces clés et enverrait l'action sans le préfixe : signalé par un `!` dans NEWS.

### 5.4 Éditeur

- Table des arguments : deux colonnes ajoutées après Charset, `Optional` (`GtkCheckButton` `macro-optional-<k>`) et `Prefix` (`GtkEntry`
  `macro-prefix-<k>`, insensible quand la case est décochée).
- **Cocher** : découpage automatique (fonction pure `gt_macro_action_split_prefix(action, k, &new_action, &prefix)` dans `macro_row.c`, testée) — le
  texte qui précède le spécificateur de l'argument k dans l'action, en remontant jusqu'au premier espace (compris) sans franchir la fin du
  spécificateur précédent, une séquence d'échappement entière (`\x41` n'est jamais coupé) ni le début de l'action, est retiré de l'action et placé dans
  Prefix ; rien à prendre (spécificateur précédé d'un espace, d'un autre spécificateur, ou en tête) → préfixe vide, action inchangée. L'action de la
  ligne et la cellule Action sont mises à jour.
- **Décocher** : `gt_macro_action_join_prefix()` réinsère le préfixe juste avant le spécificateur k, le champ est vidé.
- Modifier Prefix à la main : appliqué à la ligne comme les autres réglages (vecteur gardé seulement si tous ses champs sont valides : un préfixe est
  toujours valide).
- Aperçu : préfixe et valeur en bleu ; argument omis avec les valeurs par défaut : `∅` grisé à sa place, aucun octet compté.

### 5.5 Panneau

- `macro_arg_widget.c` : une liste (`%#Liste`) facultative a une première entrée « (none) » (valeur `""`) ; c'est le choix initial quand la valeur
  par défaut est vide. Un `%s` ou nombre facultatif vide sans valeur par défaut a le texte indicatif « (omitted) ». Une valeur par défaut non vide
  reste envoyée quand le champ est vide (règle actuelle) : pour pouvoir omettre l'argument depuis le panneau, la valeur par défaut doit être vide.
- L'infobulle utilise `gt_macro_arg_describe()` (donc « optional, prefix " -password=" »). Un champ facultatif vide n'est jamais rouge.

## 6. Mise en page de l'éditeur

- Page Macros, de haut en bas : ligne « Tab: » + filtre ; tableau ; les deux rangées de boutons (Add, Delete, Capture Shortcut, Delete Shortcut /
  Move Up, Move Down) **juste sous le tableau** ; séparateur horizontal `macro-settings-separator` ; marge de 12 px ; table des arguments
  `macro-defaults` ; aperçu `macro-preview-box`. Séparateur et marge sont cachés avec la table quand la macro sélectionnée n'a pas d'argument (et
  qu'aucun aperçu n'est montré).
- Page Lists : ligne « List: » + filtre ; tableau ; boutons juste en dessous.
- En haut de la fenêtre : ligne `macro-source` (§4), puis le sélecteur de pages ; en bas : message d'erreur, séparateur, Help / Cancel / OK (inchangé).
- Taille par défaut portée à 900 × 640 pour les deux colonnes ajoutées.

## 7. Tests

- Unitaires : `gt_macro_set_group_by_tab` (stabilité, General, identifiants gardés, TRUE/FALSE) ; `test_macro_row` : regroupement des lignes et des
  entrées, déplacement borné au groupe, split/join du préfixe (espaces, échappements, spécificateur précédent, tête d'action) ; expansion avec
  argument facultatif (vide, rempli, liste « (none) », échappements du préfixe, limites ignorées si omis, segments) ; `macro_file` et `project_file` :
  aller-retour d'`arg_optional`/`arg_prefix` (espaces de tête), lecture tolérante, `name` (absent, présent, snapshot) ; `same_structure` ; `GtProject`
  (`name`, nom affiché, modifié) ; `GtMacros` : regroupement à la lecture, `UPDATED` insensible au seul mélange.
- GUI : renommer un projet sans fichier, avec déplacement du fichier (et cible existante refusée) ; filtre Tab et Add sous filtre ; regroupement après
  édition de la cellule Tab ; filtre List ; Reload d'un onglet libre et d'un onglet lié modifié sur disque (éditeur resté ouvert, question si lignes
  modifiées) ; cocher / décocher Optional (action et préfixe), envoi depuis le panneau champ vide puis rempli (octets lus sur le pty) ; dump de la
  mise en page (séparateur sous les boutons).
- Traductions dans `po/fr.po` ; CLAUDE.md et NEWS mis à jour.

## 8. Hors périmètre

Filtre enregistré entre deux ouvertures ; renommer les onglets du panneau depuis l'éditeur ; préfixe pour une autre partie que celle qui précède
l'argument (suffixe) ; arguments facultatifs dans le texte de l'action (syntaxe `%[ … %]`, écartée au profit du réglage par argument).
