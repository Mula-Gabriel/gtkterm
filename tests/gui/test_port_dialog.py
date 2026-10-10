# Dialogue Port (§6.4, C12) : Annuler ne change rien, instance unique, champs série masqués en TCP avec les défauts « localhost » / « 2323 », port
# TCP « 0 » refusé avec message, attente de caractère cochée et vide refusée (délai grisé), OK à 9600 rouvre le port (état et titre) sans rien écrire.
from gtktest import *


def ouvrir():
    e.action('port')
    return e.find1(name='port-dialog')


def ferme():
    return wait_until(lambda: not e.find(name='port-dialog'), 5)


def erreur():
    w = e.find(name='port-error')
    return w[0]['label'] if w else ''


e = Env('port_dialog')
e.write_config(e.general() + e.profile())
e.start(port=False, connect_browser=False)
base = '%s  115200-8-N-1' % e.port
check(wait_until(lambda: e.status_text() == base, 5), 'port du profil ouvert')

# 1. Annuler ne change rien
ouvrir()
check(e.find1(name='port-baud')['label'] == '115200', 'debit du profil affiche')
e.set_text('port-baud', '4800')
e.activate('port-cancel')
check(ferme(), 'Annuler ferme le dialogue')
check(e.status_text() == base, 'Annuler : port inchange')
ouvrir()
check(e.find1(name='port-baud')['label'] == '115200', 'Annuler : debit du profil intact')

# 2. instance unique (C12) : une seconde demande ramène le même dialogue
e.action('port')
time.sleep(0.5)
check(len(e.find(name='port-dialog')) == 1, 'un seul port-dialog')

# 3. Serial -> TCP Client : champs série masqués, défauts TCP
e.set_text('port-type', 'TCP Client')
check(wait_until(lambda: not e.find(name='port-device') and not e.find(name='port-baud'), 3), 'champs serie masques en TCP')
check(e.find1(name='port-tcp-host')['label'] == 'localhost', 'hote par defaut localhost')
check(e.find1(name='port-tcp-port')['label'] == '2323', 'port TCP par defaut 2323')

# 4. port TCP « 0 » refusé : message dans le dialogue, rien n'est appliqué
e.set_text('port-tcp-port', '0')
e.activate('port-ok')
check(wait_until(lambda: '65535' in erreur(), 3), 'port TCP 0 refuse avec la plage')
check(e.find(name='port-dialog') != [], 'dialogue toujours ouvert')
check(e.status_text() == base and e.alerts() == [], 'rien n est applique, pas de boite')

# 5. retour en série : attente de caractère cochée et vide refusée
e.set_text('port-type', 'Serial')
check(wait_until(lambda: e.find(name='port-device') != [] and erreur() == '', 3), 'champs serie revenus, erreur effacee')
e.activate('port-advanced')
e.find1(name='port-wait-char-check')                 # visible une fois l'expander déplié
e.activate('port-wait-char-check')
check(wait_until(lambda: e.find1(name='port-line-delay')['sensitive'] is False, 3), 'delai grise quand l attente est cochee')
e.set_text('port-wait-char', '')
e.activate('port-ok')
check(wait_until(lambda: 'character' in erreur(), 3), 'attente cochee et vide refusee')
e.activate('port-wait-char-check')
check(wait_until(lambda: e.find1(name='port-line-delay')['sensitive'] is True, 3), 'delai de nouveau actif')

# 6. OK à 9600 : port rouvert, titre à jour, rien d'écrit dans .gtktermrc
e.set_text('port-baud', '9600')
e.activate('port-ok')
check(ferme(), 'OK ferme le dialogue')
attendu = '%s  9600-8-N-1' % e.port
check(wait_until(lambda: e.status_text() == attendu, 5), 'port rouvert a 9600')
check(wait_until(lambda: any(w.get('title') == 'GTKTerm - ' + attendu for w in e.windows()), 5), 'titre de la fenetre a jour')
check(e.alerts() == [], 'aucune boite (9600 est standard)')
with open(e.config_path, encoding='utf-8') as f:
    check('speed=9600' not in f.read(), 'rien n est enregistre sans Save configuration')
e.finish()
sys.exit(conclude())
