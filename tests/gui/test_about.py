# Boîte À propos GTK 4 : ouverte par app.about, fermée par Échap, elle n'écrit rien — un .gtktermrc à l'ancien format reste intact, sans copie
# .avant-migration (porté de test_config_migration) ; auteurs corrigés, révision git affichée, plus de champ d'URL ; une seule instance.
from gtktest import *

e = Env('about')
ancien = ('[default]\nport\t= %s\nspeed\t= 115200\nbits\t= 8\nstopbits\t= 1\nparity\t= none\nflow\t= none\nwait_delay\t= 0\nwait_char\t= -1\n'
          'echo\t= False\ncrlfauto\t= False\nautoreconnect_enabled\t= False\nesc_clear_screen\t= False\ntimestamp\t= False\n'
          'update_url\t= https://invalid.example/none.git\nupdate_check_startup\t= False\n\n[window]\nwidth=760\n') % e.port
e.write_config(ancien)
e.start()

e.action('about')
e.wait_window('About GTKTerm')
d = e.dump()
check(bool(e.find(d, name='about-dialog')), 'boite A propos ouverte')
textes = ' '.join(w.get('label') or '' for w in e.find(d))
check('Julien Schmitt' in textes and 'Schimtt' not in textes, 'nom de l auteur corrige', textes[:300])
check(local_revision() in textes, 'revision git affichee', textes[:300])
check(not e.find(d, name='update-url') and 'github.com' not in textes, 'plus d URL dans A propos')

e.action('about')
time.sleep(0.5)
check(len(e.find(name='about-dialog')) == 1, 'une seule boite A propos')

e.bw.focus_window('About GTKTerm')
e.bw.key('Escape')
check(wait_until(lambda: not e.find(name='about-dialog'), 5), 'A propos fermee par Echap')
contenu = e.read_config()
check(contenu == ancien and not os.path.exists(e.config_path + '.avant-migration'), 'A propos fermee : rien n est ecrit')
e.finish()
sys.exit(conclude())
