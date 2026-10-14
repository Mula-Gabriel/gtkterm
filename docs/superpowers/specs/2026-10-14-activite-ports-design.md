# Indicateurs d'activité des ports (onglets et barre d'état)

Date : 2026-10-14 · Branche : `feature/activite-ports` (depuis `master`) · Maquette validée : https://claude.ai/artifact/E3h48dYSVs7zxEUFUiog93

## Contexte

Aujourd'hui, rien ne dit si un port est vivant : l'onglet passe seulement en italique (classe `lost`) quand la liaison est perdue ou en
reconnexion, et la barre d'onglets est cachée avec un seul onglet. On ne voit ni la réception de données, ni un port ouvert qui ne dit rien.

## Objectif

Un indicateur discret, à gauche du nom de chaque onglet (et dans la barre d'état quand il n'y a qu'un onglet), qui montre d'un coup d'œil l'état
d'activité du port : réception, repos, perte, reconnexion, port muet.

## Hors périmètre

- Lignes modem (chute de CD) : seule la perte de liaison (`GT_LINK_LOST`) compte (choix de l'utilisateur).
- Activité en émission, débit, compteurs d'octets.
- Les ports Lua 1–15 (seul le port de l'onglet, sa liaison).

## 1. États

Un seul état à la fois, du plus prioritaire au moins prioritaire :

| État (`GtPortActivityState`, nick) | Condition | Rendu dans l'emplacement |
|---|---|---|
| `closed` | liaison `CLOSED` | emplacement vide |
| `dead` | liaison `OPEN` depuis ≥ *délai sans données* sans un octet reçu depuis l'entrée en `OPEN` ; ou liaison `LOST`/`RECONNECTING` depuis ≥ *délai sans données* (compté depuis la perte) | tête de mort, nom grisé |
| `reconnecting` | liaison `RECONNECTING` | spinner, arc rouge |
| `lost` | liaison `LOST` | pastille rouge pleine |
| `connecting` | liaison `CONNECTING` (client TCP) ou `LISTENING` (serveur TCP sans client) | spinner, arc couleur d'accent |
| `receiving` | `OPEN`, dernier octet reçu il y a moins de 300 ms | pastille verte pleine |
| `idle` | `OPEN`, au moins un octet reçu depuis l'entrée en `OPEN`, plus rien depuis ≥ *délai de repos* | Zzz en chenillard |
| `waiting` | `OPEN` sans octet reçu (avant le délai sans données) | pastille vide (contour) |
| `active` | `OPEN`, dernier octet reçu il y a moins que le délai de repos | pastille vide (contour) |

Règles :

- Un port qui a reçu au moins un octet depuis son entrée en `OPEN` ne passe **jamais** en `dead` tant qu'il reste `OPEN` : il va en `idle`.
- « Depuis l'entrée en `OPEN` » : chaque (re)connexion repart de zéro (un appareil revenu après une perte et resté muet passe en `dead`). Une
  réouverture d'un port déjà `OPEN` (OK de la fenêtre Port, profil chargé, SIGUSR1 : `OPEN` → `OPEN` sans notification de la liaison) repart aussi
  de zéro : `gt_session_open_port()` appelle `gt_port_activity_reset()` juste avant `gt_link_open()`.
- `CONNECTING` et `LISTENING` ne passent jamais en `dead` (on attend une connexion, pas des données) ; un serveur dont le client part revient
  en `LISTENING`, donc au spinner.
- **Délai de perte** : compté depuis l'entrée dans {`LOST`, `RECONNECTING`} à partir d'un autre état, **pas** remis à zéro par les passages de
  l'un à l'autre. Cas réels couverts : perte = `LOST` puis `RECONNECTING` dans la foulée (deux notifications) ; échec d'ouverture avec
  reconnexion automatique = `CLOSED` → `RECONNECTING` sans `LOST` ; auto-reconnect désactivé pendant `RECONNECTING` → `LOST`, réactivé →
  `RECONNECTING`. `LOST` sans reconnexion automatique reste `lost` jusqu'au délai sans données, puis `dead`.
- Une réception hors `OPEN` est ignorée (ni état ni « non lu »).
- Un délai à 0 désactive l'état correspondant (`idle` ou `dead`).

**Non lu** (indépendant de l'état) : un onglet non affiché qui reçoit au moins un octet passe en « non lu » (nom en gras) ; l'afficher l'efface.

## 2. Réglages

Deux champs `gint` du profil (`GtProfile`), en secondes, 0 à 3600 (0 = désactivé), lus par `read_int()` (pas `read_uint()`, qui ignore 0) et
bornés par `gt_profile_validate()` hors plage (message de validation) ; une valeur non entière est ignorée sans message, comme les autres
entiers du profil :

- `idle_timeout_s`, défaut 10 — clé `.gtktermrc` `activity_idle_timeout` ;
- `no_data_timeout_s`, défaut 60 — clé `activity_no_data_timeout`.

Lus et écrits avec les autres clés du profil ; un profil sans ces clés prend les défauts. Ils s'éditent dans la fenêtre Port, expander « Advanced
Configuration Options », nouveau cadre « Port activity » : « Idle after (seconds): », « No data after (seconds): » (`GtkSpinButton`, 0–3600, noms
stables `port-idle-timeout`, `port-no-data-timeout`), portés par `GtPortForm` (`port_form.c` : `from_profile`, `apply`) comme les autres
champs du dialogue, sinon OK les écraserait. OK met à jour le profil de la session (pas `.gtktermrc` : il faut « Save configuration », comme
pour tout le profil).

## 3. Architecture

### 3.1 Cœur : `GtPortActivity` (`src/port_activity.c`, `gtkterm_core`, GLib seulement)

GObject sans dépendance à la liaison (le cœur n'inclut pas `link.h`) : il reçoit des événements datés et calcule l'état.

```c
GtPortActivity *gt_port_activity_new(void);
void  gt_port_activity_set_link_state(GtPortActivity *a, GtPortActivityLink st, gint64 now_us); /* CLOSED, CONNECTING, OPEN, LISTENING, LOST, RECONNECTING */
void  gt_port_activity_received(GtPortActivity *a, gint64 now_us);
void  gt_port_activity_set_timeouts(GtPortActivity *a, guint idle_s, guint no_data_s);
void  gt_port_activity_set_viewed(GtPortActivity *a, gboolean viewed);   /* vu : efface « non lu » ; non vu : une réception le pose. Défaut : vu */
void  gt_port_activity_reset(GtPortActivity *a, gint64 now_us);          /* réouverture : « déjà reçu » oublié, compteur sans données repart */
void  gt_port_activity_tick(GtPortActivity *a, gint64 now_us);           /* réévalue les échéances */
GtPortActivityState gt_port_activity_get_state(GtPortActivity *a);
gboolean gt_port_activity_get_unread(GtPortActivity *a);
```

- Propriétés `state` (enum enregistrée `GtPortActivityState`, nicks du tableau) et `unread` (booléen) ; notification **seulement sur
  changement** : un flux continu reste `receiving` sans aucune notification par paquet.
- Échéances : une seule source GLib (`g_timeout_add`) armée sur la prochaine échéance (fin des 300 ms, délai de repos, délai sans données). Un
  événement ne réarme la source que si la nouvelle échéance est **plus proche** que celle déjà armée : une réception repousse les échéances, donc ne
  crée aucune source (critère 5) ; à l'expiration, `tick` réévalue et réarme sur l'échéance suivante si elle n'est pas atteinte. Délai arrondi au
  supérieur (µs → ms, au moins 1 ms) : jamais de boucle de réveils à 0 ms. Une propriété `use-timer` (vrai par défaut, construction seulement) permet aux tests de piloter le temps à
  la main par `tick`.
- Le type du lien est un enum propre au cœur (`GtPortActivityLink`), converti depuis `GtLinkState` par la session. Les deux enums sont
  enregistrés par `g_enum_register_static` (comme `link.c`). Fichiers : `port_activity.c` dans `core_sources` (`src/meson.build`),
  `test_port_activity` dans `tests/meson.build`.

### 3.2 Application : `GtSession`

- Chaque session crée son `GtPortActivity`, exposé par `gt_session_get_activity()` ; elle relaie `received` (heure monotone) et `notify::state` de sa
  liaison (sans rien ouvrir ni fermer : réentrance, `link.h`), et applique les délais de son profil dans `gt_session_apply_profile()` **et** dans
  `gt_session_open_port()` (OK de la fenêtre Port passe par ce seul chemin), où elle appelle aussi `gt_port_activity_reset()` avant `gt_link_open()`.
- `gt_session_shutdown()` arrête la source de l'activité (état `closed`).
- `app.c`, sur `notify::active` du gestionnaire : `set_viewed(TRUE)` pour la session active, `FALSE` pour les autres. « Vu » vaut vrai par défaut :
  la première session, active avant le branchement du gestionnaire (`install_relays`), n'a besoin de rien ; une session ajoutée en arrière-plan
  reçoit `FALSE` au changement d'onglet actif suivant.

### 3.3 Interface (`src/ui/`)

- **`activity_icon.c`** (nouveau) : widget d'emplacement 18 × 16 px, `gt_activity_icon_new(GtPortActivity *)`, lié à `state` par
  `g_signal_connect_object`. Enfants dans une `GtkStack` sans transition : pastille (`GtkBox` CSS `activity-dot`, classes `receiving`/`lost`),
  spinner (`GtkDrawingArea` : cercle + arc, couleurs lues par `gtk_widget_get_color()` sur la zone, dont la classe CSS `connecting` ou
  `reconnecting` fixe la couleur : cairo ne voit pas le CSS), tête de mort (`GtkImage` par `gtk_image_new_from_icon_name("skull-symbolic")`,
  SVG ajouté à `gtkterm.gresource.xml` et dossier rendu visible par `gtk_icon_theme_add_resource_path()`, pour la recoloration symbolique), Zzz
  (trois `GtkLabel` « Z », « z », « z », tailles décroissantes, en escalier).
  - **Animations par le code**, pas en CSS : GTK coupe les animations CSS quand le bureau réduit les animations (`gtk-enable-animations`), ce qui
    est le cas chez l'utilisateur. Spinner : rotation par `gtk_widget_add_tick_callback`, redessin limité à environ 15 images par seconde (un
    serveur en `LISTENING` affiché tourne indéfiniment) ; Zzz : chenillard par minuteur de 800 ms (Z seul → Z z →
    Z z z → Z seul…). Minuteur et rappel n'existent que tant que l'état correspondant est affiché **et** le widget est cartographié (`map`/`unmap`).
  - La classe CSS de l'état (`activity-<nick>`) est posée sur l'emplacement ; nom stable fourni par l'appelant ; nom accessible traduit selon
    l'état (« Receiving data », « Idle », « No data received », « Connection lost », « Reconnecting », « Connecting », « Waiting for data »).
  - CSS (un fournisseur pour l'écran, installé une fois dans `app.c`, avec le CSS existant) : pastille 8 px ronde, contour `alpha(currentColor,0.4)`,
    verte `#2ec27e`, rouge `#e01b24` ; Zzz et arc `connecting` en bleu fixe `#3584e4` (pas `@accent_color`, que certains thèmes ne définissent
    pas). La règle italique `.lost label` de `app.c` est supprimée.
- **`tab_label.c`** : l'emplacement (nom `tab-activity`) à gauche du nom ; classe `dead` sur la boîte (nom grisé, `dim-label`), classe `unread` (nom
  en gras) liée à la propriété `unread`. La classe `lost` est gardée sur la boîte (tests) mais l'italique disparaît.
- **`status_bar.c`** : `gt_status_bar_new(link, activity)` (signature changée) ; un emplacement (nom `status-activity`) avant le texte d'état,
  visible seulement quand le classeur n'a qu'une page : `gt_status_bar_set_activity_visible(bar, gboolean)`, appelée par `update_tab_state()` de
  `app.c` (là où `gt_tab_label_set_closable()` l'est déjà), la barre de chaque page trouvée par `gt_session_view_get_slot(view, "status-bar")`.
- **`test_hooks.c`** : `test-sessions` gagne `activity` (nick) et `unread` (booléen).

## 4. Erreurs et cas limites

- Profil avec délais hors plage ou non entiers : valeur par défaut et message de validation, comme les autres champs.
- Changement des délais pendant qu'un port est ouvert : état réévalué immédiatement (`tick`).
- Session fermée : la source de l'activité est retirée avant la fin de la session ; l'icône, détruite avec la page, se déconnecte d'elle-même.
- Horloge : uniquement `g_get_monotonic_time()` (insensible aux changements d'heure).

## 5. Tests

- **Unitaire** `test_port_activity` (horloge manuelle, `use-timer` faux) : chaque transition du tableau ; 300 ms de `receiving` ; flux continu sans
  notification répétée ; les trois chemins de perte du §1 (délai non remis à zéro) ; `reset` sur un port `OPEN` ; réception hors `OPEN` ignorée ;
  plus un cas court avec `use-timer` vrai (délais de 1 s, `g_main_context_iteration` bornée) qui vérifie l'armement réel de la source ; « déjà reçu → jamais `dead` » ; nouvelle connexion → compteur remis à zéro ; `LISTENING` → `connecting`, jamais `dead` ; `LOST` sans
  reconnexion → `dead` au délai ; délais à 0 ; changement de délais en cours ; `unread` posé seulement hors vue, effacé par `set_viewed(TRUE)`.
- **Profil** : aller-retour des deux clés, absence → défauts, hors plage → corrigé (tests existants du magasin et de `profile`).
- **GUI** (`tests/gui/`, pty) : `test_activite_ports` — profil avec délais de 2 s et 5 s ; chaque état lu en boucle avec échéance (jamais de
  pause fixe) : ouverture → `waiting` ; écriture **continue** sur le pty → `receiving` (l'état de 300 ms n'est pas cherché après une écriture
  isolée) ; arrêt → `active` puis `idle` ; second onglet muet → `dead` ; réception sur l'onglet non affiché → `unread`, effacé en l'affichant ;
  un seul onglet → `status-activity` visible, deux onglets → caché ; les deux champs de la fenêtre Port : OK, « Save configuration », puis
  relecture du `.gtktermrc` privé.
- `meson test -C build`, `meson test -C build --suite gui`, build ASan.

## 6. Documentation et traductions

`README.md` (section interface), `CLAUDE.md` (module `port_activity.c`, `activity_icon.c`, champs du profil), `po/POTFILES.in` et `po/fr.po` pour les
nouvelles chaînes.

## 7. Critères d'acceptation

1. Chaque état du tableau s'affiche dans l'onglet comme sur la maquette, un seul indicateur à la fois, à la place de la pastille.
2. Avec un seul onglet, le même indicateur est dans la barre d'état ; avec deux ou plus, seulement dans les onglets.
3. Le Zzz et le spinner bougent même quand le bureau réduit les animations.
4. Les deux délais se règlent dans la fenêtre Port, s'appliquent à l'onglet par OK, sont enregistrés par « Save configuration » et valent 10 s et
   60 s par défaut ; 0 est relu comme 0.
5. Aucun minuteur ne tourne pour un port fermé ou une icône non affichée ; un flux continu ne provoque aucune notification par paquet.
6. Tous les tests passent ; les couches sont respectées (`port_activity.c` sans GTK ni `link.h`).
