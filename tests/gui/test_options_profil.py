# Review Focus 3 : les cases du menu suivent un rechargement de profil. Écho décoché à la main puis profil qui le coche : l'état de l'action
# (Describe = ce que montre la case) et l'écho réel suivent ; même chose dans l'autre sens.
from gtktest import *
from gi.repository import GLib


def charger(nom):
    e.action('load-profile')
    e.find1(name='profile-window')
    e.bw.click(e.find1(name='profile-row', label=nom))
    e.activate('profile-ok')
    check(wait_until(lambda: not e.find(name='profile-window'), 5), 'profil %s charge' % nom)


def frappe(texte):
    """Frappe réelle dans le terminal ; rend les octets reçus par l'esclave pty."""
    e.drain()
    e.bw.click(e.find1(name='terminal'))
    e.bw.type(texte)
    return e.read_pty(1.0)


e = Env('options_profil')
e.write_config(e.general() + e.profile(echo='true') + e.profile('muet', echo='false'))
e.start(port=False)
check(wait_until(lambda: e.status_text() == '%s  115200-8-N-1' % e.port, 5), 'port ouvert')
check(e.state('local-echo') is True, 'echo du profil default au demarrage')

# 1. décoché à la main : plus d'écho
e.set_state('local-echo', GLib.Variant('b', False))
check(wait_until(lambda: e.state('local-echo') is False, 3), 'echo decoche a la main')
check(frappe('aq') == b'aq', 'frappe envoyee sur le port')
time.sleep(0.5)
check('aq' not in e.terminal_text(), 'pas d echo apres decochage')

# 2. profil avec echo=true : la case et l'écho suivent
charger('default')
check(wait_until(lambda: e.state('local-echo') is True, 3), 'case Local echo cochee par le profil')
check(frappe('zw') == b'zw', 'frappe envoyee apres chargement')
check(wait_until(lambda: 'zw' in e.terminal_text(), 3), 'echo effectif apres chargement')

# 3. sens inverse : coché à la main, profil sans écho
e.set_state('local-echo', GLib.Variant('b', True))
check(wait_until(lambda: e.state('local-echo') is True, 3), 'echo coche a la main')
charger('muet')
check(wait_until(lambda: e.state('local-echo') is False, 3), 'case Local echo decochee par le profil')
check(frappe('xk') == b'xk', 'frappe envoyee avec le profil muet')
time.sleep(0.5)
check('xk' not in e.terminal_text(), 'pas d echo avec le profil muet')
e.finish()
sys.exit(conclude())
