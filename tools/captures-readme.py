# Captures du README (docs/images/gtkterm-*.png) : GTKTerm sous un gtk4-broadwayd privé, avec des macros de démonstration, un faux appareil sur la
# pty et un serveur TCP local pour le second onglet ; chaque image est écrite par le crochet de test app.test-screenshot. Jamais sur l'écran réel.
# Usage, depuis la racine du dépôt, après « ninja -C build » :
#   GTKTERM_BIN=$PWD/build/src/ui/gtkterm GTKTERM_GUARD=$PWD/build/tests/gui/gtkterm_guard.so GTKTERM_GUI_RUNS=$PWD/build/tests/gui/runs \
#     sh tests/gui/run-gui-test.sh tools/captures-readme.py
import sys, os, time, socket, threading
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tests', 'gui'))
from gtktest import *
from gi.repository import GLib
OUT = os.path.join(ROOT, 'docs', 'images')
SW, SH = 1440, 900

e = Env('readme_demo')
write_macros_v2(e.macros_path, [
    dict(label='AT', action='AT\\r', tab='Modem'),
    dict(label='Identify', action='ATI\\r', tab='Modem'),
    dict(label='Echo', action='ATE%#OnOff\\r', tab='Modem'),
    dict(label='Configure', action='AT+CFG=%#Modes,[Timeout ms]%d\\r', tab='Modem', args=['auto', '500']),
    dict(label='s1', action='[Network]%&', tab='Modem'),
    dict(label='Dial', action='ATD[Number]%s\\r', tab='Modem', args=['+33123456789']),
    dict(label='Hang up', action='ATH\\r', tab='Modem'),
    dict(label='s2', action='[Registers]%&', tab='Modem'),
    dict(label='Read register', action='RD [Address]%u\\r', tab='Modem', args=['16']),
    dict(label='Write register', action='WR [Address]%u [Value]%u\\r', tab='Modem', args=['16', '255']),
    dict(label='Reset', action='ATZ\\r', tab='Modem'),
    dict(label='Read temperature', action='TEMP?\\r', tab='Sensors', polling=True, period=1000),
    dict(label='Read humidity', action='HUM?\\r', tab='Sensors'),
    dict(label='Channel', action='CH %#Channels?\\r', tab='Sensors', args=['1']),
    dict(label='Power on', action='PWR 1\\r', tab='Power'),
    dict(label='Power off', action='PWR 0\\r', tab='Power'),
    dict(label='Dump', action='DBG DUMP\\r', tab='Debug'),
    dict(label='Version', action='VER\\r', tab='Firmware'),
], lists=[('OnOff', [('On', '1'), ('Off', '0')]), ('Modes', [('Auto', 'auto'), ('Manual', 'manual'), ('Sleep', 'sleep')]),
          ('Channels', [('Channel 1', '1'), ('Channel 2', '2'), ('Channel 3', '3'), ('Channel 4', '4')])])
macro_config(e, e.macros_path, '[window]\nwidth=1360\nheight=820\npaned_position=880\nmacro_panel_visible=true\nmaximized=true\nv_paned_position=330\n')

# Faux appareil : répond aux commandes reçues sur la pty
stop = threading.Event()
def device():
    buf = b''
    while not stop.is_set():
        try:
            d = e.read_pty(0.05)
        except OSError:	# pty fermée à la fin du scénario
            return
        if not d:
            continue
        buf += d
        while b'\r' in buf:
            cmd, buf = buf.split(b'\r', 1)
            rep = {b'TEMP?': b'temp=23.8C\r\n', b'HUM?': b'hum=41%\r\n', b'AT': b'OK\r\n', b'ATI': b'DEMO-MODEM rev B\r\nOK\r\n'}.get(cmd.strip(), b'OK\r\n')
            time.sleep(0.02)
            try:
                e.write_pty(rep)
            except OSError:
                return

# Serveur TCP pour le second onglet
srv = socket.socket(); srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); srv.bind(('127.0.0.1', 4000)); srv.listen(1)
def tcp():
    try:
        c, _ = srv.accept()
        c.sendall(b'Welcome to the demo gateway\r\nready> ')
        while not stop.is_set():
            time.sleep(0.1)
    except OSError:
        pass
threading.Thread(target=tcp, daemon=True).start()

e.start_broadway()
from broadway import Broadway
e.bw = Broadway(e.display, dump=e.dump, width=SW, height=SH)
time.sleep(0.3)
bw = e.bw
e.start(connect_browser=False)
e.bw = bw
time.sleep(1.0)

def shot(name):
    e.dump(); time.sleep(0.4); e.dump()
    e.action('test-screenshot', GLib.Variant('s', os.path.join(OUT, 'gtkterm-' + name + '.png')))
    time.sleep(0.4)

# Onglet 2 : client TCP
e.action('new-tab')
e.find1(name='port-dialog', timeout=8)
e.set_text('port-type', 'TCP Client')
time.sleep(0.3)
e.set_text('port-tcp-host', 'localhost')
e.set_text('port-tcp-port', '4000')
e.activate('port-ok')
time.sleep(1.0)
e.set_state('active-tab', GLib.Variant('i', 0))
time.sleep(0.5)

# Journal de démarrage horodaté
for l in [b'DEMO-MODEM firmware v1.4.2 (build 2026-09-30)\r\n', b'Booting...\r\n', b'[ OK ] Flash: 4096 KiB\r\n', b'[ OK ] Sensors: 4 channels\r\n',
          b'[ OK ] Radio: link up, RSSI -61 dBm\r\n', b'temp=23.6C hum=42%\r\n', b'Ready.\r\n']:
    e.write_pty(l); time.sleep(0.12)
threading.Thread(target=device, daemon=True).start()
e.set_state('local-echo', GLib.Variant('b', True))
for lab in ('AT', 'Identify'):
    e.activate('macro-%d' % macro_id(e, lab)); time.sleep(0.4)
time.sleep(0.5)
shot('main')

# Panneau de script
e.set_state('show-macro-panel', GLib.Variant('b', False))
e.set_state('show-script-panel', GLib.Variant('b', True))
time.sleep(0.5)
script = '''-- Poll the device and log each temperature reading
for i = 1, 5 do
    gtkterm.send(0, "TEMP?\\r")
    local data, err = gtkterm.wait_for(0, "\\n", 1000)
    if data then
        gtkterm.log(("reading %d: %s C"):format(i, data:match("temp=([%d.]+)C")))
    else
        gtkterm.log("no answer: " .. err)
    end
    gtkterm.sleep(300)
end
gtkterm.log("done")
'''
e.set_text('script-editor', script)
time.sleep(0.3)
e.activate('script-run')
time.sleep(3.5)
shot('script')

# Vue hexadécimale
e.set_state('show-script-panel', GLib.Variant('b', False))
e.set_state('show-macro-panel', GLib.Variant('b', True))
e.set_state('view-mode', GLib.Variant('s', 'hex'))
e.set_state('hex-index', GLib.Variant('b', True))
time.sleep(0.6)
shot('hex')
e.set_state('view-mode', GLib.Variant('s', 'ascii'))

# Éditeur de macros
open_editor(e)
e.bw.click(editor_rows(e, 'macro-cell')[3]['cells']['shortcut'])	# ligne « Configure » (sans la vérification d'editor_select : liste virtualisée)
time.sleep(0.6)
shot('macro-editor')
e.activate('macro-cancel') if e.find(name='macro-cancel') else None
stop.set(); srv.close()
e.finish()
sys.exit(conclude())
