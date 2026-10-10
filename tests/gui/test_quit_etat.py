# app.quit enregistre l'état de la fenêtre ([window] width/height/maximized) et retire x/y (spec §3.2-7 et §4.4) ; l'état enregistré est
# restauré au lancement suivant ; le reste du fichier (profil, [general]) est conservé.
from gtktest import *

e = Env('quit_etat')
e.write_config(e.general() + e.profile() + '[window]\nx=33\ny=44\nwidth=640\nheight=480\nmaximized=false\n')
e.start(connect_browser=False)
mw = e.find1(name='main-window')
check((mw['w'], mw['h']) == (640, 480), 'taille restauree depuis [window]', (mw['w'], mw['h']))
e.action('quit')
try:
    e.proc.wait(10)
except subprocess.TimeoutExpired:
    pass
check(e.proc.poll() is not None and e.proc.returncode == 0, 'app.quit : fin avec le code 0', e.proc.returncode)
win = e.config_group('window')
check(win.get('width') == '640' and win.get('height') == '480', 'largeur et hauteur enregistrees', win)
check(win.get('maximized') == 'false', 'maximized enregistre', win)
check('x' not in win and 'y' not in win, 'x et y retires', win)
check(e.config_group('profile:default').get('port') == e.port, 'profil conserve', e.read_config()[:300])
check(e.config_group('general').get('update_check_startup') == 'false', '[general] conserve', e.read_config()[:300])

# Relance : même état, sans x ni y
e.start(connect_browser=False)
mw = e.find1(name='main-window')
check((mw['w'], mw['h']) == (640, 480), 'taille restauree au lancement suivant', (mw['w'], mw['h']))
e.finish()
sys.exit(conclude())
