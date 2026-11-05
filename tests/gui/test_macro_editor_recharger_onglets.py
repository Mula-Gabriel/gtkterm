# Configure Macros, Reload propagé aux autres onglets : après un Reload réussi, les autres onglets qui lisent le même fichier (même .ini pour des
# onglets libres, même modèle pour des onglets liés) sont proposés : « Reload all tabs » (par défaut) les relit, « This tab only » les laisse ;
# un onglet sur un autre fichier n'est jamais compté ni relu.
import sys
from gi.repository import GLib
from gtktest import *


def show_tab(e, i):
    e.set_state('active-tab', GLib.Variant('i', i))
    check(wait_until(lambda: e.active_session()['index'] == i, 3), 'onglet %d affiche' % i)


def labels(e):
    return sorted(l for l in macro_buttons(e))


def open_editor_active(e):
    e.action('macros')
    e.find1(name='macro-list')


def reload_question(e, attendu):
    e.activate('macro-reload')
    check(wait_until(lambda: any(a.startswith(attendu) for a in e.alerts()), 3), 'question %r : %r' % (attendu, e.alerts()))
    b = {w['label']: w for w in e.dump() if w.get('type') == 'GtkButton' and w.get('mapped') and w.get('label') in ('Reload all tabs', 'This tab only')}
    check(b.get('Reload all tabs', {}).get('focus') is True, 'Reload all tabs par defaut : %r' % {k: v.get('focus') for k, v in b.items()})
    check(e.find1(name='macro-reload').get('sensitive') is False, 'macro-reload insensible pendant la question')


e = Env('macro_editor_recharger_onglets')
ini = e.project_path('m.ini')
autre = e.project_path('autre.ini')
write_macros_v2(ini, [dict(label='a', action='A')])
write_macros_v2(autre, [dict(label='b', action='B')])
macro_config(e, ini)
e.start()
p2 = e.new_pty()
p3 = e.new_pty()
open_tab(e, p2.port)                            # même .ini que le premier onglet
open_tab(e, p3.port)
e.action('load-macros')
load_macros_free(e, autre)
check(wait_until(lambda: e.sessions()[2]['macros_file'] == autre, 5), 'troisieme onglet : autre fichier')

# This tab only : seul l'onglet de l'éditeur relit
show_tab(e, 0)
open_editor_active(e)
write_macros_v2(ini, [dict(label='a', action='A'), dict(label='z', action='Z')])
reload_question(e, 'Also reload the macros of the other tab using this file?')
e.close_alert('This tab only')
check(wait_until(lambda: [r['label'] for r in editor_rows(e)] == ['a', 'z'], 3), 'onglet de l editeur relu : %r' % [r['label'] for r in editor_rows(e)])
check(e.find1(name='macro-reload').get('sensitive') is True, 'macro-reload de nouveau sensible')
e.activate('macro-cancel')
check(wait_until(lambda: not e.find(name='macro-editor'), 3), 'editeur ferme')
show_tab(e, 1)
check(wait_until(lambda: labels(e) == ['a'], 3), 'This tab only : second onglet inchange : %r' % labels(e))

# Reload all tabs depuis le second onglet : le premier suit, le troisième (autre fichier) non
write_macros_v2(ini, [dict(label='a', action='A'), dict(label='y', action='Y')])
open_editor_active(e)
reload_question(e, 'Also reload the macros of the other tab using this file?')
e.close_alert('Reload all tabs')
check(wait_until(lambda: [r['label'] for r in editor_rows(e)] == ['a', 'y'], 3), 'editeur relu : %r' % [r['label'] for r in editor_rows(e)])
check(e.find(name='macro-editor'), 'editeur reste ouvert')
e.activate('macro-cancel')
check(wait_until(lambda: not e.find(name='macro-editor'), 3), 'editeur ferme')
show_tab(e, 0)
check(wait_until(lambda: labels(e) == ['a', 'y'], 3), 'Reload all tabs : premier onglet relu : %r' % labels(e))
show_tab(e, 2)
check(wait_until(lambda: labels(e) == ['b'], 3), 'autre fichier : troisieme onglet inchange : %r' % labels(e))

# Onglets liés au même modèle : le Reload de l'un propose l'autre (le troisième, lié à rien, n'est pas compté)
save_project_as(e, e.project_path('p.gtkterm'))
check(wait_until(lambda: all(s['in_project'] for s in e.sessions()), 5), 'onglets de projet')
modele = e.project_path('modele.ini')
write_macros_v2(modele, [dict(label='T1', action='T')])
for i in (0, 1):
    show_tab(e, i)
    e.action('load-macros')
    file_dialog(e, 'Load macros file', modele)
    check(wait_until(lambda: any(a.startswith('Use this macros file in the project') for a in e.alerts()), 5), 'question copie ou lien : %r' % e.alerts())
    e.close_alert('Link as template')
    check(wait_until(lambda: e.sessions()[i]['template'] == modele, 5), 'onglet %d lie : %r' % (i, e.sessions()[i].get('template')))
write_macros_v2(modele, [dict(label='T1', action='T'), dict(label='T2', action='U')])
open_editor_active(e)                           # onglet 1
reload_question(e, 'Also reload the macros of the other tab using this file?')
e.close_alert('Reload all tabs')
check(wait_until(lambda: [r['label'] for r in editor_rows(e)] == ['T1', 'T2'], 3), 'modele relu : %r' % [r['label'] for r in editor_rows(e)])
e.activate('macro-cancel')
check(wait_until(lambda: not e.find(name='macro-editor'), 3), 'editeur ferme')
show_tab(e, 0)
check(wait_until(lambda: labels(e) == ['T1', 'T2'], 3), 'premier onglet lie relu : %r' % labels(e))
check(e.sessions()[0]['template'] == modele, 'toujours lie')
e.action('project-save')
check(wait_until(lambda: e.project().get('modified') is False, 5), 'projet enregistre : %r' % e.project())
e.finish()
sys.exit(conclude())
