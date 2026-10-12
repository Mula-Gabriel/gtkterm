# Conflit d'enregistrement des macros (6c, spec §3.2-15) : un même .ini pour deux onglets libres ; le fichier est réécrit par un tiers ; un
# réglage enregistrable (période de polling) dans l'onglet 2 pose la question « Overwrite / Reload / Cancel » (boîte modale, sans boucle imbriquée) :
# Cancel (bouton par défaut : Entrée n'écrase pas) laisse la version du tiers sur le disque ; Reload relit le fichier (le panneau montre la macro du
# tiers) ; Overwrite écrit la version de l'onglet. Sans profil default (aucun fichier chargé au démarrage), le premier enregistrement ne réécrit pas
# le fichier par défaut présent : question « The macros file already exists. », Cancel le garde, Reload le lit.
import os, sys, time
from gi.repository import GLib
from gtktest import *

e = Env('macros_conflit')
ma = e.macros_path
write_macros_v2(ma, [dict(label='Poll', action='<P>', period=1000)])
macro_config(e, ma)
e.start()


def tiers(label):
    """Réécriture du fichier par un tiers (autre programme) : contenu et identité changent."""
    time.sleep(0.05)
    write_macros_v2(ma, [dict(label=label, action='<%s>' % label)])
    return open(ma).read()


def periode(libelle, valeur):
    """Popover de polling du bouton de ce libellé : période saisie, Entrée."""
    e.bw.click(e.find1(name='macro-%d' % macro_id(e, libelle)), button=3)
    e.find1(name='polling-popover')
    e.bw.key('a', mods=('ctrl',))
    e.bw.type(valeur)
    e.bw.key('Return')


def boite(timeout=5):
    """Boîte de conflit ouverte : texte, chemin en détail et les trois boutons."""
    check(wait_until(lambda: e.alerts(), timeout), 'boite de conflit ouverte')
    texte = e.alerts()[0]
    check('The macros file changed on disk.' in texte and ma in texte, 'texte et chemin de la boite : %r' % texte)
    d = e.dump()
    noms = {w.get('label') for w in d if w.get('type') == 'GtkButton' and w.get('mapped')}
    check({'Overwrite', 'Reload', 'Cancel'} <= noms, 'boutons Overwrite, Reload, Cancel : %r' % sorted(x for x in noms if x))


def boutons_boite():
    """{libellé: widget} des boutons de la boîte ouverte."""
    d = e.dump()
    natifs = {a['native'] for a in e._alert_natives(d)}
    return {w['label']: w for w in d if w['native'] in natifs and w['type'] == 'GtkButton' and w.get('label')}


# Onglet 1 : polling activé (case du popover) → fichier écrit
e.bw.click(e.find1(name='macro-%d' % macro_id(e, 'Poll')), button=3)
e.find1(name='polling-popover')
e.bw.click(e.find1(name='polling-mode'))
check(wait_until(lambda: 'polling=true' in open(ma).read()), 'onglet 1 : polling enregistre')
e.bw.key('Escape')
check(wait_until(lambda: not e.find(name='polling-popover')), 'popover ferme')

# Onglet 2 (même fichier), puis le tiers réécrit le fichier
p2 = e.new_pty()
open_tab(e, p2.port)
check(e.sessions()[1]['macros_file'] == ma, 'onglet 2 : meme fichier')
macro_id(e, '⏱ Poll')
version_tiers = tiers('TIERS')

# Cancel : rien n'est écrit, la version du tiers reste
periode('⏱ Poll', '500')
boite()
e.close_alert('Cancel')
check(wait_until(lambda: not e.alerts()), 'boite fermee par Cancel')
check(open(ma).read() == version_tiers, 'Cancel : fichier = version du tiers')
check('⏱ Poll' in macro_buttons(e), 'Cancel : le panneau montre toujours Poll')

# Cancel est le bouton par défaut (focus), Overwrite porte destructive-action : Entrée n'écrase pas
periode('⏱ Poll', '700')
boite()
b = boutons_boite()
check(b.get('Cancel', {}).get('focus') is True, 'focus sur Cancel : %r' % {k: v.get('focus') for k, v in b.items()})
check('destructive-action' in classes(b.get('Overwrite')) and 'suggested-action' not in classes(b.get('Overwrite')),
      'Overwrite : destructive-action, pas suggested-action : %r' % classes(b.get('Overwrite')))
e.bw.key('Return')
check(wait_until(lambda: not e.alerts()), 'Entree ferme la boite')
check(open(ma).read() == version_tiers, 'Entree : fichier = version du tiers')

# Reload : le fichier est relu, le panneau de l'onglet 2 montre la macro du tiers
periode('⏱ Poll', '600')
boite()
e.close_alert('Reload')
check(wait_until(lambda: 'TIERS' in macro_buttons(e) and '⏱ Poll' not in macro_buttons(e)), 'Reload : panneau de l onglet 2 = TIERS', macro_buttons(e))
check(open(ma).read() == version_tiers, 'Reload : fichier inchange')

# Overwrite : nouvelle réécriture par le tiers, puis la période de l'onglet 2 écrase
tiers('TIERS')
periode('TIERS', '500')
boite()
e.close_alert('Overwrite')
check(wait_until(lambda: 'polling_period_ms=500' in open(ma).read()), 'Overwrite : fichier contient polling_period_ms=500')
check(wait_until(lambda: not e.alerts()), 'boite fermee par Overwrite')

# Sans profil default, le démarrage ne charge aucun fichier de macros : le fichier par défaut, présent et non vide, n'est jamais réécrit en
# silence par le premier enregistrement (question « The macros file already exists. », Cancel par défaut) ; Reload le lit
e.action('quit')
e.proc.wait(10)
e.stop()
write_macros_v2(e.macros_path, [dict(label='DEFAUT', action='<DEFAUT>')])
defaut = open(e.macros_path).read()
e.write_config(e.general() + e.profile(name='autre'))
e.start()
check(macro_buttons(e) == {}, 'aucune macro chargee au demarrage : %r' % sorted(macro_buttons(e)))
open_editor(e)
e.activate('macro-add')
check(wait_until(lambda: len(editor_rows(e)) == 1, 3), 'ligne ajoutee')
ligne = editor_rows(e)[0]
e.set_text(ligne['cells']['label']['id'], 'NEUF')
e.set_text(ligne['cells']['action']['id'], '<NEUF>')
check(wait_until(lambda: [r['action'] for r in editor_rows(e)] == ['<NEUF>'], 3), 'macro NEUF saisie')
e.activate('macro-ok')
check(wait_until(lambda: e.alerts(), 5), 'question avant d ecraser le fichier par defaut')
texte = (e.alerts() or [''])[0]
check('The macros file already exists.' in texte and e.macros_path in texte, 'texte et chemin : %r' % texte)
check(boutons_boite().get('Cancel', {}).get('focus') is True, 'focus sur Cancel')
e.close_alert('Cancel')
check(wait_until(lambda: not e.alerts()), 'boite fermee par Cancel')
check(open(e.macros_path).read() == defaut, 'Cancel : fichier par defaut intact')
open_editor(e)
e.activate('macro-ok')
check(wait_until(lambda: e.alerts(), 5), 'question de nouveau')
e.close_alert('Reload')
check(wait_until(lambda: 'DEFAUT' in macro_buttons(e) and 'NEUF' not in macro_buttons(e)), 'Reload : panneau = fichier par defaut', macro_buttons(e))
check(open(e.macros_path).read() == defaut, 'Reload : fichier par defaut intact')
check(e.sessions()[0]['macros_file'] == e.macros_path, 'Reload : fichier par defaut courant : %r' % e.sessions()[0]['macros_file'])
e.finish()
sys.exit(conclude())
