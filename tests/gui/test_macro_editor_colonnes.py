# Colonnes de « Configure Macros » (sous-projet 6c, spec §5) : chaque colonne est au moins aussi large que son titre ; glisser le bord droit de la
# première colonne de 40 px ne change que sa largeur (la dernière, qui s'étend, rétrécit ; les autres sont inchangées).
import os, sys
from gtktest import *

e = Env('macro_editor_colonnes')
write_macros_v2(e.macros_path, [dict(label='A', action='<A>')])
macro_config(e, e.macros_path)
e.start()
open_editor(e)


def entetes():
    """[(titre, widget GtkColumnViewTitle)] de la liste des macros, de gauche à droite."""
    d = e.dump()
    vue = [w for w in d if w.get('name') == 'macro-list' and w['mapped']][0]
    ts = sorted((w for w in d if w['type'] == 'GtkColumnViewTitle' and w['native'] == vue['native'] and w['mapped'] and w.get('x') is not None
                 and vue['y'] <= w['y'] < vue['y'] + vue['h'] and vue['x'] <= w['x'] < vue['x'] + vue['w']), key=lambda w: w['x'])
    out = []
    for t in ts:
        lab = [w for w in d if w['native'] == t['native'] and w['type'] == 'GtkLabel' and w['mapped'] and t['x'] <= w['x'] < t['x'] + t['w']
               and t['y'] <= w['y'] < t['y'] + t['h']]
        out.append((lab[0]['label'] if lab else None, t, lab[0] if lab else None))
    return out


h = entetes()
check([t for t, _, _ in h] == ['Label', 'Shortcut', 'Action', 'Tab'], 'quatre colonnes : %r' % [t for t, _, _ in h])
for titre, w, lab in h:
    check(w['w'] >= lab['natural_w'], 'colonne %s : largeur %d >= largeur naturelle du titre %d (mesuree, pas allouee)' % (titre, w['w'], lab['natural_w']))

# Glisser le bord droit de la première colonne de 40 px vers la droite
def largeurs():
    return [w['w'] for _, w, _ in entetes()]


def glisser(dx):
    t = e.bw.settled(entetes()[0][1])
    sid = e.bw.surface_for(t)
    y = t['y'] + t['h'] // 2
    x = t['x'] + t['w'] - 2
    e.bw.drag_in_surface(sid, x, y, x + dx, y)


def attendre(pred):
    return wait_until(lambda: pred(largeurs()), 3)


avant = largeurs()
glisser(40)
check(attendre(lambda a: a[0] != avant[0]), 'glisser : la premiere colonne change : %r -> %r' % (avant, largeurs()))
apres = largeurs()
check(abs(apres[0] - avant[0] - 40) <= 4, 'premiere colonne +40 px (+-4) : %d -> %d' % (avant[0], apres[0]))
check(apres[1] == avant[1] and apres[2] == avant[2], 'colonnes 2 et 3 inchangees : %r -> %r' % (avant, apres))
check(apres[3] < avant[3] and abs(avant[3] - apres[3] - (apres[0] - avant[0])) <= 4, 'seule la derniere colonne retrecit : %r -> %r' % (avant, apres))
check(sum(apres) == sum(avant), 'largeur totale inchangee : %r -> %r' % (sum(avant), sum(apres)))

# Retour : glisser de -40 rend la largeur d'origine
glisser(-40)
check(attendre(lambda a: abs(a[0] - avant[0]) <= 4), 'glisser de -40 : largeur d origine : %r -> %r' % (avant, largeurs()))

# Jamais plus étroite que le titre : glisser très loin vers la gauche
glisser(-300)
check(attendre(lambda a: a[0] < avant[0]), 'glisser vers la gauche : la colonne retrecit : %r' % largeurs())
h = entetes()
for titre, w, lab in h:
    check(w['w'] >= lab['natural_w'], 'apres retrecissement, colonne %s : largeur %d >= largeur naturelle du titre %d' % (titre, w['w'], lab['natural_w']))
check(len(h) == 4 and h[0][1]['w'] <= avant[0] - 40, 'la colonne a bien retreci : %r' % largeurs())
e.activate('macro-cancel')
check(wait_until(lambda: not e.find(name='macro-editor'), 3), 'editeur ferme par Cancel')
e.finish()
sys.exit(conclude())
