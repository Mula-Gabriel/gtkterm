# Barre de recherche (spec §6.3) : app.find ouvre ; Entrée cherche (occurrence sélectionnée : Copy actif), Maj+Entrée sélectionne l'autre des deux
# occurrences (précédente, recherche circulaire) — texte sélectionné lu par app.copy puis app.paste (le collage part sur le port) ; erreur
# d'expression dans la barre, sans boîte ; Échap ferme, retire le surlignage et rend le focus au terminal ; app.find rouvre ; Ctrl+Maj+F.
import sys
from gtktest import *


def ouverte():
    return bool(e.find(name='search-entry'))


def erreur():
    w = e.find(name='search-error')
    return (w[0].get('label') or '') if w else ''


def selection():
    """Texte sélectionné : app.copy puis app.paste, qui l'envoie sur le port (écho local désactivé)."""
    e.drain()
    e.action('copy')
    time.sleep(0.2)
    e.action('paste')
    return e.read_pty(0.8).lower()


e = Env('search')
e.write_config(e.general() + e.profile())
e.start()
e.write_pty(b'alpha\r\nligne cherche-1 ici\r\nligne CHERCHE-2 ici\r\nomega\r\n')
check(wait_until(lambda: 'omega' in e.terminal_text(), 3), 'texte recu')
check(not ouverte(), 'barre fermee au demarrage')

e.action('find')
check(wait_until(ouverte, 3), 'app.find ouvre la barre')
e.bw.click(e.find1(name='search-entry'))
e.set_text('search-entry', 'cherche-[0-9]')
check(not e.describe('copy')[0], 'aucune selection avant la recherche')
e.bw.key('Return')
check(wait_until(lambda: e.describe('copy')[0], 3), 'Entree : occurrence trouvee et selectionnee')
premiere = selection()
check(premiere in (b'cherche-1', b'cherche-2'), 'Entree : une des deux occurrences selectionnee (sans casse)', repr(premiere))
e.bw.key('Return', mods=('shift',))
time.sleep(0.3)
seconde = selection()
check(seconde in (b'cherche-1', b'cherche-2') and seconde != premiere, "Maj+Entree : l'autre occurrence (precedente) selectionnee", repr(seconde))

e.set_text('search-entry', '(')
e.bw.key('Return')
check(wait_until(lambda: erreur() != '', 3), "erreur d'expression affichee dans la barre")
check(e.alerts() == [], "aucune boite pour l'erreur")
e.set_text('search-entry', 'omega')
check(wait_until(lambda: erreur() == '', 3), 'erreur effacee a la modification')

e.bw.key('Escape')
check(wait_until(lambda: not ouverte(), 3), 'Echap ferme la barre')
check(wait_until(lambda: not e.describe('copy')[0], 3), 'surlignage retire')
e.drain()
e.bw.type('k')
check(e.read_pty(0.8) == b'k', 'focus rendu au terminal : la frappe part sur le port')

e.action('find')
check(wait_until(ouverte, 3), 'app.find rouvre la barre apres Echap')
e.action('find')
check(wait_until(lambda: not ouverte(), 3), 'app.find referme la barre ouverte')
e.bw.click(e.find1(name='terminal'))
e.bw.key('f', mods=('ctrl', 'shift'))
check(wait_until(ouverte, 3), 'Ctrl+Maj+F ouvre la barre')
e.finish()
sys.exit(conclude())
