# Démarrage avec un projet (sous-projet 6c, spec §3.2-14) : projet écrit à la main (deux onglets, onglet actif 1) rouvert par [general] last_project ;
# last_project vers un fichier absent : boîte d'erreur après la présentation, un onglet libre, last_project effacé (plus de boîte au démarrage
# suivant) ; fichier illisible (droits) : boîte, last_project gardé ; -p avec un last_project valide : pas de projet ;
# --project FICHIER : projet ouvert ; --project avec -p : code 1 sans fenêtre.
import os, sys, subprocess, time
from gtktest import *

e = Env('projet_demarrage')
b = e.new_pty()
pa = os.path.basename(e.port)
pb = os.path.basename(b.port)
banc = e.project_path('banc.gtkterm')
absent = e.project_path('absent.gtkterm')


def onglet(n, port):
    return ('[tab:%d]\nprofile_name=default\ntransport=serial\nport=%s\nspeed=115200\nbits=8\nstopbits=1\nparity=none\nflow=none\nwait_delay=0\n'
            'wait_char=-1\necho=false\ncrlfauto=false\nautoreconnect_enabled=false\nesc_clear_screen=false\ntimestamp=false\n\n' % (n, port))


with open(banc, 'w') as f:
    f.write('[gtkterm-project]\nversion=1\nactive=1\n\n' + onglet(0, e.port) + onglet(1, b.port))


def fin():
    """Fermeture (app.quit) : code 0 ; l'Env reste utilisable pour le lancement suivant."""
    e.action('quit')
    try:
        rc = e.proc.wait(10)
    except subprocess.TimeoutExpired:
        rc = None
    check(rc == 0, 'application terminee avec le code 0 (%r)' % rc)
    e.stop()


def noms():
    return [s['name'] for s in e.sessions()]


# 1. Démarrage sans argument avec last_project : projet rouvert, onglet actif 1
e.write_config(e.general(last_project=banc) + e.profile())
e.start(port=False)
check(wait_until(lambda: len(e.sessions()) == 2, 5), 'deux onglets : %r' % e.sessions())
check(noms() == [pa, pb], 'ports des onglets : %r' % noms())
check(wait_until(lambda: e.active_session()['index'] == 1, 3), 'onglet actif 1 : %r' % e.sessions())
check(e.project().get('open') is True and e.project().get('path') == banc, 'projet ouvert : %r' % e.project())
check(all(s['in_project'] for s in e.sessions()), 'onglets de projet')
check(wait_until(lambda: all(s['state'] == 'open' for s in e.sessions()), 5), 'ports ouverts : %r' % [s['state'] for s in e.sessions()])
check(e.alerts() == [], 'aucune boite : %r' % e.alerts())
fin()

# 2. last_project vers un fichier absent : boîte d'erreur après la présentation (qui dit que le projet ne sera plus rouvert), un onglet libre,
# aucun projet, last_project effacé ; le démarrage suivant ne montre plus de boîte
e.write_config(e.general(last_project=absent) + e.profile())
e.start(port=False)
check(bool(wait_until(lambda: e.alerts(), 5)), 'boite d erreur apres la presentation')
a = e.alerts()
check(bool(a) and a[0].startswith('Error') and 'Cannot open the project' in a[0] and absent in a[0], 'titre et chemin dans la boite : %r' % a)
check(bool(a) and 'no longer reopened' in a[0], 'la boite dit que le projet ne sera plus rouvert : %r' % a)
check(bool(e.find(name='main-window')), 'fenetre principale presente')
e.close_alert('OK')
check(len(e.sessions()) == 1 and e.sessions()[0]['name'] == pa, 'un onglet libre : %r' % e.sessions())
check(e.project().get('open') is False, 'aucun projet : %r' % e.project())
check('last_project' not in e.config_group('general'), 'last_project efface : %r' % e.config_group('general'))
fin()
e.start(port=False)
check(wait_until(lambda: len(e.sessions()) == 1, 5), 'second demarrage : un onglet')
time.sleep(1)	# preuve négative : la boîte de démarrage est affichée dès la présentation de la fenêtre ; 1 s couvre largement ce délai
check(e.alerts() == [], 'second demarrage : aucune boite : %r' % e.alerts())
check(e.project().get('open') is False, 'second demarrage : aucun projet')
fin()

# 2b. last_project vers un fichier refusé pour une autre raison (droits) : boîte, mais last_project gardé (support momentanément illisible)
interdit = e.project_path('interdit.gtkterm')
with open(interdit, 'w') as f:
    f.write('[gtkterm-project]\nversion=1\nactive=0\n\n' + onglet(0, e.port))
os.chmod(interdit, 0)
try:
    e.write_config(e.general(last_project=interdit) + e.profile())
    e.start(port=False)
    check(bool(wait_until(lambda: e.alerts(), 5)), 'fichier illisible : boite d erreur')
    a = e.alerts()
    check(bool(a) and 'Cannot open the project' in a[0] and 'no longer reopened' not in a[0], 'fichier illisible : message sans effacement : %r' % a)
    e.close_alert('OK')
    check(e.config_group('general').get('last_project') == interdit, 'fichier illisible : last_project garde : %r' % e.config_group('general'))
    fin()
finally:
    os.chmod(interdit, 0o644)

# 3. -p avec un last_project valide : la ligne de commande l'emporte (un onglet, pas de projet)
e.write_config(e.general(last_project=banc) + e.profile(port='/dev/null'))
e.start()
check(len(e.sessions()) == 1 and e.sessions()[0]['name'] == pa, '-p : un onglet sur la pty : %r' % e.sessions())
check(e.project().get('open') is False, '-p : pas de projet : %r' % e.project())
check(not e.sessions()[0]['in_project'], '-p : onglet libre')
check(e.alerts() == [], '-p : aucune boite : %r' % e.alerts())
fin()

# 4. --project FICHIER (sans last_project)
e.write_config(e.general() + e.profile())
e.start('--project', banc, port=False)
check(wait_until(lambda: len(e.sessions()) == 2 and e.project().get('open') is True, 5), '--project : deux onglets, projet ouvert : %r' % e.sessions())
check(e.project().get('path') == banc and e.active_session()['index'] == 1, '--project : chemin et onglet actif : %r' % e.project())
check(e.config_group('general').get('last_project') == banc, '--project : last_project ecrit : %r' % e.config_group('general'))
fin()

# 5. --project avec -p : refusé, code 1, aucune fenêtre
try:
    e.start('--project', banc, '-p', e.port, port=False, connect_browser=False)
except RuntimeError as ex:
    log('start :', ex)
try:
    rc = e.proc.wait(10)
except subprocess.TimeoutExpired:
    rc = None
check(rc == 1, '--project avec -p : code de sortie 1 (%r)' % rc)
check('Usage' in e.stderr() or '--project' in e.stderr(), 'message sur la sortie d erreur : %r' % e.stderr()[-200:])
e.close()
sys.exit(conclude())
