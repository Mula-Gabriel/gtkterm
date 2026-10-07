# Raccourcis de macros : la macro part par la liaison, la touche n'atteint pas le terminal (M43).
from gtktest import *

e = Env('raccourcis_macros')
ma = os.path.join(e.root, 'macros.ini')
e.write_macros(ma, [('M1', '<Control>F5', '<M1>', '', '', 0, 1000), ('M2', '<Control>F6', '<M2>', '', '', 0, 1000)])
e.write_rc(e.profile(macros_file=ma))
e.start(); time.sleep(2)
bw = Broadway(e.display)
term = find1(e.app, 'terminal')
for touche, attendu in (('F5', b'<M1>'), ('F6', b'<M2>')):
    bw.click_obj(term); bw.warm(); e.drain(0.2)
    bw.key(touche, GDK_CONTROL)
    d = e.read_pty(0.8)
    check(d == attendu, 'Ctrl+%s envoie %r seul' % (touche, attendu), d)
finish(e)
sys.exit(conclude())
