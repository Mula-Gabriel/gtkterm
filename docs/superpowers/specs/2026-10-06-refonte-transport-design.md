# Sous-projet 2 — Cœur transport sans GTK, historique brut, envoi et écho

- Date : 2026-10-06
- Branche : `feature/refonte-transport` (fusion dans `master` selon la règle habituelle : écrasement en commits thématiques, fusion, suppression de la branche, publication sur `Maison`)
- Entrées : revue `docs/superpowers/reviews/2026-10-06-revue-code.md` (M1–M6, M28 partiel, M37, point 5 de l'architecture, remarques mineures reportées), sous-projets 0 et 1 fusionnés (socle de tests, harnais de bouchons, `serial_termios.c`, `config_file.c`)

## Contexte : la feuille de route et GTK 4

L'utilisateur passera le logiciel sur les dernières versions de GTK. Le **sous-projet 6 devient la migration vers GTK 4**, au lieu d'un découpage d'`interface.c` en GTK 3. Ce sous-projet prépare cette migration :
- tout ce qu'il crée hors du rendu VTE ne dépend que de GLib (et de GUdev pour la liaison) ;
- l'objet que l'interface observe est un **GObject à propriétés et signaux**, que l'interface GTK 3 utilise aujourd'hui et qu'une interface GTK 4 pourra lier directement (`g_object_bind_property`).

## Objectif

1. Le transport ne connaît plus l'interface : il reçoit ses paramètres et prévient par des fonctions de rappel.
2. Une seule machine d'état gère la connexion du port principal : ouverture, perte de liaison, reconnexion, fermeture (M1–M6).
3. Le tampon de réception garde les octets **bruts**, avec les envois, dans un historique étiqueté. Le CR/LF automatique et l'horodatage deviennent une transformation appliquée à la lecture (point 5 de l'architecture).
4. Tous les envois passent par un seul chemin ; l'écho est conservé et redessiné à sa place (M37).
5. Les nombres magiques de la configuration du port deviennent des enums nommés ; la façade `serial.c` disparaît.

## Hors périmètre

- L'état d'application unique (drapeaux écho, CR/LF, horodatage dupliqués entre `interface.c` et `config`) : sous-projet 6. Ce sous-projet lit ces options depuis `config` au même endroit qu'aujourd'hui.
- La réutilisation du transport par les ports secondaires Lua : sous-projet 5. Le transport est néanmoins conçu par instance pour le permettre.
- Les dialogues de configuration (sous-projet 3) et des macros (sous-projet 4), sauf les lignes qui appelaient l'ancienne interface du transport.
- Le format de `.gtktermrc` ne change pas.

## Décisions validées avec l'utilisateur

1. **Périmètre** : transport sans interface (A), historique brut (B), envoi et écho séparés (C), enums (E). L'état d'application unique (D) attend le sous-projet 6.
2. **Perte de liaison** : aucune boîte de dialogue. La barre d'état et le titre affichent « — perdu », puis « — reconnexion… » si la reconnexion automatique est active : un essai silencieux toutes les 2 s, plus un essai immédiat quand udev signale le retour du périphérique. Les erreurs d'une ouverture **demandée par l'utilisateur** (port occupé, introuvable…) gardent leur boîte de dialogue.
3. **Journal** : le texte reçu tel que le montre la vue ASCII (CR/LF automatique et horodatage selon les options), quelle que soit la vue affichée ; l'écho n'est pas journalisé.
4. **Écho** : conservé dans l'historique, à sa place parmi les données reçues. Il réapparaît au changement de vue. En vue hexadécimale, il a sa propre ligne et ne décale pas l'index des octets reçus. La sauvegarde brute, Lua et le journal ne contiennent que les octets reçus.
5. **Approche** : transport à fonctions de rappel (bas niveau) ; liaison en GObject observé par l'interface.
6. **Affichage de l'écho** (proposé et accepté avec la conception) : la frappe clavier est répétée dans la ligne, en bleu ; un envoi en bloc (macro, hexadécimal, fichier, Lua) est affiché sur sa propre ligne bleue préfixée « > ».

## 1. Architecture

Trois couches :

| Couche | Fichiers | Dépendances | Rôle |
|---|---|---|---|
| **Cœur** (`gtkterm_core`) | `transport.c/.h`, `serial_termios.c/.h`, `history.c/.h`, `rx_transform.c/.h`, `device_match.c/.h` | GLib seule (+ termios, sockets) | E/S du port, historique, transformation, correspondance de périphérique — testables sans fenêtre |
| **Liaison** | `link.c/.h` (`GtkTermLink`) | GLib, GObject, GUdev | Machine d'état de la connexion, moniteur udev, historique du port principal, signaux vers les consommateurs |
| **Interface** (GTK 3) | `interface.c`, `terminal_display.c`, `files.c`, `logging.c`, `term_config.c`, `script_engine.c` (abonnement) | GTK, VTE | S'abonne à la liaison, affiche, envoie par la liaison |

Fichiers supprimés : `serial.c`, `serial.h` (ce qu'il en reste d'utile va dans `transport.h` : table des débits, `set_port_baudrate`), `buffer.c`, `buffer.h`, `device_monitor.c`, `device_monitor.h` (absorbés par `link.c`).

## 2. Transport (cœur)

```c
typedef enum { GT_PARITY_NONE, GT_PARITY_ODD, GT_PARITY_EVEN } GtParity;
typedef enum { GT_FLOW_NONE, GT_FLOW_XONXOFF, GT_FLOW_RTSCTS, GT_FLOW_RS485 } GtFlow;
typedef enum { GT_MODEM_DTR, GT_MODEM_RTS } GtModemLine;

typedef struct {
	TransportType type;               /* TRANSPORT_SERIAL, TRANSPORT_TCP_CLIENT, TRANSPORT_TCP_SERVER */
	gchar *port;                      /* série */
	guint baudrate;
	gint bits;                        /* 5..8 */
	gint stopbits;                    /* 1, 2 */
	GtParity parity;
	GtFlow flow;
	gint rs485_before_ms, rs485_after_ms;
	gboolean lock_port;
	gchar *host;                      /* TCP */
	gchar *tcp_port;
} TransportParams;

typedef enum {
	TRANSPORT_EVENT_CONNECTED,        /* TCP client : connexion établie (asynchrone) */
	TRANSPORT_EVENT_CLIENT_CONNECTED, /* TCP serveur : un client est accepté */
	TRANSPORT_EVENT_CLIENT_GONE,      /* TCP serveur : le client est parti, on écoute à nouveau */
	TRANSPORT_EVENT_LOST              /* série ou TCP client : la liaison est perdue (detail = raison) */
} TransportEvent;

typedef struct {
	void (*on_data)(const guint8 *data, gsize len, gpointer user_data);
	void (*on_event)(TransportEvent ev, const gchar *detail, gpointer user_data);
} TransportCallbacks;

GtTransport *gt_transport_open(const TransportParams *params, const TransportCallbacks *cb, gpointer user_data, GError **error);
void         gt_transport_close(GtTransport *t);           /* selon les paramètres OUVERTS (M1) ; tolère NULL */
gssize       gt_transport_send(GtTransport *t, const guint8 *data, gsize len);
gint         gt_transport_get_modem_lines(GtTransport *t); /* masque TIOCM_*, -1 si sans objet, signale LOST sur EIO */
void         gt_transport_toggle_line(GtTransport *t, GtModemLine line);
void         gt_transport_send_break(GtTransport *t);
guint        gt_transport_get_actual_baudrate(GtTransport *t);
gint         gt_transport_get_fd(GtTransport *t);
dev_t        gt_transport_get_device_number(GtTransport *t); /* st_rdev relevé à l'ouverture (série), 0 sinon */
```

Règles :
- **Aucun appel à l'interface** (`show_message`, `Set_*`, `add_input`, `put_chars` disparaissent de `transport.c`). Les erreurs d'ouverture remontent par `GError` (domaine `GT_TRANSPORT_ERROR`, messages traduits identiques aux actuels).
- **Par instance** : tout l'état statique actuel (`transport_fd`, `listen_fd`, `client_fd`, sources, `termios_save`, débit) passe dans la structure opaque `GtTransport`. Plusieurs instances peuvent coexister (préparation du sous-projet 5).
- La **fermeture** suit les paramètres de l'instance (type ouvert) : restauration termios et déverrouillage seulement pour une instance série, fermeture d'un descripteur une seule fois (fin de la double fermeture de `listen_fd` en mode serveur, M1).
- **Pas de reconnexion dans le transport** : il signale `LOST` et s'arrête ; la politique de reconnexion appartient à la liaison.
- La connexion TCP client reste asynchrone (`CONNECTED` signalé à l'établissement, `LOST` en cas d'échec de connexion ou de fin de flux, M5).
- RS485, `MSG_NOSIGNAL`, `serial_termios_apply` et le caractère d'attente non signé (sous-projet 1) sont conservés ; le caractère d'attente quitte le transport (§5).
- Le transport peut être exécuté sur une boucle GLib quelconque (contexte par défaut) ; il n'ouvre ni fenêtre ni dialogue.

## 3. Liaison (`GtkTermLink`, GObject)

Une instance unique pour le port principal, créée au démarrage.

**Propriétés** (notifiées par `notify::`) :
- `state` (enum `GtLinkState`) : `CLOSED`, `OPEN`, `LISTENING` (serveur TCP sans client), `LOST`, `RECONNECTING` ;
- `status-text` (chaîne) : chemin complet du port et réglage réel (« /dev/ttyUSB0  115200-8-N-1 »), « tcp://hôte:port », « tcp://*:port (serveur, aucun client) », suivi de « — perdu » ou « — reconnexion… » selon l'état ; « Aucun port ouvert » quand fermé. Plus de troncature `%.15s` ;
- `auto-reconnect` (booléen) ;
- `reconnect-interval-ms` (entier, 2000 par défaut ; réglable par les tests).

**Méthodes** :
- `gboolean gt_link_open(GtkTermLink *, const TransportParams *, GError **)` : ouverture demandée par l'utilisateur ; ferme d'abord la connexion courante ;
- `void gt_link_close(GtkTermLink *)` : fermeture demandée ; annule toute reconnexion ;
- `gssize gt_link_send(GtkTermLink *, const guint8 *, gsize, GtSendOrigin origin)` (§6) ;
- accès aux lignes modem, DTR/RTS, break (délégués au transport ouvert) ;
- `GtHistory *gt_link_get_history(GtkTermLink *)`.

**Signaux** :
- `open-failed (const gchar *message)` : émis seulement pour une ouverture demandée par l'utilisateur (jamais pendant une reconnexion) ; l'interface l'affiche dans une boîte de dialogue ;
- `received (const guint8 *data, gsize len)` : octets bruts reçus, après leur ajout à l'historique ;
- `sent (const guint8 *data, gsize len, GtSendOrigin origin)` : octets réellement envoyés, après leur ajout à l'historique ;
- `history-cleared` : l'historique a été vidé (commande « Effacer ») ; les vues se vident.

**Machine d'état** :

| État | Événement | Nouvel état | Effet |
|---|---|---|---|
| tout | `gt_link_open` réussi | `OPEN` (`LISTENING` en serveur) | moniteur udev armé si série |
| tout | `gt_link_open` échoué (y compris l'échec asynchrone d'un client TCP) | `CLOSED` ; puis `RECONNECTING` si `auto-reconnect` et transport série ou client TCP | signal `open-failed` (une boîte), puis minuterie et, en série, moniteur udev armé (l'« add » du chemin configuré déclenche un essai immédiat) ; serveur TCP : reste `CLOSED` |
| `OPEN`/`LISTENING` | `LOST` du transport, `EIO` des lignes modem, udev « remove » du périphérique ouvert | `LOST` | transport fermé ; puis `RECONNECTING` si `auto-reconnect` |
| `RECONNECTING` | minuterie (intervalle) ou udev « add » du périphérique | `OPEN` si l'ouverture réussit, sinon reste `RECONNECTING` | aucune boîte de dialogue |
| `LISTENING` | `CLIENT_CONNECTED` | `OPEN` | — |
| `OPEN` (serveur) | `CLIENT_GONE` | `LISTENING` | — |
| tout | `gt_link_close` | `CLOSED` | minuterie et moniteur arrêtés |
| `LOST` | `auto-reconnect` passé à vrai | `RECONNECTING` | — |
| `RECONNECTING` | `auto-reconnect` passé à faux | `LOST` | minuterie arrêtée |

**Moniteur udev** (dans la liaison) :
- armé à chaque ouverture réussie d'un port série (et à l'échec d'une ouverture demandée quand `auto-reconnect` est actif), désarmé à la fermeture et pour les transports TCP (M2) ;
- correspondance par **numéro de périphérique** (`st_rdev` relevé à l'ouverture), avec en secours le chemin et les liens udev (M6) — fonction pure `device_match()` dans le cœur ;
- pendant `RECONNECTING`, l'événement « add » d'un périphérique correspondant au **chemin configuré** (le numéro peut changer au rebranchement) déclenche un essai immédiat ;
- le moniteur n'ouvre jamais le port de lui-même à l'armement (fin de la double ouverture au démarrage, M4).

**Démarrage** : une seule ouverture (`gt_link_open` depuis `main`), après lecture de la configuration et de la ligne de commande ; en cas d'échec, une seule boîte de dialogue.

## 4. Historique et transformation (cœur)

**`GtHistory`** :
- enregistrements `{ GtRecordKind kind (RX | TX), GtSendOrigin origin (pour TX), gint64 wall_time_us, GBytes ou tampon, gsize len }` ;
- capacité : 128 Ko de données utiles ; à l'ajout, les plus anciens enregistrements sont retirés en entier ; un enregistrement plus grand que la capacité n'en garde que la fin ;
- interface : `gt_history_new(capacity)`, `gt_history_append(h, kind, origin, wall_time_us, data, len)`, `gt_history_foreach(h, func, user_data)` (du plus ancien au plus récent), `gt_history_clear(h)`, `gt_history_copy_rx(h)` (octets reçus seulement, dans l'ordre, pour la sauvegarde brute).

**`GtRxTransform`** (transformation à la lecture) :
- état : CR reçu en attente, début de ligne (horodatage à écrire) ;
- options : `crlf_auto`, `timestamp` ;
- `gt_rx_transform_feed(tr, data, len, wall_time_us, GString *out)` : produit le texte ; mêmes règles CR/LF qu'aujourd'hui (`buffer.c` actuel) ; l'horodatage en début de ligne utilise l'**heure de réception** passée en argument, au format actuel `[j.HHh.MMm.SSs.mmm] ` ;
- `gt_rx_transform_reset(tr)` ;
- pure : aucune horloge interne, aucune E/S.

## 5. Consommateurs

| Consommateur | Source | Contenu |
|---|---|---|
| Vue ASCII (`terminal_display.c`) | `received`, `sent`, rejeu de l'historique | RX transformé par sa propre `GtRxTransform` ; TX en bleu selon §6 ; TX affiché seulement si l'option écho est active |
| Vue hexadécimale | idem | RX brut, index et colonnes comptés sur les seuls octets reçus ; TX sur sa propre ligne bleue, sans compter dans l'index |
| Changement de vue, ou changement des options CR/LF, horodatage, écho, octets par ligne, index | historique | la vue est vidée puis redessinée depuis l'historique (`gt_history_foreach`) |
| Journal (`logging.c`) | `received` | texte transformé par sa propre `GtRxTransform` (options courantes) ; pas d'écho ; `logging_set_suspended` (correctif provisoire de M31) est supprimé |
| Sauvegarde brute (`files.c`) | `gt_history_copy_rx` | octets reçus bruts |
| Sauvegarde ASCII (`files.c`) | historique, RX seulement | texte transformé (comme la vue ASCII), filtré comme aujourd'hui |
| Lua, port 0 (`script_engine.c`) | `received` | octets bruts ; la connexion et la déconnexion du gestionnaire se font sur le thread principal (`run_on_main_sync`), ce qui règle aussi la remarque reportée « set_tap_func appelé depuis le thread Lua » |
| Attente d'un caractère pendant l'envoi de fichier (`files.c`) | `received` | octets bruts ; la comparaison avec `config.car` quitte le transport |

## 6. Envoi et écho

- **Un seul chemin** : `gt_link_send(link, data, len, origin)` avec `GtSendOrigin` ∈ `KEYBOARD`, `MACRO`, `LUA`, `HEX`, `FILE`. Appelants : saisie clavier (`Got_Input`), macros (`macros.c`, `macro_panel.c`), Lua port 0 (`script_engine.c`, sur le thread principal), envoi hexadécimal (`interface.c`), envoi de fichier (`files.c`).
- La liaison écrit par le transport ; seuls les octets **réellement écrits** sont ajoutés à l'historique (TX) puis émis par `sent` ; la valeur de retour est le nombre d'octets écrits (ou négatif).
- `send_serial()` disparaît. La règle « Échap efface l'écran » reste dans la saisie clavier (sous-projet 1, décision 1).
- **Affichage de l'écho** (vue ASCII) : origine `KEYBOARD` répétée dans la ligne, en bleu, sans préfixe ni retour à la ligne ajouté ; autres origines sur une ligne bleue « > … », les CR/LF finaux retirés (comme aujourd'hui).
- **Historique** : le TX est toujours enregistré ; l'option écho décide seulement de l'affichage, et l'activer redessine la vue avec l'envoyé déjà présent.

## 7. Enums et configuration

- `struct configuration_port` : `parite` devient `GtParity`, `flux` devient `GtFlow` (noms de champs conservés pour limiter le diff ; valeurs numériques identiques 0/1/2 et 0/1/2/3, donc `.gtktermrc` et la ligne de commande inchangés).
- `term_config.c` gagne `void config_to_transport_params(const struct configuration_port *, TransportParams *)` ; c'est l'unique point de conversion.
- `configure_echo`, `configure_crlfauto`, `configure_autoreconnect_enable`, `configure_esc_clear_screen` (ex-`serial.c`) vont dans `term_config.c`.
- `Config_port()`, `Close_port()`, `Send_chars()`, `lis_sig()`, `Set_signals()`, `sendbreak()`, `get_port_string()` disparaissent ; leurs appelants passent par la liaison.

## 8. Tests

**Unitaires** (`gtkterm_core`) :
- `test_history` : ajout, ordre, retrait des plus anciens à la capacité, enregistrement plus grand que la capacité, `copy_rx` sans TX, `clear` ;
- `test_rx_transform` : tous les cas CR/LF de l'actuel `put_chars` (CR seul, LF seul, CRLF, CRCR, CR puis caractère), horodatage en début de ligne avec heure injectée, ligne coupée entre deux appels, options désactivées (sortie = entrée) ;
- `test_device_match` : numéro de périphérique égal, chemin égal, lien égal, aucune correspondance.

**Intégration** (harnais de bouchons) :
- `test_transport` porté sur la nouvelle interface : réglages termios sur pty, RS485 RTS (ioctl simulé), envoi TCP vers pair fermé (pas de SIGPIPE), **`LOST` signalé** à la fermeture du maître pty et à la fin de flux TCP client, `CONNECTED` asynchrone, serveur : `CLIENT_CONNECTED` puis `CLIENT_GONE`, deux instances simultanées, fermeture selon le type ouvert ;
- `test_link` : machine d'état complète sur pty et TCP local — ouverture réussie/échouée (`open-failed` seulement à l'ouverture demandée), perte → `RECONNECTING` → `OPEN` quand la pty réapparaît sous le même nom (lien symbolique recréé), fermeture qui annule la reconnexion, `auto-reconnect` basculé en cours, événements udev simulés par une entrée de test `gt_link_handle_device_event(link, action, devnum, path)`, `status-text` de chaque état, `received`/`sent` et contenu de l'historique, aucune boîte de dialogue (bouchon `show_message` jamais appelé pendant une reconnexion).

**Interface** (suite séparée `gui`) :
- le banc d'essai graphique du sous-projet 1 (affichage `broadwayd` privé, bus D-Bus et accessibilité privés, `HOME`/`XDG_*` isolés, ports pty, garde-fou contre les vrais ports et la vraie configuration, faux `git`) entre dans `tests/gui/` ;
- déclaré par `meson test --suite gui` ; le test se déclare sauté (code 77) si `broadwayd`, `dbus-run-session` ou Atspi manquent ;
- scénarios de ce sous-projet : perte de liaison (maître pty fermé) → barre d'état « — perdu » puis « — reconnexion… » sans boîte de dialogue, puis retour à l'état ouvert ; écho conservé au changement de vue ASCII → hexa → ASCII ; index hexa non décalé par l'écho ; journal identique à la vue ASCII sans écho ; horodatage activé après coup affiché sur les données déjà reçues ; sauvegarde brute sans écho ni horodatage ; frappe clavier répétée dans la ligne ;
- les scénarios EM du sous-projet 1 encore pertinents sont conservés comme tests de non-régression.

## 9. Défauts et remarques traités

- M1 (fermeture selon la configuration nouvelle, double fermeture), M2 (udev indépendant du transport, démarré une fois), M3 (reconnexion : boîte toutes les 2 s, minuterie réarmée après fermeture), M4 (double ouverture au démarrage), M5 (TCP client : fin de flux sans mise à jour, état affiché trop tôt), M6 (lien hors udev), M37 (écho mal placé, perdu au changement de vue), point 5 de l'architecture (tampon transformé), troncature `%.15s`.
- Remarques reportées : `set_tap_func` depuis le thread Lua (sous-projet 1, tâche 8) ; journal dupliqué traité structurellement (suppression de `logging_set_suspended`) ; écho journalisé via `put_hexadecimal` (tâches 14/15).

## 10. Critères d'acceptation

1. `meson test -C build` (suites `unit` et `lint`) : aucun échec ; `meson test -C build-asan` : aucun échec.
2. `meson test -C build --suite gui` : aucun échec sur le poste de développement (broadwayd et Atspi présents).
3. `transport.c`, `history.c`, `rx_transform.c`, `device_match.c` sont dans `gtkterm_core` et n'incluent aucun en-tête GTK, VTE ou d'interface (vérifié par `grep`) ; `link.c` n'inclut ni GTK ni VTE.
4. `serial.c`, `buffer.c`, `device_monitor.c` n'existent plus ; plus aucun appel à `show_message`/`Set_status_message`/`Set_window_title` depuis le transport ou la liaison.
5. Aucun changement de format de `.gtktermrc` ni d'option de ligne de commande.
