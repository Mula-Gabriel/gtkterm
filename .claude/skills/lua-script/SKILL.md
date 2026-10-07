---
name: lua-script
description: Scaffold a new GtkTerm Lua example/automation script following the scripts/ conventions and the gtkterm.* engine API. Use when the user wants a new Lua script, a serial/TCP automation, a CSV logger, or a macro/protocol demo for GtkTerm.
disable-model-invocation: true
---

# lua-script — scaffold a GtkTerm Lua script

Generate a new Lua script under `scripts/` that drives GtkTerm through its embedded
Lua 5.4 engine (`src/script_engine.c`). Match the existing examples in tone and style.

## Conventions (follow these)

- File name: next free number, snake_case, e.g. `scripts/12_my_thing.lua`.
- Header: a `-- NN_name.lua` line plus 1–3 comment lines describing the goal. Comments and log strings are in **French**, matching the rest of `scripts/`.
- **Handle 0 is always the main GtkTerm port** — already open, no `open()` needed.
- Other handles (1, 2, …) must be opened with `gtkterm.open(...)` and closed with `gtkterm.close(handle)`. Always check the `ok, err` return.
- `gtkterm.log(...)` and `print(...)` both write to the script console. Use `gtkterm.sleep(ms)` for pacing. `os.exit([code])` ends the script (never GtkTerm): `os.exit()`, `os.exit(true)` and `os.exit(0)` end it normally, any other code ends it in error (« Script exited with status N », code not truncated). « Stop » interrupts any script, even a pure computation loop, and writes no « Lua error » line.

## Engine API (`gtkterm.*`)

Failures return `false` (or `nil`) followed by a stable English reason: `"invalid handle"`, `"already open"`, `"not open"`,
`"not connected"`, `"link lost"`, `"timeout"`, `"closed"`, `"invalid bits"`, `"invalid stop"`, `"invalid parity"`, `"invalid flow"`,
`"invalid baud"`, `"invalid port"`, `"unknown type (serial/tcp_client/tcp_server)"`, `"macro not found"`, `"invalid hex: …"`
(followed by the decoder's reason, e.g. `invalid character at position 4`), or the transport's error message (no trailing newline;
for a `tcp_client` it names the host you gave before the numeric address: `Cannot connect to localhost (127.0.0.1:4000): …`).
A wrong argument type raises a Lua error.

| Call | Returns | Notes |
|------|---------|-------|
| `log(value)` / `print(...)` | – | write to the GtkTerm script console (`print`: tab-separated, like standard Lua) |
| `sleep(ms)` | – | block the script ms milliseconds; interrupted at once by « Stop » |
| `time()` | number | monotonic time in seconds, for timing/duration |
| `send(handle, str [, timeout_ms=5000])` | `true` \| `false, err` | everything written or failure: partial writes retried until the timeout (`"timeout"`); nothing writable → `"not connected"`; lost → `"link lost"`; handle 0 = main port |
| `send_hex(handle, "DE AD BE EF" [, timeout_ms])` | `true` \| `false, err` | hex pairs, whitespace between bytes only; invalid text → `false, "invalid hex: …"`, nothing sent |
| `read(handle)` | string | drain everything currently buffered (may be ""); still works on a lost port |
| `wait_for(handle, pattern [, timeout_ms=5000])` | string \| `nil, "timeout"` \| `nil, "closed"` | data up to and including pattern (binary safe); data already received is returned first, then a lost/closed port gives `"closed"` at once (what is left stays available to `read`). Handle 0: its queue is never closed, so only data or `"timeout"` |
| `status(handle)` | string | `"open"`, `"connecting"`, `"listening"`, `"connected"`, `"lost"`, `"closed"` (handle 0: state of the main link) |
| `open(handle, "serial", dev [, baud=9600, bits=8, stop=1, parity=0, flow=0])` | `true` \| `false, err` | any baud the port accepts; parity `0/1/2` or `"none"/"odd"/"even"`, flow `0/1/2` or `"none"/"xon"/"rts"`; port locked |
| `open(handle, "tcp_client", host, port [, timeout_ms=5000])` | `true` \| `false, err` | IPv4/IPv6; the name is resolved on the script thread (the resolution itself cannot be stopped), then each address is tried in turn (refused → next, e.g. `localhost` to an IPv4-only server), the timeout bounding the whole; waits for the connection (`"timeout"`); a timeout of 0 or a negative one returns `false, "timeout"` at once |
| `open(handle, "tcp_server", port [, host])` | `true` \| `false, err` | optional listen address; a new client replaces the previous one; `status()` tells whether a client is connected; port 0 is accepted (the system picks a port) but the chosen port is not returned, so use a fixed port when a peer must connect; a send after the client left gives `false, "not connected"` |
| `close(handle)` | – | no effect on a closed port; every port is closed automatically when the script ends or is stopped. Serial: waits first for pending output to be sent (at most 2 s in all; skipped when stopping, **including `os.exit`**: call `gtkterm.close(h)` before `os.exit` when a last `send` must not be lost), so a last `send` before the end is not lost |
| `clear(handle)` | – | empty the receive buffer of handle (not the terminal) |
| `save_csv(file, col1, col2, ...)` | `true` \| `false, err` | append a CSV row; first call with header strings |
| `send_macro(index_or_label, ...args)` | `true`/`false` \| `false, "macro not found"` | send a configured macro (0-based integer index or label; an index outside 0..2147483647 is "not found", a non-integer number raises an error); true once actually sent |
| `get_macros()` | table | `{ {index=, label=, ...}, ... }` |
| `get_lists()` | table | configured macro lists |

Receive buffers hold 1 MiB per port; on overflow the oldest bytes are dropped and the console says so once per port and run.

Serial close and large sends: the wait on close relies on the driver's output queue (`TIOCOUTQ`) plus the time of 64 bytes for the UART FIFO, then the port's settings (including its baud rate) are restored and the unsent output discarded. A USB adapter with a large internal buffer can still hold data the driver does not count: the end of a big send may then go out at the restored baud rate or be cut. Prefer a short last frame (a `wait_for` on the device's answer is the reliable acknowledgement).

Stopping (« Stop », or closing GtkTerm): every `gtkterm.*` function and `print` raises the stop at once when it is requested, and `pcall` cannot swallow it for good. Limits:
- the name resolution of a `tcp_client` host (`getaddrinfo`) cannot be interrupted: a « Stop » during it is seen when it returns;
- long C calls (`io.read`, `os.execute`, a huge `string.rep`, pathological patterns) are only interrupted when they return; `os.execute`, `io.read`/`lines`/`popen`/`open`/`write` and file `read`/`lines`/`write` also see the stop on entry, so a loop of slow calls stops after the current one;
- a pure Lua loop inside a `__gc` finalizer cannot be interrupted (Lua disables hooks there); finalizers run at the end cannot open ports or wait;
- a loop that only calls the standard library sees the stop every 1000 instructions;
- `debug.sethook` is removed, but `package.loadlib` can bypass that removal.

## Template

```lua
-- NN_NAME.lua
-- DESCRIPTION (1–3 lignes, en français).

gtkterm.log("=== NAME ===")

-- Sur le port principal (handle 0, toujours ouvert) :
gtkterm.send(0, "AT\r\n")
local resp = gtkterm.wait_for(0, "OK", 2000)
gtkterm.log(resp or "timeout")

-- Pour un port secondaire, ouvrir/fermer explicitement :
-- local ok, err = gtkterm.open(1, "serial", "/dev/ttyUSB0", 115200, 8, 1, 0, 0)
-- if not ok then gtkterm.log("open: " .. tostring(err)); return end
-- ...
-- gtkterm.close(1)

gtkterm.log("=== Terminé ===")
```

## Steps

1. Ask the user (or infer from the request) what the script should do and which transport(s).
2. Pick the next free `scripts/NN_*.lua` number (`ls scripts/`).
3. Write the script from the template, French comments/logs, with `ok, err` checks on every `open`.
4. Show the file path and a one-line note on how to run it from GtkTerm's script panel.
