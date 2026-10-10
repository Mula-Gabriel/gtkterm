# SIGUSR2 ferme le port de tous les onglets, SIGUSR1 les rouvre (spec §3.2-15).
import os, signal, sys
from gtktest import *

e = Env('onglets_sigusr')
e.write_config(e.general() + e.profile())
e.start()
p2 = e.new_pty()
open_tab(e, p2.port)
check(wait_until(lambda: all(s['state'] == 'open' for s in e.sessions())), 'deux ports ouverts')
os.kill(e.proc.pid, signal.SIGUSR2)
check(wait_until(lambda: all(s['state'] == 'closed' for s in e.sessions())), 'SIGUSR2 : tous les ports fermes')
os.kill(e.proc.pid, signal.SIGUSR1)
check(wait_until(lambda: all(s['state'] == 'open' for s in e.sessions())), 'SIGUSR1 : tous les ports rouverts')
e.finish()
sys.exit(conclude())
