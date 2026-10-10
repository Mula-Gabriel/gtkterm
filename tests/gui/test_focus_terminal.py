# Review Focus 4 : après la fermeture du popover de polling (Échap, Entrée), du menu des onglets, de la barre de recherche et d'un dialogue,
# et après un clic sur un bouton de macro, la frappe suivante dans la fenêtre part sur le port (focus clavier rendu au terminal).
# focus_window (clic sur la barre de titre) ne fait que rendre la surface broadway active : le widget qui a le focus n'en dépend pas.
import os, re, time
from gtktest import *

e = Env('focus_terminal')
ma = e.macros_path
write_macros_v2(ma, [dict(label='P', action='<P>')])
macro_config(e, ma)
e.start()
bouton = 'macro-%d' % macro_id(e, 'P')


def frappe(car, apres):
    e.bw.focus_window(e.title())
    e.drain()
    e.bw.type(car)
    d = e.read_pty(0.6)
    check(d == car.encode(), 'apres %s : la frappe %r part sur le port (%r)' % (apres, car, d))


e.bw.click(e.find1(name='terminal'))
frappe('a', 'un clic dans le terminal')

e.bw.click(e.find1(name=bouton), button=3)
e.find1(name='polling-popover')
e.bw.key('Escape')
check(wait_until(lambda: not e.find(name='polling-popover')), 'popover de polling ferme par Echap')
frappe('b', 'la fermeture du popover de polling par Echap')

e.bw.click(e.find1(name=bouton), button=3)
e.find1(name='polling-popover')
e.bw.key('Return')
check(wait_until(lambda: not e.find(name='polling-popover')), 'popover de polling ferme par Entree')
frappe('c', 'la fermeture du popover de polling par Entree')

e.drain()
e.bw.click(e.find1(name=bouton))
d = e.read_pty(0.6)
check(d == b'<P>', 'clic sur P envoie <P> (%r)' % d)
frappe('d', 'un clic sur un bouton de macro')

e.bw.click(e.find1(name='macro-tab-General'), button=3)
e.find1(name='tabs-popover')
e.bw.key('Escape')
check(wait_until(lambda: not e.find(name='tabs-popover')), 'menu des onglets ferme par Echap')
frappe('e', 'la fermeture du menu des onglets')

e.action('find')
e.find1(name='search-entry')
e.bw.key('Escape')
check(wait_until(lambda: not e.find(name='search-entry')), 'barre de recherche fermee par Echap')
frappe('f', 'la fermeture de la barre de recherche')

e.action('terminal')
e.find1(name='terminal-dialog')
e.activate('terminal-close')
check(wait_until(lambda: not e.find(name='terminal-dialog')), 'dialogue Terminal ferme')
frappe('g', 'la fermeture du dialogue Terminal')
e.finish()
sys.exit(conclude())
