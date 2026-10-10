# Horodatage activé après coup (app.timestamp) : affiché sur les lignes déjà reçues (rejeu de l'historique).
import re
import sys
from gi.repository import GLib
from gtktest import *


def texte():
    return e.terminal_text()


e = Env('horodatage')
e.write_config(e.general() + e.profile())
e.start(connect_browser=False)
e.write_pty(b'L1\r\nL2\r\n')
check(wait_until(lambda: 'L2' in texte(), 3), 'lignes recues')
check('[' not in texte(), 'sans horodatage au depart : %r' % texte()[:120])
e.set_state('timestamp', GLib.Variant('b', True))
for ligne in ('L1', 'L2'):
    motif = re.compile(r'^\[\d+\.\d\dh\.\d\dm\.\d\ds\.\d{3}\] %s$' % ligne, re.M)
    check(wait_until(lambda: motif.search(texte()), 3), 'horodatage retroactif sur %s : %r' % (ligne, texte()[:200]))
e.finish()
sys.exit(conclude())
