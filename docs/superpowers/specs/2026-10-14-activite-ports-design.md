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
| `connecting` | liaison `CONNECTING` (client TCP) | spinner, arc couleur d'accent |
| `receiving` | `OPEN`, dernier octet reçu il y a moins de 300 ms | pastille verte pleine |
| `idle` | `OPEN`, au moins un octet reçu depuis l'entrée en `OPEN`, plus rien depuis ≥ *délai de repos* | Zzz en chenillard |
| `waiting` | `OPEN` sans octet reçu (avant le délai sans données), ou `LISTENING` (serveur TCP sans client) | pastille vide (contour) |
| `active` | `OPEN`, dernier octet reçu il y a moins que le délai de repos | pastille vide (contour) |

Règles :

- Un port qui a reçu au moins un octet depuis son entrée en `OPEN` ne passe **jamais** en `dead` tant qu'il reste `OPEN` : il va en `idle`.
- « Depuis l'entrée en `OPEN` » : chaque (re)connexion repart de zéro (un appareil revenu après une perte et resté muet passe en `dead`).
- `LISTENING` ne passe jamais en `dead` (on attend un client, pas des données).
- `LOST` sans reconnexion automatique reste `lost` jusqu'au délai sans données, puis `dead`.
- Un délai à 0 désactive l'état correspondant (`idle` ou `dead`).

**Non lu** (indépendant de l'état) : un onglet non affiché qui reçoit au moins un octet passe en « non lu » (nom en gras) ; l'afficher l'efface.

## 2. Réglages

Deux champs du profil (`GtProfile`), en secondes, 0 à 3600 (0 = désactivé), corrigés par `gt_profile_validate()` hors plage :

- `idle_timeout_s`, défaut 10 — clé `.gtktermrc` `activity_idle_timeout` ;
- `no_data_timeout_s`, défaut 60 — clé `activity_no_data_timeout`.

Lus et écrits avec les autres clés du profil ; un profil sans ces clés prend les défauts. Ils s'éditent dans la fenêtre Port, expander « Advanced
Configuration Options », nouveau cadre « Port activity » : « Idle after (seconds): », « No data after (seconds): » (`GtkSpinButton`, 0–3600, noms
stables `port-idle-timeout`, `port-no-data-timeout`). OK applique les délais à la session comme le reste du profil (`profile_applied`).

## 3. Architecture

### 3.1 Cœur : `GtPortActivity` (`src/port_activity.c`, `gtkterm_core`, GLib seulement)

GObject sans dépendance à la liaison (le cœur n'inclut pas `link.h`) : il reçoit des événements datés et calcule l'état.

```c
GtPortActivity *gt_port_activity_new(void);
void  gt_port_activity_set_link_state(GtPortActivity *a, GtPortActivityLink st, gint64 now_us); /* CLOSED, CONNECTING, OPEN, LISTENING, LOST, RECONNECTING */
void  gt_port_activity_received(GtPortActivity *a, gint64 now_us);
void  gt_port_activity_set_timeouts(GtPortActivity *a, guint idle_s, guint no_data_s);
void  gt_port_activity_set_viewed(GtPortActivity *a, gboolean viewed);   /* vu : efface « non lu » ; non vu : une réception le pose */
void  gt_port_activity_tick(GtPortActivity *a, gint64 now_us);           /* réévalue les échéances */
GtPortActivityState gt_port_activity_get_state(GtPortActivity *a);
gboolean gt_port_activity_get_unread(GtPortActivity *a);
```

- Propriétés `state` (enum enregistrée `GtPortActivityState`, nicks du tableau) et `unread` (booléen) ; notification **seulement sur
  changement** : un flux continu reste `receiving` sans aucune notification par paquet.
- Échéances : une seule source GLib (`g_timeout_add`) armée sur la prochaine échéance (fin des 300 ms, délai de repos, délai sans données), recalculée
  après chaque événement ; elle appelle `tick(g_get_monotonic_time())`. `gt_port_activity_received` ne réarme la source que si l'échéance la plus proche
  change (pas de source créée par paquet). Une propriété `use-timer` (vrai par défaut, construction seulement) permet aux tests de piloter le temps à
  la main par `tick`.
- Le type du lien est un enum propre au cœur (`GtPortActivityLink`), converti depuis `GtLinkState` par la session.

### 3.2 Application : `GtSession`

- Chaque session crée son `GtPortActivity`, exposé par `gt_session_get_activity()` ; elle relaie `received` (heure monotone) et `notify::state` de sa
  liaison (sans rien ouvrir ni fermer : réentrance, `link.h`), et applique les délais de son profil à la création et à chaque `profile_applied`.
- `gt_session_shutdown()` arrête la source de l'activité (état `closed`).
- `app.c`, sur `notify::active` du gestionnaire : `set_viewed(TRUE)` pour la session active, `FALSE` pour les autres.

### 3.3 Interface (`src/ui/`)

- **`activity_icon.c`** (nouveau) : widget d'emplacement 18 × 16 px, `gt_activity_icon_new(GtPortActivity *)`, lié à `state` par
  `g_signal_connect_object`. Enfants dans une `GtkStack` sans transition : pastille (`GtkBox` CSS `activity-dot`, classes `receiving`/`lost`),
  spinner (`GtkDrawingArea` : cercle gris + arc), tête de mort (`GtkImage` d'un SVG symbolique de la gresource, `skull-symbolic.svg`), Zzz
  (trois `GtkLabel` « Z », « z », « z », tailles décroissantes, en escalier).
  - **Animations par le code**, pas en CSS : GTK coupe les animations CSS quand le bureau réduit les animations (`gtk-enable-animations`), ce qui
    est le cas chez l'utilisateur. Spinner : rotation par `gtk_widget_add_tick_callback` ; Zzz : chenillard par minuteur de 800 ms (Z seul → Z z →
    Z z z → Z seul…). Minuteur et rappel n'existent que tant que l'état correspondant est affiché **et** le widget est cartographié (`map`/`unmap`).
  - La classe CSS de l'état (`activity-<nick>`) est posée sur l'emplacement ; nom stable fourni par l'appelant ; nom accessible traduit selon
    l'état (« Receiving data », « Idle », « No data received », « Connection lost », « Reconnecting », « Connecting », « Waiting for data »).
  - CSS (un fournisseur pour l'écran, installé une fois dans `app.c`, avec le CSS existant) : pastille 8 px ronde, contour `alpha(currentColor,0.4)`,
    verte `#2ec27e`, rouge `#e01b24` ; Zzz en `@accent_color`.
- **`tab_label.c`** : l'emplacement (nom `tab-activity`) à gauche du nom ; classe `dead` sur la boîte (nom grisé, `dim-label`), classe `unread` (nom
  en gras) liée à la propriété `unread`. La classe `lost` est gardée sur la boîte (tests) mais l'italique disparaît.
- **`status_bar.c`** : un emplacement (nom `status-activity`) avant le texte d'état, visible seulement quand le classeur n'a qu'une page :
  `gt_status_bar_set_activity_visible(bar, gboolean)`, appelée par `app.c` là où `gt_tab_label_set_closable()` l'est déjà.
- **`test_hooks.c`** : `test-sessions` gagne `activity` (nick) et `unread` (booléen).

## 4. Erreurs et cas limites

- Profil avec délais hors plage ou non entiers : valeur par défaut et message de validation, comme les autres champs.
- Changement des délais pendant qu'un port est ouvert : état réévalué immédiatement (`tick`).
- Session fermée : la source de l'activité est retirée avant la fin de la session ; l'icône, détruite avec la page, se déconnecte d'elle-même.
- Horloge : uniquement `g_get_monotonic_time()` (insensible aux changements d'heure).

## 5. Tests

- **Unitaire** `test_port_activity` (horloge manuelle, `use-timer` faux) : chaque transition du tableau ; 300 ms de `receiving` ; flux continu sans
  notification répétée ; « déjà reçu → jamais `dead` » ; nouvelle connexion → compteur remis à zéro ; `LISTENING` jamais `dead` ; `LOST` sans
  reconnexion → `dead` au délai ; délais à 0 ; changement de délais en cours ; `unread` posé seulement hors vue, effacé par `set_viewed(TRUE)`.
- **Profil** : aller-retour des deux clés, absence → défauts, hors plage → corrigé (tests existants du magasin et de `profile`).
- **GUI** (`tests/gui/`, pty) : `test_activite_ports` — profil avec délais de 1 s et 2 s ; ouverture → `waiting` ; écriture sur le pty →
  `receiving` puis `active` puis `idle` ; second onglet muet → `dead` ; réception sur l'onglet non affiché → `unread`, effacé en l'affichant ;
  un seul onglet → `status-activity` visible, deux onglets → caché ; les deux champs de la fenêtre Port enregistrés dans le profil.
- `meson test -C build`, `meson test -C build --suite gui`, build ASan.

## 6. Documentation et traductions

`README.md` (section interface), `CLAUDE.md` (module `port_activity.c`, `activity_icon.c`, champs du profil), `po/POTFILES.in` et `po/fr.po` pour les
nouvelles chaînes.

## 7. Critères d'acceptation

1. Chaque état du tableau s'affiche dans l'onglet comme sur la maquette, un seul indicateur à la fois, à la place de la pastille.
2. Avec un seul onglet, le même indicateur est dans la barre d'état ; avec deux ou plus, seulement dans les onglets.
3. Le Zzz et le spinner bougent même quand le bureau réduit les animations.
4. Les deux délais se règlent dans la fenêtre Port, sont enregistrés avec le profil et valent 10 s et 60 s par défaut.
5. Aucun minuteur ne tourne pour un port fermé ou une icône non affichée ; un flux continu ne provoque aucune notification par paquet.
6. Tous les tests passent ; les couches sont respectées (`port_activity.c` sans GTK ni `link.h`).
