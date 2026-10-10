# Fermeture (portage GTK 4) pendant qu'un script Lua tourne (port Lua TCP, appel C non interruptible), qu'une macro est en polling et que des
# données arrivent : arrêt dans l'ordre de close-request (moteur, ordonnanceur, liaison), code 0, aucun rappel sur un widget détruit.
import os
import sys
import threading
import time
from gi.repository import GLib
from gtktest import *

e = Env('fermeture_script')
macros = os.path.join(e.root, 'macros.ini')
write_macros_v2(macros, [dict(label='PollA', action='<A1>', tab='T1', polling=True, period=50)])
e.write_config(e.general() + e.profile(macros_file=macros))
e.start()
e.find1(name='terminal', timeout=10)
check(wait_until(lambda: e.status_text().startswith(e.port), 5), 'port ouvert au demarrage (%r)' % e.status_text())

e.set_state('show-script-panel', GLib.Variant('b', True))
e.find1(name='script-editor', timeout=5)
# Le script ouvre un port Lua puis reste dans un appel C non interruptible (os.execute) : à la fermeture (~2,5 s après le lancement),
# GTKTerm attend sa fin (~1,5 s) ; la boucle qui suit s'arrête au crochet.
e.set_text('script-editor', "assert(gtkterm.open(1, 'tcp_server', 0))\ngtkterm.log('tick')\nos.execute('sleep 4')\nwhile true do end\n")
time.sleep(0.3)
e.activate('script-run')
check(wait_until(lambda: 'tick' in (e.find1(name='script-console').get('label') or ''), 3), 'script en cours (console)')
e.activate('macro-%d' % macro_id(e, '⏱ PollA'))	# bouton principal d'une macro en mode polling : « ⏱ <libellé> »
time.sleep(0.5)
d = e.read_pty(0.5)
check(d.count(b'<A1>') >= 3, 'polling actif (%r)' % d[:80])

arret = threading.Event()


def emettre():
    while not arret.is_set():
        try:
            e.write_pty(b'donnees recues pendant la fermeture\r\n')
            e.read_pty(0.01)
        except (OSError, ValueError):
            time.sleep(0.01)


t = threading.Thread(target=emettre, daemon=True)
t.start()
time.sleep(0.3)
e.finish()
arret.set()
t.join(2)
sys.exit(conclude())
