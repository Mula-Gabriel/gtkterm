# Valeurs invalides d'un profil (modifié à la main) : validées et corrigées avant d'être appliquées au terminal (F4) ;
# term_columns=0 n'atteint plus vte_terminal_set_size (critique GTK), term_scrollback=-1 ne donne plus un défilement infini.
from gtktest import *

e = Env('config_valeurs_corrigees')
e.write_rc('[general]\nupdate_check_startup=false\nupdate_url=https://invalid.example/none.git\nupdate_prefix=%s/prefix\n\n'
           '[profile:default]\nport=%s\nspeed=115200\nterm_columns=0\nterm_rows=0\nterm_scrollback=-1\n' % (e.root, e.pty))
e.start(force_port=False); time.sleep(2)
check(wait_until(lambda: status_text(e.app) == '%s  115200-8-N-1' % e.pty, 5), 'profil charge', status_text(e.app))
check('vte_terminal_set_size' not in e.stderr(), 'aucune taille invalide appliquee au terminal', e.stderr()[-400:])
check(alerts(e.app) == [], 'corrections silencieuses : aucune boite')
finish(e)
sys.exit(conclude())
