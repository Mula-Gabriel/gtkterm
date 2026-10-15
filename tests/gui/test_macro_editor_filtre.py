# Configure Macros, filtres et regroupement (spec docs/superpowers/specs/2026-10-15-nom-projet-et-editeur-macros-design.md, §2.3, §3, §6) :
# lignes regroupées à l'ouverture ; filtre Tab: ; Add sous filtre crée la macro dans l'onglet ; changer la cellule Tab regroupe ; Move borné au groupe ;
# filtre List: et Add Entry ; boutons sous le tableau, séparateur avant la table des arguments.
from gtktest import *

e = Env('macro_editor_filtre')
ini = e.project_path('m.ini')
write_macros_v2(ini, [dict(label='a', action='A', tab='T1'), dict(label='b', action='B'), dict(label='c', action='C%d', tab='T1')],
                lists=[('L1', [('x', '1')]), ('L2', [('y', '2')])])
macro_config(e, ini)
e.start()
open_editor(e)
check([r['label'] for r in editor_rows(e)] == ['a', 'c', 'b'], 'regroupe : %r' % [r['label'] for r in editor_rows(e)])
e.set_text('macro-tab-filter', 'T1')
check(wait_until(lambda: [r['label'] for r in editor_rows(e)] == ['a', 'c'], 3), 'filtre T1')
e.activate('macro-add')
check(wait_until(lambda: len(editor_rows(e)) == 3 and editor_rows(e)[2]['tab'] == 'T1', 3), 'Add sous filtre : onglet T1 : %r' % editor_rows(e))
e.set_text('macro-tab-filter', 'General')
check(wait_until(lambda: [r['label'] for r in editor_rows(e)] == ['b'], 3), 'filtre General')
e.set_text('macro-tab-filter', 'All')
# Move borné : « c » (dernier de T1 avant la nouvelle) … la nouvelle ligne, dernière de T1, ne descend pas dans General
editor_select(e, 2)
e.activate('macro-down')
check([r['label'] for r in editor_rows(e)][3] == 'b', 'Move Down refuse au bord du groupe')


def editer_tab(rang, texte):
    """Édition réelle de la cellule Tab de la ligne rang (GtkEditableLabel) : double clic ouvre l'édition (Entrée ne l'ouvre pas sous broadway),
    Fin et Retour arrière effacent (pas Ctrl+A : avec le focus dans la cellule, GTK sélectionne aussi toutes les lignes, list.select-all),
    saisie, Entrée valide."""
    cell = editor_rows(e)[rang]['cells']['tab']
    e.bw.click(cell, settle=0.05)
    e.bw.click(cell, settle=0.05)
    e.bw.key('End')
    for _ in range(len(cell.get('label') or '')):
        e.bw.key('BackSpace', settle=0.05)
    e.bw.type(texte)
    e.bw.key('Return')


editer_tab(3, 'T1')                      # « b » passe dans T1 : regroupé à la fin de l'édition
check(wait_until(lambda: [r['label'] for r in editor_rows(e)] == ['a', 'c', '', 'b'], 3), 'regroupe apres edition : %r' % editor_rows(e))
# « b » était déjà la dernière ligne : un vrai déplacement, « c » passe en T2 (nouveau groupe, après T1), sélection comprise ; puis retour en T1
etiquettes = lambda: [r['label'] for r in editor_rows(e)]
editer_tab(1, 'T2')
check(wait_until(lambda: etiquettes() == ['a', '', 'b', 'c'], 3), 'c deplacee dans son groupe T2 : %r' % etiquettes())
check(wait_until(lambda: [r['selected'] for r in editor_rows(e)] == [False, False, False, True], 3), 'la selection suit c : %r' % [r['selected'] for r in editor_rows(e)])
editer_tab(3, 'T1')
check(wait_until(lambda: [(r['label'], r['tab']) for r in editor_rows(e)] == [('a', 'T1'), ('', 'T1'), ('b', 'T1'), ('c', 'T1')], 3), 'c revenue en T1')
# Review Focus 4 : filtre T1, toutes les lignes T1 (4) passent en T3 → le filtre revient à All
e.set_text('macro-tab-filter', 'T1')
for _ in range(4):
    editer_tab(0, 'T3')
check(wait_until(lambda: len(editor_rows(e)) == 4, 3), 'filtre revenu a All : 4 lignes visibles')
# La dernière ligne éditée (« b ») reste sélectionnée quand le filtre revient à All (GtkMultiSelection garde une ligne retirée puis remise
# dans le même items-changed) : la sélection est vidée par un vrai geste (Ctrl+Maj+A, list.unselect-all) avant de vérifier Delete
editor_select(e, 0)
e.bw.key('A', mods=('ctrl', 'shift'))
check(wait_until(lambda: not any(r['selected'] for r in editor_rows(e)), 3), 'selection videe : %r' % [r['selected'] for r in editor_rows(e)])
e.activate('macro-delete')               # aucune sélection : rien n'est supprimé
check(len(editor_rows(e)) == 4, 'Delete sans selection : rien supprime')
# Le séparateur n'est montré qu'avec la table des arguments ou l'aperçu : une ligne envoyable sélectionnée (« a », action A)
editor_select(e, 0)
# Mise en page : boutons sous le tableau, séparateur au-dessus de la table des arguments
d = e.dump()
liste, add, sep = (e.find1(name=n) for n in ('macro-list', 'macro-add', 'macro-settings-separator'))
check(add['y'] > liste['y'] and sep['y'] > add['y'], 'boutons sous le tableau, separateur dessous')
# Lists
e.bw.click(e.find1(label='Lists', type='GtkLabel'))   # onglet du sélecteur de pages (test-activate ne l'atteint pas : libellé enfant du bouton)
e.set_text('lists-filter', 'L2')
listes = lambda: [r['name'] for r in editor_rows(e, 'list-cell', ('name', 'display', 'value'))]   # cellules « list-cell-name »
check(wait_until(lambda: listes() == ['L2'], 3), 'filtre L2 : %r' % listes())
e.activate('lists-add')
check(wait_until(lambda: listes() == ['L2', 'L2'], 3), 'Add Entry dans L2 : %r' % listes())
e.activate('macro-cancel')
e.finish()
sys.exit(conclude())
