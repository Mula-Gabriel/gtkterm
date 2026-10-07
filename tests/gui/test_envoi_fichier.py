# Envoi de fichier par la liaison : fichier intact, sans délai et avec attente d'un caractère renvoyé par l'équipement.
from gtktest import *

def envoyer(e, chemin):
    bw = Broadway(e.display)
    term = find1(e.app, 'terminal')
    e.drain(0.2)
    menu_key(bw, e.app, 'File', 'Send RAW file', focus_obj=term)
    w = focus_window(bw, e.app, 'Send RAW File')
    bw.key('l', GDK_CONTROL); time.sleep(0.4)
    bw.type(chemin); time.sleep(0.3)
    fix_path_entry(w, chemin)
    bw.key('Return')

# 1. Sans délai
e = Env('envoi_fichier')
e.write_rc(e.profile())
data = b''.join(b'Ligne %05d ABCDEFGHIJKLMNOPQRSTUVWXYZ\n' % i for i in range(1000))
txt = os.path.join(e.root, 'texte.txt')
open(txt, 'wb').write(data)
e.start(); time.sleep(2)
envoyer(e, txt)
got = e.read_pty(6.0)
check(got == data, 'fichier recu intact (%d octets)' % len(data), (len(got), got[:60]))
check(e.proc.poll() is None, 'application vivante')
finish(e)

# 2. Attente du caractère « > » après chaque ligne : l'équipement simulé le renvoie
e = Env('envoi_fichier_attente')
e.write_rc(e.profile(wait_char=62))
data = b''.join(b'L%03d\n' % i for i in range(50))
txt = os.path.join(e.root, 'texte.txt')
open(txt, 'wb').write(data)
e.start(); time.sleep(2)
envoyer(e, txt)
got = b''
t0 = time.time()
while len(got) < len(data) and time.time() - t0 < 20:
    chunk = e.read_pty(0.2)
    got += chunk
    for _ in range(chunk.count(b'\n')):
        e.write_pty(b'>')
check(got == data, 'fichier recu intact avec attente du caractere', (len(got), got[-40:]))
finish(e)

# 3. Perte du port pendant l'attente du caractère : l'équipement ne renvoie rien, le transfert s'arrête avec un message
def fenetres_transfert(app):
    return [w for w in windows(app) if 'transfer in progress' in name(w)]

e = Env('envoi_fichier_perte')
e.write_rc(e.profile(wait_char=62))
data = b''.join(b'L%03d\n' % i for i in range(50))
txt = os.path.join(e.root, 'texte.txt')
open(txt, 'wb').write(data)
e.start(); time.sleep(2)
envoyer(e, txt)
got = b''
t0 = time.time()
while b'\n' not in got and time.time() - t0 < 5:
    got += e.read_pty(0.2)
check(got == b'L000\n', 'premiere ligne envoyee puis attente du caractere', got)
check(wait_until(lambda: fenetres_transfert(e.app), 2), 'fenetre de transfert affichee')
e.unplug()
check(wait_until(lambda: not fenetres_transfert(e.app), 3), 'fenetre de transfert fermee a la perte du port', [name(w) for w in windows(e.app)])
check(wait_until(lambda: alerts(e.app), 3), 'message d erreur affiche')
al = alerts(e.app)
if al:
    check(find(al[0], None, 'Error sending file') != [], 'message : Error sending file', [name(n) for n, _ in walk(al[0])])
    act(find1(al[0], 'push button', '^_?OK$'))
    check(wait_until(lambda: not alerts(e.app), 3), 'message ferme')
check(e.proc.poll() is None, 'application vivante')
finish(e)
sys.exit(conclude())
