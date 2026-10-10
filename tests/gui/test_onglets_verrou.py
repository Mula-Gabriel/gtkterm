# Même port série dans un second onglet : refusé par le verrou du transport (boîte d'erreur open-failed, préfixée du nom de l'onglet), l'onglet reste,
# port fermé.
import os, sys
from gtktest import *

e = Env('onglets_verrou')
e.write_config(e.general() + e.profile())
e.start()
e.action('new-tab')
e.find1(name='port-dialog')
e.activate('port-ok')	# prérempli : même port que l'onglet actif
check(wait_until(lambda: len(e.sessions()) == 2), 'second onglet cree')
check(wait_until(lambda: e.alerts(), 5), 'boite d erreur : port deja tenu')
check(any(e.port in a or 'lock' in a.lower() for a in e.alerts()), 'message cite le port ou le verrou', e.alerts())
check(any(os.path.basename(e.port) + ': ' in a for a in e.alerts()), 'message prefixe du nom court de l onglet', e.alerts())
e.close_alert('OK')
check(e.sessions()[1]['state'] == 'closed', 'second onglet : port ferme')
check(e.sessions()[0]['state'] == 'open', 'premier onglet : port toujours ouvert')
e.finish()
sys.exit(conclude())
