# Enregistrement d'un projet (sous-projet 6c, spec §3.2-15) : Save project as avec un nom sans « .gtkterm » dont la version suffixée existe déjà
# demande d'abord (Cancel n'écrit rien, Replace écrit) ; fichier projet réécrit par un tiers puis Save : question Overwrite / Reload / Cancel,
# Cancel bouton par défaut (focus) et Overwrite destructive-action, Entrée n'écrase pas ; Cancel (rien d'écrit), Overwrite (écrit), Reload (onglets
# relus depuis le disque) ; Reload avec un script en cours : confirmation « Close all tabs? », Cancel garde tout.
import os, sys, time
from gi.repository import GLib
from gtktest import *

BOUCLE = 'while true do gtkterm.sleep(20) end\n'

e = Env('projet_conflit')
e.write_config(e.general() + e.profile())
e.start(port=False)
banc = e.project_path('banc.gtkterm')


def contenu():
    return open(banc).read() if os.path.exists(banc) else ''


def tiers(old, new):
    """Réécriture du fichier projet par un tiers (taille changée : l'identité change à coup sûr)."""
    texte = contenu()
    check(old in texte, 'tiers : %r present dans le fichier' % old)
    with open(banc, 'w') as f:
        f.write(texte.replace(old, new))
    return open(banc).read()


def boutons():
    """{libellé: widget} des boutons de la boîte ouverte."""
    d = e.dump()
    natifs = {a['native'] for a in e._alert_natives(d)}
    return {w['label']: w for w in d if w['native'] in natifs and w['type'] == 'GtkButton' and w.get('label')}


def question(texte, chemin):
    check(wait_until(lambda: e.alerts(), 5), 'boite ouverte (%s)' % texte)
    a = e.alerts()
    check(bool(a) and texte in a[0] and chemin in a[0], 'texte et chemin de la boite : %r' % a)


def modifier():
    """Écho basculé : le projet est modifié."""
    e.set_state('local-echo', GLib.Variant('b', not e.state('local-echo')))
    check(wait_until(lambda: e.project().get('modified') is True, 3), 'projet modifie')


# 1. Save project as : projet enregistré (écho faux)
save_project_as(e, banc)
check(wait_until(lambda: e.project().get('path') == banc and e.project().get('modified') is False, 5), 'projet enregistre : %r' % e.project())
check('echo=false' in contenu(), 'echo=false enregistre')

# 2. Save project as « banc » (sans suffixe) : banc.gtkterm existe, question d'abord ; Cancel n'écrit rien, Replace écrit
modifier()
avant = contenu()
save_project_as(e, e.project_path('banc'))
question('already exists', banc)
e.close_alert('Cancel')
check(wait_until(lambda: not e.alerts(), 3), 'boite fermee par Cancel')
check(contenu() == avant, 'Cancel : fichier inchange')
check(e.project().get('modified') is True, 'Cancel : projet toujours modifie')
check(not os.path.exists(e.project_path('banc')), 'aucun fichier « banc » sans suffixe')
save_project_as(e, e.project_path('banc'))
question('already exists', banc)
e.close_alert('Replace')
check(wait_until(lambda: 'echo=true' in contenu(), 3), 'Replace : fichier ecrit (echo=true)')
check(wait_until(lambda: e.project().get('modified') is False and e.project().get('path') == banc, 3), 'Replace : projet enregistre : %r' % e.project())

# 3. Tiers, puis Save : question de conflit ; Cancel est le bouton par défaut (focus), Overwrite destructive-action ; Entrée n'écrase pas
version_tiers = tiers('timestamp=false', 'timestamp=true')
modifier()
e.action('project-save')
question('The project file changed on disk.', banc)
b = boutons()
check({'Overwrite', 'Reload', 'Cancel'} <= set(b), 'boutons Overwrite, Reload, Cancel : %r' % sorted(b))
check(b.get('Cancel', {}).get('focus') is True, 'focus sur Cancel : %r' % {k: v.get('focus') for k, v in b.items()})
check('destructive-action' in classes(b.get('Overwrite')) and 'suggested-action' not in classes(b.get('Overwrite')),
      'Overwrite : destructive-action, pas suggested-action : %r' % classes(b.get('Overwrite')))
check('suggested-action' not in classes(b.get('Cancel')), 'Cancel : pas suggested-action : %r' % classes(b.get('Cancel')))
e.bw.key('Return')
check(wait_until(lambda: not e.alerts(), 3), 'Entree ferme la boite')
check(contenu() == version_tiers, 'Entree : fichier = version du tiers')
check(e.project().get('modified') is True, 'Entree : projet toujours modifie')

# 4. Cancel : rien d'écrit
e.action('project-save')
question('The project file changed on disk.', banc)
e.close_alert('Cancel')
check(wait_until(lambda: not e.alerts(), 3), 'boite fermee par Cancel')
check(contenu() == version_tiers, 'Cancel : fichier = version du tiers')

# 5. Overwrite : écrit
e.action('project-save')
question('The project file changed on disk.', banc)
e.close_alert('Overwrite')
check(wait_until(lambda: 'timestamp=false' in contenu(), 3), 'Overwrite : fichier ecrit (timestamp=false)')
check(wait_until(lambda: e.project().get('modified') is False, 3), 'Overwrite : projet enregistre')

# 6. Reload : les onglets sont relus depuis le disque (horodatage du tiers)
tiers('timestamp=false', 'timestamp=true')
modifier()
e.action('project-save')
question('The project file changed on disk.', banc)
e.close_alert('Reload')
check(wait_until(lambda: e.state('timestamp') is True, 5), 'Reload : horodatage du fichier relu')
check(wait_until(lambda: e.project().get('modified') is False, 3), 'Reload : projet non modifie')
check(len(e.sessions()) == 1 and e.sessions()[0]['in_project'], 'Reload : un onglet de projet : %r' % e.sessions())

# 7. Reload avec un script en cours : « Close all tabs? » ; Cancel garde l'onglet et son script, Close tabs relit
e.set_state('show-script-panel', GLib.Variant('b', True))
check(wait_until(lambda: e.find(name='script-editor')), 'panneau de script')
e.set_text('script-editor', BOUCLE)
e.activate('script-run')
check(wait_until(lambda: e.active_session()['busy']), 'onglet occupe (script en cours)')
id_avant = e.active_session()['id']
version_tiers = tiers('timestamp=true', 'timestamp=false')
e.action('project-save')
question('The project file changed on disk.', banc)
e.close_alert('Reload')
question('Close all tabs?', 'script')
e.close_alert('Cancel')
check(wait_until(lambda: not e.alerts(), 3), 'confirmation fermee par Cancel')
time.sleep(0.3)  # preuve negative : le rappel de Cancel passe par une source d'inactivite, 0,3 s laisse le temps a une relecture fautive
check(e.active_session()['id'] == id_avant and e.active_session()['busy'], 'Cancel : meme onglet, script toujours en cours')
check(e.state('timestamp') is True, 'Cancel : rien relu')
check(contenu() == version_tiers, 'Cancel : fichier = version du tiers')
e.action('project-save')
question('The project file changed on disk.', banc)
e.close_alert('Reload')
question('Close all tabs?', 'script')
e.close_alert('Close tabs')
check(wait_until(lambda: e.state('timestamp') is False, 6), 'Close tabs : fichier relu (horodatage faux)')
check(wait_until(lambda: not e.active_session()['busy'] and e.active_session()['id'] != id_avant, 6), 'Close tabs : nouvel onglet, aucun script : %r'
      % e.sessions())
check(contenu() == version_tiers, 'Reload : fichier inchange')
e.finish()
sys.exit(conclude())
