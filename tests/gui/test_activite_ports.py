# Indicateurs d'activité des ports : attente, réception, actif, repos, port muet (tête de mort), non lu sur l'onglet caché, indicateur de la barre
# d'état avec un seul onglet, délais réglés dans la fenêtre Port puis enregistrés par Save configuration.
import sys
import time
from gtktest import *


def activite(e, index=0):
    s = e.sessions()
    return s[index]['activity'] if len(s) > index else None


e = Env('activite_ports')
e.write_config(e.general() + e.profile(activity_idle_timeout=2, activity_no_data_timeout=5))
e.start(port=False)
check(wait_until(lambda: e.active_session()['state'] == 'open', 5), 'port ouvert')
check(wait_until(lambda: activite(e) in ('waiting', 'dead'), 3), 'ouvert sans donnees : attente (%r)' % activite(e))
check(wait_until(lambda: e.find(name='status-activity'), 3), 'un seul onglet : indicateur dans la barre d etat')
check(wait_until(lambda: e.find(name='status-activity', css='activity-waiting') or e.find(name='status-activity', css='activity-dead'), 3),
      'classe CSS de l etat sur l indicateur')
check(not e.find(name='tab-activity'), 'un seul onglet : barre d onglets cachee, aucun indicateur d onglet visible')

# Réception continue : receiving (seulement observé pendant l'écriture) ; arrêt : active puis idle
fin = time.time() + 5
vu = set()
while time.time() < fin and 'receiving' not in vu:
    e.write_pty(b'x')
    vu.add(activite(e))
    time.sleep(0.05)
check('receiving' in vu, 'reception continue : receiving', vu)
check(wait_until(lambda: activite(e) == 'active', 3), 'arret de la reception : actif (%r)' % activite(e))
check(wait_until(lambda: activite(e) == 'idle', 6), 'silence de 2 s : repos (%r)' % activite(e))
check(wait_until(lambda: e.find(name='status-activity', css='activity-idle'), 3), 'classe CSS activity-idle sur l indicateur')
time.sleep(5)       # rester au repos au-delà du délai sans données (5 s)
check(activite(e) == 'idle', 'deja recu : jamais tete de mort (%r)' % activite(e))

# Deuxième onglet muet : tête de mort ; l'indicateur quitte la barre d'état
p2 = e.new_pty()
open_tab(e, p2.port)
check(wait_until(lambda: not e.find(name='status-activity'), 3), 'deux onglets : plus d indicateur dans la barre d etat')
check(wait_until(lambda: len(e.find(name='tab-activity')) == 2, 3), 'deux onglets : un indicateur par onglet')
check(wait_until(lambda: activite(e, 1) == 'dead', 10), 'onglet muet 5 s : tete de mort (%r)' % activite(e, 1))
check(wait_until(lambda: e.find(name='tab-label', css='dead'), 3), 'etiquette de l onglet muet grisee')
check(wait_until(lambda: e.find(name='tab-activity', css='activity-dead'), 3), 'classe CSS activity-dead sur l indicateur d onglet')
check(activite(e, 0) == 'idle', 'premier onglet toujours au repos (%r)' % activite(e, 0))

# Non lu : réception sur le premier onglet, caché
check(e.sessions()[0]['unread'] is False, 'rien de non lu avant la reception')
e.write_pty(b'y')
check(wait_until(lambda: e.sessions()[0]['unread'] is True, 3), 'reception hors vue : non lu')
check(wait_until(lambda: e.find(name='tab-label', css='unread'), 3), 'etiquette de l onglet non lu en gras')
e.action('previous-tab')
check(wait_until(lambda: e.sessions()[0]['active'] is True, 3), 'premier onglet affiche')
check(wait_until(lambda: e.sessions()[0]['unread'] is False, 3), 'onglet affiche : non lu efface')
check(wait_until(lambda: not e.find(name='tab-label', css='unread'), 3), 'etiquette sans la classe unread')

# Délais réglés dans la fenêtre Port, OK, puis Save configuration : relus dans le fichier
e.action('port')
e.find1(name='port-dialog')
e.activate('port-advanced')
e.find1(name='port-idle-timeout')                   # visible une fois l'expander déplié
e.set_text('port-idle-timeout', '7')
e.set_text('port-no-data-timeout', '0')
e.activate('port-ok')
check(wait_until(lambda: not e.find(name='port-dialog'), 5), 'dialogue Port ferme')
cfg = e.config_group('profile:default')
check(cfg.get('activity_idle_timeout') == '2', 'OK seul n ecrit pas .gtktermrc', cfg)
e.action('save-profile')
check(e.find1(name='profile-name')['label'] == 'default', 'nom prerempli avec le profil actif')
e.activate('profile-ok')
check(wait_until(lambda: 'already exists' in ' '.join(str(a) for a in e.alerts()), 5), 'confirmation de remplacement de default')
e.close_alert('Overwrite')
check(wait_until(lambda: not e.find(name='profile-window'), 5), 'fenetre d enregistrement fermee')
cfg = e.config_group('profile:default')
check(cfg.get('activity_idle_timeout') == '7' and cfg.get('activity_no_data_timeout') == '0', 'delais enregistres', cfg)
e.finish()
sys.exit(conclude())
