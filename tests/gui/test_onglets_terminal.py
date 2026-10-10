# Dialogue Terminal lié à sa session (spec §3.2-12) : ouvert sur l'onglet 1, il règle toujours l'onglet 1 même quand l'onglet 2 devient actif ;
# l'onglet 2 ne change pas. Fermer l'onglet 1 ferme le dialogue sans rien appliquer ni ouvrir de boîte.
from gi.repository import GLib
from gtktest import *


def props():
    return e.terminal_props()


def sur_onglet(i):
    e.set_state('active-tab', GLib.Variant('i', i))
    check(wait_until(lambda: e.active_session()['index'] == i), 'onglet %d actif' % (i + 1))
    return props()


e = Env('onglets_terminal')
e.write_config(e.general() + e.profile(term_scrollback=10000))
e.start()
p2 = e.new_pty()
open_tab(e, p2.port)
nom2 = os.path.basename(p2.port)
avant2 = sur_onglet(1)
avant1 = sur_onglet(0)
check(avant1.get('scrollback') == 10000 and avant2.get('scrollback') == 10000, 'defilement du profil dans les deux onglets', (avant1, avant2))

# Dialogue ouvert sur l'onglet 1, puis l'onglet 2 devient actif : les réglages vont à l'onglet 1
e.action('terminal')
e.find1(name='terminal-dialog')
sur_onglet(1)
e.set_text('terminal-scrollback', '500')
e.set_text('terminal-fg', 'rgb(255,0,0)')
time.sleep(0.5)	# vérification négative : laisse le temps à une application à tort sur l'onglet 2 de se produire
apres2 = props()
check(apres2.get('scrollback') == 10000, 'onglet 2 : defilement inchange', apres2)
check(apres2.get('foreground') == avant2.get('foreground'), 'onglet 2 : couleur du texte inchangee', apres2)
apres1 = sur_onglet(0)
check(apres1.get('scrollback') == 500, 'onglet 1 : defilement du dialogue applique', apres1)
check(apres1.get('foreground') == 'rgb(255,0,0)', 'onglet 1 : couleur du texte du dialogue appliquee', apres1)

# Fermer l'onglet 1 (bouton de son étiquette, onglet 2 actif) : le dialogue se ferme, rien n'est appliqué, aucune boîte
sur_onglet(1)
fermetures = sorted(e.find(name='tab-close'), key=lambda w: w['x'])
check(len(fermetures) == 2, 'deux boutons de fermeture d onglet', fermetures)
e.activate(fermetures[0]['id'])
check(wait_until(lambda: len(e.sessions()) == 1, 5), 'onglet 1 ferme')
check(e.sessions()[0]['name'] == nom2, 'il reste l onglet 2', e.sessions())
check(wait_until(lambda: not e.find(name='terminal-dialog'), 3), 'le dialogue Terminal se ferme avec son onglet')
fin2 = props()
check(fin2.get('scrollback') == 10000 and fin2.get('foreground') == avant2.get('foreground'), 'onglet 2 toujours inchange', fin2)
time.sleep(0.5)	# vérification négative : laisse le temps à une boîte de message éventuelle d'apparaître
check(not e.alerts(), 'aucune boite', e.alerts())
e.finish()
sys.exit(conclude())
