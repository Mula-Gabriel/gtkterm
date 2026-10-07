# --disable-port-lock (-L) survit au chargement d'un profil par -c (F2) : le port n'est pas verrouillé (flock).
import fcntl
from gtktest import *


def verrou_libre(e):
    try:
        fcntl.flock(e.slave, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        return False
    fcntl.flock(e.slave, fcntl.LOCK_UN)
    return True


# Un environnement (et un broadwayd) par lancement : broadwayd ne survit pas toujours à un client tué
# témoin : sans -L, le port est verrouillé
e = Env('config_verrou_temoin')
e.write_rc(e.profile() + e.profile(name='autre', speed=9600))
e.start(args=['-c', 'autre'], force_port=False); time.sleep(2)
check(wait_until(lambda: status_text(e.app) == '%s  9600-8-N-1' % e.pty, 5), 'profil autre charge (temoin)', status_text(e.app))
check(not verrou_libre(e), 'sans -L : port verrouille')
finish(e)
# -L avant -c : l'option n'est pas perdue au chargement du profil
e = Env('config_verrou')
e.write_rc(e.profile() + e.profile(name='autre', speed=9600))
e.start(args=['-L', '-c', 'autre'], force_port=False); time.sleep(2)
check(wait_until(lambda: status_text(e.app) == '%s  9600-8-N-1' % e.pty, 5), 'profil autre charge avec -L', status_text(e.app))
check(verrou_libre(e), '-L puis -c : port non verrouille')
finish(e)
sys.exit(conclude())
