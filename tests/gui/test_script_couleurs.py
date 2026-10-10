# Couleurs, disposition et session du panneau de script (spec §3.2-15, §6.6) : le schéma est écrit sous $XDG_DATA_HOME/gtkterm/styles et jamais sous
# gtksourceview-* ; sans choix explicite, [colors] n'a ni bg ni fg et l'éditeur suit le terminal (chargement de profil, dialogue Terminal) ; un fond
# choisi est écrit et ne suit plus ; Reset (sans fermer) puis Apply le rend au terminal ; des bg/fg hérités de GTK 3 égaux au terminal suivent de nouveau.
import os
import re
import sys
import time
from gi.repository import GLib
from gtktest import *

# Profils sur la pty du scénario (Env.profile) : le profil chargé rouvre son port, qui ne doit jamais être /dev/ttyS0 (garde-fou EMGUARD)
def sombre(e):
    return e.profile(term_background='0;0;0;1;', term_foreground='1;1;1;1;')


def clair(e):
    return e.profile('clair', term_background='1;1;1;1;', term_foreground='0;0;0;1;')


def schemas(e):
    return [os.path.join(r, f) for r, _, fs in os.walk(e.root) for f in fs if f == 'gtkterm-custom.xml']


def fond(e):
    s = [p for p in schemas(e) if os.path.join('gtkterm', 'styles') in p]
    if not s:
        return None
    m = re.search(r'name="text"\s+foreground="(#[0-9a-f]{6})"\s+background="(#[0-9a-f]{6})"', open(s[0]).read())
    return m.group(2) if m else None


def demarrer(nom, contenu):
    """contenu(e) : texte de .gtktermrc."""
    e = Env(nom)
    e.write_config(contenu(e))
    e.start()
    e.find1(name='terminal', timeout=10)
    e.set_state('show-script-panel', GLib.Variant('b', True))
    e.find1(name='script-editor', timeout=5)
    return e


def fond_terminal(e, couleur):
    """Couleur de fond choisie dans le dialogue Terminal (effet immédiat), puis Close."""
    e.action('terminal')
    e.find1(name='terminal-dialog', timeout=5)
    e.set_text('terminal-bg', couleur)
    time.sleep(0.3)
    e.activate('terminal-close')
    check(wait_until(lambda: not e.find(name='terminal-dialog'), 3), 'dialogue Terminal ferme')


# 1. Rien de choisi : suivi du terminal (profil chargé, dialogue Terminal), schéma au bon endroit, disposition et session enregistrées
e = demarrer('script_couleurs', lambda e: e.general() + sombre(e) + clair(e))
check(wait_until(lambda: fond(e) == '#000000', 5), 'schema ecrit sous gtkterm/styles, fond = terminal (%r)' % fond(e))
check(not [p for p in schemas(e) if 'gtksourceview-' in p], 'aucun schema sous gtksourceview-* (%r)' % schemas(e))
e.set_state('script-layout', GLib.Variant('s', 'leftright'))
check(wait_until(lambda: e.state('script-layout') == 'leftright', 2), 'disposition gauche/droite')
script = os.path.join(e.root, 'mon script.lua')
with open(script, 'w') as f:
    f.write("gtkterm.log('ouvert')\n")
e.activate('script-open')
file_dialog(e, 'Open Lua Script', script)
check(wait_until(lambda: e.find1(name='script-editor').get('label') == "gtkterm.log('ouvert')\n", 3), 'script ouvert dans l editeur')
e.action('load-profile')
e.find1(name='profile-window', timeout=5)
e.bw.click(e.find1(name='profile-row', label='clair'))
time.sleep(0.3)
e.activate('profile-ok')
check(wait_until(lambda: fond(e) == '#ffffff', 5), 'fond suit le profil charge (%r)' % fond(e))
fond_terminal(e, '#336699')
check(wait_until(lambda: fond(e) == '#336699', 5), 'fond suit le dialogue Terminal (%r)' % fond(e))
e.finish()
couleurs = e.config_group('colors')
check('bg' not in couleurs and 'fg' not in couleurs, 'ni bg ni fg ecrits sans choix (%r)' % couleurs)
check(e.config_group('layout').get('orientation') == 'leftright', 'disposition enregistree')
check(e.config_group('session').get('script_path') == script, 'chemin du script enregistre a l identique')

# 2. Fond choisi : écrit, ne suit plus ; Reset sans fermer puis Apply : suivi rétabli, clé retirée
e = demarrer('script_couleurs_choix', lambda e: e.general() + sombre(e))
e.activate('script-colors')
e.find1(name='script-colors-window', timeout=5)
e.set_text('script-color-bg', '#102030')
e.activate('script-colors-apply')
check(wait_until(lambda: not e.find(name='script-colors-window'), 3), 'Apply ferme la fenetre')
check(wait_until(lambda: fond(e) == '#102030', 5), 'fond choisi applique (%r)' % fond(e))
couleurs = e.config_group('colors')
check(couleurs.get('bg') == 'rgb(16,32,48)' and 'fg' not in couleurs, 'bg choisi ecrit, fg non (%r)' % couleurs)
check('lineno_bg' not in couleurs and 'comment' not in couleurs, 'couleurs derivees du fond non ecrites (suivies) (%r)' % couleurs)
fond_terminal(e, '#336699')
time.sleep(0.5)
check(fond(e) == '#102030', 'fond choisi ne suit plus le terminal (%r)' % fond(e))
e.activate('script-colors')
e.find1(name='script-colors-window', timeout=5)
e.activate('script-colors-reset')
time.sleep(0.3)
check(e.find(name='script-colors-window') != [], 'Reset ne ferme pas la fenetre')
check(e.find1(name='script-color-bg').get('label') == 'rgb(51,102,153)', 'Reset : fond = terminal (%r)' % e.find1(name='script-color-bg').get('label'))
e.activate('script-colors-apply')
check(wait_until(lambda: fond(e) == '#336699', 5), 'apres Reset+Apply, fond = terminal (%r)' % fond(e))
check('bg' not in e.config_group('colors'), 'bg retire apres Reset+Apply')
e.finish()

# 3. Fichier écrit par GTK 3 : bg/fg égaux au terminal = suivis ; une couleur vraiment choisie reste
e = demarrer('script_couleurs_gtk3', lambda e: e.general() + sombre(e) + '[colors]\nbg=rgb(0,0,0)\nfg=rgb(255,255,255)\ncomment=rgb(1,2,3)\n\n')
check(wait_until(lambda: fond(e) == '#000000', 5), 'fond initial = terminal')
fond_terminal(e, '#ffffff')
check(wait_until(lambda: fond(e) == '#ffffff', 5), 'bg herite de GTK 3 suit de nouveau le terminal (%r)' % fond(e))
e.finish()
couleurs = e.config_group('colors')
check('bg' not in couleurs and 'fg' not in couleurs, 'bg/fg herites retires (%r)' % couleurs)
check(couleurs.get('comment') == 'rgb(1,2,3)', 'couleur choisie conservee (%r)' % couleurs)
sys.exit(conclude())
