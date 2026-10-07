# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

GTKTerm is a GTK+3 serial-port terminal emulator for Linux (this is a fork with substantial macro, TCP-transport, and Lua-scripting additions). Written in C, built with Meson/Ninja. Comments and commit messages are in French.

## Build / Run

```sh
meson setup build        # once (build/ already exists here; re-run only after meson.build changes)
ninja -C build           # build — the common edit/compile loop
./build/src/gtkterm      # run
meson test -C build      # tests unitaires (GLib GTest) + syntaxe des exemples Lua
meson test -C build --suite gui   # suite graphique (broadway + AT-SPI isolés, jamais l'écran réel)
sudo meson install -C build --no-rebuild   # system-wide install after `ninja -C build` (+ gtk-update-icon-cache)
```

A PostToolUse hook (`.claude/settings.json`) runs `ninja -C build` automatically after edits to `src/*.c|*.h`, so compile errors surface immediately.

**Tests:** `meson test -C build` runs GLib GTest unit tests (`tests/`) against `gtkterm_core`, a static library of the GTK-free modules (`transport.c`, `history.c`, `rx_transform.c`, `device_match.c`, `view_render.c`, `serial_termios.c`, `config_file.c`, `macros_format.c`, `macros_list.c`, `baudrates.c`, `baud.c`, `parsecfg.c`, `i18n.c` — see `core_sources` in `src/meson.build`) linked by both the app and the tests, plus integration tests that compile real `src/` modules (`link.c`, `macros.c`, `script_engine.c`) against UI stubs (`tests/stubs/`); plus `luac -p` on `scripts/*.lua`. Meson builds test executables with plain `ninja` too; configure with `-Dtests=false` (`meson_options.txt`) to skip the suite — the self-update script does. Cases tied to a known bug from `docs/superpowers/reviews/2026-10-06-revue-code.md` are marked with its ID and reported as `# TODO` without failing the suite (list them with `grep '# TODO' build/meson-logs/testlog.txt`; `meson test -v` only shows `EXPECTEDFAIL`); a marked case that starts passing fails, so the marker gets removed with the fix. See `tests/README.md` (sanitizers, valgrind, how to add a test). There is no remote CI: tests run locally. GUI behavior is covered by the `gui` suite (`tests/gui/`, see `tests/README.md`).

Dependencies: `gtk+-3.0`, `vte-2.91`, `gudev-1.0`, `lua5.4`, `gtksourceview-4`. `jq` is **not** installed — shell scripting here uses `python3` for JSON.

## Architecture — the data flow that spans files

**Layers (sub-project 2):** the GLib-only core (`gtkterm_core`: `transport.c`, `history.c`, `rx_transform.c`, `device_match.c`, `view_render.c`, `serial_termios.c`, `config_file.c`, plus the older macro/baud/parsecfg/i18n helpers), the link (`link.c`, `GtkTermLink` GObject + GUdev) and the GTK 3 UI. The UI never talks to the transport directly.

**Receive path (device → screen):** `transport.c` reads bytes → `on_data` callback → **`link.c`** appends them to the `GtHistory` (RX record with its wall time) and emits `received (GBytes *data, gint64 wall_time_us)` → subscribers: `terminal_display.c` (renders through `view_render.c`, then `vte_terminal_feed()`), `logging.c` (own `GtRxTransform`, no echo), `files.c` (wait-for-char during file send), the Lua engine (port 0, connected on the main thread).

**Send path:** everything goes through `gt_link_send(link, data, len, GtSendOrigin)` (keyboard, macro, Lua, hex, file). Only bytes actually written are appended to the history (TX record) and emitted by `sent`; echo is rendered from that, so it stays in place across view switches. The history keeps **raw** bytes: CR/LF auto and timestamps are a read-time transform (`GtRxTransform`), so toggling an option, the echo or the view redraws the terminal from the history (`terminal_display_redraw()`).

**Transport (`transport.c` / `transport.h`):** one instance per opened port (`gt_transport_open(params, callbacks, user_data, GError **)`), three modes — `TRANSPORT_SERIAL`, `TRANSPORT_TCP_CLIENT`, `TRANSPORT_TCP_SERVER`; no UI calls, errors via `GError`, `LOST` reported (deferred to an idle) instead of reconnecting. Serial specifics: termios in `serial_termios.c`, baud-rate tables in `baud.c`/`baudrates.c`. Privileged TCP ports use `CAP_NET_BIND_SERVICE`.

**Link (`link.c` / `link.h`):** single state machine for the main port — `CLOSED`, `CONNECTING`, `OPEN`, `LISTENING`, `LOST`, `RECONNECTING`; properties `state`, `status-text`, `auto-reconnect`, `reconnect-interval-ms`; signals `open-failed` (user-requested open only → error dialog), `received`, `sent`, `history-cleared`. Link loss never opens a dialog: the status bar shows « — lost » / « — reconnecting… »; silent retry every 2 s plus an immediate retry when udev reports the device back. With `auto-reconnect`, a failed user-requested open (serial or TCP client) shows `open-failed` once, then goes to `RECONNECTING` too (device plugged in / server started after GTKTerm). The udev monitor lives here and matches by device number, then path/links (`device_match.c`). **Re-entrancy (see `link.h`):** handlers of `received`, `sent` and `notify::state`/`notify::status-text` may run inside the transport's fd watch or inside `gt_link_send`; they must not call `gt_link_open`/`gt_link_close` nor run a nested main loop (`show_message`, `gtk_dialog_run`) — defer with `g_idle_add`.

**Lua scripting (`script_engine.c` + `script_panel.c`):** scripts run on a **background GThread** (`g_thread_new("lua-script", …)`). The Lua thread must never touch GTK directly — all UI/port effects are marshalled back to the GTK main loop with `g_idle_add` (see `log_idle_cb`, `send_macro_idle`, `done_idle_cb`), guarded by `engine_mutex` and per-handle mutexes. The `gtkterm.*` Lua API is registered here; the main port (0) is observed through the link's `received` signal, connected and disconnected on the main thread. `script_panel.c` is the editor/console UI (GtkSourceView 4). **When adding a `gtkterm.*` function, keep this rule: do the work on the Lua thread, but defer any GTK/transport side effect via `g_idle_add`.** Example scripts live in `scripts/` (use the `/lua-script` skill to scaffold new ones).

**Macros subsystem (four files):**
- `macros.c` — core logic, send, polling.
- `macros_format.c` — printf-style format arguments (`%d`, `%s`, `%#ListName`, length modifiers) with per-type range truncation/validation.
- `macros_list.c` — named value lists referenced as `%#ListName`.
- `macro_panel.c` — the button panel UI (argument fields, tab groups, polling toggles, `%&` separators).

Macros and lists are stored in a **standalone `.ini` file**, separate from the port config; the last-used path is remembered in `.gtktermrc`.

**Config (`term_config.c` + `parsecfg.c`):** the port/window config is `.gtktermrc` under `$XDG_CONFIG_HOME` (migrated from `$HOME`). Profiles are INI sections handled by `parsecfg.c` (old third-party parser). The same file also holds GKeyFile groups `[window]`, `[colors]`, `[layout]`, `[session]` (window state in `term_config.c`; script-editor colors, layout and last script in `script_panel.c`); `term_config.c` strips those groups before calling parsecfg and restores them after (`cfg_strip_kf_sections` / `cfg_restore_kf_sections`). Two mechanisms on one file is fragile (review defects C5, C13); sub-project 3 replaces parsecfg with a single GKeyFile store.

**Self-update (`update.c` + `data/gtkterm-update.sh`):** the running binary knows its own commit because `src/meson.build` generates `git_revision.h` (`GIT_REVISION`) with `vcs_tag()` on every build (with `safe.directory=*` so a root rebuild keeps the real SHA). Install as root with `meson install --no-rebuild`, never `sudo ninja install`: right after a commit, a root `ninja` rewrites `build/src/git_revision.h` as root and the next user build fails with Permission denied. `update_check()` runs `git ls-remote <update_url> HEAD` asynchronously (`g_subprocess`) and compares the remote SHA against `GIT_REVISION` — silent at startup unless a newer revision exists (skippable), always reporting when triggered manually from the About dialog. Accepting launches `data/gtkterm-update.sh` (installed under `PACKAGE_DATA_DIR`, also baked by meson) inside a detected terminal emulator; the script clones/updates into `$XDG_CACHE_HOME/gtkterm/src`, installs build deps, builds, `pkexec`-installs, and restarts the app by PID. The repo URL and install prefix come from `config.update_url`/`config.update_prefix` (`.gtktermrc`, editable in the About dialog; defaults to the GitHub fork and `/usr/local`).

**Other entry points:** `gtkterm.c` (`main`, wiring order; schedules the startup update check via `g_idle_add`), `interface.c` (the large GTK GUI — menus, VTE widget, view switching, shortcuts, About dialog), `link.c` (port state machine, GUdev hotplug), `user_signals.c` (SIGUSR1/2 → open/close port), `cmdline.c` (CLI options incl. `--transport/--host/--tcp-port`), `search.c`, `logging.c`, `files.c`, `i18n.c` (gettext; translations in `po/`).

## Code style

The codebase is **mixed-style** — be deliberate. The dominant style (`link.c`, `interface.c`, `term_config.c`) is **tabs**, **Allman braces** (`{` on its own line), and **no space before `(`** (`if(cond)`, not `if (cond)`). A few files (e.g. `terminal_display.c`) use a different GNU-ish 2-space style. **Match the style of the file you are editing.** A `.clang-format` encoding the dominant style is present, but do NOT bulk-reformat existing files — it would create huge diffs across the mixed styles. Format only new code / new files intentionally.
