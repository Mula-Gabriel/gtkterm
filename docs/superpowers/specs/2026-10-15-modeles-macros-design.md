# Modèles de macros liés et valeurs propres au projet — spec

Date : 2026-10-15. Suite du sous-projet 6c (projets) et du chantier des arguments de macros. Option B validée par l'utilisateur :
- un onglet de projet peut **importer** un fichier de macros (copie, comportement 6c) ou le **lier** comme **modèle** ;
- un onglet lié garde dans le projet ses **valeurs propres** (valeurs par défaut des arguments, réglage de polling) qui surchargent celles du modèle ;
- à l'OK de « Configure Macros » sur un onglet lié, GTKTerm demande : enregistrer dans le modèle (tous les projets qui l'utilisent) ou garder dans ce projet ;
- à l'ouverture du projet, le modèle est relu : s'il a changé, il est réimporté et les valeurs propres sont réappliquées ;
- une action « Détacher » transforme un onglet lié en copie.

## 1. Périmètre

Concerne seulement les **onglets de projet** (`in-project`). Un onglet libre a déjà son `.ini` comme fichier courant : rien ne change pour lui.
Hors périmètre : rafraîchir les autres **instances** de GTKTerm quand un modèle est réécrit (elles le relisent à la prochaine ouverture du
projet, et la garde d'identité du fichier empêche d'écraser une version plus récente ; les onglets de la même fenêtre, eux, sont rafraîchis : §2.8) ; chemins de modèle relatifs au projet (le chemin est absolu) ;
surcharger autre chose que les valeurs par défaut et le polling.

## 2. Décisions

### 2.1 État d'un onglet lié (couche application, `GtMacros`)

Un `GtMacros` peut être **lié** ; il garde, dans des champs **distincts** de `path`/`stamp` (qui restent ceux du `.ini` d'un onglet libre et que
`gt_macros_set_embedded(TRUE)` efface toujours) :
- `template_path` : chemin absolu du modèle ;
- `base` : le jeu du modèle tel que lu en dernier (`GtMacroSet`, à part du jeu courant) ;
- `template_stamp` : identité du fichier modèle (`GtFileStamp`), prise **avant** la lecture, pour la garde de conflit ;
- `generation` : compteur incrémenté à chaque changement du jeu effectif ou du lien (`replace_set`, application de lignes, polling par
  `gt_macros_edited`, lier, détacher, rafraîchir) ; une réponse asynchrone (§2.6, §2.7, §2.8) relève la génération en posant sa question et
  n'applique rien si elle a changé (ni comparaison de pointeurs de jeu, sujette à la réutilisation d'adresse).
`gt_macros_set_embedded(TRUE)` ne touche pas au lien ; `gt_macros_reload` et `gt_macros_save_file` ignorent le modèle (ils ne servent qu'au `.ini`
d'un onglet libre ou à l'export). Le lien n'a de sens qu'en projet, mais un `GtMacros` pas encore intégré peut le recevoir (`gt_macros_copy_from`
d'un nouvel onglet, passé en projet ensuite par le gestionnaire).

Le jeu courant (`gt_macros_get_set`, celui que montrent le panneau et l'éditeur et qu'envoient clics, raccourcis, polling et Lua) est le **jeu effectif** :
la base plus les valeurs propres. **Invariant** : jeu effectif et base ont la **même structure** (§2.2) ; ils ne diffèrent que par les valeurs par défaut
des arguments (`GtMacro.args`) et par le polling (`polling`, `polling_period_ms`). Les valeurs propres ne sont donc pas stockées à part en mémoire :
elles sont la **différence** entre base et jeu effectif, calculée à la demande (§2.3).

### 2.2 Même structure (`gt_macro_set_same_structure`, cœur)

Deux jeux ont la même structure quand ils ont le même nombre de macros et, rang par rang, mêmes `label`, `shortcut`, `action`, `tab`, mêmes vecteurs
`arg_min`, `arg_max`, `arg_charset` (NULL et vecteur entièrement vide sont égaux), et les mêmes listes (mêmes noms, dans le même ordre, mêmes entrées
affichage/valeur dans le même ordre). Les identifiants ne comptent pas.

### 2.3 Valeurs propres (`GtMacroOverrides`, nouveau module cœur `macro_override.c`)

- Une valeur propre désigne une macro par son **libellé** et son **rang parmi les macros de même libellé** (`occurrence`, 0 pour la première ; le
  libellé vide est un libellé comme un autre). Elle porte, par rang d'argument, une valeur par défaut (une chaîne quelconque, la chaîne vide comprise),
  et éventuellement le réglage de polling (mode + période).
- `gt_macro_overrides_diff(base, effective)` : base et effective de même structure (sinon NULL). Pour chaque macro de rang i : chaque argument k dont la
  valeur par défaut diffère (règle d'égalité unique, utilisée partout dans cette spec : `args` NULL équivaut à un vecteur de `""`) donne une valeur
  propre ; un polling (mode ou période) différent donne
  une valeur propre de polling. Macros sans différence : rien. Ordre : celui des macros.
- `gt_macro_overrides_apply(overrides, base, &dropped)` : copie de base (nouveaux identifiants) où chaque valeur propre est appliquée à la macro
  (libellé, occurrence). Abandons (comptés dans `dropped`, un par argument ou par polling) :
  - toute la valeur propre quand la macro (libellé, occurrence) n'existe plus ;
  - une valeur par défaut dont le rang dépasse le nombre d'arguments de la macro, ou refusée par `gt_macro_arg_check` avec le type et les limites de la
    macro du modèle (il a pu durcir ses limites) ; une valeur de liste (`%#Liste`) n'est pas vérifiée (règle des listes du jeu) ;
  - un polling quand la macro n'est pas envoyable (`gt_macro_is_sendable` : séparateur, action vide) ou que la période sort de
    [`GT_MACRO_PERIOD_MIN_MS`, `GT_MACRO_PERIOD_MAX_MS`] (jamais ramenée dans les bornes). Une macro envoyable sans argument (« AT\r ») garde son polling.
- Conséquence assumée du calcul par différence : une valeur propre égale à la valeur du modèle n'est plus une valeur propre ; si le modèle prend un
  jour la valeur choisie dans le projet, les deux se confondent et le projet suit ensuite le modèle pour cette valeur.
- Une liste de valeurs propres lue dans un fichier est **normalisée** : une seule entrée par (libellé, occurrence) (la dernière gagne, avertissement),
  dans l'ordre de lecture.
- Les valeurs propres ne contiennent jamais les valeurs d'usage du panneau (jamais enregistrées) ni l'état de marche du polling.

### 2.4 Format dans le projet (`project_file.c`)

`[tab:N]` gagne la clé `macros_template` (chemin absolu, écrite seulement pour un onglet lié). Pour un onglet lié :
- `[tab:N:macro:M]` et `[tab:N:list:K]` contiennent la **base** (copie du modèle lors du dernier enregistrement du projet), pas le jeu effectif ;
- les valeurs propres sont des groupes `[tab:N:override:K]` (K = 1, 2, … dans l'ordre) : `label` (toujours écrite), `occurrence` (écrite si ≠ 0),
  `arg_<k>` (k à partir de 1, une clé par argument surchargé, valeur écrite par `g_key_file_set_string`), `polling` (booléen) et
  `polling_period_ms` (même nom de clé que le format des macros) écrits ensemble quand le polling est surchargé ; à la lecture, l'un sans l'autre est
  ignoré avec un avertissement, et une période hors bornes est gardée telle quelle pour être abandonnée par `apply` (comptée dans `dropped`).
Un onglet non lié n'a ni `macros_template` ni groupe `override` : son format ne change pas. La version du projet reste 1 : un GTKTerm plus ancien ignore la
clé et les groupes `override` et ouvre la copie de la base (il perd les valeurs propres et le lien, rien d'autre). À la lecture, un groupe `override` sans
`label` ou dont l'`occurrence` est négative ou non numérique (groupe entier ignoré : il viserait une autre macro), une clé `arg_<k>` dont k n'est pas un entier ≥ 1, une période illisible : la valeur concernée (ou le
groupe sans libellé) est ignorée avec un `g_warning` qui cite fichier, groupe et clé. `macros_template` vide ou relatif : onglet non lié, avec un
`g_warning`.

`GtProjectTab` gagne `gchar *macros_template` (NULL : non lié) et `GtMacroOverrides *overrides` (jamais NULL, vide si non lié). L'**instantané** de
comparaison (« modifié ») d'un onglet lié contient `macros_template` et les valeurs propres mais **pas la base** : un modèle réimporté à l'ouverture ne rend
pas le projet « modifié » ; la copie de la base dans le fichier n'est rafraîchie qu'au prochain enregistrement du projet.

### 2.5 Ouverture d'un projet

Pour chaque onglet lié (`replace_all_tabs`, onglet en projet, avant la création de la page comme la copie actuelle), `gt_macros_link_from_project(m, template, cached_base, overrides, &dropped, &error_text)` :
- le modèle est relu (identité prise avant la lecture). Lu : la base devient le jeu lu ; état `UPDATED` si son texte (`gt_macro_set_to_key_file` avec le
  préfixe vide, sérialisé) diffère de celui de la base du projet, sinon `UNCHANGED` ;
- illisible ou absent : la base est la copie du projet, état `MISSING`, `error_text` = message de l'erreur ; l'onglet **reste lié** (le chemin est gardé :
  un support démonté revient), identité telle que relevée (absent : `exists` FALSE) ;
- jeu effectif = `gt_macro_overrides_apply(overrides, base)`, installé par `gt_macros_replace_set` (usage et polling remis à zéro comme tout chargement).

L'interface (`replace_all_tabs` de `app.c`), après la création de la page de l'onglet :
- `UPDATED` : `status_flash` de 5 s « Macros template %s updated » (nom de base du fichier) ; avec `dropped` > 0 : « Macros template %s updated: %u
  project values no longer apply » ;
- `MISSING` : message (boîte non bloquante, avertissement) « Cannot read the macros template %s: %s. The copy saved in the project is used. » ;
- `UNCHANGED` avec `dropped` > 0 (modèle inchangé mais valeurs invalides dans un projet modifié à la main) : même flash que `UPDATED` avec abandon.
**« Modifié » :** après la liaison, si `dropped` vaut 0, les valeurs propres de cet onglet dans les données lues (celles passées ensuite à
`gt_project_begin` comme référence) sont remplacées par le diff du jeu vivant (`gt_macro_overrides_diff(base, effectif)`) : un fichier écrit à la main
(ordre, doublons, valeur propre égale au modèle) ne rend pas le projet « modifié ». Avec `dropped` > 0, les données lues gardent leurs valeurs : l'instantané
diffère, le projet est « modifié » et l'enregistrement retire les valeurs abandonnées.

### 2.6 Lier, importer, détacher

- **Load macros file…** dans un onglet de projet : après le choix du fichier, `gt_ui_choose` « Use this macros file in the project » (texte secondaire : le
  chemin), boutons « Copy into the project » (défaut), « Link as template », « Cancel » ; aucun destructeur.
  - Copie : `gt_macros_load_file(m, path, TRUE)` comme en 6c ; dès que le jeu est remplacé (réussite, ou échec non interactif qui installe un jeu vide),
    l'onglet devient **non lié** (la copie remplace tout) ; un échec interactif ne change rien (message, jeu inchangé, le lien reste).
  - Lien : `gt_macros_link_template(m, path)` : lecture interactive ; échec, **fichier absent compris** : message « Cannot load macros file: … », rien ne
    change ; succès : base = jeu lu, jeu effectif = copie de la base (aucune valeur propre), modèle = `path` rendu absolu ; usage et polling remis à zéro.
  - La réponse arrive de façon asynchrone : elle n'applique rien si la session est fermée, n'est plus en projet, ou si la génération a changé (§2.1).
  - Dans un onglet libre : inchangé (le fichier devient le fichier courant), sans question.
- **Détacher** : action d'onglet `detach-macros-template` (relais `app.detach-macros-template`, menu Macros « Detach from template » après « Save macros
  file… »), activée seulement quand l'onglet est lié. `gt_macros_detach_template(m)` : oublie modèle, base et identité ; le jeu effectif reste tel quel
  (les valeurs propres deviennent les valeurs de la copie) ; ni polling ni usage touchés ; `edited` (le projet devient modifié). Flash 3 s « Detached
  from macros template %s ».
- `gt_macros_set_embedded(m, FALSE)` (fermeture du projet, onglet libre) détache aussi. `gt_macros_load_file` détache dès que le jeu est remplacé (intégré ou non).
- **Nouvel onglet** d'un projet : copie du jeu de l'onglet actif **et de son lien** (même modèle, copie de la base, même identité, mêmes valeurs propres) :
  `gt_macros_copy_from(dst, src)`.
- Le moment où l'état lié change (lier, détacher, ouverture) appelle `gt_macros_edited` : la session met à jour l'activation de
  `detach-macros-template` dans son gestionnaire du rappel `edited`.

### 2.7 OK de « Configure Macros » sur un onglet lié

**Listes :** `gt_macro_set_apply_edits` prend possession de ses listes ; les nouvelles fonctions de `GtMacros` qui reçoivent des lignes prennent un
`const GtMacroLists *` et en font une copie (`gt_macro_lists_copy`) pour **chaque** application (simulation comprise) ; l'éditeur libère les listes qu'il
a construites.

**Chemin unique d'application :** `gt_macros_commit_edits(m, rows, n, lists)` = `gt_macros_apply_edits` (copie des listes) + `gt_macro_poller_sync` +
`gt_macros_changed` (panneau, raccourcis, `edited`). C'est le chemin de l'éditeur pour tous les cas (non lié compris : il remplace les trois appels
actuels d'`apply_edits()` de `macro_editor.c`, suivis comme aujourd'hui de `gt_ui_macros_save` pour un onglet libre).

Le jeu effectif n'est pas touché avant la réponse. `gt_macros_classify_edits(m, rows, n, lists)` simule l'application sur un **clone** du jeu effectif qui
garde les identifiants (`gt_macro_set_clone`, cœur, réservé aux simulations : le clone n'est jamais installé) et rend :
- `NONE` : même structure (§2.2) et `gt_macro_overrides_diff(effectif, simulé)` vide → l'éditeur se ferme sans rien appliquer ;
- `DEFAULTS` : même structure, diff non vide (seules des valeurs par défaut changent ; l'éditeur ne change pas le polling) ;
- `STRUCTURE` : tout autre changement (libellé, raccourci, action, onglet, ajout, suppression, ordre, listes, limites).

Onglet non lié : comportement actuel, sans question. Onglet lié, `gt_ui_choose(editeur, text, detail, …)` (pas de titre : `text` est la phrase en gras,
`detail` le texte secondaire) :
- `DEFAULTS` → text « These macros come from the template %s » (nom de base), detail « Keep the new default values in this project only, or save them to
  the template, for every project that uses it? », boutons « Keep in this project » (défaut), « Save to template », « Cancel ».
- `STRUCTURE` → même text, detail « These changes can only be saved to the template (every project that uses it), or the tab can be detached from the
  template and keep its own copy. », boutons « Save to template », « Detach and keep a copy », « Cancel » (défaut). Aucun destructeur.
- « Keep in this project » : `gt_macros_commit_edits` (valeurs d'usage des macros dont le défaut change effacées) ; l'éditeur se ferme.
- « Detach and keep a copy » : `gt_macros_detach_template` puis `gt_macros_commit_edits` ; l'éditeur se ferme.
- « Save to template » : `gt_macros_save_template_edits(m, rows, n, lists, force=FALSE)` (§2.8), **l'éditeur restant ouvert** : `SAVED` → il se ferme ;
  `FAILED` → message, il reste ouvert (rien n'est perdu) ; `CONFLICT` → question de conflit (§2.8), il reste ouvert jusqu'à la réponse.
- « Cancel », -1 (Échap, boîte fermée) : l'éditeur **reste ouvert**, rien n'est appliqué.
**Durée de vie :** le rappel reçoit une `GWeakRef` sur la fenêtre de l'éditeur (jamais le `MacroEditor`, libéré avec elle) et la génération relevée ;
fenêtre détruite (session fermée, jeu remplacé) ou génération changée (détachement par le menu, polling, autre éditeur…) : rien n'est appliqué (si la
fenêtre vit encore, elle reste ouverte). `MacroEditor` a un indicateur « question en cours » : pendant une question, `on_ok` ne fait rien et
`on_close_request` rend TRUE (fermeture refusée) ; l'indicateur retombe à la réponse. La fermeture par la barre de titre vaut OK, donc pose la même question
et ne ferme que si la réponse le dit.

### 2.8 Enregistrer dans le modèle

`gt_macros_save_template_edits(m, rows, n, lists, force)` → `GT_MACROS_SAVED`, `FAILED` ou `CONFLICT` :
1. `before` = clone du jeu effectif ; `after` = clone où les lignes sont appliquées (`gt_macro_set_apply_edits`).
2. Nouvelle base = `gt_macro_overrides_merge_template(base, before, after)` : copie de `after` ; pour chaque macro de `after` présente dans `before` (même
   identifiant ; i = `gt_macro_set_index_of(before, id)`, rang de la macro de base correspondante, base et before ayant la même structure), et pour chaque
   rang d'argument k présent dans les deux avec le **même type** (`macro_arg_info_t.type`, et même nom de liste pour une liste) : si la valeur par défaut
   **n'a pas changé** dans cette édition (règle NULL ≡ `""`), elle reprend `base[i].args[k]` — à condition que `gt_macro_arg_check` l'accepte avec le
   type et les limites de `after` (listes : non vérifiées), sinon la valeur de `after` est gardée — : la valeur propre du projet ne fuit pas dans le
   modèle et reste une valeur propre ; une valeur changée va au modèle (la valeur propre de ce rang disparaît, base et effectif devenant égaux). Rangs de
   type changé ou nouveaux : valeur de `after` (si l'action change, la valeur propre d'un rang de même type est gardée, celle d'un rang de type changé
   part au modèle : cas rare, assumé). Le polling reprend celui de `base[i]` (l'éditeur ne le change pas). Macros nouvelles : telles quelles.
3. Garde d'identité : si le modèle a changé sur le disque depuis sa dernière lecture ou écriture (`gt_file_stamp_changed`) et `!force` → `CONFLICT`, rien
   n'est écrit ni appliqué. Un modèle marqué absent (ouverture `MISSING`) qui existe maintenant est aussi un conflit.
4. Écriture `gt_macro_file_save(nouvelle base, template)` ; échec → `FAILED`, message « Cannot save macros template: … » (interactif), rien n'est appliqué.
5. Succès : identité reprise après l'écriture ; base = nouvelle base ; `gt_macros_commit_edits` sur le jeu effectif (mêmes lignes) ; invariant tenu
   (même structure, valeurs propres restantes) ; flash 3 s « Macros template %s saved ».

`CONFLICT` → l'éditeur (encore ouvert, indicateur « question en cours ») pose `gt_ui_choose` text « The macros template changed on disk. », detail le
chemin, boutons Overwrite (destructeur) / Reload / Cancel (défaut), avec la même référence faible et la génération. Overwrite : même appel avec
`force=TRUE` sur les lignes de l'éditeur (relues à ce moment). Reload : `gt_macros_reload_template(m)` (modèle relu, valeurs propres actuelles
réappliquées par `gt_macros_refresh` ci-dessous ; le jeu étant remplacé ou mis à jour, l'éditeur se ferme — ses modifications sont perdues, c'est le sens
de Reload ; échec de lecture : message, rien ne change, l'éditeur reste). Cancel / -1 : l'éditeur reste ouvert.

**Onglets frères :** après un `SAVED`, l'interface rafraîchit chaque **autre** session de la fenêtre liée au même chemin : `gt_macros_refresh_template(m)`
relit le modèle (échec : rien, avertissement) puis applique `gt_macro_overrides_diff(ancienne base, effectif)` à la nouvelle base. Si le résultat a la même
structure que le jeu effectif, la mise à jour se fait **en place** (valeurs par défaut et réglages de polling recopiés rang par rang, valeurs d'usage
effacées pour les macros dont le défaut change, `gt_macro_poller_sync`, `gt_macros_changed`) : le polling en marche continue ; sinon
`gt_macros_replace_set` (polling arrêté, usage effacé, éditeur de cet onglet fermé). `gt_macros_reload_template` suit la même règle.

### 2.9 Polling et panneau

Le popover de polling change le jeu effectif (comme aujourd'hui, `gt_macros_edited`) : sur un onglet lié, c'est une valeur propre, sans question. Les
champs d'argument du panneau restent des valeurs d'usage, jamais enregistrées ni dans le modèle ni dans le projet.

### 2.10 Interface d'information

- « Configure Macros » d'un onglet lié montre en haut un libellé `macro-template-info` : « Template: %s » (chemin complet), caché pour un onglet non lié.
- Le crochet de test `test-sessions` gagne la clé `template` (chemin, `""` si non lié).

### 2.11 Couches

- Cœur : `macro_override.c/.h` (`GtMacroOverride`, `GtMacroOverrides`, diff, apply, merge_template, key file), `macro_set.c`
  (`gt_macro_set_same_structure`, `gt_macro_set_clone`), `project_file.c` (clé, groupes, instantané).
- Application : `macros.c` (état lié, §2.1, 2.5–2.8), `project.c` (`gt_project_capture` : pour un onglet lié, base copiée dans `t->macros`, chemin, valeurs
  propres par diff), `session.c` (action `detach-macros-template`).
- Interface : `profile_dialogs.c` (question de Load macros file…), `macro_editor.c` (classification, questions, libellé), `macros_save.c` (conflit du
  modèle), `app.c` (`replace_all_tabs`, `gt_app_add_session`, `tab_action_names`), `menus.ui`, `test_hooks.c`.

### 2.12 Messages (anglais, traduits dans `po/fr.po`)

« Use this macros file in the project », « Copy into the project », « Link as template », « Detach from template », « Detached from macros template %s »,
« These macros come from the template %s », les deux textes de §2.7, « Keep in this project », « Save to template », « Detach and keep a copy », « Macros
template %s saved », « Macros template %s updated », « Macros template %s updated: %u project values no longer apply » (pluriel par `ngettext`), « Cannot
read the macros template %s: %s. The copy saved in the project is used. », « Cannot save macros template: %s », « The macros template changed on disk. »,
« Template: %s », « %s: [%s] %s: invalid value ignored » (avertissement de lecture).

## 3. Tests

- Unitaires cœur : `test_macro_override` (diff, apply avec abandons — macro disparue, rang dépassé, limite durcie, période hors bornes —, libellés en
  double et vides, chaîne vide surchargée, merge_template : argument inchangé garde la base, argument changé va au modèle, macro ajoutée/supprimée,
  key file aller-retour et valeurs invalides), `test_macro_set` (same_structure, clone garde les ids, edits_copy), `test_project_file` (aller-retour d'un
  onglet lié, ancien format inchangé, instantané sans la base, lecture tolérante, normalisation des doublons).
- Intégration (`test_macros_send` ou nouveau `test_macros_template`, contre les bouchons) : lier, classer des modifications, garder dans le projet,
  enregistrer dans le modèle (contenu du fichier : valeur propre non fuitée ; valeur reprise refusée par de nouvelles limites), échec d'écriture (rien
  appliqué), conflit puis force, rechargement, rafraîchissement en place (polling en marche conservé) et par remplacement, détacher, génération,
  link_from_project UPDATED / UNCHANGED / MISSING avec abandon, lien sur fichier absent refusé, copy_from avant set_embedded(TRUE) (lien gardé),
  set_embedded(FALSE), listes non consommées par classify.
- `test_project` : capture d'un onglet lié (base, chemin, valeurs propres), « modifié » insensible à une base réimportée.
- GUI : `tests/gui/test_macros_modele.py` — projet, Load macros file… → « Link as template » ; Configure Macros : changer une valeur par défaut → « Keep in
  this project » → fichier modèle inchangé, panneau montre la nouvelle valeur ; projet enregistré (clés `macros_template`, groupe `override`) ; modèle
  réécrit sur le disque (autre valeur par défaut) ; projet rouvert → flash « updated », valeur propre conservée et nouvelle valeur du modèle visibles ;
  changer une autre valeur → « Save to template » → fichier modèle à jour, la valeur propre n'y est pas ; changement de libellé → question STRUCTURE →
  « Detach and keep a copy » → `template` vide dans `test-sessions` ; `app.detach-macros-template` désactivée.
