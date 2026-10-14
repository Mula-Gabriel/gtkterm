# Fermeture avec un projet modifié (sous-projet 6c, spec §3.1-6 et §3.2-9) : app.quit pose la question Save / Don't save / Cancel (boîte
# modale, sans boucle imbriquée) ; Cancel garde la fenêtre ; Don't save quitte (code 0) sans toucher au fichier ; au lancement suivant (sans
# option de profil) le dernier projet est rouvert ; un Save qui échoue (dossier en lecture seule) affiche l'erreur et ne ferme pas ; Save
# l'enregistre avant de quitter.
import os, sys, time, subprocess
from gi.repository import GLib
from gtktest import *

e = Env('projet_fermeture')
e.write_config(e.general() + e.profile())
e.start(port=False)
dossier = e.project_path('projets')	# dossier du projet : mis en lecture seule pour faire échouer Save (écriture atomique : fichier temporaire + rename)
os.makedirs(dossier)
banc = os.path.join(dossier, 'banc.gtkterm')


def boutons():
    d = e.dump()
    natifs = {a['native'] for a in e._alert_natives(d)}
    return {w.get('label') for w in d if w['native'] in natifs and w['type'] == 'GtkButton' and w.get('label')}


def question():
    check(wait_until(lambda: e.alerts(), 5), 'question a la fermeture')
    check(any('Save changes to the project?' in a and 'banc' in a for a in e.alerts()), 'texte de la question : %r' % e.alerts())
    check({'Save', "Don't save", 'Cancel'} <= boutons(), 'boutons Save, Don t save, Cancel : %r' % sorted(boutons()))


def attendre_fin():
    try:
        e.proc.wait(10)
    except subprocess.TimeoutExpired:
        pass
    return e.proc.poll()


save_project_as(e, banc)
check(wait_until(lambda: e.project().get('path') == banc and e.project().get('modified') is False, 5), 'projet enregistre : %r' % e.project())
e.set_state('local-echo', GLib.Variant('b', True))
check(wait_until(lambda: e.project().get('modified') is True, 3), 'projet modifie (echo)')
avant = open(banc).read()

# Cancel : la fenêtre reste
e.action('quit')
question()
e.close_alert('Cancel')
time.sleep(0.5)
check(e.proc.poll() is None and bool(e.find(name='main-window')), 'Cancel : fenetre toujours la')
check(e.project().get('modified') is True, 'Cancel : projet toujours modifie')

# Don't save : fin, fichier inchangé
e.action('quit')
question()
e.close_alert("Don't save")
check(attendre_fin() == 0, "Don't save : application terminee avec le code 0 (%r)" % e.proc.poll())
check(open(banc).read() == avant, "Don't save : fichier projet inchange")
e.stop()

# Lancement suivant : dernier projet rouvert ; modifié puis Save à la fermeture
e.start(port=False)
check(wait_until(lambda: e.project().get('path') == banc, 5), 'dernier projet rouvert : %r' % e.project())
check(e.state('local-echo') is False, 'echo du projet enregistre (faux)')
check(wait_until(lambda: e.title() == 'banc - %s' % os.path.basename(e.port), 3), 'titre du projet rouvert : %r' % e.title())
e.set_state('local-echo', GLib.Variant('b', True))
check(wait_until(lambda: e.project().get('modified') is True, 3), 'projet rouvert modifie (echo)')

# Save qui échoue (dossier en lecture seule) : message d'erreur, la fermeture n'a pas lieu
avant = open(banc).read()
os.chmod(dossier, 0o555)
try:
    e.action('quit')
    question()
    e.close_alert('Save')
    check(wait_until(lambda: any(a.startswith('Error') for a in e.alerts()), 5), 'Save impossible : message d erreur (%r)' % e.alerts())
    e.close_alert()
    time.sleep(0.5)
    check(e.proc.poll() is None and bool(e.find(name='main-window')), 'Save impossible : fenetre toujours la, GTKTerm tourne')
    check(e.project().get('modified') is True, 'Save impossible : projet toujours modifie')
finally:
    os.chmod(dossier, 0o755)
check(open(banc).read() == avant, 'Save impossible : fichier projet inchange')

e.action('quit')
question()
e.close_alert('Save')
check(attendre_fin() == 0, 'Save : application terminee avec le code 0 (%r)' % e.proc.poll())
check('echo=true' in open(banc).read(), 'Save : echo enregistre dans le fichier')
e.finish(quit=False)
sys.exit(conclude())
