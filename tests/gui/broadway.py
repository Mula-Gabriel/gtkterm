# Client minimal du protocole broadway (GTK3) : se fait passer pour le navigateur pour injecter souris et clavier.
import socket, os, struct, threading, time, base64

GDK_SHIFT, GDK_CONTROL, GDK_MOD1 = 1, 4, 8
BUTTON_MASK = {1: 1 << 8, 2: 1 << 9, 3: 1 << 10}
KEYSYM = {'Escape': 0xff1b, 'Return': 0xff0d, 'Tab': 0xff09, 'BackSpace': 0xff08, 'Control_L': 0xffe3, 'Shift_L': 0xffe1, 'Alt_L': 0xffe9}
for i in range(1, 13):
    KEYSYM['F%d' % i] = 0xffbe + i - 1


class Broadway:
    def __init__(self, display, width=1600, height=1000):
        self.port = 8080 + display
        self.sock = socket.create_connection(('127.0.0.1', self.port))
        key = base64.b64encode(os.urandom(16)).decode()
        req = ('GET /socket HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: %s\r\n'
               'Sec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: broadway\r\nOrigin: http://127.0.0.1:%d\r\n\r\n') % (self.port, key, self.port)
        self.sock.sendall(req.encode())
        resp = b''
        while b'\r\n\r\n' not in resp:
            resp += self.sock.recv(1)
        if b' 101 ' not in resp.split(b'\r\n')[0]:
            raise RuntimeError('handshake broadway refusé: %r' % resp)
        self.surfaces = {}   # id -> dict(x,y,w,h,visible,temp,parent)
        self.stack = []      # ordre d'empilement (dernier = dessus)
        self.serial = 0
        self.ts = 1000
        self.state = 0
        self.lock = threading.Lock()
        self.alive = True
        self.t = threading.Thread(target=self._reader, daemon=True)
        self.t.start()
        self.send('d', [width, height])
        time.sleep(0.5)

    # ---------- trames ----------
    def _recv_exact(self, n):
        b = b''
        while len(b) < n:
            c = self.sock.recv(n - len(b))
            if not c:
                raise EOFError
            b += c
        return b

    def _reader(self):
        buf = b''
        try:
            while self.alive:
                h = self._recv_exact(2)
                fin, op, ln = h[0] & 0x80, h[0] & 0x0f, h[1] & 0x7f
                if ln == 126:
                    ln = struct.unpack('>H', self._recv_exact(2))[0]
                elif ln == 127:
                    ln = struct.unpack('>Q', self._recv_exact(8))[0]
                data = self._recv_exact(ln)
                if op == 8:
                    break
                if op == 9:
                    continue
                buf += data
                if fin:
                    self._handle(buf)
                    buf = b''
        except Exception:
            pass

    def _handle(self, m):
        p = 0
        g16 = lambda: struct.unpack_from('<H', m, p)[0]
        g16s = lambda: struct.unpack_from('<h', m, p)[0]
        with self.lock:
            while p < len(m):
                c = chr(m[p]); p += 1
                self.serial = struct.unpack_from('<I', m, p)[0]; p += 4
                if c == 's':
                    i = g16(); x = g16s.__call__() if False else struct.unpack_from('<h', m, p + 2)[0]
                    y = struct.unpack_from('<h', m, p + 4)[0]; w = struct.unpack_from('<H', m, p + 6)[0]; hh = struct.unpack_from('<H', m, p + 8)[0]
                    temp = m[p + 10] != 0; p += 11
                    self.surfaces[i] = dict(x=x, y=y, w=w, h=hh, visible=False, temp=temp, parent=0)
                    self.stack.append(i)
                elif c in 'SHdrR':
                    i = g16(); p += 2
                    if c == 'S' and i in self.surfaces:
                        self.surfaces[i]['visible'] = True
                        if i in self.stack: self.stack.remove(i)
                        self.stack.append(i)
                    elif c == 'H' and i in self.surfaces:
                        self.surfaces[i]['visible'] = False
                    elif c == 'd':
                        self.surfaces.pop(i, None)
                        if i in self.stack: self.stack.remove(i)
                    elif c == 'r' and i in self.stack:
                        self.stack.remove(i); self.stack.append(i)
                elif c == 'p':
                    i = g16(); par = struct.unpack_from('<H', m, p + 2)[0]; p += 4
                    if i in self.surfaces: self.surfaces[i]['parent'] = par
                elif c == 'm':
                    i = g16(); ops = m[p + 2]; p += 3
                    s = self.surfaces.get(i, {})
                    if ops & 1:
                        s['x'], s['y'] = struct.unpack_from('<hh', m, p); p += 4
                    if ops & 2:
                        s['w'], s['h'] = struct.unpack_from('<HH', m, p); p += 4
                elif c == 'b':
                    i = g16(); p += 6
                    sz = struct.unpack_from('<I', m, p)[0]; p += 4 + sz
                elif c == 'g':
                    p += 3
                elif c == 'u':
                    pass
                elif c == 'k':
                    p += 2
                elif c == 'D':
                    pass
                else:
                    break

    def send(self, cmd, args):
        self.ts += 10
        full = [ord(cmd), self.serial, self.ts] + list(args)
        payload = b''.join(struct.pack('>i', a) for a in full)
        mask = os.urandom(4)
        hdr = bytes([0x82])
        ln = len(payload)
        hdr += bytes([0x80 | ln]) if ln < 126 else bytes([0x80 | 126]) + struct.pack('>H', ln)
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        self.sock.sendall(hdr + mask + masked)

    # ---------- haut niveau ----------
    def surface_at(self, x, y):
        with self.lock:
            for i in reversed(self.stack):
                s = self.surfaces.get(i)
                if s and s['visible'] and s['x'] <= x < s['x'] + s['w'] and s['y'] <= y < s['y'] + s['h']:
                    return i, s
        return 0, None

    def move(self, x, y, sid=None):
        if sid is not None:
            with self.lock:
                i, s = sid, self.surfaces.get(sid)
        else:
            i, s = self.surface_at(x, y)
        wx, wy = (x - s['x'], y - s['y']) if s else (x, y)
        self.send('m', [i, i, x, y, wx, wy, self.state])
        return i, wx, wy

    def click(self, x, y, button=1, sid=None):
        i, wx, wy = self.move(x, y, sid)
        time.sleep(0.05)
        self.state |= BUTTON_MASK[button]
        self.send('b', [i, i, x, y, wx, wy, self.state, button])
        time.sleep(0.05)
        self.send('B', [i, i, x, y, wx, wy, self.state, button])
        self.state &= ~BUTTON_MASK[button]
        time.sleep(0.2)

    def toplevel_surface(self, obj):
        """Surface broadway de la fenêtre de premier niveau qui contient obj (appariée par la taille)."""
        from gi.repository import Atspi
        top = obj
        while True:
            par = top.get_parent()
            if par is None or par.get_role_name() == 'application':
                break
            top = par
        ex = top.get_component_iface().get_extents(Atspi.CoordType.SCREEN)
        self._last_top = ex
        with self.lock:
            for i in reversed(self.stack):
                sf = self.surfaces.get(i)
                if sf and sf['visible'] and sf['w'] == ex.width and sf['h'] == ex.height:
                    return i, sf
        return None, None

    def popup_surface(self):
        with self.lock:
            for i in reversed(self.stack):
                sf = self.surfaces.get(i)
                if sf and sf['visible'] and sf['temp']:
                    return i, sf
        return None, None

    def click_obj(self, obj, button=1, dx=None, dy=None, popup=False):
        from gi.repository import Atspi
        ex = obj.get_component_iface().get_extents(Atspi.CoordType.SCREEN)
        i, sf = self.popup_surface() if popup else self.toplevel_surface(obj)
        if sf is None:
            raise RuntimeError('surface introuvable pour %s' % obj.get_name())
        top = self._last_top
        x = sf['x'] + (ex.x - top.x) + (dx if dx is not None else ex.width // 2)
        y = sf['y'] + (ex.y - top.y) + (dy if dy is not None else ex.height // 2)
        self.click(x, y, button, sid=i)
        return x, y

    def warm(self):
        self.send('k', [KEYSYM['Shift_L'], 0]); time.sleep(0.03); self.send('K', [KEYSYM['Shift_L'], 1]); time.sleep(0.2)

    def type(self, text):
        for ch in text:
            if ch == '\n':
                self.key('Return')
            elif ch.isupper():
                self.key(ord(ch), GDK_SHIFT)
            else:
                self.key(ord(ch))

    def key(self, keysym, mods=0):
        if isinstance(keysym, str):
            keysym = KEYSYM.get(keysym, ord(keysym) if len(keysym) == 1 else None)
        st = 0
        if mods & GDK_CONTROL:
            self.send('k', [KEYSYM['Control_L'], st]); st |= GDK_CONTROL
        if mods & GDK_SHIFT:
            self.send('k', [KEYSYM['Shift_L'], st]); st |= GDK_SHIFT
        if mods & GDK_MOD1:
            self.send('k', [KEYSYM['Alt_L'], st]); st |= GDK_MOD1
        self.send('k', [keysym, st])
        time.sleep(0.03)
        self.send('K', [keysym, st])
        if mods & GDK_MOD1:
            st &= ~GDK_MOD1; self.send('K', [KEYSYM['Alt_L'], st])
        if mods & GDK_SHIFT:
            st &= ~GDK_SHIFT; self.send('K', [KEYSYM['Shift_L'], st])
        if mods & GDK_CONTROL:
            st &= ~GDK_CONTROL; self.send('K', [KEYSYM['Control_L'], st])
        time.sleep(0.2)

    def close(self):
        self.alive = False
        try:
            self.sock.close()
        except Exception:
            pass


def focus_top(bw, w=None, h=None, dx=300, dy=22):
    """Clique dans la barre de titre de la dernière surface non temporaire visible (ou de taille w x h) pour lui donner le focus clavier."""
    with bw.lock:
        cands = [(i, bw.surfaces[i]) for i in bw.stack if i in bw.surfaces and bw.surfaces[i]['visible'] and not bw.surfaces[i]['temp']
                 and (w is None or (bw.surfaces[i]['w'] == w and bw.surfaces[i]['h'] == h))]
    i, sf = cands[-1]
    bw.click(sf['x'] + min(dx, sf['w'] // 2), sf['y'] + dy)
    return i, sf
