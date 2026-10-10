# Écho local d'une macro envoyée par le panneau : ligne « > <octets> » placée après la réception qui la précède, gardée à sa place au passage
# ASCII → hexa → ASCII, masquée quand l'écho est décoché puis réaffichée à sa place quand il est recoché (app.local-echo).
import os, re, time
from gi.repository import GLib
from gtktest import *

e = Env('echo_macro')
ma = e.macros_path
write_macros_v2(ma, [dict(label='CMD', action='CMD')])
e.write_config(e.general() + e.profile(echo='true', macros_file=ma))
e.start()
macro_id(e, 'CMD')


def ordre_ok():
    t = e.terminal_text()
    return 'RX1' in t and '> CMD' in t and t.index('RX1') < t.index('> CMD')


e.write_pty(b'RX1\r\n')
check(wait_until(lambda: 'RX1' in e.terminal_text(), 3), 'RX1 recu')
e.drain()
e.activate('macro-%d' % macro_id(e, 'CMD'))
check(e.read_pty(1.0) == b'CMD', 'macro envoyee au port')
check(wait_until(ordre_ok, 3), 'echo en bloc « > CMD » apres la reception', e.terminal_text()[:200])
e.set_state('view-mode', GLib.Variant('s', 'hex'))
time.sleep(0.5)
e.set_state('view-mode', GLib.Variant('s', 'ascii'))
check(wait_until(ordre_ok, 3), 'echo conserve a sa place apres ASCII -> hexa -> ASCII', e.terminal_text()[:200])
e.set_state('local-echo', GLib.Variant('b', False))
check(wait_until(lambda: '> CMD' not in e.terminal_text() and 'RX1' in e.terminal_text(), 3), 'echo desactive : envoi masque, reception gardee')
e.set_state('local-echo', GLib.Variant('b', True))
check(wait_until(ordre_ok, 3), 'echo reactive : envoi reaffiche a sa place', e.terminal_text()[:200])
e.finish()
sys.exit(conclude())
