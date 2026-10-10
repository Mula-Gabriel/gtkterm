# Ctrl+B tapé dans le terminal part sur le port (0x02, STX) : aucun raccourci de l'application ne l'intercepte (spec §3.2-8 : l'alias
# Ctrl+B de « Send break » est retiré ; Send break reste sur Ctrl+Maj+B). « b » seul n'envoie que « b » (pas de Contrôle parasite).
from gtktest import *

e = Env('clavier_ctrl_b')
e.write_config(e.general() + e.profile())
e.start()
e.bw.click(e.find1(name='terminal'))
e.drain(0.2)
e.bw.key('b', ('ctrl',))
check(e.read_pty(0.8) == b'\x02', 'Ctrl+B envoie 0x02 sur le port')
e.bw.key('b')
check(e.read_pty(0.8) == b'b', 'b seul envoie b')
check(e.proc.poll() is None, 'application toujours ouverte')
e.finish()
sys.exit(conclude())
