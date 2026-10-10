# Journal (portage GTK 4) : texte reçu tel que la vue ASCII le montre (CR/LF automatique), sans écho ; chemin avec espace (Review Focus 5),
# enregistré et reproposé à l'identique.
import os
import sys
import time
from gtktest import *

e = Env('journal')
e.write_config(e.general() + e.profile(echo='true', crlfauto='true'))
e.start()
term = e.find1(name='terminal', timeout=10)
e.drain()
logf = os.path.join(e.root, 'journal du port.txt')
check(e.state('log-paused') is False, 'Pause decochee au demarrage')
e.action('log-start')
file_dialog(e, 'Log file selection', logf)
check(wait_until(lambda: os.path.exists(logf), 3), 'journal cree au chemin avec espace')
e.write_pty(b'a\nb\n')
time.sleep(0.5)
e.bw.click(term)
e.bw.type('k')
time.sleep(0.5)
e.write_pty(b'c\r')
time.sleep(0.5)
check('k' in e.terminal_text(), 'echo visible dans la vue')
e.action('log-stop')
time.sleep(0.5)
data = open(logf, 'rb').read() if os.path.exists(logf) else None
check(data == b'a\r\nb\r\nc\r', 'journal = texte recu transforme, sans echo : %r' % (data,))
e.action('log-start')
e.wait_window('Log file selection')
check(wait_until(lambda: [w for w in e.dump() if w.get('type') in ('GtkText', 'GtkEntry') and w.get('label') == 'journal du port.txt'], 3),
      'dernier journal repropose a l identique')
e.bw.focus_window('Log file selection')
e.bw.key('Escape')
check(wait_until(lambda: not [w for w in e.windows() if w.get('title') == 'Log file selection'], 3), 'selecteur ferme sans journal')
check(e.state('log-paused') is False, 'aucun journal apres annulation')

# Journal impossible à créer (dossier en lecture seule) : message du module affiché une seule fois, sans second enrobage
ro = os.path.join(e.root, 'lecture seule')
os.makedirs(ro)
os.chmod(ro, 0o555)
refuse = os.path.join(ro, 'refuse.txt')
e.action('log-start')
file_dialog(e, 'Log file selection', refuse)
check(wait_until(lambda: e.alerts(), 3), 'message d erreur affiche')
texte = '\n'.join(e.alerts())
check(texte.count(refuse) == 1 and 'Permission denied' in texte and texte.count('Cannot') == 1, 'message non double : %r' % texte)
e.close_alert()
check(e.state('log-paused') is False and not os.path.exists(refuse), 'aucun journal ouvert apres l echec')
os.chmod(ro, 0o755)
e.finish()
sys.exit(conclude())
