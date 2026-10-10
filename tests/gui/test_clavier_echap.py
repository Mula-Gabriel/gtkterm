# « ESC clear screen » (profil esc_clear_screen=true) : la touche Échap tapée dans le terminal efface l'écran local (historique vidé,
# redessin) sans rien envoyer (M32) ; sans l'option, Échap part sur le port (0x1B).
from gtktest import *

e = Env('clavier_echap')
e.write_config(e.general() + e.profile(esc_clear_screen='true'))
e.start()
e.write_pty(b'AVANT-ECHAP\r\n')
check(wait_until(lambda: 'AVANT-ECHAP' in e.terminal_text(), 3), 'texte recu affiche', e.terminal_text()[:120])
e.bw.click(e.find1(name='terminal'))
e.drain(0.2)
e.bw.key('Escape')
check(wait_until(lambda: 'AVANT-ECHAP' not in e.terminal_text(), 3), 'Echap efface l ecran', e.terminal_text()[:120])
check(e.read_pty(0.5) == b'', 'Echap non envoye au port')
e.write_pty(b'APRES\r\n')
check(wait_until(lambda: 'APRES' in e.terminal_text() and 'AVANT-ECHAP' not in e.terminal_text(), 3), 'reception apres effacement', e.terminal_text()[:120])
e.finish()

e = Env('clavier_echap_envoye')
e.write_config(e.general() + e.profile())
e.start()
e.bw.click(e.find1(name='terminal'))
e.drain(0.2)
e.bw.key('Escape')
check(e.read_pty(0.8) == b'\x1b', 'sans l option, Echap part sur le port')
e.finish()
sys.exit(conclude())
