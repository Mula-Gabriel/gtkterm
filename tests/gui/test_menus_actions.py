# Menus et actions (sous-projet 6a, §6.2) : chaque élément de menus.ui vise une action exportée ; cases du menu = état réel ; options de vue
# indépendantes du mode (spec §5) ; raccourcis Ctrl+Maj+L, Ctrl+Maj+B, F5/F6 ; Échap avec et sans « ESC clear screen » ; Copy/Paste/Select All.
# 6c (spec §3.2-10) : menus de premier niveau File, Edit, View, Port, Terminal, Macros, Log, Help ; actions project-* sans paramètre ni état ;
# Ctrl+Maj+O ouvre le sélecteur « Open project ».
import os
import sys
import xml.etree.ElementTree as ET
from gi.repository import GLib
from gtktest import *

MENUS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'src', 'ui', 'menus.ui')
BOOLEENNES = ('local-echo', 'crlf-auto', 'timestamp', 'esc-clear-screen', 'hex-index', 'auto-reconnect')


def menus_premier_niveau():
    racine = ET.parse(MENUS).getroot()
    barre = [m for m in racine.iter('menu') if m.get('id') == 'menubar'][0]
    return [[a.text.strip() for a in sm.findall('attribute') if a.get('name') == 'label'][0] for sm in barre.findall('submenu')]


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
check(menus_premier_niveau() == ['File', 'Edit', 'View', 'Port', 'Terminal', 'Macros', 'Log', 'Help'], 'menus de premier niveau dans l ordre : %r'
      % menus_premier_niveau())

# 2. Valeurs de départ et SetState
check(e.state('view-mode') == 'ascii', 'vue ASCII au demarrage : %r' % e.state('view-mode'))
check(e.state('hex-bytes') == 16, 'hex-bytes 16 par defaut : %r' % e.state('hex-bytes'))
for nom in BOOLEENNES:
    check(e.state(nom) is False, '%s faux au depart' % nom)
    e.set_state(nom, GLib.Variant('b', True))
    check(wait_until(lambda: e.state(nom) is True, 2), '%s vrai apres SetState' % nom)
    e.set_state(nom, GLib.Variant('b', False))
    check(wait_until(lambda: e.state(nom) is False, 2), '%s faux apres SetState' % nom)

# 3. Cases du menu = état réel (menu Terminal ouvert par un clic)
e.set_state('local-echo', GLib.Variant('b', True))
e.bw.click(e.find1(label='Terminal'))
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
# 6b : chaque action garde son type de paramètre et d'état (relais)
ATTENDU = {
    'local-echo': ('', 'b'), 'crlf-auto': ('', 'b'), 'timestamp': ('', 'b'), 'esc-clear-screen': ('', 'b'), 'hex-index': ('', 'b'),
    'auto-reconnect': ('', 'b'), 'view-mode': ('s', 's'), 'hex-bytes': ('u', 'u'), 'log-paused': ('', 'b'), 'show-hex-bar': ('', 'b'),
    'show-macro-panel': ('', 'b'), 'show-script-panel': ('', 'b'), 'script-layout': ('s', 's'),
    'clear-screen': ('', None), 'open-port': ('', None), 'close-port': ('', None), 'send-break': ('', None), 'toggle-dtr': ('', None),
    'toggle-rts': ('', None), 'clear-scrollback': ('', None), 'copy': ('', None), 'paste': ('', None), 'select-all': ('', None), 'find': ('', None),
    'send-file': ('', None), 'save-raw': ('', None), 'save-ascii': ('', None), 'log-start': ('', None), 'log-stop': ('', None), 'log-clear': ('', None),
    'detach-macros-template': ('', None),
}
for nom, (ptype, stype) in ATTENDU.items():
    en, pt, st = e.describe(nom)
    check(pt == ptype, 'type de parametre de %s : %r' % (nom, pt))
    check((stype is None) == (st is None), 'etat de %s present ou absent comme attendu' % nom)
    check(e.state_type(nom) == stype, 'type exact de l etat de %s : %r' % (nom, e.state_type(nom)))

# 10. Actions de projet (6c) : sans paramètre ni état ; Ctrl+Maj+O ouvre le sélecteur « Open project » (Échap le ferme)
for nom in ('project-new', 'project-open', 'project-save', 'project-save-as', 'project-close'):
    en, pt, st = e.describe(nom)
    check(pt == '' and st is None, '%s sans parametre ni etat (%r, %r)' % (nom, pt, st))
e.bw.click(e.find1(name='terminal'))
e.bw.key('o', mods=('ctrl', 'shift'))
ouvert = wait_until(lambda: [w for w in e.windows() if w.get('title') == 'Open project'], 5)
check(bool(ouvert), 'Ctrl+Maj+O ouvre « Open project » (%r)' % [w.get('title') for w in e.windows()])
if ouvert:
    e.bw.focus_window('Open project')
    e.bw.key('Escape')
    check(wait_until(lambda: not [w for w in e.windows() if w.get('title') == 'Open project'], 5), 'Echap ferme « Open project »')
check(len(e.sessions()) == 1 and e.project().get('open') is False, 'rien ne change apres l annulation')
e.finish()
sys.exit(conclude())
