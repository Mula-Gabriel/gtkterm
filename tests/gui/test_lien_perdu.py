# Perte de liaison : barre d'état « — lost » puis « — reconnecting… », aucune boîte de dialogue, retour à l'état ouvert.
from gtktest import *

# 1. Reconnexion automatique active
e = Env('lien_perdu')
port = e.use_link()
e.write_rc(e.profile(port=port, autoreconnect_enabled='True'))
e.start(force_port=False); time.sleep(2)
base = '%s  115200-8-N-1' % port
check(wait_until(lambda: status_text(e.app) == base, 5), 'ouvert au demarrage (chemin complet)', status_text(e.app))
e.unplug()
check(wait_until(lambda: status_text(e.app).endswith(' — reconnecting…'), 5), 'reconnexion affichee', status_text(e.app))
check(alerts(e.app) == [], 'aucune boite de dialogue a la perte')
time.sleep(4.5)
check(alerts(e.app) == [], 'aucune boite de dialogue pendant les essais')
e.replug()
check(wait_until(lambda: status_text(e.app) == base, 6), 'retour a l etat ouvert', status_text(e.app))
check(wait_until(lambda: name(find1(e.app, 'frame')) == 'GTKTerm - ' + base, 3), 'titre de fenetre a jour', name(find1(e.app, 'frame')))
e.write_pty(b'retour\r\n')
check(wait_until(lambda: 'retour' in terminal_text(e.app), 3), 'reception apres reconnexion')
finish(e)

# 2. Sans reconnexion automatique
e = Env('lien_perdu_sans')
port = e.use_link()
e.write_rc(e.profile(port=port))
e.start(force_port=False); time.sleep(2)
e.unplug()
check(wait_until(lambda: status_text(e.app).endswith(' — lost'), 5), 'perte affichee', status_text(e.app))
time.sleep(2.5)
check(status_text(e.app).endswith(' — lost'), 'pas de reconnexion sans l option', status_text(e.app))
check(alerts(e.app) == [], 'aucune boite de dialogue')
finish(e)
sys.exit(conclude())
