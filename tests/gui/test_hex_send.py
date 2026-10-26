# Barre d'envoi hexadécimal (spec §6.3) : « 41 42;43 » envoie ABC ; « 1FF » refusé (rien envoyé, message, saisie gardée) ; ↑/↓ parcourent
# l'historique ; ni la saisie refusée ni la saisie vide n'y entrent ; app.show-hex-bar affiche la barre. File d'émission : 20 000 octets vers une
# pty qui n'est pas lue arrivent tous, dans l'ordre, quand elle est lue ensuite ; port fermé : « Nothing sent », saisie gardée.
import sys
from gi.repository import GLib
from gtktest import *


def flash():
    w = e.find(name='status-flash')
    return (w[0].get('label') or '') if w else ''


def saisie():
    w = e.find(name='hex-entry')
    return (w[0].get('label') or '') if w else None


def envoyer(texte):
    if texte:
        e.bw.type(texte)
    e.bw.key('Return')


e = Env('hex_send')
e.write_config(e.general() + e.profile())
e.start()
check(e.state('show-hex-bar') is False, 'barre cachee par defaut')
check(saisie() is None, 'champ absent barre cachee')
e.set_state('show-hex-bar', GLib.Variant('b', True))
check(wait_until(lambda: saisie() is not None, 3), 'app.show-hex-bar affiche la barre')
e.bw.click(e.find1(name='hex-entry'))
e.drain()

envoyer('41 42;43')
check(e.read_pty(1.0) == b'ABC', '41 42;43 envoie ABC')
check(wait_until(lambda: flash() == '3 byte(s) sent!', 2), 'message d envoi : %r' % flash())
check(wait_until(lambda: saisie() == '', 2), 'saisie videe apres envoi')

envoyer('1FF')
check(e.read_pty(1.0) == b'', '1FF : rien envoye')
check(wait_until(lambda: '1FF' in flash() and 'nothing sent' in flash(), 2), '1FF : message dans la barre d etat : %r' % flash())
check(saisie() == '1FF', 'saisie refusee gardee pour correction')
e.set_text('hex-entry', '')
envoyer('')
check(e.read_pty(0.5) == b'', 'saisie vide : rien envoye')
envoyer('44')
check(e.read_pty(1.0) == b'D', '44 envoie D')

e.bw.key('Up')
check(wait_until(lambda: saisie() == '44', 2), 'fleche haut : derniere saisie : %r' % saisie())
e.bw.key('Up')
check(wait_until(lambda: saisie() == '41 42;43', 2), 'fleche haut : saisie precedente (ni 1FF ni vide) : %r' % saisie())
e.bw.key('Up')
check(saisie() == '41 42;43', 'rien avant la premiere saisie')
e.bw.key('Down')
check(wait_until(lambda: saisie() == '44', 2), 'fleche bas : saisie suivante')
e.bw.key('Down')
check(wait_until(lambda: saisie() == '', 2), 'fleche bas apres la derniere : champ vide')

# Gros envoi : bien plus que le tampon de la pty (personne ne la lit pendant l'envoi) ; tout part par la file d'émission, rien n'est perdu
gros = ' '.join(['%02X' % (0x41 + i % 26) for i in range(20000)])
attendu = bytes(0x41 + i % 26 for i in range(20000))
e.set_text('hex-entry', gros)
e.bw.click(e.find1(name='hex-entry'))
e.bw.key('Return')
check(wait_until(lambda: flash() == '20000 byte(s) sent!', 5), 'gros envoi accepte en entier : %r' % flash())
lu = e.read_pty(3.0)
check(lu == attendu, 'gros envoi : %d octets recus sur 20000, dans l ordre : %s' % (len(lu), lu == attendu[:len(lu)]))
check(wait_until(lambda: saisie() == '', 2), 'saisie videe apres le gros envoi')

# Port fermé : rien ne part, la saisie reste pour réessayer
e.action('close-port')
e.set_text('hex-entry', '45')
e.bw.click(e.find1(name='hex-entry'))
e.bw.key('Return')
check(wait_until(lambda: flash() == 'Nothing sent: the port is not open', 3), 'port ferme : message exact : %r' % flash())
check(saisie() == '45', 'port ferme : saisie gardee : %r' % saisie())
check(e.read_pty(0.5) == b'', 'port ferme : rien envoye')
e.set_text('hex-entry', '')
e.set_state('show-hex-bar', GLib.Variant('b', False))
check(wait_until(lambda: saisie() is None, 3), 'barre cachee par l action')
e.finish()
sys.exit(conclude())
