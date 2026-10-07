# Journal : texte reçu tel que la vue ASCII le montre (CR/LF automatique), sans écho.
from gtktest import *

e = Env('journal')
ma = os.path.join(e.root, 'macros.ini')
e.write_macros(ma, [('X', 'None', 'X', '', '', 0, 1000)])
e.write_rc(e.profile(echo='True', crlfauto='True', macros_file=ma))
e.start(); time.sleep(2)
bw = Broadway(e.display)
term = find1(e.app, 'terminal')
logf = os.path.join(e.root, 'journal.txt')
menu_key(bw, e.app, 'Log', 'To file...', focus_obj=term)
file_dialog(bw, e.app, 'Log file selection', logf)
e.write_pty(b'a\nb\n'); time.sleep(0.5)
act(macro_button(e.app, 'X')); time.sleep(0.5)
bw.click_obj(term); bw.warm(); bw.type('k'); time.sleep(0.5)
e.write_pty(b'c\r'); time.sleep(0.5)
check('> X' in terminal_text(e.app), 'echo visible dans la vue')
menu(e.app, 'Log', 'Stop'); time.sleep(0.5)
data = open(logf, 'rb').read() if os.path.exists(logf) else None
check(data == b'a\r\nb\r\nc\r', 'journal = texte recu transforme, sans echo', data)
finish(e)
sys.exit(conclude())
