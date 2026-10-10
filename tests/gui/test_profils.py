# Profils par les fenêtres GTK 4 (§6.4, Review Focus 5) : nom vide et nom réservé refusés dans la fenêtre, nom non ASCII avec espaces enregistré puis
# relu, chargement d'un autre profil (9600), remplacement de « default » par « Default » après confirmation, Entrée sur une ligne = OK, suppression
# confirmée (Annuler d'abord), profil ajouté par une autre instance visible et conservé à la fermeture.
from gtktest import *

NOM = 'atelier é'


def ouvrir(action):
    e.action(action)
    return e.find1(name='profile-window')


def lignes():
    return sorted(w['label'] for w in e.find(name='profile-row'))


def choisir(nom):
    """Vrai clic (broadway) sur la ligne du profil : la sélection et le focus clavier suivent le clic."""
    e.bw.click(e.find1(name='profile-row', label=nom))
    time.sleep(0.3)


def fermee():
    return wait_until(lambda: not e.find(name='profile-window'), 5)


def erreur():
    w = e.find(name='profile-error')
    return w[0]['label'] if w else ''


def alertes():
    return ' '.join(str(a) for a in e.alerts())


def rc():
    with open(e.config_path, encoding='utf-8') as f:
        return f.read()


def groupe(contenu, nom):
    """Corps du groupe [profile:nom], jusqu'au groupe suivant."""
    tete = '[profile:%s]' % nom
    return contenu.split(tete, 1)[1].split('\n[', 1)[0] if tete in contenu else ''


e = Env('profils')
e.write_config(e.general() + e.profile(echo='true') + e.profile('autre', speed=9600))
e.start(port=False)
base = '%s  115200-8-N-1' % e.port
check(wait_until(lambda: e.status_text() == base, 5), 'profil default ouvert')

# 0. nom vide (blancs seulement) puis nom réservé : refusés dans la fenêtre, sans boîte, rien n'est écrit
ouvrir('save-profile')
check(e.find1(name='profile-name')['label'] == 'default', 'nom prerempli avec le profil actif')
e.set_text('profile-name', '   ')
e.activate('profile-ok')
check(wait_until(lambda: erreur() != '', 3), 'nom vide refuse dans profile-error')
e.set_text('profile-name', 'Window')
check(wait_until(lambda: erreur() == '', 3), 'erreur effacee a la frappe')
e.activate('profile-ok')
check(wait_until(lambda: erreur() != '', 3), 'nom reserve Window refuse')
check(e.find(name='profile-window') != [], 'fenetre toujours ouverte apres un refus')
check(e.alerts() == [], 'refus sans boite de message')
contenu = rc()
check('[profile:]' not in contenu and '[profile:   ]' not in contenu and '[profile:Window]' not in contenu, 'aucun groupe pour un nom refuse')

# 1. « atelier é » (espaces autour rognés) enregistré tel quel
e.set_text('profile-name', ' %s ' % NOM)
e.activate('profile-ok')
check(fermee(), 'fenetre fermee apres enregistrement')
contenu = rc()
check('[profile:%s]' % NOM in contenu, 'profil non ASCII enregistre')
check('[profile:default]' in contenu, 'profil default conserve')

# 2. charger « autre » (9600) : liste relue, sélection par clic, OK
ouvrir('load-profile')
check(lignes() == sorted([NOM, 'autre', 'default']), 'liste des profils')
choisir('autre')
e.activate('profile-ok')
check(fermee(), 'fenetre de chargement fermee')
attendu = '%s  9600-8-N-1' % e.port
check(wait_until(lambda: e.status_text() == attendu, 5), 'profil autre charge, port rouvert a 9600')

# 3. « Default » : la comparaison sans casse demande de remplacer « default » (C6)
ouvrir('save-profile')
check(e.find1(name='profile-name')['label'] == 'autre', 'nom prerempli avec le profil charge')
e.set_text('profile-name', 'Default')
e.activate('profile-ok')
check(wait_until(lambda: 'already exists' in alertes(), 5), 'confirmation de remplacement demandee')
e.close_alert('Overwrite')
check(fermee(), 'fenetre fermee apres remplacement')
contenu = rc()
check('[profile:Default]' not in contenu and 'speed=9600' in groupe(contenu, 'default'), 'default remplace sans doublon de casse')

# 4. recharger « atelier é » par Entrée sur la ligne (activate de la GtkListView)
ouvrir('load-profile')
choisir(NOM)
e.bw.key('Return')
check(fermee(), 'Entree sur une ligne = OK')
check(wait_until(lambda: e.status_text() == base, 5), 'profil non ASCII relu (115200)')

# 5. supprimer « atelier é » : Annuler la confirmation garde le profil, la confirmer le retire
ouvrir('delete-profile')
choisir(NOM)
e.activate('profile-ok')
check(wait_until(lambda: NOM in alertes(), 5), 'confirmation de suppression nommant le profil')
e.close_alert('Cancel')
check(wait_until(lambda: e.alerts() == [], 3) and '[profile:%s]' % NOM in rc(), 'Annuler garde le profil')
e.activate('profile-ok')
check(wait_until(lambda: e.alerts() != [], 5), 'confirmation redemandee')
e.close_alert('Delete')
check(fermee(), 'fenetre de suppression fermee')
contenu = rc()
check('[profile:%s]' % NOM not in contenu and '[profile:default]' in contenu, 'profil supprime')
check(e.alerts() == [], 'aucune boite d erreur')

# 6. une autre instance (ou une modification à la main) ajoute un profil : la liste le montre (F1)
with open(e.config_path, 'a', encoding='utf-8') as f:
    f.write('\n[profile:externe]\nport=/dev/null\nspeed=4800\n')
ouvrir('load-profile')
check('externe' in lignes(), 'profil d une autre instance dans la liste')
e.activate('profile-cancel')
check(fermee(), 'Cancel ferme la fenetre')

# 7. fermeture : [window] enregistré, pas de profil fantôme (le profil actif a été supprimé), profil externe conservé
e.finish()
contenu = rc()
check('[window]' in contenu, 'etat de la fenetre enregistre a la fermeture')
check('[profile:%s]' % NOM not in contenu and '[profile:autre]' in contenu, 'pas de profil fantome a la fermeture')
check('[profile:externe]' in contenu and 'speed=4800' in groupe(contenu, 'externe'), 'profil d une autre instance conserve')
sys.exit(conclude())
