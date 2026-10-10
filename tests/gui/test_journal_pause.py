# Journal, pause et sensibilités (annexe SP2, spec §6.7) : la transformation est nourrie pendant la pause, donc une pause au milieu d'une ligne
# puis une reprise ne coupe pas la ligne (pas d'horodatage au point de reprise) ; Pause (case) est une action booléenne ; les entrées du menu Log
# suivent l'état du journal (menu ouvert par un clic sur « Log », comparé à l'activation des actions lue par Describe) ; Clear tronque le fichier ouvert.
import os
import re
import sys
import time
from gi.repository import GLib
from gtktest import *

LIBELLES = ('To file...', 'Pause', 'Stop', 'Clear')
ACTIONS_JOURNAL = (('To file...', 'log-start'), ('Pause', 'log-paused'), ('Stop', 'log-stop'), ('Clear', 'log-clear'))


def menu_journal(e):
    """Ouvre le menu Log par un clic sur son titre dans la barre de menus (comme test_menus_actions ; F10 dépendrait du focus du VTE) ; rend
    {libellé: sensible} de ses entrées, puis le referme. Vérifie au passage que chaque entrée suit l'activation de son action (Describe)."""
    e.bw.click(e.find1(label='Log'))
    check(wait_until(lambda: [w for w in e.dump() if w.get('type') == 'GtkModelButton' and w.get('label') == 'To file...'], 3), 'menu Log ouvert')
    etats = {w['label']: w['sensitive'] for w in e.dump() if w.get('type') == 'GtkModelButton' and w.get('label') in LIBELLES}
    e.bw.key('Escape')
    wait_until(lambda: not [w for w in e.dump() if w.get('type') == 'GtkModelButton' and w.get('label') == 'To file...'], 3)
    actions = {libelle: e.describe(action)[0] for libelle, action in ACTIONS_JOURNAL}
    check(etats == actions, 'menu Log = activation des actions (%r / %r)' % (etats, actions))
    return etats


SANS_JOURNAL = {'To file...': True, 'Pause': False, 'Stop': False, 'Clear': False}
AVEC_JOURNAL = {'To file...': False, 'Pause': True, 'Stop': True, 'Clear': True}

e = Env('journal_pause')
e.write_config(e.general() + e.profile(echo='false', crlfauto='false', timestamp='true'))
e.start()
e.find1(name='terminal', timeout=10)
e.drain()
etats = menu_journal(e)
check(etats == SANS_JOURNAL, 'menu Log sans journal : %r' % (etats,))

# 1. Pause au milieu d'une ligne puis reprise
logf = os.path.join(e.root, 'pause.txt')
e.action('log-start')
file_dialog(e, 'Log file selection', logf)
check(wait_until(lambda: os.path.exists(logf), 3), 'journal cree')
etats = menu_journal(e)
check(etats == AVEC_JOURNAL, 'menu Log avec journal : %r' % (etats,))
e.write_pty(b'AB\n')
time.sleep(0.5)
e.set_state('log-paused', GLib.Variant('b', True))
check(wait_until(lambda: e.state('log-paused') is True, 2), 'Pause cochee par SetState')
e.write_pty(b'CD')
time.sleep(0.5)
e.action('log-paused')
check(wait_until(lambda: e.state('log-paused') is False, 2), 'Activate bascule la case : reprise')
e.write_pty(b'EF\nGH\n')
time.sleep(0.5)
e.action('log-stop')
time.sleep(0.3)
check(e.state('log-paused') is False, 'Pause decochee a l arret')
data = open(logf, 'rb').read()
check(b'CD' not in data, 'rien ecrit pendant la pause : %r' % data)
check(re.fullmatch(rb'\[[^\]]+\] AB\nEF\n\[[^\]]+\] GH\n', data) is not None, 'ligne reprise sans horodatage au point de reprise : %r' % data)
etats = menu_journal(e)
check(etats == SANS_JOURNAL, 'menu Log apres arret : %r' % (etats,))

# 2. Clear tronque le fichier ouvert
logc = os.path.join(e.root, 'vider.txt')
e.action('log-start')
file_dialog(e, 'Log file selection', logc)
check(wait_until(lambda: os.path.exists(logc), 3), 'second journal cree')
e.write_pty(b'x\n')
check(wait_until(lambda: os.path.getsize(logc) > 0, 3), 'donnees journalisees')
e.action('log-clear')
check(wait_until(lambda: os.path.getsize(logc) == 0, 3), 'Clear tronque le fichier')
e.write_pty(b'y\n')
time.sleep(0.5)
e.action('log-stop')
time.sleep(0.3)
data = open(logc, 'rb').read()
check(re.fullmatch(rb'\[[^\]]+\] y\n', data) is not None, 'seules les donnees apres Clear : %r' % data)
e.finish()
sys.exit(conclude())
