# Menus et actions (sous-projet 6a, §6.2) : chaque élément de menus.ui vise une action exportée ; cases du menu = état réel ; options de vue
# indépendantes du mode (spec §5) ; raccourcis Ctrl+Maj+L, Ctrl+Maj+B, F5/F6 ; Échap avec et sans « ESC clear screen » ; Copy/Paste/Select All.
import os
import sys
import xml.etree.ElementTree as ET
from gi.repository import GLib
from gtktest import *

MENUS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'src', 'ui', 'menus.ui')
BOOLEENNES = ('local-echo', 'crlf-auto', 'timestamp', 'esc-clear-screen', 'hex-index', 'auto-reconnect')


def actions_du_menu():
    racine = ET.parse(MENUS).getroot()
    return sorted({a.text.strip() for a in racine.iter('attribute') if a.get('name') == 'action'})


def texte():
    return e.terminal_text()


e = Env('menus_actions')
e.write_config(e.general() + e.profile())
e.start()

# 1. Chaque action visée par menus.ui existe
exportees = set(e.actions())
noms = actions_du_menu()
check(len(noms) >= 20, 'menus.ui lu : %d actions' % len(noms))
for nom in noms:
    check(nom.startswith('app.') and nom[4:] in exportees, 'action du menu exportee : %s' % nom)

# 2. Valeurs de départ et SetState
check(e.state('view-mode') == 'ascii', 'vue ASCII au demarrage : %r' % e.state('view-mode'))
check(e.state('hex-bytes') == 16, 'hex-bytes 16 par defaut : %r' % e.state('hex-bytes'))
for nom in BOOLEENNES:
    check(e.state(nom) is False, '%s faux au depart' % nom)
    e.set_state(nom, GLib.Variant('b', True))
    check(wait_until(lambda: e.state(nom) is True, 2), '%s vrai apres SetState' % nom)
    e.set_state(nom, GLib.Variant('b', False))
    check(wait_until(lambda: e.state(nom) is False, 2), '%s faux apres SetState' % nom)

# 3. Cases du menu = état réel (menu Configuration ouvert par un clic)
e.set_state('local-echo', GLib.Variant('b', True))
e.bw.click(e.find1(label='Configuration'))
echo = e.find1(label='Local echo', type='GtkModelButton')
horo = e.find1(label='Timestamp', type='GtkModelButton')
check(echo.get('active') is True, 'case Local echo cochee comme l option')
check(horo.get('active') is False, 'case Timestamp decochee comme l option')
e.bw.key('Escape')
check(wait_until(lambda: not e.find(label='Local echo', type='GtkModelButton'), 3), 'menu referme')
e.set_state('local-echo', GLib.Variant('b', False))

# 4. « Show index » et « Hexadecimal chars » en vue ASCII : l'option change, la vue reste ASCII (spec §5)
e.write_pty(b'AB\r\n')
check(wait_until(lambda: 'AB' in texte(), 3), 'AB recu')
e.set_state('hex-index', GLib.Variant('b', True))
e.set_state('hex-bytes', GLib.Variant('u', 8))
check(e.state('view-mode') == 'ascii', 'Show index / Hexadecimal chars en vue ASCII : vue inchangee')
check(e.state('hex-index') is True and e.state('hex-bytes') == 8, 'options de la vue hexadecimale enregistrees')
check(wait_until(lambda: 'AB' in texte(), 2) and '41 42' not in texte(), 'affichage toujours ASCII : %r' % texte()[:80])
e.set_state('view-mode', GLib.Variant('s', 'hex'))
check(wait_until(lambda: '     0: 41 42 0D 0A' in texte(), 3), 'index applique au passage en hexadecimal : %r' % texte()[:160])
e.write_pty(b'CDEFGHIJ')
check(wait_until(lambda: '     8: 47 48 49 4A' in texte(), 3), 'lignes de 8 octets : %r' % texte()[:200])
e.set_state('view-mode', GLib.Variant('s', 'ascii'))
e.set_state('hex-index', GLib.Variant('b', False))
e.set_state('hex-bytes', GLib.Variant('u', 16))
check(wait_until(lambda: 'ABCDEFGHIJ' in texte().replace('\n', ''), 3), 'retour en ASCII')

# 5. Échap : efface l'écran sans rien envoyer avec l'option, envoyé sans elle
e.set_state('esc-clear-screen', GLib.Variant('b', True))
e.bw.click(e.find1(name='terminal'))
e.drain()
e.bw.key('Escape')
check(wait_until(lambda: 'AB' not in texte(), 3), 'Echap efface l ecran avec l option')
check(e.read_pty(0.5) == b'', 'Echap non envoye avec l option')
e.set_state('esc-clear-screen', GLib.Variant('b', False))
e.bw.key('Escape')
check(e.read_pty(0.8) == b'\x1b', 'Echap envoye sans l option')

# 6. Ctrl+Maj+L (app.clear-screen)
e.write_pty(b'EFFACE\r\n')
check(wait_until(lambda: 'EFFACE' in texte(), 3), 'EFFACE recu')
e.bw.key('l', mods=('ctrl', 'shift'))
check(wait_until(lambda: 'EFFACE' not in texte(), 3), 'Ctrl+Maj+L efface l ecran')

# 7. Select All, Copy (sensible selon la sélection), Paste (le texte collé part sur le port)
e.write_pty(b'COPIE\r\n')
check(wait_until(lambda: 'COPIE' in texte(), 3), 'COPIE recu')
check(not e.describe('copy')[0], 'Copy inactif sans selection')
e.action('select-all')
check(wait_until(lambda: e.describe('copy')[0], 3), 'Copy actif apres Select All')
e.action('copy')
e.drain()
e.action('paste')
check(b'COPIE' in e.read_pty(1.5), 'Paste envoie le texte copie au port')

# 8. Signaux de contrôle : actifs en série ; Ctrl+Maj+B = break, aucun octet (ni STX), application intacte
for nom in ('send-break', 'toggle-dtr', 'toggle-rts'):
    check(e.describe(nom)[0], '%s actif sur une liaison serie ouverte' % nom)
e.bw.click(e.find1(name='terminal'))
e.drain()
e.bw.key('b', mods=('ctrl', 'shift'))
check(e.read_pty(0.8) == b'', 'Ctrl+Maj+B : break, aucun octet envoye')
e.bw.type('z')
check(e.read_pty(0.8) == b'z', 'frappe toujours envoyee apres le break')

# 9. F6 ferme le port, F5 le rouvre (raccourcis clavier réels)
e.bw.key('F6')
check(wait_until(lambda: not e.describe('send-break')[0], 3), 'F6 : port ferme, Send break inactif')
e.drain()
e.bw.type('x')
check(e.read_pty(0.8) == b'', 'port ferme : rien envoye')
e.bw.key('F5')
check(wait_until(lambda: e.describe('send-break')[0], 3), 'F5 : port rouvert, Send break actif')
e.bw.type('y')
check(e.read_pty(0.8) == b'y', 'port rouvert : frappe envoyee')
e.finish()
sys.exit(conclude())
