# Sous-projet 5 — Moteur Lua : ports sur le transport par instance, arrêt fiable, envois complets

Date : 2026-10-07. Branche : `feature/refonte-lua`. Suite des sous-projets 2 (transport sans GTK, `GtkTermLink`), 3 (configuration)
et 4 (macros). Décisions prises en autonomie (consigne de l'utilisateur : « travaille en autonomie, décide tout toi-même »).

## 1. Objectif

Le moteur Lua (`script_engine.c`) duplique encore l'ouverture série (termios, 9 débits avec repli silencieux sur 9600) et TCP
(IPv4 seulement, `connect` bloquant) que le sous-projet 2 a rassemblée dans `transport.c`, partage l'état de ses ports sans
verrou avec les rappels de la boucle principale et ne sait pas arrêter un script qui calcule. Ce sous-projet :

1. fait passer les ports Lua 1 à 15 par `GtTransport` (une instance par port, sur le thread principal), comme le port principal ;
2. range la réception de chaque port dans une file d'octets bornée et sûre entre threads (`GtRxQueue`, cœur sans GTK) ;
3. arrête réellement un script sur « Stop » (crochet Lua), neutralise `os.exit`, redirige `print` vers la console ;
4. rend `send`/`send_hex` complets ou en échec explicite (écritures partielles et `EAGAIN` repris, délai, raison de l'échec) ;
5. arrête proprement le script à la fermeture de GTKTerm (ports Lua refermés, termios restaurés) ;
6. ne fige plus l'interface quand un script journalise en boucle serrée.

Défauts de la revue (`docs/superpowers/reviews/2026-10-06-revue-code.md`) traités : M25, M28, point d'architecture 6 (« termios/TCP
du moteur Lua ≈ transport.c », état des ports partagé sans verrou), duplication « termios/TCP du moteur Lua », code mort `L_global`
et `chan_err`, et les remarques reportées : courses `lua_port_close` contre `port_read_cb`/`tcp_accept_cb`, `g_source_remove` sur une
source déjà retirée, absence de garde contre `run_on_main_sync` appelé du thread principal, fermeture de l'application pendant un
script (thread abandonné), journal Lua à `G_PRIORITY_DEFAULT` qui peut figer le rafraîchissement. M26 (`wait_for` binaire), M27
(ordre de `send_macro`), M29 (exemples, `SKILL.md`) et M30 (thread joint) sont déjà corrigés (sous-projets 1 et 4) : leurs tests
restent et passent.

Hors périmètre (reporté, noté dans l'annexe de la revue) : la barre d'état « Macro … sent! » à chaque `send_macro` Lua (affichage
des macros, sous-projet 6) ; les fuites sur erreur mémoire Lua (`longjmp` à travers des allocations GLib) ; l'interruption d'un
appel bloquant de la bibliothèque standard (`io.read`, `os.execute`) — documentée ; les onglets et projets (Lua visera alors
l'onglet par son port).

## 2. Architecture

```
thread Lua                                   thread principal (boucle GLib par défaut)
──────────                                   ─────────────────────────────────────────
gtkterm.open/close/send ──run_on_main_sync──► GtScriptPort (script_port.c, cœur)
                                                 └─ GtTransport (transport.c) ──on_data──► GtRxQueue
gtkterm.read/wait_for/clear ────────────────────────────────────────────── (verrou) ──────┘
gtkterm.send(0)/send_macro/get_* ──run_on_main_sync──► gt_link_send / macros_send / instantanés
port 0 : signal « received » de la liaison (connecté sur le thread principal) ──► GtRxQueue du port 0
```

Règle inchangée et désormais sans exception : **le thread Lua ne touche ni GTK, ni un transport, ni la liaison** ; il n'accède
qu'aux files (`GtRxQueue`, verrouillées) et à l'état atomique des ports, et fait exécuter tout le reste sur le thread principal par
`run_on_main_sync`. La table des ports (`GtScriptPort *ports[16]`) n'est lue et écrite que par le thread Lua ; les rappels du
transport ne reçoivent que leur `GtScriptPort`.

### 2.1 `rx_queue.c` / `rx_queue.h` (cœur, nouveau)

File d'octets reçus, bornée, sûre entre threads (un `GMutex` et un `GCond`).

```c
typedef struct _GtRxQueue GtRxQueue;

typedef enum
{
	GT_RX_WAIT_FOUND,	/* motif trouvé : *out = données jusqu'au motif inclus, retirées de la file */
	GT_RX_WAIT_TIMEOUT,	/* échéance atteinte sans motif */
	GT_RX_WAIT_CLOSED,	/* file fermée (port perdu ou fermé) et motif absent des données restantes */
	GT_RX_WAIT_CANCELLED	/* *cancel est devenu non nul pendant l'attente */
} GtRxWaitResult;

GtRxQueue      *gt_rx_queue_new          (gsize max_bytes);
void            gt_rx_queue_free         (GtRxQueue *q);
/* Ajoute ; au-delà de max_bytes, les octets les plus anciens sont jetés. Renvoie le nombre d'octets jetés par cet appel. */
gsize           gt_rx_queue_push         (GtRxQueue *q, const guint8 *data, gsize len);
GBytes         *gt_rx_queue_take_all     (GtRxQueue *q);
void            gt_rx_queue_clear        (GtRxQueue *q);
gsize           gt_rx_queue_length       (GtRxQueue *q);
/* Octets jetés (débordement) depuis l'appel précédent, remis à zéro. */
gsize           gt_rx_queue_take_dropped (GtRxQueue *q);
/* Fermée : les attentes en cours et futures se terminent par CLOSED si le motif est absent ; push est sans effet. */
void            gt_rx_queue_set_closed   (GtRxQueue *q, gboolean closed);
gboolean        gt_rx_queue_is_closed    (GtRxQueue *q);
/* Vide, rouvre, remet le compteur de pertes à zéro. */
void            gt_rx_queue_reset        (GtRxQueue *q);
/* deadline_us : temps monotone (g_get_monotonic_time) ; cancel : lu atomiquement au moins toutes les 20 ms, peut être NULL. */
GtRxWaitResult  gt_rx_queue_wait_for     (GtRxQueue *q, const guint8 *pattern, gsize pattern_len, gint64 deadline_us,
                                          const gint *cancel, GBytes **out);
```

Ordre des issues de `wait_for` à chaque réveil : motif trouvé → `FOUND` (même file fermée ou annulation demandée : les données
déjà là sont rendues) ; sinon annulation → `CANCELLED` ; sinon fermée → `CLOSED` ; sinon échéance passée → `TIMEOUT` ; sinon
attente sur le `GCond` jusqu'à la plus proche de l'échéance et de +20 ms. Motif vide : `FOUND` immédiat avec des données vides.
Recherche binaire (`memmem`, longueurs explicites). `push` réveille les attentes (`g_cond_broadcast`), `set_closed` aussi.
Taille des ports Lua et du port 0 : **1 Mio** (`GT_SCRIPT_RX_MAX`, était 256 Kio avec perte silencieuse du bloc entier).

### 2.2 `script_port.c` / `script_port.h` (cœur, nouveau)

Un port ouvert par un script : un `GtTransport` et une `GtRxQueue`, plus un état lisible de n'importe quel thread.

```c
typedef enum
{
	GT_SCRIPT_PORT_CONNECTING,	/* client TCP : connexion en cours */
	GT_SCRIPT_PORT_OPEN,		/* série ouvert, client TCP connecté */
	GT_SCRIPT_PORT_LISTENING,	/* serveur TCP sans client */
	GT_SCRIPT_PORT_CONNECTED,	/* serveur TCP avec un client */
	GT_SCRIPT_PORT_LOST		/* série ou client TCP : liaison perdue ou connexion refusée (pas de reconnexion) */
} GtScriptPortState;

typedef struct _GtScriptPort GtScriptPort;

/* Thread principal. Série : verrou exclusif (lock_port forcé à TRUE), refus si un autre programme ou le port principal le tient. */
GtScriptPort     *gt_script_port_open        (const TransportParams *params, GError **error);
/* Thread principal : ferme le transport (termios restaurés), ferme la file, libère. */
void              gt_script_port_close       (GtScriptPort *port);
/* Thread principal : une tentative d'écriture. > 0 octets écrits ; 0 rien d'écrit (connexion en cours, serveur sans client) ;
 * -1 avec *err = errno (EAGAIN : réessayer ; EIO : perdu ; autre : échec). */
gssize            gt_script_port_try_send    (GtScriptPort *port, const guint8 *data, gsize len, gint *err);
/* N'importe quel thread. */
GtScriptPortState gt_script_port_get_state   (GtScriptPort *port);
GtRxQueue        *gt_script_port_get_queue   (GtScriptPort *port);
/* N'importe quel thread : raison de la perte (copie, NULL si non perdu). */
gchar            *gt_script_port_dup_reason  (GtScriptPort *port);
/* Thread principal, jamais bloquant (amendé, correctifs 1) : octets en attente d'émission (série : TIOCOUTQ ; 0 sinon) et débit
 * obtenu (série, 0 sinon), pour la vidange bornée avant fermeture. */
guint             gt_script_port_get_output_pending (GtScriptPort *port);
guint             gt_script_port_get_baudrate (GtScriptPort *port);

/* Aides de l'API Lua, sans Lua. */
gboolean          gt_script_parity_from_name (const gchar *name, GtParity *out);	/* none, odd, even */
gboolean          gt_script_flow_from_name   (const gchar *name, GtFlow *out);		/* none, xon, rts */
/* Paires de chiffres hexadécimaux (casse indifférente), blancs (espace, tabulation, CR, LF) permis entre les octets seulement.
 * Erreur (G_CONVERT_ERROR_ILLEGAL_SEQUENCE : le cœur reste sans GIO) : caractère invalide (avec sa position, 0-based) ou nombre impair de chiffres. */
GBytes           *gt_script_hex_decode       (const gchar *text, GError **error);
```

Événements du transport : `CONNECTED` → `OPEN` ; `CLIENT_CONNECTED` → `CONNECTED` ; `CLIENT_GONE` → `LISTENING` (la file reste
ouverte : un client suivant peut venir) ; `LOST` → `LOST`, raison retenue, file fermée (`gt_rx_queue_set_closed`). La réception
(`on_data`) pousse dans la file. État stocké en `gint` atomique.

Transport (amendé, correctifs 1) : un client TCP dont la connexion asynchrone échoue essaie l'adresse résolue suivante avant de
signaler `LOST` (profite aussi au port principal) ; `gt_transport_get_output_pending` (série, `TIOCOUTQ`) ; les réglages série
d'origine sont relevés avant le changement de débit, qui est donc restauré à la fermeture.

### 2.3 `script_engine.c` (refondu)

- **Ports** : `GtScriptPort *ports[16]` (indice 0 inutilisé, le port 0 est la liaison) ; `open`/`close`/tentatives d'envoi par
  `run_on_main_sync`. Port 0 : `GtRxQueue` du moteur, alimentée par le signal `received` de la liaison (connecté et déconnecté sur
  le thread principal, comme aujourd'hui) ; état lu par `gt_link_get_state` sur le thread principal.
- **Fin de script** : tous les ports Lua refermés sur le thread principal, file du port 0 vidée, puis `done`.
- **Vidange série avant fermeture** (amendé, correctifs 1) : `close(h)` et la fermeture de fin de script attendent, sur le thread
  Lua, que la sortie série en attente soit émise (relevé `TIOCOUTQ` par `run_on_main_sync` toutes les 5 ms, pause interruptible),
  au plus `octets × 10 / débit + 500 ms`, plafonné à 2 s par port ; le transport garde son `tcflush(TCOFLUSH)` (jamais de `tcdrain`
  bloquant sur le thread principal). Amendé (correctifs 2) : une seule échéance de 2 s pour toute la vidange, marge de FIFO comprise
  (pause finale de `64 octets × 10 / débit + 10 ms`, que le tampon du pilote ait été vide ou non au départ, car `TIOCOUTQ` ne compte
  pas la FIFO de l'UART ni les tampons d'un adaptateur USB), raccourcie pour ne jamais dépasser l'échéance. Un adaptateur USB à gros
  tampon interne peut retenir plus que ce que le pilote compte : la fin d'un gros envoi peut alors partir au débit restauré (ou être
  coupée) ; la durée de fin d'un script atteint donc 2 s par port série encombré. Aucune vidange si l'arrêt est demandé (« Stop », `os.exit`, fermeture de GTKTerm), sur un port
  perdu ni en TCP.
- **Finaliseurs** (amendé, correctifs 1) : après le nettoyage de fin de script, l'arrêt est posé avant `lua_close` (raison de fin
  déjà décidée) : un `__gc` ne peut plus ouvrir de port ni attendre (toute fonction enveloppée lève l'arrêt). Lua coupe les crochets
  pendant un finaliseur : une boucle Lua pure dans un `__gc` n'est pas interruptible.
- **Locale du thread Lua** (amendé, correctifs 2) : seul `LC_NUMERIC` passe en « C » pour ce thread (`newlocale(LC_NUMERIC_MASK, …)`
  sur une copie de la locale globale) : les nombres gardent leur point, mais `LC_MESSAGES` reste celui du processus, donc les messages
  journalisés par le thread Lua (« Lua error », « Script exited with status », débordement) sont traduits.
- **Résolution de nom** (amendé, correctifs 1 et 2) : le client TCP résout le nom (`getaddrinfo`) sur le thread Lua, jamais sur le
  thread principal ; la résolution n'est pas interruptible (l'arrêt est vu à son retour). Un délai nul ou négatif rend `"timeout"`
  avant toute résolution. Chaque adresse est ouverte par son écriture numérique, l'une après l'autre ; la raison d'échec est celle du
  dernier essai, précédée du nom donné par le script quand il diffère de l'adresse (« Cannot connect to localhost (127.0.0.1:4000): … »).
- **Arrêt** (`script_engine_stop`) : `stop_requested` (atomique) mis à 1. Un crochet Lua (`lua_sethook`, `LUA_MASKCOUNT`, toutes
  les 1000 instructions) lève alors une erreur dont l'objet est une valeur légère propre au moteur, après s'être réarmé à chaque
  instruction (amendé au plan) ; un `pcall` du script peut l'attraper une fois, l'instruction suivante hors du `pcall` la relève, donc
  toute boucle finit par en sortir. La raison de fin se décide par les drapeaux (sortie, fin sans erreur, arrêt), pas par l'objet d'erreur. Les fonctions
  bloquantes du moteur (`sleep`, `wait_for`, attente de connexion, reprises d'envoi) vérifient l'arrêt au moins toutes les 20 ms
  et lèvent la même erreur. Les fonctions bloquantes de la bibliothèque standard (`os.execute`, `io.read`, `io.lines` et son
  itérateur, `io.popen`, `io.open`, `io.write`, méthodes `read`, `lines`, `write` des fichiers) sont enveloppées comme l'API :
  l'arrêt est vu à leur entrée (amendé, correctifs 1 : sans `LUA_MASKCALL`), l'appel en cours n'est pas interrompu. L'enveloppe (`std_guard`)
  appelle la fonction d'origine en mode protégé et rend à ses erreurs textuelles la position de l'appelant Lua et le nom de la fonction
  (« script:3: bad argument #1 to 'open' »), que l'appel depuis une fonction C leur ferait perdre (correctifs 2). L'attente de `sleep` passe par un `GCond` réveillé par `stop`.
- **`os.exit([code])`** : termine **le script**, jamais GTKTerm. Sans argument, `true` ou `0` : fin normale ; `false` ou un autre
  entier : fin en erreur, la console affiche « Script exited with status N » (N = 1 pour `false`). Mise en œuvre : drapeau de
  sortie + même mécanisme que l'arrêt.
- **`print(...)`** : écrit dans la console (valeurs passées par `tostring`, séparées par une tabulation, fin de ligne).
- **Exécution protégée** : création de l'API et exécution du script dans un `lua_pcall` d'une fonction C ; plus aucune erreur
  n'est levée hors d'un `pcall`. `lua_atpanic` est tout de même installé : il journalise le message sur la sortie d'erreur.
- **Fin et raison** : `ScriptDoneFunc` reçoit `GtScriptEnd` : `GT_SCRIPT_END_FINISHED`, `GT_SCRIPT_END_ERROR` (erreur Lua ou
  `os.exit` non nul ; le message est journalisé avant), `GT_SCRIPT_END_STOPPED` (« Stop » ou fermeture de GTKTerm).
- **Journal** : les textes du thread Lua (et les avertissements du moteur) s'accumulent dans un tampon protégé par un verrou ; une
  seule source à la fois, une minuterie de 20 ms à `G_PRIORITY_DEFAULT_IDLE` (après le rafraîchissement GTK), vide le tampon d'un coup
  vers la fonction de journal (amendé au plan : une source d'inactivité pure se déclencherait à chaque ligne). Un octet nul est écrit
  `\0` et le lot est rendu UTF-8 valide (`g_utf8_make_valid`). Plafond 1 Mio en attente : au-delà, le texte est jeté et une ligne « [N bytes of log output dropped] »
  le signale au vidage suivant. `done` vide d'abord le tampon.
- **`run_on_main_sync`** : appelé depuis le thread principal (noté par `script_engine_setup`), exécute la fonction directement (pas
  d'interblocage) ; sinon source d'inactivité à `G_PRIORITY_DEFAULT` sur le contexte par défaut, inscrite dans une file de demandes en
  attente, et attente de son exécution.
- **`script_engine_shutdown()`** (nouveau, thread principal, appelé par `main` après `gtk_main()`) : sans script, ne fait rien.
  Sinon retire les fonctions de journal et de fin (les widgets sont détruits), demande l'arrêt. Amendé (correctifs 2) : le contexte par
  défaut ne tourne plus (ses sources toucheraient une interface détruite) ; le thread principal exécute lui-même les demandes
  `run_on_main_sync` en attente (fermeture des ports, fin de l'écoute du port 0), en retirant leurs sources, jusqu'à la fin du thread
  Lua ou 3 s. Thread terminé : joint (sa source de fin retirée), ports fermés. Toujours bloqué au bout de 3 s (appel bloquant de la bibliothèque standard) : avertissement `g_warning`, on quitte sans
  le joindre.

### 2.4 Interface (`script_panel.c`, `gtkterm.c`)

- Fin de script : « [Script finished] », « [Script stopped] » ou « [Script finished with an error] » selon `GtScriptEnd`.
- Console bornée à **10 000 lignes** : au-delà, les plus anciennes sont retirées (une boucle de journal ne fait plus grossir le
  `GtkTextBuffer` sans fin).
- Ordre réel de fermeture (amendé, correctifs 2) : à la destruction de la fenêtre (`on_main_window_destroy`, `interface.c`), la liaison est
  débranchée de l'interface, les voyants et le polling arrêtés (`macro_panel_shutdown`, `macros_shutdown`), puis `script_engine_stop()` ;
  après `gtk_main()`, `gtkterm.c` répète le débranchement et l'arrêt des macros, puis appelle `script_engine_shutdown()`, et enfin ferme
  la liaison. `script_engine_shutdown()` passe donc après l'arrêt des macros et avant `gt_link_close`.

## 3. API Lua (compatible : les scripts existants gardent leur sens)

Conventions : un argument du mauvais type lève une erreur Lua (`luaL_check*`), comme aujourd'hui ; un échec d'opération renvoie
`false` (ou `nil`) suivi d'une chaîne de raison. Les raisons renvoyées aux scripts restent en anglais et stables (les scripts
peuvent les comparer) : `"invalid handle"`, `"already open"`, `"not open"`, `"not connected"`, `"link lost"`, `"timeout"`,
`"closed"`, ou le message de l'erreur du transport sans fin de ligne.

| Appel | Retour | Changement |
|-------|--------|------------|
| `open(h, "serial", dev [, baud=9600, bits=8, stop=1, parity=0, flow=0])` | `true` \| `false, err` | Tout débit accepté par le transport (débits non standard compris) ; débit impossible → erreur au lieu de 9600. `bits` 5..8, `stop` 1..2, `parity` 0/1/2 ou `"none"`/`"odd"`/`"even"`, `flow` 0/1/2 ou `"none"`/`"xon"`/`"rts"`. Port verrouillé. Hors domaine : `"invalid bits"`, `"invalid stop"`, `"invalid parity"`, `"invalid flow"`, `"invalid baud"`. |
| `open(h, "tcp_client", host, port [, timeout_ms=5000])` | `true` \| `false, err` | IPv4 et IPv6 ; attend la connexion au plus `timeout_ms` (`false, "timeout"`, port refermé). Amendé (correctifs 1) : le nom est résolu sur le thread Lua (jamais sur le thread principal ; la résolution elle-même n'est pas interruptible), puis les adresses sont essayées l'une après l'autre (refus → suivante : `localhost` vers un serveur IPv4 seul), le délai bornant l'ensemble ; raison d'échec : celle du dernier essai, précédée du nom donné quand il diffère de l'adresse (correctifs 2) ; délai nul ou négatif : `false, "timeout"` sans résolution. |
| `open(h, "tcp_server", port [, host])` | `true` \| `false, err` | Adresse d'écoute facultative ; port 0 = port libre choisi par le système ; un client suivant remplace le précédent. |
| `close(h)` | – | Sans effet sur un port fermé ou `h` hors 1..15. Série : attend d'abord l'émission de la sortie en attente (2 s au plus, voir §2.3). |
| `status(h)` | chaîne | **Nouveau** : `"open"`, `"connecting"`, `"listening"`, `"connected"`, `"lost"`, `"closed"`. `h = 0` : état de la liaison (`OPEN` → `"open"`, `CONNECTING` → `"connecting"`, `LISTENING` → `"listening"`, `LOST`/`RECONNECTING` → `"lost"`, `CLOSED` → `"closed"`). Hors 0..15 : `"closed"`. |
| `send(h, data [, timeout_ms=5000])` | `true` \| `false, err` | Tout est écrit ou échec : écritures partielles et `EAGAIN` repris sur le thread Lua (pause de 2 ms entre essais, le thread principal n'est jamais bloqué) jusqu'au délai (`"timeout"`) ; rien d'écrivable (connexion en cours, serveur sans client, liaison fermée) → `"not connected"` immédiatement ; perdu → `"link lost"` ; port Lua non ouvert → `"not open"` ; `h` hors 0..15 → `"invalid handle"`. |
| `send_hex(h, hex [, timeout_ms=5000])` | `true` \| `false, err` | Texte invalide → `false, "invalid hex: …"` sans rien envoyer ; puis comme `send`. |
| `wait_for(h, pattern [, timeout_ms=5000])` | données \| `nil, "timeout"` \| `nil, "closed"` | Port perdu/fermé et motif absent → `nil, "closed"` tout de suite au lieu d'attendre le délai. `h` hors 0..15 → `nil, "invalid handle"`. |
| `read(h)`, `clear(h)` | chaîne, – | Inchangés ; sur un port perdu, `read` rend encore ce qui a été reçu. |
| `sleep(ms)` | – | Interrompu immédiatement par « Stop ». |
| `log(v)`, `print(...)` | – | `print` écrit dans la console. |
| `os.exit([code])` | – | Termine le script, pas GTKTerm. |
| `send_macro`, `get_macros`, `get_lists`, `time`, `save_csv` | | Inchangés, sauf (correctifs 2) : l'indice de `send_macro` est un entier (`1.5` lève une erreur), hors 0..2147483647 il rend `false, "macro not found"` au lieu d'être tronqué ; `save_csv` convertit toutes ses valeurs avant d'ouvrir le fichier. |

Débordement d'une file (1 Mio) : les plus anciens octets sont jetés ; au premier débordement d'un port pendant un script, la console
affiche « Port N: receive buffer full, oldest data dropped » (une fois par port et par exécution).

## 4. Erreurs et cas limites

- `open` sur `h` = 0 : `false, "invalid handle"` (le port 0 est la liaison, déjà ouverte).
- Ouverture série d'un périphérique tenu par le port principal : refus du verrou du transport, message du transport rendu.
- Arrêt demandé pendant `open` d'un client TCP ou pendant une reprise d'envoi : erreur d'arrêt levée, le port est refermé au
  nettoyage de fin de script.
- Arrêt pendant `send_macro`/`get_*` : l'appel synchrone en cours se termine (le thread principal l'exécute), l'arrêt prend effet
  à l'instruction Lua suivante.
- Script bloqué dans `io.read`/`os.execute` : « Stop » ne peut pas interrompre l'appel en cours (documenté), il est vu au retour,
  à l'entrée de l'appel suivant ; à la fermeture de GTKTerm, abandon
  au bout de 3 s.
- Erreurs Lua : message journalisé « Lua error: … » (traduit), fin `GT_SCRIPT_END_ERROR`.
- Exécutions successives : `stop_requested`, drapeau de sortie et pertes signalées remis à zéro au lancement.

## 5. Tests

Tous automatiques, sans écran réel ni vrai port série (paires pty, boucle locale TCP), dans les suites existantes.

- `tests/test_rx_queue.c` (unitaire) : ajout et lecture, `clear`, débordement (plus anciens jetés, compte des pertes), recherche
  binaire, motif vide, motif à cheval sur deux ajouts, échéance, fermeture pendant l'attente, données rendues avant `CLOSED`,
  annulation pendant l'attente (réveil en moins de 100 ms), producteur sur un autre thread.
- `tests/test_script_port.c` (unitaire, boucle GLib) : série sur pty (paramètres appliqués, réception dans la file, envoi), périphérique
  absent (erreur), pty dont le maître est fermé → `LOST` + file fermée, verrou (deuxième ouverture du même esclave refusée), client
  TCP vers un écouteur local (`CONNECTING` → `OPEN`), connexion refusée → `LOST` avec raison, serveur (`LISTENING` → `CONNECTED` →
  `LISTENING`), `try_send` sur serveur sans client → 0, `try_send` jusqu'à `EAGAIN` sur un pty non lu ; `gt_script_hex_decode`
  (valide, blancs, casse, impair, invalide avec position), noms de parité et de flux.
- `tests/test_script_engine.c` (intégration, existant, étendu) : « Stop » interrompt `while true do end` et une boucle de `pcall`
  (fin `STOPPED` en moins de 1 s) ; « Stop » pendant `sleep(60000)` et `wait_for(…, 60000)` ; `os.exit()` → `FINISHED`,
  `os.exit(3)` → `ERROR` avec le message, le processus de test survit ; `print` dans le journal ; raisons de fin ; journal en rafale
  (100 000 lignes) regroupé (moins de 1 000 appels de la fonction de journal, rien de perdu sous le plafond) ; série sur pty avec
  débit 115200 et parité `"even"`, parité invalide → `false, "invalid parity"` ; `send` de 1 Mio vers un pty non lu → `false,
  "timeout"`, puis avec un lecteur → `true` et 1 Mio reçu à l'identique ; `send_hex` invalide → rien d'envoyé ; port perdu →
  `wait_for` rend `nil, "closed"` en moins de 500 ms, `status` = `"lost"` ; serveur TCP : `status` `"listening"` puis `"connected"` ;
  `script_engine_shutdown` pendant une boucle infinie avec un port pty ouvert → retour en moins de 3 s, descripteurs refermés ;
  `send(0, …)` liaison en échec → `false, "not connected"`. Bouchon de la liaison : `gt_link_get_state`.
- Suite graphique (`tests/gui/test_script_arret.py`) : panneau de scripts, script `while true do end`, « Run » puis « Stop » →
  « [Script stopped] » dans la console et « Run » de nouveau actif ; script `while true do end` relancé, fermeture de la fenêtre →
  le processus se termine (code 0) en moins de 10 s.
- `lua-syntax` : nouvel exemple compris.

## 6. Documentation et exemples

- `scripts/11_tcp_server.lua` : serveur TCP, `status`, écho des lignes reçues, arrêt propre.
- Exemples existants relus contre l'API (aucun changement de sens attendu ; `07_tcp_client.lua` montre le délai de connexion).
- `.claude/skills/lua-script/SKILL.md` : tableau de l'API mis à jour (`status`, délais, raisons, `print`, `os.exit`, Stop).
- `CLAUDE.md` : paragraphe Lua (ports sur `GtScriptPort`/`GtTransport`, files, arrêt, journal regroupé, `shutdown`) et liste
  `core_sources` (`rx_queue.c`, `script_port.c`) ; `tests/README.md` : nouveaux tests.
- Revue : M25 et M28 marqués « Corrigé par le sous-projet 5 (`feature/refonte-lua`) », point d'architecture 6 et remarques Lua
  annotés, annexe des remarques mineures reportées par le sous-projet 5.
- `po/fr.po` : nouvelles chaînes traduites (« [Script stopped] », « [Script finished with an error] », « Script exited with
  status %d », « Port %d: receive buffer full, oldest data dropped », « [%zu bytes of log output dropped] », « Lua error: %s »),
  `po/POTFILES.in` à jour.
