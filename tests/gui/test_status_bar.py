# Barre d'état (spec §6.3) : texte lié à la liaison, message temporaire qui remplace puis rend le texte d'état ; voyants DTR…RI et
# Send break / DTR / RTS seulement sur une liaison série ouverte (absents en client TCP vers 127.0.0.1, présents sur une pty).
import socket
import sys
from gtktest import *

VOYANTS = ['led-%s' % n for n in ('DTR', 'RTS', 'CTS', 'CD', 'DSR', 'RI')]
SIGNAUX = ('send-break', 'toggle-dtr', 'toggle-rts')


def flash(e):
    w = e.find(name='status-flash')
    return w[0].get('label') if w else None


def voyants_visibles(e):
    return [n for n in VOYANTS if e.find(name=n)]


# 1. Client TCP vers un serveur local
srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(('127.0.0.1', 0))
srv.listen(1)
srv.settimeout(10)
tcp = srv.getsockname()[1]
e = Env('status_bar_tcp')
e.write_config(e.general() + e.profile(transport='tcp-client', socket_host='127.0.0.1', socket_port=str(tcp)))
e.start(port=False, connect_browser=False)
conn, _ = srv.accept()
check(wait_until(lambda: e.status_text() == 'tcp://127.0.0.1:%d' % tcp, 5), 'client TCP connecte : %r' % e.status_text())
check(voyants_visibles(e) == [], 'aucun voyant en TCP : %r' % voyants_visibles(e))
for nom in SIGNAUX:
    check(not e.describe(nom)[0], '%s inactif en TCP' % nom)
e.finish()
conn.close()
srv.close()

# 2. Série (pty)
e = Env('status_bar_serie')
e.write_config(e.general() + e.profile())
e.start(connect_browser=False)
base = '%s  115200-8-N-1' % e.port
check(wait_until(lambda: e.status_text() == base, 5), 'port serie ouvert : %r' % e.status_text())
check(wait_until(lambda: voyants_visibles(e) == VOYANTS, 3), 'six voyants visibles en serie : %r' % voyants_visibles(e))
for nom in SIGNAUX:
    check(e.describe(nom)[0], '%s actif en serie' % nom)
leds = [w for n in VOYANTS for w in e.find(name=n)]
check(len(leds) == len(VOYANTS), 'les six voyants sont dans le dump : %d' % len(leds))
check(leds and all(not w.get('sensitive') for w in leds), 'pty sans lignes modem : voyants eteints')

# message temporaire : remplace le texte d'état, puis le rend
e.action('send-break')
check(wait_until(lambda: flash(e) == 'Break signal sent!' and not e.find(name='status-text'), 2), 'message temporaire a la place de l etat')
check(wait_until(lambda: flash(e) is None and e.find(name='status-text'), 3), 'etat rendu apres le message')
check(e.status_text() == base, 'texte d etat inchange : %r' % e.status_text())

# fermeture puis réouverture du port
e.action('close-port')
check(wait_until(lambda: e.status_text() == 'No open port', 3), 'port ferme : %r' % e.status_text())
check(wait_until(lambda: voyants_visibles(e) == [], 3), 'voyants caches port ferme')
check(not e.describe('send-break')[0], 'Send break inactif port ferme')
e.action('open-port')
check(wait_until(lambda: e.status_text() == base and voyants_visibles(e) == VOYANTS, 5), 'port rouvert : voyants revenus')
e.finish()

# 3. Série (pty) avec lignes modem simulées par le garde-fou (TIOCMGET) : DTR, CTS et DSR actives, seulement ces voyants allumés
e = Env('status_bar_modem')
e.write_config(e.general() + e.profile())
e.start(connect_browser=False, extra_env={'GTKTERM_GUARD_FAKE_MODEM': '0x122'})
check(wait_until(lambda: voyants_visibles(e) == VOYANTS, 5), 'six voyants visibles (lignes simulees)')


def allumes():
    return [n for n in VOYANTS for w in e.find(name=n) if w.get('sensitive')]


check(wait_until(lambda: allumes() == ['led-DTR', 'led-CTS', 'led-DSR'], 3), 'seuls DTR, CTS et DSR allumes : %r' % allumes())
e.finish()
sys.exit(conclude())
