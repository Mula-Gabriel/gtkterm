# Fermeture (portage GTK 4) pendant qu'un script Lua envoie une macro en boucle : la demande d'envoi en attente au moment de l'arrêt n'est plus
# exécutée (elle toucherait la barre d'état détruite) ; code 0, thread Lua joint (finish refuse « still running »).
import os
import sys
import threading
import time
from gi.repository import GLib
from gtktest import *

e = Env('fermeture_script_macro')
macros = os.path.join(e.root, 'macros.ini')
write_macros_v2(macros, [dict(label='Ping', action='<P>', tab='T1')])
e.write_config(e.general() + e.profile(macros_file=macros))
e.start()
e.find1(name='terminal', timeout=10)
check(wait_until(lambda: e.status_text().startswith(e.port), 5), 'port ouvert au demarrage (%r)' % e.status_text())
e.set_state('show-script-panel', GLib.Variant('b', True))
e.find1(name='script-editor', timeout=5)
e.set_text('script-editor', "gtkterm.log('debut')\nwhile true do gtkterm.send_macro('Ping') end\n")
time.sleep(0.3)

arret = threading.Event()
recu = []


def lire():
    while not arret.is_set():
        try:
            recu.append(e.read_pty(0.05))
        except (OSError, ValueError):
            time.sleep(0.01)


t = threading.Thread(target=lire, daemon=True)
t.start()
e.activate('script-run')
time.sleep(1.5)
check(b'<P>' in b''.join(recu), 'envois de la macro en cours')
e.finish()
arret.set()
t.join(2)
sys.exit(conclude())
