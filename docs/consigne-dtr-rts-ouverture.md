# gtkterm : laisser DTR et RTS au repos à l'ouverture d'un port série

Dépôt : `~/git/gtkterm` (fork maison, branche `master`, seul remote `Maison` : **ne rien pousser**, l'utilisateur s'en charge). Lis d'abord `CLAUDE.md` et `README.md` du dépôt.

## Le problème, constaté le 2026-11-12

La carte d'autotest F3 (LPC1224, adaptateur FTDI FT232R, `/dev/serial/by-id/usb-FTDI_FT232R_USB_UART_AG0KJM4A-if00-port0`, console à 9600 8N1) ne répond pas dans gtkterm sous Linux, alors qu'elle marche ailleurs.

- **DTR est câblé sur le reset de la carte** : DTR actif, la carte est tenue en reset et reste muette.
- **RTS pilote probablement l'entrée en chargeur ISP** (non vérifié sur schéma) : au relâchement du reset avec RTS actif, la carte démarre dans le bootloader ROM et ne renvoie que des octets illisibles.
- **Le noyau Linux lève DTR et RTS à l'`open()` d'un tty** (comportement standard, `HUPCL` les rebaisse à la fermeture). Une application qui ne les rebaisse pas tout de suite laisse la carte en reset, ou la fait démarrer en ISP.
- Preuve (pyserial) : avec `s.dtr = s.rts = False` posés **avant** `open()`, puis une impulsion DTR pour reset, la carte démarre (`Init !`, `Welcome to F3 Autotest`) et répond à `Simulation help`. Avec DTR actif : silence. Ouverture par défaut : octets illisibles.

gtkterm ne touche aujourd'hui DTR/RTS qu'à la demande (bascule manuelle, `src/transport.c` vers la ligne `int bit = (line == GT_MODEM_DTR) ? TIOCM_DTR : TIOCM_RTS;`) et pour le RS485 (`TIOCMBIS`/`TIOCMBIC` de RTS). Rien ne fixe leur état à l'ouverture.

## Ce qui est demandé

1. **Option par port** (dans les paramètres de connexion, enregistrée avec le profil / le projet comme les autres réglages du port) : état de DTR et de RTS à l'ouverture — `actif` (comportement actuel, par défaut, pour ne rien casser) ou `inactif`.
2. **À l'ouverture**, si `inactif` : baisser la ligne **immédiatement après `open()`**, avant tout `tcsetattr` qui pourrait la relever, par `ioctl(fd, TIOCMBIC, …)`. Vérifier que le réglage de vitesse ou le contrôle de flux matériel (`CRTSCTS`) ne relève pas RTS ensuite ; si le contrôle de flux RTS/CTS est actif, l'option RTS est sans objet (le dire dans l'IHM).
3. **Pas de relance du DTR** à la reconnexion automatique, au changement de vitesse ni à la réouverture après débranchement USB : même règle à chaque `open()`.
4. **Fermeture** : documenter l'effet de `HUPCL` (le noyau baisse DTR/RTS à la fermeture : c'est sans danger ici, la carte redémarre normalement). Ne pas désactiver `HUPCL` sans raison.
5. **Barre d'état** : l'indicateur DTR/RTS (`src/ui/status_bar.c`) doit montrer l'état réel juste après l'ouverture.
6. Le RS485 (RTS piloté par rafale) garde son comportement ; l'option ne s'applique qu'hors RS485.
7. Le premier reset volontaire reste possible : la bascule manuelle existante de DTR suffit (impulsion = reset de la carte).

## Limite à connaître

Le noyau lève DTR/RTS pendant quelques microsecondes entre `open()` et le `TIOCMBIC` : une impulsion de reset reste possible à chaque ouverture (la carte redémarre, sans conséquence). Si l'on veut zéro impulsion, il n'existe pas de solution portable côté application (seulement un réglage EEPROM du FTDI ou un `stty -hupcl` persistant qui ne couvre pas l'`open()`) : le noter dans la doc plutôt que d'en promettre plus.

## Vérification

- Tests unitaires du dépôt s'il y en a pour le transport (`tests/`), dont un cas qui vérifie l'ordre `open()` → `TIOCMBIC` (bouchon d'ioctl) avec l'option à `inactif`, et l'absence d'appel avec l'option à `actif`.
- Essai réel, **demandé à l'utilisateur** (humain dans la boucle) : profil FTDI de la carte à 9600 8N1, DTR et RTS `inactifs` → `Simulation help` répond ; avec `actif` → silence (ancien comportement).
- Commits en français, terminés par la ligne `Co-Authored-By` habituelle ; aucun push.
