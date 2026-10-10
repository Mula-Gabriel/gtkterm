# Polling d'une macro (porté de l'ancienne suite GTK 3) : envois répétés par la liaison, clignotement, arrêt au second clic ;
# masquer un autre onglet ne l'arrête pas (EM-1), masquer le sien l'arrête (§3.2-12) ; la période saisie n'est appliquée qu'à la
# validation, jamais touche par touche (M22) ; période invalide : champ en erreur et message dans le popover ; « Polling Mode » décoché.
import os, re, time
from gtktest import *

e = Env('polling')
ma = e.macros_path
write_macros_v2(ma, [dict(label='PollA', action='<A1>', tab='T1', polling=True, period=200),
                     dict(label='Autre', action='<B>', tab='T2')])
macro_config(e, ma)
e.start()

bouton = 'macro-%d' % macro_id(e, '⏱ PollA')
e.drain()
e.activate(bouton)
d = e.read_pty(1.5)
check(d.count(b'<A1>') >= 4, 'polling actif : au moins 4 envois en 1,5 s (%r)' % d[:80])
check(wait_until(lambda: 'polling-blink' in classes(e.find1(name=bouton)), 2), 'le bouton clignote pendant le polling')


def case_onglet(nom):
    cases = [w for w in e.find(type='GtkCheckButton') if w.get('label') == nom]
    check(len(cases) == 1, 'case %s du menu des onglets' % nom)
    return cases[0]


# Menu des onglets (clic droit sur la barre) : masquer T2 laisse tourner T1
e.bw.click(e.find1(name='macro-tab-T2'), button=3)
e.find1(name='tabs-popover')
check(case_onglet('T2').get('active'), 'case T2 cochee')
e.bw.click(case_onglet('T2'))
check(wait_until(lambda: not e.find(name='macro-tab-T2')), 'onglet T2 masque')
e.bw.key('Escape')
check(wait_until(lambda: not e.find(name='tabs-popover')), 'menu des onglets ferme par Echap')
e.drain()
d = e.read_pty(1.0)
check(d.count(b'<A1>') >= 3, 'onglet T2 masque : le polling de T1 continue (%r)' % d[:80])

# Période : « 2000 » tapé touche par touche n'est pas appliqué avant Entrée (sinon « 2 » donnerait une rafale à 2 ms)
e.bw.click(e.find1(name=bouton), button=3)
e.find1(name='polling-popover')
check(e.find1(name='polling-period').get('label') == '200', 'periode actuelle 200 affichee')
e.bw.key('a', mods=('ctrl',))
e.drain()
t0 = time.time()
e.bw.type('2000')
d = e.read_pty(0.6)
duree = time.time() - t0
check(d.count(b'<A1>') <= duree / 0.2 + 2, 'saisie en cours non appliquee : %d envois en %.1f s' % (d.count(b'<A1>'), duree))
e.bw.key('Return')
check(wait_until(lambda: not e.find(name='polling-popover')), 'Entree : periode valide, popover ferme')
e.drain()
d = e.read_pty(1.5)
check(d.count(b'<A1>') <= 1, 'periode 2000 ms appliquee a la validation (%r)' % d)
check('polling_period_ms=2000' in open(ma).read(), 'periode 2000 enregistree')

# Période invalide : champ en erreur, message, popover ouvert ; Échap ferme sans rien changer
e.bw.click(e.find1(name=bouton), button=3)
e.find1(name='polling-popover')
e.bw.key('a', mods=('ctrl',))
e.bw.type('0')
e.bw.key('Return')
check(wait_until(lambda: 'error' in classes(e.find1(name='polling-period'))), 'periode 0 : champ en erreur')
check('86400000' in (e.find1(name='polling-error').get('label') or ''), 'message de periode invalide dans le popover')
check(bool(e.find(name='polling-popover')), 'periode invalide : le popover reste ouvert')
e.bw.key('Escape')
check(wait_until(lambda: not e.find(name='polling-popover')), 'Echap ferme le popover')
check('polling_period_ms=2000' in open(ma).read(), 'periode invalide non enregistree')

# Second clic : arrêt, plus de clignotement
e.activate(bouton)
time.sleep(0.5)
e.drain()
check(e.read_pty(1.0) == b'', 'polling arrete par le second clic')
check(wait_until(lambda: 'polling-blink' not in classes(e.find1(name=bouton)), 2), 'plus de clignotement a l\'arret')

# Masquer l'onglet de la macro arrête son polling ; le réafficher ne le relance pas
e.activate(bouton)
check(e.read_pty(0.6).count(b'<A1>') >= 1, 'polling relance')
e.bw.click(e.find1(name='macro-tab-T1'), button=3)
e.find1(name='tabs-popover')
e.bw.click(case_onglet('T1'))
check(wait_until(lambda: not e.find(name=bouton)), 'onglet T1 masque')
e.drain()
check(e.read_pty(1.0) == b'', 'onglet masque : son polling est arrete')
e.bw.click(case_onglet('T1'))
check(wait_until(lambda: bool(e.find(name=bouton))), 'onglet T1 reaffiche')
e.bw.key('Escape')
e.drain()
check(e.read_pty(0.8) == b'', 'reafficher l\'onglet ne relance pas le polling')

# « Polling Mode » décoché : libellé sans sablier, mode enregistré
e.bw.click(e.find1(name=bouton), button=3)
mode = e.find1(name='polling-mode')
check(mode.get('active'), 'case Polling Mode cochee')
e.bw.click(mode)
e.bw.key('Escape')
check(wait_until(lambda: e.find1(name=bouton).get('label') == 'PollA'), 'libelle sans le sablier')
check('polling=false' in open(ma).read(), 'mode polling desactive enregistre')
e.finish()
sys.exit(conclude())
