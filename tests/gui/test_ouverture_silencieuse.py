# Ouverture silencieuse (spec docs/superpowers/specs/2026-10-15-ouverture-silencieuse-plein-ecran-drapeaux-design.md, §1) : au démarrage, un port
# qu'on n'arrive pas à ouvrir passe en tête de mort sans boîte de message, la raison dans la barre d'état ; « Open Port » demandé garde la boîte ;
# « Close Port » rend l'état fermé.
import sys
from gtktest import *

e = Env('ouverture_silencieuse')
absent = e.project_path('tty-absent')
e.write_config(e.general() + e.profile())
e.start('--port', absent, port=False)
check(wait_until(lambda: e.sessions() and e.sessions()[0]['activity'] == 'dead', 5), 'echec au demarrage : tete de mort (%r)' % e.sessions())
check(wait_until(lambda: e.find(name='status-activity', css='activity-dead'), 3), 'indicateur de la barre d etat en tete de mort')
flash = e.find1(name='status-flash', mapped=None)
check(absent in (flash.get('label') or ''), 'raison dans la barre d etat : %r' % flash.get('label'))
check(not e.alerts(), 'aucune boite au demarrage : %r' % e.alerts())
# Ouverture demandée : la boîte reste
e.action('open-port')
check(wait_until(lambda: bool(e.alerts()), 5), 'Open Port : boite de message')
e.close_alert('OK')
check(e.sessions()[0]['activity'] == 'dead', 'toujours en tete de mort apres l echec demande')
# Fermeture demandée : plus de tête de mort
e.action('close-port')
check(wait_until(lambda: e.sessions()[0]['activity'] == 'closed', 3), 'Close Port : ferme (%r)' % e.sessions()[0]['activity'])
e.finish()
sys.exit(conclude())
