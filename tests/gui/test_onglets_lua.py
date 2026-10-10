# Lua par onglet (spec §3.1-3) : le port 0 d'un script est la liaison de son onglet, pour toute l'exécution ; deux scripts en parallèle ;
# fermer un onglet pendant son script ne touche pas l'autre.
import os, sys, time
from gi.repository import GLib
from gtktest import *

ECHO = 'while true do local d = gtkterm.wait_for(0, "PING", 100) if d then gtkterm.send(0, "%s") end end\n'

e = Env('onglets_lua')
e.write_config(e.general() + e.profile())
e.start()
e.set_state('show-script-panel', GLib.Variant('b', True))
check(wait_until(lambda: e.find(name='script-editor')), 'panneau de script du premier onglet')
e.set_text('script-editor', ECHO % 'PONG1')
e.activate('script-run')
p2 = e.new_pty()
open_tab(e, p2.port)
check(wait_until(lambda: e.find(name='script-editor')), 'panneau de script du second onglet (visibilite copiee)')
check((e.find1(name='script-editor').get('label') or '') != ECHO % 'PONG1', 'editeur du second onglet independant')
e.set_text('script-editor', ECHO % 'PONG2')
e.activate('script-run')
check(wait_until(lambda: all(s['busy'] for s in e.sessions())), 'deux scripts en cours')

e.drain()
p2.drain()
e.write_pty(b'PING')
check(b'PONG1' in e.read_pty(1.5), 'script du premier onglet repond sur son port, second onglet affiche')
check(b'PONG' not in p2.read(0.3), 'rien sur le second port')
p2.write(b'PING')
check(b'PONG2' in p2.read(1.5), 'script du second onglet repond sur son port')

e.action('close-tab')	# second onglet, occupé : confirmation
check(wait_until(lambda: e.alerts(), 3), 'confirmation de fermeture')
e.close_alert('Close tab')
check(wait_until(lambda: len(e.sessions()) == 1, 6), 'second onglet ferme pendant son script')
e.drain()
e.write_pty(b'PING')
check(b'PONG1' in e.read_pty(1.5), 'le script du premier onglet tourne toujours')
e.finish()
sys.exit(conclude())
