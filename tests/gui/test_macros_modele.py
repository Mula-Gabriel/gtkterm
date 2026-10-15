# Modèles de macros liés (spec docs/superpowers/specs/2026-10-15-modeles-macros-design.md, §3 « GUI ») : dans un projet, « Load macros file… »
# → « Link as template » lie l'onglet au .ini (clé template de test-sessions, libellé « Template: … » de Configure Macros) ; une valeur par défaut
# changée → « Keep in this project » : le modèle n'est pas réécrit, le panneau montre la valeur propre ; Save project écrit macros_template et un
# groupe [tab:0:override:1] ; le modèle réécrit sur le disque puis le projet rouvert : flash « Macros template … updated », valeur propre et nouvelle
# valeur du modèle visibles, projet non modifié ; « Save to template » écrit la valeur changée, jamais la valeur propre ; un nouvel onglet copie le
# lien et app.detach-macros-template (D-Bus) le détache ; un libellé changé pose la question STRUCTURE → « Detach and keep a copy » détache l'onglet.
import os, sys, time
from gi.repository import GLib
from gtktest import *

e = Env('macros_modele')
e.write_config(e.general() + e.profile())
modele = e.project_path('modele.ini')
banc = e.project_path('banc.gtkterm')
write_macros_v2(modele, [dict(label='A', action='A%d;', args=['1']),
                         dict(label='B', action='B%d;', args=['2']),
                         dict(label='C', action='C%d;', args=['3'])])
e.start()
p2 = e.new_pty()


def octets():
    with open(modele, 'rb') as f:
        return f.read()


def defaut(label):
    """Texte indicatif (valeur par défaut résolue) du premier champ d'argument de la macro de ce libellé dans le panneau."""
    w = e.find(name='macro-%d-arg-0' % macro_id(e, label))
    return w[0].get('placeholder') if w else None


def flash():
    w = e.find(name='status-flash')
    return w[0].get('label', '') if w else ''


def groupe(texte, label):
    """Groupe [macro:N] du .ini de ce libellé."""
    return next((b for b in texte.split('\n\n') if ('\nlabel=%s\n' % label) in b), '')


def editer_defaut(rang, valeur, apercu):
    """Configure Macros : ligne rang sélectionnée, valeur par défaut du premier argument, aperçu attendu ; OK."""
    open_editor(e)
    editor_select(e, rang, col='label')
    check(wait_until(lambda: bool(e.find(name='macro-default-0')), 3), 'table des arguments affichee (ligne %d)' % rang)
    e.set_text('macro-default-0', valeur)
    check(wait_until(lambda: e.find1(name='macro-preview').get('label') == apercu, 3),
          'apercu %s : %r' % (apercu, e.find1(name='macro-preview').get('label')))
    e.activate('macro-ok')


def question(detail):
    """Question de l'éditeur d'un onglet lié : texte nommant le modèle et ce texte secondaire."""
    check(wait_until(lambda: e.alerts(), 5), 'question du modele posee')
    texte = (e.alerts() or [''])[0]
    check('These macros come from the template modele.ini' in texte and detail in texte, 'texte de la question : %r' % texte)
    check(e.find(name='macro-editor'), 'editeur encore ouvert pendant la question')


# 1. Projet d'un onglet, Load macros file… → « Link as template »
save_project_as(e, banc)
check(wait_until(lambda: e.project().get('path') == banc and e.sessions()[0]['in_project'], 5), 'projet enregistre : %r' % e.project())
check(not e.describe('detach-macros-template')[0], 'onglet non lie : Detach from template inactif')
e.action('load-macros')
file_dialog(e, 'Load macros file', modele)
check(wait_until(lambda: any(a.startswith('Use this macros file in the project') for a in e.alerts()), 5), 'question copie ou lien : %r' % e.alerts())
e.close_alert('Link as template')
check(wait_until(lambda: e.sessions()[0]['template'] == modele, 5), 'template = chemin du modele : %r' % e.sessions()[0].get('template'))
check(wait_until(lambda: e.describe('detach-macros-template')[0], 3), 'onglet lie : Detach from template actif')
check(wait_until(lambda: sorted(l for l in macro_buttons(e) if l) == ['A', 'B', 'C'], 5), 'panneau : macros du modele : %r' % macro_buttons(e))
check(defaut('A') == '1' and defaut('B') == '2' and defaut('C') == '3', 'defauts du modele 1, 2, 3 : %r' % [defaut(x) for x in 'ABC'])
check(wait_until(lambda: e.project().get('modified') is True, 3), 'projet modifie par le lien')
origine = octets()

# 2. Configure Macros : libellé du modèle ; A = 10 → « Keep in this project » : modèle inchangé, panneau à 10
open_editor(e)
info = e.find1(name='macro-template-info')
check(info.get('label') == 'Template: ' + modele, 'libelle du modele : %r' % info.get('label'))
e.activate('macro-ok')	# rien de changé : l'éditeur se ferme sans question
check(wait_until(lambda: not e.find(name='macro-editor'), 3), 'editeur ferme sans question (aucun changement)')
check(e.alerts() == [], 'aucune question sans changement : %r' % e.alerts())
editer_defaut(0, '10', 'A10;')
question('Keep the new default values in this project only')
e.close_alert('Keep in this project')
check(wait_until(lambda: not e.find(name='macro-editor'), 3), 'Keep in this project : editeur ferme')
check(wait_until(lambda: defaut('A') == '10', 5), 'panneau : defaut de A = 10 : %r' % defaut('A'))
time.sleep(0.5)
check(octets() == origine, 'Keep in this project : modele inchange (octets)')
check(e.sessions()[0]['template'] == modele, 'toujours lie')

# 3. Save project : macros_template et valeur propre dans le .gtkterm
e.action('project-save')
check(wait_until(lambda: e.project().get('modified') is False, 5), 'Save project : non modifie : %r' % e.project())
texte = open(banc, encoding='utf-8').read()
check('macros_template=%s\n' % modele in texte, 'projet : cle macros_template : %r' % texte[:400])
check('[tab:0:override:1]' in texte, 'projet : groupe [tab:0:override:1]')
surcharge = next((b for b in texte.split('\n\n') if b.startswith('[tab:0:override:1]')), '')
check('label=A\n' in surcharge + '\n' and 'arg_1=10' in surcharge, 'valeur propre de A : %r' % surcharge)
check('[tab:0:override:2]' not in texte, 'une seule valeur propre')

# 4. Le modèle est réécrit sur le disque (B = 20), le projet rouvert : flash « updated », A = 10 (projet) et B = 20 (modèle), non modifié
time.sleep(0.05)
write_macros_v2(modele, [dict(label='A', action='A%d;', args=['1']),
                         dict(label='B', action='B%d;', args=['20']),
                         dict(label='C', action='C%d;', args=['3'])])
open_project(e, banc)
check(wait_until(lambda: 'Macros template modele.ini updated' in flash(), 5), 'flash « Macros template modele.ini updated » : %r' % flash())
check('no longer apply' not in flash(), 'aucune valeur abandonnee : %r' % flash())
check(wait_until(lambda: e.sessions()[0]['template'] == modele, 5), 'rouvert : toujours lie : %r' % e.sessions())
check(wait_until(lambda: defaut('A') == '10' and defaut('B') == '20' and defaut('C') == '3', 5),
      'rouvert : A = 10 (projet), B = 20 (modele), C = 3 : %r' % [defaut(x) for x in 'ABC'])
check(e.alerts() == [], 'aucune boite a la reouverture : %r' % e.alerts())
# Preuve négative : le recalcul de « modifié » est regroupé à l'inactivité (au plus un par 300 ms) ; 1,5 s couvre ce regroupement avec une marge de 5
time.sleep(1.5)
check(e.project().get('modified') is False, 'modele reimporte : projet non modifie : %r' % e.project())

# 5. C = 30 → « Save to template » : le modèle reçoit C = 30, garde A = 1 (la valeur propre 10 ne fuit pas)
editer_defaut(2, '30', 'C30;')
question('Keep the new default values in this project only')
e.close_alert('Save to template')
check(wait_until(lambda: not e.find(name='macro-editor'), 3), 'Save to template : editeur ferme')
check(wait_until(lambda: 'args=30;' in groupe(open(modele).read(), 'C'), 5), 'modele : C = 30 : %r' % groupe(open(modele).read(), 'C'))
t = open(modele).read()
check('args=1;' in groupe(t, 'A') and 'args=10;' not in groupe(t, 'A'), 'modele : A = 1, valeur propre non ecrite : %r' % groupe(t, 'A'))
check('args=20;' in groupe(t, 'B'), 'modele : B = 20 garde : %r' % groupe(t, 'B'))
check(wait_until(lambda: defaut('A') == '10' and defaut('C') == '30', 5), 'panneau : A = 10, C = 30 : %r' % [defaut(x) for x in 'ABC'])
check(e.sessions()[0]['template'] == modele, 'toujours lie apres Save to template')

# 6. Nouvel onglet du projet : copie du lien ; app.detach-macros-template (D-Bus) le détache, le premier reste lié
open_tab(e, p2.port)
check(wait_until(lambda: [s['template'] for s in e.sessions()] == [modele, modele], 5), 'nouvel onglet lie au meme modele : %r' % e.sessions())
check(wait_until(lambda: defaut('A') == '10' and defaut('C') == '30', 5), 'nouvel onglet : A = 10, C = 30 : %r' % [defaut(x) for x in 'AC'])
avant = octets()
e.action('detach-macros-template')
check(wait_until(lambda: [s['template'] for s in e.sessions()] == [modele, ''], 5), 'D-Bus : onglet 2 detache, onglet 1 lie : %r' % e.sessions())
check(wait_until(lambda: 'Detached from macros template modele.ini' in flash(), 3), 'flash « Detached from macros template » : %r' % flash())
check(wait_until(lambda: not e.describe('detach-macros-template')[0], 3), 'onglet 2 : Detach from template inactif')
check(defaut('A') == '10', 'onglet 2 detache : valeur propre gardee dans la copie : %r' % defaut('A'))
check(octets() == avant, 'detacher ne touche pas au modele')
e.set_state('active-tab', GLib.Variant('i', 0))
check(wait_until(lambda: e.active_session()['index'] == 0, 3), 'premier onglet actif')
check(wait_until(lambda: e.describe('detach-macros-template')[0], 3), 'onglet 1 (lie) : Detach from template actif')

# 7. Libellé changé → question STRUCTURE → « Detach and keep a copy »
open_editor(e)
lignes = editor_rows(e)
check([r['label'] for r in lignes] == ['A', 'B', 'C'], 'lignes de l\'editeur : %r' % [r['label'] for r in lignes])
e.set_text(lignes[0]['cells']['label']['id'], 'A2')
check(wait_until(lambda: [r['label'] for r in editor_rows(e)] == ['A2', 'B', 'C'], 3), 'libelle saisi : %r' % [r['label'] for r in editor_rows(e)])
e.activate('macro-ok')
question('These changes can only be saved to the template')
b = {w['label']: w for w in e.dump() if w.get('type') == 'GtkButton' and w.get('mapped') and w.get('label')}
check({'Save to template', 'Detach and keep a copy', 'Cancel'} <= set(b), 'boutons STRUCTURE : %r' % sorted(b))
check(b.get('Cancel', {}).get('focus') is True, 'Cancel par defaut : %r' % {k: v.get('focus') for k, v in b.items()})
e.close_alert('Detach and keep a copy')
check(wait_until(lambda: not e.find(name='macro-editor'), 3), 'Detach and keep a copy : editeur ferme')
check(wait_until(lambda: e.sessions()[0]['template'] == '', 5), 'onglet 1 detache : template vide : %r' % e.sessions()[0])
check(wait_until(lambda: not e.describe('detach-macros-template')[0], 3), 'app.detach-macros-template desactivee')
check(wait_until(lambda: 'A2' in macro_buttons(e), 5), 'panneau : A2 : %r' % macro_buttons(e))
check(defaut('A2') == '10' and defaut('C') == '30', 'copie : A2 = 10, C = 30 : %r' % [defaut('A2'), defaut('C')])
check(octets() == avant, 'Detach and keep a copy : modele inchange')
open_editor(e)
check(wait_until(lambda: not e.find(name='macro-template-info'), 3), 'libelle du modele cache')
check(e.find(name='macro-template-info', mapped=None), 'libelle du modele present mais cache')
e.activate('macro-ok')
check(wait_until(lambda: not e.find(name='macro-editor'), 3), 'editeur ferme')
check(wait_until(lambda: e.project().get('modified') is True, 3), 'projet modifie par le detachement')
e.action('project-save')
check(wait_until(lambda: e.project().get('modified') is False, 5), 'projet enregistre')
texte = open(banc, encoding='utf-8').read()
check('macros_template' not in texte and ':override:' not in texte, 'projet : plus de lien ni de valeur propre : %r' % texte[:400])
check(e.alerts() == [], 'aucune boite : %r' % e.alerts())
e.finish()
sys.exit(conclude())
