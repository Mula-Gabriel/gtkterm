# Écho conservé à sa place au changement de vue, et rétroactif quand on rebascule l'option (app.local-echo, app.view-mode).
import sys
from gi.repository import GLib
from gtktest import *


def texte():
    return e.terminal_text()


def ordre_ok():
    t = texte()
    return 'RX1' in t and 'CMD' in t and t.index('RX1') < t.index('CMD')


e = Env('echo_vues')
e.write_config(e.general() + e.profile(echo='true'))
e.start()
e.write_pty(b'RX1\r\n')
check(wait_until(lambda: 'RX1' in texte(), 3), 'RX1 recu')
e.bw.click(e.find1(name='terminal'))
e.drain()
e.bw.type('CMD')
check(e.read_pty(1.0) == b'CMD', 'frappe envoyee au port')
check(wait_until(ordre_ok, 3), 'echo apres la reception : %r' % texte()[:200])
e.set_state('view-mode', GLib.Variant('s', 'hex'))
check(wait_until(lambda: '52 58 31' in texte(), 3), 'vue hexadecimale : %r' % texte()[:200])
e.set_state('view-mode', GLib.Variant('s', 'ascii'))
check(wait_until(ordre_ok, 3), 'echo conserve a sa place apres ASCII -> hexa -> ASCII : %r' % texte()[:200])
e.write_pty(b'RX2\r\n')
check(wait_until(lambda: texte().find('RX2') > texte().find('CMD') >= 0, 3), 'reception suivante apres l echo')
e.set_state('local-echo', GLib.Variant('b', False))
check(wait_until(lambda: 'CMD' not in texte() and 'RX1' in texte() and 'RX2' in texte(), 3), 'echo desactive : envoi masque, reception gardee')
e.set_state('local-echo', GLib.Variant('b', True))
check(wait_until(ordre_ok, 3), 'echo reactive : envoi reaffiche a sa place : %r' % texte()[:200])
e.finish()
sys.exit(conclude())
