# Écho conservé à sa place au changement de vue, et rétroactif quand on rebascule l'option.
from gtktest import *

e = Env('echo_vues')
ma = os.path.join(e.root, 'macros.ini')
e.write_macros(ma, [('CMD', 'None', 'CMD', '', '', 0, 1000)])
e.write_rc(e.profile(echo='True', macros_file=ma))
e.start(); time.sleep(2)

def ordre_ok():
    t = terminal_text(e.app)
    return 'RX1' in t and '> CMD' in t and t.index('RX1') < t.index('> CMD')

e.write_pty(b'RX1\r\n'); time.sleep(0.5)
act(macro_button(e.app, 'CMD')); time.sleep(0.5)
check(e.read_pty(1.0) == b'CMD', 'macro envoyee au port')
check(wait_until(ordre_ok, 3), 'echo apres la reception', terminal_text(e.app)[:200])
menu(e.app, 'View', 'Hexadecimal'); time.sleep(1.0)
menu(e.app, 'View', 'ASCII'); time.sleep(1.0)
check(wait_until(ordre_ok, 3), 'echo conserve a sa place apres ASCII -> hexa -> ASCII', terminal_text(e.app)[:200])
e.write_pty(b'RX2\r\n')
check(wait_until(lambda: terminal_text(e.app).find('RX2') > terminal_text(e.app).find('> CMD') >= 0, 3), 'reception suivante apres l echo')
menu(e.app, 'Configuration', 'Local echo'); time.sleep(1.0)
check(wait_until(lambda: '> CMD' not in terminal_text(e.app) and 'RX1' in terminal_text(e.app), 3), 'echo desactive : envoi masque, reception gardee')
menu(e.app, 'Configuration', 'Local echo'); time.sleep(1.0)
check(wait_until(ordre_ok, 3), 'echo reactive : envoi reaffiche a sa place')
finish(e)
sys.exit(conclude())
