# Arguments facultatifs (spec docs/superpowers/specs/2026-10-15-nom-projet-et-editeur-macros-design.md, §5) : table des arguments de
# « Configure Macros » — les cellules Default suivent Optional, « (none) » d'une liste facultative gardé, message de préfixe retiré.
# Séparé de test_macros_facultatifs : sous GTK 4.24, gtk4-broadwayd plante plus tôt dans celui-ci, ces contrôles restent ainsi exécutés.
import sys
from gtktest import *

e = Env('macros_facultatifs_defauts')
ini = e.project_path('m.ini')
# Un %s facultatif vide à longueur minimale 2 n'est pas en erreur et n'empêche pas d'enregistrer
# le défaut d'un autre argument ; une liste facultative offre « (none) » et le garde (jamais la première entrée écrite en silence)
write_macros_v2(ini, [dict(label='Lim', action='%s,%d;', arg_min=['2', ''], arg_optional=['1', ''], arg_prefix=['', '']),
                      dict(label='Mo2', action='mode%#Mode,%d;', arg_optional=['1', ''], arg_prefix=['', ''])],
                lists=[('Mode', [('Fast', 'fast'), ('Slow', 'slow')])])
macro_config(e, ini)
e.start()
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
check(wait_until(lambda: (e.find1(name='macro-editor-error').get('label') or '') == 'Prefix of argument 1 removed', 3), 'retour sur Lim : message reaffiche')
e.set_text(editor_rows(e)[0]['cells']['action']['id'], '%s,%d;')
check(wait_until(lambda: not e.find(name='macro-editor-error') or not e.find1(name='macro-editor-error').get('label'), 3), 'Lim : argument revenu, message retire')
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
