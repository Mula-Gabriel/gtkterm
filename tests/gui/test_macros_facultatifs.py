# Arguments facultatifs (spec docs/superpowers/specs/2026-10-15-nom-projet-et-editeur-macros-design.md, §5) : panneau — un %s facultatif vide
# n'envoie ni préfixe ni valeur ; rempli, il envoie le préfixe puis la valeur ; une liste facultative a « (none) ».
# (la liste a trois entrées : à deux, le panneau montre deux boutons au lieu d'une liste déroulante.)
from gtktest import *

e = Env('macros_facultatifs')
ini = e.project_path('m.ini')
write_macros_v2(ini, [dict(label='Co', action='connect %s%s;', args=['srv', ''], arg_optional=['', '1'], arg_prefix=['', ' -password=']),
                      dict(label='Mo', action='mode%#Mode;', arg_optional=['1'], arg_prefix=[' -m='])],
                lists=[('Mode', [('Fast', 'fast'), ('Mid', 'mid'), ('Slow', 'slow')])])
macro_config(e, ini)
e.start()
co = macro_id(e, 'Co')
e.drain()
e.activate('macro-%d' % co)
check(wait_until(lambda: b'connect srv;' in e.read_pty(0.3), 3), 'facultatif vide : ni prefixe ni valeur')
check(e.find1(name='macro-%d-arg-1' % co).get('placeholder') == '(omitted)', 'texte indicatif (omitted)')
e.bw.click(e.find1(name='macro-%d-arg-1' % co))
e.set_text('macro-%d-arg-1' % co, 'abc')
e.bw.key('Return')        # Entrée : valeur d'usage prise
e.drain()
e.activate('macro-%d' % co)
check(wait_until(lambda: b'connect srv -password=abc;' in e.read_pty(0.3), 3), 'rempli : prefixe puis valeur')
mo = macro_id(e, 'Mo')
e.drain()
e.activate('macro-%d' % mo)
check(wait_until(lambda: b'mode;' in e.read_pty(0.3), 3), 'liste facultative sur (none) : omise')
e.set_text('macro-%d-arg-0' % mo, 'Slow')
e.drain()
e.activate('macro-%d' % mo)
check(wait_until(lambda: b'mode -m=slow;' in e.read_pty(0.3), 3), 'liste choisie : prefixe et valeur')
e.finish()
