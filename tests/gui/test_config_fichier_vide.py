# Fichier de configuration vide, ou sans aucun profil (seulement [window]) : traité comme un premier lancement (F5),
# [profile:default] créé avec les défauts, pas de « No section "default" ».
from gtktest import *


def premier_lancement(e, label):
    e.start(); time.sleep(2)
    q = wait_until(lambda: alerts(e.app), 5)
    texte = ' '.join(get_text(t) or name(t) for t in find(q[0], 'label')) if q else ''
    check('has been created' in texte and 'No section' not in texte, label + ' : message de premier lancement', texte)
    if q:
        act(find1(q[0], 'push button', '^_?OK$'))
        check(wait_until(lambda: not alerts(e.app), 3), label + ' : message ferme')
    contenu = open(e.rc).read()
    check('[profile:default]' in contenu, label + ' : profil default cree', contenu[:300])
    return contenu


# Un environnement (et un broadwayd) par lancement : broadwayd ne survit pas toujours à un client tué
e = Env('config_fichier_vide')
e.write_rc('')
premier_lancement(e, 'fichier vide')
finish(e)

e = Env('config_fichier_sans_profil')
e.write_rc('[window]\nwidth=700\n')
contenu = premier_lancement(e, 'fichier sans profil')
check('width=700' in contenu, 'groupe [window] conserve', contenu)
check(alerts(e.app) == [], 'aucune autre boite')
finish(e)
sys.exit(conclude())
