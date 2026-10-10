# Fichier de configuration vide, ou sans aucun profil (seulement [window]) : traité comme un premier lancement (F5), [profile:default]
# créé avec les défauts, message « has been created » (affiché après la fenêtre), pas de « No section "default" ».
from gtktest import *


def premier_lancement(e, label):
    e.start(connect_browser=False)
    q = wait_until(lambda: e.alerts(), 5)
    texte = q[0] if q else ''
    check('has been created' in texte and 'No section' not in texte, label + ' : message de premier lancement', texte)
    if q:
        e.close_alert('OK')
        check(wait_until(lambda: not e.alerts(), 3), label + ' : message ferme')
    contenu = e.read_config()
    check('[profile:default]' in contenu, label + ' : profil default cree', contenu[:300])
    return contenu


e = Env('config_fichier_vide')
e.write_config('')
premier_lancement(e, 'fichier vide')
e.finish()

e = Env('config_fichier_sans_profil')
e.write_config('[window]\nwidth=700\n')
contenu = premier_lancement(e, 'fichier sans profil')
check('width=700' in contenu, 'groupe [window] conserve', contenu)
check(e.alerts() == [], 'aucune autre boite', e.alerts())
e.finish()
sys.exit(conclude())
