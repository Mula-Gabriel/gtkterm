# Configure Macros, fichier visé et Reload (spec docs/superpowers/specs/2026-10-15-nom-projet-et-editeur-macros-design.md, §4) :
# onglet libre : « File: <chemin> » ; fichier réécrit sur le disque → Reload sans modification : lignes relues, éditeur ouvert ; avec une ligne
# modifiée : question, Cancel garde les lignes, Reload les jette ; onglet de projet non lié : « Embedded in the project », pas de bouton ;
# onglet lié à un modèle : « Template: <chemin> », modèle réécrit → Reload relit les lignes (valeurs par défaut en place, puis structure changée).
import sys
from gtktest import *

e = Env('macro_editor_recharger')
ini = e.project_path('m.ini')
write_macros_v2(ini, [dict(label='a', action='A')])
macro_config(e, ini)
e.start()
open_editor(e)
check(e.find1(name='macro-source-label').get('label') == 'File: ' + ini, 'libelle du fichier : %r' % e.find1(name='macro-source-label').get('label'))
write_macros_v2(ini, [dict(label='a', action='A'), dict(label='z', action='Z')])
e.activate('macro-reload')
check(wait_until(lambda: [r['label'] for r in editor_rows(e)] == ['a', 'z'], 3), 'relu sans question : %r' % [r['label'] for r in editor_rows(e)])
check(e.find(name='macro-editor'), 'editeur reste ouvert')
check(e.alerts() == [], 'aucune question sans modification : %r' % e.alerts())
e.activate('macro-add')                       # ligne modifiée
write_macros_v2(ini, [dict(label='q', action='Q')])
e.activate('macro-reload')
check(wait_until(lambda: any('Discard your changes' in a for a in e.alerts()), 3), 'question posee : %r' % e.alerts())
b = {w['label']: w for w in e.dump() if w.get('type') == 'GtkButton' and w.get('mapped') and w.get('label') in ('Reload', 'Cancel')}
check(b.get('Cancel', {}).get('focus') is True, 'Cancel par defaut : %r' % {k: v.get('focus') for k, v in b.items()})
check('destructive-action' in classes(b.get('Reload')), 'Reload destructeur : %r' % classes(b.get('Reload')))
e.close_alert('Cancel')
check(len(editor_rows(e)) == 3, 'Cancel : lignes gardees : %r' % [r['label'] for r in editor_rows(e)])
e.activate('macro-reload')
check(wait_until(lambda: e.alerts(), 3), 'question reposee')
e.close_alert('Reload')
check(wait_until(lambda: [r['label'] for r in editor_rows(e)] == ['q'], 3), 'Reload : lignes du disque : %r' % [r['label'] for r in editor_rows(e)])
check(e.find(name='macro-editor'), 'editeur ouvert apres Reload')
check(wait_until(lambda: 'q' in macro_buttons(e), 3), 'panneau : macro q : %r' % macro_buttons(e))
e.activate('macro-cancel')
check(wait_until(lambda: not e.find(name='macro-editor'), 3), 'editeur ferme')

# Onglet de projet non lié
save_project_as(e, e.project_path('p.gtkterm'))
check(wait_until(lambda: e.sessions()[0]['in_project'], 5), 'onglet de projet')
open_editor(e)
check(e.find1(name='macro-source-label').get('label') == 'Embedded in the project', 'integre au projet : %r' % e.find1(name='macro-source-label').get('label'))
check(not e.find(name='macro-reload'), 'pas de bouton Reload')
e.activate('macro-cancel')
check(wait_until(lambda: not e.find(name='macro-editor'), 3), 'editeur ferme')

# Onglet lié à un modèle
modele = e.project_path('modele.ini')
write_macros_v2(modele, [dict(label='T1', action='T%d;', args=['5'])])
e.action('load-macros')
file_dialog(e, 'Load macros file', modele)
check(wait_until(lambda: any(a.startswith('Use this macros file in the project') for a in e.alerts()), 5), 'question copie ou lien : %r' % e.alerts())
e.close_alert('Link as template')
check(wait_until(lambda: e.sessions()[0]['template'] == modele, 5), 'onglet lie : %r' % e.sessions()[0].get('template'))
open_editor(e)
check(e.find1(name='macro-source-label').get('label') == 'Template: ' + modele, 'libelle du modele : %r' % e.find1(name='macro-source-label').get('label'))
check(e.find(name='macro-reload'), 'bouton Reload visible (onglet lie)')
# Valeur par défaut changée seulement : rechargement en place (même jeu), lignes refaites quand même
write_macros_v2(modele, [dict(label='T1', action='T%d;', args=['7'])])
e.activate('macro-reload')
check(e.alerts() == [], 'aucune question sans modification : %r' % e.alerts())
editor_select(e, 0, col='label')
check(wait_until(lambda: e.find(name='macro-preview') and e.find1(name='macro-preview').get('label') == 'T7;', 3),
      'defaut relu en place : %r' % (e.find(name='macro-preview') or [{}])[0].get('label'))
check(e.find(name='macro-editor'), 'editeur ouvert (en place)')
# Structure changée : jeu remplacé, éditeur gardé ouvert, toujours lié
write_macros_v2(modele, [dict(label='T1', action='T%d;', args=['7']), dict(label='T2', action='U')])
e.activate('macro-reload')
check(wait_until(lambda: [r['label'] for r in editor_rows(e)] == ['T1', 'T2'], 3), 'modele relu : %r' % [r['label'] for r in editor_rows(e)])
check(e.find(name='macro-editor'), 'editeur ouvert (structure changee)')
check(e.find1(name='macro-source-label').get('label') == 'Template: ' + modele, 'toujours le modele : %r' % e.find1(name='macro-source-label').get('label'))
check(e.sessions()[0]['template'] == modele, 'toujours lie')
e.activate('macro-cancel')
check(e.alerts() == [], 'aucune boite : %r' % e.alerts())
e.action('project-save')                      # projet modifié par le lien : enregistré, la fermeture ne pose pas de question
check(wait_until(lambda: e.project().get('modified') is False, 5), 'projet enregistre : %r' % e.project())
e.finish()
sys.exit(conclude())
