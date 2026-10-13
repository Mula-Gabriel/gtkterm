# Mise à jour par versions taguées — conception

Date : 2026-10-12

## Objectif

Le gestionnaire de mise à jour suit aujourd'hui la tête de `master` (`git ls-remote <url> HEAD`, puis `reset --hard origin/master` dans le script). Chaque poussée sur `master` est donc
proposée à tous les postes. Avant de basculer tout le monde sur la refonte GTK 4, on passe à des **versions publiées** :

1. une version est un tag git `vX.Y.Z` ; la mise à jour installe une version, jamais la tête d'une branche ;
2. la fenêtre de mise à jour liste les versions publiées et permet d'installer **n'importe laquelle, plus ancienne comprise**, pour revenir en arrière si une version casse quelque chose ;
3. elle affiche les **nouveautés** et les **avertissements** (ce qui casse) des versions concernées, lus dans un fichier `NEWS` en français ;
4. la première version publiée est `v1.3.2` : la ligne GTK 3 actuellement sur GitHub (`0a013c2`) **plus ce gestionnaire** ; la refonte GTK 4 sera `v2.0.0`.

Hors périmètre : traduction des notes, signature des tags, API « Releases » d'un hébergeur, migration inverse automatique de la configuration, branche `Ubuntu24LTS`.

## Deux lignes, une fusion

- **Ligne GTK 3** (clone dédié `~/git/gtkterm-gtk3`, branche `feature/maj-par-tags` partie de `origin/master` = `0a013c2`) : tout le gestionnaire est écrit ici d'abord, interface GTK 3
  dans `src/update.c`. Fusionnée dans le `master` de cette ligne, taguée `v1.3.2`.
- **Ligne refonte** (`~/git/gtkterm`, branche `feature/maj-par-tags` partie de `master`) : son premier commit est la **fusion** du `master` GTK 3 (vraie fusion, jamais écrasée : c'est
  elle qui rend le `master` GTK 3 ancêtre du nouveau `master`, donc la poussée sur GitHub possible sans forcer). Résolution : tout ce que la refonte a réécrit garde sa version
  (`src/update.c` reste supprimé) ; les fichiers communs (`update_versions.c/.h`, `data/gtkterm-update.sh`, `NEWS`, `tools/publier-version.sh`, cette spec) sont repris, le script avec
  les dépendances GTK 4. Par-dessus, l'implémentation GTK 4 (`update_check.c`, `src/ui/update_window.c`, tests), écrasée en commits thématiques, puis fusion dans `master`, tag `v2.0.0`.

Le module de logique (`update_versions.c`) est **identique** sur les deux lignes : seule l'interface est écrite deux fois.

## Versions et publication

- **Version publiée** : tag dont le nom correspond exactement à `^v[0-9]+\.[0-9]+\.[0-9]+$` (chaque nombre sans zéro de tête superflu, ≤ 9 chiffres). Les autres tags sont ignorés. Ordre :
  numérique, composante par composante (`v1.10.0` > `v1.9.3`), jamais la date.
- **Version installée** : à la construction, `git describe --tags --match 'v[0-9]*' --abbrev=7` est inscrit dans le binaire (`GIT_DESCRIBE`), à côté de `GIT_REVISION`.
  - `v2.0.0` : version publiée exacte ;
  - `v2.0.0-3-gabc1234` : construction de développement, **plus récente** que `v2.0.0` et **plus ancienne** que toute version publiée supérieure ;
  - sans git ou sans tag joignable : la version de `meson.build` (`meson.project_version()`), traitée comme une version exacte.
- **`NEWS`** à la racine, en français, texte brut :
  ```
  # Nouveautés de GTKTerm

  ## v2.0.0 — 2026-10-20
  - Onglets : un port par onglet, sessions indépendantes
  - Projets : un fichier .gtkterm regroupe tous les onglets
  ! La configuration ~/.config/.gtktermrc est convertie au nouveau format ; v1.3.2 ne la lit plus.
    Pour revenir à v1.3.2 avec vos réglages, remettez ~/.config/.gtktermrc.avant-migration à sa place.
  ```
  Une section commence par une ligne `## vX.Y.Z` (la suite de la ligne, date ou titre, est libre et affichée). Dans une section : `- ` nouveauté, `! ` avertissement, une ligne qui
  commence par deux espaces prolonge l'élément précédent, une ligne vide est ignorée, toute autre ligne est gardée comme texte simple. Ce qui précède la première section est ignoré. Un titre de niveau 1 (`# …`) clôt la section en cours : ce qui suit, jusqu'à la prochaine `## vX.Y.Z`, n'est pas affiché (l'historique ajouté sous `# Historique` reste ainsi hors des notes). Une
  section d'une version qui n'a pas (encore) de tag n'est jamais affichée.
- **`tools/publier-version.sh vX.Y.Z`** : refuse si l'arbre de travail n'est pas propre, si le nom n'est pas une version valide, si le tag existe déjà, si `meson.build` ne porte pas
  `X.Y.Z`, ou si `NEWS` n'a pas de section `## vX.Y.Z` ; sinon crée le tag **annoté** (message : « GTKTerm vX.Y.Z ») sur `HEAD`. Il ne pousse rien : la poussée (GitHub et Maison) reste
  manuelle.

## Module `update_versions.c` (GLib seulement)

Aucune dépendance GTK, aucune globale, aucun sous-processus : il reçoit des chaînes et rend des structures. Fonctions (noms indicatifs) :

- `gt_version_parse(str, &ver)` : `vX.Y.Z` → `GtVersion { major, minor, patch, distance }` (`distance` = nombre de commits après le tag pour une sortie de `git describe`, 0 sinon) ;
  accepte aussi `X.Y.Z` (version de `meson.build`). FALSE si invalide.
- `gt_version_compare(a, b)` : ordre numérique, puis `distance`.
- `gt_version_list_from_refs(text)` : sortie de `git for-each-ref --format='%(refname:strip=2)' refs/tags` → liste des versions publiées, triée de la plus récente à la plus ancienne,
  sans doublon.
- `gt_news_parse(text)` → liste de sections `{ version, titre, éléments[] }`, chaque élément `{ genre (nouveauté, avertissement, texte), texte }`.
- `gt_news_select(sections, installed, target)` : si `target` > `installed`, les sections des versions `v` avec `installed < v ≤ target` ; si `target` < `installed`, celles avec
  `target < v ≤ installed` (« ce que vous quittez ») ; même version : aucune. Résultat trié de la plus récente à la plus ancienne ; le sens (montée, retour) est rendu avec.
- `gt_update_verdict(installed, newest, skipped)` (démarrage) : `AVAILABLE` si `newest` > `installed` et `newest` ≠ `skipped` ; `SKIPPED` si égale à `skipped` ; sinon `UP_TO_DATE`.

Ligne refonte : le module rejoint `gtkterm_core`, testé par `tests/test_update_versions.c` (analyse des versions et de `describe`, ordre, refs, `NEWS` : éléments, prolongements,
section sans tag, sélection dans les deux sens, verdict). Ligne GTK 3 : pas de suite de tests ; le même fichier y est vérifié par la procédure de bout en bout ci-dessous.

## Récupération des versions et des notes

- **Cache de l'application** : `$XDG_CACHE_HOME/gtkterm/versions.git`, dépôt nu distinct de la copie de travail du script (`…/gtkterm/src`), qu'il ne touche jamais.
  - absent : `git clone --bare --filter=tree:0 -- <url> versions.git` (historique et tags, sans arbres ni fichiers) ;
  - présent : `remote set-url origin -- <url>`, puis `git fetch --prune --prune-tags --force origin '+refs/tags/*:refs/tags/*'` ;
  - un serveur qui ignore le filtre envoie tout : ça fonctionne quand même, en plus lourd ;
  - mêmes protections qu'aujourd'hui : `GIT_TERMINAL_PROMPT=0`, `LC_ALL=C`, `-c protocol.ext.allow=never`, sous-processus `GSubprocess` annulable (tué à l'annulation).
  - annulation : le git en cours reçoit `SIGTERM` (pas `SIGKILL`, qui l'empêcherait de nettoyer ses verrous et son clone inachevé) ;
  - cache abîmé : rien n'est supprimé, le dossier est renommé à côté en `versions.git.abandonne-<horodatage en µs>` puis l'opération repart **une seule fois** d'un clone, seulement pour un échec
    LOCAL : `remote set-url` ou `for-each-ref` en échec, `fetch` dont l'erreur désigne le dépôt local (`Unable to create '….lock': File exists`, `cannot lock ref`, `not a git repository`, `bad config`, `is corrupt` sur un chemin du cache ; l'URL est retirée du message avant examen), ou clone refusé
    parce que le dossier cible est non vide. Un échec réseau (DNS, connexion, authentification, source introuvable) rend l'erreur et garde le cache : hors ligne, rien n'est écarté. Un
    second échec rend l'erreur de la seconde tentative, un renommage impossible l'erreur d'origine, une erreur de communication avec git ne déplace rien ;
  - **verrou** : tout accès au cache passe par un `flock` sur `versions.git.lock` (fichier voisin), tenu pendant toute l'opération, reclone compris ; occupé, on réessaie toutes les 200 ms sans bloquer la
    boucle (annulable), 10 s au plus, puis « Another update check is in progress. » (`G_IO_ERROR_BUSY`). Un `.lock` trouvé dans le cache est donc périmé par construction ;
- **Versions** : `git for-each-ref` sur ce cache, puis `gt_version_list_from_refs()`.
- **Notes** : `git show <version la plus récente>:NEWS` (git va chercher l'arbre et le fichier à la demande). La version la plus récente porte l'historique complet, corrections de notes
  anciennes comprises. Une version la plus récente sans `NEWS` (impossible avec le script de publication) donne une zone de notes vide, sans erreur.
- **Enchaînement** : une seule opération asynchrone « actualiser » (clone ou fetch, for-each-ref, show), annulable, qui rend versions et sections ou une erreur. L'interface ne bloque
  jamais pendant ce temps.
- **Démarrage** (si « Vérifier au démarrage ») : la même opération, en différé comme aujourd'hui ; silencieuse sauf si `gt_update_verdict()` rend `AVAILABLE`. Un échec (réseau, ou autre instance qui actualise en même temps
  et dont le verrou reste tenu plus de 10 s : « Another update check is in progress. ») est silencieux au démarrage, affiché dans la fenêtre sinon.
- **« Ignorer cette version »** enregistre le **nom de tag** dans `update_skipped_version` (`[general]` de `.gtktermrc` pour la refonte, clé parsecfg du même nom pour la ligne GTK 3).
  L'ancienne clé `update_skipped_revision` (une révision) n'est plus lue ni écrite.

## Fenêtre de mise à jour

Une seule fenêtre, « Updates » (Help › Check for updates…), qui remplace aussi la boîte « Update now / Later / Skip this version » du démarrage.

```
Installed version:  v2.0.0 (abc1234)
Update source:      [https://github.com/Mula-Gabriel/gtkterm.git          ]
[x] Check for updates at startup                              [Check now]
Version:            [v2.1.0 — latest  ▾]
┌─────────────────────────────────────────────────────────────────────┐
│ ⚠ Warnings                                                          │
│   v2.1.0 : …                                                        │
│ v2.1.0 — 2026-11-02                                                 │
│   • …                                                               │
└─────────────────────────────────────────────────────────────────────┘
<état : Checking… / erreur / GTKTerm is up to date.>
[Skip this version]                 [Update to v2.1.0]          [Close]
```

- **Version installée** : `GIT_DESCRIBE` et les 7 premiers caractères de `GIT_REVISION`.
- **Liste des versions** : de la plus récente à la plus ancienne ; libellés « vX.Y.Z », « vX.Y.Z — latest », « vX.Y.Z — installed » (les deux si c'est le cas). Insensible tant que
  l'actualisation n'a rien rendu. Sélection initiale : la plus récente.
- **Notes** (lecture seule, défilante) : les avertissements des sections retenues **d'abord**, regroupés sous « Warnings » et mis en évidence (gras, couleur d'avertissement), chacun
  préfixé de sa version ; puis chaque section (titre « vX.Y.Z — <suite de la ligne> », éléments en puces, texte simple tel quel), sans ses avertissements : chacun n'est montré qu'une fois, en tête
  (la fenêtre de v1.3.2 les répète encore dans leur section ; d'où des avertissements d'une ligne, la marche à suivre allant dans le texte simple). Retour en arrière : un en-tête « You are leaving these
  versions: » précède les sections. Même version : « This version is installed. ». Le texte des notes n'est jamais interprété comme du balisage Pango.
- **Bouton principal** selon la sélection : « Update to vX » (plus récente), « Roll back to vX » (plus ancienne), « Reinstall vX » (installée, pour réparer). Si les sections retenues
  contiennent au moins un avertissement, une confirmation (« Install vX? » + rappel « See the warnings above. ») est demandée avant de lancer le script ; sinon lancement direct.
- **« Skip this version »** : visible seulement quand la version sélectionnée est plus récente que l'installée ; enregistre ce tag et ferme la fenêtre.
- **Démarrage** : verdict `AVAILABLE` → la fenêtre s'ouvre, version la plus récente sélectionnée, notes affichées. Une seule fenêtre à la fois (instance unique, comme aujourd'hui).
- **Lancement** : comme aujourd'hui (terminal détecté, liste d'arguments, jamais une chaîne de shell), avec le tag en 4ᵉ argument ; le tag est revalidé (`gt_version_parse`) juste avant.
- Libellés : msgids anglais par `_()`, traduction dans `po/fr.po` ; les notes (`NEWS`) ne sont pas traduites. Ligne refonte : widgets nommés pour les tests graphiques
  (`update-version`, `update-notes`, `update-install`, `update-skip`, `update-check`, `update-result`), pas de `GtkDialog`, rappels non bloquants, règles GTK 4.14 du projet.

## Script d'installation `data/gtkterm-update.sh`

Appel : `gtkterm-update.sh <url> <prefix> <pid> <tag>` (le tag est obligatoire ; sans lui, le script s'arrête avec un message). Le même script est identique sur les deux lignes, sauf
les listes de dépendances (GTK 3 / GTK 4).

1. **Contrôles** : URL refusée si elle ressemble à une option ou à un transport `ext::` (inchangé) ; tag refusé s'il ne correspond pas à `^v[0-9]+\.[0-9]+\.[0-9]+$`.
2. **Source** : copie de travail `$XDG_CACHE_HOME/gtkterm/src`. Absente : `git clone --no-checkout -- <url>`. Présente : `remote set-url`, puis
   `git fetch --force origin '+refs/tags/*:refs/tags/*'`. Puis `git checkout --force --detach refs/tags/<tag>` et `git clean -ffdx -e build` (aucun fichier d'une autre version ne
   reste). Tag absent après le fetch : arrêt avec un message.
3. **Relance** : si le script de la version obtenue diffère de celui qui tourne et que `GTKTERM_UPDATE_REEXEC` est vide, `exec` du script obtenu avec les mêmes quatre arguments. Toutes
   les versions taguées comprennent ce contrat : c'est la version **visée** qui choisit ses dépendances et sa construction (GTK 3 pour `v1.3.2`, GTK 4 pour `v2.x`).
4. **Registre** (utilisateur) : si `build/meson-logs/install-log.txt` existe et que le registre `$XDG_CACHE_HOME/gtkterm/installed-files.txt` n'existe pas, le journal est copié
   dans ce registre (amorçage, une seule fois, avant que `build/` ne soit supprimé).
5. **Une seule autorisation pour tout le travail privilégié** : un unique `pkexec` (script fixe, valeurs passées en arguments, jamais interprétées) qui enchaîne
   - l'installation des dépendances de construction (commande fixée par le script selon la distribution ; sautée par `GTKTERM_UPDATE_DEPS=0`) ;
   - la construction **en tant que l'utilisateur** (`runuser -u <utilisateur> -- env HOME=<home> …` : `build/` et tout ce qu'il contient lui appartiennent) : `rm -rf build` (par root,
     ce qui règle aussi les fichiers qu'un `meson install` précédent y a écrits), `meson setup --prefix=<prefix> -Dtests=false build <source>` (jamais `--reconfigure` : passer de
     GTK 4 à GTK 3 dans le même dossier n'est pas fiable), `ninja -C build` ; sans root (tests), la construction se fait directement ;
   - `meson install -C build --no-rebuild` ;
   - le ménage : lignes du registre absentes du nouveau `build/meson-logs/install-log.txt`, retirées seulement si le chemin commence par `<prefix>/`, ne contient pas `..`, désigne un
     fichier ordinaire ou un lien (jamais un dossier) et n'appartient à aucun paquet (`dpkg -S` / `rpm -qf` / `pacman -Qo`) ; sans registre, règle de secours : retrait de
     `<prefix>/share/applications/gtkterm.desktop` s'il n'appartient à aucun paquet et que la version installée ne le fournit pas.

   **Une mise à jour = une seule demande de mot de passe.** Si l'utilisateur refuse (« Annuler ») ou si une étape échoue, le script s'arrête aussitôt avec un message (« Authorization
   refused or update failed: GTKTerm was not updated. ») et ne redemande rien.
6. **Registre mis à jour** (utilisateur) : en cas de succès, le nouveau journal remplace le registre.
7. **Redémarrage** : inchangé (fin du PID donné, relance de `<prefix>/bin/gtkterm` sous `nohup`).

Effet : les lanceurs (`gtkterm.desktop` de la ligne GTK 3, `eu.mulagabriel.gtkterm.desktop` de la refonte) et tout autre fichier propre à une version ne restent pas après un passage à
une autre version, dans les deux sens.

## Bascule et publication

1. **`v1.3.2`** (sur feu vert) : poussée du `master` GTK 3 et du tag `v1.3.2` vers GitHub et Maison, **le tag avec ou avant `master`**. Les postes installés (`0a013c2`, ancien
   gestionnaire) voient une nouvelle tête de `master`, s'y mettent à jour une dernière fois par l'ancien mécanisme, et récupèrent le tag avec l'historique (`GIT_DESCRIBE` = `v1.3.2`). À
   partir de là, ils ne suivent plus `master`.
2. **`v2.0.0`** (plus tard, sur feu vert) : poussée du tag `v2.0.0` et de la refonte sur une branche dédiée (`refonte`), **pas sur `master`** : un poste encore sur
   `0a013c2` suit la tête de `master` avec l'ancien script et resterait bloqué sur une construction GTK 4 impossible avec les dépendances GTK 3. Chaque poste en `v1.3.2` ou plus se voit
   proposer `v2.0.0` au démarrage, avec ses avertissements ; rien n'est forcé, et `Roll back to v1.3.2` reste possible.
3. Quand plus aucun poste n'est sur `0a013c2`, `master` peut avancer (avance rapide grâce à la fusion) : seuls les tags atteignent les utilisateurs.

## Vérification

- **Ligne refonte** : `meson test -C build` (dont `test_update_versions`, `test_update_check` adapté) et la suite `gui` (`test_update_window.py` adapté : faux `git` qui sert des tags
  et un `NEWS`, liste des versions, libellés du bouton principal, avertissements, confirmation, « Skip this version », ouverture au démarrage).
- **Script** : `sh -n`, et passage du script sur un dépôt local de test (URL `file://`, tags factices) avec un préfixe sous le dossier temporaire, `pkexec` remplacé par une variable
  d'environnement réservée aux tests (`GTKTERM_UPDATE_SUDO`, vide par défaut = `pkexec`) : passage sur un tag, relance sur le script obtenu, registre et ménage des fichiers retirés,
  **exactement une autorisation par mise à jour**, arrêt propre sur refus. Aucun test ni agent n'exécute jamais un script sans cette échappatoire (l'ancien script appelle `pkexec`).
- **Bout en bout (fait par l'utilisateur, installe réellement dans `/usr/local`)** : depuis `v1.3.2` installée, monter en `v2.0.0` (avertissements affichés, confirmation, un seul
  lanceur ensuite), revenir à `v1.3.2` (« You are leaving these versions », un seul lanceur, configuration à restaurer selon l'avertissement), remonter. Une marche à suivre est
  fournie avec le dépôt de test local comme source.

## Risques

- **`runuser` absent** (util-linux : présent sur Ubuntu, Debian, Fedora, Arch) : le script s'arrête avec un message avant toute construction en root.
- **Environnement minimal de `pkexec`** : les chemins de `meson` et `ninja` sont résolus côté utilisateur et passés en arguments, `HOME` est redonné à la construction.
- **Serveur sans filtre partiel** : clone complet du cache, plus lent la première fois seulement.
- **Configuration après retour à `v1.3.2`** : non convertie automatiquement ; couvert par l'avertissement de `NEWS` (restaurer `.gtktermrc.avant-migration`).
- **Deux instances qui actualisent le cache en même temps** : la seconde attend le verrou du cache (10 s au plus, puis « Another update check is in progress. ») ; silencieux au démarrage, message dans la fenêtre, « Check now » réessaie.
