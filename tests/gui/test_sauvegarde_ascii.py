# Sauvegarde ASCII (fin de M35, spec §3.2-16) : TAB, CR, LF et octets >= 0x80 conservés, autres contrôles retirés, écho absent ; CR/LF auto lu au
# moment de l'enregistrement (option changée après la réception).
import os
import sys
import time
from gi.repository import GLib
from gtktest import *

e = Env('sauvegarde_ascii')
e.write_config(e.general() + e.profile(echo='true', crlfauto='false', timestamp='false'))
e.start()
term = e.find1(name='terminal', timeout=10)
e.drain()
e.write_pty(b'col1\tcol2\x07\x1b[0m fin\r\n\xc3\xa9t\xc3\xa9\n')
time.sleep(0.5)
e.bw.click(term)
e.bw.type('k')
check(e.read_pty(0.5) == b'k', 'frappe partie sur le port')

out1 = os.path.join(e.root, 'ascii.txt')
e.action('save-ascii')
file_dialog(e, 'Save ASCII File', out1)
check(wait_until(lambda: os.path.exists(out1), 3), 'fichier ASCII ecrit')
d1 = open(out1, 'rb').read() if os.path.exists(out1) else None
check(d1 == b'col1\tcol2[0m fin\r\n\xc3\xa9t\xc3\xa9\n', 'TAB/CR/LF/UTF-8 gardes, BEL et ESC retires, sans echo : %r' % (d1,))

e.set_state('crlf-auto', GLib.Variant('b', True))
check(wait_until(lambda: e.state('crlf-auto') is True, 2), 'CR/LF auto active')
out2 = os.path.join(e.root, 'ascii crlf.txt')
e.action('save-ascii')
file_dialog(e, 'Save ASCII File', out2)
check(wait_until(lambda: os.path.exists(out2), 3), 'second fichier ASCII ecrit')
d2 = open(out2, 'rb').read() if os.path.exists(out2) else None
check(d2 == b'col1\tcol2[0m fin\r\n\xc3\xa9t\xc3\xa9\r\n', 'CR/LF auto lu a l enregistrement : %r' % (d2,))
e.finish()
sys.exit(conclude())
