# M43, côté non envoyable : le raccourci d'une macro sans action ou d'un séparateur n'est pas consommé ; la touche continue vers le
# terminal, qui l'envoie sur le port sous sa forme de séquence (Ctrl+F5 = ESC [15;5~, Ctrl+F7 = ESC [18;5~). Une macro envoyable
# consomme la sienne : seuls ses octets partent.
import os, re, time
from gtktest import *

e = Env('raccourci_non_envoyable')
ma = e.macros_path
write_macros_v2(ma, [dict(label='Vide', shortcut='<Control>F5', action=''),
                     dict(label='Sep', shortcut='<Control>F7', action='%&'),
                     dict(label='Pleine', shortcut='<Control>F6', action='<P>')])
macro_config(e, ma)
e.start()
macro_id(e, 'Pleine')
e.bw.click(e.find1(name='terminal'))
for touche, sequence in (('F5', b'\x1b[15;5~'), ('F7', b'\x1b[18;5~')):
    e.drain()
    e.bw.key(touche, mods=('ctrl',))
    d = e.read_pty(0.8)
    check(d == sequence, 'Ctrl+%s (macro non envoyable) : la touche atteint le terminal (%r)' % (touche, d))
e.drain()
e.bw.key('F6', mods=('ctrl',))
d = e.read_pty(0.8)
check(d == b'<P>', 'Ctrl+F6 (macro envoyable) : seule la macro part (%r)' % d)
e.finish()
sys.exit(conclude())
