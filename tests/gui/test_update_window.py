# Fenêtre « Updates » et vérification au démarrage (sous-projet 6a, M33/M34) : l'URL vérifiée est celle affichée (passée telle quelle à git,
# en un seul argument, et enregistrée) ; résultat « up to date » / « available » selon le faux git ; « Skip this version » enregistré et
# respecté au redémarrage ; « Update now » lance le terminal avec des arguments intacts ; fermeture pendant une vérification sans erreur.
from gtktest import *

LOCALE = local_revision()
AUTRE = 'e' * 40 if LOCALE.lower() != 'e' * 40 else 'd' * 40
URL = "file:///srv/dépôt l'essai/gtkterm.git"
PREFIX = '/opt/gtkterm essai'
PROPOSITION = 'A new version of GTKTerm is available.'

e = Env('update_window')
check(LOCALE != 'unknown', 'revision construite connue', LOCALE)
# Profil présent : pas de message de premier lancement ; URL injoignable de general() ; vérification au démarrage activée ici
e.write_config(e.general(update_check_startup='true', update_prefix=PREFIX) + e.profile())


def rc():
    return e.read_config()


def ls_remote():
    return [a for a in e.fake_calls('git') if 'ls-remote' in a]


def resultat():
    return e.find1(name='update-result').get('label') or ''


def alerte(texte):
    return any(texte in t for t in e.alerts())


def fenetre_fermee():
    return wait_until(lambda: not e.find(name='update-window'), 5)


# 1. Démarrage : mise à jour disponible → proposition ; « Skip this version » enregistré (M34)
e.fake_git(sha=AUTRE)
e.start()
check(wait_until(lambda: alerte(PROPOSITION), 10), 'proposition au demarrage', e.alerts())
appels = ls_remote()
check(bool(appels) and appels[-1][-3:] == ['--', 'https://invalid.example/none.git', 'HEAD'], 'git ls-remote -- <url> HEAD', appels)
e.close_alert('Skip this version')
check(wait_until(lambda: 'update_skipped_revision=%s' % AUTRE in rc(), 5), 'revision ignoree enregistree', rc())

# 2. Ouvrir puis fermer la fenêtre sans rien changer : rien n'est écrit
avant = rc()
e.action('updates')
e.find1(name='update-window')
check(resultat() == '', 'resultat vide a l ouverture', resultat())
e.activate('update-close')
check(fenetre_fermee(), 'fenetre fermee par Close')
check(rc() == avant, 'fenetre ouverte puis fermee : rien n est ecrit')

# 3. « Check now » vérifie l'URL affichée et l'enregistre ; faux git « à jour »
e.action('updates')
e.find1(name='update-window')
e.set_text('update-url', URL)
e.fake_git(sha=LOCALE)
n = len(ls_remote())
e.activate('update-check')
check(wait_until(lambda: 'up to date' in resultat(), 10), 'resultat : a jour', resultat())
check(len(ls_remote()) == n + 1 and ls_remote()[-1][-3:] == ['--', URL, 'HEAD'], 'git a recu l URL affichee en un seul argument', ls_remote()[-1:])
check('update_url=%s' % URL in rc(), 'URL affichee enregistree')
check(not e.alerts(), 'aucune proposition quand c est a jour', e.alerts())

# 4. Mise à jour disponible : proposée même si elle a été ignorée (vérification manuelle) ; « Later » n'écrit rien
avant = rc()
e.fake_git(sha=AUTRE)
e.activate('update-check')
check(wait_until(lambda: 'available' in resultat(), 10), 'resultat : disponible', resultat())
check(wait_until(lambda: alerte(PROPOSITION), 5), 'proposition en verification manuelle malgre la revision ignoree')
e.close_alert('Later')
time.sleep(0.5)
check(not e.alerts() and rc() == avant, 'Later : rien n est ecrit')

# 5. « Update now » : le terminal reçoit le script, l'URL vérifiée et le préfixe en arguments séparés et intacts (M34)
# Barrière dure : sans le faux xterm en tête du PATH, « Update now » lancerait un vrai terminal (et le script installé) ; le scénario s'arrête là
if not check(os.access(os.path.join(FAKEBIN, 'xterm'), os.X_OK), 'faux xterm en place : aucun vrai terminal ne sera lance'):
    e.finish()
    sys.exit(conclude())
e.activate('update-check')
check(wait_until(lambda: alerte(PROPOSITION), 10), 'nouvelle proposition')
e.close_alert('Update now')
check(bool(wait_until(lambda: e.fake_calls('xterm'), 5)), 'terminal lance')
args = (e.fake_calls('xterm') or [[]])[-1]
check(len(args) >= 7 and args[-7:-5] == ['sh', '-c'], 'commande sh -c en tete', args)
check(args[-4].endswith('/gtkterm-update.sh') and args[-3] == URL and args[-2] == PREFIX and args[-1].isdigit(), 'arguments du script intacts', args)
check(wait_until(lambda: alerte('The update is running in a terminal window.'), 5), 'message de lancement', e.alerts())
e.close_alert()

# 6. Case « au démarrage » : enregistrée à chaque changement
e.activate('update-startup')
check(wait_until(lambda: 'update_check_startup=false' in rc(), 5), 'case decochee enregistree', rc())
e.activate('update-startup')
check(wait_until(lambda: 'update_check_startup=true' in rc(), 5), 'case recochee enregistree', rc())

# 7. Fermeture pendant une vérification : annulée, sans effet ni erreur (stderr vérifié par finish)
e.fake_git(sha=AUTRE, delay=3)
n = len(ls_remote())
e.activate('update-check')
check(wait_until(lambda: len(ls_remote()) > n, 5), 'verification lancee')
e.activate('update-close')
check(fenetre_fermee(), 'fenetre fermee pendant la verification')
time.sleep(4)
check(not e.alerts() and not e.find(name='update-window'), 'verification annulee : ni proposition ni fenetre recreee', e.alerts())
e.action('updates')
check(resultat() == '', 'fenetre rouverte neuve', resultat())
e.activate('update-close')
check(fenetre_fermee(), 'fenetre refermee')

# 8. Redémarrage : la révision ignorée n'est plus proposée au démarrage
e.fake_git(sha=AUTRE)
e.stop()
n = len(ls_remote())
e.start()
check(wait_until(lambda: len(ls_remote()) > n, 10), 'verification au demarrage relancee')
time.sleep(1.5)
check(not e.alerts(), 'revision ignoree : aucune proposition au demarrage', e.alerts())

# 9. Fermeture (app.quit) pendant une proposition ouverte : annulée, code 0, stderr propre (vérifiés par finish, Review Focus 2)
e.action('updates')
e.find1(name='update-window')
e.activate('update-check')
check(wait_until(lambda: alerte(PROPOSITION), 10), 'proposition ouverte avant la fermeture')
e.finish()
sys.exit(conclude())
