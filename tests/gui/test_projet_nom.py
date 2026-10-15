# Nom de projet (spec docs/superpowers/specs/2026-10-15-nom-projet-et-editeur-macros-design.md, §1) : Rename project… sans fichier (nom dans le
# titre, projet modifié, Save as propose <nom>.gtkterm), avec déplacement du fichier, refus si la cible existe.
import os
import sys
from gtktest import *

e = Env('projet_nom')
e.write_config(e.general() + e.profile())
e.start()
check(not e.describe('project-rename')[0], 'sans projet : Rename project inactif')
e.action('project-new')
check(wait_until(lambda: e.project().get('open'), 5), 'projet ouvert')
e.action('project-rename')
e.find1(name='rename-project')
check(not e.find(name='project-rename-file'), 'sans fichier : pas de case de renommage du fichier')
e.set_text('project-name', 'Banc moteur')
e.activate('rename-ok')
check(wait_until(lambda: e.project().get('stored_name') == 'Banc moteur', 3), 'nom stocke : %r' % e.project())
check(wait_until(lambda: e.title().startswith('Banc moteur •'), 3), 'titre : %r' % e.title())
banc = e.project_path('Banc moteur.gtkterm')
save_project_as(e, banc)        # le sélecteur propose « Banc moteur.gtkterm » ; save_project_as tape le chemin complet
check(wait_until(lambda: e.project().get('path') == banc and not e.project().get('modified'), 5), 'enregistre : %r' % e.project())
check('name=Banc moteur' in open(banc).read(), 'cle name ecrite')
# Renommer avec le fichier
e.action('project-rename')
e.find1(name='project-rename-file')
e.set_text('project-name', 'Banc 2')
e.activate('rename-ok')
neuf = e.project_path('Banc 2.gtkterm')
check(wait_until(lambda: e.project().get('path') == neuf, 3), 'chemin deplace : %r' % e.project())
check(os.path.exists(neuf) and not os.path.exists(banc), 'fichier deplace')
check(e.project().get('modified') is True, 'nom change : projet modifie (fichier non reecrit)')
check('last_project=' + neuf in e.read_config(), 'last_project mis a jour')
# Cible existante : refus dans la fenêtre, rien ne change
open(e.project_path('Pris.gtkterm'), 'w').write('x')
e.action('project-rename')
e.set_text('project-name', 'Pris')
e.activate('rename-ok')
check(wait_until(lambda: 'already exists' in (e.find1(name='rename-error').get('label') or ''), 3), 'refus affiche')
check(e.project().get('path') == neuf and e.project().get('stored_name') == 'Banc 2', 'rien ne change : %r' % e.project())
e.activate('rename-cancel')
e.action('project-save')
e.finish()
sys.exit(conclude())
