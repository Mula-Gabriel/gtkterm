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
# Frappe dans l'action : un état transitoire à un argument de moins ne perd ni la case ni le préfixe (message seulement)
cellule = editor_rows(e)[0]['cells']['action']['id']
e.set_text(cellule, 'login %s;')
check(wait_until(lambda: (e.find1(name='macro-editor-error').get('label') or '') == 'Prefix of argument 2 removed', 3),
      'message de prefixe perdu : %r' % e.find1(name='macro-editor-error').get('label'))
check(not e.find(name='macro-optional-1'), 'etat transitoire : plus de deuxieme argument')
e.set_text(cellule, 'login %s%s;')
check(wait_until(lambda: bool(e.find(name='macro-optional-1')) and e.find1(name='macro-optional-1').get('active') is True, 3),
      'case toujours cochee apres l\'etat transitoire')
check(e.find1(name='macro-prefix-1').get('label') == ' -password=', 'prefixe garde : %r' % e.find1(name='macro-prefix-1').get('label'))
check(not e.find(name='macro-editor-error') or not e.find1(name='macro-editor-error').get('label'), 'message retire')
e.activate('macro-ok')
# Envoi depuis le panneau avec le champ vide : le préfixe ne part pas ; rempli, il part
pw = macro_id(e, 'Pw')
e.bw.click(e.find1(name='macro-%d-arg-0' % pw))
e.set_text('macro-%d-arg-0' % pw, 'bob')
e.bw.key('Return')        # Entrée : valeur d'usage prise
e.drain()
e.activate('macro-%d' % pw)
check(wait_until(lambda: b'login bob;' in e.read_pty(0.3), 3), 'facultatif vide apres edition : omis')
# Cellules Default d'arguments facultatifs : un %s facultatif vide à longueur minimale 2 n'est pas en erreur et n'empêche pas d'enregistrer
# le défaut d'un autre argument ; une liste facultative offre « (none) » et le garde (jamais la première entrée écrite en silence)
write_macros_v2(ini, [dict(label='Lim', action='%s,%d;', arg_min=['2', ''], arg_optional=['1', ''], arg_prefix=['', '']),
                      dict(label='Mo2', action='mode%#Mode,%d;', arg_optional=['1', ''], arg_prefix=['', ''])],
                lists=[('Mode', [('Fast', 'fast'), ('Slow', 'slow')])])
e.action('load-macros')
file_dialog(e, 'Load macros file', ini)
check(wait_until(lambda: not e.find(name='macro-editor'), 3), 'editeur ferme')
open_editor(e)
editor_select(e, 0, col='label')
check(wait_until(lambda: bool(e.find(name='macro-default-1')), 3), 'table des arguments de Lim')
e.set_text('macro-default-1', '9')
check(wait_until(lambda: 'error' not in classes(e.find1(name='macro-default-0')), 3), 'defaut facultatif vide : pas en erreur malgre min 2')
# Optional décoché puis recoché sans changer l'action (rien ne précède l'argument, préfixe vide) : la cellule Default suit
e.activate('macro-optional-0')
check(wait_until(lambda: 'error' in classes(e.find1(name='macro-default-0')), 3), 'decoche : defaut vide en erreur (min 2)')
e.activate('macro-optional-0')
check(wait_until(lambda: 'error' not in classes(e.find1(name='macro-default-0')), 3), 'recoche : defaut vide accepte')


def libelles_dans(w):
    return [x.get('label') for x in e.dump() if x.get('type') == 'GtkLabel' and x.get('mapped') and x.get('native') == w.get('native')
            and w['x'] <= x['x'] and x['x'] + x['w'] <= w['x'] + w['w'] and w['y'] <= x['y'] and x['y'] + x['h'] <= w['y'] + w['h']]


editor_select(e, 1, col='label')
check(wait_until(lambda: bool(e.find(name='macro-min-1')), 3), 'table des arguments de Mo2')
check(wait_until(lambda: '(none)' in libelles_dans(e.find1(name='macro-default-0')), 3),
      'liste facultative : (none) choisi (%r)' % libelles_dans(e.find1(name='macro-default-0')))
e.set_text('macro-min-1', '0')        # écrit la ligne (défauts compris)
check(wait_until(lambda: '(none)' in libelles_dans(e.find1(name='macro-default-0')), 3), 'liste facultative : (none) garde')
# Message « Prefix of argument 1 removed » d'une ligne retiré quand une autre ligne est affichée
editor_select(e, 0, col='label')
check(wait_until(lambda: bool(e.find(name='macro-prefix-0')), 3), 'table de Lim')
e.set_text('macro-prefix-0', 'p')
cellule = editor_rows(e)[0]['cells']['action']['id']
e.set_text(cellule, 'x;')
check(wait_until(lambda: (e.find1(name='macro-editor-error').get('label') or '') == 'Prefix of argument 1 removed', 3), 'message de prefixe perdu (Lim)')
editor_select(e, 1, col='label')
check(wait_until(lambda: not e.find(name='macro-editor-error') or not e.find1(name='macro-editor-error').get('label'), 3), 'autre ligne : message retire')
editor_select(e, 0, col='label')
e.set_text(editor_rows(e)[0]['cells']['action']['id'], '%s,%d;')
check(wait_until(lambda: e.find1(name='macro-default-1').get('label') == '9', 3), 'Lim : defaut 9 toujours la')
e.activate('macro-ok')
check(wait_until(lambda: not e.find(name='macro-editor'), 3), 'editeur ferme par OK')


def groupe(label):
    blocs = open(ini).read().split('\n\n')
    return next((b for b in blocs if ('\nlabel=%s\n' % label) in b), '')


check(wait_until(lambda: 'args=;9;' in groupe('Lim'), 3), '.ini : defaut 9 de Lim enregistre (%r)' % groupe('Lim'))
check('arg_min=;0;' in groupe('Mo2') and 'fast' not in groupe('Mo2'), '.ini : Mo2 sans defaut fast (%r)' % groupe('Mo2'))
e.finish()
sys.exit(conclude())
