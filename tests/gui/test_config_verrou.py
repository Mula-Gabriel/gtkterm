# --disable-port-lock (-L) survit au chargement d'un profil par -c (F2) : le port n'est pas verrouillé (flock).
import fcntl
import os
import sys
from gtktest import *


def verrou_libre(e):
    fd = os.open(e.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        return False
    finally:
        os.close(fd)
    return True


# témoin : sans -L, le port est verrouillé
e = Env('config_verrou_temoin')
e.write_config(e.general() + e.profile() + e.profile(name='autre', speed=9600))
e.start('-c', 'autre', port=False, connect_browser=False)
check(wait_until(lambda: e.status_text() == '%s  9600-8-N-1' % e.port, 5), 'profil autre charge (temoin) : %r' % e.status_text())
check(not verrou_libre(e), 'sans -L : port verrouille')
e.finish()
# -L avant -c : l'option n'est pas perdue au chargement du profil
e = Env('config_verrou')
e.write_config(e.general() + e.profile() + e.profile(name='autre', speed=9600))
e.start('-L', '-c', 'autre', port=False, connect_browser=False)
check(wait_until(lambda: e.status_text() == '%s  9600-8-N-1' % e.port, 5), 'profil autre charge avec -L : %r' % e.status_text())
check(verrou_libre(e), '-L puis -c : port non verrouille')
e.finish()
sys.exit(conclude())
