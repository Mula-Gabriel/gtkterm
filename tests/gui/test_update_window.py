# Fenêtre « Updates » (mise à jour par versions taguées) : versions servies par le faux git, ouverture au démarrage seulement pour une version
# plus récente non ignorée, notes de NEWS (avertissements en tête, retour en arrière), bouton principal selon le sens, confirmation quand il y a des
# avertissements, « Skip this version » enregistré, script lancé avec le tag, échec de la source, fermeture pendant une actualisation.
from gtktest import *

DESCRIBE = local_describe()
URL = "file:///srv/dépôt l'essai/gtkterm.git"
PREFIX = '/opt/gtkterm essai'
NOUVELLE, MILIEU, INTERMEDIAIRE, ANCIENNE = 'v99.0.0', 'v98.0.0', 'v0.5.0', 'v0.0.1'
TAGS = [ANCIENNE, INTERMEDIAIRE, 'essai', MILIEU, NOUVELLE]
NEWS = ('# Nouveautés\n\n## v99.0.0 — 2099-01-01\n- Nouveauté lointaine\n! Avertissement lointain\n  sur deux lignes\n\n'
        '## v98.0.0 — 2098-01-01\n- Nouveauté intermédiaire\n\n## v0.5.0 — 2001-01-01\n- Ancienne intermédiaire\n\n## v0.0.1 — 2000-01-01\n- Toute première\n')

e = Env('update_window')
check(DESCRIBE != 'unknown', 'GIT_DESCRIBE construit connu', DESCRIBE)
e.write_config(e.general(update_check_startup='true', update_prefix=PREFIX) + e.profile())


def rc():
    return e.read_config()


def git_calls(cmd):
    return [a for a in e.fake_calls('git') if cmd in a]


def resultat():
    return e.find1(name='update-result').get('label') or ''


def libelle(name):
    w = e.find(name=name)
    return (w[0].get('label') or '') if w else None


def notes(css=None):
    """Libellés visibles de la fenêtre « Updates » (dont ceux du panneau de notes), filtrés par classe CSS."""
    nat = fenetre_native()
    return [w.get('label') or '' for w in e.dump() if w.get('type') == 'GtkLabel' and w.get('mapped') and w.get('native') == nat and w.get('label')
            and (css is None or css in (w.get('css') or []))]


def fenetre_native():
    f = e.find(name='update-window')
    return f[0]['native'] if f else None


def fenetre_ouverte():
    return bool(e.find(name='update-window'))


def fenetre_fermee():
    return wait_until(lambda: not fenetre_ouverte(), 5)


def rouvrir():
    """Rouvre la fenêtre (fermée par un lancement réussi) et attend la liste des versions."""
    e.action('updates')
    return check(wait_until(lambda: libelle('update-install') == 'Update to ' + NOUVELLE, 10), 'fenetre rouverte, liste chargee', libelle('update-install'))


def sensible(name):
    w = e.find(name=name)
    return bool(w) and w[0].get('sensitive', True)


def choisir(texte):
    e.set_text('update-version', texte)


# 1. Démarrage : version plus récente → la fenêtre s'ouvre seule, plus récente choisie, avertissements en tête (une seule fois)
e.fake_git(tags=TAGS, news=NEWS)
e.start()
check(wait_until(fenetre_ouverte, 10), 'fenetre ouverte au demarrage')
clones = git_calls('clone')
check(bool(clones) and clones[0][-3:-1] == ['--', 'https://invalid.example/none.git'] and '--filter=tree:0' in clones[0] and '--bare' in clones[0], 'clone nu filtre de l URL', clones)
check(wait_until(lambda: libelle('update-install') == 'Update to ' + NOUVELLE, 5), 'bouton : Update to', libelle('update-install'))
avert = notes('update-warning')
check(any('Avertissement lointain sur deux lignes' in t for t in avert), 'avertissement de NEWS affiche (prolongement joint)', avert)
check(any(t.startswith('v99.0.0 — 2099-01-01') for t in notes('heading')), 'titre de section', notes('heading'))
check('Warnings' in notes('update-warning') and 'Warnings' in notes('heading'), 'en-tete Warnings : classes update-warning et heading', notes('update-warning'))
tous = notes()
check(sum('Avertissement lointain' in t for t in tous) == 1 and not any(t.startswith('⚠') for t in tous), 'avertissement affiche une seule fois (en tete, pas repete dans sa section)', tous)
check(any(t.startswith('v99.0.0 — Avertissement lointain') for t in avert), 'avertissement en tete prefixe de sa version', avert)
check(e.find(name='update-skip') != [], 'Skip visible pour une version plus recente')
check(NOUVELLE in resultat(), 'resultat : nouvelle version', resultat())
check(bool(e.find1(name='update-install').get('focus')), 'focus initial sur Install (pas sur l etiquette de la version installee)')

# 2. « Skip this version » : tag enregistré, fenêtre fermée
e.activate('update-skip')
check(fenetre_fermee(), 'fenetre fermee par Skip')
check(wait_until(lambda: 'update_skipped_version=%s' % NOUVELLE in rc(), 5), 'version ignoree enregistree', rc())

# 3. Redémarrage : version ignorée → aucune fenêtre au démarrage
e.stop()
n = len(git_calls('fetch'))
e.start()
check(wait_until(lambda: len(git_calls('fetch')) > n, 10), 'actualisation au demarrage (cache present : fetch)')
time.sleep(1.5)
check(not fenetre_ouverte(), 'version ignoree : pas de fenetre au demarrage')

# 3b. Version installée = la plus récente (seules des versions plus anciennes sont servies) : aucune fenêtre au démarrage, même sans version ignorée
e.stop()
e.write_config(e.general(update_check_startup='true', update_prefix=PREFIX) + e.profile())
e.fake_git(tags=[ANCIENNE, INTERMEDIAIRE], news=NEWS)
n = len(git_calls('fetch'))
e.start()
check(wait_until(lambda: len(git_calls('fetch')) > n, 10), 'actualisation au demarrage (versions plus anciennes seulement)')
time.sleep(1.5)
check(not fenetre_ouverte(), 'version installee la plus recente : pas de fenetre au demarrage')
e.stop()
e.fake_git(tags=TAGS, news=NEWS)
e.start()
check(wait_until(lambda: e.sessions(), 10), 'GTKTerm relance')

# 4. Ouverture à la main : actualisation automatique ; retour en arrière vers la plus ancienne
avant = rc()
e.action('updates')
check(wait_until(lambda: libelle('update-install') == 'Update to ' + NOUVELLE, 10), 'fenetre ouverte a la main, liste chargee', libelle('update-install'))
choisir(ANCIENNE)
check(wait_until(lambda: libelle('update-install') == 'Roll back to ' + ANCIENNE, 5), 'bouton : Roll back to', libelle('update-install'))
# Retour : sections des versions quittées (v0.5.0 entre v0.0.1 et l'installée), pas celle de la cible
check(any('You are leaving these versions:' in t for t in notes()), 'en-tete du retour en arriere', notes())
check(any(t.startswith('v0.5.0 — 2001-01-01') for t in notes('heading')), 'section de la version quittee', notes('heading'))
check(not any('Toute première' in t for t in notes()), 'section de la cible absente', notes())
check(not e.find(name='update-skip'), 'Skip masque pour un retour en arriere')
choisir(MILIEU)
check(wait_until(lambda: libelle('update-install') == 'Update to ' + MILIEU, 5), 'bouton : Update to (version intermediaire)', libelle('update-install'))
check(not e.find(name='update-skip'), 'Skip masque pour une version plus recente qui n est pas la derniere')
check(rc() == avant, 'ouvrir la fenetre n ecrit rien')

# 5. Lancement : retour sans avertissement (la section quittée n'en a pas) → script lancé directement, avec le tag
if not check(os.access(os.path.join(FAKEBIN, 'xterm'), os.X_OK), 'faux xterm en place : aucun vrai terminal ne sera lance'):
    e.finish()
    sys.exit(conclude())
e.set_text('update-url', URL)
e.activate('update-check')
check(wait_until(lambda: libelle('update-install') is not None and NOUVELLE in resultat(), 10), 'actualisation sur l URL affichee', resultat())
check('update_url=%s' % URL in rc(), 'URL affichee enregistree')
check(git_calls('set-url')[-1][-2:] == ['--', URL], 'remote set-url -- <url> en un seul argument', git_calls('set-url')[-1:])
choisir(ANCIENNE)
wait_until(lambda: libelle('update-install') == 'Roll back to ' + ANCIENNE, 5)
e.activate('update-install')
check(bool(wait_until(lambda: e.fake_calls('xterm'), 5)), 'terminal lance sans confirmation')
args = (e.fake_calls('xterm') or [[]])[-1]
check(len(args) >= 8 and args[-8:-6] == ['sh', '-c'], 'commande sh -c en tete', args)
check(args[-5].endswith('/gtkterm-update.sh') and args[-4] == URL and args[-3] == PREFIX and args[-2].isdigit() and args[-1] == ANCIENNE, 'arguments du script intacts', args)
check(wait_until(lambda: any('The update is running in a terminal window.' in t for t in e.alerts()), 5), 'message de lancement', e.alerts())
e.close_alert()
check(fenetre_fermee(), 'fenetre Updates fermee apres le lancement')
rouvrir()

# 6. Montée avec avertissements : confirmation ; Annuler ne lance rien, Installer lance avec le tag
choisir(NOUVELLE + ' — latest')
wait_until(lambda: libelle('update-install') == 'Update to ' + NOUVELLE, 5)
n = len(e.fake_calls('xterm'))
e.activate('update-install')
check(wait_until(lambda: any('Install %s?' % NOUVELLE in t for t in e.alerts()), 5), 'confirmation demandee', e.alerts())
e.close_alert('Cancel')
time.sleep(0.5)
check(len(e.fake_calls('xterm')) == n, 'Annuler : rien n est lance')
e.activate('update-install')
check(wait_until(lambda: any('Install %s?' % NOUVELLE in t for t in e.alerts()), 5), 'confirmation redemandee')
e.close_alert('Install')
check(wait_until(lambda: len(e.fake_calls('xterm')) > n, 5), 'Installer : terminal lance')
check(e.fake_calls('xterm')[-1][-1] == NOUVELLE, 'tag de la version choisie', e.fake_calls('xterm')[-1][-1:])
check(wait_until(lambda: any('The update is running in a terminal window.' in t for t in e.alerts()), 5), 'message de lancement', e.alerts())
e.close_alert()
check(fenetre_fermee(), 'fenetre Updates fermee apres le lancement (Installer)')
rouvrir()

# 6b. Projet modifié (aucun avertissement dans les notes d'un retour en arrière) : la confirmation est demandée quand même ; Annuler ne lance rien
save_project_as(e, e.project_path('banc'))
e.set_state('local-echo', GLib.Variant('b', True))
check(wait_until(lambda: e.project().get('modified') is True, 5), 'projet modifie', e.project())
choisir(ANCIENNE)
check(wait_until(lambda: libelle('update-install') == 'Roll back to ' + ANCIENNE, 5), 'bouton : Roll back to (projet modifie)', libelle('update-install'))
n = len(e.fake_calls('xterm'))
e.activate('update-install')
check(wait_until(lambda: any('Install %s?' % ANCIENNE in t and 'GTKTerm will be closed and restarted' in t and 'See the warnings' not in t for t in e.alerts()), 5),
      'confirmation (projet modifie, sans avertissement)', e.alerts())
e.close_alert('Cancel')
time.sleep(0.5)
check(len(e.fake_calls('xterm')) == n, 'projet modifie, Annuler : rien n est lance')
e.activate('update-install')
check(wait_until(lambda: any('Install %s?' % ANCIENNE in t for t in e.alerts()), 5), 'confirmation redemandee (projet modifie)')
e.close_alert('Install')
check(wait_until(lambda: len(e.fake_calls('xterm')) > n, 5), 'projet modifie, Installer : terminal lance')
check(wait_until(lambda: any('GTKTerm will be closed and restarted when it finishes.' in t for t in e.alerts()), 5), 'message de lancement mis a jour', e.alerts())
e.close_alert()
check(fenetre_fermee(), 'fenetre Updates fermee apres le lancement (projet modifie)')
e.set_state('local-echo', GLib.Variant('b', False))
check(wait_until(lambda: e.project().get('modified') is False, 5), 'projet de nouveau non modifie', e.project())
rouvrir()

# 6c. Version installée proposée dans la liste : « Reinstall vX » et « This version is installed. » (seulement si GIT_DESCRIBE est une étiquette exacte)
if re.match(r'^v\d+\.\d+\.\d+$', DESCRIBE):
    e.fake_git(tags=TAGS + [DESCRIBE], news=NEWS)
    n = len(git_calls('fetch'))
    e.activate('update-check')
    wait_until(lambda: len(git_calls('fetch')) > n, 10)
    wait_until(lambda: sensible('update-check') and sensible('update-version'), 10)
    choisir(DESCRIBE + ' — installed')
    check(wait_until(lambda: libelle('update-install') == 'Reinstall ' + DESCRIBE, 5), 'bouton : Reinstall', libelle('update-install'))
    check(any('This version is installed.' in t for t in notes()), 'note : This version is installed.', notes())
    e.fake_git(tags=TAGS, news=NEWS)
else:
    log('6c ignore : GIT_DESCRIBE %s n est pas une etiquette exacte vX.Y.Z (Reinstall et « This version is installed. » non verifiables ici)' % DESCRIBE)

# 7. Source injoignable : message dans la fenêtre, bouton réactivé
e.fake_git(tags=None)
e.set_text('update-url', URL + '.autre')
e.activate('update-check')
check(wait_until(lambda: 'Could not check for updates' in resultat(), 10), 'echec de la source affiche', resultat())
choisir(MILIEU)
time.sleep(0.5)
check(bool(e.find1(name='update-check').get('focus')), 'apres l echec : focus sur Check now')
check(not sensible('update-install'), 'apres l echec avec une autre URL : changer de version ne reactive pas Install')
check(sensible('update-check') and sensible('update-url'), 'apres l echec : Check now et URL sensibles')

# 8. Case « au démarrage » : enregistrée à chaque changement
e.activate('update-startup')
check(wait_until(lambda: 'update_check_startup=false' in rc(), 5), 'case decochee enregistree', rc())
e.activate('update-startup')
check(wait_until(lambda: 'update_check_startup=true' in rc(), 5), 'case recochee enregistree', rc())

# 9. Fermeture pendant une actualisation : annulée, sans effet ni erreur (stderr vérifié par finish)
e.fake_git(tags=TAGS, news=NEWS, delay=3)
choisir(NOUVELLE + ' — latest')
check(wait_until(lambda: e.find(name='update-skip') != [], 3), 'avant l actualisation : Skip visible pour la derniere version')
n = len(git_calls('fetch'))
e.activate('update-check')
check(wait_until(lambda: len(git_calls('fetch')) > n, 5), 'actualisation lancee')
check(e.find(name='update-skip') == [], 'pendant l actualisation : Skip masque')
check(not sensible('update-check') and not sensible('update-url') and not sensible('update-install'), 'pendant l actualisation : Check now, URL et Install insensibles')
choisir(MILIEU)
time.sleep(0.5)
check(not sensible('update-install'), 'pendant l actualisation : changer de version ne reactive pas Install')
e.activate('update-close')
check(fenetre_fermee(), 'fenetre fermee pendant l actualisation')
time.sleep(4)
check(not fenetre_ouverte() and not e.alerts(), 'actualisation annulee : ni fenetre recreee ni boite', e.alerts())

# 10. Fermeture de GTKTerm (app.quit) fenêtre ouverte : code 0, stderr propre (vérifiés par finish)
e.fake_git(tags=TAGS, news=NEWS)
e.action('updates')
check(wait_until(fenetre_ouverte, 5), 'fenetre ouverte avant la fermeture')
e.finish()
sys.exit(conclude())
