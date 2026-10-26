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
`script_engine.c`) against stubs (`tests/stubs/`), application-layer tests linking the whole `gtkterm_app` with their own `GtAppUi` table (`test_session`,
`test_session_manager`, `test_app_config`, `test_cmdline`), and two GTK-free-logic tests of `src/ui/`
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

**Layers (sub-projects 2 to 6b):** three layers; a layer never includes a header of the one below it.
1. **Core** (`gtkterm_core`, GLib/GObject/GIO only, unit-tested): transport (`transport.c`, `serial_termios.c`, `baud.c`, `baudrates.c`), history and rendering
   (`history.c`, `rx_transform.c`, `view_render.c`), `device_match.c`, configuration (`config_file.c`, `profile.c`, `config_store.c`), macros model
   (`macro_set.c`, `macros_format.c`, `macros_list.c`, `macro_file.c`, `macro_poller.c`, `macro_arg.c`, `macro_preview.c`, `macro_override.c`), Lua ports (`rx_queue.c`, `script_port.c`), and the sub-project 6a
   modules: `term_options.c` (`GtTermOptions`), `hex_input.c`, `capture.c`, `file_sender.c` (`GtFileSender`), `rx_logger.c` (`GtRxLogger`), `update_check.c`, `update_versions.c`, `update_fetch.c`,
   `app_ui.c` (`GtAppUi`), `serial_ports.c` (serial device discovery), `port_form.c` (Port dialog validation), `macro_view.c` (macro panel display rules: a list argument that is optional is never
   `GT_MACRO_VIEW_TWO_VALUES`, it stays a drop-down with « (none) »),
   `action_relay.c` (`GtActionRelay`, 6b), `port_activity.c` (`GtPortActivity`: activity state of a port, see below), and the sub-project 6c modules: `profile_keyfile.c` (`GtProfile` ↔ named GKeyFile group, shared by `.gtktermrc`, the macro `.ini` and the
   project), `file_stamp.c` (`GtFileStamp`), `project_file.c` (`GtProjectData` ↔ `.gtkterm` file), `window_state.h` (`GtWindowState`, shared), plus `i18n.c`.
2. **Application** (static library `gtkterm_app`; GLib + gudev + Lua, still no GTK header): `link.c` (`GtkTermLink`), `session.c` (`GtSession`),
   `session_manager.c` (`GtSessionManager`), `script_engine.c` (`GtScriptEngine`), `macros.c` (`GtMacros`), `app_config.c` (store facade, profiles of a
   session, window state, `last_project`), `cmdline.c` (`GtCmdline`, `--project`), `project.c` (`GtProject`, 6c). It reaches the UI only through the `GtAppUi` hook table.
3. **GTK 4 UI** (`src/ui/`, the only code that includes `<gtk/gtk.h>`): views and controllers; they read the state, never copy it.
`grep -rn 'gtk/gtk.h' src/*.c src/*.h` must stay empty; no `extern` UI variable outside its module; the UI never talks to the transport directly.

**Tabs (sub-project 6b): one complete GTKTerm instance per tab.** A `GtSession` (`session.h`) holds all of a tab's state: its link, `GtTermOptions`,
`GtProfile` and profile name, `GtMacros`, `GtScriptEngine`, the tab action group (`gt_session_get_actions()`, a `GSimpleActionGroup`) and the `short-name`
(`gt_session_short_name_for()`: serial device basename, `host:port` for a TCP client, `:port` for a server) and `status-text` properties; id 1, 2, 3… never
reused; `gt_session_is_busy()` = script running or file being sent (`gt_session_set_file_sending()`, held by the page's `file_actions.c`). There is no default
link and no global `GtProfile config` any more: code receives its session or asks `GtSessionManager` (`session_manager.h`: `GListModel` of sessions in tab
order, `active` property; the UI is master of the order and calls `add`/`remove`/`move`, nobody else changes the model). **No static variable may hold
per-tab state**; allowed statics are truly unique: the store and `settings` (`app_config.c`), the message queue (`dialogs.c`), the main thread and the
`run_on_main_sync` request queue (`script_engine.c`), the `GtAppUi` table, the single instances of the modal Port, Terminal and profile dialogs and of the
About / Updates windows, and the application state of `src/ui/app.c` (manager, window, relays, SIGUSR sources). Command-line options apply to the first tab
only; tabs are saved only by a **project** (see « Projects », 6c): without one, start-up opens one free tab.

**UI hooks (`app_ui.h`):** `GtAppUi` = `message` (non-blocking message box on the window, global; NULL → stderr), and, each receiving the `GtSession *`
concerned (the UI finds its page with `gt_session_view_lookup()`; nothing happens when the page is missing or shut down), `status_flash` (temporary status-bar
text), `macros_changed` (rebuild that page's macro panel and shortcuts), `macro_polling_changed`, `profile_applied` (re-apply that session's options,
terminal and colours, reopen its port). Call them through `gt_app_message()` / `gt_app_status_flash(session, ms, fmt, …)` (printf-style). The GTK 4 UI
installs its table in `main()` before parsing the command line; messages raised before the main window is presented are queued and shown, in order, once it
is (`gt_ui_main_window_ready()`). Tests install their own table. Main thread only; a hook never runs a nested main loop.

**Single option state (`GtTermOptions`, `term_options.c`):** one GObject per session holds echo, CR/LF auto, timestamp, ESC clears screen, view mode (`GtViewMode`, nicks
`ascii`/`hex`), hex bytes per line (8, 10, 16, 24 or 32; any other value is refused) and hex index. Properties notify only on change; the `changed` signal fires
after those notifications, once per GObject dispatch (`g_object_set()` of several properties, `gt_term_options_apply_profile()`, a thaw), never without a
change, so the terminal redraws once. Auto-reconnect stays the link's `auto-reconnect` property.
The matching `GtProfile` fields of the session's profile (`echo`, …) are only read by `gt_term_options_apply_profile()` (start-up, profile load, command line) and written by
`gt_term_options_store_profile()` (just before `app_config_save_profile()`): never add another copy. View mode, bytes per line and index are not saved (start:
ASCII, 16, no index).

**GTK 4 application (`src/ui/app.c`, `src/ui/menus.ui`):** `GtkApplication` with id `eu.mulagabriel.gtkterm` and `G_APPLICATION_NON_UNIQUE` (one process
per launch); the desktop file is `eu.mulagabriel.gtkterm.desktop`, so Wayland matches window and launcher; the icon stays `gtkterm`. `main()`
(`src/ui/main.c`): SIGPIPE ignored → `config_file_init()` → gettext → `gt_ui_install_app_hooks()` → `script_engine_setup()` → `GtSessionManager` and the
first `GtSession` → `read_command_line(first, …)` (fills `GtCmdline` and the first session's profile: profile, its macros file, options, validation;
`--help` exits 0 and an unknown option 1, before any window; messages are queued) → `gt_app_new(&cmd, sessions)` → `g_application_run()` with `argv[0]` only.
`startup`: application actions and all accelerators, test hook; `activate`: main window, page of the first session, `app.*` relays, profile applied, window
presented, queued messages shown, port opened, SIGUSR1/SIGUSR2 (open/close the port of **every** session), deferred update check.
**Every action is an `app.*` action** (only application actions are exported on D-Bus). Tab actions live in each session's action group and are exposed as
`app.<name>` by a **relay** (`GtActionRelay`, core: delegates `activate`/`change_state` to the action of the same name in its target group, mirrors `enabled`
and `state`, notifies only on change; without a target or action: disabled, last state kept) retargeted to the active session (`notify::active` of the
manager). The session puts in its group `local-echo`, `crlf-auto`, `timestamp`, `esc-clear-screen`, `hex-index`, `view-mode`, `hex-bytes` (`GPropertyAction`s
on its `GtTermOptions`), `auto-reconnect` (on its link), `clear-screen`, `open-port`, `close-port`, `send-break`, `toggle-dtr`, `toggle-rts`, `detach-macros-template` (enabled only while its macros are linked to a
template); the page adds
`clear-scrollback`, `copy`, `paste`, `select-all`, `find`, `show-hex-bar`, `show-macro-panel`, `show-script-panel` (`GPropertyAction`s on a widget's
`visible`), `script-layout`, `send-file`, `save-raw`, `save-ascii`, `log-start`, `log-paused` (boolean), `log-stop`, `log-clear` (list: `tab_action_names` in
`app.c`). Application actions without relay read the active session when activated: `quit`, `about`, `updates`, `port`, `terminal`, `macros`,
`load-profile`, `save-profile`, `delete-profile`, `load-macros`, `save-macros`, `new-tab`, `close-tab` (disabled with one tab), `next-tab`, `previous-tab`
(circular), the project actions `project-new`, `project-open`, `project-save`, `project-save-as`, `project-rename`, `project-close` (the last two disabled without an open project), `active-tab`
(state `i`: index of the shown
tab; `SetState(i)` shows tab `i`, out of range ignored). Menus are a `GMenuModel` (`menus.ui`, in the
resource `src/ui/gtkterm.gresource.xml`) whose items name existing actions (order File, Edit, View, Port, Terminal, Macros, Log, Help; the **project is the document**, so
File = New project, Open project…, Save project, Save project as…, Rename project…, Close project | Send RAW file…, Save RAW file…, Save ASCII file… | Quit; Edit = Copy, Paste, Select All, Find |
Clear screen, Clear scrollback; Port = New port… (`new-tab`), Close tab | Settings… (`port`), Open Port, Close Port, Autoreconnect | Send break, Toggle DTR, Toggle RTS |
Load/Save/Delete profile…; Terminal = Settings… (`terminal`), Local echo, CR LF auto, ESC clear screen, Timestamp; Macros = Configure macros…, Load macros file…, Save macros file…, Detach from template;
« configuration » is « profile » in labels); accelerators go through
`gtk_application_set_accels_for_action` (Ctrl+Shift+L/K/R/C/V/F/A/B and F5–F8 in `app_actions.c`, Ctrl+Shift+S in `port_dialog.c`, Ctrl+Shift+Q/T/W/O (O = Open project) and
Ctrl+Shift+Page Down/Up in `app.c` (no shortcut for Save project: Ctrl+Shift+S is taken); no plain Ctrl shortcut, so Ctrl+letter, Ctrl+B and Ctrl+Page included, reaches the port); menu labels carry no
mnemonic, so Alt+letter reaches the terminal too. **One shutdown path:** `app.quit` calls `gtk_window_close()` on the main window, exactly like the title bar;
`close-request` (`src/ui/app.c`; with a modified project it first asks Save / Don't save / Cancel without a nested loop) saves `[window]` and the active page's script panel state once (a single
write of `.gtktermrc`), shuts every page down
(`gt_session_view_shutdown()`: file send and log, macro panel, macro shortcuts, status bar detached, page actions removed), closes the message queue, then
`app_stop_services()` stops the update check, the SIGUSR1/2 sources, the notebook handlers, the `open-failed` handlers and the relays, then every session in tab
order (`gt_session_shutdown()`: Lua engine, macros (poller), actions disabled, link closed, signal `closed`); `shutdown` runs the same stop (idempotent) and
frees the rest. Always page first, then session.

**UI modules (`src/ui/`):** `main_window.c` (`GtkApplicationWindow`: menu bar + `GtkNotebook` named `tabs`, scrollable, tab bar shown only from two pages,
pages reorderable; default size 1000 × 600; state through `GtWindowState`), `session_view.c` (the page of a session, `GtkBox` named `session-view`: search
bar, vertical `GtkPaned` [horizontal `GtkPaned` (terminal | macro panel) / script panel], hex send bar, status bar; slots `macro-panel`, `script-panel`,
`hex-bar`, `status-bar`, `search-bar`; `gt_session_view_new(app, session, state, layout_from)`, `gt_session_view_lookup(session)`,
`gt_session_view_shutdown()`), `tab_label.c` (`tab-label` box: activity indicator `tab-activity` left of `tab-name` bound to `short-name`, tooltip
bound to `status-text`, CSS classes `lost` (no style of its own) while the link is lost or reconnecting, `dead` (name greyed) and `unread` (name in bold)
from the session's `GtPortActivity`, `tab-close` button → `gt_app_close_session()`, insensitive with one tab), `activity_icon.c` (activity indicator:
fixed 18 × 16 `GtkOverlay`, accessible role image named after the state, one opaque indicator among dot, spinner, Zzz and skull, CSS class
`activity-<nick>`; animated by timers (66 ms spinner, 800 ms Zzz), not CSS, only while that state is shown and the widget mapped),
`terminal_view.c` (VTE; renders the link history through `view_render.c`; keyboard → `gt_link_send(GT_SEND_KEYBOARD)`; `gt_terminal_view_new(link, options,
session)`), `app_actions.c`, `status_bar.c`
(label bound to the link's `status-text`, temporary message without a stack, DTR/RTS/CTS/CD/DSR/RI LEDs polled only while a serial link is open,
activity indicator `status-activity` shown only with a single tab: `gt_status_bar_set_activity_visible()` from `update_tab_state()` of `app.c`),
`search_bar.c`, `hex_send_bar.c` (parsing by `hex_input.c`), `dialogs.c` (`gt_ui_message`, `gt_ui_confirm`, `gt_ui_choose`, `gt_ui_open_file`, `gt_ui_save_file`),
`port_dialog.c`, `terminal_dialog.c` + `terminal_dialog.ui`, `profile_dialogs.c`, `file_actions.c` + `file_send_window.c`, `macro_shortcuts.c`, `macro_panel.c`,
`macro_editor.c` (with `macro_row.c`, GLib/GIO rows, and `macro_accel.c`, key logic; both unit-tested without a display), `macro_arg_widget.c` (one argument field, shared by the macro panel and the
editor's argument table: entry for `%s`/numeric, `GtkDropDown` for `%#List`, checked with the argument's limits, `error` class and the reason in the tooltip on an invalid entry;
`gt_macro_arg_widget_new_full()` / `gt_macro_arg_widget_set_optional()`: an optional list gets a first entry « (none) » (value `""`, initial choice when the default is empty), an optional
`%s`/number the placeholder « (omitted) », an empty optional field is never red), `project_rename.c` (« Rename project… » window), `macros_save.c` (`gt_ui_macros_save()`: macros file save with the
Overwrite / Reload / Cancel question), `script_panel.c`, `update_window.c`, `about.c`, `test_hooks.c`.
**Script panel shown at run time** (`session_view.c`): when it becomes visible at the bottom, it gets at least max(a third of the page height, 150 px) if the vertical separator was never
set or leaves it less than 150 px (`SCRIPT_PANEL_MIN_HEIGHT`); a saved or dragged position that leaves it 150 px or more is kept as is.
Every page module keeps its state on the page or the session (`g_object_set_data_full`), never in a static.
**Tabs in `app.c`:** the `GtkNotebook` is master of the order and of the shown page: `switch-page` (connected *after* the default handler, so the current
page is already the new one) → `gt_session_manager_set_active()`, `page-reordered` → `gt_session_manager_move()`; `notify::active` → relays retargeted,
window title bound to the session's `status-text` (« GTKTerm - <status> »), `app.active-tab` state, keyboard focus to the page's terminal (deferred to an
idle). `app.new-tab` opens the Port dialog in creation mode (`gt_port_dialog_show_new()`, pre-filled with a copy of the active profile; the dialog closes
before calling its callback; Cancel creates nothing); OK → `gt_app_add_session(profile, name)`: new session, macros file loaded (non-interactive) from the
profile's `macros_file`, else the active tab's current macros path, else none (never the default file by itself), page inserted right of the active one
(separators and panel visibility copied from the active page, script panel layout too, empty editor), shown, profile applied, port opened.
`gt_app_close_session()` (`app.close-tab`, tab close button) asks `gt_ui_confirm` only when the session is busy, then: page shut down, session shut down,
page removed (the neighbour, right then left, becomes active), session removed. An `open-failed` box is prefixed with the tab's short name when there are
several tabs. The Port, Terminal, profile dialogs and « Configure Macros » act on the session active when they open and close without applying anything on
its `closed` signal.
**Dialogs never block:** transient modal `GtkWindow`s (the message, confirmation and choice boxes `gt_ui_message`/`gt_ui_confirm`/`gt_ui_choose` are
windows built by GTKTerm, CSS classes `dialog` and `message`, not `GtkAlertDialog`: spec §15), `GtkFileDialog`, `GtkFontDialogButton`, `GtkColorDialogButton`,
all with callbacks; a callback that finds its dialog destroyed does nothing (weak pointer or
`GCancellable`). `gt_ui_choose(…, labels, default_choice, destructive_choice, cb, data)` takes the index of the default button (focus, Enter) and of a
`destructive-action` one (-1: none, the default button is then `suggested-action`): a conflict question puts Cancel by default and Overwrite as destructive,
so Enter never overwrites someone else's work. No `GtkDialog`, no `gtk_dialog_run`, no nested main loop anywhere; message text is never parsed as Pango markup. After a dialog, popover or the
search bar closes, keyboard focus goes back to the terminal.

**Compiler-enforced GTK version:** `src/ui/` is built with `-DGDK_VERSION_MIN_REQUIRED=GDK_VERSION_4_14 -DGDK_VERSION_MAX_ALLOWED=GDK_VERSION_4_14
-Werror=deprecated-declarations`: a deprecated GTK API or one newer than 4.14 breaks the build, even on a newer machine. VTE and GtkSourceView calls must exist
in 0.76 and 5.12 (check the "Since" of each call; the Ubuntu 24.04 build is the reference).

**Receive path (device → screen):** `transport.c` reads bytes → `on_data` callback → **`link.c`** appends them to the `GtHistory` (RX record with its wall time)
and emits `received (GBytes *data, gint64 wall_time_us)` → subscribers: `src/ui/terminal_view.c` (renders through `view_render.c`, then `vte_terminal_feed()`),
the RX logger (`GtRxLogger`: own `GtRxTransform`, no echo, transform fed even while paused so resuming never cuts a line; a write error stops it and reports
through the `message` hook), the file sender (`gt_file_sender_feed_rx()`, wait-for-char), the Lua engine (port 0, connected on the main thread).

**Send path:** everything goes through the link (spec `docs/superpowers/specs/2026-10-26-file-emission-design.md`). `gt_link_send(link, data, len, GtSendOrigin)`
(keyboard and paste, macro click/shortcut/polling, hex bar, Lua `send_macro`) is **all accepted or nothing**: `len` once accepted (written at once, the rest in the
transport's **send queue**, written as the port takes it), 0 when the link is not open (nothing is queued: never a replay after a reconnection), -1 and `errno`
otherwise (`ENOBUFS`: queue full, `GT_TRANSPORT_QUEUE_MAX` = 1 MiB). `gt_link_try_send()` is one non-blocking write for senders that regulate themselves (file
send through `file_actions.c`, Lua `send`/`send_hex` on port 0): -1/`EAGAIN` while the queue is not empty, so order is kept. `gt_link_get_queued()`. Only bytes
actually written are appended to the history (TX record, wall time of the write, origin carried by the transport's tag) and emitted by `sent` — from the
transport's `on_sent`, possibly later from the fd watch; echo is rendered from that, so it stays in place across view switches. A refused paste or keystroke
flashes a hint to use « Send RAW file… » (the terminal view holds a weak pointer to its session); the hex bar says « %d byte(s) sent! » only when everything was
accepted, else « Nothing sent: … » and keeps the entry. The queue dies with the transport (close, loss, server client gone or replaced). The history keeps **raw** bytes: CR/LF
auto and timestamps are a read-time transform (`GtRxTransform`), so toggling an option, the echo or the view emits `changed` on `GtTermOptions` and redraws
the terminal once from the history (`gt_terminal_view_redraw()`). « Clear scrollback » only clears VTE's scrollback: a later redraw brings the content back
from the history, which stays the reference for « Save RAW/ASCII » (`capture.c`; each capture remembers its own last file).

**Transport (`transport.c` / `transport.h`):** one instance per opened port (`gt_transport_open(params, callbacks, user_data, GError **)`), three modes —
`TRANSPORT_SERIAL`, `TRANSPORT_TCP_CLIENT`, `TRANSPORT_TCP_SERVER`; no UI calls, errors via `GError`, `LOST` reported (deferred to an idle) instead of
reconnecting. Serial specifics: termios in `serial_termios.c`, baud-rate tables in `baud.c`/`baudrates.c`. Privileged TCP ports use `CAP_NET_BIND_SERVICE`.
**Send queue:** `gt_transport_send_queued(t, data, len, tag)` (all or nothing: empty queue → direct write, the rest queued as `GBytes` chunks with their tag;
over the room left under `GT_TRANSPORT_QUEUE_MAX` → -1/`ENOBUFS`, nothing written), a `G_IO_OUT` watch present **only while the queue is not empty**, callback
`on_sent(data, len, tag, user_data)` per chunk actually written (optional: `TransportCallbacks` initialised with two fields stay valid; called after the queue is
updated, so a handler may send again), `gt_transport_try_send()` (one write; -1/`EAGAIN` while the queue is not empty or an RS485 cycle runs;
`gt_transport_send()` = it with a NULL tag, used by Lua ports 1–15), `gt_transport_get_queued()`. The queue is dropped at close, at loss and, for a server,
when the client leaves or is replaced; a write error from the watch is a loss (serial, TCP client) or `CLIENT_GONE` (server). **RS485 never blocks:** one
`TIOCMBIS` per burst, « before » delay by `g_timeout`, queue written by the watch, then `TIOCOUTQ` polled every 5 ms and the UART shift register
(`TIOCSERGETLSR`/`TIOCSER_TEMT`) when the driver gives it (bounded: time of the driver's pending bytes + 1 s), « after » delay by `g_timeout`, one `TIOCMBIC`; bytes queued while draining or during the « after »
delay resume writing without toggling RTS; no `usleep`/`tcdrain` on the main thread, no RTS toggle on `EAGAIN`. In RS485, `try_send` queues what fits for one
cycle when idle (file send: one RTS cycle per chunk). Lua ports 1–15 cannot be opened in RS485.

**Link (`link.c` / `link.h`):** single state machine for the main port — `CLOSED`, `CONNECTING`, `OPEN`, `LISTENING`, `LOST`, `RECONNECTING`; properties
`state`, `status-text`, `auto-reconnect`, `reconnect-interval-ms`; signals `open-failed` (user-requested open only → error message box), `received`, `sent`,
`history-cleared`. Link loss never opens a dialog: the status bar shows « — lost » / « — reconnecting… »; silent retry every 2 s plus an immediate retry when
udev reports the device back. With `auto-reconnect`, a failed user-requested open (serial or TCP client) shows `open-failed` once, then goes to `RECONNECTING`
too (device plugged in / server started after GTKTerm). The udev monitor lives here and matches by device number, then path/links (`device_match.c`).
**Re-entrancy (see `link.h`):** handlers of `received`, `sent` and `notify::state`/`notify::status-text` may run inside the transport's fd watch (read or
send queue) or inside `gt_link_send`/`gt_link_try_send`; they must not call `gt_link_open`/`gt_link_close` — defer with `g_idle_add`. A `sent` handler may send
again (its bytes go behind those waiting).

**Port activity (`port_activity.c` + `src/ui/activity_icon.c`):** `GtPortActivity` (core, no `link.h`: it gets dated events, `GtPortActivityLink`
converted by the session) computes one state per tab, by priority `closed`, `dead`, `reconnecting`, `lost`, `connecting` (`CONNECTING` or
`LISTENING`), `receiving` (last byte < 300 ms ago), `idle`, `waiting` (open, nothing received yet), `active`; plus `unread` (byte received while the
tab is not viewed, cleared by `set_viewed(TRUE)` from `notify::active` in `app.c`). A port that has received since entering `OPEN` never goes `dead`
while open (it goes `idle`); every (re)connection and `gt_session_open_port()` (`gt_port_activity_reset()` before `gt_link_open()`) starts from zero;
the lost delay counts from entering {`LOST`, `RECONNECTING`}, not reset between them. Properties notify only on change (a continuous stream stays
`receiving` without a notification per packet); one GLib timeout armed on the nearest deadline, re-armed only when an event brings it closer;
monotonic clock only; `use-timer` false lets tests drive time by `tick`. Delays: `GtProfile` `idle_timeout_s` (default 10) and `no_data_timeout_s`
(default 60), 0 – 3600 s, 0 disables the state, keys `activity_idle_timeout` / `activity_no_data_timeout`, edited in Port › Advanced Configuration
Options › Port activity (spin buttons `port-idle-timeout`, `port-no-data-timeout`, carried by `GtPortForm`), applied by `gt_session_apply_profile()`
and `gt_session_open_port()`; closing the link (`gt_session_shutdown()` included) gives `closed`, which has no deadline, so no timeout.

**Lua scripting (`script_engine.c` + `src/ui/script_panel.c`, sub-project 5):** one `GtScriptEngine` per session (`gt_script_engine_new(link, macros)`:
port 0 is that session's link, `send_macro`/`get_macros`/`get_lists` use its `GtMacros`); engines are independent (thread, ports 1–15, queues, log, stop
flag), the contract below holds for each, two scripts of two tabs run in parallel and a serial port held by one is refused to the other (transport lock).
Scripts run on a **background GThread** (`g_thread_new("lua-script", …)`). The
Lua thread never touches GTK, a transport or the link: it only uses the receive queues (`GtRxQueue`, `rx_queue.c`: locked, 1 MiB, oldest bytes dropped,
overflow reported once per port and run) and the atomic port state, and runs everything else on the main thread through `run_on_main_sync` (direct call when
invoked from the main thread, recorded by `script_engine_setup`). Lua ports 1–15 are `GtScriptPort` (`script_port.c`, core: one `GtTransport` + one
`GtRxQueue` each, opened/closed/written on the main thread; the port table is read and written by the Lua thread only); port 0 is the link, observed through
its `received` signal (connected/disconnected on the main thread); its queue is never closed, so `wait_for(0, …)` returns only data or `"timeout"`.
`send`/`send_hex` write everything or fail with a reason (partial writes and `EAGAIN` retried on the Lua thread with a 2 ms pause, timeout; port 0 through
`gt_link_try_send`, so a `send` after a big `send_macro` waits for the send queue to empty); failures return
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
reports `GtScriptEnd` (finished / error / stopped). `send_macro` uses `gt_macros_send_quiet()` (no « sent! » message): true once accepted (queued), else
false plus `"not connected"`, `"link lost"`, `"send queue full"` or a system reason. **Closing a tab or GTKTerm:** `gt_session_shutdown()` stops the session's engine
after its page (file send and log) is shut down: `gt_script_engine_shutdown()` does not run the main loop — the main thread itself executes the pending
`run_on_main_sync` requests (of every engine: one queue, one main thread) until the thread ends,
and abandons the thread after 3 s (the UI is frozen meanwhile). Closing the window first sets the stop flag of every engine, then shuts them down one by one.
When a tab is closed, an abandoned engine is never freed and GTKTerm goes on: its thread and its Lua ports 1–15 stay alive until the long C call that blocks
it returns (it then sees the stop, closes its ports and ends). `src/ui/script_panel.c` is the editor/console UI
(GtkSourceView 5; Open, Save, Run, Stop, Colors… buttons; top/bottom or left/right layout from the context menu; console capped at 10 000 lines; colours
dialog as a modal `GtkWindow`; the GtkSourceView colour scheme is written to `$XDG_DATA_HOME/gtkterm/styles/`, added to the style search path). **When adding
a `gtkterm.*` function: work on the Lua thread, run any GTK/transport/link side effect on the main thread via `run_on_main_sync`, never hold a queue lock
across it, register it through the guarded wrappers (`api_guard`), and make any blocking wait check `stop_requested` at least every 20 ms.** Example scripts
live in `scripts/` (use the `/lua-script` skill to scaffold new ones).

**Macros subsystem (sub-projects 4, 6a, 6b, macro arguments and linked templates, specs `docs/superpowers/specs/2026-10-14-arguments-macros-design.md`,
`docs/superpowers/specs/2026-10-15-modeles-macros-design.md`):**
- `macro_set.c` (core) — `GtMacroSet`: ordered macros with a **stable in-memory id** (never reused), label/shortcut/action/tab, stored arguments, polling mode +
  period (1 ms – 24 h), its own named lists (`GtMacroLists`, `macros_list.c`); `gt_macro_expand()` is the only expansion (format then escapes; `n_args < 0` =
  stored arguments); `gt_macro_set_apply_edits()` applies the edit window rows by id, so arguments and polling follow the macro, never the position. `GtMacro.args` are the **default values** of the
  arguments (saved with the macro); `gt_macro_set_copy()` copies a set (new ids), `gt_macro_set_clone()` keeps the ids (simulations only, never installed),
  `gt_macro_set_group_by_tab()` (stable sort of the macros by `tab`, groups in order of first appearance, the empty tab (« General ») a group like another, ids kept, TRUE if the order changed;
  called **at read time** by `gt_macro_set_from_key_file()` and the old-format reader, so every `.ini`, template, cached base or embedded project set is grouped as soon as it is read; a file is
  never rewritten by the grouping alone, so an old mixed project does not open « modified »), `gt_macro_set_clear_args()` drops a macro's defaults, `gt_macro_set_same_structure()` compares two
  sets ignoring ids, defaults and polling (same count and,
  rank by rank, label, shortcut, action, tab, limits (NULL ≡ all empty), optional flags and prefixes (NULL ≡ all empty); same lists, names, entries and order). **Optional arguments:**
  `GtMacro.arg_optional` (`"1"` or `""`) and `arg_prefix` (raw action text, escapes untreated), vectors parallel to the arguments, NULL or exactly one item per format argument, never entirely
  empty (then NULL); unlike limits they are **kept** rank by rank when the argument count changes (truncated or padded with `""`; the editor reports a non-empty prefix lost by truncation in
  `macro-editor-error`, « Prefix of argument %d removed »); `gt_macro_expand()` / `gt_macro_expand_segments()`: an optional argument whose resolved value is **empty** (empty field without default,
  « (none) » of a list, `""` or `nil` from Lua, or beyond the arguments supplied) sends nothing, neither prefix nor value, otherwise the prefix (escapes `\r`, `\n`, `\xHH` treated like the action)
  then the formatted value; `gt_macro_check_args()` skips an empty optional argument; segments: the prefix belongs to the argument's segment, an omitted argument gives a zero-length
  `GT_MACRO_SEG_OMITTED` segment (preview run `GT_PREVIEW_OMITTED`, text « ∅ »); `gt_macro_arg_describe()` adds « optional » and « prefix "…" ». **Limits:** `GtMacro.arg_min`, `arg_max`,
  `arg_charset`, vectors parallel to the
  arguments like `args` (`""` = no constraint, dropped with `args` when the argument count changes, each limit re-validated for the argument of the same rank);
  numbers: min/max values; `%s`: min/max length in **bytes sent** and a charset; `%c` and lists: none (`gt_macro_set_set_limits()`). `gt_macro_expand_segments()`
  gives the same bytes as `gt_macro_expand()` plus segments (text, argument N, escape) for the preview; `gt_macro_check_args()` checks arguments before a send.
- `macro_arg.c` (core) — one argument's rules: charsets (`""` UTF-8, `printable` 0x20–0x7E, `ascii`, `latin1` sent as **one byte per character**,
  `class:<chars and ranges a-b, \ escapes>`; `gt_macro_charset_parse`), `gt_macro_arg_limit_valid` / `_limits_valid` (a limit is a valid value of the type;
  min ≤ max), `gt_macro_arg_encode`, `gt_macro_arg_check` (type, then limits; an empty number counts as 0; verdict + translated reason « above maximum 100 »),
  `gt_macro_arg_describe` (« integer, 0 – 100 (default 50) »), `gt_macro_arg_type_name`.
- `macro_preview.c` (core) — preview of the line sent: `gt_macro_preview_runs()` (runs of text, argument, special: control characters named `CR`, `LF`…,
  else `\xHH`) and `gt_macro_preview_hex()` (uppercase hex, truncated with « … »).
- `macro_override.c` (core) — **project values** of a tab linked to a template (`GtMacroOverrides`, list of `GtMacroOverride`): a macro is named by its
  **label** and its **occurrence** (rank among macros of the same label, 0 first; the empty label is a label), with per-rank default values (any string, `""`
  included) and optionally a polling setting (mode + period). `gt_macro_overrides_diff(base, effective)` (NULL unless same structure; equality rule: `args`
  NULL ≡ all `""`), `gt_macro_overrides_apply(o, base, &dropped)` (copy of base, new ids; drops, one per argument or polling: macro gone, rank beyond its
  arguments, value refused by `gt_macro_arg_check` with the template's type and limits (lists not checked; `""` always accepted for an optional argument), polling of a non-sendable macro or period out of
  [`GT_MACRO_PERIOD_MIN_MS`, `GT_MACRO_PERIOD_MAX_MS`], never clamped), `gt_macro_overrides_merge_template(base, before, after)` (new template after an edit:
  a default unchanged by the edit, of a rank of the same type, takes back the base's value if `after`'s limits accept it (`""` of an optional argument always), so a project value never leaks into
  the template; polling taken from base), key file groups `<prefix>override:K` (tolerant read, one entry per label + occurrence, the last read wins).
  A project value equal to the template's is no project value any more (difference semantics, assumed). Usage values and running state are never in it.
- `macros_format.c` (core) — printf-style arguments (`%d`, `%s`, `%#ListName`, length modifiers); the lists are passed in (`%#x` is a list only if list `x`
  exists).
- `macro_file.c` (core) — the standalone `.ini`: v2 format (`[gtkterm-macros] version=2`, `[macro:N]`, `[list:N]`, values escaped by GKeyFile); old
  `[macros]`/`[lists]` files are read and rewritten as v2 on the first save. Before overwriting any present, non-empty file that is not a readable v2 (old
  format, bad syntax, unknown version, foreign file), the save makes a one-time copy `<file>.avant-migration`; if that copy already exists with a different
  content, the save is refused and nothing is overwritten. A file with groups but none of `[gtkterm-macros]`/`[macros]`/`[lists]` is refused on load
  (`G_KEY_FILE_ERROR_GROUP_NOT_FOUND`); an empty file gives an empty set. A non-interactive load failure (profile) still makes that file the current path
  (empty set, warning shown once the window is up), so later saves never fall back to the default file. The set ↔ GKeyFile functions take a group prefix (`gt_macro_set_to_key_file` /
  `gt_macro_set_from_key_file`; empty prefix = the `.ini`, `tab:N:` = a project tab). Keys `arg_optional` and `arg_prefix` (GKeyFile lists, written only when one value is not empty; versions
  unchanged, an older GTKTerm ignores them and sends the action without the prefixes; tolerant read: wrong length truncated or padded with a warning naming file, group and key, an `arg_optional`
  value other than `""`/`"1"` becomes `""` with a warning), `arg_min`, `arg_max`, `arg_charset` (lists, written only when one value is not empty; version stays 2, an older GTKTerm ignores them; an
  invalid
  limit is dropped with a warning naming file, group and key). An invalid escape in `args`, `displays` or `values` keeps the raw value with a warning; an empty list is read
  back; the refusal when a different `<file>.avant-migration` exists says how to cure it (rename or delete it).
- `macro_poller.c` (core) — polling scheduler: one GLib timeout per running macro, sends through a callback (`gt_macros_send`), state callback to the
  panel. The running state is never saved.
- `macros.c` (application layer, no GTK) — `GtMacros`, one per session (`gt_macros_new(link, owner)`: current set, file path, poller on the session's link,
  so a polling keeps sending on its tab's port whatever tab is shown and stops when its tab closes); `gt_macros_send(m, id, args, n)` is the **only** send
  path for panel click, shortcut and polling (it flashes « Macro "%s" sent! » through `status_flash(owner)` once `gt_link_send` accepted it; a full send queue
  or an error flashes « Macro "%s" not sent: … », at most once per 3 s per macro; a closed port says nothing; a polling tick is **skipped** while the
  link's send queue is not empty, so a polling faster than the line never piles up), `gt_macros_send_quiet()` the same for Lua
  without the message; UI effects through the `GtAppUi` hooks with the owner session (`macros_changed`, `macro_polling_changed`, `message`). **Arguments (6c):** the panel no longer saves what the user
  types or picks. `GtMacros` keeps **usage values** (id → raw arguments, born empty, never saved; `gt_macros_set_usage_arg`, `gt_macros_reset_usage` (id 0: all), `gt_macros_has_usage`,
  `gt_macros_get_usage` (raw, read by the panel)); **an empty field means the default value**: `gt_macros_get_args` resolves each argument (non-empty usage
  value, else default, else `""`; NULL without defaults nor usable usage values, so `V%d` sends `V%d`) and every send (click, shortcut, polling, Lua
  `send_macro`) uses it; replacing or loading a set clears them. **Refused send:** `gt_macros_check_args` checks the resolved (or Lua) arguments first; an
  invalid one sends nothing, flashes « Macro "%s" not sent: argument %d: %s » (3 s, not repeated for the same macro within 3 s; polling goes on), Lua
  gets `false, "invalid argument"`. **Embedded mode**
  (`gt_macros_set_embedded`, a project tab): no file, no write; Configure Macros and polling change the set in memory (`gt_macros_set_edited_func` → the session's `edited`). **Identity guard:** a
  `GtMacros` with a file keeps a `GtFileStamp` (taken before the read); `gt_macros_save_file(m, path, interactive, force)` returns `GT_MACROS_CONFLICT` when the file changed on disk since the last
  read or write, or, without a current file nor a path, when the default file exists and is not empty (never read by this tab: a silent write would lose it) (non-interactive: warning, nothing
  written); `src/ui/macros_save.c` then asks Overwrite / Reload (re-read, usage values cleared; without a current file, the default file) / Cancel, Cancel by default.
  `gt_macros_reload()` refuses a file that no longer exists (message hook « The macros file %s no longer exists. », FALSE, set, path and link unchanged: never the empty set that
  `gt_macros_load_file()` still installs for a missing file at start-up or profile load).
  `gt_macros_apply_edits()` applies the editor's rows and clears the usage values of a macro whose default values changed (they would hide the new default);
  `gt_macros_commit_edits()` (= apply with a copy of the lists + `gt_macro_poller_sync` + `gt_macros_changed`) is the editor's single apply path.
  **Linked template** (project tabs only): a `GtMacros` can be linked, in fields distinct from `path`/`stamp`: `template_path` (absolute), `base` (the template's
  set as last read, `gt_macros_get_template_set()`), `template_stamp` (taken before the read) and a `generation` counter (`gt_macros_get_generation()`) bumped
  by every change of the effective set or of the link (replace_set, apply, `gt_macros_edited`, link, detach, refresh): an asynchronous answer records it when
  asking and applies nothing if it changed. **Invariant:** the current set (the **effective** set: panel, editor, sends, polling, Lua) has the **same structure**
  as the base; project values are not stored, they are `gt_macros_get_overrides()` = diff(base, effective) (a `gt_macros_replace_set` of another structure
  drops the link with a `g_warning`). `gt_macros_link_template(m, path)` (interactive read, a missing file refused: « Cannot load macros file: … », nothing
  changes; else base = read set, effective = copy, usage and polling reset); `gt_macros_link_from_project(m, path, cached_base, overrides, &dropped,
  &error_text)` (project open: template re-read, `UPDATED` if its text differs from the cached base, else `UNCHANGED`; unreadable: base = cached copy,
  `MISSING`, still linked; effective = apply); `gt_macros_detach_template()` (forgets template, base and stamp; effective set, polling, usage kept; flash
  « Detached from macros template %s », `edited`); `gt_macros_copy_from(dst, src)` (new tab: set and link copied); `gt_macros_classify_edits()` (simulates the
  rows on a clone: `NONE`, `DEFAULTS` (only defaults changed), `STRUCTURE`; the lists passed are never taken); `gt_macros_save_template_edits(m, rows, n,
  lists, force)` (new base = merge_template, `CONFLICT` unless forced when the template changed on disk (a `MISSING` one that now exists too), `FAILED` +
  « Cannot save macros template: … », `SAVED`: stamp retaken, base replaced, commit, flash « Macros template %s saved »); `gt_macros_refresh_template()` /
  `gt_macros_reload_template()` (re-read, current project values reapplied: in place when the structure is unchanged (defaults and polling copied rank by
  rank, running polling kept), else `gt_macros_replace_set`). `gt_macros_set_embedded(TRUE)` keeps the link, `gt_macros_set_embedded(FALSE)` detaches;
  `gt_macros_load_file()` forgets the link as soon as it replaces the set (an interactive failure changes nothing); `gt_macros_reload` and
  `gt_macros_save_file` ignore the template. The link changing calls `gt_macros_edited`: the session updates `detach-macros-template` in its `edited` callback.
- `src/ui/macro_shortcuts.c` — one `GtkShortcutController` per page (`GTK_SHORTCUT_SCOPE_LOCAL`, capture phase, on the page), rebuilt on `macros_changed`: only
  the shown tab's macros answer; the shortcut of a sendable macro consumes the key (M43), otherwise the key goes on to the terminal.
- `src/ui/macro_panel.c` — the button panel, a view of the set and the poller: tabs are grouped toggle buttons in a `GtkFlowBox` + a `GtkStack` of pages;
  right click on a button → polling popover (mode, period applied on Enter and on close; invalid period = `error` style + message in the popover); right click
  on the tab bar → per-tab check boxes (hidden tabs are not saved; hiding one stops its pollings); argument fields (`macro_arg_widget.c`) write usage values to `GtMacros`, never a file; an empty
  field shows the default as placeholder and sends it, a field out of limits is red, its tooltip describes the argument (and the reason); a
  button's right-click menu has « Reset arguments » (empties the fields); rebuilding never touches
  polling.
- `src/ui/macro_editor.c` — « Configure Macros », non-modal, one instance per session (title « Configure Macros — <short name> »), not attached to the
  application (application accelerators do not apply there, so the capture can see Ctrl+Shift+Q): `GtkColumnView` over a `GListStore` of rows carrying the stable id (no header
  sorting: the order is the user's), Lists page alike; Capture Shortcut refuses a shortcut already taken by a macro or by an application action; OK (or closing
  by the title bar) = `gt_macros_apply_edits()` (`gt_macro_set_apply_edits()` on the current set; a macro whose default values change loses its
  usage values); the window closes without saving if the set is replaced meanwhile. Under the table, the **argument table** `macro-defaults` (a `GtkGrid`, hidden for a macro without argument)
  shows one row per format argument of the selected macro by rank (rebuilt when the action or a list changes): `#`, `Format` (`%.1f`, `%#Mode`), `Type`,
  `Default` (`macro-default-<k>`, built with the argument's Optional and Prefix: empty accepted despite the limits, « (none) » first in an optional list; toggling Optional without changing
  the action rebuilds the table so that cell follows), `Min`/`Max` (`macro-min-<k>`, `macro-max-<k>`, none for `%c` and lists), `Charset` (`%s` only: `GtkDropDown`
  `macro-charset-<k>` UTF-8 / Printable ASCII / ASCII / 8 bits (Latin-1) / Custom class, class entry `macro-class-<k>` shown for Custom class); a vector
  (defaults, min, max, charsets) is kept only when all its fields are valid, a default only if it passes its limits; OK applies them with the row. Below,
  `macro-preview-box`: « Line sent (default values): », `macro-preview` (Pango markup: arguments blue, red when invalid, specials purple) and
  `macro-preview-bytes` (« 50 35 30 3B · 4 bytes »), for a single selected sendable row. Default size 900 × 640. The columns have a fixed starting width, never narrower
  than their title (`natural_w` in the test dump), and only the last one takes the remaining room. **Grouping and filters:** the rows are kept grouped by tab (Lists page: by list) with
  `gt_macro_rows_group(store, GtRowKeyFunc)` with `gt_macro_row_tab_key` / `gt_macro_list_row_key` (`macro_row.c`, same stable rule as the core; Move Up/Down through
  `gt_macro_rows_move_grouped()`), at construction, after Add / Add Entry, after a Reload and, for a Tab / List cell
  edit, once the edit ends (deferred, not on each keystroke); the selection follows the moved rows. Move Up/Down is all or nothing and never lets a row leave its group. A `GtkDropDown`
  `macro-tab-filter` (« Tab filter », label « Tab: ») above the Macros table and `lists-filter` (« List filter », « List: ») above the Lists table offer « All » then each key in group order (the
  empty tab shown « General »); the `GtkColumnView` shows a `GtkFilterListModel` over the store and one function maps the selected (filtered) positions to store positions (Delete, Move, shortcuts,
  argument table, preview); the filter is not saved and falls back to « All » when its key disappears. Add under a filter creates the macro in that tab (« General » → empty) after the last
  selected row of the group, else at its end; without a filter the new macro takes the tab of the last selected row (Add Entry: the list of the selected entry, else « NewList »). **Target file
  line:** a box `macro-source` (label `macro-source-label`, button `macro-reload`) replaces `macro-template-info` at the top: « Template: <full path> » (linked tab), « File: <path> » (free tab
  with a current file), « Embedded in the project » (unlinked project tab, button hidden), « No macros file » (free tab without file, button hidden); updated on the session's `edited` and
  `gt_macro_editor_macros_changed()`. **Reload** (`macro-reload`): if the rows differ from the set (`gt_macros_classify_edits()` ≠ `NONE` or lists differ) asks « Discard your changes and reload
  the macros file? » Reload (destructive) / Cancel (default) (answer with a `GWeakRef` on the window and the generation; the button is insensitive while a question is pending), then
  `gt_macros_reload_template()` (linked) or `gt_macros_reload()`; **the editor stays open**: a `reloading` flag makes `gt_macro_editor_macros_changed()` rebuild rows (grouped), argument table,
  preview and filters (a filter is kept while its key exists) and take the new `editor->set`; a **missing file is refused** (« The macros file %s no longer exists. ») and an unreadable one too («
  Cannot reload the macros file %s. »), shown in `macro-editor-error`, nothing changes (the editor tests the file itself before `gt_macros_reload()`, which refuses it too, so only the label
  speaks, without a message box); outside the flag a set replaced elsewhere still closes the window. **Optional / Prefix:** two argument-table
  columns after Charset, `macro-optional-<k>` (`GtkCheckButton`) and `macro-prefix-<k>` (`GtkEntry`, insensitive while unchecked); checking splits the prefix off the action with
  `gt_macro_action_split_prefix()` (text before the specifier of argument k back to the first space, space included, never crossing the previous specifier, a whole escape sequence or the start of
  the action; nothing to take → empty prefix, action unchanged; `%%` becomes `%`), unchecking `gt_macro_action_join_prefix()` puts it back before the specifier; both vectors follow the typing in
  the Action cell (« Prefix of argument %d removed » in `macro-editor-error` while a non-empty prefix is beyond the arguments, cleared when another row is shown); the preview shows prefix and value in blue and an omitted argument as a greyed « ∅ ». The page layout is, top to bottom: `macro-source`, page switcher, filter row, table, the
  two button rows **right under the table**, separator `macro-settings-separator` + 12 px margin (hidden with the argument table when the macro has no argument), `macro-defaults`,
  `macro-preview-box`. **Linked tab:** OK applies nothing before the answer:
  `gt_macros_classify_edits()` → `NONE` closes, `DEFAULTS` asks « These macros come from the template %s » Keep in this project (default) / Save to template /
  Cancel, `STRUCTURE` asks Save to template / Detach and keep a copy / Cancel (default); no destructive button. Keep = commit; Detach = detach then commit;
  Save to template keeps the editor open until `SAVED` (`FAILED`: stays; `CONFLICT`: Overwrite (destructive, forced save with the rows re-read) / Reload /
  Cancel (default)); Cancel or Escape leaves it open. The answer holds a `GWeakRef` on the window and the generation; while a question is pending OK does
  nothing and closing is refused (the title bar is OK, so it asks too). After a `SAVED`, `gt_app_refresh_template_siblings()` (`app.c`) refreshes every other
  tab of the window linked to the same path and closes that tab's open editor (`gt_macro_editor_close()`) without applying anything.

Macros and lists live in that standalone `.ini`, separate from the port config; each free tab can use a different file; the last-used path is remembered in the
active profile of that tab (`config_remember_macros_file(session)`, no-op in a project tab). A project tab has no `.ini` of its own (it may be linked to a
template `.ini`): see « Projects ».

**Config (`config_store.c` + `profile.c` + `app_config.c`):** `.gtktermrc` under `$XDG_CONFIG_HOME` (migrated from `$HOME`) is read and written only by
`GtConfigStore` (GKeyFile): `[general]` (update settings), `[profile:NAME]` (one `GtProfile` each), and the owner groups `[window]` (`app_config.c`, as a
`GtWindowState`: `width`, `height` (default 1000 × 600), `maximized`, `paned_position`, `v_paned_position`, macro/script panel and hex bar visibility; `x`/`y`
are no longer read and are removed on the next save; aberrant values are ignored; size and maximization are the window's, separators and visibilities the
page's: the first page reads them, a new page copies the active page's, closing saves the active page's), `[colors]`, `[layout]`, `[session]`
(`src/ui/script_panel.c`; `[colors]` is shared by every page — « Colors… » saves then refreshes all of them through `gt_app_foreach_view()` —, each of its nine
keys written only when the user chose that colour, otherwise removed so the editor follows the terminal colours or its default palette; `[layout]` and
`[session] script_path` are read by the first page and saved from the active page at close; a panel without a script path never removes `script_path`),
which access their groups through
`gt_config_store_get_key_file()` then
`config_save_store()`. `app_config.c` (application layer) is the store facade (`config_file_init`, `config_get_store`, `config_reload_store`,
`config_save_store`) and the profile operations of the UI on a session (its own `GtProfile` and profile name; « Load/Save/Delete profile » act on the
active tab): `app_config_load_profile(session, …)` (load into that session + its macros file + validation + `profile_applied` hook; the macros file goes
through `app_config_load_profile_macros(session)`, the single rule shared by start-up, Load profile and the free tab of Close project: the profile's
`macros_file`, else the current file, else the default file; nothing in a project tab; a start-up without a `default` profile loads no profile, hence no macros file),
`app_config_save_profile(session, …)`, `app_config_delete_profile()`, `app_config_list_profiles()`, `app_config_get_last_project()` / `app_config_set_last_project()` (`[general] last_project`,
same re-read / write order). Profile names go through
`gt_profile_name_validate()` (trimmed, 1–64 characters, no `[`, `]` or control character; `window`, `colors`, `layout`, `session` refused whatever the case;
a case-insensitive duplicate asks for overwrite confirmation). Starting without a `default` profile
while others exist uses the defaults silently (`default` is created only by an explicit save). Old parsecfg files are migrated in memory on load, with a
one-time backup `.gtktermrc.avant-migration`. Only explicit saves write a whole profile; loading a macros file writes only the active profile's `macros_file`
key (and only if the path changed and the profile still exists), the Updates window only its `[general]` settings, « Skip this version » only
`update_skipped_version` (a tag name). Several GTKTerm instances share the file: every UI write calls `config_reload_store()` first (re-read if the file's identity
changed — `gt_config_store_reload_if_changed()`), applies its single change, then saves; keep that order when adding a writer. The window state is written
once, by `close-request`. An unreadable file makes the store read-only: nothing is ever written over it.

**Projects (sub-project 6c; spec `docs/superpowers/specs/2026-10-10-refonte-projets-design.md`):** a project is **one `.gtkterm` file holding a whole workstation**: every tab with its port and
terminal settings, its page state, **a copy** of its macro set and **a copy** of its Lua script text (never a path to a `.ini` or a `.lua`). Format (`project_file.c`, GKeyFile, hand-editable):
`[gtkterm-project]` (`version=1`, `active=N`, optional `name=`: the project name, trimmed, empty = none, made valid UTF-8 with a warning; an older GTKTerm ignores it; part of the « modified »
snapshot), one `[tab:N]` per tab (N = 0, 1, … in tab order: the keys of a `[profile:…]` through `profile_keyfile.c`, plus page state and `script`), the macros as
`[tab:N:macro:M]` and `[tab:N:list:K]` (keys of the v2 macro format); a tab linked to a macros template also has `macros_template` (absolute path; empty or
relative → not linked, warning), its groups `macro`/`list` hold the **base** (template as of the last project save) and its project values are
`[tab:N:override:K]` (`label`, `occurrence` if ≠ 0, `arg_<k>` from 1, `polling` + `polling_period_ms` together; an invalid value or group ignored with a
warning naming file, group and key; version stays 1, an older GTKTerm opens the base copy). `GtProjectTab` has `macros_template` (NULL: not linked) and
`overrides` (never NULL). `gt_project_data_load()` reads and validates the **whole** file before anything else (unknown version, missing header, no tab, tab
without a valid profile → refused with a message, nothing closed; an out-of-range profile is corrected with a warning like a loaded profile and the project is then « modified »; a value that is not
valid UTF-8 (a hand-edited script) is made valid, invalid bytes replaced, with a warning); `gt_project_data_save()` writes atomically (`g_file_set_contents`); `gt_project_data_snapshot()` is the text
compared for « modified » (same serialisation, **without** page state, active tab, usage values or the base of a linked tab: a template re-imported at open
never makes the project modified).
- **Free tabs and project tabs, never mixed:** with no project open, tabs work as in 6b (profiles in `.gtktermrc`, macros in a `.ini`). Once a project is open or saved, **every** tab of the window
  belongs to it (`gt_session_set_in_project()`, property `in-project`): its macros are **embedded** (`GtMacros` without a path, Configure Macros and polling edit the set in memory; Load macros file…
  asks « Use this macros file in the project »: Copy into the project (default; the tab is no longer linked) / Link as template (`gt_macros_link_template`) /
  Cancel, the answer ignored if the session closed, left the project or its generation changed; Save macros file… exports to a chosen `.ini` without linking
  the tab; Macros › Detach from template turns a linked tab into a copy; polling set from the panel on a linked tab is a project value, without question), its script text is the project's (`script-text` property, held by the session; Open… loads a file into the
  editor, Save exports to a `.lua`, `[session] script_path` is neither read nor written), a new tab gets a copy of the active tab's macros (and of its template link, `gt_macros_copy_from`) and an empty script. Load/Save/Delete profile stay available
  (loading changes the tab, so the project becomes modified; saving writes the profile to `.gtktermrc`).
- **`GtProject`** (`project.c`, application layer, one per window held by `src/ui/app.c`): properties `open`, `path` (NULL: no file, shown « Untitled project »), `name`, `modified`;
`gt_project_set_name()` (trimmed, empty → NULL, schedules the « modified » recompute), `gt_project_get_stored_name()` (NULL: none), `gt_project_get_name()` (stored name, else file name without
`.gtkterm`, else « Untitled project »; empty when closed), `gt_project_move_file()` (rename of the file: path and `GtFileStamp` follow, content not rewritten until the next save);
`gt_project_capture()`
  builds a `GtProjectData` from the sessions (profile through `gt_term_options_store_profile`, set copy (linked tab: base copy, template path and project values
  by diff), script text, page state through the UI callback `GtProjectPageFunc`);
  `gt_project_begin/end/save`; `gt_project_save()` on a closed project fails (« No project is open »). **« Modified »** is not tracked event by event: the session's `edited` signal (macros changed or
  polling set, `GtTermOptions` `changed`, `profile_applied`, script text, tab added / closed / moved) schedules a recompute at `G_PRIORITY_DEFAULT_IDLE`, grouped, at most once per 300 ms, comparing
  snapshots; closing recomputes at once. Title: « <name> • - <short name> » when modified, « <name> - <short name> » otherwise; no project: the 6b title.
- **Actions (`app.c`):** `project-new` (one tab, profile `default` or the defaults, no macros or script, no file), `project-open` (Ctrl+Shift+O; replaces **all** tabs), `project-save` (without a file
  = Save as), `project-rename` (File menu after Save project as…, no shortcut, disabled without a project; `src/ui/project_rename.c`: modal GTKTerm window `rename-project`, entry `project-name`
  (1–128 characters, no control character, else `error` style and OK insensitive), check box `project-rename-file` shown only when the project has a file, checked by default: the target is the
  name with `/`, `\` and control characters replaced by `_` plus `.gtkterm` in the same folder; an existing target or a failed `g_rename()` is shown in `rename-error`, the window stays open and
  nothing changes; success updates the path, the stamp and `last_project`; Save as proposes `<name>.gtkterm` for a named project without file), `project-save-as` (when `.gtkterm` is appended to
  the typed name and that file exists, `gt_ui_confirm` asks first: the chooser only confirmed the typed name), `project-close` (back to
  one free tab with the `default` profile, its macros loaded like at start-up by `app_config_load_profile_macros()`, `last_project` cleared). Each destructive operation first asks `gt_ui_confirm` if a
  tab is busy, then Save / Don't save / Cancel (`gt_ui_choose`) if the project is modified; a project request arriving while another one is pending (a project dialog already open) is ignored. Open
  reads and validates the file before the questions, and reads it again if it was rewritten meanwhile (Save of the project being reopened); the tabs are then recreated in order, each with its profile,
  page, macros and script, the `active` tab shown (a linked tab goes through `gt_macros_link_from_project()` before its page; once the page exists, `UPDATED` →
  5 s flash « Macros template %s updated », with drops « …: %u project values no longer apply » (ngettext; same flash for `UNCHANGED` with drops), `MISSING`
  → warning message « Cannot read the macros template %s: %s. The copy saved in the project is used. »; without drops the reference data take the live
  diff, so a hand-written file does not show « modified », with drops the project is modified and saving removes them); the recreated tabs keep the `lock_port` (-L) of the tab active before. Save as from free tabs copies each tab's in-memory macro set and editor text;
  if it fails the project stays open as « Untitled project » (modified, embedded macros), like New project, and Save as can be retried. Quitting with a modified project asks the same question; SIGTERM
  or session end asks nothing and saves nothing. Reload after a conflict replaces the tabs too, so it asks the same « Close all tabs? » confirmation first when a tab is busy.
- **`last_project`:** `[general] last_project` (absolute path) is written by a successful open or save and cleared by Close project; New project leaves it alone. Start-up: `--project FILE` opens that
  project; otherwise, if the command line gives neither a profile (`-c`) nor port settings, the last project is reopened; a missing or refused file → message queued until the window is up, then the 6b
  start-up; a `last_project` that no longer exists while its folder does (`G_FILE_ERROR_NOENT`) is cleared and the message says so, any other refusal (permissions, folder missing because a medium is
  unmounted, damaged file) keeps it. `--project` with `-c` or port settings is refused at parsing (message, exit 1; `-L` is allowed).
- **`GtFileStamp`** (`file_stamp.c`, core): `exists`, device, inode, size, mtime (s + ns) of a file as read or written; `gt_file_stamp_take/equal/changed`. Used by the project, by `GtMacros` (its
  `.ini`) and by the store. Before writing, if the file changed on disk since, the UI asks **Overwrite / Reload / Cancel** (project: `app.c`; macros: `macros_save.c`; Cancel is the default button,
  Overwrite `destructive-action`); an unknown identity (project begun without a stamp) is never a conflict; a non-interactive save never asks and never overwrites (warning, failure). This ends the
  silent « last save wins » between tabs and instances sharing a `.ini`.
- **Stays outside the project:** `[window]` (window size), the global `[colors]` / `[layout]`, `[general]`, the named profiles of `.gtktermrc`.

**Self-update (`update_versions.c` + `update_fetch.c` + `update_check.c` + `src/ui/update_window.c` + `data/gtkterm-update.sh`; spec `docs/superpowers/specs/2026-10-12-maj-par-tags-design.md`):**
updates install **published versions** = git tags `vX.Y.Z` (numeric order, never the date), never a branch head. The build knows `GIT_REVISION` and `GIT_DESCRIBE`
(`git describe --tags --match 'v[0-9]*' --abbrev=7`, both by `vcs_tag()` in `src/meson.build`, with `safe.directory=*`; the installed version is `GIT_DESCRIBE`, else the meson version, else 0.0.0;
`vX.Y.Z-N-g…` is newer than `vX.Y.Z` and older than the next one). Install as root with `meson install --no-rebuild`, never `sudo ninja install` (a root `ninja` rewrites `build/src/git_revision.h`
and the next user build fails). `update_versions.c` (core, `gt_version_*`, `gt_news_*`, `gt_release_verdict`) is pure logic; `gt_releases_fetch_async()` (`update_fetch.c`, core) keeps a bare cache
`$XDG_CACHE_HOME/gtkterm/versions.git` (`clone --bare --filter=tree:0`, then `remote set-url` + `fetch --prune --force +refs/tags/*:refs/tags/*`; a URL change refreshes tags from the new source
only), lists the tags and reads `NEWS` of the newest tag (`git -c protocol.ext.allow=never`, `GIT_TERMINAL_PROMPT=0`, `LC_ALL=C`, cancellable; an empty URL or one starting with `-` is refused).
`NEWS` (French, root of the repo): `## vX.Y.Z …` opens a section, `- ` item, `! ` warning, two leading spaces continue the previous item, a `# …` level-1 heading closes the current section (the
`# Historique` text stays unshown), a section without a published tag is never shown. The « Updates » window (Help › Check for updates…, refreshes when opened; Install and the version list
insensitive during a refresh) lists the versions, shows the notes between the installed and the chosen version (warnings once, at the top, each prefixed with its version, never repeated in their section; rollback: « You are leaving these versions: »), « Update to /
Roll back to / Reinstall vX » (confirmation when the notes hold a warning, or when the project is modified or a tab is busy: the script closes GTKTerm with SIGTERM and restarts it), « Skip
this version » (newest version only; `[general] update_skipped_version` = tag; the old `update_skipped_revision` is neither read
nor written) and closes after a successful launch; the start-up check is silent unless a newer, non-skipped version exists, and then opens that window. Launching uses an argument list
(`g_spawn_async`, never a shell string) in a detected terminal: `gtkterm-update.sh <url> <prefix> <pid> <tag>` (tag re-validated). The script takes a lock `$XDG_CACHE_HOME/gtkterm/update.lock`
(with its PID), checks the tag out in `$XDG_CACHE_HOME/gtkterm/src`, re-executes the script **of that version** (so it picks its own dependencies: GTK 3 for v1.3.x, GTK 4 for v2.x), then asks
**one single authorization** (`pkexec`) for everything privileged: dependencies, the build **dropped to the user** with `runuser` (`rm -rf build`, `meson setup -Dtests=false`, `ninja`),
`meson install --no-rebuild`, and the cleanup of files of the registry `installed-files.txt` missing from the new install log (under the prefix, no `..`, files and links only, not owned by a
package); it then restarts the app by PID. `GTKTERM_UPDATE_SUDO` / `GTKTERM_UPDATE_DEPS=0` are for tests only (an agent never runs the script otherwise); without a registry, a fallback removes the
pre-GTK 4 `<prefix>/share/applications/gtkterm.desktop` unless a package owns it or the installed version provides it. Tests: the sh TAP tests `update_script` (local upstream repo, throwaway
prefix, fake `pkexec` counting authorizations) and `publier_version`, `test_update_versions`, `test_update_fetch`. The repo URL and install prefix come from
`settings.update_url`/`settings.update_prefix` (`[general]` of `.gtktermrc`; defaults to the GitHub fork and `/usr/local`). Release: `tools/publier-version.sh vX.Y.Z` (clean tree, `meson.build`
version, `NEWS` section; annotated tag, no push).

**GUI test harness (`tests/gui/`, see `tests/README.md`):** under broadway GTK 4 there is no AT-SPI and VTE does not expose its text, so the 73 scenarios drive
GTKTerm through three bricks: (1) **D-Bus actions** — `org.gtk.Actions` (`Activate`, `SetState`, `Describe`) on the private session bus; start-up is detected
when the name `eu.mulagabriel.gtkterm.test.p<PID>` appears; (2) the **test hook** (`src/ui/test_hooks.c`, compiled unless `-Dtest_hooks=false`, active only
when `GTKTERM_TEST_HOOKS=1`, which also switches the application id to `eu.mulagabriel.gtkterm.test.p<PID>` without `NON_UNIQUE`): actions `app.test-dump`
(every widget of the visible windows and popovers: type, accessible role, label, widget name, CSS classes `css`, flags, geometry), `app.test-terminal-text`,
`app.test-terminal-props` (both on the active tab), `app.test-activate(s)`, `app.test-set-text((ss))`, `app.test-sessions` (state `aa{sv}`, one dict per tab in
order: `index`, `id`, `name`, `state`, `profile`, `macros_file`, `active`, `busy`, `in_project`, `script`, `activity` (state nick), `unread`, `template` (linked macros template path, empty if
none)), `app.test-project` (state `a{sv}`: `open`, `path`, `modified`, `name` (displayed), `stored_name` (empty without a stored name)); the dump also gives
`natural_w` (natural width measured by `gtk_widget_measure`, for mapped widgets), and when not empty `placeholder` (`GtkEntry`), `tooltip` and `markup` (a `GtkLabel` using Pango markup); (3) a **fake broadway browser** (`tests/gui/broadway.py`, `gtk4-broadwayd`'s protocol) for real gestures:
typing in the terminal, shortcuts, menus by clicks, file choosers by Ctrl+L, context menus (a click waits for the widget's geometry to be stable), tab
drags (`drag_in_surface`). Widgets a test must reach carry a stable `gtk_widget_set_name()` and an accessible name: name them when adding UI; `find`/`find1`
skip unmapped widgets by default, so a stable name designates the shown tab's widget. Tabs in `gtktest.py`: `Env.new_pty()` (extra `Pty`: `port`, `read`,
`drain`, `write`, closed with the `Env`), `Env.sessions()` / `active_session()` (read `test-sessions`), `Env.project()` (reads `test-project`), `Env.project_path(name)` (a file in the scenario's
private HOME), `open_tab(env, port)` (real Port dialog),
`save_project_as(env, path)` / `open_project(env, path)` (real file choosers). A scenario
whose failure comes with an internal assertion of `gtk4-broadwayd` in its log (a GTK defect, seen with GTK 4.24) is reported SKIP (77), not FAIL.
Each scenario runs isolated (`run-gui-test.sh`: own broadway display, `dbus-run-session` on a private bus without service directories,
`GDK_DEBUG=no-portals`, private HOME/XDG_*, `guard.c` preloaded to refuse real serial ports and the user's real configuration (and, under
`GTKTERM_GUARD_FAKE_MODEM`, to simulate `TIOCMGET` on a pty), pty pairs only, fake `git`: `bin/git` serves `tags`, `news` and `delay` files, set by `Env.fake_git(tags=…, news=…, delay=…)`, with
`local_describe()` for the build's `GIT_DESCRIBE`).

**Other entry points:** `src/ui/main.c` (`main`, wiring order), `src/ui/app.c` (`GtkApplication`, relays, tabs, actions, shutdown), `cmdline.c` (CLI
options incl. `--transport/--host/--tcp-port`, `-T serial`, `none` for
`--parity`/`--flow`), `link.c` (port state machine, GUdev hotplug), `i18n.c` (gettext; translations in `po/`: msgids in English, French in `po/fr.po`; every
file with translatable strings, `.ui` files included, is listed in `po/POTFILES.in`; regenerate with `ninja -C build gtkterm-pot gtkterm-update-po`).

## Code style

The codebase is **mixed-style** — be deliberate. The dominant style (`link.c`, `app_config.c`, every file of `src/ui/`) is **tabs**, **Allman braces** (`{` on
its own line), and **no space before `(`** (`if(cond)`, not `if (cond)`). A few files use another style (`macros_format.c`, `macros_list.c`: GNU-ish 2
spaces; `script_engine.c`: 4 spaces). **Match the style of the file you are editing.** A `.clang-format` encoding the dominant style is present, but do NOT
bulk-reformat existing files — it would create huge diffs across the mixed styles. Format only new code / new files intentionally.

GTK 4 rules for `src/ui/`: no deprecated API and nothing newer than GTK 4.14 / VTE 0.76 / GtkSourceView 5.12; no `GtkDialog`, `gtk_dialog_run` or nested main
loop; new actions are `app.*` actions (stateful ones as `GPropertyAction` on the real state, never a copy; a per-tab action goes in the session's group
and its name in `tab_action_names` of `app.c`); no static variable holds per-tab state; every user-visible string goes through `_()` with an
English msgid (and its translation in `po/fr.po`); widgets reached by GUI tests get a stable name.
