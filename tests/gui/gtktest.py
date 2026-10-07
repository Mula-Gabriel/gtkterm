# Bibliothèque de la suite graphique : environnement isolé, pty, broadway, pilotage AT-SPI, assertions.
import os, sys, time, shutil, signal, subprocess, select, tty, termios, re

SCR = os.path.dirname(os.path.abspath(__file__))
BIN = os.environ['GTKTERM_BIN']
GUARD = os.environ['GTKTERM_GUARD']
FAKEBIN = os.path.join(SCR, 'bin')
RUNS = os.environ['GTKTERM_GUI_RUNS']

import gi
gi.require_version('Atspi', '2.0')
from gi.repository import Atspi


def log(*a):
    print('[em]', *a, flush=True)


import atexit
_ENVS = []


@atexit.register
def _cleanup_all():
    for e in _ENVS:
        try:
            e.close()
        except Exception:
            pass


class Env:
    def __init__(self, name, lang='C', display=None):
        display = display or int(os.environ.get('EM_DISPLAY', '37'))
        _ENVS.append(self)
        self.name = name
        self.root = os.path.join(RUNS, name)
        if os.path.exists(self.root):
            shutil.rmtree(self.root)
        self.home = os.path.join(self.root, 'home')
        self.cfg = os.path.join(self.root, 'config')
        self.cache = os.path.join(self.root, 'cache')
        self.data = os.path.join(self.root, 'data')
        self.runtime = os.path.join(self.root, 'run')
        for d in (self.home, self.cfg, self.cache, self.data, self.runtime):
            os.makedirs(d)
        os.chmod(self.runtime, 0o700)
        self.giomods = os.path.join(self.root, 'giomods')
        os.makedirs(self.giomods)
        self.lang = lang
        self.display = display
        self.broadway = None
        self.proc = None
        self.link = None
        self.master, self.slave = os.openpty()
        tty.setraw(self.slave)
        self.pty = os.ttyname(self.slave)
        os.set_blocking(self.master, False)
        self.rc = os.path.join(self.cfg, '.gtktermrc')
        self.stderr_path = os.path.join(self.root, 'gtkterm.stderr')
        self.fakegit_log = os.path.join(self.root, 'fakegit.log')

    # ---------- fichiers ----------
    def profile(self, name='default', **kw):
        p = dict(port=self.pty, speed=115200, bits=8, stopbits=1, parity='none', flow='none', wait_delay=0, wait_char=-1,
                 echo='False', crlfauto='False', autoreconnect_enabled='False', esc_clear_screen='False', timestamp='False',
                 update_check_startup='False', update_url='https://invalid.example/none.git', update_prefix=self.root + '/prefix')
        p.update(kw)
        s = '[%s]\n' % name + ''.join('%s\t= %s\n' % (k, v) for k, v in p.items()) + '\n'
        return s

    def write_rc(self, text):
        with open(self.rc, 'w') as f:
            f.write(text)

    def write_macros(self, path, macros, lists=None):
        """macros : liste de tuples (label, shortcut, action, tab, args, polling, period)."""
        out = ['[macros]']
        for i, m in enumerate(macros):
            m = list(m) + [''] * (7 - len(m))
            label, sc, action, tab, args, pol, per = m
            out.append('macro_%d=%s::%s::%s::%s::%s::%s::%s' % (i, label, sc or 'None', action.replace('\\', '\\\\'), tab, args, pol or 0, per or 1000))
        if lists:
            out.append('[lists]')
            for i, l in enumerate(lists):
                out.append('list_%d=%s' % (i, l))
        with open(path, 'w') as f:
            f.write('\n'.join(out) + '\n')

    # ---------- processus ----------
    def env(self):
        e = {k: v for k, v in os.environ.items() if k not in ('DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY', 'LD_PRELOAD', 'LANGUAGE')}
        e.update(HOME=self.home, XDG_CONFIG_HOME=self.cfg, XDG_CACHE_HOME=self.cache, XDG_DATA_HOME=self.data,
                 XDG_STATE_HOME=os.path.join(self.root, 'state'), XDG_RUNTIME_DIR=self.runtime,
                 GDK_BACKEND='broadway', BROADWAY_DISPLAY=':%d' % self.display, GSETTINGS_BACKEND='memory',
                 LANG=self.lang, LC_ALL=self.lang, PATH=FAKEBIN + ':' + os.environ['PATH'], LD_PRELOAD=GUARD,
                 EM_FAKEGIT_LOG=self.fakegit_log, NO_AT_BRIDGE='0', GTK_A11Y='atspi', GIO_USE_VFS='local', GIO_USE_VOLUME_MONITOR='unix', GTK_USE_PORTAL='0', GIO_MODULE_DIR=self.giomods, XDG_DATA_DIRS='/usr/local/share:/usr/share',
                 ASAN_OPTIONS='verify_asan_link_order=0:detect_leaks=0:abort_on_error=0:halt_on_error=1:log_path=' + os.path.join(self.root, 'asan'))
        if self.lang == 'C':
            e['LANGUAGE'] = 'C'
        return e

    def start_broadway(self):
        e = self.env()
        e.pop('LD_PRELOAD')
        self.broadway = subprocess.Popen(['broadwayd', '--address', '127.0.0.1', ':%d' % self.display], env=e, stdout=subprocess.DEVNULL, stderr=open(os.path.join(self.root, 'broadwayd.log'), 'w'))
        time.sleep(0.7)

    def start(self, args=None, extra_env=None, wait_app=True, wrapper=None, force_port=True):
        if self.broadway is None:
            self.start_broadway()
        e = self.env()
        if extra_env:
            e.update(extra_env)
        binary = BIN
        cmd = (wrapper or []) + [binary] + (['--port', self.pty] if force_port else []) + (args or [])
        self.stderr_f = open(self.stderr_path, 'a')
        self.proc = subprocess.Popen(cmd, env=e, stdout=self.stderr_f, stderr=self.stderr_f, cwd=self.root)
        log('gtkterm pid', self.proc.pid, 'port', self.pty)
        self.app = None
        if wait_app:
            self.app = self.find_app()
        return self.proc

    def find_app(self, timeout=20):
        t0 = time.time()
        while time.time() - t0 < timeout:
            if self.proc.poll() is not None:
                raise RuntimeError('gtkterm a quitté rc=%s' % self.proc.returncode)
            desk = Atspi.get_desktop(0)
            for i in range(desk.get_child_count()):
                a = desk.get_child_at_index(i)
                try:
                    if a and a.get_process_id() in self.pids() and a.get_child_count() > 0:
                        return a
                except Exception:
                    pass
            time.sleep(0.3)
        raise RuntimeError('application gtkterm introuvable dans AT-SPI')

    def pids(self):
        out = {self.proc.pid}
        try:
            for l in subprocess.run(['pgrep', '-P', str(self.proc.pid)], capture_output=True, text=True).stdout.split():
                out.add(int(l))
        except Exception:
            pass
        return out

    def stop(self, timeout=5):
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        rc = self.proc.returncode if self.proc else None
        return rc

    def close(self):
        try:
            self.stop(timeout=2)
        except Exception:
            pass
        if self.broadway:
            self.broadway.terminate()
            try:
                self.broadway.wait(3)
            except subprocess.TimeoutExpired:
                self.broadway.kill()
        for fd in (self.master, self.slave):
            try:
                os.close(fd)
            except OSError:
                pass

    # ---------- port derrière un lien symbolique (perte et retour du périphérique) ----------
    def use_link(self):
        """Le port est un lien symbolique stable vers la pty : on peut la « débrancher » et la « rebrancher »."""
        self.link = os.path.join(self.root, 'port')
        os.symlink(self.pty, self.link)
        return self.link

    def unplug(self):
        for fd in (self.master, self.slave):
            try:
                os.close(fd)
            except OSError:
                pass
        self.master = self.slave = -1

    def replug(self):
        self.master, self.slave = os.openpty()
        tty.setraw(self.slave)
        self.pty = os.ttyname(self.slave)
        os.set_blocking(self.master, False)
        os.unlink(self.link)
        os.symlink(self.pty, self.link)

    # ---------- pty ----------
    def drain(self, settle=0.3):
        """Vide ce qui est déjà en attente sur le pty (octets envoyés avant cet instant)."""
        return self.read_pty(settle)

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

    def write_pty(self, data):
        os.write(self.master, data)

    def asan_reports(self):
        return [os.path.join(self.root, f) for f in os.listdir(self.root) if f.startswith('asan')]

    def stderr(self):
        try:
            return open(self.stderr_path, errors='replace').read()
        except FileNotFoundError:
            return ''


# ---------- AT-SPI ----------
def role(a):
    try:
        return a.get_role_name()
    except Exception:
        return '?'


def name(a):
    try:
        return a.get_name() or ''
    except Exception:
        return ''


def walk(a, depth=0, maxdepth=40):
    yield a, depth
    if depth >= maxdepth:
        return
    try:
        n = a.get_child_count()
    except Exception:
        return
    for i in range(n):
        try:
            c = a.get_child_at_index(i)
        except Exception:
            continue
        if c is not None:
            yield from walk(c, depth + 1, maxdepth)


def dump(a, maxdepth=40, out=sys.stdout, showing_only=False):
    for n, d in walk(a, maxdepth=maxdepth):
        st = ''
        try:
            ss = n.get_state_set()
            st = ' '.join(s for s, k in (('showing', Atspi.StateType.SHOWING), ('modal', Atspi.StateType.MODAL), ('checked', Atspi.StateType.CHECKED),
                                          ('sensitive', Atspi.StateType.SENSITIVE), ('active', Atspi.StateType.ACTIVE)) if ss.contains(k))
            if showing_only and not ss.contains(Atspi.StateType.SHOWING) and d > 1:
                continue
        except Exception:
            pass
        txt = ''
        try:
            if n.get_text_iface() is not None:
                txt = ' text=%r' % Atspi.Text.get_text(n, 0, min(Atspi.Text.get_character_count(n), 200))
        except Exception:
            pass
        print('  ' * d + '[%s] %r {%s}%s' % (role(n), name(n), st, txt), file=out)


def find(a, role_name=None, name_re=None, pred=None, showing=None, maxdepth=40):
    res = []
    for n, d in walk(a, maxdepth=maxdepth):
        if role_name and role(n) != role_name:
            continue
        if name_re is not None and not re.search(name_re, name(n)):
            continue
        if showing is not None:
            try:
                if n.get_state_set().contains(Atspi.StateType.SHOWING) != showing:
                    continue
            except Exception:
                continue
        if pred and not pred(n):
            continue
        res.append(n)
    return res


def find1(a, role_name=None, name_re=None, timeout=5, **kw):
    t0 = time.time()
    while True:
        r = find(a, role_name, name_re, **kw)
        if r:
            return r[0]
        if time.time() - t0 > timeout:
            raise LookupError('introuvable: role=%s name=%s' % (role_name, name_re))
        time.sleep(0.2)


def act(a, action_name=None):
    ai = a.get_action_iface()
    if ai is None:
        raise RuntimeError('pas d\'action sur %s %r' % (role(a), name(a)))
    n = ai.get_n_actions()
    names = [ai.get_action_name(i) for i in range(n)]
    idx = 0
    if action_name:
        idx = names.index(action_name)
    return ai.do_action(idx)


def menu(app, *path, timeout=5):
    """Active un chemin de menus : menu(app, 'Configuration', 'Port')."""
    cur = app
    for i, p in enumerate(path):
        rn = 'menu' if i < len(path) - 1 else None
        item = None
        t0 = time.time()
        while item is None:
            for cand in find(cur, None, '^_?%s$' % re.escape(p)):
                if role(cand) in ('menu', 'menu item', 'check menu item', 'radio menu item'):
                    item = cand
                    break
            if item is None:
                if time.time() - t0 > timeout:
                    raise LookupError('menu introuvable: %s' % p)
                time.sleep(0.2)
        act(item)
        time.sleep(0.4)
        cur = item
    return cur


def windows(app):
    out = []
    for i in range(app.get_child_count()):
        c = app.get_child_at_index(i)
        if c is not None:
            out.append(c)
    return out


def set_text(entry, text):
    return Atspi.EditableText.set_text_contents(entry, text)


def get_text(obj):
    try:
        t = obj.get_text_iface()
    except Exception:
        return None
    if t is None:
        return None
    return Atspi.Text.get_text(obj, 0, Atspi.Text.get_character_count(obj))


def menu_click(bw, app, *path, timeout=5):
    """Comme menu(), mais par de vrais clics broadway (nécessaire quand l'élément ouvre un gtk_dialog_run)."""
    cur = app
    for i, p in enumerate(path):
        item = None
        t0 = time.time()
        while item is None:
            for cand in find(cur, None, '^_?%s$' % re.escape(p), showing=True):
                if role(cand) in ('menu', 'menu item', 'check menu item', 'radio menu item'):
                    item = cand
                    break
            if item is None:
                if time.time() - t0 > timeout:
                    raise LookupError('menu introuvable: %s' % p)
                time.sleep(0.2)
        bw.click_obj(item)
        time.sleep(0.5)
        cur = item
    return cur


def menu_key(bw, app, top, item, focus_obj=None, tries=4):
    """Ouvre un menu de la barre par le clavier (F10, flèches) et active l'élément : passe par de vraies touches broadway.
    Vérifie par AT-SPI que l'élément visé est bien sélectionné avant Entrée ; sinon Échap et nouvel essai."""
    bar = find1(app, 'menu bar')
    tops = [c for c in (bar.get_child_at_index(i) for i in range(bar.get_child_count())) if c is not None]
    names = [name(c) for c in tops]
    ti = names.index(top)
    m = tops[ti]
    for attempt in range(tries):
        items = [m.get_child_at_index(i) for i in range(m.get_child_count())]
        nav = [c for c in items if role(c) != 'separator' and c.get_state_set().contains(Atspi.StateType.SENSITIVE)]
        ii = [name(c) for c in nav].index(item)
        target = nav[ii]
        if focus_obj is not None:
            bw.click_obj(focus_obj)
        bw.warm()
        bw.key('F10'); time.sleep(0.5)
        for _ in range(ti):
            bw.key(0xff53); time.sleep(0.2)   # Right
        for _ in range(ii + 1):
            bw.key(0xff54); time.sleep(0.12)  # Down
        time.sleep(0.3)
        ss = target.get_state_set()
        if ss.contains(Atspi.StateType.SELECTED) or ss.contains(Atspi.StateType.FOCUSED) or ss.contains(Atspi.StateType.ARMED):
            bw.key('Return'); time.sleep(0.6)
            return True
        log('menu_key: %s/%s non sélectionné (essai %d), états=%s' % (top, item, attempt + 1, [ (n.value_nick) for n in ss.get_states()]))
        for _ in range(3):
            bw.key('Escape'); time.sleep(0.15)
        time.sleep(0.5)
    raise RuntimeError('menu_key: impossible d\'activer %s/%s' % (top, item))


def wait_window(app, name_re, timeout=8):
    t0 = time.time()
    while time.time() - t0 < timeout:
        for w in windows(app):
            if re.search(name_re, name(w)):
                return w
        time.sleep(0.2)
    raise LookupError('fenêtre introuvable: %s' % name_re)


def focus_window(bw, app, name_re, dy=15, timeout=8):
    """Attend la fenêtre (AT-SPI) puis clique dans sa barre de titre via broadway pour lui donner le focus clavier."""
    w = wait_window(app, name_re, timeout)
    t0 = time.time()
    while True:
        i, sf = bw.toplevel_surface(w)
        if sf is not None or time.time() - t0 > timeout:
            break
        time.sleep(0.2)
    if sf is None:
        raise RuntimeError('surface broadway introuvable pour %s' % name_re)
    bw.click(sf['x'] + sf['w'] // 2, sf['y'] + dy)
    time.sleep(0.3)
    bw.warm()
    return w


from broadway import Broadway, GDK_CONTROL, GDK_SHIFT, GDK_MOD1

R = []
FAILS = []
# Bruit connu de GTK sous broadway, sans rapport avec GTKTerm
KNOWN_NOISE = ("gtk_widget_get_preferred_height_for_width: assertion 'width >= 0' failed",
               "gtk_widget_queue_draw_area: assertion 'height >= 0' failed")


def rep(*a):
    s = ' '.join(str(x) for x in a)
    R.append(s)
    log(s)


def check(cond, label, detail=''):
    rep(('OK    ' if cond else 'ECHEC ') + label + ((' : %r' % (detail,)) if (detail != '' and not cond) else ''))
    if not cond:
        FAILS.append(label)
    return cond


def wait_until(pred, timeout=5.0, step=0.1):
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


def terminal_text(app):
    return get_text(find1(app, 'terminal')) or ''


def status_text(app):
    return name(find1(app, 'status bar'))


def alerts(app):
    return [w for w in windows(app) if role(w) == 'alert']


def macro_button(app, label):
    return find1(app, 'push button', '^%s$' % re.escape(label), showing=True)


def finish(e, quit=True):
    if quit and e.proc.poll() is None:
        try:
            menu(e.app, 'File', 'Quit')
            e.proc.wait(10)
        except Exception as ex:
            rep('quit via menu impossible:', ex)
    rep('sortie : rc=%s' % e.stop())
    reps = e.asan_reports()
    check(not reps, 'aucun rapport ASan', reps)
    st = e.stderr()
    bad = [l for l in st.splitlines()
           if any(k in l for k in ('runtime error', 'Sanitizer', 'CRITICAL', 'EMGUARD', 'Segmentation')) and not any(n in l for n in KNOWN_NOISE)]
    check(not bad, 'stderr sans erreur', bad[:5])
    e.close()
    with open(os.path.join(e.root, 'result.txt'), 'w') as f:
        f.write('\n'.join(R))


def conclude():
    rep('BILAN : %d echec(s)%s' % (len(FAILS), (' : ' + ', '.join(FAILS)) if FAILS else ''))
    return 1 if FAILS else 0


def file_dialog(bw, app, title, path):
    w = focus_window(bw, app, title)
    bw.key('l', GDK_CONTROL); time.sleep(0.4)
    bw.type(path); time.sleep(0.4)
    txt = fix_path_entry(w, path)
    bw.key('Return'); time.sleep(1.5)
    return txt


def fix_path_entry(w, path, typed_prefix_ok=True):
    """Après la frappe, vérifie le champ de saisie du sélecteur ; le corrige par AT-SPI si une touche a été perdue."""
    ents = find(w, 'text', showing=True)
    if not ents:
        time.sleep(0.8)
        ents = find(w, 'text', showing=True) or find(w, 'text')
    vals = [get_text(t) for t in ents]
    if path in vals or os.path.basename(path) in vals:
        return vals, False
    for t, v in zip(ents, vals):
        if v:
            set_text(t, path); time.sleep(0.3)
            return [get_text(x) for x in ents], True
    return vals, None
