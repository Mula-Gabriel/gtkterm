# Argument par défaut très long (200 caractères, montré en texte indicatif dans un champ vide) : la fenêtre garde sa taille par défaut (le
# panneau défile au lieu de l'élargir), le terminal garde au moins la moitié de la largeur, le bouton de la macro reste dans la fenêtre, cliquable, et l'envoi contient l'argument entier.
import os, re, time
from gtktest import *

e = Env('macro_argument_long')
ma = e.macros_path
long_arg = ''.join('%d' % (i % 10) for i in range(200))
write_macros_v2(ma, [dict(label='Long', action='L%s;', args=[long_arg])])
macro_config(e, ma)
e.start()
bouton = 'macro-%d' % macro_id(e, 'Long')

mw = e.find1(name='main-window')
check(mw['w'] == 1000, 'fenetre a sa largeur par defaut (1000) malgre l\'argument de 200 caracteres', mw['w'])
term = e.find1(name='terminal')
check(term['w'] >= 1000 // 2 - 10, 'le terminal garde au moins la moitie de la largeur', term['w'])
champ = e.find1(name='%s-arg-0' % bouton)
check(champ.get('label') == '' and champ.get('placeholder') == long_arg, 'champ vide, argument entier en texte indicatif',
      (champ.get('label'), (champ.get('placeholder') or '')[:20]))
b = e.bw.settled(e.find1(name=bouton))
check(b['x'] >= term['x'] + term['w'] and b['x'] + b['w'] <= mw['x'] + mw['w'], 'bouton Long dans la fenetre, a droite du terminal',
      (b['x'], b['w'], term['x'] + term['w'], mw['x'] + mw['w']))
e.drain()
e.bw.click(b)
d = e.read_pty(1.0)
check(d == ('L%s;' % long_arg).encode(), 'clic sur Long : argument entier envoye (%r)' % d[:40])
e.finish()
sys.exit(conclude())
