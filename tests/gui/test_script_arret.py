# Arrêt d'un script Lua (portage GTK 4) : « Stop » interrompt une boucle de calcul pure, la console affiche « [Script stopped] » sans « Lua error »
# et « Run » redevient actif ; fermer GTKTerm pendant une telle boucle (app.quit, même chemin close-request que la barre de titre, spec §4.4) sort
# avec le code 0 en moins de 10 s.
import sys
import time
from gi.repository import GLib
from gtktest import *

BOUCLE = 'while true do end\n'


def widget(e, nom):
    return e.find1(name=nom)


def sensible(e, nom):
    return widget(e, nom)['sensitive']


def console(e):
    return widget(e, 'script-console').get('label') or ''


e = Env('script_arret')
e.write_config(e.general() + e.profile(echo='false'))
e.start()
e.find1(name='terminal', timeout=10)
e.set_state('show-script-panel', GLib.Variant('b', True))
check(wait_until(lambda: e.state('show-script-panel') is True and e.find(name='script-editor'), 5), 'panneau de script affiche')
check((widget(e, 'script-editor').get('label') or '').startswith('-- GTKTerm Lua script'), 'modele par defaut dans l editeur')
e.set_text('script-editor', BOUCLE)
check(wait_until(lambda: widget(e, 'script-editor').get('label') == BOUCLE, 3), "script saisi dans l'editeur")
check(wait_until(lambda: e.sessions()[0].get('script') == BOUCLE, 3), "texte de l'editeur tenu par la session (script-text)")
check(e.sessions()[0].get('in_project') is False, 'onglet libre : pas en projet')

check(sensible(e, 'script-run') and not sensible(e, 'script-stop'), 'au repos : Run actif, Stop inactif')
e.activate('script-run')
check(wait_until(lambda: sensible(e, 'script-stop') and not sensible(e, 'script-run'), 5), 'script en cours : Stop actif, Run inactif')
time.sleep(1.0)
check(e.find(name='main-window') != [], 'GTKTerm vivant pendant la boucle')
e.activate('script-stop')
check(wait_until(lambda: '[Script stopped]' in console(e), 5), 'console : [Script stopped] (%r)' % console(e)[-300:])
check(wait_until(lambda: sensible(e, 'script-run') and not sensible(e, 'script-stop'), 5), 'Run de nouveau actif apres Stop')
time.sleep(0.6)
check('Lua error' not in console(e), 'aucune ligne « Lua error » apres un arret (%r)' % console(e)[-300:])

e.activate('script-run')
check(wait_until(lambda: sensible(e, 'script-stop'), 5), 'script relance')
check(wait_until(lambda: '[Script stopped]' not in console(e), 2), 'console videe au lancement')
time.sleep(1.0)
check(sensible(e, 'script-stop'), 'Stop toujours actif juste avant la fermeture')
t0 = time.time()
e.finish()
duree = time.time() - t0
check(duree < 10, 'fin en moins de 10 s (%.1f s)' % duree)
sys.exit(conclude())
