# Fermeture par le bouton « Close » de la barre de titre pendant qu'un script Lua envoie une macro en boucle : la demande d'envoi
# en attente au moment de l'arrêt n'est plus exécutée (elle afficherait un message dans la barre d'état détruite).
import threading
from gtktest import *

e = Env('fermeture_script_macro')
ma = os.path.join(e.root, 'macros.ini')
e.write_macros(ma, [('Ping', 'None', '<P>', 'T1', '', 0, 1000)])
e.write_rc(e.profile(macros_file=ma))
e.start(); time.sleep(2)
frame = find1(e.app, 'frame')
check(status_text(e.app).startswith(e.pty), 'port ouvert au demarrage', status_text(e.app))

menu(e.app, 'View', 'Script panel'); time.sleep(1)
editeur = find1(e.app, 'text', pred=lambda n: (get_text(n) or '').startswith('-- GTKTerm Lua script'))
set_text(editeur, "gtkterm.log('debut')\nwhile true do gtkterm.send_macro('Ping') end\n"); time.sleep(0.5)

# Le pty est lu en continu : les envois ne se bloquent jamais faute de lecteur
arret = threading.Event()
recu = []


def lire():
    while not arret.is_set():
        recu.append(e.read_pty(0.05))


t = threading.Thread(target=lire, daemon=True)
t.start()
act(find1(e.app, 'push button', '^Run$', showing=True)); time.sleep(1.5)
check(b'<P>' in b''.join(recu), 'envois de la macro en cours')
act(find1(frame, 'push button', '^Close$', showing=True))
try:
    e.proc.wait(10)
except subprocess.TimeoutExpired:
    pass
arret.set()
t.join(2)
check(e.proc.poll() is not None, 'application terminee par le bouton Close')
check(e.proc.returncode == 0, 'code de sortie nul', e.proc.returncode)
check('still running' not in e.stderr(), 'thread Lua joint avant la sortie', e.stderr()[-400:])
finish(e, quit=False)
sys.exit(conclude())
