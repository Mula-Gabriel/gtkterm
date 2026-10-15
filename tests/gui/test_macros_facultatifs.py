# Arguments facultatifs (spec docs/superpowers/specs/2026-10-15-nom-projet-et-editeur-macros-design.md, §5) : panneau — un %s facultatif vide
# n'envoie ni préfixe ni valeur ; rempli, il envoie le préfixe puis la valeur ; une liste facultative a « (none) ».
# (la liste a deux entrées : facultative, elle reste une liste déroulante au lieu de deux boutons.)
import sys
from gtktest import *

e = Env('macros_facultatifs')
ini = e.project_path('m.ini')
write_macros_v2(ini, [dict(label='Co', action='connect %s%s;', args=['srv', ''], arg_optional=['', '1'], arg_prefix=['', ' -password=']),
                      dict(label='Mo', action='mode%#Mode;', arg_optional=['1'], arg_prefix=[' -m='])],
                lists=[('Mode', [('Fast', 'fast'), ('Slow', 'slow')])])
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
# Éditeur : cocher Optional découpe le préfixe hors de l'action, décocher le remet ; aperçu ∅
write_macros_v2(ini, [dict(label='Pw', action='login %s -password=%s;')])
e.action('load-macros')
file_dialog(e, 'Load macros file', ini)
open_editor(e)
editor_select(e, 0)
check(wait_until(lambda: bool(e.find(name='macro-optional-1')), 3), 'colonne Optional')
check(e.find1(name='macro-prefix-1').get('sensitive') is False, 'Prefix insensible decoche')
e.activate('macro-optional-1')
check(wait_until(lambda: editor_rows(e)[0]['action'] == 'login %s%s;', 3), 'action decoupee : %r' % editor_rows(e)[0]['action'])
check(wait_until(lambda: e.find1(name='macro-prefix-1').get('label') == ' -password=' and e.find1(name='macro-prefix-1').get('sensitive'), 3),
      'prefixe : %r' % e.find1(name='macro-prefix-1').get('label'))
check(wait_until(lambda: '∅' in (e.find1(name='macro-preview').get('label') or ''), 3), 'apercu : argument omis (%r)' % e.find1(name='macro-preview').get('label'))
check('#77767b' in (e.find1(name='macro-preview').get('markup') or ''), 'apercu : ∅ grise (%r)' % e.find1(name='macro-preview').get('markup'))
e.activate('macro-optional-1')
check(wait_until(lambda: editor_rows(e)[0]['action'] == 'login %s -password=%s;', 3), 'decoche : prefixe remis')
check(wait_until(lambda: e.find1(name='macro-prefix-1').get('label') == '', 3), 'decoche : champ vide')
e.activate('macro-optional-1')
check(wait_until(lambda: editor_rows(e)[0]['action'] == 'login %s%s;', 3), 'recoche')
e.activate('macro-ok')
# Envoi depuis le panneau avec le champ vide : le préfixe ne part pas ; rempli, il part
pw = macro_id(e, 'Pw')
e.bw.click(e.find1(name='macro-%d-arg-0' % pw))
e.set_text('macro-%d-arg-0' % pw, 'bob')
e.bw.key('Return')        # Entrée : valeur d'usage prise
e.drain()
e.activate('macro-%d' % pw)
check(wait_until(lambda: b'login bob;' in e.read_pty(0.3), 3), 'facultatif vide apres edition : omis')
e.finish()
sys.exit(conclude())
