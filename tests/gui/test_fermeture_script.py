# Fermeture par le bouton « Close » de la barre de titre pendant qu'un script Lua tourne, qu'une macro est en polling et que des
# données arrivent : la fin de main() arrête le script sans qu'aucun rappel (réception, état de la liaison, voyants, polling,
# clignotement, journal du script) ne touche les widgets détruits.
import threading
from gtktest import *

e = Env('fermeture_script')
ma = os.path.join(e.root, 'macros.ini')
e.write_macros(ma, [('PollA', 'None', '<A1>', 'T1', '', 1, 50)])
e.write_rc(e.profile(macros_file=ma))
e.start(); time.sleep(2)
frame = find1(e.app, 'frame')
check(status_text(e.app).startswith(e.pty), 'port ouvert au demarrage', status_text(e.app))

menu(e.app, 'View', 'Script panel'); time.sleep(1)


def est_editeur(n):
    return (get_text(n) or '').startswith('-- GTKTerm Lua script')


editeur = find1(e.app, 'text', pred=est_editeur)
# Le script ouvre un port Lua puis reste dans un appel C non interruptible (os.execute) : à la fermeture (~2,5 s après le lancement),
# GTKTerm attend sa fin (~1,5 s) ; la boucle qui suit s'arrête au crochet. (Fermeture des ports Lua et restauration des
# paramètres : vérifiées par /script_engine/shutdown-pendant-boucle, le processus qui sort ici les fermerait de toute façon.)
set_text(editeur, "assert(gtkterm.open(1, 'tcp_server', 0))\ngtkterm.log('tick')\nos.execute('sleep 4')\nwhile true do end\n"); time.sleep(0.5)
act(find1(e.app, 'push button', '^Run$', showing=True)); time.sleep(1.2)
check(find(e.app, 'text', pred=lambda n: 'tick' in (get_text(n) or '')) != [], 'script en cours (console)')
act(macro_button(e.app, '⏱ PollA')); time.sleep(0.5)
d = e.read_pty(0.5)
check(d.count(b'<A1>') >= 3, 'polling actif', d)

# Données reçues en continu pendant la fermeture
arret = threading.Event()


def emettre():
    while not arret.is_set():
        try:
            e.write_pty(b'donnees recues pendant la fermeture\r\n')
        except OSError:
            pass
        e.read_pty(0.01)


t = threading.Thread(target=emettre, daemon=True)
t.start()
time.sleep(0.3)
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
