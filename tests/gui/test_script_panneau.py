# Hauteur du panneau de script (tour de correctifs de la tâche 11 de 6c) : affiché pendant que GTKTerm tourne, sans [window] v_paned_position,
# le panneau reçoit une hauteur utile (l'éditeur reste dessiné, plus de 100 px) ; masqué puis réaffiché, de même ; une position enregistrée
# valable ([window] v_paned_position) est respectée telle quelle.
import sys, time
from gi.repository import GLib
from gtktest import *


def editeur(e):
    return e.find(name='script-editor')


def stable(e, quoi):
    """Panneau affiché depuis 1,5 s : éditeur toujours mappé, plus de 100 px de haut."""
    time.sleep(1.5)
    ed = editeur(e)
    check(len(ed) == 1 and ed[0].get('h', 0) > 100, '%s : editeur mappe et haut de plus de 100 px (%r)'
          % (quoi, [(w['mapped'], w.get('h')) for w in e.find(name='script-editor', mapped=None)]))


# 1. Sans position enregistrée : affiché à l'exécution, masqué puis réaffiché
e = Env('script_panneau')
e.write_config(e.general() + e.profile())
e.start()
e.find1(name='terminal', timeout=10)
e.set_state('show-script-panel', GLib.Variant('b', True))
check(wait_until(lambda: e.state('show-script-panel') is True, 3), 'panneau de script affiche')
stable(e, 'premier affichage')
e.set_state('show-script-panel', GLib.Variant('b', False))
check(wait_until(lambda: not editeur(e), 3), 'panneau masque')
e.set_state('show-script-panel', GLib.Variant('b', True))
stable(e, 'reaffichage')
e.finish()

# 2. Position enregistrée valable : respectée (bord haut du panneau à v_paned_position sous le haut de la page)
e = Env('script_panneau_position')
e.write_config(e.general() + e.profile() + '[window]\nscript_panel_visible=true\nv_paned_position=200\n')
e.start()
page = e.find1(name='session-view', timeout=10)
panneau = e.find1(name='script-panel')
check(abs(panneau['y'] - page['y'] - 200) <= 3, 'position enregistree respectee (panneau a %d px du haut de la page)' % (panneau['y'] - page['y']))
stable(e, 'position enregistree')
e.finish()
sys.exit(conclude())
