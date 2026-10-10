# Fumée de l'interface GTK 4 : démarrage détecté par le nom D-Bus, actions app.* exportées, dump (fenêtre principale, terminal, barre de
# menus), titre lié à l'état de la liaison, octets reçus sur la pty affichés, À propos, app.quit → code 0, stderr propre.
from gtktest import *

e = Env('fumee')
e.write_config(e.general() + e.profile())
e.start(connect_browser=False)
check(e.proc.poll() is None, 'application demarree, nom D-Bus pris : ' + e.dest)
acts = e.actions()
for a in ('quit', 'about', 'test-dump', 'test-terminal-text', 'test-activate', 'test-set-text'):
    check(a in acts, 'action app.%s exportee' % a, acts)
check(e.find1(name='tabs')['type'] == 'GtkNotebook', 'fenetre a onglets (GtkNotebook « tabs »)')
check(len(e.find(name='session-view')) == 1, 'une page d onglet')
check(e.find1(name='terminal') is not None, 'terminal dans la page')
check(e.find1(name='session-view') is not None and e.find1(name='macro-panel') is not None, 'panneau de macros dans la page')
court = e.port.rsplit('/', 1)[-1]  # nom court de l'onglet : nom de base du port
d0 = e.dump()
check(bool(e.find(d0, label=court, type='GtkLabel', mapped=None)), 'etiquette d onglet presente (nom court %r)' % court)
check(not e.find(d0, label=court, type='GtkLabel'), 'barre d onglets cachee avec une seule page (etiquette non mappee)')
d = e.dump()
check(bool(e.find(d, name='main-window', type='GtkApplicationWindow')), 'fenetre principale dans le dump')
check(bool(e.find(d, name='terminal', type='VteTerminal')), 'terminal dans le dump')
check(bool(e.find(d, name='menubar', type='GtkPopoverMenuBar')), 'barre de menus dans le dump')
check(bool(e.find(d, name='terminal', type='VteTerminal')) and e.find(d, name='terminal')[0]['focus'], 'focus clavier sur le terminal')
attendu = 'GTKTerm - %s  115200-8-N-1' % e.port
check(wait_until(lambda: e.title() == attendu, 5), 'titre = GTKTerm - <etat de la liaison>, port ouvert', e.title())
check(e.alerts() == [], 'aucune boite au demarrage', e.alerts())
e.write_pty(b'HELLO-GTK4\r\nligne2\r\n')
check(wait_until(lambda: 'HELLO-GTK4\nligne2' in e.terminal_text(), 5), 'texte recu affiche dans le terminal', e.terminal_text()[:200])
e.action('about')
check(wait_until(lambda: [w for w in e.windows() if w['type'] == 'GtkAboutDialog'], 5), 'app.about ouvre la boite A propos')
e.finish()
sys.exit(conclude())
