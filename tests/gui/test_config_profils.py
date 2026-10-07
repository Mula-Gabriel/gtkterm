# Profils par les dialogues : nom vide refusé, enregistrer sous un nouveau nom, charger un autre profil, remplacer un profil existant
# (nom saisi avec une autre casse, confirmation), recharger, supprimer ; profil ajouté par une autre instance conservé.
from gtktest import *

def ouvrir_liste(e, bw, term, titre):
    menu_key(bw, e.app, 'Configuration', titre, focus_obj=term)
    w = wait_window(e.app, titre)
    time.sleep(0.5)
    return w

def lignes(w):
    tbl = [t for t in find(w, None) if role(t) in ('tree table', 'table')][0]
    return tbl, [name(Atspi.Table.get_accessible_at(tbl, r, 0)) or get_text(Atspi.Table.get_accessible_at(tbl, r, 0)) for r in range(Atspi.Table.get_n_rows(tbl))]

e = Env('config_profils')
# « autre » (9600 bauds) sert à vérifier qu'un chargement change vraiment le profil en cours
e.write_rc(e.profile(echo='True') + e.profile(name='autre', speed=9600))
e.start(); time.sleep(2)
bw = Broadway(e.display)
term = find1(e.app, 'terminal')

# 0. nom vide : refusé, pas de groupe [profile:] (F6)
menu_key(bw, e.app, 'Configuration', 'Save configuration', focus_obj=term)
w = wait_window(e.app, 'Save configuration')
set_text(find1(w, 'text', showing=True), ''); time.sleep(0.3)
bw.click_obj(find1(w, 'push button', '^_?OK$'))
q = wait_until(lambda: alerts(e.app), 5)
check(bool(q) and 'cannot be empty' in ' '.join(get_text(t) or name(t) for t in find(q[0], 'label')), 'nom vide refuse')
if q:
    act(find1(q[0], 'push button', '^_?OK$'))
    check(wait_until(lambda: not alerts(e.app), 3), 'message ferme')
check('[profile:]' not in open(e.rc).read(), 'pas de profil sans nom', open(e.rc).read()[:300])

# 1. enregistrer sous « atelier »
menu_key(bw, e.app, 'Configuration', 'Save configuration', focus_obj=term)
w = wait_window(e.app, 'Save configuration')
ent = find1(w, 'text', showing=True)
set_text(ent, 'atelier'); time.sleep(0.3)
bw.click_obj(find1(w, 'push button', '^_?OK$')); time.sleep(1.0)
contenu = open(e.rc).read()
check('[profile:atelier]' in contenu, 'profil atelier enregistre', contenu[:400])
check('[profile:default]' in contenu, 'profil default conserve')

# 2. charger « autre » (9600)
w = ouvrir_liste(e, bw, term, 'Load configuration')
tbl, noms = lignes(w)
check(noms == ['atelier', 'autre', 'default'], 'liste des profils', noms)
Atspi.Table.add_row_selection(tbl, noms.index('autre')); time.sleep(0.3)
bw.click_obj(find1(w, 'push button', '^_?OK$')); time.sleep(1.5)
check(wait_until(lambda: status_text(e.app) == '%s  9600-8-N-1' % e.pty, 5), 'profil autre charge', status_text(e.app))

# 3. enregistrer sous « Default » : la comparaison sans casse propose de remplacer « default » (C6) ; confirmation par gtk_dialog_run
menu_key(bw, e.app, 'Configuration', 'Save configuration', focus_obj=term)
w = wait_window(e.app, 'Save configuration')
set_text(find1(w, 'text', showing=True), 'Default'); time.sleep(0.3)
bw.click_obj(find1(w, 'push button', '^_?OK$'))
q = wait_until(lambda: alerts(e.app), 5)
check(bool(q) and 'already exists' in ' '.join(get_text(t) or name(t) for t in find(q[0], 'label')), 'confirmation de remplacement demandee')
if q:
    bw.click_obj(find1(q[0], 'push button', '^_?Yes$')); time.sleep(1.0)
contenu = open(e.rc).read()
defaut = contenu.split('[profile:default]')[1].split('\n[')[0] if '[profile:default]' in contenu else ''
check('[profile:Default]' not in contenu and 'speed=9600' in defaut, 'profil default remplace (sans doublon de casse)', contenu)

# 4. recharger « atelier » (115200)
w = ouvrir_liste(e, bw, term, 'Load configuration')
tbl, noms = lignes(w)
Atspi.Table.add_row_selection(tbl, noms.index('atelier')); time.sleep(0.3)
bw.click_obj(find1(w, 'push button', '^_?OK$')); time.sleep(1.5)
check(wait_until(lambda: status_text(e.app) == '%s  115200-8-N-1' % e.pty, 5), 'profil atelier recharge', status_text(e.app))

# 5. supprimer « atelier »
w = ouvrir_liste(e, bw, term, 'Delete configuration')
tbl, noms = lignes(w)
Atspi.Table.add_row_selection(tbl, noms.index('atelier')); time.sleep(0.3)
bw.click_obj(find1(w, 'push button', '^_?OK$')); time.sleep(1.0)
contenu = open(e.rc).read()
check('[profile:atelier]' not in contenu and '[profile:default]' in contenu, 'profil atelier supprime', contenu[:400])
check(alerts(e.app) == [], 'aucune boite d erreur')
# une autre instance (ou une modification a la main) ajoute un profil : la liste le montre, la fermeture ne l'efface pas (F1)
with open(e.rc, 'a') as f:
    f.write('\n[profile:externe]\nport=/dev/null\nspeed=4800\n')
w = ouvrir_liste(e, bw, term, 'Load configuration')
tbl, noms = lignes(w)
check('externe' in noms, 'profil d une autre instance dans la liste', noms)
bw.click_obj(find1(w, 'push button', '^_?Cancel$')); time.sleep(0.8)
# la fermeture par la barre de titre enregistre l'etat de la fenetre : le profil actif supprime n'est pas recree
act(find1(find1(e.app, 'frame'), 'push button', '^Close$', showing=True))
try:
    e.proc.wait(10)
except subprocess.TimeoutExpired:
    pass
contenu = open(e.rc).read()
check('[window]' in contenu, 'etat de la fenetre enregistre a la fermeture', contenu)
check('[profile:atelier]' not in contenu and '[profile:autre]' in contenu, 'pas de profil fantome a la fermeture', contenu)
check('[profile:externe]' in contenu and 'speed=4800' in contenu, 'profil d une autre instance conserve a la fermeture', contenu)
finish(e, quit=False)
sys.exit(conclude())
