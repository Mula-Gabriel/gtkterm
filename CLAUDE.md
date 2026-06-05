# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

GTKTerm is a GTK+3 serial-port terminal emulator for Linux (this is a fork with substantial macro, TCP-transport, and Lua-scripting additions). Written in C, built with Meson/Ninja. Comments and commit messages are in French.

## Build / Run

```sh
meson setup build        # once (build/ already exists here; re-run only after meson.build changes)
ninja -C build           # build — the common edit/compile loop
./build/src/gtkterm      # run
ninja -C build install   # system-wide install (+ gtk-update-icon-cache)
```

A PostToolUse hook (`.claude/settings.json`) runs `ninja -C build` automatically after edits to `src/*.c|*.h`, so compile errors surface immediately.

**No test suite exists.** CI (`.circleci/config.yml`) only does a build on `debian:buster` — note that image is EOL and its GTK/VTE/meson versions lag the local build, so green CI ≠ builds locally. Verify changes by building and running the app, and by running the Lua examples in `scripts/`.

Dependencies: `gtk+-3.0`, `vte-2.91`, `gudev-1.0`, `lua5.4`, `gtksourceview-4`. `jq` is **not** installed — shell scripting here uses `python3` for JSON.

## Architecture — the data flow that spans files

**Receive path (device → screen):**
`transport.c` reads bytes → `put_chars()` in **`buffer.c`** → the registered *display function* → **`terminal_display.c`** (`put_text` for ASCII, `put_hexadecimal` for hex) → `vte_terminal_feed()` into the VTE widget.

`buffer.c` holds a 128 KB local buffer of everything received and **decouples reception from display via function pointers**:
- `display_func` — where received bytes are rendered. Swapped by `set_view()` in `interface.c` to switch ASCII/hex view (`set_display_func(put_text|put_hexadecimal)`).
- `tap_func` — a second sink; this is how the Lua engine observes incoming data without disturbing display.
- `clear_func` — clears the view.

So to change how incoming data is displayed/captured, you change which function is registered with the buffer, not the receive loop.

**Transport abstraction (`transport.c` / `transport.h`):** one interface (`transport_open/close/send/get_fd/...`) over three modes — `TRANSPORT_SERIAL`, `TRANSPORT_TCP_CLIENT`, `TRANSPORT_TCP_SERVER`. `serial.c` is the serial-specific layer below it (termios, baud-rate tables in `baud.c`/`baudrates.c`, control signals DTR/RTS). Privileged TCP ports use `CAP_NET_BIND_SERVICE`.

**Lua scripting (`script_engine.c` + `script_panel.c`):** scripts run on a **background GThread** (`g_thread_new("lua-script", …)`). The Lua thread must never touch GTK directly — all UI/port effects are marshalled back to the GTK main loop with `g_idle_add` (see `log_idle_cb`, `send_macro_idle`, `done_idle_cb`), guarded by `engine_mutex` and per-handle mutexes. The `gtkterm.*` Lua API is registered here. `script_panel.c` is the editor/console UI (GtkSourceView 4). **When adding a `gtkterm.*` function, keep this rule: do the work on the Lua thread, but defer any GTK/transport side effect via `g_idle_add`.** Example scripts live in `scripts/` (use the `/lua-script` skill to scaffold new ones).

**Macros subsystem (four files):**
- `macros.c` — core logic, send, polling.
- `macros_format.c` — printf-style format arguments (`%d`, `%s`, `%#ListName`, length modifiers) with per-type range truncation/validation.
- `macros_list.c` — named value lists referenced as `%#ListName`.
- `macro_panel.c` — the button panel UI (argument fields, tab groups, polling toggles, `%&` separators).

Macros and lists are stored in a **standalone `.ini` file**, separate from the port config; the last-used path is remembered in `.gtktermrc`.

**Config (`term_config.c` + `parsecfg.c`):** the port/window config is `.gtktermrc` under `$XDG_CONFIG_HOME` (migrated from `$HOME`). `parsecfg.c` is the generic parser; `term_config.c` extracts macro/list keyfile sections out before parsing and re-appends them after. Script-editor colors are a separate file: `~/.config/gtkterm_script_colors.ini`.

**Other entry points:** `gtkterm.c` (`main`, wiring order), `interface.c` (the large GTK GUI — menus, VTE widget, view switching, shortcuts), `device_monitor.c` (GUdev serial-device hotplug), `user_signals.c` (SIGUSR1/2 → open/close port), `cmdline.c` (CLI options incl. `--transport/--host/--tcp-port`), `search.c`, `logging.c`, `files.c`, `i18n.c` (gettext; translations in `po/`).

## Code style

The codebase is **mixed-style** — be deliberate. The dominant style (`buffer.c`, `interface.c`, `term_config.c`) is **tabs**, **Allman braces** (`{` on its own line), and **no space before `(`** (`if(cond)`, not `if (cond)`). A few files (e.g. `terminal_display.c`) use a different GNU-ish 2-space style. **Match the style of the file you are editing.** A `.clang-format` encoding the dominant style is present, but do NOT bulk-reformat existing files — it would create huge diffs across the mixed styles. Format only new code / new files intentionally.
