# Raccourcis de macros (porté de l'ancienne suite GTK 3 et des anciens cas d'affichage de test_macros_send.c) : la macro part par la
# liaison et la touche n'atteint pas le terminal (M43) ; toujours un seul envoi après deux remplacements du jeu (une entrée par macro,
# M17) ; « Macro "…" sent! » dans la barre d'état.
import os, re, time
from gtktest import *

e = Env('raccourcis_macros')
ma = e.macros_path
write_macros_v2(ma, [dict(label='M1', shortcut='<Control>F5', action='<M1>'), dict(label='M2', shortcut='<Control>F6', action='<M2>')])
macro_config(e, ma)
e.start()
ancien = macro_id(e, 'M1')  # les identifiants ne sont jamais réutilisés : un jeu remplacé se voit à l'identifiant


def touches(etape):
    e.bw.click(e.find1(name='terminal'))
    for touche, attendu in (('F5', b'<M1>'), ('F6', b'<M2>')):
        e.drain()
        e.bw.key(touche, mods=('ctrl',))
        d = e.read_pty(0.8)
        check(d == attendu, '%s : Ctrl+%s envoie %r seul (%r)' % (etape, touche, attendu, d))


touches('demarrage')
e.bw.key('F5', mods=('ctrl',))
check(wait_until(lambda: any('Macro "M1" sent!' in (w.get('label') or '') for w in e.find(name='status-flash')), 0.7),
      'barre d\'etat : Macro "M1" sent!')
for i in (1, 2):
    e.action('load-macros')
    file_dialog(e, 'Load macros file', ma)
    check(wait_until(lambda: macro_buttons(e).get('M1') not in (None, ancien)), 'rechargement %d : jeu remplace, panneau reconstruit' % i)
    ancien = macro_buttons(e).get('M1', ancien)
touches('apres deux rechargements')
e.finish()
sys.exit(conclude())
