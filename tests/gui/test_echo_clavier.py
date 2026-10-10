# Frappe clavier : envoyée au port ; avec l'écho (profil puis app.local-echo), répétée dans la ligne, sans ligne « > » ; sans écho, non affichée.
import sys
from gi.repository import GLib
from gtktest import *


def texte():
    return e.terminal_text()


e = Env('echo_clavier')
e.write_config(e.general() + e.profile(echo='true'))
e.start()
check(e.state('local-echo') is True, 'echo du profil reporte sur app.local-echo')
e.write_pty(b'prompt> ')
check(wait_until(lambda: 'prompt> ' in texte(), 3), 'invite recue')
e.bw.click(e.find1(name='terminal'))
e.drain()
e.bw.type('abc')
check(e.read_pty(0.8) == b'abc', 'frappe envoyee au port')
check(wait_until(lambda: 'prompt> abc' in texte(), 3), 'frappe repetee dans la ligne : %r' % texte()[:120])
check('> a' not in texte().replace('prompt> abc', ''), 'pas de ligne > pour la frappe')
e.set_state('local-echo', GLib.Variant('b', False))
check(wait_until(lambda: texte().strip() == 'prompt>', 3), 'echo retire : frappe masquee, reception gardee : %r' % texte()[:120])
e.bw.type('d')
check(e.read_pty(0.8) == b'd', 'frappe envoyee sans echo')
check(texte().strip() == 'prompt>', 'frappe non affichee sans echo : %r' % texte()[:120])
e.finish()
sys.exit(conclude())
