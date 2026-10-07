# Polling d'une macro : envois répétés par la liaison, arrêt au second clic.
from gtktest import *

e = Env('polling')
ma = os.path.join(e.root, 'macros.ini')
e.write_macros(ma, [('PollA', 'None', '<A1>', '', '', 1, 200)])
e.write_rc(e.profile(macros_file=ma))
e.start(); time.sleep(2)
# Une macro en mode polling porte le libellé « ⏱ <nom> » (macro_panel.c, update_button_label)
b = macro_button(e.app, '⏱ PollA')
act(b)
d = e.read_pty(1.5)
check(d.count(b'<A1>') >= 4, 'polling actif : au moins 4 envois en 1,5 s', d)
act(b); time.sleep(0.5); e.drain(0.3)
d = e.read_pty(1.0)
check(d == b'', 'polling arrete', d)
finish(e)
sys.exit(conclude())
