# Capture d'un raccourci dans « Configure Macros » (§6.5, §3.2-8) : Ctrl+Maj+Q refusé (pris par app.quit ; l'application ne quitte pas),
# raccourci d'une autre macro refusé, touche de caractère seule refusée, Échap annule la capture sans fermer la fenêtre ; Ctrl+F8 accepté
# puis retiré par « Delete Shortcut » ; l'aide décrit arguments, listes, séparateurs, raccourcis et polling.
import os, re, time
from gtktest import *

e = Env('macro_editor_raccourci')
ma = e.macros_path
write_macros_v2(ma, [dict(label='A', shortcut='<Control>F5', action='<A>'), dict(label='B', action='<B>')])
macro_config(e, ma)
e.start()
macro_id(e, 'B')
open_editor(e)
check([r['shortcut'] for r in editor_rows(e)] == ['<Control>F5', 'None'], 'raccourcis initiaux %r' % [r['shortcut'] for r in editor_rows(e)])


def message():
    w = e.find(name='macro-editor-error')
    return (w[0].get('label') or '', classes(w[0])) if w else ('', [])


editor_select(e, 1)
e.activate('macro-capture')
# test-activate est synchrone (« clicked » émis avant la réponse D-Bus) : la consigne est lue sans attente
check('Escape' in message()[0] and 'error' not in message()[1], 'test-activate synchrone : consigne affichee des le retour de l appel %r' % (message(),))

e.bw.key('q', mods=('ctrl', 'shift'))
check(wait_until(lambda: 'quit' in message()[0] and 'error' in message()[1]), 'Ctrl+Maj+Q refuse (app.quit) : %r' % (message(),))
check(editor_rows(e)[1]['shortcut'] == 'None', 'raccourci de B inchange apres le refus')
check(bool(e.find(name='main-window')) and bool(e.find(name='macro-editor')), 'l\'application n\'a pas quitte')

e.bw.key('F5', mods=('ctrl',))
check(wait_until(lambda: '"A"' in message()[0] and 'error' in message()[1]), 'Ctrl+F5 refuse (macro A) : %r' % (message(),))

e.bw.key('b')
check(wait_until(lambda: 'Ctrl' in message()[0] and 'error' in message()[1]), 'touche b seule refusee : %r' % (message(),))
check(editor_rows(e)[1]['shortcut'] == 'None', 'raccourci de B toujours vide')

e.bw.key('Escape')
check(wait_until(lambda: message()[0] == ''), 'Echap : capture annulee, message efface')
e.bw.key('F8', mods=('ctrl',))
time.sleep(0.3)
check(editor_rows(e)[1]['shortcut'] == 'None', 'apres Echap, Ctrl+F8 n\'est pas capture')
check(bool(e.find(name='macro-editor')), 'Echap n\'a pas ferme la fenetre')

e.activate('macro-capture')
check('Escape' in message()[0] and 'error' not in message()[1], 'nouvelle capture : consigne affichee %r' % (message(),))
e.bw.key('F8', mods=('ctrl',))
check(wait_until(lambda: editor_rows(e)[1]['shortcut'] in ('<Control>F8', '<Primary>F8')), 'Ctrl+F8 accepte : %r' % editor_rows(e)[1]['shortcut'])
check(message()[0] == '', 'message efface apres acceptation')
e.activate('macro-unshortcut')
check(wait_until(lambda: editor_rows(e)[1]['shortcut'] == 'None'), 'Delete Shortcut retire le raccourci de B')

e.activate('macro-help')
check(wait_until(lambda: any('%#' in t and 'Polling' in t and 'Escape' in t for t in e.alerts())), 'aide complete : %r' % e.alerts())
e.close_alert('OK')
e.activate('macro-ok')
check(wait_until(lambda: not e.find(name='macro-editor')), 'OK ferme la fenetre')
texte = open(ma).read()
check('F8' not in texte and 'shortcut=<Control>F5' in texte, 'fichier : A garde Ctrl+F5, B sans raccourci')
e.finish()
sys.exit(conclude())
