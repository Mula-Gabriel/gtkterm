# Frappe clavier avec écho : répétée dans la ligne, sans ligne « > ».
from gtktest import *

e = Env('echo_clavier')
e.write_rc(e.profile(echo='True'))
e.start(); time.sleep(2)
bw = Broadway(e.display)
term = find1(e.app, 'terminal')
e.write_pty(b'prompt> '); time.sleep(0.5)
bw.click_obj(term); bw.warm(); e.drain(0.2)
bw.type('abc'); time.sleep(0.8)
check(e.read_pty(0.5) == b'abc', 'frappe envoyee au port')
check(wait_until(lambda: 'prompt> abc' in terminal_text(e.app), 3), 'frappe repetee dans la ligne', terminal_text(e.app)[:120])
check('> a' not in terminal_text(e.app).replace('prompt> abc', ''), 'pas de ligne > pour la frappe')
finish(e)
sys.exit(conclude())
