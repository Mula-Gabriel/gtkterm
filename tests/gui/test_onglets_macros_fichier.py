# Fichier de macros d'un nouvel onglet quand le profil n'en nomme aucun : le nouvel onglet charge exactement ce que le premier a chargé.
# 1. Aucun profil « default » (premier onglet sans fichier) : le nouvel onglet n'en charge aucun non plus, pas même le fichier par défaut.
# 2. Profil « default » sans macros_file (premier onglet : fichier par défaut relu, contrat de load_profile) : le nouvel onglet relit le même fichier.
import os, sys
from gi.repository import GLib
from gtktest import *

# 1. Pas de profil « default » : défauts silencieux, aucun fichier de macros chargé dans le premier onglet
e = Env('onglets_macros_fichier')
write_macros_v2(e.macros_path, [dict(label='DEF', action='<DEF>')])	# fichier par défaut présent : il ne doit être chargé nulle part
e.write_config(e.general() + e.profile(name='autre'))
e.start()
e.set_state('show-macro-panel', GLib.Variant('b', True))
check(e.sessions()[0]['macros_file'] == '', 'premier onglet sans profil default : aucun fichier de macros', e.sessions())
p2 = e.new_pty()
open_tab(e, p2.port)
check(e.sessions()[1]['macros_file'] == '', 'nouvel onglet : aucun fichier de macros, comme le premier', e.sessions())
check(e.state('show-macro-panel') is True, 'nouvel onglet : panneau de macros affiche')
check('DEF' not in macro_buttons(e), 'nouvel onglet : le fichier par defaut n est pas charge', macro_buttons(e))
e.finish()

# 2. Profil « default » sans macros_file : le premier onglet relit le fichier par défaut, le nouvel onglet aussi
e = Env('onglets_macros_fichier_defaut')
write_macros_v2(e.macros_path, [dict(label='DEF', action='<DEF>')])
e.write_config(e.general() + e.profile())
e.start()
e.set_state('show-macro-panel', GLib.Variant('b', True))
check(e.sessions()[0]['macros_file'] == e.macros_path, 'premier onglet, profil default sans macros_file : fichier par defaut', e.sessions())
check(wait_until(lambda: 'DEF' in macro_buttons(e)), 'premier onglet : macro DEF')
p2 = e.new_pty()
open_tab(e, p2.port)
check(e.sessions()[1]['macros_file'] == e.macros_path, 'nouvel onglet : meme fichier que le premier', e.sessions())
check(wait_until(lambda: 'DEF' in macro_buttons(e)), 'nouvel onglet : macro DEF')
e.finish()
sys.exit(conclude())
