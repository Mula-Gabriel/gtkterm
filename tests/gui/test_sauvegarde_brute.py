# Sauvegarde brute (portage GTK 4) : octets reçus seulement, ni écho ni horodatage ni CR/LF ajouté ; « Save RAW file » propose ensuite son propre
# dernier fichier, « Save ASCII file » non (spec §3.2-16).
import os
import sys
import time
from gtktest import *

def champ_nom(e, nom):
    """Champ « Name » du sélecteur d'enregistrement contenant exactement nom (les lignes de la liste de fichiers ne sont pas des GtkText)."""
    return [w for w in e.dump() if w.get('type') in ('GtkText', 'GtkEntry') and w.get('label') == nom]


def fermer_selecteur(e, titre):
    e.bw.focus_window(titre)
    e.bw.key('Escape')
    check(wait_until(lambda: not [w for w in e.windows() if w.get('title') == titre], 3), 'selecteur %s ferme' % titre)


e = Env('sauvegarde_brute')
e.write_config(e.general() + e.profile(echo='true', crlfauto='true', timestamp='true'))
e.start()
term = e.find1(name='terminal', timeout=10)
e.drain()
e.write_pty(b'\x01raw\r\n\xff')
time.sleep(0.5)
e.bw.click(term)
e.bw.type('k')
check(e.read_pty(0.5) == b'k', 'frappe partie sur le port (enregistrement TX dans l historique)')
out = os.path.join(e.root, 'brut.bin')
e.action('save-raw')
file_dialog(e, 'Save RAW File', out)
check(wait_until(lambda: os.path.exists(out), 3), 'fichier ecrit')
data = open(out, 'rb').read() if os.path.exists(out) else None
check(data == b'\x01raw\r\n\xff', 'sauvegarde brute = octets recus seuls : %r' % (data,))

e.action('save-raw')
e.wait_window('Save RAW File')
check(wait_until(lambda: champ_nom(e, 'brut.bin'), 3), 'Save RAW propose son dernier fichier')
fermer_selecteur(e, 'Save RAW File')

# Échec d'écriture (dossier en lecture seule) : message du module affiché une seule fois, chemin et cause compris, sans second enrobage
ro = os.path.join(e.root, 'lecture seule')
os.makedirs(ro)
os.chmod(ro, 0o555)
refuse = os.path.join(ro, 'refuse.bin')
e.action('save-raw')
file_dialog(e, 'Save RAW File', refuse)
check(wait_until(lambda: e.alerts(), 3), 'message d erreur affiche')
texte = '\n'.join(e.alerts())
check(texte.count(refuse) == 1 and 'Permission denied' in texte and texte.count('Cannot') == 1, 'message non double : %r' % texte)
e.close_alert()
os.chmod(ro, 0o755)
e.action('save-ascii')
e.wait_window('Save ASCII File')
time.sleep(0.5)
check(not champ_nom(e, 'brut.bin'), 'Save ASCII ne propose pas le fichier RAW')
fermer_selecteur(e, 'Save ASCII File')
e.finish()
sys.exit(conclude())
