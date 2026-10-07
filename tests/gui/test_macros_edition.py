# Fenêtre d'édition des macros : supprimer, déplacer, capturer un raccourci ; arguments et polling suivent leur macro, jamais la
# position (M16) ; « Capture Shortcut » et « Delete Shortcut » fonctionnent en sélection multiple (M44) ; une action contenant « :: » et « | » survit à
# l'enregistrement et au rechargement du fichier (M21).
from gtktest import *


def ouvrir_edition(e, bw, term):
    menu_key(bw, e.app, 'Configuration', 'Macros', focus_obj=term)
    w = wait_window(e.app, 'Configure Macros')
    time.sleep(0.5)
    return w


def table(w):
    return [t for t in find(w, None) if role(t) in ('tree table', 'table')][0]


def selectionner(bw, w, ligne):
    """Sélectionne une seule ligne de la liste des macros (AT-SPI ; à défaut, clic sur la cellule « Label »)."""
    tbl = table(w)
    try:
        Atspi.Selection.clear_selection(tbl)
    except Exception:
        pass
    if not Atspi.Table.add_row_selection(tbl, ligne):
        bw.click_obj(Atspi.Table.get_accessible_at(tbl, ligne, 0))
    time.sleep(0.3)


def libelles(w):
    tbl = table(w)
    return [get_text(Atspi.Table.get_accessible_at(tbl, r, 0)) or name(Atspi.Table.get_accessible_at(tbl, r, 0)) for r in range(Atspi.Table.get_n_rows(tbl))]


def bouton(w, nom):
    return find1(w, 'push button', '^_?%s$' % nom, showing=True)


def envoi(e, lib, attendu):
    e.drain(0.2)
    act(macro_button(e.app, lib))
    d = e.read_pty(0.6)
    check(d == attendu, 'clic %s envoie %r' % (lib, attendu), d)


e = Env('macros_edition')
ma = os.path.join(e.root, 'macros.ini')
e.write_macros_v2(ma, [
    dict(label='A', action='<A>', polling=True, period=200),
    dict(label='B', action='<B%d::x|y>', args=['5']),
    dict(label='C', action='<C%d>', args=['7']),
])
e.write_rc(e.profile(macros_file=ma))
e.start(); time.sleep(2)
bw = Broadway(e.display)
term = find1(e.app, 'terminal')

envoi(e, 'B', b'<B5::x|y>')
envoi(e, 'C', b'<C7>')
act(macro_button(e.app, '⏱ A')); time.sleep(0.5)
check(e.read_pty(0.8).count(b'<A>') >= 2, 'polling de A actif')

# 1. supprimer A (1re ligne) : son polling s'arrête, aucune macro n'en hérite, C garde son argument (M16)
w = ouvrir_edition(e, bw, term)
check(libelles(w) == ['A', 'B', 'C'], 'liste initiale', libelles(w))
selectionner(bw, w, 0)
bw.click_obj(bouton(w, 'Delete')); time.sleep(0.3)
bw.click_obj(bouton(w, 'OK')); time.sleep(1.0)
e.drain(0.3)
check(e.read_pty(0.8) == b'', 'polling de la macro supprimee arrete')
check(not [b for b in find(e.app, 'push button', showing=True) if (name(b) or '').startswith('⏱')], 'aucune macro en mode polling')
envoi(e, 'C', b'<C7>')
envoi(e, 'B', b'<B5::x|y>')

# 2. remonter C au-dessus de B : l'ordre change dans le fichier, les arguments suivent
w = ouvrir_edition(e, bw, term)
selectionner(bw, w, 1)
bw.click_obj(find1(w, 'push button', '^Move _?Up$', showing=True))
time.sleep(0.3)
bw.click_obj(bouton(w, 'OK')); time.sleep(1.0)
texte = open(ma).read()
check('[gtkterm-macros]' in texte and texte.find('label=C') < texte.find('label=B'), 'ordre C puis B enregistre au format v2', texte[:500])
check('action=<B%d::x|y>' in texte and 'args=5;' in texte, 'action avec :: et | enregistree telle quelle', texte[:500])
envoi(e, 'C', b'<C7>')
envoi(e, 'B', b'<B5::x|y>')

# 3. capturer un raccourci pour B (M44 : la liste est en sélection multiple)
w = ouvrir_edition(e, bw, term)
selectionner(bw, w, 1)
bw.click_obj(find1(w, 'push button', '^_?Capture Shortcut$', showing=True)); time.sleep(0.3)
bw.key('F8', GDK_CONTROL); time.sleep(0.5)
bw.click_obj(bouton(w, 'OK')); time.sleep(1.0)
check('shortcut=<Primary>F8' in open(ma).read() or 'shortcut=<Control>F8' in open(ma).read(), 'raccourci capture enregistre', open(ma).read()[:500])
bw.click_obj(term); bw.warm(); e.drain(0.2)
bw.key('F8', GDK_CONTROL)
d = e.read_pty(0.8)
check(d == b'<B5::x|y>', 'Ctrl+F8 envoie la macro B', d)

# 3 bis. supprimer ce raccourci (« Delete Shortcut », M44) : retiré du fichier, la touche ne déclenche plus la macro
w = ouvrir_edition(e, bw, term)
selectionner(bw, w, 1)
bw.click_obj(bouton(w, 'Delete Shortcut')); time.sleep(0.3)
bw.click_obj(bouton(w, 'OK')); time.sleep(1.0)
texte = open(ma).read()
check('F8' not in texte, 'raccourci supprime du fichier', texte[:500])
bw.click_obj(term); bw.warm(); e.drain(0.2)
bw.key('F8', GDK_CONTROL)
d = e.read_pty(0.8)
check(b'<B' not in d, 'Ctrl+F8 ne declenche plus la macro B', d)

# 4. recharger le fichier par le menu : tout est relu à l'identique
menu_key(bw, e.app, 'Configuration', 'Load macros file...', focus_obj=term)
file_dialog(bw, e.app, 'Load macros file', ma)
envoi(e, 'B', b'<B5::x|y>')
envoi(e, 'C', b'<C7>')
finish(e)
sys.exit(conclude())
