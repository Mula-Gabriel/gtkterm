# Fenêtre « Configure Macros » (porté de l'ancienne suite GTK 3) : supprimer, déplacer, capturer un raccourci, le supprimer ;
# arguments et polling suivent leur macro, jamais la position (M16) ; une action avec « :: » et « | » survit à l'enregistrement (M21) ;
# un changement de jeu pendant l'édition ferme la fenêtre sans rien enregistrer (anciens cas d'affichage GTK 3 de test_macros_send.c).
# Lignes lues dans le dump (editor_rows), sélection par un vrai clic sur la cellule « Shortcut », boutons par test-activate.
import os, re, time
from gtktest import *

e = Env('macros_edition')
ma = e.macros_path
write_macros_v2(ma, [dict(label='A', action='<A>', polling=True, period=200),
                     dict(label='B', action='<B%d::x|y>', args=['5']),
                     dict(label='C', action='<C%d>', args=['7'])])
macro_config(e, ma)
e.start()


def envoi(libelle, attendu):
    e.drain()
    e.activate('macro-%d' % macro_id(e, libelle))
    d = e.read_pty(0.6)
    check(d == attendu, 'clic %s envoie %r (%r)' % (libelle, attendu, d))


def libelles():
    return [r['label'] for r in editor_rows(e)]


def valider():
    e.activate('macro-ok')
    check(wait_until(lambda: not e.find(name='macro-editor')), 'fenetre d\'edition fermee par OK')


envoi('B', b'<B5::x|y>')
envoi('C', b'<C7>')
e.activate('macro-%d' % macro_id(e, '⏱ A'))
check(e.read_pty(0.8).count(b'<A>') >= 2, 'polling de A actif')

# 1. Supprimer A : son polling s'arrête, aucune macro n'en hérite, C garde son argument (M16)
open_editor(e)
check(libelles() == ['A', 'B', 'C'], 'liste initiale %r' % libelles())
editor_select(e, 0)
e.activate('macro-delete')
check(wait_until(lambda: libelles() == ['B', 'C']), 'A retiree de la liste')
valider()
e.drain()
check(e.read_pty(0.8) == b'', 'polling de la macro supprimee arrete')
check(not [l for l in macro_buttons(e) if (l or '').startswith('⏱')], 'aucune macro en mode polling')
envoi('C', b'<C7>')
envoi('B', b'<B5::x|y>')

# 2. Remonter C au-dessus de B : ordre changé dans le fichier, arguments qui suivent ; une seconde demande présente la même fenêtre (instance unique)
open_editor(e)
e.action('macros')                     # synchrone : une seconde fenêtre serait déjà présentée au retour
e.after_frame()
check(len([w for w in e.windows() if w.get('title') == 'Configure Macros']) == 1, 'fenetre d\'edition a instance unique')
editor_select(e, 1)
e.activate('macro-up')
check(wait_until(lambda: libelles() == ['C', 'B']), 'C remontee')
check(editor_rows(e)[0]['selected'], 'la selection suit la ligne deplacee')
valider()
texte = open(ma).read()
check('[gtkterm-macros]' in texte and texte.find('label=C') < texte.find('label=B'), 'ordre C puis B enregistre au format v2')
check('action=<B%d::x|y>' in texte and 'args=5;' in texte, 'action avec :: et | enregistree telle quelle')
envoi('C', b'<C7>')
envoi('B', b'<B5::x|y>')

# 3. Capturer Ctrl+F8 pour B
open_editor(e)
editor_select(e, 1)
e.activate('macro-capture')            # synchrone : la consigne est déjà affichée au retour
check('Escape' in (e.find1(name='macro-editor-error').get('label') or ''), 'capture en cours')
e.bw.key('F8', mods=('ctrl',))
check(wait_until(lambda: 'F8' in editor_rows(e)[1]['shortcut']), 'Ctrl+F8 capture pour B')
valider()
check(re.search(r'shortcut=<(Primary|Control)>F8', open(ma).read()) is not None, 'raccourci capture enregistre')
e.bw.click(e.find1(name='terminal'))
e.drain()
e.bw.key('F8', mods=('ctrl',))
d = e.read_pty(0.8)
check(d == b'<B5::x|y>', 'Ctrl+F8 envoie la macro B (%r)' % d)

# 3 bis. Delete Shortcut : retiré du fichier, la touche ne déclenche plus B
open_editor(e)
editor_select(e, 1)
e.activate('macro-unshortcut')
check(wait_until(lambda: editor_rows(e)[1]['shortcut'] == 'None'), 'raccourci de B retire de la liste')
valider()
check('F8' not in open(ma).read(), 'raccourci supprime du fichier')
e.bw.click(e.find1(name='terminal'))
e.drain()
e.bw.key('F8', mods=('ctrl',))
d = e.read_pty(0.8)
check(b'<B' not in d, 'Ctrl+F8 ne declenche plus la macro B (%r)' % d)

# 4. Changement de jeu pendant l'édition : la fenêtre se ferme sans rien enregistrer
open_editor(e)
editor_select(e, 0)
e.activate('macro-delete')
avant = open(ma).read()
e.action('load-macros')
file_dialog(e, 'Load macros file', ma)
check(wait_until(lambda: not e.find(name='macro-editor')), 'fenetre d\'edition fermee au changement de jeu')
check(open(ma).read() == avant, 'rien d\'enregistre par la fenetre fermee')
envoi('C', b'<C7>')
envoi('B', b'<B5::x|y>')
e.finish()
sys.exit(conclude())
