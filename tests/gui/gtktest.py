# Bibliothèque de la suite graphique GTK 4 (tests/gui/) : environnement isolé, pty, gtk4-broadwayd privé, pilotage par les actions
# D-Bus org.gtk.Actions (actions app.* de GTKTerm) et par le crochet de test (src/ui/test_hooks.c : test-dump, test-terminal-text,
# test-activate, test-set-text), faux navigateur broadway (broadway.py) pour les vrais gestes, assertions.
# Sous broadway, GTK 4 n'a pas d'AT-SPI : tout ce que la suite GTK 3 lisait par AT-SPI se lit dans le dump du crochet.
import os, sys, time, shutil, signal, subprocess, select, tty, re, socket, tempfile, zlib, atexit

import gi
gi.require_version('Gio', '2.0')
from gi.repository import Gio, GLib

SCR = os.path.dirname(os.path.abspath(__file__))
BIN = os.environ['GTKTERM_BIN']
GUARD = os.environ['GTKTERM_GUARD']
RUNS = os.environ['GTKTERM_GUI_RUNS']
FAKEBIN = os.path.join(SCR, 'bin')
APP_ID = 'eu.mulagabriel.gtkterm.test.p%d'
APP_PATH = '/eu/mulagabriel/gtkterm/test/p%d'

R = []
FAILS = []
_ENVS = []
# Bruit connu de GTK 4 sous broadway, sans rapport avec GTKTerm (aucun mesuré par l'essai du 2026-10-09)
KNOWN_NOISE = ()
BAD_STDERR = ('runtime error', 'Sanitizer', 'CRITICAL', '-WARNING **', 'EMGUARD', 'Segmentation', 'GTKTERM-TEST:')


def log(*a):
    print('[gui]', *a, flush=True)


def rep(*a):
    s = ' '.join(str(x) for x in a)
    R.append(s)
    log(s)


def check(cond, msg, detail=''):
    rep(('OK    ' if cond else 'ECHEC ') + msg + ((' : %r' % (detail,)) if (detail != '' and not cond) else ''))
    if not cond:
        FAILS.append(msg)
    return cond


def wait_until(pred, timeout=5, step=0.1):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            v = pred()
            if v:
                return v
        except Exception:
            pass
        time.sleep(step)
    try:
        return pred()
    except Exception:
        return False


def local_revision():
    """GIT_REVISION de la construction testée (git_revision.h) ; « unknown » si introuvable."""
    try:
        with open(os.environ.get('GTKTERM_GIT_REVISION_H', ''), encoding='utf-8') as f:
            m = re.search(r'#define GIT_REVISION "([^"]*)"', f.read())
        return m.group(1) if m else 'unknown'
    except OSError:
        return 'unknown'


def conclude():
    rep('BILAN : %d echec(s)%s' % (len(FAILS), (' : ' + ', '.join(FAILS)) if FAILS else ''))
    return 1 if FAILS else 0


@atexit.register
def _cleanup_all():
    for e in _ENVS:
        try:
            e.close()
        except Exception:
            pass


def _on_sigterm(signum, frame):
    # meson (délai dépassé) ou timeout : nettoyage par atexit (processus tués par groupe) avant de sortir
    sys.exit(124)


signal.signal(signal.SIGTERM, _on_sigterm)


def _port_free(port):
    s = socket.socket()
    try:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind(('127.0.0.1', port))
        return True
    except OSError:
        return False
    finally:
        s.close()


def _kill_group(proc, timeout=5):
    if proc is None or proc.poll() is not None:
        return
    try:
        os.killpg(proc.pid, signal.SIGTERM)
        proc.wait(timeout)
    except subprocess.TimeoutExpired:
        os.killpg(proc.pid, signal.SIGKILL)
        proc.wait()
    except ProcessLookupError:
        pass


class Env:
    """Un lancement de gtkterm : dossiers privés runs/<name>/, paire pty (esclave brute = port série), gtk4-broadwayd propre."""

    def __init__(self, name, lang='C', display=None):
        _ENVS.append(self)
        self.name = name
        self.root = os.path.join(RUNS, name)
        if os.path.exists(self.root):
            shutil.rmtree(self.root)
        self.home = os.path.join(self.root, 'home')
        self.cfg = os.path.join(self.root, 'config')
        for d in (self.home, self.cfg, os.path.join(self.root, 'cache'), os.path.join(self.root, 'data'), os.path.join(self.root, 'state'),
                  os.path.join(self.root, 'giomods')):
            os.makedirs(d)
        # XDG_RUNTIME_DIR court : gtk4-broadwayd y nomme une socket (chemin ≤ 108 octets)
        self.runtime = tempfile.mkdtemp(prefix='gt4-', dir='/tmp')
        self.lang = lang
        # Affichage propre au scénario, dérivé de son nom (port 8080 + N libre)
        self.display = display or 100 + zlib.crc32(name.encode()) % 800
        while display is None and not _port_free(8080 + self.display):
            self.display += 1
        self.broadway = None
        self.proc = None
        self.bw = None
        self.link = None
        self.master, self.slave = os.openpty()
        tty.setraw(self.slave)
        self.port = os.ttyname(self.slave)
        os.set_blocking(self.master, False)
        self.config_path = os.path.join(self.cfg, '.gtktermrc')
        self.macros_path = os.path.join(self.cfg, 'gtkterm_macros.ini')
        self.stderr_path = os.path.join(self.root, 'gtkterm.stderr')
        self.fakegit_log = os.path.join(self.root, 'fakegit.log')
        self.fakegit_dir = os.path.join(self.root, 'fakegit')	# réponse du faux git : fichiers « sha » et « delay » (fake_git)
        os.makedirs(self.fakegit_dir)
        self.bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)

    # ---------- fichiers ----------
    def general(self, **kw):
        """Groupe [general] : jamais de vérification de mise à jour ni de dépôt réel."""
        g = dict(update_check_startup='false', update_url='https://invalid.example/none.git', update_prefix=os.path.join(self.root, 'prefix'))
        g.update(kw)
        return '[general]\n' + ''.join('%s=%s\n' % kv for kv in g.items()) + '\n'

    def profile(self, name='default', **kw):
        """Groupe [profile:<name>] (clés de config_store.c) ; port = la pty."""
        p = dict(port=self.port, speed=115200, bits=8, stopbits=1, parity='none', flow='none', wait_delay=0, wait_char=-1, echo='false',
                 crlfauto='false', autoreconnect_enabled='false', esc_clear_screen='false', timestamp='false')
        p.update(kw)
        return '[profile:%s]\n' % name + ''.join('%s=%s\n' % kv for kv in p.items()) + '\n'

    def write_config(self, text):
        with open(self.config_path, 'w') as f:
            f.write(text)

    def read_config(self):
        with open(self.config_path) as f:
            return f.read()

    def config_group(self, group):
        """Clés du groupe [group] de .gtktermrc (valeurs brutes, chaînes)."""
        out, cur = {}, None
        for line in self.read_config().splitlines():
            line = line.strip()
            if line.startswith('[') and line.endswith(']'):
                cur = line[1:-1]
            elif cur == group and '=' in line and not line.startswith('#'):
                k, v = line.split('=', 1)
                out[k.strip()] = v.strip()
        return out

    def fake_git(self, sha=None, delay=0):
        """Réponse du faux git pour les appels suivants : SHA de HEAD (None : échec, code 128), délai en secondes avant la réponse."""
        for nom, val in (('sha', sha), ('delay', str(delay) if delay else None)):
            p = os.path.join(self.fakegit_dir, nom)
            if val is None:
                if os.path.exists(p):
                    os.remove(p)
            else:
                with open(p, 'w') as f:
                    f.write(val)

    def fake_calls(self, prog='git'):
        """Appels journalisés par un faux programme de tests/gui/bin ('git' ou 'xterm') : liste des listes d'arguments, dans l'ordre."""
        try:
            with open(self.fakegit_log, encoding='utf-8') as f:
                lignes = f.read().splitlines()
        except FileNotFoundError:
            return []
        tag = 'FAKE' + prog.upper()
        return [l.split('\t')[1:] for l in lignes if l.split('\t')[0] == tag]

    # ---------- processus ----------
    def env(self):
        e = {k: v for k, v in os.environ.items() if k not in ('DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY', 'LD_PRELOAD', 'LANGUAGE', 'GTK_A11Y', 'NO_AT_BRIDGE',
                                                              'GTK_USE_PORTAL', 'GDK_DEBUG')}
        e.update(HOME=self.home, XDG_CONFIG_HOME=self.cfg, XDG_CACHE_HOME=os.path.join(self.root, 'cache'), XDG_DATA_HOME=os.path.join(self.root, 'data'),
                 XDG_STATE_HOME=os.path.join(self.root, 'state'), XDG_RUNTIME_DIR=self.runtime, XDG_DATA_DIRS='/usr/local/share:/usr/share',
                 GDK_BACKEND='broadway', BROADWAY_DISPLAY=':%d' % self.display, GSETTINGS_BACKEND='memory', GTK_A11Y='none',
                 GIO_USE_VFS='local', GIO_USE_VOLUME_MONITOR='unix', GDK_DEBUG='no-portals', GIO_MODULE_DIR=os.path.join(self.root, 'giomods'),
                 LANG=self.lang, LC_ALL=self.lang, PATH=FAKEBIN + ':' + os.environ['PATH'], LD_PRELOAD=GUARD, EM_FAKEGIT_LOG=self.fakegit_log,
                 EM_FAKEGIT_DIR=self.fakegit_dir,
                 GTKTERM_TEST_HOOKS='1',
                 ASAN_OPTIONS='verify_asan_link_order=0:detect_leaks=0:abort_on_error=0:halt_on_error=1:log_path=' + os.path.join(self.root, 'asan'))
        if self.lang == 'C':
            e['LANGUAGE'] = 'C'
        return e

    def start_broadway(self):
        e = self.env()
        e.pop('LD_PRELOAD')
        self.broadway = subprocess.Popen(['gtk4-broadwayd', '--address', '127.0.0.1', ':%d' % self.display], env=e, stdout=subprocess.DEVNULL,
                                         stderr=open(os.path.join(self.root, 'broadwayd.log'), 'w'), start_new_session=True)
        if not wait_until(lambda: not _port_free(8080 + self.display), 5, 0.05):
            raise RuntimeError('gtk4-broadwayd :%d ne répond pas' % self.display)

    def start(self, *args, port=True, connect_browser=True, extra_env=None):
        """gtkterm [--port <pty>] args… ; attend le nom D-Bus de l'application puis la fenêtre principale dans le dump."""
        if self.broadway is None:
            self.start_broadway()
        e = self.env()
        if extra_env:
            e.update(extra_env)
        cmd = [BIN] + (['--port', self.port] if port else []) + list(args)
        self.stderr_f = open(self.stderr_path, 'a')
        self.proc = subprocess.Popen(cmd, env=e, stdout=self.stderr_f, stderr=self.stderr_f, cwd=self.root, start_new_session=True)
        self.dest = APP_ID % self.proc.pid
        self.path = APP_PATH % self.proc.pid
        log('gtkterm pid', self.proc.pid, 'port', self.port, 'affichage', self.display)
        t0 = time.time()
        while not self._has_name():
            if self.proc.poll() is not None:
                raise RuntimeError('gtkterm a quitté (rc=%s) avant de prendre son nom D-Bus' % self.proc.returncode)
            if time.time() - t0 > 20:
                raise RuntimeError('nom D-Bus %s absent après 20 s' % self.dest)
            time.sleep(0.05)
        self.find1(name='main-window', timeout=10)
        self.bw = None
        if connect_browser:
            self._connect_browser()
        return self.proc

    def _connect_browser(self):
        """Faux navigateur (broadway.Broadway) ; attend la surface de la fenêtre principale."""
        from broadway import Broadway
        self.bw = Broadway(self.display, dump=self.dump)
        self.bw.surface_for(self.find1(name='main-window'), timeout=5)

    def _has_name(self):
        r = self.bus.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus', 'NameHasOwner',
                               GLib.Variant('(s)', (self.dest,)), None, Gio.DBusCallFlags.NONE, 2000, None)
        return r.unpack()[0]

    def stop(self, timeout=5):
        if self.bw is not None:
            self.bw.close()
            self.bw = None
        _kill_group(self.proc, timeout)
        return self.proc.returncode if self.proc else None

    def close(self):
        try:
            self.stop(timeout=2)
        except Exception:
            pass
        _kill_group(self.broadway, 3)
        for fd in (self.master, self.slave):
            try:
                os.close(fd)
            except OSError:
                pass
        self.master = self.slave = -1
        shutil.rmtree(self.runtime, ignore_errors=True)

    # ---------- actions D-Bus ----------
    def _call(self, method, params):
        r = self.bus.call_sync(self.dest, self.path, 'org.gtk.Actions', method, params, None, Gio.DBusCallFlags.NONE, 10000, None)
        return r.unpack()

    def action(self, name, param=None):
        """org.gtk.Actions.Activate de app.<name> ; param : GLib.Variant ou None."""
        self._call('Activate', GLib.Variant('(sava{sv})', (name, [param] if param is not None else [], {})))

    def set_state(self, name, value):
        self._call('SetState', GLib.Variant('(sva{sv})', (name, value, {})))

    def describe(self, name):
        """(activée, type du paramètre, état ou None)"""
        enabled, ptype, st = self._call('Describe', GLib.Variant('(s)', (name,)))[0]
        return enabled, ptype, (st[0] if st else None)

    def state(self, name):
        return self.describe(name)[2]

    def actions(self):
        return sorted(self._call('List', None)[0])

    # ---------- crochet de test ----------
    def dump(self):
        """Widgets des fenêtres visibles (test-dump) ; chaque entrée reçoit aussi native_w / native_h (taille de la surface qui la porte)."""
        self.action('test-dump')
        d = self.state('test-dump')
        natives = {w['native']: w for w in d if 'surface_w' in w}
        for w in d:
            n = natives.get(w['native'])
            if n is not None:
                w['native_w'], w['native_h'] = n['surface_w'], n['surface_h']
        return d

    def find(self, dump=None, name=None, label=None, type=None, native=None, mapped=True, css=None):
        d = self.dump() if dump is None else dump
        return [w for w in d if (name is None or w.get('name') == name) and (label is None or w.get('label') == label)
                and (type is None or w['type'] == type) and (native is None or w['native'] == native)
                and (mapped is None or w['mapped'] == mapped) and (css is None or css in w['css'])]

    def find1(self, timeout=5, **criteria):
        r = wait_until(lambda: self.find(**criteria), timeout)
        if not r:
            raise AssertionError('widget introuvable %r ; fenêtres : %r' % (criteria, [w.get('title') for w in self.windows()]))
        return r[0]

    def windows(self, dump=None):
        return [w for w in (self.dump() if dump is None else dump) if w['depth'] == 0]

    def title(self):
        """Titre de la fenêtre principale (« GTKTerm - <texte d'état> »)."""
        return self.find1(name='main-window')['title']

    def wait_window(self, title, timeout=5):
        """Fenêtre de premier niveau de titre exact title."""
        r = wait_until(lambda: [w for w in self.windows() if w.get('title') == title], timeout)
        if not r:
            raise AssertionError('fenêtre « %s » introuvable ; fenêtres : %r' % (title, [w.get('title') for w in self.windows()]))
        return r[0]

    def _alert_natives(self, d):
        # Boîtes de message et de confirmation de src/ui/dialogs.c : fenêtres modales de classes CSS « dialog » et « message »
        # (GtkMessageDialog : celles d'un GtkAlertDialog, s'il en revenait)
        return [w for w in d if w['depth'] == 0 and (w['type'] == 'GtkMessageDialog' or ('message' in w['css'] and 'dialog' in w['css']))]

    def alerts(self, dump=None):
        """Textes des boîtes de message ouvertes : titre et détail, séparés par « \\n »."""
        d = self.dump() if dump is None else dump
        out = []
        for a in self._alert_natives(d):
            labels = [w['label'] for w in d if w['native'] == a['native'] and w['type'] == 'GtkLabel' and w['mapped'] and w.get('label')]
            buttons = {w['label'] for w in d if w['native'] == a['native'] and w['type'] == 'GtkButton' and w.get('label')}
            out.append('\n'.join(l for l in labels if l not in buttons))
        return out

    def close_alert(self, button_label='OK'):
        d = self.dump()
        al = self._alert_natives(d)
        if not al:
            raise AssertionError('aucune boîte de message ouverte')
        b = [w for w in d if w['native'] == al[0]['native'] and w['type'] == 'GtkButton' and w.get('label') == button_label]
        if not b:
            raise AssertionError('bouton « %s » absent de la boîte : %r' % (button_label, self.alerts(d)))
        self.action('test-activate', GLib.Variant('s', b[0]['id']))
        n = len(al)
        wait_until(lambda: len(self._alert_natives(self.dump())) < n, 3)

    def terminal_text(self):
        self.action('test-terminal-text')
        return self.state('test-terminal-text')

    def terminal_props(self):
        """Police, défilement, curseur et couleurs du VTE (crochet test-terminal-props) : dict font, scrollback, cursor, foreground, background."""
        self.action('test-terminal-props')
        return self.state('test-terminal-props')

    def status_text(self):
        """Texte d'état de la liaison : libellé du widget status-text, même caché par un message temporaire."""
        w = self.find(name='status-text', mapped=None)
        if not w:
            raise AssertionError('widget status-text absent (barre d\'état) ; utiliser Env.title()')
        return w[0].get('label', '')

    def _present(self, key, d):
        return [w for w in d if w['mapped'] and key in (w.get('name'), w.get('label'), w['id'])]

    def activate(self, key):
        """Clic programmé d'un widget visible désigné par son nom, son libellé ou son id de dump. Synchrone : ses effets sont visibles au retour
        (un GtkButton reçoit « clicked » avant la réponse D-Bus). Un bouton insensible n'est jamais cliqué : le crochet le signale sur stderr et
        le scénario s'arrête aussitôt."""
        if not wait_until(lambda: self._present(key, self.dump()), 3):
            raise AssertionError('test-activate : widget « %s » introuvable' % key)
        avant = len(self.stderr())
        self.action('test-activate', GLib.Variant('s', key))
        refus = [l for l in self.stderr()[avant:].splitlines() if l.startswith('GTKTERM-TEST: test-activate')]
        if refus:
            raise AssertionError(refus[0])

    def after_frame(self, timeout=2):
        """Attend une image de chaque surface visible (compteur « frame » du dump, qui demande une image à chaque appel) : une vérification
        négative lit alors un état mis en page après l'action qui la précède (lignes d'une GtkColumnView liées de nouveau, par exemple)."""
        avant = {w['native']: w['frame'] for w in self.dump() if 'frame' in w}
        ok = wait_until(lambda: all(w['frame'] > avant.get(w['native'], -1) for w in self.dump() if 'frame' in w), timeout, 0.02)
        if not ok:
            log('after_frame : aucune image en %.1f s' % timeout)      # signalé dans le journal du scénario, comme le repli de settled
        return ok

    def set_text(self, key, text):
        if not wait_until(lambda: self._present(key, self.dump()), 3):
            raise AssertionError('test-set-text : widget « %s » introuvable' % key)
        self.action('test-set-text', GLib.Variant('(ss)', (key, text)))

    # ---------- pty ----------
    def read_pty(self, duration=0.5):
        buf = b''
        t0 = time.time()
        while time.time() - t0 < duration:
            r, _, _ = select.select([self.master], [], [], 0.05)
            if r:
                try:
                    buf += os.read(self.master, 65536)
                except OSError:
                    pass
        return buf

    def drain(self, settle=0.3):
        """Vide ce qui est déjà en attente sur la pty."""
        return self.read_pty(settle)

    def write_pty(self, data):
        os.write(self.master, data)

    # ---------- port derrière un lien symbolique (perte et retour du périphérique) ----------
    def use_link(self):
        """Le port devient un lien symbolique stable vers la pty ; à passer à start(…, port=False) ou dans le profil."""
        self.link = os.path.join(self.root, 'port')
        os.symlink(self.port, self.link)
        return self.link

    def unplug(self):
        """Débranche : pty fermée ET lien retiré (comme le nœud d'un adaptateur USB). Un lien laissé pendant vers /dev/pts/N désignerait la
        prochaine pty créée sur la machine (numérotée au plus petit libre), et GTKTerm s'y reconnecterait (même correctif que l'ancienne suite GTK 3)."""
        for fd in (self.master, self.slave):
            try:
                os.close(fd)
            except OSError:
                pass
        self.master = self.slave = -1
        if self.link and os.path.lexists(self.link):
            os.unlink(self.link)

    def replug(self):
        self.master, self.slave = os.openpty()
        tty.setraw(self.slave)
        self.port = os.ttyname(self.slave)
        os.set_blocking(self.master, False)
        if os.path.lexists(self.link):
            os.unlink(self.link)
        os.symlink(self.port, self.link)

    # ---------- fin ----------
    def asan_reports(self):
        return [os.path.join(self.root, f) for f in os.listdir(self.root) if f.startswith('asan')]

    def stderr(self):
        try:
            return open(self.stderr_path, errors='replace').read()
        except FileNotFoundError:
            return ''

    def finish(self, quit=True):
        """app.quit (si quit et si l'application tourne), code 0 attendu, stderr propre, aucun rapport ASan ; écrit result.txt."""
        if quit and self.proc is not None and self.proc.poll() is None:
            try:
                self.action('quit')
                self.proc.wait(10)
            except Exception as ex:
                rep('app.quit impossible :', ex)
        if self.proc is not None:
            check(self.proc.poll() is not None, 'application terminee')
            rc = self.stop()
            check(rc == 0, 'code de sortie nul', rc)
        check(not self.asan_reports(), 'aucun rapport ASan', self.asan_reports())
        bad = [l for l in self.stderr().splitlines() if any(k in l for k in BAD_STDERR) and not any(n in l for n in KNOWN_NOISE)]
        check(not bad, 'stderr sans erreur', bad[:5])
        self.close()
        with open(os.path.join(self.root, 'result.txt'), 'w') as f:
            f.write('\n'.join(R) + '\n')


def macros_v2_text(*libelles):
    """Fichier de macros v2 (macro_file.c) : une macro par libellé, action = libellé, sans raccourci ni onglet."""
    out = ['[gtkterm-macros]', 'version=2', '']
    for i, libelle in enumerate(libelles):
        out += ['[macro:%d]' % (i + 1), 'label=%s' % libelle, 'shortcut=', 'action=%s' % libelle, 'tab=', 'polling=false', 'polling_period_ms=1000', '']
    return '\n'.join(out) + '\n'


def file_dialog(env, title, path, timeout=8):
    """Choisit path (chemin absolu) dans le sélecteur de fichiers GTK 4 interne intitulé title, au clavier comme un utilisateur.
    Comportement mesuré (GTK 4.14.5, GtkFileDialog sans portail = GtkFileChooserDialog, sous gtk4-broadwayd) :
    - ouverture : focus sur la liste des fichiers (GtkColumnListView), champ d'emplacement caché ; Ctrl+L l'affiche (GtkFileChooserEntry
      vide) et y met le focus ; un chemin absolu frappé y est accepté tel quel, sans complétion en ligne, et Entrée choisit le fichier ;
    - enregistrement : le focus est déjà dans le champ « Name » (GtkFileChooserEntry prérempli « gtkterm_macros.ini ») et Ctrl+L ne change
      rien ; Ctrl+A puis un chemin absolu remplace le nom, Entrée enregistre à ce chemin (dossier compris).
    Étapes :
    1. la fenêtre apparaît dans le dump (un portail serait un autre processus, absent du dump : wait_window échoue) ;
    2. focus clavier par un clic dans sa barre de titre (bw.focus_window : broadway pose toutes les surfaces en (0, 0), la surface est visée par son id) ;
    3. Ctrl+L, puis attente d'un GtkFileChooserEntry focalisé dans le sélecteur ; Ctrl+A (remplace le nom proposé), frappe rapide ;
    4. le GtkFileChooserEntry du sélecteur doit contenir exactement path ; une complétion en ligne (suffixe après le texte frappé) est retirée
       par Suppr ; une touche perdue : Ctrl+A et nouvelle frappe, une seule fois, à la cadence normale ;
    5. Entrée ; le sélecteur doit disparaître (sinon : boîte « remplacer ? », chemin refusé…). En cas d'échec, l'AssertionError donne le
       titre, le chemin et le texte du champ."""
    env.wait_window(title, timeout)
    env.bw.focus_window(title)
    time.sleep(0.3)
    env.bw.key('l', mods=('ctrl',))

    def champs():
        d = env.dump()
        win = [w for w in d if w['depth'] == 0 and w.get('title') == title]
        if not win:
            return []
        return [w for w in d if w['native'] == win[0]['native'] and w['type'] == 'GtkFileChooserEntry' and w['mapped']]

    def focalise():
        d = env.dump()
        win = [w for w in d if w['depth'] == 0 and w.get('title') == title]
        return bool(win) and any(w['native'] == win[0]['native'] and w['type'] == 'GtkText' and w['focus'] and w['mapped'] for w in d)

    if not (wait_until(lambda: champs() and focalise(), 3)):
        raise AssertionError('file_dialog : aucun champ de saisie focalisé dans « %s » après Ctrl+L' % title)

    def saisi():
        textes = [w.get('label', '') for w in champs()]
        if path in textes:
            return 'exact'
        if any(t.startswith(path) for t in textes):
            return 'suffixe'
        return None

    for settle in (0.02, 0.15):                 # frappe rapide, puis une reprise à la cadence normale (touche perdue sous charge)
        env.bw.key('a', mods=('ctrl',))
        env.bw.type(path, settle=settle)
        etat = wait_until(saisi, 2)
        if etat == 'suffixe':
            env.bw.key('Delete')                   # Suppr : retire la complétion sélectionnée
            etat = 'exact' if wait_until(lambda: saisi() == 'exact', 2) else None
        if etat == 'exact':
            break
    else:
        raise AssertionError('file_dialog : chemin non saisi dans « %s » : %r (champ : %r)' % (title, path, [w.get('label') for w in champs()]))
    env.bw.key('Return')
    if not wait_until(lambda: not [w for w in env.windows() if w.get('title') == title], timeout):
        raise AssertionError('file_dialog : « %s » toujours ouvert après Entrée' % title)


# ---------- Macros (panneau et éditeur) ----------
def _gk(v):
    """Valeur échappée comme GKeyFile : \\ \\n \\t \\r, espace de tête \\s."""
    v = v.replace('\\', '\\\\').replace('\n', '\\n').replace('\t', '\\t').replace('\r', '\\r')
    return '\\s' + v[1:] if v.startswith(' ') else v


def _gk_list(items):
    return ''.join(_gk(x).replace(';', '\\;') + ';' for x in items)


def write_macros_v2(path, macros, lists=None):
    """Fichier de macros v2 (macro_file.c). macros : dicts (label, shortcut, action, tab, args [liste], polling, period) ;
    lists : [(nom, [(affichage, valeur), ...])]."""
    out = ['[gtkterm-macros]', 'version=2', '']
    for i, m in enumerate(macros):
        out.append('[macro:%d]' % (i + 1))
        for k in ('label', 'shortcut', 'action', 'tab'):
            out.append('%s=%s' % (k, _gk(m.get(k, ''))))
        if m.get('args') is not None:
            out.append('args=%s' % _gk_list(m['args']))
        out.append('polling=%s' % ('true' if m.get('polling') else 'false'))
        out.append('polling_period_ms=%d' % m.get('period', 1000))
        out.append('')
    for i, (nom, entrees) in enumerate(lists or []):
        out += ['[list:%d]' % (i + 1), 'name=%s' % _gk(nom), 'displays=%s' % _gk_list(a for a, _ in entrees),
                'values=%s' % _gk_list(v for _, v in entrees), '']
    with open(path, 'w') as f:
        f.write('\n'.join(out) + '\n')


def write_macros_v1(path, macros, lists=None):
    """Ancien format [macros]/[lists]. macros : tuples (label, shortcut, action, tab, args, polling, period) ; lists : chaînes « Nom:Aff=val|... »."""
    out = ['[macros]']
    for i, m in enumerate(macros):
        label, sc, action, tab, args, pol, per = list(m) + [''] * (7 - len(m))
        out.append('macro_%d=%s::%s::%s::%s::%s::%s::%s' % (i, label, sc or 'None', action.replace('\\', '\\\\'), tab, args, pol or 0, per or 1000))
    if lists:
        out.append('[lists]')
        out += ['list_%d=%s' % (i, l) for i, l in enumerate(lists)]
    with open(path, 'w') as f:
        f.write('\n'.join(out) + '\n')


def macro_config(env, macros_file, extra=''):
    """.gtktermrc des scénarios de macros : [general] sans vérification de mise à jour, profil default sur la pty avec ce fichier de macros ;
    extra : groupes supplémentaires (texte complet, avec leur en-tête)."""
    env.write_config(env.general() + env.profile(macros_file=macros_file) + extra)


def classes(w):
    """Classes CSS d'un widget du dump (clé « css » du crochet de test)."""
    return list((w or {}).get('css') or [])


def macro_buttons(env, dump=None):
    """{libellé: identifiant} des boutons principaux visibles du panneau de macros (nom « macro-<id> »)."""
    out = {}
    for w in (dump if dump is not None else env.dump()):
        m = re.match(r'^macro-(\d+)$', w.get('name') or '')
        if m and w.get('mapped'):
            out[w.get('label')] = int(m.group(1))
    return out


def macro_id(env, label, timeout=5):
    """Identifiant en mémoire de la macro dont le bouton principal porte ce libellé (« ⏱ X » en mode polling) ; attend le panneau."""
    vu = {}
    if not wait_until(lambda: vu.update(macro_buttons(env)) or label in vu, timeout):
        raise AssertionError('bouton de macro %r introuvable ; vus : %r' % (label, sorted(vu)))
    return vu[label]


# ---------- Fenêtre « Configure Macros » ----------
def editor_rows(env, prefix='macro-cell', cols=('label', 'shortcut', 'action', 'tab')):
    """Lignes visibles d'une GtkColumnView de l'éditeur, de haut en bas : dicts {colonne: texte, 'selected': bool, 'cells': {colonne: widget}}.
    Cellules nommées « <prefix>-<colonne> », appariées par rang d'ordonnée ; la ligne (GtkColumnViewRowWidget / rôle row) qui contient le
    centre de la première cellule donne l'état sélectionné."""
    d = env.dump()
    cells = {c: sorted((w for w in d if w.get('name') == '%s-%s' % (prefix, c) and w.get('mapped')), key=lambda w: w['y']) for c in cols}
    rows = [w for w in d if w.get('mapped') and (w.get('type') == 'GtkColumnViewRowWidget' or w.get('role') == 'row')]
    out = []
    for i in range(min(len(cells[c]) for c in cols)):
        ref = cells[cols[0]][i]
        cy = ref['y'] + ref['h'] / 2
        row = [r for r in rows if r.get('native') == ref.get('native') and r['y'] <= cy < r['y'] + r['h']]
        line = {c: cells[c][i].get('label') or '' for c in cols}
        line['selected'] = bool(row and row[0].get('selected'))
        line['cells'] = {c: cells[c][i] for c in cols}
        out.append(line)
    return out


def editor_select(env, index, prefix='macro-cell', col='shortcut'):
    """Sélectionne la seule ligne « index » par un vrai clic sur sa cellule non éditable « col », puis attend l'état sélectionné."""
    rows = editor_rows(env, prefix)
    env.bw.click(rows[index]['cells'][col])
    attendu = [k == index for k in range(len(rows))]
    ok = wait_until(lambda: [r['selected'] for r in editor_rows(env, prefix)] == attendu, 3)
    check(ok, 'ligne %d seule selectionnee : %r' % (index, [r['selected'] for r in editor_rows(env, prefix)]))


def open_editor(env):
    """Ouvre « Configure Macros » par app.macros et attend sa liste."""
    env.action('macros')
    env.wait_window('Configure Macros')
    env.find1(name='macro-list')


def close_by_titlebar(env, window_name):
    """Ferme une fenêtre par le bouton de fermeture de sa barre de titre (GtkWindowControls, classe CSS « close »), par un vrai clic."""
    win = env.find1(name=window_name)
    boutons = [w for w in env.dump() if w.get('native') == win.get('native') and w.get('type') == 'GtkButton' and 'close' in classes(w)
               and w.get('mapped')]
    check(len(boutons) == 1, 'bouton de fermeture de %s trouve (%d)' % (window_name, len(boutons)))
    env.bw.click(boutons[0])
