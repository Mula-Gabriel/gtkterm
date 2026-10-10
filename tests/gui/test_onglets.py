# Onglets (sous-projet 6b) : un seul au démarrage (barre cachée), New tab par le dialogue Port prérempli (Annuler ne crée rien), barre visible à deux,
# disposition du panneau de script reprise de l'onglet actif, frappe sur le port de l'onglet affiché, clic sur une étiquette puis raccourci de macro
# (focus au terminal de la page), Ctrl+Page sans Maj envoyé au port, navigation (Ctrl+Maj+Page, app.active-tab), titre, Ctrl+Maj+W, dernier onglet non fermable, Ctrl+Maj+T.
import os, sys, time
from gi.repository import GLib
from gtktest import *

e = Env('onglets')
write_macros_v2(e.macros_path, [dict(label='M1', shortcut='<Control>F5', action='<M1>')])
macro_config(e, e.macros_path)	# le nouvel onglet copie le profil : même fichier de macros, chargé dans ses propres macros
e.start()
p2 = e.new_pty()
check(len(e.sessions()) == 1, 'un seul onglet au demarrage')
check(not e.find(name='tab-label'), 'barre d onglets cachee avec un onglet')
check(not e.describe('close-tab')[0], 'close-tab desactivee avec un onglet')

e.action('new-tab')
e.find1(name='port-dialog')
check(e.find1(name='port-device').get('label') == e.port, 'dialogue prerempli avec le port de l onglet actif')
e.activate('port-cancel')
check(wait_until(lambda: not e.find(name='port-dialog')), 'Annuler ferme le dialogue')
check(len(e.sessions()) == 1, 'Annuler ne cree pas d onglet')

# Disposition du panneau de script : le nouvel onglet reprend celle de l'onglet actif
e.set_state('script-layout', GLib.Variant('s', 'leftright'))
check(e.state('script-layout') == 'leftright', 'premier onglet : disposition gauche-droite')
e.set_state('show-script-panel', GLib.Variant('b', True))
check(wait_until(lambda: e.find(name='script-editor')), 'premier onglet : panneau de script affiche')


def editeur_a_gauche_de_la_console():
    """Éditeur et console de script de la page affichée, côte à côte : l'éditeur à gauche, sur la même hauteur (disposition gauche-droite)."""
    ed, co = e.find(name='script-editor'), e.find(name='script-console')
    if len(ed) != 1 or len(co) != 1 or ed[0].get('w', 0) <= 0 or co[0].get('w', 0) <= 0:
        return False
    ed, co = ed[0], co[0]
    return ed['x'] + ed['w'] <= co['x'] and ed['y'] < co['y'] + co['h'] and co['y'] < ed['y'] + ed['h']

open_tab(e, p2.port)
check(wait_until(lambda: len(e.find(name='tab-label')) == 2), 'barre d onglets visible avec deux onglets')
check(e.describe('close-tab')[0], 'close-tab active avec deux onglets')
check(e.state('active-tab') == 1, 'le nouvel onglet est actif, a droite')
check(wait_until(lambda: e.active_session()['state'] == 'open'), 'port du nouvel onglet ouvert')
check(wait_until(lambda: os.path.basename(p2.port) in (e.title() or '')), 'titre de la fenetre : onglet actif (%r)' % e.title())
check(e.state('script-layout') == 'leftright', 'nouvel onglet : disposition du panneau de script reprise de l onglet actif')
check(e.state('show-script-panel') is True, 'nouvel onglet : panneau de script affiche comme dans l onglet actif')
check(wait_until(editeur_a_gauche_de_la_console), 'nouvel onglet : editeur de script a gauche de la console (geometrie reelle)',
      [(w.get('name'), w.get('x'), w.get('y'), w.get('w'), w.get('h')) for w in e.find(name='script-editor') + e.find(name='script-console')])
e.set_state('show-script-panel', GLib.Variant('b', False))	# page du second onglet (actif) sans panneau de script ; le premier garde le sien

# Nouvel onglet : le focus est au terminal de sa page dès sa création (sans clic), sa macro part sur son port
time.sleep(0.3)
e.drain()
p2.drain()
e.bw.key('F5', mods=('ctrl',))
d = p2.read(1.0)
check(d == b'<M1>', 'nouvel onglet : focus au terminal, raccourci de macro sur son port (%r)' % d)
check(e.read_pty(0.3) == b'', 'rien sur le port du premier onglet')

e.drain()
p2.drain()
e.bw.click(e.find1(name='terminal'))
e.bw.type('xy')
check(p2.read(1.0) == b'xy', 'frappe envoyee sur le port de l onglet affiche')
check(e.read_pty(0.3) == b'', 'rien sur le port de l autre onglet')

# Ctrl+Page (sans Maj), focus au terminal : la séquence de la touche part sur le port de l'onglet actif, le GtkNotebook ne change pas d'onglet
e.drain()
p2.drain()
e.bw.key('Page_Up', mods=('ctrl',))
e.bw.key('Page_Down', mods=('ctrl',))
d = p2.read(1.0)
check(d == b'\x1b[5;5~\x1b[6;5~', 'Ctrl+Page precedente/suivante : sequences sur le port de l onglet actif (%r)' % d)
check(e.read_pty(0.3) == b'', 'Ctrl+Page : rien sur le port de l autre onglet')
check(e.state('active-tab') == 1, 'Ctrl+Page : l onglet actif ne change pas', e.state('active-tab'))

e.bw.key('Page_Up', mods=('ctrl', 'shift'))
check(wait_until(lambda: e.state('active-tab') == 0), 'Ctrl+Maj+Page precedente : premier onglet')
check(wait_until(lambda: os.path.basename(e.port) in (e.title() or '')), 'titre suit l onglet actif')
# Changement d'onglet au clavier : le focus va au terminal de la page affichée, la macro de cet onglet part sur son port
time.sleep(0.3)
e.drain()
p2.drain()
e.bw.key('F5', mods=('ctrl',))
d = e.read_pty(1.0)
check(d == b'<M1>', 'raccourci apres Ctrl+Maj+Page : macro sur le port du premier onglet (%r)' % d)
check(p2.read(0.3) == b'', 'rien sur le port du second onglet')
e.bw.key('Page_Down', mods=('ctrl', 'shift'))
check(wait_until(lambda: e.state('active-tab') == 1), 'Ctrl+Maj+Page suivante : second onglet')

# Clic sur l'étiquette du premier onglet, puis raccourci de macro : le focus revient au terminal de la page, la macro part sur son port
etiquette = [w for w in e.find(name='tab-name') if w.get('label') == os.path.basename(e.port)]
check(len(etiquette) == 1, 'etiquette du premier onglet', [w.get('label') for w in e.find(name='tab-name')])
e.bw.click(etiquette[0])
check(wait_until(lambda: e.state('active-tab') == 0), 'clic sur l etiquette : premier onglet actif')
time.sleep(0.3)
e.drain()
p2.drain()
e.bw.key('F5', mods=('ctrl',))
d = e.read_pty(1.0)
check(d == b'<M1>', 'raccourci apres le clic sur l etiquette : macro sur le port du premier onglet (%r)' % d)
check(p2.read(0.3) == b'', 'rien sur le port du second onglet')

e.bw.key('Page_Down', mods=('ctrl', 'shift'))
check(wait_until(lambda: e.state('active-tab') == 1), 'Ctrl+Maj+Page suivante : second onglet de nouveau')
e.set_state('active-tab', GLib.Variant('i', 0))
check(wait_until(lambda: e.active_session()['index'] == 0), 'app.active-tab change d onglet')

e.bw.key('w', mods=('ctrl', 'shift'))
check(wait_until(lambda: len(e.sessions()) == 1), 'Ctrl+Maj+W ferme l onglet actif')
check(e.sessions()[0]['name'] == os.path.basename(p2.port), 'l onglet restant est le second')
check(wait_until(lambda: not e.find(name='tab-label')), 'barre cachee de nouveau')
check(not e.describe('close-tab')[0], 'close-tab desactivee de nouveau')
e.action('close-tab')
time.sleep(0.5)
check(len(e.sessions()) == 1, 'le dernier onglet ne se ferme pas')

e.bw.key('t', mods=('ctrl', 'shift'))
check(bool(e.find1(name='port-dialog')), 'Ctrl+Maj+T ouvre le dialogue Port')
e.activate('port-cancel')
e.finish()
sys.exit(conclude())
