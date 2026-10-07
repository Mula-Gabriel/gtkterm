# Polling d'une macro : envois répétés par la liaison, arrêt au second clic ; masquer un autre onglet ne l'arrête pas (note EM-1) ;
# la période saisie n'est appliquée qu'à la validation, jamais touche par touche (M22).
from gtktest import *

e = Env('polling')
ma = os.path.join(e.root, 'macros.ini')
e.write_macros(ma, [('PollA', 'None', '<A1>', 'T1', '', 1, 200), ('Autre', 'None', '<B>', 'T2', '', 0, 1000)])
e.write_rc(e.profile(macros_file=ma))
e.start(); time.sleep(2)
bw = Broadway(e.display)
# Une macro en mode polling porte le libellé « ⏱ <nom> » (macro_panel.c, update_button_appearance)
b = macro_button(e.app, '⏱ PollA')
act(b)
d = e.read_pty(1.5)
check(d.count(b'<A1>') >= 4, 'polling actif : au moins 4 envois en 1,5 s', d)

# Masquer l'onglet T2 par le menu contextuel de la barre d'onglets : le polling de T1 continue
bw.click_obj(find1(e.app, 'toggle button', '^T2$', showing=True), button=3); time.sleep(0.8)
act(find1(e.app, 'check menu item', '^T2$', showing=True)); time.sleep(0.8)
e.drain(0.2)
d = e.read_pty(1.0)
check(d.count(b'<A1>') >= 3, 'onglet T2 masque : le polling de T1 continue', d)

# Période : « 2000 » tapé touche par touche n'est pas appliqué avant la validation (sinon « 2 » donnerait une rafale à 2 ms)
bw.click_obj(macro_button(e.app, '⏱ PollA'), button=3); time.sleep(0.8)
bw.key('a', GDK_CONTROL); time.sleep(0.2)
e.drain(0.1)
t0 = time.time()
bw.type('2000')
d = e.read_pty(0.6)
# La saisie dure plus d'une seconde : à 200 ms, environ une émission par 0,2 s ; appliquer « 2 » ou « 20 » donnerait une rafale (une centaine)
duree = time.time() - t0
check(d.count(b'<A1>') <= duree / 0.2 + 2, 'saisie en cours non appliquee (cadence inchangee à 200 ms)', '%d envois en %.1f s' % (d.count(b'<A1>'), duree))
bw.key('Return'); time.sleep(0.8)
e.drain(0.3)
d = e.read_pty(1.5)
check(d.count(b'<A1>') <= 1, 'periode 2000 ms appliquee a la validation', d)
check('polling_period_ms=2000' in open(ma).read(), 'periode enregistree', open(ma).read()[:400])

act(macro_button(e.app, '⏱ PollA')); time.sleep(0.5); e.drain(0.3)
d = e.read_pty(1.0)
check(d == b'', 'polling arrete', d)
finish(e)
sys.exit(conclude())
