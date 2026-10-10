# Migration d'un .gtktermrc à l'ancien format (parsecfg) : profil relu sans perte (port, écho, fichier de macros dont le chemin contient une espace),
# rien n'est écrit avant la fermeture, fichier réécrit au nouveau format à la fermeture, copie de sauvegarde identique à l'original.
# (« À propos fermé sans changement n'écrit rien » : test_about ; boutons de macros : scénarios du panneau de macros.)
from gtktest import *


def rc():
    with open(e.config_path, encoding='utf-8') as f:
        return f.read()


e = Env('config_migration')
dossier = os.path.dirname(e.config_path)
ma = os.path.join(dossier, 'mes macros.ini')
with open(ma, 'w', encoding='utf-8') as f:
    f.write(macros_v2_text('MIG'))
ancien = ('[default]\nport\t= %s\nspeed\t= 115200\nbits\t= 8\nstopbits\t= 1\nparity\t= none\nflow\t= none\nwait_delay\t= 0\nwait_char\t= -1\n'
          'echo\t= True\ncrlfauto\t= False\nautoreconnect_enabled\t= False\nesc_clear_screen\t= False\ntimestamp\t= False\n'
          'font\t= "Monospace 13"\nterm_rows\t= 80\nterm_columns\t= 25\nmacros_file\t= "%s"\ntransport_type\t= 0\n'
          'update_url\t= https://invalid.example/none.git\nupdate_check_startup\t= False\nupdate_prefix\t= %s/prefix\n\n'
          '[window]\nwidth=760\n') % (e.port, ma, dossier)
e.write_config(ancien)
e.start(port=False)
check(wait_until(lambda: e.status_text() == '%s  115200-8-N-1' % e.port, 5), 'port du profil migre')
time.sleep(1.0)                                # un échec de chargement des macros serait signalé par une boîte différée
check(e.alerts() == [], 'macros chargees sans message (chemin avec espace)')
check(e.state('local-echo') is True, 'echo du profil migre (case du menu)')
e.drain()
e.bw.click(e.find1(name='terminal'))
e.bw.type('mig')
check(e.read_pty(1.0) == b'mig', 'frappe envoyee sur le port')
check(wait_until(lambda: 'mig' in e.terminal_text(), 3), 'echo du profil migre actif')
check(rc() == ancien and not os.path.exists(e.config_path + '.avant-migration'), 'rien n est ecrit avant la fermeture')
e.finish()
contenu = rc()
check('[profile:default]' in contenu and '[general]' in contenu, 'fichier reecrit au nouveau format')
check('font=Monospace 13' in contenu, 'police sans guillemets')
check('macros_file=%s' % ma in contenu, 'fichier de macros conserve')
check('term_columns=80' in contenu and 'term_rows=25' in contenu, 'colonnes et lignes remises a l endroit')
check('[window]' in contenu, 'groupe [window] conserve')
sauvegarde = e.config_path + '.avant-migration'
check(os.path.exists(sauvegarde) and open(sauvegarde, encoding='utf-8').read() == ancien, 'copie de sauvegarde identique a l original')
sys.exit(conclude())
