# Limites des arguments de macros (spec docs/superpowers/specs/2026-10-14-arguments-macros-design.md) : panneau (bulle descriptive, champ vide =
# défaut en texte indicatif, valeur hors limites en classe error et envoi refusé avec message dans la barre d'état, longueur et jeu de caractères
# d'un %s, latin1 envoyé en un octet par caractère), puis « Configure Macros » (table des arguments : limites, jeu de caractères, classe ; aperçu
# de la ligne et de ses octets, défaut devenu invalide en rouge), OK en dernier et .ini relu depuis Python.
# Ordre imposé : toute reconstruction du panneau (OK de l'éditeur, Reset arguments…) peut faire tomber gtk4-broadwayd (assertion interne de
# GTK 4.24, SKIP 77) ; les vérifications du panneau passent donc d'abord, l'OK de l'éditeur à la fin.
import time
from gtktest import *

e = Env('macros_limites')
ma = e.macros_path
write_macros_v2(ma, [dict(label='P', action='P%d;', args=['50'], arg_min=['0'], arg_max=['100']),
                     dict(label='N', action='N%s;', arg_min=['1'], arg_max=['4'], arg_charset=['printable']),
                     dict(label='L', action='L%s;', args=['é'], arg_charset=['latin1'])])
macro_config(e, ma)
e.start()


def envoi(libelle, attendu, msg):
    e.drain()
    e.activate('macro-%d' % macro_id(e, libelle))
    d = e.read_pty(0.6)
    check(d == attendu, '%s : clic %s envoie %r (%r)' % (msg, libelle, attendu, d))


def flash():
    w = e.find(name='status-flash')
    return w[0].get('label', '') if w else ''


# 1. Panneau, macro P (entier 0 – 100, défaut 50)
p, n, l = macro_id(e, 'P'), macro_id(e, 'N'), macro_id(e, 'L')
cp, cn = 'macro-%d-arg-0' % p, 'macro-%d-arg-0' % n
w = e.find1(name=cp)
check('integer, 0 – 100 (default 50)' in (w.get('tooltip') or ''), 'bulle de P (%r)' % w.get('tooltip'))
check(w.get('label') == '' and w.get('placeholder') == '50', 'champ P vide, defaut 50 en texte indicatif', (w.get('label'), w.get('placeholder')))
envoi('P', b'P50;', 'champ vide = defaut')
e.set_text(cp, '140')
check(wait_until(lambda: 'error' in classes(e.find1(name=cp))), '140 hors limites : classe error')
check('above maximum 100' in (e.find1(name=cp).get('tooltip') or ''), 'raison dans la bulle (%r)' % e.find1(name=cp).get('tooltip'))
envoi('P', b'', '140 refuse')
check(wait_until(lambda: 'not sent' in flash()), 'message « not sent » dans la barre d\'etat (%r)' % flash())
check('Macro "P" not sent: argument 1: above maximum 100' in flash(), 'texte du message (%r)' % flash())
e.set_text(cp, '')
check(wait_until(lambda: 'error' not in classes(e.find1(name=cp))), 'champ P vide de nouveau valide')
envoi('P', b'P50;', 'champ revide = defaut')

# 2. Macro N (%s, 1 – 4 octets, ASCII imprimable)
w = e.find1(name=cn)
check('text, 1 – 4 bytes, printable ASCII' in (w.get('tooltip') or ''), 'bulle de N (%r)' % w.get('tooltip'))
e.set_text(cn, 'abcde')
check(wait_until(lambda: 'error' in classes(e.find1(name=cn))), 'abcde trop long : classe error')
envoi('N', b'', 'abcde refuse')
e.set_text(cn, 'ab')
check(wait_until(lambda: 'error' not in classes(e.find1(name=cn))), 'ab valide')
envoi('N', b'Nab;', 'ab envoye')
e.set_text(cn, 'aé')
check(wait_until(lambda: 'error' in classes(e.find1(name=cn))), 'é hors ASCII imprimable : classe error')
check('not allowed' in (e.find1(name=cn).get('tooltip') or ''), 'caractere refuse dans la bulle (%r)' % e.find1(name=cn).get('tooltip'))
envoi('N', b'', 'aé refuse')

# 3. Macro L (%s latin1, défaut é) : un octet 0xE9
envoi('L', b'L\xe9;', 'latin1')

# 4. Éditeur : table des arguments et aperçu de P
open_editor(e)
editor_select(e, 0, col='label')
check(wait_until(lambda: bool(e.find(name='macro-defaults'))), 'table des arguments visible pour P')
check(e.find1(name='macro-min-0').get('label') == '0', 'macro-min-0 = 0 (%r)' % e.find1(name='macro-min-0').get('label'))
check(e.find1(name='macro-max-0').get('label') == '100', 'macro-max-0 = 100 (%r)' % e.find1(name='macro-max-0').get('label'))
check(not e.find(name='macro-charset-0'), 'pas de jeu de caracteres pour un entier')
check(e.find1(name='macro-preview').get('label') == 'P50;', 'apercu P50; (%r)' % e.find1(name='macro-preview').get('label'))
octets = e.find1(name='macro-preview-bytes').get('label') or ''
check(octets.startswith('50 35 30 3B') and octets.endswith('4 bytes'), 'octets de l\'apercu (%r)' % octets)
check('#e01b24' not in (e.find1(name='macro-preview').get('markup') or ''), 'defaut 50 valide : pas de rouge')
e.set_text('macro-max-0', '10')
check(wait_until(lambda: '#e01b24' in (e.find1(name='macro-preview').get('markup') or '')),
      'max 10 : defaut 50 en rouge dans l\'apercu (%r)' % e.find1(name='macro-preview').get('markup'))
check('error' in classes(e.find1(name='macro-default-0')), 'champ de defaut 50 en erreur avec max 10')
e.set_text('macro-max-0', 'x')
check(wait_until(lambda: 'error' in classes(e.find1(name='macro-max-0'))), 'limite x invalide : classe error')
e.set_text('macro-max-0', '10')
check(wait_until(lambda: 'error' not in classes(e.find1(name='macro-max-0'))), 'limite 10 valide')
e.set_text('macro-default-0', '7')
check(wait_until(lambda: e.find1(name='macro-preview').get('label') == 'P7;'), 'apercu P7; (%r)' % e.find1(name='macro-preview').get('label'))
check('#e01b24' not in (e.find1(name='macro-preview').get('markup') or ''), 'defaut 7 valide : plus de rouge')
check(e.find1(name='macro-preview-bytes').get('label').startswith('50 37 3B'), 'octets P7; (%r)' % e.find1(name='macro-preview-bytes').get('label'))

# 5. N : jeu « Custom class », entrée de classe visible seulement pour lui
editor_select(e, 1, col='label')
check(wait_until(lambda: bool(e.find(name='macro-charset-0'))), 'jeu de caracteres de N affiche')
check(not e.find(name='macro-class-0'), 'entree de classe cachee pour Printable ASCII')
e.set_text('macro-charset-0', 'Custom class')
check(wait_until(lambda: bool(e.find(name='macro-class-0'))), 'entree de classe visible pour Custom class')
e.set_text('macro-class-0', 'a-c')

# 6. OK en dernier : .ini relu
e.activate('macro-ok')
check(wait_until(lambda: not e.find(name='macro-editor')), 'editeur ferme par OK')


def groupe(label):
    blocs = open(ma).read().split('\n\n')
    return next((b for b in blocs if ('\nlabel=%s\n' % label) in b), '')


check(wait_until(lambda: 'arg_max=10;' in groupe('P')), '.ini : arg_max=10; pour P (%r)' % groupe('P'))
check('args=7;' in groupe('P') and 'arg_min=0;' in groupe('P'), '.ini : args=7; et arg_min=0; pour P (%r)' % groupe('P'))
check('arg_charset=class:a-c;' in groupe('N') and 'arg_max=4;' in groupe('N'), '.ini : classe a-c pour N (%r)' % groupe('N'))
check('arg_charset=latin1;' in groupe('L'), '.ini : latin1 garde pour L (%r)' % groupe('L'))
e.finish()
sys.exit(conclude())
