# Ordre des macros dans « Configure Macros » : Move Up / Move Down déplacent la sélection (bords sans effet), un clic sur un en-tête de
# colonne ne trie pas (aucun trieur) ; l'ordre est conservé après OK, dans le fichier, dans le panneau et après redémarrage ; Cancel
# n'enregistre rien ; fermer par la barre de titre vaut OK.
import os, re, time
from gtktest import *

e = Env('macro_editor_ordre')
ma = e.macros_path
write_macros_v2(ma, [dict(label='M%d' % i, action='<%d>' % i) for i in (1, 2, 3, 4)])
macro_config(e, ma)
e.start()
macro_id(e, 'M1')


def libelles():
    return [r['label'] for r in editor_rows(e)]


def ordre_fichier():
    return re.findall(r'^label=(M\d)$', open(ma).read(), re.M)


def ordre_panneau():
    boutons = [w for w in e.dump() if re.match(r'^macro-\d+$', w.get('name') or '') and w.get('mapped')]
    return [w.get('label') for w in sorted(boutons, key=lambda w: w['y'])]


open_editor(e)
check(libelles() == ['M1', 'M2', 'M3', 'M4'], 'ordre initial %r' % libelles())
editor_select(e, 2)
e.activate('macro-up')
check(wait_until(lambda: libelles() == ['M1', 'M3', 'M2', 'M4']), 'Move Up : M3 au-dessus de M2 %r' % libelles())
e.activate('macro-up')
check(wait_until(lambda: libelles() == ['M3', 'M1', 'M2', 'M4']), 'Move Up : M3 en tete')
e.activate('macro-up')                  # synchrone ; une image passée, les lignes affichées reflètent le modèle
e.after_frame()
check(libelles() == ['M3', 'M1', 'M2', 'M4'] and editor_rows(e)[0]['selected'], 'Move Up en tete : sans effet, selection gardee')
editor_select(e, 1)
e.activate('macro-down')
check(wait_until(lambda: libelles() == ['M3', 'M2', 'M1', 'M4']), 'Move Down : M1 sous M2')
editor_select(e, 3)
e.activate('macro-down')
e.after_frame()
check(libelles() == ['M3', 'M2', 'M1', 'M4'], 'Move Down en bas : sans effet')

# En-têtes de colonne : un clic ne trie pas
editeur = e.find1(name='macro-editor')
for titre in ('Label', 'Shortcut', 'Action', 'Tab'):
    entete = [w for w in e.dump() if w.get('label') == titre and w.get('type') == 'GtkLabel' and w.get('native') == editeur.get('native')
              and w.get('mapped')]
    check(len(entete) >= 1, 'en-tete %s trouve' % titre)
    e.bw.click(entete[0])
    time.sleep(0.3)
    check(libelles() == ['M3', 'M2', 'M1', 'M4'], 'clic sur l\'en-tete %s : ordre inchange %r' % (titre, libelles()))
e.activate('macro-ok')
check(wait_until(lambda: not e.find(name='macro-editor')), 'OK ferme la fenetre')
check(ordre_fichier() == ['M3', 'M2', 'M1', 'M4'], 'ordre enregistre %r' % ordre_fichier())
check(wait_until(lambda: ordre_panneau() == ['M3', 'M2', 'M1', 'M4']), 'ordre du panneau %r' % ordre_panneau())

# Cancel : rien n'est enregistré
open_editor(e)
editor_select(e, 0)
e.activate('macro-down')
check(wait_until(lambda: libelles() == ['M2', 'M3', 'M1', 'M4']), 'deplacement avant Cancel')
e.activate('macro-cancel')
check(wait_until(lambda: not e.find(name='macro-editor')), 'Cancel ferme la fenetre')
check(ordre_fichier() == ['M3', 'M2', 'M1', 'M4'], 'Cancel : ordre du fichier inchange')

# Redémarrage : même ordre dans le panneau et dans l'éditeur
e.action('quit')                       # fin propre (close-request), pas de SIGTERM
try:
    e.proc.wait(10)
except subprocess.TimeoutExpired:
    pass
check(e.stop() == 0, 'premier lancement : app.quit, code 0')
e.start()
macro_id(e, 'M1')
check(ordre_panneau() == ['M3', 'M2', 'M1', 'M4'], 'apres redemarrage : ordre du panneau %r' % ordre_panneau())
open_editor(e)
check(libelles() == ['M3', 'M2', 'M1', 'M4'], 'apres redemarrage : ordre de l\'editeur %r' % libelles())

# Fermeture par la barre de titre = OK
editor_select(e, 3)
e.activate('macro-up')
check(wait_until(lambda: libelles() == ['M3', 'M2', 'M4', 'M1']), 'M4 remontee')
close_by_titlebar(e, 'macro-editor')
check(wait_until(lambda: not e.find(name='macro-editor')), 'fenetre fermee par la barre de titre')
check(wait_until(lambda: ordre_fichier() == ['M3', 'M2', 'M4', 'M1']), 'barre de titre : ordre enregistre %r' % ordre_fichier())
e.finish()
sys.exit(conclude())
