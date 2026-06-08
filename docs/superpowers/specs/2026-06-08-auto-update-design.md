# Auto-update for GTKTerm — Design

Date: 2026-06-08

## Goal

Add an in-app update mechanism that:

1. Checks for a new version at startup (toggleable) and prompts the user.
2. Adds a **"Check for updates now"** button to the Help → About dialog.
3. Uses a **configurable update URL** (default: the maintainer's GitHub).
4. Pulls the code from git, installs all build dependencies, builds, and installs.
5. Installs into system paths in a way that is **independent of any original git
   checkout**, so once an update has run the user can delete the original git folder.

## Architecture

Two pieces:

- **Update module** (`src/update.c` / `src/update.h`) — C code: startup version
  check, the confirmation/progress dialogs, spawning the script, reading config.
- **Update script** (`data/gtkterm-update.sh`) — a self-contained shell script that
  does the actual clone/deps/build/install. Installed to `$datadir/gtkterm/`.

The app never builds in-process. It spawns the script. The script clones and builds
in a managed cache directory and installs to the configured prefix, so the system
install never depends on the original folder the user happened to run from.

## Version detection

- At build time, `meson.build` runs `git rev-parse HEAD` and bakes the result into
  `config.h` as `GIT_REVISION`. If the source is not a git checkout (tarball), the
  value falls back to `"unknown"`.
- At startup (only if `update_check_startup` is true), the app runs
  `git ls-remote <update_url> master` asynchronously (via `g_spawn_async_with_pipes`)
  to get the remote HEAD sha, and compares it to `GIT_REVISION`.
- If they differ → non-blocking dialog with **[Update now] [Later] [Skip this
  version]**. "Skip this version" records the remote sha so it is not prompted again
  until a newer one appears.
- If `GIT_REVISION == "unknown"`, the startup auto-check is skipped. The manual
  "Check for updates now" button still works and offers to update anyway.

## Update script (`data/gtkterm-update.sh`)

Runs as the normal (non-root) user. Escalates only the two steps that need root,
each via `pkexec`.

```
PREFIX from arg/env (default /usr/local)
URL    from arg/env (default https://github.com/Mula-Gabriel/gtkterm.git)
WORKDIR=${XDG_CACHE_HOME:-$HOME/.cache}/gtkterm/src

1. If WORKDIR is a git repo: git fetch origin && git reset --hard origin/master
   else: git clone "$URL" "$WORKDIR"
2. Detect distro from /etc/os-release (ID / ID_LIKE) -> pacman | apt | dnf
3. pkexec <pkgmanager install build-deps>            # root step 1
4. meson setup build --prefix="$PREFIX" (reconfigure if needed) && ninja -C build
5. pkexec ninja -C build install                     # root step 2
```

Two `pkexec` prompts are expected: dependencies must be installed before the build,
and the install must run after the build. The build itself runs as the user inside
`~/.cache/gtkterm/src`, so the resulting system install has no dependency on the
original folder.

Per-distro build-dependency package lists (gtk3, vte 2.91, gudev, lua 5.4,
gtksourceview-4, meson, ninja, gcc, pkgconf, git), mapped for pacman / apt / dnf.
Unrecognized distros exit with a clear "unsupported distro, install deps manually"
message and the list of required libraries.

The script streams progress to stdout/stderr and uses distinct exit codes so the
caller can tell success from failure.

## UI — "Check for updates now"

`help_about_callback` currently calls the stock `gtk_show_about_dialog()`, which
cannot host custom buttons. It will be rebuilt using `gtk_about_dialog_new()` with
the same fields (program-name, logo, version, comments, copyright, authors, website,
license) and a **"Check for updates now"** button added to its action area.

Clicking the button runs the same remote check. On confirmation, the app spawns the
update script and shows a **progress dialog**: a `GtkTextView` inside a scrolled
window that streams the script's stdout/stderr (read via `GIOChannel` on the spawned
pipes). On success → "Update complete. Restart GTKTerm now?". On failure → the dialog
stays open showing the output and an error status.

## Config (`term_config.c`, `.gtktermrc`)

Three new keys, saved/loaded alongside existing config, with defaults:

| Key | Default | Meaning |
|-----|---------|---------|
| `update_url` | `https://github.com/Mula-Gabriel/gtkterm.git` | git repo to pull from |
| `update_check_startup` | `True` | run the version check at startup |
| `update_prefix` | `/usr/local` | install prefix passed to the script |

These are added to the config struct, to `Config_Set_Default()` (or equivalent
default init), and to the save/load routines mirroring existing keys.

## Files touched

New:
- `src/update.c`, `src/update.h` — update module
- `data/gtkterm-update.sh` — the update script

Modified:
- `meson.build` — bake `GIT_REVISION` into `config.h` (with tarball fallback)
- `src/meson.build` — add `update.c` / `update.h` to sources
- `data/meson.build` — install `gtkterm-update.sh` to `$datadir/gtkterm/`
- `src/interface.c` — rebuild the About dialog and add the button
- `src/term_config.c` (+ `term_config.h`) — three config keys, defaults, save/load
- `src/gtkterm.c` — call the startup check after the UI is built (if enabled)

## Out of scope (YAGNI)

- Configurable branch (fixed to `master`).
- Distros beyond Arch / Debian-family / Fedora.
- Rollback / version pinning beyond "skip this version".
- Single-prompt privilege escalation (two `pkexec` prompts accepted).

## Code style

Match each file's existing style: `interface.c` / `term_config.c` use tabs, Allman
braces, no space before `(`. New files (`update.c`, `update.h`) follow that dominant
style. The shell script is POSIX `sh`-compatible where practical.
