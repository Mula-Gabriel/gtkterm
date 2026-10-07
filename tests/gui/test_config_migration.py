# Migration d'un .gtktermrc à l'ancien format (parsecfg) : profil relu sans perte, fichier réécrit au nouveau format, copie de sauvegarde.
from gtktest import *

e = Env('config_migration')
ma = os.path.join(e.root, 'mes macros.ini')
e.write_macros(ma, [('MIG', 'None', 'MIG', '', '', 0, 1000)])
ancien = ('[default]\nport\t= %s\nspeed\t= 115200\nbits\t= 8\nstopbits\t= 1\nparity\t= none\nflow\t= none\nwait_delay\t= 0\nwait_char\t= -1\n'
          'echo\t= True\ncrlfauto\t= False\nautoreconnect_enabled\t= False\nesc_clear_screen\t= False\ntimestamp\t= False\n'
          'font\t= "Monospace 13"\nterm_rows\t= 80\nterm_columns\t= 25\nmacros_file\t= "%s"\ntransport_type\t= 0\n'
          'update_url\t= https://invalid.example/none.git\nupdate_check_startup\t= False\nupdate_prefix\t= %s/prefix\n\n'
          '[window]\nwidth=760\n') % (e.pty, ma, e.root)
e.write_rc(ancien)
e.start(force_port=False); time.sleep(2)
check(wait_until(lambda: status_text(e.app) == '%s  115200-8-N-1' % e.pty, 5), 'port du profil migre', status_text(e.app))
check(wait_until(lambda: find(e.app, 'push button', '^MIG$', showing=True), 5), 'macros chargees depuis un chemin avec espace')
act(macro_button(e.app, 'MIG')); time.sleep(0.5)
check(e.read_pty(1.0) == b'MIG', 'macro envoyee')
check(wait_until(lambda: '> MIG' in terminal_text(e.app), 3), 'echo du profil migre actif')
# le dialogue A propos ferme sans changer l'URL n'ecrit rien (F3) : le fichier reste a l'ancien format, pas de copie de sauvegarde
bw = Broadway(e.display)
term = find1(e.app, 'terminal')
menu_key(bw, e.app, 'Help', 'About', focus_obj=term)
about = wait_window(e.app, 'About')
closeb = [b for b in find(about, 'push button', '^Close$')]
act(closeb[-1]); time.sleep(1.0)
check(open(e.rc).read() == ancien and not os.path.exists(e.rc + '.avant-migration'), 'A propos ferme sans changement : rien n est ecrit')
# la fermeture par la barre de titre enregistre l'etat de la fenetre : le fichier passe au nouveau format
act(find1(find1(e.app, 'frame'), 'push button', '^Close$', showing=True))
try:
    e.proc.wait(10)
except subprocess.TimeoutExpired:
    pass
contenu = open(e.rc).read()
check('[profile:default]' in contenu and '[general]' in contenu, 'fichier reecrit au nouveau format', contenu[:300])
check('font=Monospace 13' in contenu, 'police sans guillemets')
check('macros_file=%s' % ma in contenu, 'fichier de macros conserve')
check('term_columns=80' in contenu and 'term_rows=25' in contenu, 'colonnes et lignes remises a l endroit')
check('[window]' in contenu, 'groupe [window] conserve')
check(os.path.exists(e.rc + '.avant-migration') and open(e.rc + '.avant-migration').read() == ancien, 'copie de sauvegarde identique a l original')
finish(e, quit=False)
sys.exit(conclude())
