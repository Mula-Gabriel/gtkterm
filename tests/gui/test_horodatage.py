# Horodatage activé après coup : affiché sur les lignes déjà reçues (rejeu de l'historique).
from gtktest import *

e = Env('horodatage')
e.write_rc(e.profile())
e.start(); time.sleep(2)
e.write_pty(b'L1\r\nL2\r\n'); time.sleep(0.8)
check('L1' in terminal_text(e.app) and '[' not in terminal_text(e.app), 'sans horodatage au depart', terminal_text(e.app)[:120])
menu(e.app, 'Configuration', 'Timestamp'); time.sleep(1.0)
for ligne in ('L1', 'L2'):
    motif = re.compile(r'^\[\d+\.\d\dh\.\d\dm\.\d\ds\.\d{3}\] %s$' % ligne, re.M)
    check(wait_until(lambda: motif.search(terminal_text(e.app)), 3), 'horodatage retroactif sur %s' % ligne, terminal_text(e.app)[:200])
finish(e)
sys.exit(conclude())
