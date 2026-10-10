# Faux navigateur pour gtk4-broadwayd (GTK 4.14) : se connecte en WebSocket comme le client JavaScript embarqué (broadway.js, lisible par
# `strings /usr/bin/gtk4-broadwayd`), suit les surfaces, répond aux aller-retours et injecte souris et clavier. Repris de l'essai de
# faisabilité du 2026-10-09 ; utilisé par gtktest.py (Env.bw) quand le geste réel est l'objet du test.
#
# Protocole GTK 4 (différences avec GTK 3 : opérations numériques, pas d'octet « temp » dans NEW_SURFACE, ROUNDTRIP auquel le
# navigateur DOIT répondre, SCREEN_SIZE_CHANGED à 3 arguments).
# Serveur -> navigateur : trames WebSocket binaires, suites d'opérations « u8 op, u32 serial » (petit-boutiste) puis, selon op :
#     0 GRAB_POINTER      u16 id, u8 owner_events          1 UNGRAB_POINTER   —
#     2 NEW_SURFACE       u16 id, s16 x, s16 y, u16 w, u16 h
#     3 SHOW / 4 HIDE / 5 RAISE / 6 LOWER / 7 DESTROY  u16 id
#     8 MOVE_RESIZE       u16 id, u8 drapeaux, [s16 x, s16 y si bit 1], [u16 w, u16 h si bit 2]
#     9 SET_TRANSIENT_FOR u16 id, u16 parent              10 DISCONNECTED   —
#    12 SET_SHOW_KEYBOARD u16                             13 UPLOAD_TEXTURE u32 id, u32 taille, octets (PNG)
#    14 RELEASE_TEXTURE   u32 id                          15 SET_NODES      u16 id, u32 n, n*4 octets
#    16 ROUNDTRIP         u16 id, u32 tag  -> répondre ROUNDTRIP_NOTIFY [id, tag]
# Navigateur -> serveur : trame binaire masquée = int32 gros-boutistes [type, dernier serial reçu, horodatage ms, args…]
#     0 ENTER / 1 LEAVE    [surface réelle, surface cible, rootX, rootY, winX, winY, état, mode de croisement]
#     2 POINTER_MOVE       [surface réelle, surface cible, rootX, rootY, winX, winY, état]
#     3 BUTTON_PRESS / 4 BUTTON_RELEASE  [surface réelle, surface cible, rootX, rootY, winX, winY, état, bouton]
#     7 KEY_PRESS / 8 KEY_RELEASE  [keysym, état]          11 CONFIGURE_NOTIFY [id, x, y, w, h] (à chaque NEW_SURFACE / MOVE_RESIZE)
#    12 SCREEN_SIZE_CHANGED [w, h, échelle]                14 ROUNDTRIP_NOTIFY [id, tag]
# Le focus clavier suit les clics (géré par le serveur ; le JS n'envoie jamais FOCUS).
#
# Deux pièges mesurés par l'essai :
# - masque du bouton présent à l'appui ET au relâchement (sinon la prise implicite du premier widget cliqué n'est jamais relâchée) ;
# - gtk4-broadwayd pose toutes les surfaces en (0, 0) : un dialogue recouvre sa parente ; on vise donc une surface par son id,
#   retrouvée par sa taille (surface_w/h du crochet test-dump, ombres CSD comprises), jamais par « la plus haute sous le point ».
import socket, os, struct, threading, time, base64, sys

GDK_SHIFT, GDK_CONTROL, GDK_ALT = 1 << 0, 1 << 2, 1 << 3
MODS = {'shift': GDK_SHIFT, 'ctrl': GDK_CONTROL, 'control': GDK_CONTROL, 'alt': GDK_ALT}
BUTTON_MASK = {1: 1 << 8, 2: 1 << 9, 3: 1 << 10}

(OP_GRAB_POINTER, OP_UNGRAB_POINTER, OP_NEW_SURFACE, OP_SHOW_SURFACE, OP_HIDE_SURFACE, OP_RAISE_SURFACE, OP_LOWER_SURFACE,
 OP_DESTROY_SURFACE, OP_MOVE_RESIZE, OP_SET_TRANSIENT_FOR, OP_DISCONNECTED, OP_SURFACE_UPDATE, OP_SET_SHOW_KEYBOARD,
 OP_UPLOAD_TEXTURE, OP_RELEASE_TEXTURE, OP_SET_NODES, OP_ROUNDTRIP) = range(17)
(EV_ENTER, EV_LEAVE, EV_POINTER_MOVE, EV_BUTTON_PRESS, EV_BUTTON_RELEASE, EV_TOUCH, EV_SCROLL, EV_KEY_PRESS, EV_KEY_RELEASE,
 EV_GRAB_NOTIFY, EV_UNGRAB_NOTIFY, EV_CONFIGURE_NOTIFY, EV_SCREEN_SIZE_CHANGED, EV_FOCUS, EV_ROUNDTRIP_NOTIFY) = range(15)
CROSSING_NORMAL = 0

KEYSYM = {'Escape': 0xff1b, 'Return': 0xff0d, 'Tab': 0xff09, 'BackSpace': 0xff08, 'Delete': 0xffff, 'Home': 0xff50, 'End': 0xff57,
          'Left': 0xff51, 'Up': 0xff52, 'Right': 0xff53, 'Down': 0xff54, 'Page_Up': 0xff55, 'Page_Down': 0xff56,
          'Control_L': 0xffe3, 'Shift_L': 0xffe1, 'Alt_L': 0xffe9}
for _i in range(1, 13):
    KEYSYM['F%d' % _i] = 0xffbe + _i - 1


class Broadway:
    """Client WebSocket de gtk4-broadwayd. surfaces : id -> dict(x, y, w, h, visible, parent) ; stack : ordre d'empilement.
    dump : fonction sans argument qui rend le dump du crochet (gtktest.Env.dump), pour focus_window()."""

    def __init__(self, display, dump=None, width=1600, height=1000, host='127.0.0.1'):
        self.port = 8080 + display
        self.dump = dump
        self.sock = socket.create_connection((host, self.port), timeout=10)
        key = base64.b64encode(os.urandom(16)).decode()
        req = ('GET /socket HTTP/1.1\r\nHost: %s:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: %s\r\n'
               'Sec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: broadway\r\nOrigin: http://%s:%d\r\n\r\n') % (host, self.port, key, host, self.port)
        self.sock.sendall(req.encode())
        resp = b''
        while b'\r\n\r\n' not in resp:
            c = self.sock.recv(1)
            if not c:
                raise RuntimeError('connexion broadway fermée pendant la poignée de main')
            resp += c
        if b' 101 ' not in resp.split(b'\r\n')[0]:
            raise RuntimeError('poignée de main broadway refusée : %r' % resp)
        self.sock.settimeout(None)
        self.surfaces = {}
        self.stack = []
        self.grab = None            # (surface, owner_events) posé par GRAB_POINTER
        self.serial = 0
        self.t0 = time.monotonic()
        self.state = 0              # modificateurs et boutons enfoncés
        self.pointer_surface = 0
        self.lock = threading.RLock()
        self.send_lock = threading.Lock()
        self.changed = threading.Condition(self.lock)
        self.alive = True
        self.closing = False
        self.error = None
        self.reader = threading.Thread(target=self._reader, daemon=True)
        self.reader.start()
        self.send(EV_SCREEN_SIZE_CHANGED, [width, height, 1])

    # ---------- réception ----------
    def _recv_exact(self, n):
        b = bytearray()
        while len(b) < n:
            c = self.sock.recv(n - len(b))
            if not c:
                raise EOFError
            b += c
        return bytes(b)

    def _reader(self):
        buf = b''
        try:
            while self.alive:
                h = self._recv_exact(2)
                fin, op, masked, ln = h[0] & 0x80, h[0] & 0x0f, h[1] & 0x80, h[1] & 0x7f
                if ln == 126:
                    ln = struct.unpack('>H', self._recv_exact(2))[0]
                elif ln == 127:
                    ln = struct.unpack('>Q', self._recv_exact(8))[0]
                mask = self._recv_exact(4) if masked else None
                data = self._recv_exact(ln)
                if mask:
                    data = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
                if op == 8:
                    break
                if op == 9:
                    self._send_frame(data, opcode=0x0a)
                    continue
                if op == 10:
                    continue
                buf += data
                if fin:
                    self._handle(buf)
                    buf = b''
        except Exception as ex:
            if not self.closing:
                self._fail('fil lecteur arrete : %s: %s' % (type(ex).__name__, ex))
        finally:
            self.alive = False
            with self.lock:
                self.changed.notify_all()

    def _fail(self, msg):
        """Mémorise et journalise (stderr du scénario) une panne du faux navigateur ; send/key/click lèvent ensuite RuntimeError."""
        if self.error is None:
            self.error = msg
            sys.stderr.write('broadway : %s\n' % msg)
            sys.stderr.flush()

    def _check(self):
        if self.error is not None or (not self.alive and not self.closing):
            raise RuntimeError('faux navigateur arrete : %s' % (self.error or 'connexion fermee'))

    def _handle(self, m):
        p = 0
        replies = []
        with self.lock:
            while p < len(m):
                op = m[p]
                self.serial = struct.unpack_from('<I', m, p + 1)[0]
                p += 5
                if op == OP_NEW_SURFACE:
                    i, x, y, w, h = struct.unpack_from('<HhhHH', m, p); p += 10
                    self.surfaces[i] = dict(x=x, y=y, w=w, h=h, visible=False, parent=0)
                    self.stack.append(i)
                    replies.append((EV_CONFIGURE_NOTIFY, [i, x, y, w, h]))
                elif op in (OP_SHOW_SURFACE, OP_HIDE_SURFACE, OP_RAISE_SURFACE, OP_LOWER_SURFACE, OP_DESTROY_SURFACE):
                    i = struct.unpack_from('<H', m, p)[0]; p += 2
                    s = self.surfaces.get(i)
                    if op == OP_SHOW_SURFACE and s:
                        s['visible'] = True
                    elif op == OP_HIDE_SURFACE and s:
                        s['visible'] = False
                        if self.grab and self.grab[0] == i:
                            self.grab = None
                    elif op == OP_RAISE_SURFACE and i in self.stack:
                        self.stack.remove(i)
                        self.stack.append(i)
                    elif op == OP_LOWER_SURFACE and i in self.stack:
                        self.stack.remove(i)
                        self.stack.insert(0, i)
                    elif op == OP_DESTROY_SURFACE:
                        self.surfaces.pop(i, None)
                        if i in self.stack:
                            self.stack.remove(i)
                        if self.grab and self.grab[0] == i:
                            self.grab = None
                elif op == OP_MOVE_RESIZE:
                    i, fl = struct.unpack_from('<HB', m, p); p += 3
                    s = self.surfaces.setdefault(i, dict(x=0, y=0, w=0, h=0, visible=False, parent=0))
                    if fl & 1:
                        s['x'], s['y'] = struct.unpack_from('<hh', m, p); p += 4
                    if fl & 2:
                        s['w'], s['h'] = struct.unpack_from('<HH', m, p); p += 4
                    replies.append((EV_CONFIGURE_NOTIFY, [i, s['x'], s['y'], s['w'], s['h']]))
                elif op == OP_SET_TRANSIENT_FOR:
                    i, par = struct.unpack_from('<HH', m, p); p += 4
                    if i in self.surfaces:
                        self.surfaces[i]['parent'] = par
                        if par and par in self.stack and i in self.stack:
                            self.stack.remove(i)
                            self.stack.insert(self.stack.index(par) + 1, i)
                elif op == OP_ROUNDTRIP:
                    i, tag = struct.unpack_from('<HI', m, p); p += 6
                    replies.append((EV_ROUNDTRIP_NOTIFY, [i, tag]))
                elif op == OP_UPLOAD_TEXTURE:
                    _tid, sz = struct.unpack_from('<II', m, p); p += 8 + sz
                elif op == OP_RELEASE_TEXTURE:
                    p += 4
                elif op == OP_SET_NODES:
                    _i, n = struct.unpack_from('<HI', m, p); p += 6 + 4 * n
                elif op == OP_GRAB_POINTER:
                    i, owner = struct.unpack_from('<HB', m, p); p += 3
                    self.grab = (i, bool(owner))
                    replies.append((EV_GRAB_NOTIFY, []))
                elif op == OP_UNGRAB_POINTER:
                    self.grab = None
                    replies.append((EV_UNGRAB_NOTIFY, []))
                elif op == OP_SET_SHOW_KEYBOARD:
                    p += 2
                elif op == OP_DISCONNECTED:
                    pass
                else:                # opération inconnue : longueur inconnue, on ne peut plus décoder la suite : panne bruyante
                    raise RuntimeError('operation broadway inconnue : %d' % op)
            self.changed.notify_all()
        for ev, args in replies:     # hors verrou ; réponse immédiate (sinon le rendu GTK 4 se fige)
            self.send(ev, args)

    # ---------- émission ----------
    def _send_frame(self, payload, opcode=0x02):
        mask = os.urandom(4)
        ln = len(payload)
        hdr = bytes([0x80 | opcode])
        if ln < 126:
            hdr += bytes([0x80 | ln])
        elif ln < 65536:
            hdr += bytes([0x80 | 126]) + struct.pack('>H', ln)
        else:
            hdr += bytes([0x80 | 127]) + struct.pack('>Q', ln)
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        with self.send_lock:
            self.sock.sendall(hdr + mask + masked)

    def send(self, ev, args):
        self._check()
        ts = int((time.monotonic() - self.t0) * 1000) + 1000
        self._send_frame(b''.join(struct.pack('>i', a) for a in [ev, self.serial, ts] + list(args)))

    # ---------- surfaces ----------
    def visible_surfaces(self):
        with self.lock:
            return [(i, dict(self.surfaces[i])) for i in self.stack if i in self.surfaces and self.surfaces[i]['visible']]

    def wait_surface(self, pred, timeout=5.0):
        """Surface visible la plus haute qui vérifie pred(id, surface) : (id, surface) ou (None, None)."""
        end = time.monotonic() + timeout
        with self.lock:
            while True:
                for i in reversed(self.stack):
                    s = self.surfaces.get(i)
                    if s and s['visible'] and pred(i, s):
                        return i, dict(s)
                left = end - time.monotonic()
                if left <= 0 or not self.alive:
                    return None, None
                self.changed.wait(min(left, 0.1))

    def surface_for(self, widget, timeout=5.0):
        """Id de la surface qui porte widget (entrée du dump enrichie de native_w / native_h par gtktest)."""
        sid, _s = self.wait_surface(lambda i, s: s['w'] == widget['native_w'] and s['h'] == widget['native_h'], timeout)
        if sid is None:
            raise RuntimeError('surface %dx%d introuvable (visibles : %r)' % (widget['native_w'], widget['native_h'], self.visible_surfaces()))
        return sid

    # ---------- souris ----------
    def _target(self, real):
        """Surface qui reçoit l'événement, comme getEffectiveEventTarget() du JS (prise du pointeur par un popover)."""
        if self.grab:
            g, owner = self.grab
            if not owner or real == 0:
                return g
        return real

    def _pos(self, x, y, sid):
        s = self.surfaces.get(sid)
        return (x - s['x'], y - s['y']) if s else (x, y)

    def _move(self, x, y, sid):
        with self.lock:
            tgt = self._target(sid)
            if sid != self.pointer_surface:
                if self.pointer_surface:
                    old = self._target(self.pointer_surface)
                    self.send(EV_LEAVE, [self.pointer_surface, old, x, y, *self._pos(x, y, old), self.state, CROSSING_NORMAL])
                self.send(EV_ENTER, [sid, tgt, x, y, *self._pos(x, y, tgt), self.state, CROSSING_NORMAL])
                self.pointer_surface = sid
            self.send(EV_POINTER_MOVE, [sid, tgt, x, y, *self._pos(x, y, tgt), self.state])

    def _button(self, x, y, button, press, sid):
        # Masque du bouton à l'appui ET au relâchement (essai, probe_dbg5 : seule combinaison où tous les clics successifs arrivent)
        with self.lock:
            tgt = self._target(sid)
            self.state |= BUTTON_MASK[button]
            self.send(EV_BUTTON_PRESS if press else EV_BUTTON_RELEASE, [sid, tgt, x, y, *self._pos(x, y, tgt), self.state, button])
            if not press:
                self.state &= ~BUTTON_MASK[button]

    def click_in_surface(self, sid, wx, wy, button=1, settle=0.3):
        """Clic à (wx, wy) en coordonnées de la surface sid (celles du crochet test-dump)."""
        with self.lock:
            s = dict(self.surfaces[sid])
        x, y = s['x'] + wx, s['y'] + wy
        self._move(x, y, sid)
        time.sleep(0.05)
        self._button(x, y, button, True, sid)
        time.sleep(0.05)
        self._button(x, y, button, False, sid)
        time.sleep(settle)

    @staticmethod
    def _geometry(w):
        return tuple(w.get(k) for k in ('x', 'y', 'w', 'h', 'native_w', 'native_h'))

    def settled(self, widget, timeout=3.0):
        """Entrée du dump du même widget (même id) dont la géométrie est réelle : non nulle, lue après au moins une image de sa surface
        postérieure au premier relevé (compteur « frame » du natif), et identique dans deux dumps successifs.
        Mesuré (hex-entry juste affiché) : la géométrie reste périmée (18×2, stable d'un dump à l'autre) tant que l'horloge de
        la surface n'a pas produit d'image, soit 0,7 à 1,3 s sous broadway ; deux dumps identiques ne suffisent donc pas. Chaque test-dump
        demande une image à chaque surface (test_hooks.c) : au repos aussi, le compteur avance en quelques dizaines de ms.
        Délai borné : passé timeout, la dernière géométrie non nulle ; sans dump (Broadway seul) : le widget tel quel."""
        if self.dump is None or 'id' not in widget:
            return widget
        end = time.monotonic() + timeout
        first_frame = None
        last = None
        while True:
            d = self.dump()
            cur = [w for w in d if w['id'] == widget['id'] and w['mapped'] and 'x' in w and 'native_w' in w]
            cur = cur[0] if cur and cur[0]['w'] > 0 and cur[0]['h'] > 0 else None
            frame = None
            if cur is not None:
                nat = [w for w in d if w['native'] == cur['native'] and 'frame' in w]
                frame = nat[0]['frame'] if nat else None
                if first_frame is None:
                    first_frame = frame
            drawn = frame is None or first_frame is None or frame > first_frame
            if cur is not None and drawn and last is not None and self._geometry(cur) == self._geometry(last):
                return cur
            if time.monotonic() > end:
                if cur is None:
                    raise RuntimeError('widget sans géométrie (non mappé ou de taille nulle) après %.1f s : %r'
                                       % (timeout, {k: widget.get(k) for k in ('type', 'name', 'label')}))
                # Repli signalé (journal du scénario) : la géométrie peut être périmée si aucune image n'est passée
                print('[gui] settled : repli apres %.1f s sur %r (image %r -> %r)' % (timeout, self._geometry(cur), first_frame, frame), flush=True)
                return cur
            last = cur if drawn else None
            time.sleep(0.05)

    def click(self, widget, button=1, settle=0.3):
        """Clic au centre d'un widget du dump (géométrie x, y, w, h en coordonnées de sa surface), une fois sa géométrie stable (settled)."""
        if 'x' not in widget and (self.dump is None or 'id' not in widget):
            raise RuntimeError('widget sans géométrie (non mappé) : %r' % {k: widget.get(k) for k in ('type', 'name', 'label')})
        widget = self.settled(widget)
        sid = self.surface_for(widget)
        self.click_in_surface(sid, widget['x'] + widget['w'] // 2, widget['y'] + widget['h'] // 2, button, settle)

    def focus_window(self, title, timeout=5):
        """Donne le focus clavier à la fenêtre de titre exact title : clic dans sa barre de titre (GtkHeaderBar de la décoration)."""
        end = time.monotonic() + timeout
        while True:
            d = self.dump()
            win = [w for w in d if w['depth'] == 0 and w.get('title') == title and 'native_w' in w]
            if win:
                bars = [w for w in d if w['native'] == win[0]['native'] and w['type'] == 'GtkHeaderBar' and 'x' in w]
                target = bars[0] if bars else win[0]
                sid = self.surface_for(win[0])
                # Bord gauche de la barre (loin des boutons de la décoration et du titre centré)
                self.click_in_surface(sid, target['x'] + 8, target['y'] + target['h'] // 2)
                return win[0]
            if time.monotonic() > end:
                raise RuntimeError('fenêtre « %s » introuvable' % title)
            time.sleep(0.1)

    # ---------- clavier ----------
    @staticmethod
    def _mask(mods):
        if isinstance(mods, int):
            return mods
        m = 0
        for name in mods:
            m |= MODS[name.lower()]
        return m

    def key(self, keysym, mods=(), settle=0.15):
        """Appui + relâchement : keysym = nom (KEYSYM), caractère ou entier ; mods = ('ctrl', 'shift', 'alt') ou masque GDK."""
        if isinstance(keysym, str):
            keysym = KEYSYM[keysym] if keysym in KEYSYM else ord(keysym)
        mask = self._mask(mods)
        seq = [(GDK_CONTROL, 'Control_L'), (GDK_SHIFT, 'Shift_L'), (GDK_ALT, 'Alt_L')]
        st = self.state
        for bit, name in seq:
            if mask & bit:
                st |= bit
                self.send(EV_KEY_PRESS, [KEYSYM[name], st])
        self.send(EV_KEY_PRESS, [keysym, st])
        time.sleep(0.03)
        self.send(EV_KEY_RELEASE, [keysym, st])
        for bit, name in reversed(seq):
            if mask & bit:
                self.send(EV_KEY_RELEASE, [KEYSYM[name], st])
                st &= ~bit
        time.sleep(settle)

    def type(self, text, settle=0.15):
        for ch in text:
            if ch == '\n':
                self.key('Return', settle=settle)
            elif ch.isupper():
                self.key(ord(ch), GDK_SHIFT, settle=settle)
            else:
                self.key(ord(ch), settle=settle)

    def close(self):
        self.closing = True
        self.alive = False
        try:
            self.sock.shutdown(socket.SHUT_RDWR)
        except Exception:
            pass
        try:
            self.sock.close()
        except Exception:
            pass
