# Arguments drapeaux et fenêtre « Configure Macros » (spec docs/superpowers/specs/2026-10-15-ouverture-silencieuse-plein-ecran-drapeaux-design.md,
# §2 et §3) : un argument facultatif drapeau est une case du panneau (décochée : rien, cochée : le préfixe seul) ; colonne Flag de l'éditeur ;
# l'éditeur n'est pas transient-for (boutons Agrandir).
import sys
from gtktest import *

e = Env('macros_drapeau')
ini = e.project_path('m.ini')
write_macros_v2(ini, [dict(label='Dhcp', action='config DHCP%s;', arg_optional=['flag'], arg_prefix=[' -save'])])
macro_config(e, ini)
e.start()
d = macro_id(e, 'Dhcp')
case = e.find1(name='macro-%d-arg-0' % d)
check(case.get('type') == 'GtkCheckButton' and case.get('label') == '-save', 'case libellee -save : %r' % case)
check(case.get('active') is False, 'decochee par defaut')
e.drain()
e.activate('macro-%d' % d)
check(wait_until(lambda: b'config DHCP;' in e.read_pty(0.3), 3), 'decochee : -save non envoye')
e.activate('macro-%d-arg-0' % d)
check(wait_until(lambda: e.find1(name='macro-%d-arg-0' % d).get('active') is True, 3), 'cochee')
e.drain()
e.activate('macro-%d' % d)
check(wait_until(lambda: b'config DHCP -save;' in e.read_pty(0.3), 3), 'cochee : -save envoye')

# Éditeur : fenêtre souveraine, colonne Flag, cellule Default en case
open_editor(e)
titre = 'Configure Macros — ' + e.port.rsplit('/', 1)[-1]
fen = [w for w in e.dump() if w['depth'] == 0 and w.get('title') == titre]
check(fen and fen[0].get('transient') is False, 'editeur non transient-for : %r' % (fen[0] if fen else None))
editor_select(e, 0)
check(wait_until(lambda: bool(e.find(name='macro-flag-0')), 3), 'colonne Flag')
check(e.find1(name='macro-flag-0').get('active') is True, 'Flag coche')
check(e.find1(name='macro-default-0').get('type') == 'GtkCheckButton', 'Default : une case')
check('-save' in (e.find1(name='macro-preview').get('label') or '') or '∅' in (e.find1(name='macro-preview').get('label') or ''),
      'apercu : %r' % e.find1(name='macro-preview').get('label'))
e.activate('macro-default-0')       # défaut coché (aucune reconstruction de la table)
check(wait_until(lambda: e.find1(name='macro-default-0').get('active') is True, 3), 'defaut coche')
# F11 (plein écran) : gtk4-broadwayd ne gère pas l'état plein écran, non vérifiable ici
e.activate('macro-ok')
check(wait_until(lambda: not e.find(name='macro-editor'), 3), 'editeur ferme par OK')
check(wait_until(lambda: 'args=1;' in open(ini).read() and 'arg_optional=flag;' in open(ini).read(), 3), '.ini : defaut coche, drapeau (%r)' % open(ini).read())
# Défaut coché : la case du panneau suit ; décochée à la main, la valeur d'usage « 0 » l'emporte sur le défaut
d = macro_id(e, 'Dhcp')
check(wait_until(lambda: e.find1(name='macro-%d-arg-0' % d).get('active') is True, 3), 'panneau : coche par defaut')
e.activate('macro-%d-arg-0' % d)
e.drain()
e.activate('macro-%d' % d)
check(wait_until(lambda: b'config DHCP;' in e.read_pty(0.3), 3), 'decochee malgre le defaut coche : rien')
# En dernier (gtk4-broadwayd 4.24 plante sur cette reconstruction de la table) : décocher Flag rend une entrée à Default
open_editor(e)
editor_select(e, 0)
check(wait_until(lambda: bool(e.find(name='macro-flag-0')), 3), 'colonne Flag (2)')
e.activate('macro-flag-0')          # décoche Flag : facultatif ordinaire
check(wait_until(lambda: e.find1(name='macro-default-0').get('type') == 'GtkEntry', 3), 'Flag decoche : Default redevient une entree')
check(e.find1(name='macro-default-0').get('label') in ('', None), 'defaut vide (jamais le « 1 » de la case) : %r' % e.find1(name='macro-default-0').get('label'))
e.activate('macro-flag-0')
check(wait_until(lambda: e.find1(name='macro-default-0').get('type') == 'GtkCheckButton', 3), 'Flag recoche : Default en case')
e.finish()
sys.exit(conclude())
