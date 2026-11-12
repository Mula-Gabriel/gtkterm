# Lignes DTR et RTS au repos à l'ouverture (option du profil, Port › Options avancées) : lignes modem simulées par le garde-fou (DTR, RTS, CTS
# et DSR actives au départ, comme après l'open() du noyau). Profil avec l'option : seuls CTS et DSR restent allumés, Toggle DTR rallume DTR (reset
# volontaire) ; profil sans l'option : rien ne change ; cases cochées dans le dialogue puis OK : port rouvert, DTR et RTS baissés ; RTS/CTS grise RTS.
from gtktest import *

VOYANTS = ['led-%s' % n for n in ('DTR', 'RTS', 'CTS', 'CD', 'DSR', 'RI')]
MODEM = {'GTKTERM_GUARD_FAKE_MODEM': '0x126'}   # DTR | RTS | CTS | DSR


def allumes():
    return [n for n in VOYANTS for w in e.find(name=n) if w.get('sensitive')]


def ouvrir_avance():
    e.action('port')
    e.find1(name='port-dialog')
    e.activate('port-advanced')
    return e.find1(name='port-dtr-low'), e.find1(name='port-rts-low')


# 1. Profil avec l'option : DTR et RTS baissés dès l'ouverture, voyants à l'état réel
e = Env('lignes_ouverture')
e.write_config(e.general() + e.profile(dtr_low_on_open='true', rts_low_on_open='true'))
e.start(connect_browser=False, extra_env=MODEM)
check(wait_until(lambda: allumes() == ['led-CTS', 'led-DSR'], 5), 'DTR et RTS au repos apres l ouverture : %r' % allumes())
e.action('toggle-dtr')
check(wait_until(lambda: allumes() == ['led-DTR', 'led-CTS', 'led-DSR'], 3), 'Toggle DTR leve DTR (reset volontaire) : %r' % allumes())
dtr, rts = ouvrir_avance()
check(dtr.get('active') is True and rts.get('active') is True, 'cases cochees selon le profil')
check(rts.get('sensitive') is True, 'case RTS active sans controle de flux')
e.activate('port-cancel')
check(wait_until(lambda: not e.find(name='port-dialog'), 5), 'Annuler ferme le dialogue')
e.finish()

# 2. Profil sans l'option (fichier d'avant) : lignes laissées telles que le noyau les met ; cocher les cases puis OK les baisse
e = Env('lignes_ouverture_defaut')
e.write_config(e.general() + e.profile())
e.start(connect_browser=False, extra_env=MODEM)
check(wait_until(lambda: allumes() == ['led-DTR', 'led-RTS', 'led-CTS', 'led-DSR'], 5), 'sans l option : DTR et RTS actifs : %r' % allumes())
dtr, rts = ouvrir_avance()
check(dtr.get('active') is False and rts.get('active') is False, 'cases decochees par defaut')
e.set_text('port-flow', 'RTS/CTS')
check(wait_until(lambda: e.find1(name='port-rts-low').get('sensitive') is False, 3), 'RTS/CTS : case RTS grisee')
check('flow control' in (e.find1(name='port-rts-low').get('tooltip') or ''), 'RTS/CTS : la raison est dans l infobulle')
e.set_text('port-flow', 'None')
check(wait_until(lambda: e.find1(name='port-rts-low').get('sensitive') is True, 3), 'sans flux : case RTS de nouveau active')
e.activate('port-dtr-low')
e.activate('port-rts-low')
check(wait_until(lambda: e.find1(name='port-dtr-low').get('active') is True and e.find1(name='port-rts-low').get('active') is True, 3),
      'cases cochees')
e.activate('port-ok')
check(wait_until(lambda: not e.find(name='port-dialog'), 5), 'OK ferme le dialogue')
check(wait_until(lambda: allumes() == ['led-CTS', 'led-DSR'], 5), 'port rouvert : DTR et RTS au repos : %r' % allumes())
e.finish()
sys.exit(conclude())
