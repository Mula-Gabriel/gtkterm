# Macros intégrées d'un projet (sous-projet 6c, spec §3.2-5 et §3.2-6) : dans un onglet de projet, « Configure Macros » (OK), un changement de polling
# et « Load macros file… » n'écrivent aucun .ini (le projet est modifié) ; « Save macros file… » exporte sans retenir le chemin (macros_file vide) ;
# « New port… » donne au nouvel onglet une copie du jeu de macros, indépendante de celle du premier ; Close project (profil default sans macros_file)
# rend un onglet libre sur le fichier de macros par défaut, comme au démarrage, et Configure Macros y ajoute sans rien perdre.
import os, sys, time
from gi.repository import GLib
from gtktest import *

e = Env('projet_macros')
write_macros_v2(e.macros_path, [dict(label='BASE', action='<BASE>')])	# fichier par défaut : lu par le premier onglet avant d'entrer dans le projet
e.write_config(e.general() + e.profile())
e.start()
p2 = e.new_pty()
banc = e.project_path('banc.gtkterm')
export = os.path.join(e.root, 'export.ini')
charge = os.path.join(e.root, 'charge.ini')
write_macros_v2(charge, [dict(label='L1', action='<L1>', polling=True, period=200), dict(label='L2', action='<L2>')])


def inis():
    """{chemin: (st_mtime_ns, taille)} de tous les .ini sous la racine privée du scénario."""
    out = {}
    for d, _, fichiers in os.walk(e.root):
        for f in fichiers:
            if f.endswith('.ini'):
                p = os.path.join(d, f)
                st = os.stat(p)
                out[p] = (st.st_mtime_ns, st.st_size)
    return out


def libelles():
    return sorted(l for l in macro_buttons(e) if l)


# 1. Projet d'un onglet ; macros du fichier par défaut intégrées
macro_id(e, 'BASE')
save_project_as(e, banc)
check(wait_until(lambda: e.project().get('path') == banc and e.sessions()[0]['in_project'], 5), 'projet enregistre : %r' % e.project())
check(e.sessions()[0]['macros_file'] == '', 'onglet de projet sans macros_file : %r' % e.sessions()[0]['macros_file'])
# Preuve négative : le recalcul de « modifié » est regroupé à l'inactivité (au plus un par 300 ms) ; 1,5 s couvre ce regroupement avec une marge de 5
time.sleep(1.5)
check(e.project().get('modified') is False, 'projet non modifie')
avant = inis()
check(len(avant) >= 2, 'fichiers .ini presents avant : %r' % sorted(avant))

# 2. Configure Macros : ajout de « AJOUT », OK → aucun .ini touché, projet modifié
open_editor(e)
e.activate('macro-add')
check(wait_until(lambda: len(editor_rows(e)) == 2, 3), 'ligne ajoutee : %r' % [r['label'] for r in editor_rows(e)])
vide = [r for r in editor_rows(e) if r['label'] == '']
check(len(vide) == 1, 'une ligne vide')
if len(vide) != 1:
    sys.exit(conclude())
e.set_text(vide[0]['cells']['label']['id'], 'AJOUT')
check(wait_until(lambda: sorted(r['label'] for r in editor_rows(e)) == ['AJOUT', 'BASE'], 3), 'libelle saisi : %r' % [r['label'] for r in editor_rows(e)])
e.set_text(vide[0]['cells']['action']['id'], '<AJOUT>')	# une macro sans action n'a pas de bouton
check(wait_until(lambda: [r['action'] for r in editor_rows(e) if r['label'] == 'AJOUT'] == ['<AJOUT>'], 3), 'action saisie')
e.activate('macro-ok')
check(wait_until(lambda: not e.find(name='macro-editor'), 3), 'editeur ferme par OK')
check(wait_until(lambda: 'AJOUT' in macro_buttons(e), 3), 'bouton AJOUT dans le panneau : %r' % libelles())
check(wait_until(lambda: e.project().get('modified') is True, 3), 'projet modifie par les macros : %r' % e.project())
check(inis() == avant, 'aucun .ini cree ni modifie : %r' % {p: v for p, v in inis().items() if avant.get(p) != v})
check(e.sessions()[0]['macros_file'] == '', 'macros_file reste vide')

# 3. Load macros file… : le panneau montre les macros du .ini ; un changement de polling ne le réécrit pas
e.action('load-macros')
file_dialog(e, 'Load macros file', charge)
check(wait_until(lambda: libelles() == ['L2', '⏱ L1'], 5), 'panneau : macros du .ini charge : %r' % libelles())
check(e.sessions()[0]['macros_file'] == '', 'chemin du .ini charge non retenu : %r' % e.sessions()[0]['macros_file'])
st_charge = os.stat(charge).st_mtime_ns
contenu_charge = open(charge).read()
bouton = 'macro-%d' % macro_id(e, '⏱ L1')
e.bw.click(e.find1(name=bouton), button=3)
e.find1(name='polling-popover')
check(e.find1(name='polling-period').get('label') == '200', 'periode du .ini : 200')
e.bw.key('a', mods=('ctrl',))
e.bw.type('2000')
e.bw.key('Return')
check(wait_until(lambda: not e.find(name='polling-popover'), 3), 'popover ferme par Entree')
# Preuve négative : une écriture différée (écriture du fichier de macros, au plus 300 ms après le changement) aurait eu lieu en 1,5 s
time.sleep(1.5)
check(os.stat(charge).st_mtime_ns == st_charge and open(charge).read() == contenu_charge, 'le .ini charge n est pas reecrit apres le changement de periode')
check(inis().get(charge) is not None and all(inis().get(p) == v for p, v in avant.items()), 'aucun autre .ini touche')
check(e.alerts() == [], 'aucune boite : %r' % e.alerts())

# 4. Save macros file… : export, chemin non retenu
e.action('save-macros')
file_dialog(e, 'Save macros file', export)
check(wait_until(lambda: os.path.exists(export), 5), 'export ecrit')
texte = open(export, encoding='utf-8').read()
check('label=L1' in texte and 'label=L2' in texte and 'polling_period_ms=2000' in texte, 'export : macros et periode courante : %r' % texte[:200])
check(e.sessions()[0]['macros_file'] == '', 'export : macros_file toujours vide : %r' % e.sessions()[0]['macros_file'])
check(e.config_group('profile:default').get('macros_file') in (None, ''), 'export : macros_file du profil non ecrit : %r' % e.config_group('profile:default'))

# 5. New port… : copie du jeu dans le nouvel onglet, indépendante du premier
open_tab(e, p2.port)
check(wait_until(lambda: libelles() == ['L2', '⏱ L1'], 5), 'nouvel onglet : memes libelles : %r' % libelles())
check(e.sessions()[1]['in_project'] and e.sessions()[1]['macros_file'] == '', 'nouvel onglet dans le projet, sans macros_file : %r' % e.sessions()[1])
bouton = 'macro-%d' % macro_id(e, '⏱ L1')
e.bw.click(e.find1(name=bouton), button=3)
e.find1(name='polling-popover')
check(e.find1(name='polling-period').get('label') == '2000', 'nouvel onglet : periode copiee (2000)')
e.bw.key('a', mods=('ctrl',))
e.bw.type('500')
e.bw.key('Return')
check(wait_until(lambda: not e.find(name='polling-popover'), 3), 'popover du nouvel onglet ferme')
e.bw.click(e.find1(name=bouton), button=3)
e.find1(name='polling-popover')
check(e.find1(name='polling-period').get('label') == '500', 'nouvel onglet : periode 500 : %r' % e.find1(name='polling-period').get('label'))
e.bw.key('Escape')
check(wait_until(lambda: not e.find(name='polling-popover'), 3), 'popover ferme (Echap)')
e.set_state('active-tab', GLib.Variant('i', 0))
check(wait_until(lambda: e.active_session()['index'] == 0, 3), 'premier onglet actif')
bouton = 'macro-%d' % macro_id(e, '⏱ L1')
e.bw.click(e.find1(name=bouton), button=3)
e.find1(name='polling-popover')
check(e.find1(name='polling-period').get('label') == '2000', 'premier onglet : periode inchangee (2000) : %r' % e.find1(name='polling-period').get('label'))
e.bw.key('Escape')
check(wait_until(lambda: not e.find(name='polling-popover'), 3), 'popover ferme (Echap)')
check(inis().get(charge) is not None and os.stat(charge).st_mtime_ns == st_charge, 'le .ini charge reste intact')

e.action('project-save')
check(wait_until(lambda: e.project().get('modified') is False, 3), 'Save : projet non modifie')
check(inis().get(charge) is not None and all(inis().get(p) == v for p, v in avant.items()), 'apres Save : aucun .ini touche')

# 6. Close project, profil default sans macros_file : l'onglet libre lit ses macros comme au démarrage (fichier par défaut) ; une macro ajoutée
# par Configure Macros (OK) s'ajoute au fichier par défaut, qui ne perd aucune macro
e.action('project-close')
check(wait_until(lambda: len(e.sessions()) == 1 and e.project().get('open') is False, 5), 'Close project : un onglet libre')
check(wait_until(lambda: libelles() == ['BASE'], 5), 'onglet libre : macros du fichier par defaut : %r' % libelles())
check(e.sessions()[0]['macros_file'] == e.macros_path, 'onglet libre : fichier par defaut courant : %r' % e.sessions()[0]['macros_file'])
open_editor(e)
e.activate('macro-add')
check(wait_until(lambda: len([r for r in editor_rows(e) if r['label'] == '']) == 1, 3), 'ligne ajoutee : %r' % [r['label'] for r in editor_rows(e)])
vide = [r for r in editor_rows(e) if r['label'] == '']
if vide:
    e.set_text(vide[0]['cells']['label']['id'], 'LIBRE')
    e.set_text(vide[0]['cells']['action']['id'], '<LIBRE>')
    check(wait_until(lambda: [r['action'] for r in editor_rows(e) if r['label'] == 'LIBRE'] == ['<LIBRE>'], 3), 'macro LIBRE saisie')
e.activate('macro-ok')
check(wait_until(lambda: not e.find(name='macro-editor'), 3), 'editeur ferme par OK')
check(wait_until(lambda: 'label=LIBRE' in open(e.macros_path).read(), 3), 'fichier par defaut : LIBRE ajoutee')
texte = open(e.macros_path).read()
check('label=BASE' in texte, 'fichier par defaut : BASE gardee : %r' % texte[:300])
check(e.alerts() == [], 'aucune boite : %r' % e.alerts())
e.finish()
sys.exit(conclude())
