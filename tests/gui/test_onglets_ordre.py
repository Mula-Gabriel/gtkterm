# Ordre des onglets (plan, décision 5) : l'interface est maîtresse de l'ordre du GtkNotebook ; un glisser réel de l'étiquette d'un onglet
# (page-reordered) déplace la session dans le gestionnaire (test-sessions suit l'ordre des étiquettes), l'onglet actif reste le même.
from gi.repository import GLib
from gtktest import *


def etiquettes():
    """Noms courts des étiquettes d'onglet, de gauche à droite (géométrie de test-dump)."""
    return [w.get('label') for w in sorted(e.find(name='tab-name'), key=lambda w: w['x'])]


def noms():
    return [s['name'] for s in e.sessions()]


e = Env('onglets_ordre')
e.write_config(e.general() + e.profile())
e.start()
p2, p3 = e.new_pty(), e.new_pty()
open_tab(e, p2.port)
open_tab(e, p3.port)
n1, n2, n3 = (os.path.basename(p) for p in (e.port, p2.port, p3.port))
check(noms() == [n1, n2, n3], 'ordre initial du gestionnaire', noms())
check(wait_until(lambda: etiquettes() == [n1, n2, n3]), 'ordre initial des etiquettes', etiquettes())
e.set_state('active-tab', GLib.Variant('i', 0))
check(wait_until(lambda: e.active_session()['name'] == n1), 'premier onglet actif')

# Glisser l'étiquette du premier onglet au-delà de la dernière
tabs = sorted(e.find(name='tab-name'), key=lambda w: w['x'])
src = e.bw.settled(tabs[0])
dst = e.bw.settled(tabs[2])
sid = e.bw.surface_for(src)
y = src['y'] + src['h'] // 2
e.bw.drag_in_surface(sid, src['x'] + src['w'] // 2, y, dst['x'] + dst['w'] + 10, y)
check(wait_until(lambda: etiquettes() == [n2, n3, n1], 3), 'etiquettes reordonnees par le glisser', etiquettes())
check(wait_until(lambda: noms() == [n2, n3, n1], 3), 'le gestionnaire suit l ordre du notebook', e.sessions())
check([s['index'] for s in e.sessions()] == [0, 1, 2], 'index du gestionnaire dans l ordre', e.sessions())
check(e.active_session()['name'] == n1 and e.state('active-tab') == 2, 'l onglet deplace reste actif, a sa nouvelle position',
      (e.active_session(), e.state('active-tab')))

# Navigation après le déplacement : suit le nouvel ordre
e.action('next-tab')
check(wait_until(lambda: e.active_session()['name'] == n2), 'onglet suivant : premier de la barre (circulaire)', e.active_session())
check(not e.alerts(), 'aucune boite', e.alerts())
e.finish()
sys.exit(conclude())
