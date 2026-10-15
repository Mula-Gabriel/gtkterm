# Review Focus 1 : un message émis avant la fenêtre (profil avec bits=9, corrigé par Verify_configuration pendant la lecture de la ligne
# de commande) s'affiche une fois la fenêtre principale présentée, une seule fois ; jamais perdu. Le port s'ouvre avec la valeur corrigée.
# Échec d'ouverture du port au démarrage (signal open-failed) : ouverture automatique, aucune boîte (spec 2026-10-15-ouverture-silencieuse §1) ;
# la raison passe dans la barre d'état, l'onglet en tête de mort, l'application reste utilisable.
from gtktest import *

e = Env('messages_demarrage')
e.write_config(e.general() + e.profile(bits=9))
e.start(connect_browser=False)
q = wait_until(lambda: e.alerts(), 5)
# La file des messages n'en montre qu'une à la fois : « une seule boîte » est prouvé après fermeture (« rien ensuite »), pas par len(q).
check(bool(q) and 'Invalid number of bits: 9' in q[0], 'message du profil invalide affiche', q)
d = e.dump()
tops = e.windows(d)
mw = [w for w in tops if w.get('name') == 'main-window']
natives = e._alert_natives(d)  # boîtes de src/ui/dialogs.c (classes « dialog » et « message »), repérées par le harnais
al = [w for w in tops if w in natives]
check(bool(mw) and mw[0]['mapped'], 'fenetre principale presentee')
check(bool(mw) and bool(al) and tops.index(al[0]) > tops.index(mw[0]), 'boite creee apres la fenetre principale', [w['type'] for w in tops])
check(bool(al) and al[0].get('modal'), 'boite modale')
check(wait_until(lambda: e.title() == 'GTKTerm - %s  115200-8-N-1' % e.port, 5), 'port ouvert avec 8 bits (valeur corrigee)', e.title())
e.close_alert('OK')
check(wait_until(lambda: e.alerts() == [], 3), 'boite fermee')
time.sleep(1.0)
check(e.alerts() == [], 'une seule boite au demarrage : message affiche une seule fois, rien ensuite', e.alerts())
e.finish()

e = Env('messages_port_absent')
absent = os.path.join(e.root, 'absent')
e.write_config(e.general() + e.profile())
e.start('--port', absent, port=False, connect_browser=False)
check(wait_until(lambda: e.sessions() and e.sessions()[0]['activity'] == 'dead', 5), 'open-failed : tete de mort', e.sessions())
flash = e.find1(name='status-flash', mapped=None)
check(('Cannot open ' + absent) in (flash.get('label') or ''), 'open-failed : « Cannot open <port> » dans la barre d etat', flash.get('label'))
check(e.title() == 'GTKTerm - No open port', 'titre : aucun port ouvert', e.title())
time.sleep(1.0)
check(e.alerts() == [], 'open-failed au demarrage : aucune boite', e.alerts())
e.finish()
sys.exit(conclude())
