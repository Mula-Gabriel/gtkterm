# Ancien fichier de macros (porté) : lu au démarrage (macros, arguments, listes), inchangé tant que rien n'est enregistré, réécrit au format
# v2 à la validation de « Configure Macros », avec une copie .avant-migration identique à l'original (M21) ; la liste apparaît dans la page Lists.
import os, re, time
from gtktest import *

e = Env('macros_migration')
ma = e.macros_path
copie = ma + '.avant-migration'
write_macros_v1(ma, [('L', 'None', 'S %#Vitesses', '', '1', 0, 1000), ('Arg', 'None', 'A%d', '', '42', 0, 1000)],
                lists=['Vitesses:Lent=0|Vif=1'])
original = open(ma).read()
macro_config(e, ma)
e.start()

# Une liste de deux valeurs donne deux boutons « L Lent » / « L Vif »
vif = e.find1(label='L Vif', type='GtkButton')
e.drain()
e.activate(vif['name'])
d = e.read_pty(0.6)
check(d == b'S 1', 'liste lue : L Vif envoie S 1 (%r)' % d)
check(open(ma).read() == original and not os.path.exists(copie), 'fichier inchange avant tout enregistrement')

# Page Lists : les deux entrées de la liste
open_editor(e)
e.bw.click(e.find1(label='Lists', type='GtkLabel'))
entrees = lambda: [(r['name'], r['display'], r['value']) for r in editor_rows(e, 'list-cell', ('name', 'display', 'value'))]
check(wait_until(lambda: entrees() == [('Vitesses', 'Lent', '0'), ('Vitesses', 'Vif', '1')]), 'page Lists : %r' % entrees())

# Premier enregistrement : OK sans modification
e.activate('macro-ok')
check(wait_until(lambda: not e.find(name='macro-editor')), 'fenetre fermee par OK')
texte = open(ma).read()
check('[gtkterm-macros]' in texte and 'version=2' in texte, 'fichier reecrit au format v2')
check(os.path.exists(copie) and open(copie).read() == original, 'copie .avant-migration identique a l\'original')
e.drain()
e.activate('macro-%d' % macro_id(e, 'Arg'))
d = e.read_pty(0.6)
check(d == b'A42', 'apres migration : Arg envoie A42 (%r)' % d)
e.finish()
sys.exit(conclude())
