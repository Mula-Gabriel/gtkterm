# Ancien fichier de macros : lu au démarrage (macros, arguments, listes), inchangé tant que rien n'est enregistré, réécrit au format
# v2 au premier enregistrement, avec une copie .avant-migration identique à l'original (M21).
from gtktest import *

e = Env('macros_migration')
ma = os.path.join(e.root, 'macros.ini')
copie = ma + '.avant-migration'
e.write_macros(ma, [('L', 'None', 'S %#Vitesses', '', '1', 0, 1000), ('Arg', 'None', 'A%d', '', '42', 0, 1000)],
               lists=['Vitesses:Lent=0|Vif=1'])
original = open(ma).read()
e.write_rc(e.profile(macros_file=ma))
e.start(); time.sleep(2)
bw = Broadway(e.display)
term = find1(e.app, 'terminal')

# Une liste de deux valeurs donne deux boutons « L Lent » / « L Vif »
e.drain(0.2)
act(find1(e.app, 'push button', '^L Vif$'))  # le 2e bouton est hors de la zone visible du panneau étroit : pas de « showing »
d = e.read_pty(0.6)
check(d == b'S 1', 'liste lue : L Vif envoie S 1', d)
check(open(ma).read() == original and not os.path.exists(copie), 'fichier inchange avant tout enregistrement')

# Cliquer une macro à argument enregistre le fichier (valeur du champ) : on ne le fait qu'après la migration
# Premier enregistrement : fenêtre des macros validée sans modification
menu_key(bw, e.app, 'Configuration', 'Macros', focus_obj=term)
w = wait_window(e.app, 'Configure Macros'); time.sleep(0.5)
bw.click_obj(find1(w, 'push button', '^_?OK$', showing=True)); time.sleep(1.0)
texte = open(ma).read()
check('[gtkterm-macros]' in texte and 'version=2' in texte, 'fichier reecrit au format v2', texte[:300])
check(os.path.exists(copie) and open(copie).read() == original, 'copie .avant-migration identique a l\'original')
e.drain(0.2)
act(macro_button(e.app, 'Arg'))
d = e.read_pty(0.6)
check(d == b'A42', 'apres migration : Arg envoie A42 (argument lu de l\'ancien fichier)', d)
finish(e)
sys.exit(conclude())
