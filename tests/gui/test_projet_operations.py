# Opérations de projet (sous-projet 6c, spec §3.2-11 et §3.2-12) : New project (un onglet de projet sans titre, sans macros ni fichier de macros,
# titre « Untitled project - <onglet> ») ; un script en cours : Open, New et Close project demandent « Close all tabs? », Cancel garde tout ;
# Open project avec un projet modifié : Save puis ouverture, Don't save puis ouverture (rien d'écrit) ; rouvrir le projet courant modifié avec
# Save ouvre ce qui vient d'être enregistré (fichier relu), pas la version lue avant la question.
import os, sys, time
from gi.repository import GLib
from gtktest import *

BOUCLE = 'while true do gtkterm.sleep(20) end\n'

e = Env('projet_operations')
write_macros_v2(e.macros_path, [dict(label='BASE', action='<BASE>')])	# fichier par défaut : lu par l'onglet libre du démarrage
defaut = open(e.macros_path).read()
e.write_config(e.general() + e.profile())
e.start(port=False)
pa = os.path.basename(e.port)
b = e.new_pty()
pb = os.path.basename(b.port)
p1 = e.project_path('p1.gtkterm')
p2 = e.project_path('p2.gtkterm')
with open(p2, 'w') as f:
    f.write('[gtkterm-project]\nversion=1\nactive=0\n\n[tab:0]\nprofile_name=default\ntransport=serial\nport=%s\nspeed=115200\nbits=8\nstopbits=1\n'
            'parity=none\nflow=none\nwait_delay=0\nwait_char=-1\necho=false\ncrlfauto=false\nautoreconnect_enabled=false\nesc_clear_screen=false\n'
            'timestamp=false\n\n[tab:0:macro:1]\nlabel=P2\nshortcut=\naction=<P2>\ntab=\npolling=false\npolling_period_ms=1000\n' % b.port)


def libelles():
    return sorted(l for l in macro_buttons(e) if l)


def contenu(p):
    return open(p).read() if os.path.exists(p) else ''


def question(texte):
    check(wait_until(lambda: e.alerts(), 5), 'boite ouverte (%s)' % texte)
    check(any(texte in a for a in e.alerts()), 'texte de la boite : %r' % e.alerts())


def modifier(action):
    e.set_state(action, GLib.Variant('b', not e.state(action)))
    check(wait_until(lambda: e.project().get('modified') is True, 3), 'projet modifie (%s)' % action)


# 1. New project : un onglet de projet sans titre, sans macros ; le fichier par défaut n'y entre pas
check(wait_until(lambda: libelles() == ['BASE'], 5), 'onglet libre : macros du fichier par defaut : %r' % libelles())
e.action('project-new')
check(wait_until(lambda: e.project().get('open') is True, 5), 'New project : projet ouvert : %r' % e.project())
p = e.project()
check(p.get('path') == '' and p.get('name') == 'Untitled project' and p.get('modified') is False, 'projet sans fichier, non modifie : %r' % p)
s = e.sessions()
check(len(s) == 1 and s[0]['in_project'] and s[0]['macros_file'] == '' and s[0]['name'] == pa, 'un onglet de projet sans fichier de macros : %r' % s)
check(wait_until(lambda: libelles() == [], 3), 'New project : aucune macro : %r' % libelles())
check(wait_until(lambda: e.title() == 'Untitled project - %s' % pa, 3), 'titre : %r' % e.title())
check(contenu(e.macros_path) == defaut, 'fichier par defaut intact')

# 2. Un script en cours : Open, New et Close project demandent « Close all tabs? » ; Cancel garde tout
save_project_as(e, p1)
check(wait_until(lambda: e.project().get('path') == p1 and e.project().get('modified') is False, 5), 'projet p1 enregistre : %r' % e.project())
e.set_state('show-script-panel', GLib.Variant('b', True))
check(wait_until(lambda: e.find(name='script-editor')), 'panneau de script')
e.set_text('script-editor', BOUCLE)
e.activate('script-run')
check(wait_until(lambda: e.active_session()['busy']), 'onglet occupe (script en cours)')
id_avant = e.active_session()['id']
for nom, lancer in (('New project', lambda: e.action('project-new')), ('Open project', lambda: open_project(e, p2)),
                    ('Close project', lambda: e.action('project-close'))):
    lancer()
    question('Close all tabs?')
    e.close_alert('Cancel')
    check(wait_until(lambda: not e.alerts(), 3), '%s : confirmation fermee par Cancel' % nom)
    time.sleep(0.3)  # preuve negative : le rappel de Cancel passe par une source d'inactivite, 0,3 s laisse le temps a un remplacement fautif
    s = e.sessions()
    check(len(s) == 1 and s[0]['id'] == id_avant and s[0]['busy'], '%s, Cancel : meme onglet, script en cours : %r' % (nom, s))
    check(e.project().get('path') == p1, '%s, Cancel : projet p1 toujours ouvert : %r' % (nom, e.project()))
e.activate('script-stop')
check(wait_until(lambda: not e.active_session()['busy'], 5), 'script arrete')
e.action('project-save')
check(wait_until(lambda: e.project().get('modified') is False, 3), 'p1 enregistre avec son script')

# 3. Open project avec un projet modifié : Save, puis ouverture de p2
modifier('local-echo')
open_project(e, p2)
question('Save changes to the project?')
e.close_alert('Save')
check(wait_until(lambda: e.project().get('path') == p2, 5), 'Save : p2 ouvert : %r' % e.project())
check(wait_until(lambda: [x['name'] for x in e.sessions()] == [pb], 5), 'Save : onglet de p2 : %r' % e.sessions())
check('echo=true' in contenu(p1), 'Save : p1 enregistre avant l ouverture (echo=true)')
check(wait_until(lambda: libelles() == ['P2'], 3), 'macros de p2 : %r' % libelles())

# 4. Don't save, puis ouverture de p1 : p2 inchangé
avant = contenu(p2)
modifier('local-echo')
open_project(e, p1)
question('Save changes to the project?')
e.close_alert("Don't save")
check(wait_until(lambda: e.project().get('path') == p1, 5), "Don't save : p1 ouvert : %r" % e.project())
check(wait_until(lambda: [x['name'] for x in e.sessions()] == [pa], 5), "Don't save : onglet de p1 : %r" % e.sessions())
check(contenu(p2) == avant, "Don't save : p2 inchange")
check(e.state('local-echo') is True, 'p1 rouvert avec son echo enregistre')

# 5. Rouvrir le projet courant modifié, Save : l'ouverture prend la version qui vient d'être enregistrée
modifier('timestamp')
id_avant = e.active_session()['id']
open_project(e, p1)
question('Save changes to the project?')
e.close_alert('Save')
check(wait_until(lambda: 'timestamp=true' in contenu(p1), 3), 'Save : p1 enregistre (timestamp=true)')
check(wait_until(lambda: e.project().get('path') == p1 and len(e.sessions()) == 1 and e.active_session()['id'] != id_avant, 5),
      'p1 rouvert (nouvel onglet) : %r' % e.sessions())
check(e.state('timestamp') is True, 'p1 rouvert depuis le fichier enregistre (horodatage vrai)')
check(wait_until(lambda: e.project().get('modified') is False, 3), 'p1 rouvert non modifie : %r' % e.project())
e.finish()
sys.exit(conclude())
