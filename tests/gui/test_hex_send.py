# Barre d'envoi hexadécimal (spec §6.3) : « 41 42;43 » envoie ABC ; « 1FF » refusé (rien envoyé, message, saisie gardée) ; ↑/↓ parcourent
# l'historique ; ni la saisie refusée ni la saisie vide n'y entrent ; app.show-hex-bar affiche la barre.
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
e.set_state('show-hex-bar', GLib.Variant('b', False))
check(wait_until(lambda: saisie() is None, 3), 'barre cachee par l action')
e.finish()
sys.exit(conclude())
