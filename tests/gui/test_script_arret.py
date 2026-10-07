# Arret d'un script Lua (sous-projet 5) : « Stop » interrompt une boucle de calcul pure (crochet Lua), la console affiche
# « [Script stopped] » sans ligne « Lua error » et « Run » redevient actif ; fermer la fenetre pendant une telle boucle termine
# GTKTerm (code 0) en moins de 10 s (arret du script a la destruction de la fenetre, puis script_engine_shutdown).
from gtktest import *

BOUCLE = 'while true do end\n'


def modifiable(n):
    return n.get_state_set().contains(Atspi.StateType.EDITABLE)


def console(app):
    """Texte des objets texte non modifiables et visibles (showing) : la console du panneau de scripts, pas l'editeur ni les objets caches."""
    return '\n'.join(get_text(t) or '' for t in find(app, 'text', pred=lambda n: not modifiable(n), showing=True))


def bouton(app, libelle):
    return find1(app, 'push button', '^%s$' % libelle, showing=True)


def sensible(obj):
    return obj.get_state_set().contains(Atspi.StateType.SENSITIVE)


e = Env('script_arret')
e.write_rc(e.profile())
e.start(); time.sleep(2)
frame = find1(e.app, 'frame')
menu(e.app, 'View', 'Script panel'); time.sleep(1.0)
try:
    ed = find1(e.app, 'text', pred=lambda n: (get_text(n) or '').startswith('-- GTKTerm Lua script'))
except LookupError:
    with open(os.path.join(e.root, 'arbre.txt'), 'w') as f:
        dump(e.app, out=f)
    raise
set_text(ed, BOUCLE); time.sleep(0.3)
check(get_text(ed) == BOUCLE, "script saisi dans l'editeur", get_text(ed))

run, stop = bouton(e.app, 'Run'), bouton(e.app, 'Stop')
act(run)
check(wait_until(lambda: sensible(stop) and not sensible(run), 5), 'script en cours : Stop actif, Run inactif')
time.sleep(1.0)
check(e.proc.poll() is None, 'GTKTerm toujours vivant pendant la boucle')
act(stop)
check(wait_until(lambda: '[Script stopped]' in console(e.app), 5), 'console : [Script stopped]', console(e.app)[-300:])
check(wait_until(lambda: sensible(run) and not sensible(stop), 5), 'Run de nouveau actif apres Stop')
# Relu apres un court sondage : une ligne d'erreur tardive (journal regroupe toutes les 20 ms) ne doit pas echapper au controle
time.sleep(0.6)
check('Lua error' not in console(e.app), "aucune ligne « Lua error » apres un arret", console(e.app)[-300:])

# Relance, puis fermeture de la fenetre par le bouton « Close » de la barre de titre pendant la boucle
act(run)
check(wait_until(lambda: sensible(stop), 5), 'script relance')
time.sleep(1.0)
check(sensible(stop), 'Stop toujours actif juste avant la fermeture (script en cours)')
t0 = time.time()
act(find1(frame, 'push button', '^Close$', showing=True))
try:
    e.proc.wait(10)
except subprocess.TimeoutExpired:
    pass
duree = time.time() - t0
check(e.proc.poll() is not None, 'GTKTerm termine apres la fermeture de la fenetre', '%.1f s' % duree)
check(e.proc.returncode == 0, 'code de sortie nul', e.proc.returncode)
check(duree < 10, 'fin en moins de 10 s', '%.1f s' % duree)
finish(e, quit=False)
sys.exit(conclude())
