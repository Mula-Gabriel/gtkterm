# Vue hexadécimale : l'écho a sa propre ligne et ne décale pas l'index des octets reçus.
from gtktest import *

e = Env('hex_index')
ma = os.path.join(e.root, 'macros.ini')
e.write_macros(ma, [('Z', 'None', 'Z', '', '', 0, 1000)])
e.write_rc(e.profile(echo='True', macros_file=ma))
e.start(); time.sleep(2)
menu(e.app, 'View', 'Hexadecimal'); time.sleep(0.6)
menu(e.app, 'View', 'Show index'); time.sleep(0.6)
e.write_pty(b'ABC'); time.sleep(0.6)
act(macro_button(e.app, 'Z')); time.sleep(0.6)
e.write_pty(b'DEF')
check(wait_until(lambda: '     0: 41 42 43' in terminal_text(e.app), 3), 'premiere ligne recue, index 0', terminal_text(e.app)[:300])
check(wait_until(lambda: '     >  5A' in terminal_text(e.app), 3), 'echo sur sa propre ligne')
check(wait_until(lambda: '     3: 44 45 46' in terminal_text(e.app), 3), 'index des octets recus non decale par l echo', terminal_text(e.app)[:300])
finish(e)
sys.exit(conclude())
