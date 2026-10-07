# Fermeture par le bouton « Close » de la barre de titre : la fin de main() ferme la liaison sans toucher aux widgets détruits.
from gtktest import *

e = Env('fermeture_fenetre')
e.write_rc(e.profile())
e.start(); time.sleep(2)
frame = find1(e.app, 'frame')
check(status_text(e.app).startswith(e.pty), 'port ouvert au demarrage', status_text(e.app))
close = find1(frame, 'push button', '^Close$', showing=True)
act(close)
try:
    e.proc.wait(10)
except subprocess.TimeoutExpired:
    pass
check(e.proc.poll() is not None, 'application terminee par le bouton Close')
check(e.proc.returncode == 0, 'code de sortie nul', e.proc.returncode)
finish(e, quit=False)
sys.exit(conclude())
