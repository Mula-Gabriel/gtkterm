# Projets (sous-projet 6c, spec §3.2-3, 3.2-9 à 3.2-14) : Save project as sur deux onglets libres (macros intégrées, texte du script, last_project,
# aucun .ini ni macros_file dans le fichier) ; titre « nom • - onglet » ; Save ; l'onglet actif ne compte pas dans « modifié » ; Close project
# (retour à un onglet libre, last_project effacé) ; Open project (onglets, ports, onglet actif, macros et script restaurés, envoi de macro) ;
# import (Open…) et export (Save…) d'un script dans un onglet de projet (chemins non retenus, aucun [session] script_path écrit) ; fichier
# abîmé refusé sans rien fermer.
import os, sys, time
from gi.repository import GLib
from gtktest import *

e = Env('projet')
ini = os.path.join(e.root, 'macros.ini')
write_macros_v2(ini, [dict(label='PING%d', action='PING%d\\r', args=['5'])])
# Panneau de script affiché dès le démarrage, séparateur enregistré : l'état de page (visibilité, position) part dans le projet et revient
# à sa réouverture
macro_config(e, ini, '[window]\nscript_panel_visible=true\nv_paned_position=300\n')
e.start(port=False)	# port pris du profil default : sans option de profil sur la ligne de commande, comme un lancement ordinaire
pa = os.path.basename(e.port)
b = e.new_pty()
pb = os.path.basename(b.port)
banc = e.project_path('banc.gtkterm')


def titre():
    return e.title()


def last_project():
    return e.config_group('general').get('last_project')


# 1. Deux onglets libres : macros du .ini, script saisi dans l'onglet 0
macro_id(e, 'PING%d')
e.find1(name='script-editor', timeout=5)
e.set_text('script-editor', 'print("a")')
check(wait_until(lambda: e.sessions()[0]['script'] == 'print("a")', 3), 'texte du script tenu par la session (%r)' % e.sessions()[0]['script'])
open_tab(e, b.port)
check(e.project().get('open') is False, 'aucun projet au demarrage')
check(titre().startswith('GTKTerm - '), 'titre 6b sans projet : %r' % titre())

# 2. Save project as : les onglets deviennent des onglets de projet ; fichier et last_project écrits
save_project_as(e, banc)
check(wait_until(lambda: e.project().get('path', '').endswith('banc.gtkterm'), 5), 'chemin du projet : %r' % e.project())
p = e.project()
check(p.get('open') is True and p.get('modified') is False, 'projet ouvert, non modifie : %r' % p)
check(p.get('name') == 'banc', 'nom du projet : %r' % p.get('name'))
check(wait_until(lambda: titre() == 'banc - %s' % pb, 3), 'titre « banc - <onglet actif> » : %r' % titre())
check(all(s['in_project'] for s in e.sessions()), 'tous les onglets dans le projet : %r' % [s['in_project'] for s in e.sessions()])
check(last_project() == banc, '[general] last_project : %r' % last_project())
contenu = open(banc).read() if os.path.exists(banc) else ''
for x in ('[tab:1]', 'PING%d', 'print("a")'):
    check(x in contenu, 'le fichier projet contient %s' % x)
for x in ('.ini', 'macros_file'):
    check(x not in contenu, 'le fichier projet ne contient pas %s' % x)

# 3. Écho : modifié (« • » dans le titre) ; Save : enregistré
e.set_state('local-echo', GLib.Variant('b', True))
check(wait_until(lambda: e.project().get('modified') is True, 3), 'echo active : projet modifie')
check(wait_until(lambda: titre() == 'banc • - %s' % pb, 3), 'titre avec « • » : %r' % titre())
e.action('project-save')
check(wait_until(lambda: e.project().get('modified') is False, 3), 'Save : projet non modifie')
check(wait_until(lambda: titre() == 'banc - %s' % pb, 3), 'titre sans « • » apres Save : %r' % titre())
check('echo=true' in open(banc).read(), 'echo enregistre dans le fichier')

# 4. L'onglet actif ne compte pas dans « modifié »
e.set_state('active-tab', GLib.Variant('i', 0))
check(wait_until(lambda: e.active_session()['name'] == pa, 3), 'onglet 0 actif')
time.sleep(0.8)	# recalcul regroupé (300 ms)
check(e.project().get('modified') is False, 'changer d onglet actif ne modifie pas le projet')
check(titre() == 'banc - %s' % pa, 'titre suit l onglet actif : %r' % titre())

# 5. Close project : un onglet libre, last_project effacé
e.action('project-close')
check(wait_until(lambda: len(e.sessions()) == 1 and e.project().get('open') is False, 5), 'Close project : un seul onglet, aucun projet')
check(wait_until(lambda: titre().startswith('GTKTerm - '), 3), 'titre 6b apres Close project : %r' % titre())
check(not e.sessions()[0]['in_project'], 'onglet libre apres Close project')
check('last_project' not in e.config_group('general'), 'last_project efface : %r' % e.config_group('general'))

# 6. Open project : deux onglets (ports A et B), onglet actif 1, macros et script de l'onglet 0, envoi de la macro
open_project(e, banc)
check(wait_until(lambda: len(e.sessions()) == 2, 5), 'deux onglets apres Open project : %r' % e.sessions())
check([s['name'] for s in e.sessions()] == [pa, pb], 'ports des onglets dans l ordre : %r' % [s['name'] for s in e.sessions()])
check(wait_until(lambda: all(s['state'] == 'open' for s in e.sessions()), 5), 'ports ouverts : %r' % [s['state'] for s in e.sessions()])
check(e.active_session()['index'] == 1, 'onglet actif 1 (celui de l enregistrement)')
check(all(s['in_project'] for s in e.sessions()), 'onglets de projet')
check(e.sessions()[0]['script'] == 'print("a")', 'script de l onglet 0 restaure : %r' % e.sessions()[0]['script'])
time.sleep(0.8)
check(e.project().get('modified') is False, 'projet ouvert non modifie')
check(e.project().get('path') == banc and last_project() == banc, 'chemin et last_project apres Open project')
e.set_state('active-tab', GLib.Variant('i', 0))
check(wait_until(lambda: e.active_session()['name'] == pa, 3), 'onglet 0 actif')
mid = macro_id(e, 'PING%d')
e.drain()
e.activate('macro-%d' % mid)
d = e.read_pty(0.8)
check(d == b'PING5\r', 'macro du projet envoyee sur A : %r' % d)
check(wait_until(lambda: e.find(name='script-editor') and e.find1(name='script-editor').get('label') == 'print("a")', 3),
      'editeur de l onglet 0 : texte du projet', [(w['mapped'], w.get('h'), w.get('label', '')[:12]) for w in e.find(name='script-editor', mapped=None)])

# Import d'un script (Open…) dans l'onglet de projet : texte importé, chemin non retenu ([session] script_path jamais écrit, vu à la fin)
lua = os.path.join(e.root, 'importe.lua')
with open(lua, 'w') as f:
    f.write('print("b")\n')
e.activate('script-open')
file_dialog(e, 'Open Lua Script', lua)
check(wait_until(lambda: e.sessions()[0]['script'] == 'print("b")\n', 3), 'script importe dans l onglet : %r' % e.sessions()[0]['script'])
check(wait_until(lambda: e.project().get('modified') is True, 3), 'import : projet modifie')
e.action('project-save')
check(wait_until(lambda: e.project().get('modified') is False, 3), 'Save apres l import')
check('print("b")' in open(banc).read(), 'script importe enregistre dans le projet')


def nom_propose(titre):
    """Texte du champ « Name » du sélecteur d'enregistrement intitulé titre."""
    d = e.dump()
    win = [w for w in d if w['depth'] == 0 and w.get('title') == titre]
    return [w.get('label', '') for w in d if win and w['native'] == win[0]['native'] and w['type'] == 'GtkFileChooserEntry' and w['mapped']]


# Save du panneau de script dans l'onglet de projet : export vers un .lua choisi ; le chemin n'est pas retenu (plan, décision 12) : le Save
# suivant rouvre le sélecteur avec le nom par défaut, pas celui de l'export
export = os.path.join(e.root, 'export.lua')
e.activate('script-save')
file_dialog(e, 'Save Lua Script', export)
check(wait_until(lambda: os.path.exists(export) and open(export).read() == 'print("b")\n', 3), 'export du script : %r'
      % (open(export).read() if os.path.exists(export) else None))
check(e.project().get('modified') is False, 'export : projet non modifie')
e.activate('script-save')
check(bool(wait_until(lambda: [w for w in e.windows() if w.get('title') == 'Save Lua Script'], 5)), 'second Save : selecteur rouvert')
check(wait_until(lambda: nom_propose('Save Lua Script') == ['script.lua'], 3), 'second Save : nom par defaut, chemin de l export non retenu (%r)'
      % nom_propose('Save Lua Script'))
e.bw.focus_window('Save Lua Script')
e.bw.key('Escape')
check(wait_until(lambda: not [w for w in e.windows() if w.get('title') == 'Save Lua Script'], 5), 'Echap ferme le selecteur')

# 7. Fichier abîmé : refusé avec un message, rien n'est fermé
abime = e.project_path('abime.gtkterm')
with open(abime, 'w') as f:
    f.write('[gtkterm-project]\nversion=9\n')
open_project(e, abime)
check(wait_until(lambda: e.alerts(), 5), 'boite d erreur pour le fichier abime')
check(any('abime.gtkterm' in a for a in e.alerts()), 'message nommant le fichier : %r' % e.alerts())
e.close_alert()
check(len(e.sessions()) == 2 and e.project().get('path') == banc, 'les deux onglets et le projet restent : %r' % e.project())
check(last_project() == banc, 'last_project inchange')

e.finish()	# projet non modifié : aucune question
session = e.config_group('session')
check('script_path' not in session, 'aucun [session] script_path pour un onglet de projet : %r' % session)
sys.exit(conclude())
