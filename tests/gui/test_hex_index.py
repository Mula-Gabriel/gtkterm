# Vue hexadécimale : l'écho a sa propre ligne et ne décale pas l'index des octets reçus (app.view-mode, app.hex-index).
import sys
from gi.repository import GLib
from gtktest import *


def texte():
    return e.terminal_text()


e = Env('hex_index')
# Fenêtre large : la ligne hexadécimale de 16 octets (index, octets, ASCII : 64 colonnes au moins) doit tenir sur la largeur du terminal,
# panneau de macros affiché. À 750 px, le panneau vide (116 px, sa largeur naturelle) laisse 633 px au terminal, soit 63 colonnes
# en Monospace 12 : la ligne s'y replie et l'écriture par morceaux de la vue hexadécimale s'y décale.
e.write_config(e.general() + e.profile(echo='true') + '[window]\nwidth=1000\nheight=550\n')
e.start()
e.set_state('view-mode', GLib.Variant('s', 'hex'))
e.set_state('hex-index', GLib.Variant('b', True))
e.write_pty(b'ABC')
check(wait_until(lambda: '     0: 41 42 43' in texte(), 3), 'premiere ligne recue, index 0 : %r' % texte()[:300])
e.bw.click(e.find1(name='terminal'))
e.drain()
e.bw.type('Z')
check(e.read_pty(1.0) == b'Z', 'frappe envoyee')
check(wait_until(lambda: '     >  5A' in texte(), 3), 'echo sur sa propre ligne : %r' % texte()[:300])
e.write_pty(b'DEF')
check(wait_until(lambda: '     3: 44 45 46' in texte(), 3), 'index des octets recus non decale par l echo : %r' % texte()[:300])
e.finish()
sys.exit(conclude())
