# Cas de test_macros_send qui exigent un affichage (raccourci connecté une seule fois, raccourci déclaré traité même si l'écriture échoue,
# fenêtre d'édition fermée au changement de jeu) : sautés dans « meson test » ordinaire, exécutés ici sur le broadway privé.
from gtktest import *

exe = os.environ['GTKTERM_TEST_MACROS_SEND']
e = Env('macros_send_affichage')
e.start_broadway()
env = e.env()
env.pop('LD_PRELOAD', None)
env.update(GDK_BACKEND='broadway', BROADWAY_DISPLAY=':%d' % e.display)
for k in ('DISPLAY', 'WAYLAND_DISPLAY'):
    env.pop(k, None)
p = subprocess.run([exe, '--tap'], env=env, cwd=e.root, capture_output=True, text=True, timeout=120)
with open(os.path.join(e.root, 'test_macros_send.tap'), 'w') as f:
    f.write(p.stdout + p.stderr)
check(p.returncode == 0, 'test_macros_send sort avec le code 0', (p.stdout + p.stderr)[-800:])
sautes = [l for l in p.stdout.splitlines() if '# SKIP' in l]
check(not sautes, 'aucun cas saute (tous exécutés sur l\'affichage)', sautes)
check(any(l.startswith('ok') for l in p.stdout.splitlines()), 'sortie TAP non vide', p.stdout[-300:])
e.close()
with open(os.path.join(e.root, 'result.txt'), 'w') as f:
    f.write('\n'.join(R))
sys.exit(conclude())
