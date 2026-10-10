# Valeurs invalides d'un profil (modifié à la main) : validées et corrigées avant d'être appliquées au terminal (F4) ; term_columns=0
# n'atteint pas vte_terminal_set_size (alerte GTK), term_scrollback=-1 ne donne pas un défilement infini ; corrections silencieuses.
import sys
from gtktest import *

e = Env('config_valeurs_corrigees')
e.write_config(e.general() + e.profile(term_columns=0, term_rows=0, term_scrollback=-1))
e.start(port=False, connect_browser=False)
check(wait_until(lambda: e.status_text() == '%s  115200-8-N-1' % e.port, 5), 'profil charge : %r' % e.status_text())
check(e.alerts() == [], 'corrections silencieuses : aucune boite')
e.finish()
sys.exit(conclude())
