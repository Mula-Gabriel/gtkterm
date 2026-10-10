# Dialogue Terminal (§6.4) : titre « Terminal », instance unique, défilement borné à 100-99 999, curseur, police et couleurs appliqués immédiatement
# au VTE (lus par le crochet test-terminal-props), valeurs reprises à la réouverture, rien d'écrit avant « Save configuration », puis enregistrées.
from gtktest import *


def props():
    return e.terminal_props()


def dialogues():
    return [w for w in e.windows() if w.get('title') == 'Terminal']


def rc():
    with open(e.config_path, encoding='utf-8') as f:
        return f.read()


e = Env('terminal_dialog')
e.write_config(e.general() + e.profile(font='Monospace 12', term_scrollback=10000, term_block_cursor='true'))
e.start(port=False)
check(wait_until(lambda: props().get('scrollback') == 10000, 5), 'defilement du profil applique au demarrage')
check(props().get('cursor') == 'block', 'curseur bloc du profil')
check(props().get('font') == 'Monospace 12', 'police du profil')

# 1. ouverture : titre « Terminal », instance unique, valeurs du profil
e.action('terminal')
e.find1(name='terminal-dialog')
check(len(dialogues()) == 1, 'dialogue intitule Terminal')
check(not [w for w in e.windows() if w.get('title') == 'Main Window'], 'plus de titre Main Window')
e.action('terminal')
time.sleep(0.5)
check(len(e.find(name='terminal-dialog')) == 1, 'une seule instance')
check(e.find1(name='terminal-scrollback')['label'] == '10000', 'defilement affiche')
check(e.find1(name='terminal-block-cursor')['active'] is True, 'curseur bloc affiche')

# 2. défilement : appliqué immédiatement, borné à 100-99 999
e.set_text('terminal-scrollback', '500')
check(wait_until(lambda: props().get('scrollback') == 500, 3), 'defilement 500 applique')
e.set_text('terminal-scrollback', '50')
check(wait_until(lambda: props().get('scrollback') == 100, 3), 'defilement borne a 100')
e.set_text('terminal-scrollback', '200000')
check(wait_until(lambda: props().get('scrollback') == 99999, 3), 'defilement borne a 99999')

# 3. curseur, police, couleurs
e.activate('terminal-block-cursor')
check(wait_until(lambda: props().get('cursor') == 'ibeam', 3), 'curseur barre applique')
e.set_text('terminal-font', 'Monospace 15')
check(wait_until(lambda: props().get('font') == 'Monospace 15', 3), 'police appliquee')
e.set_text('terminal-fg', 'rgb(255,0,0)')
check(wait_until(lambda: props().get('foreground') == 'rgb(255,0,0)', 3), 'couleur du texte appliquee')
e.set_text('terminal-bg', 'rgb(0,0,128)')
check(wait_until(lambda: props().get('background') == 'rgb(0,0,128)', 3), 'couleur du fond appliquee')

# 4. Close ; réouverture : valeurs en cours (config), rien d'écrit sur disque
e.activate('terminal-close')
check(wait_until(lambda: not e.find(name='terminal-dialog'), 3), 'Close ferme le dialogue')
e.action('terminal')
check(e.find1(name='terminal-scrollback')['label'] == '99999', 'defilement repris a la reouverture')
check(e.find1(name='terminal-block-cursor')['active'] is False, 'curseur repris a la reouverture')
e.activate('terminal-close')
check(wait_until(lambda: not e.find(name='terminal-dialog'), 3), 'dialogue ferme')
check('term_scrollback=99999' not in rc(), 'rien n est ecrit sans Save configuration')

# 5. « Save configuration » enregistre les réglages du dialogue
e.action('save-profile')
e.find1(name='profile-window')
e.activate('profile-ok')
check(wait_until(lambda: e.alerts() != [], 5), 'confirmation d ecrasement de default')
e.close_alert('Overwrite')
check(wait_until(lambda: 'term_scrollback=99999' in rc(), 5), 'defilement enregistre')
contenu = rc()
check('term_block_cursor=false' in contenu and 'font=Monospace 15' in contenu, 'curseur et police enregistres')
check('term_foreground=1;0;0;1;' in contenu, 'couleur du texte enregistree')
e.finish()
sys.exit(conclude())
