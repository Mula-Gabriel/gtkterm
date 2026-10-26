# File d'émission du port principal — conception

Date : 2026-10-26. Branche : `fix/file-emission`.

## Le défaut

`gt_link_send()` (frappe, collage, macros, barre hexadécimale, `send_macro` Lua) faisait **une seule** `gt_transport_send()`, donc une seule
`write()`/`send()` sur un descripteur non bloquant (série, client TCP, client accepté par le serveur) :

- écriture partielle : le reste est perdu sans message. Un collage de 64 Kio sur une pty est coupé à ~11,5 Kio, même quand l'autre bout lit en
  continu ; en TCP, tout ce qui dépasse les tampons du noyau est perdu ;
- `EAGAIN` : tout le bloc est perdu (la frappe qui suit un gros collage, une macro envoyée pendant que le port est plein) ;
- `macros.c` affichait « Macro "%s" sent! » dès qu'un octet était parti ; la barre hexadécimale affichait un compte partiel ; la scrutation et
  `send_macro` Lua ignoraient l'échec.

Défaut voisin : en **RS485**, chaque envoi levait RTS, dormait `usleep(avant)`, écrivait, appelait `tcdrain()` (≈ 4 s pour 4 Kio à 9600 bauds),
dormait `usleep(après)` puis baissait RTS, **sur le fil principal** : l'interface gelait. RTS basculait même sur `EAGAIN`.

Ne sont pas touchés (ils reprennent déjà les écritures partielles et gardent leur régulation) : `send`/`send_hex` Lua sur le port 0
(`send_all`), les ports Lua 1–15 (`gt_script_port_try_send`), l'envoi de fichier (`GtFileSender`).

## Conception retenue

### Transport : une file d'émission par instance

Le transport possède le descripteur, ses surveillances, la perte et le RTS du RS485 : la file vit donc là.

- `TransportCallbacks` gagne `on_sent(data, len, tag, user_data)`, appelé **par morceau réellement écrit** ; NULL permis (les ports Lua et les
  tests existants, initialisés avec deux champs, restent valides).
- `gt_transport_send_queued(t, data, len, tag)` : **tout ou rien**. File vide (et pas de RS485) : écriture directe tout de suite (la frappe reste
  synchrone dans le cas courant), le reste en file. Sinon tout en file, derrière ce qui attend. Rendu : `len` si accepté ; 0 si rien n'est
  ouvert (connexion en cours, serveur sans client : rien n'est mis en file) ; -1 et `errno` : `ENOBUFS` si `len` dépasse la place restante
  (plafond `GT_TRANSPORT_QUEUE_MAX` = 1 Mio, comme `GtRxQueue` ; rien n'est écrit ni mis en file), `EIO` après une perte, l'erreur de la
  première écriture directe sinon (rien en file).
- La file est une `GQueue` de morceaux (`GBytes`, décalage, étiquette). Une surveillance `G_IO_OUT` n'est posée **que tant que la file n'est
  pas vide** ; elle écrit ce que le descripteur accepte, morceau après morceau, et appelle `on_sent` après chaque écriture (état de la file déjà
  mis à jour : un gestionnaire peut remettre des octets en file, ils passent derrière).
- La file est **jetée** à la fermeture, à la perte (`transport_mark_lost`) et, en serveur, au départ du client ou à son remplacement par un
  nouveau client : rien de destiné à l'ancien client ne part vers le suivant. Une erreur d'écriture depuis la surveillance vaut perte (série,
  client TCP) ou départ du client (serveur, `CLIENT_GONE`).
- `gt_transport_try_send(t, data, len, tag)` : une seule tentative, jamais bloquante, pour les appelants qui régulent eux-mêmes ; `-1`/`EAGAIN`
  **tant que la file n'est pas vide** (l'ordre est préservé ; jamais 0, qui voudrait dire « pas connecté »). Les octets écrits passent aussi par
  `on_sent`. `gt_transport_send(t, data, len)` = `gt_transport_try_send(t, data, len, NULL)` : inchangée pour les ports Lua 1–15.
- `gt_transport_get_queued(t)` : octets acceptés pas encore écrits.

### RS485 sans blocage

Machine d'état dans le transport : repos → RTS levé (un `TIOCMBIS`), délai « avant » par `g_timeout` → écriture de la file par la surveillance
→ file vide : `TIOCOUTQ` interrogé toutes les 5 ms jusqu'à 0, puis le registre de sortie de l'UART (`TIOCSERGETLSR`, bit `TIOCSER_TEMT`)
s'il est lisible (attente bornée : temps d'émission de ce que garde le pilote + 1 s, pour qu'un pilote qui ne se vide jamais ne garde
pas RTS levé) → délai « après » par `g_timeout` → RTS baissé (un `TIOCMBIC`) → repos. Des octets arrivés pendant l'attente de vidage ou le
délai « après » reprennent l'écriture sans rebasculer RTS : **un seul TIOCMBIS / TIOCMBIC pour toute une rafale**, aucun basculement sur
`EAGAIN`, aucun `usleep` ni `tcdrain` sur le fil principal.

Choix : `tcdrain()` est remplacé par l'interrogation de `TIOCSERGETLSR` (non bloquante) plutôt qu'un `tcdrain` « court » : sur un 16550 la FIFO
matérielle peut contenir 16 à 64 octets, soit jusqu'à 2 s de gel à 300 bauds. Un pilote sans `TIOCSERGETLSR` (USB, pty) se contente de
`TIOCOUTQ`, ce que faisait déjà `tcdrain` pour la plupart d'entre eux.

En RS485, `gt_transport_try_send` n'écrit jamais directement (RTS doit d'abord monter) : au repos, elle accepte en file ce qui tient
(au plus le plafond) et lance un cycle ; pendant un cycle, `EAGAIN`. L'envoi de fichier garde ainsi un cycle RTS par morceau, comme avant.

Les ports Lua 1–15 ne peuvent pas être ouverts en RS485 (`open` n'accepte que none/xon/rts) : rien à faire pour eux.

### Liaison

- `gt_link_send()` (frappe, macro, barre hexadécimale, `send_macro` Lua) devient **tout accepté ou rien** : `len` si accepté, 0 si la liaison
  n'est pas ouverte (rien n'est mis en file pendant CONNECTING, LISTENING, LOST : jamais de rejeu), -1 et `errno` (`ENOBUFS` : file pleine).
- Historique et signal `sent` sont émis **seulement depuis `on_sent`** : uniquement les octets réellement écrits, origine portée par l'étiquette,
  heure réelle d'écriture. L'invariant « historique = octets partis ; écho rendu depuis `sent` » est conservé.
- `gt_link_try_send()` (une seule écriture, comportement d'avant) pour `GtFileSender` (via `file_actions.c`) et `send`/`send_hex` Lua sur le
  port 0 : ils gardent leur régulation et attendent (`EAGAIN`) que la file se vide.
- `gt_link_get_queued()`.
- Contrat de réentrance (`link.h`) : `sent` peut aussi être émis depuis la surveillance du descripteur ; toujours ni `gt_link_open` ni
  `gt_link_close` dans les gestionnaires.
- Fermeture, perte, reconnexion : la file meurt avec le transport ; rien n'est rejoué après une reconnexion.

### Interface et messages

- Collage ou frappe refusés (`ENOBUFS`) : message temporaire dans la barre d'état de l'onglet, qui conseille « Send RAW file… ». La vue du
  terminal reçoit sa session (pointeur faible, aucun état statique).
- Barre hexadécimale : « %d byte(s) sent! » seulement si tout est accepté ; sinon « Nothing sent: … » (port pas ouvert, file pleine, erreur),
  saisie gardée pour réessayer.
- Macros : plus de « sent! » sur un échec ; une file pleine ou une erreur est signalée (« Macro "%s" not sent: … », au plus une fois toutes les
  3 s par macro, comme un argument refusé). Port fermé : pas de message, le « — lost » de la barre d'état reste visible (comme avant).
- Scrutation : un tir est **sauté** tant que la file du port n'est pas vide, pour qu'une scrutation plus rapide que la ligne n'accumule pas un
  retard (jusqu'à 1 Mio de trames périmées).
- Lua `send_macro` : vrai une fois mis en file ; faux avec une raison (`"not connected"`, `"link lost"`, `"send queue full"`) sinon.

## Hors champ

`gt_session_is_busy()` ne tient pas compte de la file (la question « Close this tab? » parle de script ou de fichier) : un onglet fermé pendant
qu'un gros collage part encore perd la fin, comme une fermeture de port. Point ouvert.
