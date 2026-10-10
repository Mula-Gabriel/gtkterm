# Fermeture d'un onglet occupé (spec §3.2-11) : confirmation si un script tourne, Annuler garde l'onglet, « Close tab » l'arrête proprement ;
# onglet libre fermé sans confirmation ; onglet qui envoie un fichier avec un journal actif : confirmation, puis aucune autre boîte, fenêtre de
# progression fermée, l'autre onglet fonctionne ; fermeture de la fenêtre avec plusieurs onglets dont un script en cours : code 0, aucun
# avertissement, [window] écrit, [session] script_path du premier onglet gardé (le panneau actif n'a jamais eu de chemin).
import os, sys, time
from gi.repository import GLib
from gtktest import *

BOUCLE = 'while true do gtkterm.sleep(20) end\n'

e = Env('onglets_fermeture')
script = os.path.join(e.root, 'script du premier onglet.lua')
with open(script, 'w') as f:
    f.write('print("premier")\n')
# Délai de fin de ligne : l'envoi de fichier dure (200 lignes × 50 ms)
e.write_config(e.general() + e.profile(wait_delay=50) + '[session]\nscript_path=%s\n' % script)
e.start()
p2 = e.new_pty()
open_tab(e, p2.port)
e.set_state('show-script-panel', GLib.Variant('b', True))
check(wait_until(lambda: e.find(name='script-editor')), 'panneau de script du second onglet')
e.set_text('script-editor', BOUCLE)
e.activate('script-run')
check(wait_until(lambda: e.active_session()['busy']), 'second onglet occupe (script en cours)')
e.action('close-tab')
check(wait_until(lambda: e.alerts(), 3), 'confirmation demandee')
e.close_alert('Cancel')
check(len(e.sessions()) == 2, 'Annuler garde l onglet')
check(e.active_session()['busy'], 'le script tourne toujours')
e.action('close-tab')
check(wait_until(lambda: e.alerts(), 3), 'confirmation demandee de nouveau')
e.close_alert('Close tab')
check(wait_until(lambda: len(e.sessions()) == 1, 6), 'onglet ferme apres confirmation')
check(not e.sessions()[0]['busy'], 'onglet restant libre')

# Sans script : pas de confirmation
p3 = e.new_pty()
open_tab(e, p3.port)
e.action('close-tab')
check(wait_until(lambda: len(e.sessions()) == 1, 3), 'onglet libre ferme sans confirmation')
check(not e.alerts(), 'aucune boite')

# Envoi de fichier en cours et journal actif : confirmation, puis rien d'autre
p4 = e.new_pty()
open_tab(e, p4.port)
journal = os.path.join(e.root, 'journal.txt')
e.action('log-start')
file_dialog(e, 'Log file selection', journal)
check(wait_until(lambda: os.path.exists(journal), 3), 'journal du troisieme onglet cree')
os.makedirs(os.path.join(e.root, 'envoi'), exist_ok=True)
fichier = os.path.join(e.root, 'envoi', 'long.txt')
with open(fichier, 'wb') as f:
    f.write(b''.join(b'L%03d\n' % i for i in range(200)))
e.action('send-file')
file_dialog(e, 'Send RAW File', fichier)
check(wait_until(lambda: e.find(name='file-send-window'), 3), 'fenetre de progression affichee')
check(wait_until(lambda: e.active_session()['busy']), 'onglet occupe (envoi de fichier)')
check(len(p4.read(0.5)) > 0, 'envoi en cours sur le port de l onglet')
e.action('close-tab')
check(wait_until(lambda: e.alerts(), 3), 'confirmation demandee (envoi en cours)')
check(any('Close this tab?' in a for a in e.alerts()), 'question de la confirmation', e.alerts())
e.close_alert('Close tab')
check(wait_until(lambda: len(e.sessions()) == 1, 6), 'onglet ferme pendant l envoi')
check(wait_until(lambda: not e.find(name='file-send-window'), 3), 'fenetre de progression fermee')
time.sleep(0.8)
check(not e.alerts(), 'aucune autre boite', e.alerts())
e.drain()
e.bw.click(e.find1(name='terminal'))
e.bw.type('ok')
check(e.read_pty(1.0) == b'ok', 'l autre onglet fonctionne : frappe sur son port')

# Quitter avec deux onglets dont un script en cours ; l'onglet actif n'a jamais eu de script enregistré
open_tab(e, p3.port)
e.set_state('show-script-panel', GLib.Variant('b', True))
e.set_text('script-editor', BOUCLE)
e.activate('script-run')
check(wait_until(lambda: e.active_session()['busy']), 'script en cours avant de quitter')
e.finish()	# app.quit : code 0, stderr sans erreur, aucun rapport ASan
check('[window]' in e.read_config(), '[window] enregistre a la fermeture')
check(e.config_group('session').get('script_path') == script, '[session] script_path du premier onglet garde', e.config_group('session'))
sys.exit(conclude())
