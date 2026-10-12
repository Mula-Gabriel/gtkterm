# Sous-projet 6c — Projets : un fichier qui rouvre tous les onglets, avec leurs macros et leurs scripts

- **Date** : 2026-10-10
- **Branche** : `feature/refonte-projets` (fusionnée `--no-ff` dans `master`, écrasée en commits thématiques, puis supprimée en local et sur Maison)
- **Entrées** : spec 6b `docs/superpowers/specs/2026-10-10-refonte-onglets-design.md` (hors périmètre : persistance des onglets, partage d'un fichier de
  macros, annexes SP3/SP4 → 6c) ; revue `docs/superpowers/reviews/2026-10-06-revue-code.md`, annexes « sous-projet 3 » et « sous-projet 4 » ; master au merge
  `6ba85b3`.
- **Décisions** : celles de l'utilisateur au cadrage (§3.1) ; les autres prises en autonomie (consigne de l'utilisateur pour le chantier), §3.2.

## Contexte

6b a rendu chaque onglet indépendant (`GtSession` : liaison, profil, options, `GtMacros`, `GtScriptEngine`), mais rien ne survit à la fermeture sauf les
profils de `.gtktermrc`, le dernier fichier de macros d'un profil et `[window]`/`[layout]`/`[session]`. Les macros vivent dans des `.ini` séparés ; le
panneau de macros réécrit le `.ini` à chaque argument changé, et deux onglets (ou instances) sur le même `.ini` s'écrasent sans prévenir.

## Objectif

**Un projet est un fichier unique qui contient tout un poste de travail** : les onglets, chacun avec ses réglages de port et de terminal, l'état de sa page,
**une copie** de son jeu de macros et **une copie** du texte de son script Lua. Ouvrir le projet rouvre tous les onglets ; GTKTerm rouvre le dernier projet au
démarrage. En usage normal, les valeurs d'arguments choisies dans le panneau de macros ne sont plus enregistrées ; leurs valeurs par défaut se règlent dans
l'éditeur de macros.

## Hors périmètre

- Plusieurs projets ouverts à la fois ; liste de projets récents (seul le dernier est retenu) ; projet qui référence des fichiers externes (macros ou
  scripts) au lieu de les copier.
- Fusion de projets, import d'un onglet d'un autre projet.
- Les remarques de la revue qui ne touchent ni au magasin, ni à la migration, ni à `macro_file.c` (harnais, cœur, ligne de commande : au fil de l'eau).

## 3. Décisions

### 3.1 Prises par l'utilisateur au cadrage (2026-10-10)

1. **Un projet = un ensemble d'onglets.** Chaque onglet du projet garde ses réglages (port, terminal), son jeu de macros et son script.
2. **Le projet porte une copie des macros** (pas un chemin vers le `.ini`) et **une copie du script Lua** de chaque onglet.
3. **Un seul fichier projet**, enregistré où l'on veut, facile à copier ou envoyer.
4. **Arguments et listes du panneau de macros** : ce qu'on y saisit ou choisit en usage normal n'est jamais enregistré (ni `.ini`, ni projet). La période et
   le mode de polling restent enregistrés.
5. **Valeurs par défaut des arguments** : elles se règlent dans l'éditeur de macros (« Configure Macros ») et sont enregistrées avec la macro.
6. **Démarrage** : sans argument, GTKTerm rouvre le dernier projet. **Enregistrement explicite** ; à la fermeture avec des changements non enregistrés :
   Enregistrer / Ne pas enregistrer / Annuler.
7. **Menus revus** : ouvrir un nouveau port passe par un menu **Port › New port…** ; les menus sont réorganisés de façon générale (§3.2-10).
8. **Éditeur de macros, colonnes** (fait au début de la branche, `c1ef7bc` et suivant) : largeur fixe de départ, seule la dernière colonne prend la place
   restante, de sorte qu'un redimensionnement ne déplace qu'un bord ; une colonne n'est jamais plus étroite que son titre.

### 3.2 Prises en autonomie

1. **Format** : GKeyFile, extension `.gtkterm`. En-tête `[gtkterm-project]` (`version=1`, `active=N`), un groupe `[tab:N]` par onglet (N = 0, 1, … dans
   l'ordre des onglets) avec les clés d'un `[profile:…]` plus l'état de page et `script` (texte échappé par GKeyFile), les macros de l'onglet en
   `[tab:N:macro:M]` et `[tab:N:list:K]` avec les clés du format v2 de `macro_file.c`. Lisible et modifiable à la main ; pas de blob `.ini` dans une clé.
2. **Pas de duplication de format** : la lecture/écriture d'un profil dans un groupe nommé et celle d'un jeu de macros sous un préfixe de groupe deviennent
   des fonctions du cœur réutilisées par `.gtktermrc`, les `.ini` et le projet (`gt_profile_to_key_file`/`gt_profile_from_key_file`,
   `gt_macro_set_to_key_file`/`gt_macro_set_from_key_file`). Les fichiers existants restent identiques octet pour octet à contenu égal (tests de
   non-régression sur les formats).
3. **Onglets libres et onglets de projet** : sans projet ouvert, les onglets fonctionnent comme en 6b (profils dans `.gtktermrc`, macros dans un `.ini`).
   Dès qu'un projet est ouvert ou enregistré, **tous** les onglets de la fenêtre en font partie : il n'y a jamais de mélange.
4. **Macros d'un onglet de projet** : le jeu est « intégré » (`GtMacros` sans chemin, mode projet). Configure Macros et le polling modifient le jeu en
   mémoire, jamais un fichier ; « Load macros file… » importe une copie d'un `.ini` dans l'onglet ; « Save macros file » exporte le jeu vers un `.ini` choisi
   (dialogue d'enregistrement), sans lier l'onglet à ce fichier. Un nouvel onglet d'un projet reçoit une copie du jeu de l'onglet actif.
5. **Script d'un onglet de projet** : le texte de l'éditeur est celui du projet. « Open… » charge un fichier dans l'éditeur (le texte entre dans le projet à
   l'enregistrement) ; « Save » écrit le texte dans un `.lua` (export), sans changer le projet. `[session] script_path` n'est plus lu ni écrit pour un onglet
   de projet. Un nouvel onglet d'un projet démarre avec un script vide.
6. **Arguments** : `GtMacro.args` (déjà enregistré dans les `.ini` v2) devient « valeurs par défaut ». Les valeurs d'usage vivent dans `GtMacros` (table
   identifiant → arguments, jamais enregistrée) ; le panneau démarre sur les valeurs par défaut, les envois (clic, raccourci, polling, Lua `send_macro`)
   utilisent les valeurs d'usage s'il y en a, sinon les valeurs par défaut. Le panneau n'appelle plus d'enregistrement quand un argument change. Le menu du
   clic droit d'un bouton gagne « Reset arguments » (valeurs d'usage effacées). Charger ou remplacer un jeu efface les valeurs d'usage.
7. **Éditeur de macros** : sous le tableau, une zone « Default values » montre un champ par argument de format de la macro sélectionnée (entrée pour `%s`,
   bouton numérique pour `%d`, liste déroulante pour `%#Liste` avec les valeurs de la liste), reconstruite quand l'action change ; OK applique les valeurs par
   défaut avec le reste de la ligne (`gt_macro_set_apply_edits`, par identifiant). Une macro sans argument de format n'affiche rien.
8. **Changements non enregistrés** : pas de suivi événement par événement. Le projet garde l'instantané texte de son dernier enregistrement (ou ouverture) ;
   « modifié » = l'instantané courant (même sérialisation, **sans** l'état de page ni l'onglet actif ni les valeurs d'usage) diffère. Le calcul est relancé
   à l'inactivité (`G_PRIORITY_DEFAULT_IDLE`, regroupé, au plus une fois par 300 ms) après : `macros_changed`/`macro_polling_changed`, `changed` de
   `GtTermOptions`, `profile_applied`, ajout/fermeture/déplacement d'onglet, texte du script modifié. À la fermeture, le calcul est refait d'office.
9. **Titre** : « nom du projet • » quand il est modifié, « nom du projet » sinon, suivi du nom court de l'onglet actif ; sans projet, titre de 6b.
10. **Menus réorganisés** (ordre : File, Edit, View, Port, Terminal, Macros, Log, Help). Les noms d'actions `app.*` et les raccourcis existants ne changent
    pas ; seuls l'emplacement et le libellé des entrées changent (« configuration » devient « profile » dans les libellés).
    - **File** (le projet est le document) : New project, Open project… (**Ctrl+Maj+O**), Save project, Save project as…, Close project | Send RAW file…,
      Save RAW file…, Save ASCII file… | Quit. Nouvelles actions d'application (pas d'onglet) : `project-new`, `project-open`, `project-save`,
      `project-save-as`, `project-close` ; `project-save` sans fichier = Save as ; pas de raccourci pour Save (Ctrl+Maj+S est pris).
    - **Edit** : Copy, Paste, Select all, Find | Clear screen, Clear scrollback.
    - **View** : inchangé (ASCII, Hexadecimal, Hexadecimal chars, Show index | Send hexadecimal data, Macro panel, Script panel).
    - **Port** (remplace « Control signals ») : New port… (`new-tab`, Ctrl+Maj+T : dialogue Port prérempli, nouvel onglet), Close tab (`close-tab`,
      Ctrl+Maj+W) | Settings… (`port`), Open port, Close port, Autoreconnect | Send break, Toggle DTR, Toggle RTS | Load profile…, Save profile…, Delete
      profile….
    - **Terminal** : Settings… (`terminal`, ex-« Main window »), Local echo, CR LF auto, ESC clear screen, Timestamp.
    - **Macros** : Configure macros… (`macros`), Load macros file…, Save macros file… (export dans un onglet de projet, §3.2-4).
    - **Log** : inchangé. **Help** : inchangé. Le menu contextuel du terminal ne change pas.
    Les libellés changés reçoivent leur traduction dans `po/fr.po` ; les autres catalogues gardent leurs traductions quand le libellé anglais ne change pas
    et affichent l'anglais sinon (régénération des `.po`).
11. **Ouvrir un projet** remplace tous les onglets : confirmation si un onglet est occupé (script ou envoi, comme Close tab), puis question
    Enregistrer / Ne pas enregistrer / Annuler si le projet courant est modifié. Le fichier est lu et validé **en entier avant** de toucher aux onglets : un
    fichier illisible, d'une autre version ou abîmé est refusé avec un message, rien n'est fermé. Les onglets sont ensuite créés dans l'ordre, chacun avec son
    profil (validé comme un profil chargé), sa page, ses macros et son script, et ouvre son port ; l'onglet `active` est affiché.
12. **New project** : mêmes confirmations, puis un seul onglet avec le profil `default` (ou les défauts), sans macros ni script ; le projet est sans fichier
    (« Untitled project ») tant qu'il n'est pas enregistré. **Close project** : mêmes confirmations, puis un onglet libre avec le profil `default` (retour à
    l'état 6b) ; `last_project` est effacé.
13. **Save project as** sur des onglets libres : les macros de chaque onglet sont copiées depuis son jeu en mémoire (pas relues du `.ini`), le texte du
    script depuis l'éditeur ; les onglets deviennent des onglets de projet (§3.2-4, 3.2-5).
14. **Dernier projet** : `[general] last_project` (chemin absolu) écrit à chaque ouverture ou enregistrement réussi, effacé par Close project. Au démarrage :
    `--project FICHIER` ouvre ce projet ; sinon, si la ligne de commande ne donne ni profil (`-c`) ni réglage de port, le dernier projet est rouvert ; un
    fichier absent ou refusé → message (après la présentation de la fenêtre, comme les autres messages de démarrage) et démarrage de 6b. `--project` avec un
    réglage de port : refusé à l'analyse de la ligne de commande (message, code 1).
15. **Écriture sûre et conflits** : projet et `.ini` sont écrits par `g_file_set_contents` (remplacement atomique). Un petit type du cœur `GtFileStamp`
    (périphérique, inode, taille, mtime en µs) retient l'identité lue ou écrite ; avant d'écrire, si le fichier a changé sur le disque depuis, GTKTerm demande
    **Overwrite / Reload / Cancel** (Reload : relit le fichier, valeurs d'usage effacées). Pour un `.ini` d'onglet libre, c'est la fin du « le dernier
    enregistrement gagne » silencieux entre onglets et instances. Un enregistrement non interactif (sans fenêtre) ne demande rien et n'écrase pas : il
    échoue avec un avertissement.
16. **Ce qui reste hors du projet** : `[window]` (taille de fenêtre), `[colors]`/`[layout]` globaux (couleurs et disposition par défaut du panneau de script),
    `[general]`, les profils nommés de `.gtktermrc` (Load/Save configuration restent disponibles dans un onglet de projet : charger applique le profil à
    l'onglet, ce qui modifie le projet ; enregistrer écrit le profil dans `.gtktermrc`).
17. **Remarques reportées traitées** (revue, annexes SP3 et SP4) :
    - `macro_file.c` : un échappement invalide dans `args`, `displays` ou `values` ne fait plus perdre la liste ou la macro (la valeur brute est gardée et un
      avertissement est émis) ; le refus « une copie différente existe déjà » indique le remède (renommer ou supprimer `<fichier>.avant-migration`) ; une liste
      sans entrée est relue (groupe conservé) ; un fichier présent mais illisible donne la vraie cause (droits) au lieu du message de lecture de GLib.
    - Magasin : un fichier devenu illisible en cours de session donne la vraie cause au chargement d'un profil (pas « No section ») ; un `put`/`delete` en
      mémoire est annulé si l'enregistrement qui suit échoue.
    - Migration : `transport_type` et `wait_char` non numériques sont refusés (valeur par défaut + avertissement) au lieu d'`atoi` tolérant.

## 4. Architecture

### 4.1 Couches

- **Cœur** (`gtkterm_core`, GLib) : `profile_keyfile.c` (profil ↔ groupe GKeyFile, extrait de `config_store.c`), `macro_file.c` réorganisé (jeu ↔ groupes
  préfixés ; fichier `.ini` = préfixe vide), `file_stamp.c` (`GtFileStamp`), `project_file.c` (`GtProjectData` ↔ GKeyFile : lecture validée en entier,
  écriture, instantané texte pour la comparaison).
- **Application** (`gtkterm_app`) : `GtMacros` (mode intégré, valeurs d'usage, garde d'identité du `.ini`), `GtSession` (`in-project`, texte de script
  confié par l'interface), `project.c` (`GtProject` : chemin, instantané enregistré, `last_project`, capture des sessions → `GtProjectData`, état modifié,
  propriété `modified`), `cmdline.c` (`--project`).
- **Interface** : `app.c` (actions `project-*`, ouverture qui crée les onglets, confirmations, titre, démarrage), `menus.ui` (menus réorganisés, §3.2-10),
  `macro_editor.c` (zone « Default values »), `macro_panel.c` (plus d'enregistrement sur argument, « Reset arguments »), `script_panel.c` (texte du projet,
  Open/Save en import/export), `dialogs.c` inchangé (`gt_ui_choose` sert aux questions à trois boutons).

### 4.2 `GtProjectData` (cœur)

```c
typedef struct
{
	GtProfile profile;
	GtWindowState page;	/* h_paned, v_paned, visibilités ; width/height/maximized ignorés */
	gchar *script_layout;	/* "topbottom" | "leftright" */
	GtMacroSet *macros;
	gchar *script;		/* texte, "" si aucun */
} GtProjectTab;

typedef struct
{
	GPtrArray *tabs;	/* GtProjectTab *, au moins un */
	guint active;
} GtProjectData;

gboolean gt_project_data_load(const gchar *path, GtProjectData **out, GtFileStamp *stamp, GError **error);	/* tout ou rien */
gboolean gt_project_data_save(const GtProjectData *d, const gchar *path, GtFileStamp *stamp, GError **error);
gchar   *gt_project_data_snapshot(const GtProjectData *d);	/* sans page, active ni valeurs d'usage : sert à « modifié » */
void     gt_project_data_free(GtProjectData *d);
```

`GtWindowState` passe du fichier `app_config.h` à un en-tête du cœur (`window_state.h`) pour être partagé.

### 4.3 `GtProject` (application)

Un seul projet par fenêtre, tenu par l'application (`src/ui/app.c`, statique permise « état de l'application »). Il ne crée pas d'onglets : l'interface lit
`GtProjectData` et appelle `gt_app_add_session` pour chaque onglet. `gt_project_capture(sessions, page_state_cb)` construit un `GtProjectData` depuis les
sessions (profil via `gt_term_options_store_profile`, macros copiées, texte du script et état de page fournis par l'interface par rappel).

## 5. Flux

- **Enregistrer** : capture → `GtFileStamp` vérifié (conflit → Overwrite/Reload/Cancel) → `gt_project_data_save` → instantané retenu → `last_project` écrit
  (relecture du magasin d'abord, règle de `.gtktermrc`) → titre sans « • ».
- **Ouvrir** : confirmations → `gt_project_data_load` (refus = message, rien ne change) → arrêt de toutes les pages et sessions (ordre 6b) → création des
  onglets → instantané retenu → `last_project` écrit.
- **Envoyer une macro** : `macros_send(id)` prend les valeurs d'usage de `GtMacros` si présentes, sinon `GtMacro.args`.

## 6. Erreurs et cas limites

- Version inconnue, groupe `[gtkterm-project]` absent, aucun onglet, onglet sans profil valide → refus du fichier entier, message précis.
- Profil d'un onglet hors bornes (débit, etc.) → corrigé et averti comme un profil chargé (`gt_profile_validate`), le projet est marqué modifié.
- Port d'un onglet absent à l'ouverture → l'onglet est créé quand même (état de liaison et reconnexion de 6b), pas de boîte par onglet au-delà de
  `open-failed`.
- Fichier projet en lecture seule → Save échoue avec un message ; Save as reste possible.
- Fermeture de GTKTerm par SIGTERM ou session qui se termine : pas de question, rien n'est enregistré.
- Décisions prises pendant l'exécution :
  - Un Save project as… qui échoue depuis des onglets libres laisse un projet « Untitled project » modifié (macros intégrées), comme New project ; l'utilisateur peut relancer Save as.
  - Les onglets recréés à l'ouverture d'un projet reprennent `lock_port` (-L) de l'onglet actif d'avant.
  - New project ne modifie pas `last_project` (seuls une ouverture ou un enregistrement réussi l'écrivent, Close project l'efface).
  - Une demande de projet qui arrive pendant une autre (un dialogue de projet déjà ouvert) est ignorée.
  - `GtFileStamp` retient l'heure de modification en secondes et nanosecondes, non en microsecondes comme écrit au §3.2-15.
- Décisions de la relecture finale :
  - Dans les questions de conflit (Overwrite / Reload / Cancel, projet et `.ini`), Cancel est le bouton par défaut (focus, Entrée) et Overwrite porte
    `destructive-action` : Entrée n'écrase jamais le travail d'un tiers. Reload remplace les onglets : il passe par la confirmation « Close all tabs? » si
    un script tourne ou qu'un fichier part.
  - Save project as… : quand « .gtkterm » est ajouté au nom tapé et que ce fichier existe, GTKTerm demande avant de le remplacer (le sélecteur n'a confirmé
    que le nom tapé) ; Cancel n'écrit rien.
  - Un `last_project` qui n'existe plus alors que son dossier existe est effacé au démarrage (le message le dit) ; tout autre refus (droits, dossier absent car support démonté, fichier abîmé) garde
    la clé.
  - L'onglet libre de Close project charge ses macros comme au démarrage (`macros_file` du profil `default`, sinon le fichier par défaut). Un onglet sans
    fichier de macros courant ne réécrit jamais en silence un fichier par défaut présent et non vide : c'est un conflit (question en interactif,
    avertissement sans écriture sinon).
  - Open project relit le fichier s'il a été réécrit pendant les questions (Save du projet courant qu'on rouvre).
  - Modifier les valeurs par défaut d'une macro dans l'éditeur efface ses valeurs d'usage ; une valeur non UTF-8 d'un projet modifié à la main est rendue
    valide (octets remplacés) avec un avertissement.

## 7. Tests

**Unitaires (cœur)** : `test_profile_keyfile` (aller-retour, identique à l'ancien `config_store` octet pour octet), `test_macro_file` étendu (préfixes,
`.ini` inchangé, échappement invalide gardé, liste vide relue, fichier illisible), `test_file_stamp`, `test_project_file` (aller-retour complet, script
multiligne et caractères spéciaux, version inconnue, fichier abîmé refusé en entier, instantané insensible à l'état de page et à l'onglet actif).

**Intégration** : `test_macros_send` (valeurs d'usage prioritaires, Reset, effacées au remplacement du jeu, jamais enregistrées ; mode intégré : aucune
écriture de fichier ; conflit d'identité : écriture refusée en non interactif), `test_app_config` (`last_project`), `test_cmdline` (`--project`, refus avec
un réglage de port), `test_project` (capture de deux sessions, « modifié » après changement de macros, d'option, d'onglet ; non modifié après déplacement
d'un séparateur), `test_config_store` (cause réelle d'un fichier devenu illisible ; `put` annulé après échec).

**Suite graphique** : `test_projet` (Save as avec deux onglets puis Close project puis Open : onglets, ports, macros, script et onglet actif restaurés ;
titre et « • »), `test_projet_demarrage` (dernier projet rouvert ; absent → message et démarrage normal ; `-p` → pas de projet), `test_projet_fermeture`
(question Enregistrer/Ne pas enregistrer/Annuler, Annuler garde la fenêtre), `test_projet_macros` (import et export d'un `.ini`, Configure Macros ne
touche aucun fichier, nouvel onglet avec copie du jeu), `test_macros_arguments` (argument changé non enregistré, valeur par défaut réglée dans l'éditeur
et enregistrée, Reset arguments), `test_macros_conflit` (deux onglets libres sur le même `.ini` : question Overwrite/Reload/Cancel) ; `test_menus_actions` et les scénarios qui ouvrent
un menu par clics adaptés aux nouveaux emplacements (même action atteinte, mêmes raccourcis) ; `test_macro_editor_colonnes` (largeur minimale = titre, un
redimensionnement ne bouge qu'un bord) si le glisser broadway sur un en-tête est fiable.

**Relecture finale** : `test_macros_send` (fichier par défaut présent sans fichier courant : conflit ; valeurs d'usage effacées quand l'éditeur change les
valeurs par défaut), `test_project_file` (script non UTF-8 rendu valide) ; `test_projet_conflit` (suffixe ajouté au nom d'un fichier existant, conflit du
fichier projet avec Cancel par défaut et les trois choix, Reload avec un script en cours), `test_projet_operations` (New project, « Close all tabs? »,
Open project sur un projet modifié, projet courant rouvert après Save), `test_projet_demarrage` (`last_project` absent effacé), `test_projet_macros`
(Close project sur le fichier de macros par défaut), `test_macros_conflit` (Cancel par défaut, fichier par défaut présent).

## 8. Documentation et traductions

`CLAUDE.md` (projets, format, onglets libres et de projet, valeurs d'usage et par défaut, `GtFileStamp`, menus réorganisés), `README.md` (projets), `tests/README.md`
(nouveaux scénarios), revue (annexe 6c ; remarques SP3/SP4 marquées traitées), `po/POTFILES.in`, `po/fr.po`.

## 9. Critères d'acceptation

1. `meson test -C build`, `meson test -C build-asan` et `meson test -C build --suite gui` passent (SKIP 77 de broadwayd tolérés comme en 6b).
2. Un projet de deux onglets enregistré puis rouvert (dans la même instance et au démarrage suivant) restaure onglets, réglages, macros (valeurs par défaut
   comprises), scripts et onglet actif ; le fichier contient les macros et le script, aucun chemin vers un `.ini` ou un `.lua`.
3. Changer un argument dans le panneau n'écrit aucun fichier (vérifié par l'absence de modification du `.ini` et du projet).
4. `.gtktermrc` et les `.ini` écrits par 6c sont identiques à ceux de 6b à contenu égal.
5. `grep -rn 'gtk/gtk.h' src/*.c src/*.h` est vide ; aucune statique ne porte d'état d'onglet.
