# Les options de ligne de commande ne sont pas enregistrées par une écriture partielle (fin de M10) : charger puis enregistrer un fichier de macros
# par le sélecteur GTK 4 n'écrit que macros_file dans le profil actif. (Le bouton « M2 » du panneau est vérifié par les scénarios du panneau de macros.)
from gtktest import *


def rc():
    with open(e.config_path, encoding='utf-8') as f:
        return f.read()


e = Env('config_ligne_commande')
dossier = os.path.dirname(e.config_path)
autre = os.path.join(dossier, 'port-du-profil')
os.symlink(e.port, autre)
ma = os.path.join(dossier, 'macros2.ini')
with open(ma, 'w', encoding='utf-8') as f:
    f.write(macros_v2_text('M2'))
e.write_config(e.general() + e.profile(port=autre))
e.start()                                      # --port <pty> : le port de la ligne de commande
check(wait_until(lambda: e.status_text().startswith(e.port + ' '), 5), 'port de la ligne de commande utilise')

# 1. charger un fichier de macros : seul macros_file est écrit dans le profil actif
e.action('load-macros')
file_dialog(e, 'Load macros file', ma)
check(wait_until(lambda: 'macros_file=%s' % ma in rc(), 5), 'chemin du fichier de macros enregistre')
check('port=%s' % autre in rc(), 'le profil garde son port (pas celui de --port)')
check(e.alerts() == [], 'chargement sans message')

# 2. enregistrer sous un autre nom (avec une espace) : fichier écrit, macros_file suit
copie = os.path.join(dossier, 'copie macros.ini')
e.action('save-macros')
file_dialog(e, 'Save macros file', copie)
check(wait_until(lambda: os.path.exists(copie), 5), 'fichier de macros enregistre')
check('label=M2' in open(copie, encoding='utf-8').read(), 'macro M2 dans la copie')
check(wait_until(lambda: 'macros_file=%s' % copie in rc(), 5), 'macros_file suit l enregistrement')
check('port=%s' % autre in rc(), 'port du profil toujours intact')
e.finish()
check('port=%s' % autre in rc(), 'port du profil intact apres la fermeture')
sys.exit(conclude())
