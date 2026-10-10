# Review Focus 2 : fermeture de GTKTerm (app.quit, par finish) pendant un dialogue asynchrone ouvert — sélecteur de fichiers, puis confirmation
# d'écrasement de profil, puis boîte de message : code de sortie 0, stderr propre (vérifiés par finish), rien d'écrit dans le profil.
from gtktest import *


def groupe(contenu, nom):
    tete = '[profile:%s]' % nom
    return contenu.split(tete, 1)[1].split('\n[', 1)[0] if tete in contenu else ''


# Un environnement (et un gtk4-broadwayd) par lancement
e = Env('fermeture_selecteur')
e.write_config(e.general() + e.profile())
e.start(port=False, connect_browser=False)
e.action('load-macros')
e.wait_window('Load macros file', 8)
e.finish()                                     # app.quit pendant le sélecteur

e = Env('fermeture_confirmation')
e.write_config(e.general() + e.profile())
e.start(port=False, connect_browser=False)
e.action('save-profile')
e.find1(name='profile-window')
e.activate('profile-ok')                       # « default » existe : confirmation d'écrasement
check(wait_until(lambda: 'already exists' in ' '.join(str(a) for a in e.alerts()), 5), 'confirmation ouverte')
e.finish()                                     # app.quit pendant la confirmation
with open(e.config_path, encoding='utf-8') as f:
    contenu = f.read()
check('term_scrollback' not in groupe(contenu, 'default'), 'profil non enregistre (confirmation jamais acceptee)')

# Boîte de message ouverte (profil invalide : message au démarrage) : app.quit la ferme, code 0 et stderr propre (vérifiés par finish)
e = Env('fermeture_message')
e.write_config(e.general() + e.profile(bits=9))
e.start(connect_browser=False)
check(wait_until(lambda: any('Invalid number of bits' in a for a in e.alerts()), 5), 'boite de message ouverte')
e.finish()                                     # app.quit pendant la boîte de message
# Dialogue Port ouvert
e = Env('fermeture_port')
e.write_config(e.general() + e.profile())
e.start(port=False, connect_browser=False)
e.action('port')
e.find1(name='port-dialog')
e.finish()                                     # app.quit pendant le dialogue Port
sys.exit(conclude())
