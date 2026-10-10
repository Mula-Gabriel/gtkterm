# Fermeture par le bouton « Close » de la décoration (CSD, cliqué par le faux navigateur) : même chemin que app.quit (close-request) —
# code 0, état de la fenêtre enregistré, x/y retirés.
from gtktest import *

e = Env('fermeture_fenetre')
e.write_config(e.general() + e.profile() + '[window]\nx=10\ny=20\n')
e.start()
check(wait_until(lambda: e.title().startswith('GTKTerm - ' + e.port), 5), 'port ouvert au demarrage', e.title())
check('x' in e.config_group('window'), 'x et y semes avant la fermeture', e.config_group('window'))
mw = e.find1(name='main-window')
close = [w for w in e.find(type='GtkButton', css='close') if w['native'] == mw['native']]
check(bool(close), 'bouton Close de la decoration trouve dans le dump')
if close:
    e.bw.click(close[0])
try:
    e.proc.wait(10)
except subprocess.TimeoutExpired:
    pass
check(e.proc.poll() is not None, 'application terminee par le bouton Close')
check(e.proc.returncode == 0, 'code de sortie nul', e.proc.returncode)
win = e.config_group('window')
check(win.get('width', '').isdigit() and win.get('height', '').isdigit(), 'taille de la fenetre enregistree a la fermeture (absente de la config semee)', win)
check('x' not in win and 'y' not in win, 'x et y retires', win)
e.finish(quit=False)
sys.exit(conclude())
