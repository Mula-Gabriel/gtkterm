# Actions app.* relayées vers l'onglet actif (spec §3.2-3) : écho, vue, état du port et sensibilités propres à chaque onglet ; Close port sur l'onglet
# actif seulement ; chaque terminal n'affiche que son port. Un éditeur « Configure Macros » par onglet ; fermer un onglet ferme le sien sans rien
# appliquer.
import os, sys, time
from gi.repository import GLib
from gtktest import *

e = Env('onglets_actions')
write_macros_v2(e.macros_path, [dict(label='M1', shortcut='', action='<M1>')])
macro_config(e, e.macros_path)
e.start()
p2 = e.new_pty()
open_tab(e, p2.port)
e.set_state('local-echo', GLib.Variant('b', True))
e.set_state('view-mode', GLib.Variant('s', 'hex'))
check(e.state('local-echo') is True and e.state('view-mode') == 'hex', 'second onglet : echo et vue hexadecimale')
e.set_state('active-tab', GLib.Variant('i', 0))
check(wait_until(lambda: e.state('local-echo') is False), 'premier onglet : echo inchange')
check(e.state('view-mode') == 'ascii', 'premier onglet : vue ASCII inchangee')
e.set_state('active-tab', GLib.Variant('i', 1))
check(wait_until(lambda: e.state('local-echo') is True and e.state('view-mode') == 'hex'), 'second onglet : options retrouvees')

e.write_pty(b'UN\r\n')
p2.write(b'DEUX\r\n')
check(wait_until(lambda: '44 45 55 58' in e.terminal_text()), 'second onglet affiche son port (hexa)')
check('55 4e' not in e.terminal_text().lower(), 'second onglet n affiche pas le port du premier')
e.set_state('active-tab', GLib.Variant('i', 0))
check(wait_until(lambda: 'UN' in e.terminal_text() and 'DEUX' not in e.terminal_text()), 'premier onglet : seulement son port')

e.set_state('active-tab', GLib.Variant('i', 1))
e.action('close-port')
check(wait_until(lambda: e.sessions()[1]['state'] == 'closed'), 'Close port ferme le port de l onglet actif')
check(e.sessions()[0]['state'] == 'open', 'le port de l autre onglet reste ouvert')
check(wait_until(lambda: not e.describe('send-break')[0]), 'send-break inactive : onglet actif ferme')
e.set_state('active-tab', GLib.Variant('i', 0))
check(wait_until(lambda: e.describe('send-break')[0]), 'send-break active : onglet actif ouvert en serie')

# Un éditeur de macros par onglet : deux fenêtres ; fermer un onglet ferme son éditeur sans rien appliquer
titre1 = 'Configure Macros — ' + os.path.basename(e.port)
titre2 = 'Configure Macros — ' + os.path.basename(p2.port)
e.action('macros')
e.wait_window(titre1)
e.set_state('active-tab', GLib.Variant('i', 1))
check(wait_until(lambda: e.active_session()['index'] == 1), 'second onglet actif')
e.action('macros')
e.wait_window(titre2)
check(len([w for w in e.windows() if (w.get('title') or '').startswith('Configure Macros')]) == 2, 'deux editeurs ouverts, un par onglet')
avant = open(e.macros_path).read()
mtime = os.stat(e.macros_path).st_mtime_ns
d = e.dump()
win2 = [w for w in d if w['depth'] == 0 and w.get('title') == titre2][0]
ajout = [w for w in d if w['native'] == win2['native'] and w.get('name') == 'macro-add' and w['mapped']]
check(len(ajout) == 1, 'bouton Add de l editeur du second onglet')
e.activate(ajout[0]['id'])	# ligne ajoutée dans l'éditeur du second onglet, jamais appliquée
e.action('close-tab')
check(wait_until(lambda: len(e.sessions()) == 1, 5), 'second onglet ferme')
check(wait_until(lambda: not [w for w in e.windows() if w.get('title') == titre2], 3), 'son editeur ferme avec lui')
check([w for w in e.windows() if w.get('title') == titre1], 'l editeur du premier onglet reste ouvert')
time.sleep(0.5)
check(open(e.macros_path).read() == avant and os.stat(e.macros_path).st_mtime_ns == mtime, 'fichier de macros inchange (rien applique)')
check(not e.alerts(), 'aucune boite', e.alerts())
e.activate('macro-cancel')
check(wait_until(lambda: not [w for w in e.windows() if w.get('title') == titre1], 3), 'editeur du premier onglet ferme par Cancel')
e.finish()
sys.exit(conclude())
