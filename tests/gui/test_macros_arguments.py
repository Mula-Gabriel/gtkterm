# Arguments du panneau de macros (6c, spec §3.2-6) : liste en GtkDropDown (valeur par défaut présélectionnée), entrée invalide marquée (classe
# error) dès la frappe ; les valeurs saisies servent à l'envoi mais le fichier n'est jamais touché ; « Reset arguments » (popover) revient aux
# valeurs par défaut ; l'éditeur (zone « Default values ») les enregistre ; relues après redémarrage ; liste de deux valeurs = deux boutons ;
# panneau masqué et réaffiché par app.show-macro-panel.
import os, re, time
from gi.repository import GLib
from gtktest import *

e = Env('macros_arguments')
ma = e.macros_path
write_macros_v2(ma, [dict(label='Num', action='N%d;', args=['5']),
                     dict(label='Choix', action='C%#Modes;', args=['b']),
                     dict(label='L', action='S %#Bin'),
                     dict(label='Txt', action='T%s;', args=['x'])],
                lists=[('Modes', [('Alpha', 'a'), ('Beta', 'b'), ('Gamma', 'g')]), ('Bin', [('Lent', '0'), ('Vif', '1')])])
macro_config(e, ma)
e.start()


def envoi(libelle, attendu):
    e.drain()
    e.activate('macro-%d' % macro_id(e, libelle))
    d = e.read_pty(0.6)
    check(d == attendu, 'clic %s envoie %r (%r)' % (libelle, attendu, d))


check(e.state('show-macro-panel') is True, 'panneau de macros visible au demarrage')
e.set_state('show-macro-panel', GLib.Variant('b', False))
check(wait_until(lambda: not e.find(name='macro-panel')), 'panneau masque par app.show-macro-panel')
e.set_state('show-macro-panel', GLib.Variant('b', True))
check(wait_until(lambda: bool(e.find(name='macro-panel'))), 'panneau reaffiche')

num, choix = macro_id(e, 'Num'), macro_id(e, 'Choix')
liste = e.find1(name='macro-%d-arg-0' % choix)
check(liste.get('type') == 'GtkDropDown', 'argument de liste en GtkDropDown (%s)' % liste.get('type'))
envoi('Choix', b'Cb;')

depart = open(ma).read()
mtime = os.stat(ma).st_mtime_ns


def fichier_inchange(msg):
    time.sleep(0.4)
    check(open(ma).read() == depart and os.stat(ma).st_mtime_ns == mtime, 'fichier .ini inchange : ' + msg)


# 1. Choix « Gamma » : utilisé pour l'envoi, rien d'enregistré
e.bw.click(liste)
e.bw.click(e.find1(label='Gamma', type='GtkLabel'))
time.sleep(0.3)
envoi('Choix', b'Cg;')
fichier_inchange('choix Gamma')

# 2. Entrée invalide : classe error dès la frappe ; valeur valide + Entrée : envoi, fichier inchangé
champ = 'macro-%d-arg-0' % num
e.bw.click(e.find1(name=champ))
e.set_text(champ, 'abc')
check(wait_until(lambda: 'error' in classes(e.find1(name=champ))), 'entree abc pour un argument entier marquee en erreur')
e.set_text(champ, '12')
check(wait_until(lambda: 'error' not in classes(e.find1(name=champ))), 'entree 12 valide')
e.bw.key('Return')
envoi('Num', b'N12;')
fichier_inchange('valeur 12')

# 3. Clic droit sur « Num » : « Reset arguments » rend la valeur par défaut (5)
e.bw.click(e.find1(name='macro-%d' % num), button=3)
e.find1(name='polling-popover')
reset = e.find1(name='macro-reset-args')
check(reset.get('label') == 'Reset arguments', 'bouton Reset arguments (%r)' % reset.get('label'))
e.bw.click(reset)
check(wait_until(lambda: not e.find(name='polling-popover')), 'popover ferme par Reset arguments')
check(wait_until(lambda: e.find1(name=champ).get('label') == '5'), 'champ revenu a la valeur par defaut 5 (%r)' % e.find1(name=champ).get('label'))
envoi('Num', b'N5;')
fichier_inchange('Reset arguments')

# 4. Éditeur : zone « Default values » de la ligne Num ; 7 enregistré à OK, affiché dans le panneau
open_editor(e)
check(not [w for w in e.find(name='macro-defaults')], 'zone Default values masquee sans ligne selectionnee')
editor_select(e, 0, col='label')
zone = e.find1(name='macro-defaults')
check(zone.get('mapped'), 'zone Default values visible pour la ligne Num')
check(e.find1(name='macro-default-0').get('label') == '5', 'macro-default-0 = 5 (%r)' % e.find1(name='macro-default-0').get('label'))
e.set_text('macro-default-0', '7')
e.activate('macro-ok')
check(wait_until(lambda: not e.find(name='macro-editor')), 'editeur ferme par OK')
check(wait_until(lambda: 'args=7;' in open(ma).read()), 'valeur par defaut 7 enregistree dans le .ini')
check(wait_until(lambda: e.find1(name=champ).get('label') == '7'), 'champ du panneau affiche 7 (%r)' % e.find1(name=champ).get('label'))
envoi('Num', b'N7;')

# 6. Liste de deux valeurs : deux boutons « L Lent » / « L Vif »
deux = [w for w in e.dump() if re.match(r'^macro-\d+-value-[01]$', w.get('name') or '')]
check(sorted(w.get('label') for w in deux) == ['L Lent', 'L Vif'], 'deux boutons pour la liste Bin : %r' % [w.get('label') for w in deux])
e.drain()
e.activate([w for w in deux if w.get('label') == 'L Vif'][0]['name'])
d = e.read_pty(0.6)
check(d == b'S 1', 'L Vif envoie S 1 (%r)' % d)

# 5. Redémarrage : défaut 7 enregistré, « Choix » présélectionné sur b (défaut du fichier, pas g)
e.action('quit')                       # fin propre (close-request), pas de SIGTERM
try:
    e.proc.wait(10)
except subprocess.TimeoutExpired:
    pass
check(e.stop() == 0, 'premier lancement : app.quit, code 0')
e.start()
num = macro_id(e, 'Num')
check(e.find1(name='macro-%d-arg-0' % num).get('label') == '7', 'valeur par defaut 7 relue apres redemarrage')
envoi('Num', b'N7;')
envoi('Choix', b'Cb;')
envoi('Txt', b'Tx;')

# 7. Valeur d'une entrée de liste changée sur la page Lists : la liste déroulante de « Default values » est à jour au retour sur Macros
open_editor(e)
editor_select(e, 1, col='label')	# Choix, restee selectionnee pendant le passage par la page Lists
check(wait_until(lambda: bool(e.find(name='macro-default-0'))), 'zone Default values de Choix')
e.bw.click(e.find1(label='Lists', type='GtkLabel'))
check(wait_until(lambda: bool(e.find(name='list-cell-value'))), 'page Lists affichee')
e.set_text('list-cell-value', 'z')	# première entrée : Alpha (a -> z)
e.bw.click(e.find1(label='Macros', type='GtkLabel', native=e.find1(name='macro-editor')['native']))	# onglet de l'éditeur, pas le menu Macros
check(wait_until(lambda: bool(e.find(name='macro-list'))), 'retour sur la page Macros')
check(wait_until(lambda: bool(e.find(name='macro-default-0'))), 'zone Default values de Choix toujours affichee')
e.bw.click(e.find1(name='macro-default-0'))
e.bw.click(e.find1(label='Alpha', type='GtkLabel'))
time.sleep(0.3)
e.activate('macro-ok')
check(wait_until(lambda: not e.find(name='macro-editor')), 'editeur ferme par OK')
check(wait_until(lambda: 'args=z;' in open(ma).read()), 'valeur modifiee z ecrite par la liste deroulante des valeurs par defaut')
e.finish()
sys.exit(conclude())
