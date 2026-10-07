# Sauvegarde brute : octets reçus seulement, ni écho ni horodatage ni CR/LF ajouté.
from gtktest import *

e = Env('sauvegarde_brute')
ma = os.path.join(e.root, 'macros.ini')
e.write_macros(ma, [('X', 'None', 'X', '', '', 0, 1000)])
e.write_rc(e.profile(echo='True', crlfauto='True', timestamp='True', macros_file=ma))
e.start(); time.sleep(2)
bw = Broadway(e.display)
term = find1(e.app, 'terminal')
e.write_pty(b'\x01raw\r\n\xff'); time.sleep(0.5)
act(macro_button(e.app, 'X')); time.sleep(0.5)
out = os.path.join(e.root, 'brut.bin')
menu_key(bw, e.app, 'File', 'Save RAW file', focus_obj=term)
file_dialog(bw, e.app, 'Save RAW File', out)
check(wait_until(lambda: os.path.exists(out), 3), 'fichier ecrit')
data = open(out, 'rb').read() if os.path.exists(out) else None
check(data == b'\x01raw\r\n\xff', 'sauvegarde brute = octets recus seuls', data)
finish(e)
sys.exit(conclude())
