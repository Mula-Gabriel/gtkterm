# Perte de liaison : barre d'état « — lost » puis « — reconnecting… », aucune boîte de dialogue, retour à l'état ouvert ; titre à jour.
import sys
import time
from gi.repository import GLib
from gtktest import *


def titre(e):
    return e.title()


# 1. Reconnexion automatique active
e = Env('lien_perdu')
port = e.use_link()
e.write_config(e.general() + e.profile(port=port, autoreconnect_enabled='true'))
e.start(port=False, connect_browser=False)
base = '%s  115200-8-N-1' % port
check(wait_until(lambda: e.status_text() == base, 5), 'ouvert au demarrage (chemin complet) : %r' % e.status_text())
check(e.state('auto-reconnect') is True, 'option du profil reportee sur app.auto-reconnect')
check(wait_until(lambda: titre(e) == 'GTKTerm - ' + base, 3), 'titre de fenetre : %r' % titre(e))
e.unplug()
check(wait_until(lambda: e.status_text().endswith(' — reconnecting…'), 5), 'reconnexion affichee : %r' % e.status_text())
check(e.alerts() == [], 'aucune boite de dialogue a la perte')
time.sleep(4.5)
check(e.alerts() == [], 'aucune boite de dialogue pendant les essais')
e.replug()
check(wait_until(lambda: e.status_text() == base, 6), 'retour a l etat ouvert : %r' % e.status_text())
check(wait_until(lambda: titre(e) == 'GTKTerm - ' + base, 3), 'titre de fenetre a jour : %r' % titre(e))
e.write_pty(b'retour\r\n')
check(wait_until(lambda: 'retour' in e.terminal_text(), 3), 'reception apres reconnexion')
e.finish()

# 2. Sans reconnexion automatique, puis option activée par le menu pendant la perte
e = Env('lien_perdu_sans')
port = e.use_link()
e.write_config(e.general() + e.profile(port=port))
e.start(port=False, connect_browser=False)
base = '%s  115200-8-N-1' % port
check(wait_until(lambda: e.status_text() == base, 5), 'ouvert au demarrage : %r' % e.status_text())
e.unplug()
check(wait_until(lambda: e.status_text().endswith(' — lost'), 5), 'perte affichee : %r' % e.status_text())
time.sleep(2.5)
check(e.status_text().endswith(' — lost'), 'pas de reconnexion sans l option : %r' % e.status_text())
check(e.alerts() == [], 'aucune boite de dialogue')
e.replug()
e.set_state('auto-reconnect', GLib.Variant('b', True))
check(wait_until(lambda: e.status_text() == base, 6), 'option activee pendant la perte : reconnecte : %r' % e.status_text())
e.finish()
sys.exit(conclude())
