# Envoi de fichier (portage GTK 4 du scénario de l'ancienne suite GTK 3) : fichier intact, proposé d'abord par l'option -f puis par le dernier envoi ;
# attente d'un caractère renvoyé par l'équipement ; perte du port pendant l'attente (fenêtre de progression fermée, « Error sending file ») ;
# annulation (aucun message) ; fichier vide (fin immédiate, aucun message).
import os
import sys
import time
from gtktest import *

def demarrer(nom, wait_char=-1, args=()):
    e = Env(nom)
    e.write_config(e.general() + e.profile(wait_delay=0, wait_char=wait_char))
    e.start(*args)
    e.find1(name='terminal', timeout=10)
    e.drain()
    return e


def ecrire(e, nom, data):
    os.makedirs(os.path.join(e.root, 'envoi'), exist_ok=True)     # dossier à part : la liste du sélecteur ne contient que ces fichiers
    chemin = os.path.join(e.root, 'envoi', nom)
    with open(chemin, 'wb') as f:
        f.write(data)
    return chemin


def choisir(e, chemin):
    """Send RAW file, chemin saisi dans le sélecteur."""
    e.action('send-file')
    file_dialog(e, 'Send RAW File', chemin)


def accepter_propose(e, nom):
    """Send RAW file, fichier proposé accepté. Mesuré (GTK 4.14.5, broadway) : GtkFileDialog ouvre bien le dossier du fichier proposé mais ne le
    présélectionne pas (la première ligne l'est) ; la proposition se vérifie donc par la présence de la ligne « nom » dans la liste du dossier, qu'on
    sélectionne par un clic avant Entrée."""
    e.action('send-file')
    e.wait_window('Send RAW File')
    e.bw.focus_window('Send RAW File')
    ligne = lambda: [w for w in e.dump() if w.get('type') == 'GtkInscription' and w.get('label') == nom and w.get('mapped')]
    check(wait_until(ligne, 3), 'dossier du fichier propose ouvert, ligne %s presente' % nom)
    e.bw.click(ligne()[0])
    e.bw.key('Return')


def lire(e, n, delai):
    got = b''
    t0 = time.time()
    while len(got) < n and time.time() - t0 < delai:
        got += e.read_pty(0.2)
    return got


def fenetre_envoi(e):
    return e.find(name='file-send-window')


def erreur_envoi(e):
    return any('Error sending file' in t for t in e.alerts())


# 1. Fichier de l'option -f proposé, puis dernier fichier envoyé proposé ; envoi intact sans délai
data = b''.join(b'Ligne %05d ABCDEFGHIJKLMNOPQRSTUVWXYZ\n' % i for i in range(1000))
e = Env('envoi_fichier')
txt = ecrire(e, 'texte.txt', data)        # écrit avant le démarrage : -f le désigne
e.write_config(e.general() + e.profile())
e.start('-f', txt)
e.find1(name='terminal', timeout=10)
e.drain()
accepter_propose(e, 'texte.txt')
got = lire(e, len(data), 10)
check(got == data, 'fichier de -f propose et recu intact (%d/%d octets)' % (len(got), len(data)))
check(wait_until(lambda: not fenetre_envoi(e), 3), 'fenetre de progression fermee a la fin')
data2 = b''.join(b'Autre %03d\n' % i for i in range(100))
txt2 = ecrire(e, 'autre fichier.txt', data2)
choisir(e, txt2)
got = lire(e, len(data2), 5)
check(got == data2, 'second fichier (nom avec espace) recu intact')
check(wait_until(lambda: not fenetre_envoi(e), 3), 'fenetre fermee apres le second envoi')
accepter_propose(e, 'autre fichier.txt')
got = lire(e, len(data2), 5)
check(got == data2, 'dernier fichier envoye propose au suivant')
check(not e.alerts(), 'aucun message %r' % (e.alerts(),))
e.finish()

# 2. Attente du caractère « > » après chaque ligne : l'équipement simulé le renvoie
e = demarrer('envoi_fichier_attente', wait_char=62)
data = b''.join(b'L%03d\n' % i for i in range(50))
txt = ecrire(e, 'texte.txt', data)
choisir(e, txt)
got = b''
t0 = time.time()
while len(got) < len(data) and time.time() - t0 < 20:
    chunk = e.read_pty(0.2)
    got += chunk
    for _ in range(chunk.count(b'\n')):
        e.write_pty(b'>')
check(got == data, 'fichier recu intact avec attente du caractere (%d/%d)' % (len(got), len(data)))
e.finish()

# 3. Perte du port pendant l'attente : fenêtre fermée, « Error sending file »
e = demarrer('envoi_fichier_perte', wait_char=62)
data = b''.join(b'L%03d\n' % i for i in range(50))
txt = ecrire(e, 'texte.txt', data)
choisir(e, txt)
got = b''
t0 = time.time()
while b'\n' not in got and time.time() - t0 < 5:
    got += e.read_pty(0.2)
check(got == b'L000\n', 'premiere ligne envoyee puis attente du caractere : %r' % got)
check(wait_until(lambda: fenetre_envoi(e), 2), 'fenetre de progression affichee')
barre = e.find(name='file-send-progress')
check(barre != [] and (barre[0].get('label') or '').endswith('/ 250 bytes'), 'barre de progression presente, total reel du fichier : %r'
      % [w.get('label') for w in barre])
check([w for w in e.windows() if 'transfer in progress' in (w.get('title') or '')], 'fenetre de progression ouverte, titre "transfer in progress" (%r)' % ([w.get('title') for w in e.windows()],))
e.unplug()
check(wait_until(lambda: not fenetre_envoi(e), 3), 'fenetre de progression fermee a la perte du port')
check(wait_until(lambda: erreur_envoi(e), 3), 'message : Error sending file (%r)' % (e.alerts(),))
e.close_alert()
check(wait_until(lambda: not e.alerts(), 3), 'message ferme')
check(e.find(name='main-window') != [], 'application vivante')
e.finish()

# 4. Annulation pendant l'attente, puis fichier vide : aucun message, rien de plus sur le port
e = demarrer('envoi_fichier_annulation', wait_char=62)
txt = ecrire(e, 'texte.txt', b''.join(b'L%03d\n' % i for i in range(50)))
choisir(e, txt)
check(lire(e, 5, 5) == b'L000\n', 'premiere ligne envoyee')
check(wait_until(lambda: fenetre_envoi(e), 2), 'fenetre de progression affichee')
e.activate('file-send-cancel')
check(wait_until(lambda: not fenetre_envoi(e), 3), 'fenetre fermee par Cancel')
time.sleep(0.8)
check(not e.alerts(), 'aucun message apres Cancel (%r)' % (e.alerts(),))
e.write_pty(b'>')
check(e.read_pty(1.0) == b'', 'plus rien envoye apres Cancel')
vide = ecrire(e, 'vide.txt', b'')
choisir(e, vide)
check(wait_until(lambda: not fenetre_envoi(e), 3), 'fichier vide : fenetre fermee aussitot')
time.sleep(0.5)
check(not e.alerts(), 'fichier vide : aucun message')
check(e.read_pty(0.5) == b'', 'fichier vide : rien sur le port')
e.finish()
sys.exit(conclude())
