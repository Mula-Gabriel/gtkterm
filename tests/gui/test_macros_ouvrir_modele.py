# « Load macros file… » d'un onglet libre (spec docs/superpowers/specs/2026-10-15-ouverture-silencieuse-plein-ecran-drapeaux-design.md, §4) :
# question « Open this macros file » ; « Create a project with this template » (par défaut) fait des onglets un projet sans fichier dont l'onglet
# lie le fichier comme modèle ; Cancel ne change rien.
import sys
from gtktest import *

e = Env('macros_ouvrir_modele')
ini = e.project_path('m.ini')
write_macros_v2(ini, [dict(label='Ping', action='ping;')])
e.write_config(e.general() + e.profile())
e.start()
e.action('load-macros')
file_dialog(e, 'Load macros file', ini)
check(wait_until(lambda: any(a.startswith('Open this macros file') for a in e.alerts()), 5), 'question : %r' % e.alerts())
e.close_alert('Cancel')
check(not e.project().get('open') and e.sessions()[0]['template'] == '' and e.sessions()[0]['macros_file'] != ini, 'Cancel : rien ne change')
e.action('load-macros')
file_dialog(e, 'Load macros file', ini)
check(wait_until(lambda: any(a.startswith('Open this macros file') for a in e.alerts()), 5), 'question de nouveau')
e.close_alert('Create a project with this template')
check(wait_until(lambda: e.project().get('open') and e.sessions()[0]['in_project'], 5), 'projet ouvert : %r' % e.project())
check(e.project().get('path') in ('', None), 'projet sans fichier : %r' % e.project())
check(wait_until(lambda: e.sessions()[0]['template'] == ini, 3), 'onglet lie au modele : %r' % e.sessions()[0])
check(macro_id(e, 'Ping') > 0, 'macro du modele dans le panneau')
e.drain()
e.activate('macro-%d' % macro_id(e, 'Ping'))
check(wait_until(lambda: b'ping;' in e.read_pty(0.3), 3), 'macro envoyee')
# Projet sans fichier, lié à un modèle : modifié, quitter pose la question Save / Don't save / Cancel
check(wait_until(lambda: e.project().get('modified'), 3), 'projet modifie : %r' % e.project())
e.action('quit')
check(wait_until(lambda: bool(e.alerts()), 5), 'question en quittant')
e.close_alert("Don't save")
e.finish()
sys.exit(conclude())
