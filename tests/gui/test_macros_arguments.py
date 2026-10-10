# Arguments du panneau de macros : liste en GtkDropDown (valeur mémorisée présélectionnée, choix enregistré), entrée invalide marquée
# (classe error) dès la frappe, enregistrement à Entrée ou à la perte du focus seulement si la valeur a changé, valeurs relues après
# redémarrage ; liste de deux valeurs = deux boutons ; panneau masqué et réaffiché par app.show-macro-panel.
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

# Choix « Gamma » : enregistré tout de suite
e.bw.click(liste)
e.bw.click(e.find1(label='Gamma', type='GtkLabel'))
check(wait_until(lambda: 'args=g;' in open(ma).read()), 'choix Gamma enregistre')
envoi('Choix', b'Cg;')

# Entrée invalide : classe error dès la frappe, rien d'enregistré avant la validation
champ = 'macro-%d-arg-0' % num
avant = open(ma).read()
e.bw.click(e.find1(name=champ))
e.set_text(champ, 'abc')
check(wait_until(lambda: 'error' in classes(e.find1(name=champ))), 'entree abc pour un argument entier marquee en erreur')
check(open(ma).read() == avant, 'rien d\'enregistre avant la validation')
e.set_text(champ, '12')
check(wait_until(lambda: 'error' not in classes(e.find1(name=champ))), 'entree 12 valide')
e.bw.key('Return')
check(wait_until(lambda: 'args=12;' in open(ma).read()), 'Entree : valeur 12 enregistree')
envoi('Num', b'N12;')

# Perte du focus sans changement : fichier non réécrit ; avec changement : enregistré
mtime = os.stat(ma).st_mtime_ns
e.bw.click(e.find1(name=champ))
e.bw.click(e.find1(name='terminal'))
time.sleep(0.5)
check(os.stat(ma).st_mtime_ns == mtime, 'perte du focus sans changement : fichier non reecrit')
e.bw.click(e.find1(name=champ))
e.set_text(champ, '13')
e.bw.click(e.find1(name='terminal'))
check(wait_until(lambda: 'args=13;' in open(ma).read()), 'perte du focus apres changement : valeur 13 enregistree')

# Liste de deux valeurs : deux boutons « L Lent » / « L Vif »
deux = [w for w in e.dump() if re.match(r'^macro-\d+-value-[01]$', w.get('name') or '')]
check(sorted(w.get('label') for w in deux) == ['L Lent', 'L Vif'], 'deux boutons pour la liste Bin : %r' % [w.get('label') for w in deux])
e.drain()
e.activate([w for w in deux if w.get('label') == 'L Vif'][0]['name'])
d = e.read_pty(0.6)
check(d == b'S 1', 'L Vif envoie S 1 (%r)' % d)

# Redémarrage : valeurs relues
e.action('quit')                       # fin propre (close-request), pas de SIGTERM
try:
    e.proc.wait(10)
except subprocess.TimeoutExpired:
    pass
check(e.stop() == 0, 'premier lancement : app.quit, code 0')
e.start()
num = macro_id(e, 'Num')
check(e.find1(name='macro-%d-arg-0' % num).get('label') == '13', 'valeur 13 relue apres redemarrage')
envoi('Num', b'N13;')
envoi('Choix', b'Cg;')
envoi('Txt', b'Tx;')
e.finish()
sys.exit(conclude())
