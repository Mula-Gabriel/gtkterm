# Macros par onglet (spec §3.1-1, §3.1-2) : un fichier de macros différent par onglet, panneaux différents, clic et raccourci sur le port de l'onglet,
# raccourci d'un onglet sans effet dans l'autre, polling qui continue sur son port quand on change d'onglet et s'arrête à la fermeture de son onglet.
import os, sys, time
from gi.repository import GLib
from gtktest import *

e = Env('onglets_macros')
ma = e.macros_path
mb = os.path.join(e.cfg, 'macros_b.ini')
write_macros_v2(ma, [dict(label='MA', action='<A>', shortcut='F2', polling=True, period=200)])
write_macros_v2(mb, [dict(label='MB', action='<B>')])
macro_config(e, ma)
e.start()
p2 = e.new_pty()
open_tab(e, p2.port)
check(e.sessions()[1]['macros_file'] == ma, 'nouvel onglet : fichier de macros de l onglet actif')
e.action('load-macros')
load_macros_free(e, mb)
check(wait_until(lambda: e.sessions()[1]['macros_file'] == mb), 'second onglet : fichier B')
check(e.sessions()[0]['macros_file'] == ma, 'premier onglet : fichier A inchange')
check(wait_until(lambda: macro_buttons(e) and all('MB' in l for l in macro_buttons(e))), 'panneau du second onglet : MB seulement')

e.drain()
p2.drain()
e.activate('macro-%d' % macro_id(e, 'MB'))
check(p2.read(1.0) == b'<B>', 'MB envoyee sur le port du second onglet')
check(e.read_pty(0.3) == b'', 'rien sur le port du premier')
e.bw.click(e.find1(name='terminal'))
e.bw.key('F2')
check(b'<A>' not in p2.read(0.8), 'F2 (raccourci de A) sans effet de macro dans le second onglet')
check(e.read_pty(0.3) == b'', 'F2 n envoie rien sur le premier port')

e.set_state('active-tab', GLib.Variant('i', 0))
check(wait_until(lambda: any('MA' in l for l in macro_buttons(e))), 'panneau du premier onglet : MA')
e.drain()
e.bw.click(e.find1(name='terminal'))
e.bw.key('F2')
check(b'<A>' in e.read_pty(0.8), 'F2 envoie MA dans le premier onglet')

# Polling de MA (premier onglet) : continue quand le second est affiché, s'arrête à la fermeture du premier
e.activate('macro-%d' % macro_id(e, '⏱ MA'))
check(e.read_pty(1.0).count(b'<A>') >= 3, 'polling de MA actif')
e.set_state('active-tab', GLib.Variant('i', 1))
e.drain()
p2.drain()
check(e.read_pty(1.0).count(b'<A>') >= 3, 'second onglet affiche : le polling continue sur le premier port')
check(b'<A>' not in p2.read(0.3), 'le polling ne part jamais sur le second port')
e.set_state('active-tab', GLib.Variant('i', 0))
e.action('close-tab')
check(wait_until(lambda: len(e.sessions()) == 1, 5), 'premier onglet ferme')
e.drain()
check(e.read_pty(1.0) == b'', 'polling arrete avec son onglet')
e.finish()
sys.exit(conclude())
