---
name: lua-script
description: Scaffold a new GtkTerm Lua example/automation script following the scripts/ conventions and the gtkterm.* engine API. Use when the user wants a new Lua script, a serial/TCP automation, a CSV logger, or a macro/protocol demo for GtkTerm.
disable-model-invocation: true
---

# lua-script — scaffold a GtkTerm Lua script

Generate a new Lua script under `scripts/` that drives GtkTerm through its embedded
Lua 5.4 engine (`src/script_engine.c`). Match the existing examples in tone and style.

## Conventions (follow these)

- File name: next free number, snake_case, e.g. `scripts/11_my_thing.lua`.
- Header: a `-- NN_name.lua` line plus 1–3 comment lines describing the goal. Comments and log strings are in **French**, matching the rest of `scripts/`.
- **Handle 0 is always the main GtkTerm port** — already open, no `open()` needed.
- Other handles (1, 2, …) must be opened with `gtkterm.open(...)` and closed with `gtkterm.close(handle)`. Always check the `ok, err` return.
- Prefer `gtkterm.log(...)` over `print`. Use `gtkterm.sleep(ms)` for pacing.

## Engine API (`gtkterm.*`)

| Call | Returns | Notes |
|------|---------|-------|
| `log(value)` | – | logs one value (converted with tostring) to the GtkTerm script console |
| `sleep(ms)` | – | block the script ms milliseconds |
| `time()` | number | monotonic time, for timing/duration |
| `send(handle, str)` | boolean | send a string; true if fully written (handle 0 = main port) |
| `send_hex(handle, "DEADBEEF")` | boolean | send raw bytes from a hex string; true if fully written |
| `read(handle)` | string | drain everything currently buffered (may be "") |
| `wait_for(handle, pattern, timeout_ms)` | string \| nil | data up to and including pattern, or nil on timeout |
| `open(handle, "serial", dev, baud, bits, stop, parity, flow)` | ok, err | e.g. `(1,"serial","/dev/ttyUSB0",115200,8,1,0,0)` |
| `open(handle, "tcp_client", host, port)` | ok, err | |
| `open(handle, "tcp_server", port)` | ok, err | listen on all interfaces; the first client is accepted |
| `close(handle)` | – | |
| `clear(handle)` | – | empty the receive buffer of handle (not the terminal) |
| `save_csv(file, col1, col2, ...)` | ok, err | append a CSV row; first call with header strings |
| `send_macro(index_or_label, ...args)` | ok, err | send a configured macro (0-based index or label); true once actually sent |
| `get_macros()` | table | `{ {index=, label=, ...}, ... }` |
| `get_lists()` | table | configured macro lists |

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
