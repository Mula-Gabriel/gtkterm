# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

GTKTerm is a GTK 4 serial-port terminal emulator for Linux (this is a fork with substantial macro, TCP-transport, and Lua-scripting additions). Written in C,
built with Meson/Ninja. Comments and commit messages are in French.

## Build / Run

```sh
meson setup build        # once (build/ already exists here; re-run only after meson.build changes)
ninja -C build           # build — the common edit/compile loop
./build/src/ui/gtkterm   # run — by hand only: an agent never opens a window on the real (Wayland) screen, it uses the gui suite
meson test -C build      # unit + integration tests (GLib GTest), Lua examples syntax, desktop file check
meson test -C build --suite gui   # GTK 4 GUI suite (private gtk4-broadwayd + D-Bus, never the real screen; never two GUI suites at once)
sudo meson install -C build --no-rebuild   # system-wide install after `ninja -C build` (+ gtk-update-icon-cache)
```

A PostToolUse hook (`.claude/settings.json`) runs `ninja -C build` automatically after edits to `src/*.c|*.h` (including `src/ui/`), so compile errors surface
immediately.

**Tests:** `meson test -C build` runs GLib GTest unit tests (`tests/`) against `gtkterm_core`, a static library of the GTK-free modules (see `core_sources` in
`src/meson.build`) linked by both the app and the tests, plus integration tests that compile real application-layer modules (`link.c`, `macros.c`,
`app_config.c`, `cmdline.c`, `script_engine.c`) against stubs (`tests/stubs/`) or their own `GtAppUi` table, and two GTK-free-logic tests of `src/ui/`
(`test_macro_row`, `test_macro_accel`: the latter links GTK 4 but never initialises a display); plus `luac -p` on
`scripts/*.lua` and `desktop-file-validate`. Meson builds test executables with plain `ninja` too; configure with `-Dtests=false` (`meson_options.txt`) to skip
the suite — the self-update script does. Cases tied to a known bug from `docs/superpowers/reviews/2026-10-06-revue-code.md` are marked with its ID and
reported as `# TODO` without failing the suite (list them with `grep '# TODO' build/meson-logs/testlog.txt`; `meson test -v` only shows `EXPECTEDFAIL`);
a marked case that starts passing fails, so the marker gets removed with the fix. See `tests/README.md` (sanitizers, valgrind, how to add a test, GUI
harness). There is no remote CI: tests run locally.

Dependencies: `gtk4` ≥ 4.14, `vte-2.91-gtk4` ≥ 0.76, `gtksourceview-5` ≥ 5.12 (Ubuntu 24.04's versions: the minimum holds at build and at run time),
`gudev-1.0`, `lua5.4`, `glib-2.0`/`gio-2.0` ≥ 2.70; no libadwaita, no GTK 3. The GUI suite also needs `gtk4-broadwayd`, `dbus-run-session` and `python3-gi`.
`jq` is **not** installed — shell scripting here uses `python3` for JSON.

## Architecture — layers and the data flow that spans files

**Layers (sub-projects 2 to 6a):** three layers; a layer never includes a header of the one below it.
1. **Core** (`gtkterm_core`, GLib/GObject/GIO only, unit-tested): transport (`transport.c`, `serial_termios.c`, `baud.c`, `baudrates.c`), history and rendering
   (`history.c`, `rx_transform.c`, `view_render.c`), `device_match.c`, configuration (`config_file.c`, `profile.c`, `config_store.c`), macros model
   (`macro_set.c`, `macros_format.c`, `macros_list.c`, `macro_file.c`, `macro_poller.c`), Lua ports (`rx_queue.c`, `script_port.c`), and the sub-project 6a
   modules: `term_options.c` (`GtTermOptions`), `hex_input.c`, `capture.c`, `file_sender.c` (`GtFileSender`), `rx_logger.c` (`GtRxLogger`), `update_check.c`,
   `app_ui.c` (`GtAppUi`), `serial_ports.c` (serial device discovery), `port_form.c` (Port dialog validation), `macro_view.c` (macro panel display rules),
   plus `i18n.c`.
2. **Application** (static library `gtkterm_app`; GLib + gudev + Lua, still no GTK header): `link.c` (`GtkTermLink`), `script_engine.c`, `macros.c` (macro
   state), `app_config.c` (store facade, running profile, profiles, window state), `cmdline.c` (`GtCmdline`). It reaches the UI only through the `GtAppUi`
   hook table.
3. **GTK 4 UI** (`src/ui/`, the only code that includes `<gtk/gtk.h>`): views and controllers; they read the state, never copy it.
`grep -rn 'gtk/gtk.h' src/*.c src/*.h` must stay empty; no `extern` UI variable outside its module; the UI never talks to the transport directly.

**UI hooks (`app_ui.h`):** `GtAppUi` = `message` (non-blocking message box; NULL → stderr), `status_flash` (temporary status-bar text), `macros_changed` (rebuild the
macro panel and shortcuts), `macro_polling_changed`, `profile_applied` (re-apply options, terminal and colours, reopen the port). Call them through
`gt_app_message()` / `gt_app_status_flash()` (printf-style). The GTK 4 UI installs its table in `main()` before parsing the command line; messages raised
before the main window is presented are queued and shown, in order, once it is (`gt_ui_main_window_ready()`). Tests install their own table. Main thread only;
a hook never runs a nested main loop.

**Single option state (`GtTermOptions`, `term_options.c`):** one GObject holds echo, CR/LF auto, timestamp, ESC clears screen, view mode (`GtViewMode`, nicks
`ascii`/`hex`), hex bytes per line (8, 10, 16, 24 or 32; any other value is refused) and hex index. Properties notify only on change; the `changed` signal fires
after those notifications, once per GObject dispatch (`g_object_set()` of several properties, `gt_term_options_apply_profile()`, a thaw), never without a
change, so the terminal redraws once. Auto-reconnect stays the link's `auto-reconnect` property.
The matching `GtProfile` fields (`config.echo`, …) are only read by `gt_term_options_apply_profile()` (start-up, profile load, command line) and written by
`gt_term_options_store_profile()` (just before `app_config_save_profile()`): never add another copy. View mode, bytes per line and index are not saved (start:
ASCII, 16, no index).

**GTK 4 application (`src/ui/app.c`, `src/ui/menus.ui`):** `GtkApplication` with id `eu.mulagabriel.gtkterm` and `G_APPLICATION_NON_UNIQUE` (one instance per
port); the desktop file is `eu.mulagabriel.gtkterm.desktop`, so Wayland matches window and launcher; the icon stays `gtkterm`. `main()` (`src/ui/main.c`): SIGPIPE ignored → `config_file_init()` →
gettext → `gt_ui_install_app_hooks()` → default link →
`gt_profile_init_defaults(&config)` → `macros_init()` → `read_command_line()` (fills `GtCmdline`: profile, options, validation; `--help` exits 0 and an
unknown option 1, before any window; messages are queued) → `gt_app_new()` → `g_application_run()` with `argv[0]` only. `startup`: `GtTermOptions`, actions
and accelerators, test hook; `activate`: main window, profile applied, window presented, queued messages shown, port opened, SIGUSR1/SIGUSR2 (open/close the
port), deferred update check. **Every action is an `app.*` action** (only application actions are
exported on D-Bus; in 6b they will target the active tab): stateful ones are `GPropertyAction`s on `GtTermOptions` (`local-echo`, `crlf-auto`, `timestamp`,
`esc-clear-screen`, `view-mode`, `hex-bytes`, `hex-index`), on the link (`auto-reconnect`) or on a widget's `visible` (`show-hex-bar`, `show-macro-panel`,
`show-script-panel`); `log-paused` is a boolean action. Menus are a `GMenuModel` (`menus.ui`, in the resource `src/ui/gtkterm.gresource.xml`) whose items
name existing actions; accelerators go through `gtk_application_set_accels_for_action` (Ctrl+Shift+L/K/R/Q/C/V/F/A/S/B, F5–F8, each set by the module that owns the action; no plain Ctrl shortcut, so
Ctrl+letter, Ctrl+B included, reaches the port); menu labels carry no mnemonic, so Alt+letter reaches the terminal too. **One shutdown path:** `app.quit`
calls `gtk_window_close()` on the main window, exactly like the title bar; `close-request` (`src/ui/app.c`) saves the script panel state and `[window]` once
(a single write of `.gtktermrc`), removes the actions that keep widgets alive, detaches the terminal actions and the status bar, closes the message queue, then
`app_stop_services()` stops in order the file send and the log, the update check, the SIGUSR1/2 sources and the `open-failed` handler, the Lua engine, the macro
panel, the macro shortcuts, the macros (poller), and the link; `shutdown` runs the same stop (idempotent) and frees the rest.

**UI modules (`src/ui/`):** `main_window.c` (`GtkApplicationWindow`: menu bar, search bar, vertical `GtkPaned` [horizontal `GtkPaned` (terminal | macro panel)
/ script panel], hex send bar, status bar; slots `macro-panel`, `script-panel`, `hex-bar`, `status-bar`, `search-bar`; state through `GtWindowState`),
`terminal_view.c` (VTE; renders the link history through `view_render.c`; keyboard → `gt_link_send(GT_SEND_KEYBOARD)`), `app_actions.c`, `status_bar.c`
(label bound to the link's `status-text`, temporary message without a stack, DTR/RTS/CTS/CD/DSR/RI LEDs polled only while a serial link is open),
`search_bar.c`, `hex_send_bar.c` (parsing by `hex_input.c`), `dialogs.c` (`gt_ui_message`, `gt_ui_confirm`, `gt_ui_choose`, `gt_ui_open_file`, `gt_ui_save_file`),
`port_dialog.c`, `terminal_dialog.c` + `terminal_dialog.ui`, `profile_dialogs.c`, `file_actions.c` + `file_send_window.c`, `macro_shortcuts.c`, `macro_panel.c`,
`macro_editor.c` (with `macro_row.c`, GLib/GIO rows, and `macro_accel.c`, key logic; both unit-tested without a display), `script_panel.c`, `update_window.c`, `about.c`, `test_hooks.c`.
**Dialogs never block:** transient modal `GtkWindow`s (the message, confirmation and choice boxes `gt_ui_message`/`gt_ui_confirm`/`gt_ui_choose` are
windows built by GTKTerm, CSS classes `dialog` and `message`, not `GtkAlertDialog`: spec §15), `GtkFileDialog`, `GtkFontDialogButton`, `GtkColorDialogButton`,
all with callbacks; a callback that finds its dialog destroyed does nothing (weak pointer or
`GCancellable`). No `GtkDialog`, no `gtk_dialog_run`, no nested main loop anywhere; message text is never parsed as Pango markup. After a dialog, popover or the
search bar closes, keyboard focus goes back to the terminal.

**Compiler-enforced GTK version:** `src/ui/` is built with `-DGDK_VERSION_MIN_REQUIRED=GDK_VERSION_4_14 -DGDK_VERSION_MAX_ALLOWED=GDK_VERSION_4_14
-Werror=deprecated-declarations`: a deprecated GTK API or one newer than 4.14 breaks the build, even on a newer machine. VTE and GtkSourceView calls must exist
in 0.76 and 5.12 (check the "Since" of each call; the Ubuntu 24.04 build is the reference).

**Receive path (device → screen):** `transport.c` reads bytes → `on_data` callback → **`link.c`** appends them to the `GtHistory` (RX record with its wall time)
and emits `received (GBytes *data, gint64 wall_time_us)` → subscribers: `src/ui/terminal_view.c` (renders through `view_render.c`, then `vte_terminal_feed()`),
the RX logger (`GtRxLogger`: own `GtRxTransform`, no echo, transform fed even while paused so resuming never cuts a line; a write error stops it and reports
through the `message` hook), the file sender (`gt_file_sender_feed_rx()`, wait-for-char), the Lua engine (port 0, connected on the main thread).

**Send path:** everything goes through `gt_link_send(link, data, len, GtSendOrigin)` (keyboard, macro, Lua, hex, file). Only bytes actually written are appended
to the history (TX record) and emitted by `sent`; echo is rendered from that, so it stays in place across view switches. The history keeps **raw** bytes: CR/LF
auto and timestamps are a read-time transform (`GtRxTransform`), so toggling an option, the echo or the view emits `changed` on `GtTermOptions` and redraws
the terminal once from the history (`gt_terminal_view_redraw()`). « Clear scrollback » only clears VTE's scrollback: a later redraw brings the content back
from the history, which stays the reference for « Save RAW/ASCII » (`capture.c`; each capture remembers its own last file).

**Transport (`transport.c` / `transport.h`):** one instance per opened port (`gt_transport_open(params, callbacks, user_data, GError **)`), three modes —
`TRANSPORT_SERIAL`, `TRANSPORT_TCP_CLIENT`, `TRANSPORT_TCP_SERVER`; no UI calls, errors via `GError`, `LOST` reported (deferred to an idle) instead of
reconnecting. Serial specifics: termios in `serial_termios.c`, baud-rate tables in `baud.c`/`baudrates.c`. Privileged TCP ports use `CAP_NET_BIND_SERVICE`.

**Link (`link.c` / `link.h`):** single state machine for the main port — `CLOSED`, `CONNECTING`, `OPEN`, `LISTENING`, `LOST`, `RECONNECTING`; properties
`state`, `status-text`, `auto-reconnect`, `reconnect-interval-ms`; signals `open-failed` (user-requested open only → error message box), `received`, `sent`,
`history-cleared`. Link loss never opens a dialog: the status bar shows « — lost » / « — reconnecting… »; silent retry every 2 s plus an immediate retry when
udev reports the device back. With `auto-reconnect`, a failed user-requested open (serial or TCP client) shows `open-failed` once, then goes to `RECONNECTING`
too (device plugged in / server started after GTKTerm). The udev monitor lives here and matches by device number, then path/links (`device_match.c`).
**Re-entrancy (see `link.h`):** handlers of `received`, `sent` and `notify::state`/`notify::status-text` may run inside the transport's fd watch or inside
`gt_link_send`; they must not call `gt_link_open`/`gt_link_close` — defer with `g_idle_add`.

**Lua scripting (`script_engine.c` + `src/ui/script_panel.c`, sub-project 5):** scripts run on a **background GThread** (`g_thread_new("lua-script", …)`). The
Lua thread never touches GTK, a transport or the link: it only uses the receive queues (`GtRxQueue`, `rx_queue.c`: locked, 1 MiB, oldest bytes dropped,
overflow reported once per port and run) and the atomic port state, and runs everything else on the main thread through `run_on_main_sync` (direct call when
invoked from the main thread, recorded by `script_engine_setup`). Lua ports 1–15 are `GtScriptPort` (`script_port.c`, core: one `GtTransport` + one
`GtRxQueue` each, opened/closed/written on the main thread; the port table is read and written by the Lua thread only); port 0 is the link, observed through
its `received` signal (connected/disconnected on the main thread); its queue is never closed, so `wait_for(0, …)` returns only data or `"timeout"`.
`send`/`send_hex` write everything or fail with a reason (partial writes and `EAGAIN` retried on the Lua thread with a 2 ms pause, timeout); failures return
`false`/`nil` plus a stable English reason; a `tcp_client` connection with a timeout of 0 or a negative one returns `"timeout"` at once; its host name is
resolved on the Lua thread (`getaddrinfo`, not interruptible) and each address is then opened by its numeric form and tried in turn within the timeout (the
transport itself also falls back to the next address on an asynchronous refusal). Closing a serial Lua port (`close` or end of script) first waits on the Lua
thread for pending output (`TIOCOUTQ` polled every 5 ms via `run_on_main_sync`, at most bytes × 10 / baud + 500 ms, then a pause for the UART FIFO / USB
buffers (64 bytes × 10 / baud + 10 ms, even when nothing is pending), all under a single 2 s deadline), never when a stop is requested, because the
transport's close discards it (`TCOFLUSH`). The Lua thread switches only `LC_NUMERIC` to `C` (numbers keep their point), so its messages (`Lua error: …`,
buffer overflow) are translated like the rest. A `tcp_client` failure reason names the host given by the script before the numeric address tried last
(`Cannot connect to localhost (127.0.0.1:4000): …`). Before `lua_close`, the stop flag is set so `__gc` finalizers cannot open ports or wait. « Stop » sets
an atomic flag: every `gtkterm.*` function and `print` is wrapped by `api_guard`, which raises the stop sentinel as soon as it is requested (a stop writes no
« Lua error » line); a count hook (`lua_sethook`, every 1000 instructions, then every instruction once stopping) raises it for code that calls nothing of the
API, so a script's `pcall` cannot keep swallowing it; blocking calls (`sleep` on a `GCond`, `wait_for`, connect wait, send retries) check it at least every
20 ms. Blocking standard-library functions (`os.execute`, `io.read`/`lines` (and its iterator)/`popen`/`open`/`write`, file `read`/`lines`/`write`) are
wrapped by `std_guard`, which sees the stop on entry and gives argument errors back their position and function name. Limits: long C calls (`io.read`,
`os.execute`, a huge `string.rep`, pathological patterns) are only interrupted when they return; a loop calling only the standard library sees the stop every
1000 instructions; `debug.sethook` is removed but `package.loadlib` can bypass that. `os.exit(n)` ends the script (never GTKTerm): 0 → finished, any other
code → error, code not truncated; `print` writes to the console; the script runs inside `lua_pcall` of a C function. Log text is batched in a locked buffer
flushed by a single source at `G_PRIORITY_DEFAULT_IDLE` (20 ms timer, 1 MiB cap with a dropped-bytes line); the done idle flushes it, joins the thread, then
reports `GtScriptEnd` (finished / error / stopped). `send_macro` uses `macros_send_quiet()` (no « sent! » message). **Closing GTKTerm:** the main window's
`close-request` stops the Lua engine right after the file send and the log (after saving the window state): `script_engine_shutdown()` does not run the main loop — the main thread itself
executes the pending `run_on_main_sync` requests until the thread ends, and abandons the thread after 3 s. `src/ui/script_panel.c` is the editor/console UI
(GtkSourceView 5; Open, Save, Run, Stop, Colors… buttons; top/bottom or left/right layout from the context menu; console capped at 10 000 lines; colours
dialog as a modal `GtkWindow`; the GtkSourceView colour scheme is written to `$XDG_DATA_HOME/gtkterm/styles/`, added to the style search path). **When adding
a `gtkterm.*` function: work on the Lua thread, run any GTK/transport/link side effect on the main thread via `run_on_main_sync`, never hold a queue lock
across it, register it through the guarded wrappers (`api_guard`), and make any blocking wait check `stop_requested` at least every 20 ms.** Example scripts
live in `scripts/` (use the `/lua-script` skill to scaffold new ones).

**Macros subsystem (sub-projects 4 and 6a):**
- `macro_set.c` (core) — `GtMacroSet`: ordered macros with a **stable in-memory id** (never reused), label/shortcut/action/tab, stored arguments, polling mode +
  period (1 ms – 24 h), its own named lists (`GtMacroLists`, `macros_list.c`); `gt_macro_expand()` is the only expansion (format then escapes; `n_args < 0` =
  stored arguments); `gt_macro_set_apply_edits()` applies the edit window rows by id, so arguments and polling follow the macro, never the position.
- `macros_format.c` (core) — printf-style arguments (`%d`, `%s`, `%#ListName`, length modifiers); the lists are passed in (`%#x` is a list only if list `x`
  exists).
- `macro_file.c` (core) — the standalone `.ini`: v2 format (`[gtkterm-macros] version=2`, `[macro:N]`, `[list:N]`, values escaped by GKeyFile); old
  `[macros]`/`[lists]` files are read and rewritten as v2 on the first save. Before overwriting any present, non-empty file that is not a readable v2 (old
  format, bad syntax, unknown version, foreign file), the save makes a one-time copy `<file>.avant-migration`; if that copy already exists with a different
  content, the save is refused and nothing is overwritten. A file with groups but none of `[gtkterm-macros]`/`[macros]`/`[lists]` is refused on load
  (`G_KEY_FILE_ERROR_GROUP_NOT_FOUND`); an empty file gives an empty set. A non-interactive load failure (profile) still makes that file the current path
  (empty set, warning shown once the window is up), so later saves never fall back to the default file.
- `macro_poller.c` (core) — polling scheduler: one GLib timeout per running macro, sends through a callback (the app's `macros_send`), state callback to the
  panel. The running state is never saved.
- `macros.c` (application layer, no GTK) — the app's macro state (current set, file path, poller on the default link); `macros_send(id, args, n)` is the
  **only** send path for panel click, shortcut and polling (it flashes « Macro "%s" sent! » through `status_flash`), `macros_send_quiet()` the same for Lua
  without the message; UI effects through the `GtAppUi` hooks (`macros_changed`, `macro_polling_changed`, `message`).
- `src/ui/macro_shortcuts.c` — one `GtkShortcutController` (global scope, capture phase) on the main window, rebuilt on `macros_changed`; the shortcut of a
  sendable macro consumes the key (M43), otherwise the key goes on to the terminal.
- `src/ui/macro_panel.c` — the button panel, a view of the set and the poller: tabs are grouped toggle buttons in a `GtkFlowBox` + a `GtkStack` of pages;
  right click on a button → polling popover (mode, period applied on Enter and on close; invalid period = `error` style + message in the popover); right click
  on the tab bar → per-tab check boxes (hidden tabs are not saved; hiding one stops its pollings); argument fields write to the model, rebuilding never touches
  polling.
- `src/ui/macro_editor.c` — « Configure Macros », non-modal single instance, not attached to the application (application accelerators do not apply
  there, so the capture can see Ctrl+Shift+Q): `GtkColumnView` over a `GListStore` of rows carrying the stable id (no header
  sorting: the order is the user's), Lists page alike; Capture Shortcut refuses a shortcut already taken by a macro or by an application action; OK (or closing
  by the title bar) = `gt_macro_set_apply_edits()`; the window closes without saving if the set is replaced meanwhile.

Macros and lists live in that standalone `.ini`, separate from the port config; the last-used path is remembered in the active profile of `.gtktermrc`.

**Config (`config_store.c` + `profile.c` + `app_config.c`):** `.gtktermrc` under `$XDG_CONFIG_HOME` (migrated from `$HOME`) is read and written only by
`GtConfigStore` (GKeyFile): `[general]` (update settings), `[profile:NAME]` (one `GtProfile` each), and the owner groups `[window]` (`app_config.c`, as a
`GtWindowState`: `width`, `height`, `maximized`, `paned_position`, `v_paned_position`, macro/script panel and hex bar visibility; `x`/`y` are no longer read
and are removed on the next save; aberrant values are ignored), `[colors]`, `[layout]`, `[session]` (`src/ui/script_panel.c`; each of the nine `[colors]` keys is
written only when the user chose that colour, otherwise it is removed and the editor follows the terminal colours or its default palette), which access their groups through
`gt_config_store_get_key_file()` then
`config_save_store()`. `app_config.c` (application layer) is the store facade (`config_file_init`, `config_get_store`, `config_reload_store`,
`config_save_store`), the running profile (the global `GtProfile config`; command-line options apply to it only) and the profile operations of the UI:
`app_config_load_profile()` (load + validation + `profile_applied` hook), `app_config_save_profile()`, `app_config_delete_profile()`,
`app_config_list_profiles()`. Profile names go through `gt_profile_name_validate()` (trimmed, 1–64 characters, no `[`, `]` or control character; `window`,
`colors`, `layout`, `session` refused whatever the case; a case-insensitive duplicate asks for overwrite confirmation). Starting without a `default` profile
while others exist uses the defaults silently (`default` is created only by an explicit save). Old parsecfg files are migrated in memory on load, with a
one-time backup `.gtktermrc.avant-migration`. Only explicit saves write a whole profile; loading a macros file writes only the active profile's `macros_file`
key (and only if the path changed and the profile still exists), the Updates window only its `[general]` settings, « Skip this version » only
`update_skipped_revision`. Several GTKTerm instances share the file: every UI write calls `config_reload_store()` first (re-read if the file's identity
changed — `gt_config_store_reload_if_changed()`), applies its single change, then saves; keep that order when adding a writer. The window state is written
once, by `close-request`. An unreadable file makes the store read-only: nothing is ever written over it.

**Self-update (`update_check.c` + `src/ui/update_window.c` + `data/gtkterm-update.sh`):** the running binary knows its own commit because `src/meson.build`
generates `git_revision.h` (`GIT_REVISION`) with `vcs_tag()` on every build (with `safe.directory=*` so a root rebuild keeps the real SHA). Install as root with
`meson install --no-rebuild`, never `sudo ninja install`: right after a commit, a root `ninja` rewrites `build/src/git_revision.h` as root and the next user
build fails with Permission denied. `gt_update_fetch_remote_async()` runs `git ls-remote -- <url> HEAD` (`GIT_TERMINAL_PROMPT=0`, cancellable, cancelled when
its window closes); a remote revision is offered when it differs from `GIT_REVISION` and, if the cache checkout exists, is not older than it
(`git merge-base --is-ancestor`); a local « unknown » revision is never offered at start-up; « Skip this version » is saved in `[general]
update_skipped_revision`. The start-up check is silent unless a newer revision exists; the « Updates » window (Help › Check for updates…) holds the source URL,
the start-up switch and « Check now », which checks the URL shown. Accepting launches the installed script (`PACKAGE_DATA_DIR`) with an argument list
(`g_spawn_async`, never a shell string) inside a detected terminal emulator; the script clones/updates into `$XDG_CACHE_HOME/gtkterm/src`, relaunches
once on the downloaded copy of itself (so new build dependencies are picked up; it refuses a URL that looks like an option or an `ext::` transport),
installs the GTK 4 build deps of the distribution, builds with `-Dtests=false`,
`pkexec`-installs, removes the pre-GTK 4 `<prefix>/share/applications/gtkterm.desktop` unless a package owns it, and restarts the app by PID. The repo URL and
install prefix come from `settings.update_url`/`settings.update_prefix` (`[general]` of `.gtktermrc`; defaults to the GitHub fork and `/usr/local`).

**GUI test harness (`tests/gui/`, see `tests/README.md`):** under broadway GTK 4 there is no AT-SPI and VTE does not expose its text, so the 47 scenarios drive
GTKTerm through three bricks: (1) **D-Bus actions** — `org.gtk.Actions` (`Activate`, `SetState`, `Describe`) on the private session bus; start-up is detected
when the name `eu.mulagabriel.gtkterm.test.p<PID>` appears; (2) the **test hook** (`src/ui/test_hooks.c`, compiled unless `-Dtest_hooks=false`, active only
when `GTKTERM_TEST_HOOKS=1`, which also switches the application id to `eu.mulagabriel.gtkterm.test.p<PID>` without `NON_UNIQUE`): actions `app.test-dump`
(every widget of the visible windows and popovers: type, accessible role, label, widget name, CSS classes `css`, flags, geometry), `app.test-terminal-text`,
`app.test-terminal-props`, `app.test-activate(s)`, `app.test-set-text((ss))`; (3) a **fake broadway browser** (`tests/gui/broadway.py`, `gtk4-broadwayd`'s protocol) for real gestures:
typing in the terminal, shortcuts, menus by clicks, file choosers by Ctrl+L, context menus (a click waits for the widget's geometry to be stable). Widgets a test must reach carry a stable
`gtk_widget_set_name()` and an accessible name: name them when adding UI. Each scenario runs isolated (`run-gui-test.sh`: own broadway display,
`dbus-run-session` on a private bus without service directories, `GDK_DEBUG=no-portals`, private HOME/XDG_*, `guard.c` preloaded to refuse real serial ports and the
user's real configuration (and, under `GTKTERM_GUARD_FAKE_MODEM`, to simulate `TIOCMGET` on a pty), pty pairs only, fake `git`).

**Other entry points:** `src/ui/main.c` (`main`, wiring order), `src/ui/app.c` (`GtkApplication`, actions, shutdown), `cmdline.c` (CLI options incl. `--transport/--host/--tcp-port`, `-T serial`,
`none` for
`--parity`/`--flow`), `link.c` (port state machine, GUdev hotplug), `i18n.c` (gettext; translations in `po/`: msgids in English, French in `po/fr.po`; every
file with translatable strings, `.ui` files included, is listed in `po/POTFILES.in`; regenerate with `ninja -C build gtkterm-pot gtkterm-update-po`).

## Code style

The codebase is **mixed-style** — be deliberate. The dominant style (`link.c`, `app_config.c`, every file of `src/ui/`) is **tabs**, **Allman braces** (`{` on
its own line), and **no space before `(`** (`if(cond)`, not `if (cond)`). A few files use another style (`macros_format.c`, `macros_list.c`: GNU-ish 2
spaces; `script_engine.c`: 4 spaces). **Match the style of the file you are editing.** A `.clang-format` encoding the dominant style is present, but do NOT
bulk-reformat existing files — it would create huge diffs across the mixed styles. Format only new code / new files intentionally.

GTK 4 rules for `src/ui/`: no deprecated API and nothing newer than GTK 4.14 / VTE 0.76 / GtkSourceView 5.12; no `GtkDialog`, `gtk_dialog_run` or nested main
loop; new actions are `app.*` actions (stateful ones as `GPropertyAction` on the real state, never a copy); every user-visible string goes through `_()` with an
English msgid (and its translation in `po/fr.po`); widgets reached by GUI tests get a stable name.
