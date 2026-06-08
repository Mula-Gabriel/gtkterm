# Auto-update Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an in-app self-update to GTKTerm that checks for a newer version at startup and from a "Check for updates now" button in Help → About, then pulls from a configurable git URL, installs build deps, builds, and installs to system paths independent of any original checkout.

**Architecture:** A C module (`src/update.c/.h`) handles the remote version check, confirmation/progress dialogs, and spawning a self-contained shell script (`data/gtkterm-update.sh`). The script clones/builds in `~/.cache/gtkterm/src` and installs via `pkexec`, so the system install never depends on the original folder. The build-time git commit is baked into `config.h` as `GIT_REVISION` and compared to the remote HEAD via `git ls-remote`.

**Tech Stack:** C, GTK+3, GLib (`g_spawn_async_with_pipes`, `GIOChannel`, `g_idle_add`), Meson/Ninja, POSIX shell, `pkexec`.

---

## Notes for the implementer

- **No test suite exists** in this repo (see CLAUDE.md). "Verification" in each task means: the auto-build hook compiles cleanly (`ninja -C build` runs automatically after `src/*.c|*.h` edits), plus the specific manual run described. Do **not** invent a test framework.
- **Code style:** `interface.c`, `term_config.c`, `gtkterm.c` use **tabs, Allman braces, no space before `(`** (`if(cond)`). New files `src/update.c` / `src/update.h` follow this dominant style. Match it.
- Commit after each task with French commit messages (repo convention), ending each commit body with the `Co-Authored-By` trailer.
- Build manually any time with: `ninja -C build`. Run with: `./build/src/gtkterm`.

---

## Task 1: Bake the git revision into config.h

**Files:**
- Modify: `meson.build` (the `conf` configuration_data block, around lines 25–30 where `VERSION`/`RELEASE_DATE` are set)

- [ ] **Step 1: Add GIT_REVISION to the meson config block**

In `meson.build`, immediately after the existing `conf.set_quoted('RELEASE_DATE', 'May 2024')` line, add:

```meson
# Bake the current git commit so the app can compare against the remote HEAD.
git_prog = find_program('git', required : false)
git_revision = 'unknown'
if git_prog.found()
	git_rev_result = run_command(git_prog, 'rev-parse', 'HEAD', check : false)
	if git_rev_result.returncode() == 0
		git_revision = git_rev_result.stdout().strip()
	endif
endif
conf.set_quoted('GIT_REVISION', git_revision)

# Path where the update script is installed (used by src/update.c).
conf.set_quoted('PACKAGE_DATA_DIR', join_paths(datadir, 'gtkterm'))
```

Note: `datadir` is already defined earlier in `meson.build` (`datadir = join_paths(prefix, get_option('datadir'))`). There is no existing `DATADIR` compile macro — this new `PACKAGE_DATA_DIR` is what `update.c` will use.

- [ ] **Step 2: Reconfigure and build**

Run: `meson setup --reconfigure build && ninja -C build`
Expected: build succeeds.

- [ ] **Step 3: Verify the macro is defined**

Run: `grep GIT_REVISION build/config.h`
Expected: a line like `#define GIT_REVISION "768b6bd..."` (40-hex sha), not `"unknown"` (since this is a git checkout).

- [ ] **Step 4: Commit**

```bash
git add meson.build
git commit -m "build: expose le commit git courant via GIT_REVISION dans config.h

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 2: Add update config keys (url / startup toggle / prefix)

**Files:**
- Modify: `src/term_config.h:46-65` (the `struct configuration_port`)
- Modify: `src/term_config.c` (globals ~63-97, `cfg[]` ~100-136, `Hard_default_configuration` ~1740, `Load_configuration_from_file` ~1472-1547, `Copy_configuration` ~1764-1858)

- [ ] **Step 1: Add fields to the config struct**

In `src/term_config.h`, inside `struct configuration_port`, just before the closing `};` (after `gboolean disable_port_lock;` on line 64), add:

```c
	gchar update_url[1024];      // git repo to pull updates from
	gboolean update_check_startup; // check for updates at startup
	gchar update_prefix[256];    // install prefix passed to the update script
```

- [ ] **Step 2: Add the parser globals and cfg[] entries**

In `src/term_config.c`, after `gchar **socket_port;` (line 97) add:

```c
gchar **update_url_cfg;
gint *update_check_startup_cfg;
gchar **update_prefix_cfg;
```

Then in the `cfg[]` array, immediately before the terminating `{NULL, CFG_END, NULL}` line (line 136), add:

```c
	{"update_url", CFG_STRING, &update_url_cfg},
	{"update_check_startup", CFG_BOOL, &update_check_startup_cfg},
	{"update_prefix", CFG_STRING, &update_prefix_cfg},
```

- [ ] **Step 3: Set defaults in Hard_default_configuration**

Read `src/term_config.c` around line 1740 first to see the surrounding assignments (e.g. `config.autoreconnect_enabled = FALSE;`). Add these alongside them (use the dominant tab/no-space style):

```c
	g_strlcpy(config.update_url, "https://github.com/Mula-Gabriel/gtkterm.git", sizeof(config.update_url));
	config.update_check_startup = TRUE;
	g_strlcpy(config.update_prefix, "/usr/local", sizeof(config.update_prefix));
```

- [ ] **Step 4: Load the values in Load_configuration_from_file**

Read `src/term_config.c:1530-1550` first to match the exact pattern used for string fields (e.g. how `port`/`socket_host` are copied) and the `autoreconnect_enabled[i] != -1` guard. Then, following that pattern, add inside the same per-index load block:

```c
				if(update_url_cfg != NULL && update_url_cfg[i] != NULL)
					g_strlcpy(config.update_url, update_url_cfg[i], sizeof(config.update_url));
				else
					g_strlcpy(config.update_url, "https://github.com/Mula-Gabriel/gtkterm.git", sizeof(config.update_url));

				if(update_check_startup_cfg != NULL && update_check_startup_cfg[i] != -1)
					config.update_check_startup = (gboolean)update_check_startup_cfg[i];
				else
					config.update_check_startup = TRUE;

				if(update_prefix_cfg != NULL && update_prefix_cfg[i] != NULL)
					g_strlcpy(config.update_prefix, update_prefix_cfg[i], sizeof(config.update_prefix));
				else
					g_strlcpy(config.update_prefix, "/usr/local", sizeof(config.update_prefix));
```

Note: verify the exact variable name `i` matches the loop index used in that function before pasting; adjust if the surrounding code uses a different index name.

- [ ] **Step 5: Persist the values in Copy_configuration**

Read `src/term_config.c:1850-1860` first to match the `cfgStoreValue` pattern (note `string` is the reused temp buffer and `pos` the section index). Following the `autoreconnect_enabled` example, add:

```c
	cfgStoreValue(cfg, "update_url", config.update_url, CFG_INI, pos);

	if(config.update_check_startup == FALSE)
		g_strlcpy(string, "False", sizeof(string));
	else
		g_strlcpy(string, "True", sizeof(string));
	cfgStoreValue(cfg, "update_check_startup", string, CFG_INI, pos);

	cfgStoreValue(cfg, "update_prefix", config.update_prefix, CFG_INI, pos);
```

Verify the temp buffer is named `string` and its size usage matches the surrounding `cfgStoreValue` calls (some use `g_snprintf(string, sizeof(string), ...)`); mirror whatever the adjacent code does.

- [ ] **Step 6: Build**

Run: `ninja -C build`
Expected: compiles cleanly.

- [ ] **Step 7: Manually verify round-trip**

Run `./build/src/gtkterm`, open Configuration → Port, then Save the config as "default" (or use the existing save path). Quit. Then:
Run: `grep -E 'update_url|update_check_startup|update_prefix' ~/.config/.gtktermrc`
Expected: the three keys appear with the default values.

- [ ] **Step 8: Commit**

```bash
git add src/term_config.c src/term_config.h
git commit -m "config: ajoute update_url, update_check_startup et update_prefix

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 3: The update shell script

**Files:**
- Create: `data/gtkterm-update.sh`
- Modify: `data/meson.build` (add an `install_data` for the script)

- [ ] **Step 1: Write the script**

Create `data/gtkterm-update.sh` with exactly this content:

```sh
#!/bin/sh
# GTKTerm self-update script.
# Clones/updates the repo in a managed cache dir, installs build deps,
# builds, and installs. The resulting system install does not depend on
# any original checkout, so the source folder can be deleted afterwards.
#
# Usage: gtkterm-update.sh <repo-url> <install-prefix>
# Privileged steps (dependency install, ninja install) use pkexec.

set -eu

REPO_URL="${1:-https://github.com/Mula-Gabriel/gtkterm.git}"
PREFIX="${2:-/usr/local}"
BRANCH="master"
WORKDIR="${XDG_CACHE_HOME:-$HOME/.cache}/gtkterm/src"

echo ">>> GTKTerm update"
echo ">>> repo:   $REPO_URL"
echo ">>> prefix: $PREFIX"
echo ">>> workdir:$WORKDIR"

# --- 1. get the source -------------------------------------------------
if [ -d "$WORKDIR/.git" ]; then
	echo ">>> Updating existing checkout..."
	git -C "$WORKDIR" remote set-url origin "$REPO_URL"
	git -C "$WORKDIR" fetch origin "$BRANCH"
	git -C "$WORKDIR" reset --hard "origin/$BRANCH"
else
	echo ">>> Cloning..."
	rm -rf "$WORKDIR"
	mkdir -p "$(dirname "$WORKDIR")"
	git clone --branch "$BRANCH" "$REPO_URL" "$WORKDIR"
fi

# --- 2. detect distro & build dep list ---------------------------------
PM=""
if [ -r /etc/os-release ]; then
	. /etc/os-release
fi
ID_ALL="${ID:-} ${ID_LIKE:-}"

case " $ID_ALL " in
	*arch*)   PM="pacman" ;;
	*debian*|*ubuntu*) PM="apt" ;;
	*fedora*|*rhel*)   PM="dnf" ;;
esac

if command -v pacman >/dev/null 2>&1 && [ -z "$PM" ]; then PM="pacman"; fi
if command -v apt-get >/dev/null 2>&1 && [ -z "$PM" ]; then PM="apt"; fi
if command -v dnf >/dev/null 2>&1 && [ -z "$PM" ]; then PM="dnf"; fi

case "$PM" in
	pacman)
		DEPS="gtk3 vte3 libgudev lua gtksourceview4 meson ninja gcc pkgconf git"
		INSTALL="pacman -S --needed --noconfirm $DEPS" ;;
	apt)
		DEPS="libgtk-3-dev libvte-2.91-dev libgudev-1.0-dev liblua5.4-dev libgtksourceview-4-dev meson ninja-build gcc pkg-config git"
		INSTALL="apt-get update && apt-get install -y $DEPS" ;;
	dnf)
		DEPS="gtk3-devel vte291-devel libgudev-devel lua-devel gtksourceview4-devel meson ninja-build gcc pkgconf-pkg-config git"
		INSTALL="dnf install -y $DEPS" ;;
	*)
		echo "!!! Unsupported distribution." >&2
		echo "!!! Install these libraries manually, then re-run:" >&2
		echo "!!!   gtk+-3.0, vte-2.91, gudev-1.0, lua5.4, gtksourceview-4, meson, ninja, gcc, pkg-config, git" >&2
		exit 3 ;;
esac

# --- 3. install deps (root) --------------------------------------------
echo ">>> Installing build dependencies via $PM (will prompt for authorization)..."
pkexec sh -c "$INSTALL"

# --- 4. build (as user) ------------------------------------------------
echo ">>> Configuring & building..."
cd "$WORKDIR"
if [ -d build ]; then
	meson setup --reconfigure --prefix="$PREFIX" build
else
	meson setup --prefix="$PREFIX" build
fi
ninja -C build

# --- 5. install (root) -------------------------------------------------
echo ">>> Installing (will prompt for authorization)..."
pkexec ninja -C build install

echo ">>> Update complete."
```

- [ ] **Step 2: Make it executable**

Run: `chmod +x data/gtkterm-update.sh`

- [ ] **Step 3: Install the script via meson**

In `data/meson.build`, after the `# Manpage` / `install_man('gtkterm.1')` line at the end, add:

```meson
# Self-update script, used by the in-app updater.
install_data('gtkterm-update.sh',
	install_dir: join_paths(datadir, 'gtkterm'),
	install_mode: 'rwxr-xr-x'
)
```

- [ ] **Step 4: Reconfigure & build**

Run: `meson setup --reconfigure build && ninja -C build`
Expected: build succeeds (this confirms meson accepts the new `install_data`).

- [ ] **Step 5: Smoke-test the script's non-privileged logic**

Run: `sh -n data/gtkterm-update.sh && echo "syntax ok"`
Expected: `syntax ok` (no syntax errors). Do NOT run the full script yet — it modifies the system.

- [ ] **Step 6: Commit**

```bash
git add data/gtkterm-update.sh data/meson.build
git commit -m "update: script de mise à jour autonome (clone, deps, build, install)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 4: The update C module (check + spawn + progress dialog)

**Files:**
- Create: `src/update.h`
- Create: `src/update.c`
- Modify: `src/meson.build` (add `update.c` / `update.h` to `sources`)

- [ ] **Step 1: Write the header**

Create `src/update.h`:

```c
#ifndef UPDATE_H_
#define UPDATE_H_

#include <gtk/gtk.h>

/*
 * Run the version check against the configured remote.
 *
 * manual == TRUE  : invoked from the "Check for updates now" button. Always
 *                   reports the result to the user (up-to-date / error / newer).
 * manual == FALSE : startup check. Stays silent unless a newer version is found
 *                   (and not previously skipped).
 *
 * parent is the window used to anchor any dialog (may be NULL).
 */
void update_check(GtkWindow *parent, gboolean manual);

#endif /* UPDATE_H_ */
```

- [ ] **Step 2: Write the module**

Create `src/update.c`. It uses the dominant tab/Allman/no-space style.

```c
#include "config.h"

#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <string.h>

#include "update.h"
#include "term_config.h"

extern struct configuration_port config;

/* Remembered "skip this version" sha for the current session. */
static gchar skipped_revision[64] = "";

/* ---- progress dialog: streams the update script's output ------------- */

typedef struct
{
	GtkWidget *dialog;
	GtkTextBuffer *buffer;
	GtkWidget *close_button;
} ProgressUI;

static void append_output(ProgressUI *ui, const gchar *text)
{
	GtkTextIter end;
	gtk_text_buffer_get_end_iter(ui->buffer, &end);
	gtk_text_buffer_insert(ui->buffer, &end, text, -1);
}

static gboolean on_script_output(GIOChannel *source, GIOCondition condition, gpointer data)
{
	ProgressUI *ui = (ProgressUI *)data;
	gchar *line = NULL;
	gsize len = 0;
	GIOStatus status;

	if(condition & (G_IO_IN | G_IO_PRI))
	{
		while((status = g_io_channel_read_line(source, &line, &len, NULL, NULL)) == G_IO_STATUS_NORMAL)
		{
			append_output(ui, line);
			g_free(line);
			line = NULL;
		}
	}

	if(condition & (G_IO_HUP | G_IO_ERR))
		return FALSE;

	return TRUE;
}

static void on_child_exit(GPid pid, gint status, gpointer data)
{
	ProgressUI *ui = (ProgressUI *)data;
	gboolean ok = g_spawn_check_wait_status(status, NULL);

	if(ok)
	{
		append_output(ui, _("\n>>> Update finished successfully.\n"));
		GtkWidget *q = gtk_message_dialog_new(GTK_WINDOW(ui->dialog),
				GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
				_("Update complete. Restart GTKTerm now?"));
		if(gtk_dialog_run(GTK_DIALOG(q)) == GTK_RESPONSE_YES)
		{
			gchar *argv[] = { (gchar *)"gtkterm", NULL };
			g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
			gtk_main_quit();
		}
		gtk_widget_destroy(q);
	}
	else
	{
		append_output(ui, _("\n!!! Update failed. See the log above.\n"));
	}

	gtk_widget_set_sensitive(ui->close_button, TRUE);
	g_spawn_close_pid(pid);
}

static void run_update_script(GtkWindow *parent)
{
	const gchar *script = PACKAGE_DATA_DIR "/gtkterm-update.sh";
	gchar *argv[] = {
		(gchar *)"sh", (gchar *)script,
		config.update_url, config.update_prefix, NULL
	};
	GError *error = NULL;
	GPid pid;
	gint out_fd = -1, err_fd = -1;

	ProgressUI *ui = g_new0(ProgressUI, 1);
	ui->dialog = gtk_dialog_new_with_buttons(_("Updating GTKTerm"),
			parent, GTK_DIALOG_DESTROY_WITH_PARENT,
			_("_Close"), GTK_RESPONSE_CLOSE, NULL);
	gtk_window_set_default_size(GTK_WINDOW(ui->dialog), 640, 400);
	ui->close_button = gtk_dialog_get_widget_for_response(GTK_DIALOG(ui->dialog), GTK_RESPONSE_CLOSE);
	gtk_widget_set_sensitive(ui->close_button, FALSE);

	GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
	GtkWidget *view = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
	ui->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
	gtk_container_add(GTK_CONTAINER(scroll), view);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(ui->dialog))), scroll, TRUE, TRUE, 0);
	gtk_widget_show_all(ui->dialog);

	if(!g_spawn_async_with_pipes(NULL, argv, NULL,
			G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
			NULL, NULL, &pid, NULL, &out_fd, &err_fd, &error))
	{
		append_output(ui, error ? error->message : _("Failed to start update script."));
		gtk_widget_set_sensitive(ui->close_button, TRUE);
		g_clear_error(&error);
		g_signal_connect_swapped(ui->dialog, "response", G_CALLBACK(gtk_widget_destroy), ui->dialog);
		return;
	}

	GIOChannel *out_ch = g_io_channel_unix_new(out_fd);
	GIOChannel *err_ch = g_io_channel_unix_new(err_fd);
	g_io_channel_set_flags(out_ch, G_IO_FLAG_NONBLOCK, NULL);
	g_io_channel_set_flags(err_ch, G_IO_FLAG_NONBLOCK, NULL);
	g_io_add_watch(out_ch, G_IO_IN | G_IO_HUP | G_IO_ERR, on_script_output, ui);
	g_io_add_watch(err_ch, G_IO_IN | G_IO_HUP | G_IO_ERR, on_script_output, ui);
	g_io_channel_unref(out_ch);
	g_io_channel_unref(err_ch);
	g_child_watch_add(pid, on_child_exit, ui);

	g_signal_connect_swapped(ui->dialog, "response", G_CALLBACK(gtk_widget_destroy), ui->dialog);
}

/* ---- the remote version check --------------------------------------- */

typedef struct
{
	GtkWindow *parent;
	gboolean manual;
} CheckCtx;

static void offer_update(GtkWindow *parent, const gchar *remote_sha)
{
	GtkWidget *d = gtk_dialog_new_with_buttons(_("Update available"),
			parent, GTK_DIALOG_DESTROY_WITH_PARENT,
			_("_Update now"), GTK_RESPONSE_ACCEPT,
			_("_Later"), GTK_RESPONSE_REJECT,
			_("_Skip this version"), GTK_RESPONSE_NO, NULL);
	GtkWidget *label = gtk_label_new(_("A new version of GTKTerm is available.\nUpdate now?"));
	gtk_widget_set_margin_top(label, 12);
	gtk_widget_set_margin_bottom(label, 12);
	gtk_widget_set_margin_start(label, 12);
	gtk_widget_set_margin_end(label, 12);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(d))), label, TRUE, TRUE, 0);
	gtk_widget_show_all(d);

	gint resp = gtk_dialog_run(GTK_DIALOG(d));
	gtk_widget_destroy(d);

	if(resp == GTK_RESPONSE_ACCEPT)
		run_update_script(parent);
	else if(resp == GTK_RESPONSE_NO)
		g_strlcpy(skipped_revision, remote_sha, sizeof(skipped_revision));
}

static void on_ls_remote_done(GObject *source, GAsyncResult *res, gpointer data)
{
	CheckCtx *ctx = (CheckCtx *)data;
	GError *error = NULL;
	gchar *stdout_buf = NULL;
	GSubprocess *proc = G_SUBPROCESS(source);

	gboolean ok = g_subprocess_communicate_utf8_finish(proc, res, &stdout_buf, NULL, &error);

	if(!ok || stdout_buf == NULL || stdout_buf[0] == '\0')
	{
		if(ctx->manual)
		{
			GtkWidget *m = gtk_message_dialog_new(ctx->parent,
					GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
					_("Could not check for updates:\n%s"),
					error ? error->message : _("no response from remote"));
			gtk_dialog_run(GTK_DIALOG(m));
			gtk_widget_destroy(m);
		}
		g_clear_error(&error);
		g_free(stdout_buf);
		g_free(ctx);
		return;
	}

	/* `git ls-remote` output: "<sha>\tHEAD\n" -> take the leading sha. */
	gchar remote_sha[64] = "";
	sscanf(stdout_buf, "%63s", remote_sha);
	g_free(stdout_buf);

	gboolean up_to_date = (g_strcmp0(remote_sha, GIT_REVISION) == 0);
	gboolean already_skipped = (g_strcmp0(remote_sha, skipped_revision) == 0);

	if(up_to_date)
	{
		if(ctx->manual)
		{
			GtkWidget *m = gtk_message_dialog_new(ctx->parent,
					GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
					_("GTKTerm is up to date."));
			gtk_dialog_run(GTK_DIALOG(m));
			gtk_widget_destroy(m);
		}
	}
	else if(ctx->manual || !already_skipped)
	{
		offer_update(ctx->parent, remote_sha);
	}

	g_free(ctx);
}

void update_check(GtkWindow *parent, gboolean manual)
{
	/* Startup check obeys the toggle; the manual button ignores it. */
	if(!manual && !config.update_check_startup)
		return;

	/* If we don't know our own commit, only the manual path proceeds. */
	if(g_strcmp0(GIT_REVISION, "unknown") == 0 && !manual)
		return;

	GError *error = NULL;
	GSubprocess *proc = g_subprocess_new(
			G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
			&error,
			"git", "ls-remote", config.update_url, "HEAD", NULL);

	if(proc == NULL)
	{
		if(manual)
		{
			GtkWidget *m = gtk_message_dialog_new(parent,
					GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
					_("Could not run git: %s"), error ? error->message : "");
			gtk_dialog_run(GTK_DIALOG(m));
			gtk_widget_destroy(m);
		}
		g_clear_error(&error);
		return;
	}

	CheckCtx *ctx = g_new0(CheckCtx, 1);
	ctx->parent = parent;
	ctx->manual = manual;
	g_subprocess_communicate_utf8_async(proc, NULL, NULL, on_ls_remote_done, ctx);
	g_object_unref(proc);
}
```

- [ ] **Step 3: Confirm PACKAGE_DATA_DIR is available**

This macro was added to `config.h` in Task 1 Step 1.
Run: `grep PACKAGE_DATA_DIR build/config.h`
Expected: `#define PACKAGE_DATA_DIR "/usr/local/share/gtkterm"` (or your prefix). If missing, complete Task 1 first.

- [ ] **Step 4: Add the module to the build**

In `src/meson.build`, inside the `sources = [ ... ]` list, add (alphabetically near the others, e.g. after `'transport.h',`):

```meson
	'update.c',
	'update.h',
```

- [ ] **Step 5: Build**

Run: `ninja -C build`
Expected: compiles cleanly. If `PACKAGE_DATA_DIR` is undefined, complete Task 1 and `meson setup --reconfigure build`, then rebuild.

- [ ] **Step 6: Commit**

```bash
git add src/update.c src/update.h src/meson.build
git commit -m "update: module C (vérification distante, dialogues, lancement du script)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 5: Rebuild the About dialog with a "Check for updates now" button

**Files:**
- Modify: `src/interface.c:877-900` (`help_about_callback`)
- Modify: `src/interface.c` includes (around lines 70-82)

- [ ] **Step 1: Include the update header**

In `src/interface.c`, after `#include "device_monitor.h"` (line 82), add:

```c
#include "update.h"
```

- [ ] **Step 2: Replace help_about_callback**

Replace the entire `help_about_callback` function (lines 877-900) with a manual `GtkAboutDialog` so a custom button can be added. New body:

```c
void help_about_callback(GtkAction *action, gpointer data)
{
	gchar *authors[] = {"Julien Schimtt", "Zach Davis", "Florian Euchner", "Stephan Enderlein",
			    "Kevin Picot", NULL};
	gchar *comments_program = _("GTKTerm is a simple GTK+ terminal used to communicate with the serial port.");
	gchar comments[256];
	GError *error = NULL;
	GdkPixbuf *logo = NULL;

	logo = gdk_pixbuf_new_from_resource ("/org/gtk/gtkterm/gtkterm_64x64.png", &error);
	g_snprintf(comments, sizeof(comments), "%s\n\n%s", RELEASE_DATE, comments_program);

	GtkWidget *dialog = gtk_about_dialog_new();
	gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(Fenetre));
	gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
	gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dialog), "GTKTerm fork MGU");
	if(logo != NULL)
		gtk_about_dialog_set_logo(GTK_ABOUT_DIALOG(dialog), logo);
	gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dialog), VERSION);
	gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dialog), comments);
	gtk_about_dialog_set_copyright(GTK_ABOUT_DIALOG(dialog), "Copyright © Julien Schimtt");
	gtk_about_dialog_set_authors(GTK_ABOUT_DIALOG(dialog), (const gchar **)authors);
	gtk_about_dialog_set_website(GTK_ABOUT_DIALOG(dialog), "https://github.com/Mula-Gabriel/gtkterm");
	gtk_about_dialog_set_website_label(GTK_ABOUT_DIALOG(dialog), "https://github.com/Mula-Gabriel/gtkterm");
	gtk_about_dialog_set_license_type(GTK_ABOUT_DIALOG(dialog), GTK_LICENSE_LGPL_3_0);

	/* Add the "Check for updates now" button to the dialog action area. */
	gtk_dialog_add_button(GTK_DIALOG(dialog), _("Check for updates now"), 1);

	gint resp;
	do
	{
		resp = gtk_dialog_run(GTK_DIALOG(dialog));
		if(resp == 1)
			update_check(GTK_WINDOW(dialog), TRUE);
	}
	while(resp == 1);

	gtk_widget_destroy(dialog);
	if(logo != NULL)
		g_object_unref(logo);
}
```

- [ ] **Step 3: Build**

Run: `ninja -C build`
Expected: compiles cleanly.

- [ ] **Step 4: Manual verification**

Run: `./build/src/gtkterm`. Open Help → About.
Expected: the About dialog shows the usual info **plus** a "Check for updates now" button. Click it: it runs the check and either reports "up to date", an error (e.g. if offline), or offers the update. Closing the About dialog works normally.

- [ ] **Step 5: Commit**

```bash
git add src/interface.c
git commit -m "interface: bouton 'vérifier maintenant' dans la fenêtre À propos

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 6: Wire the startup check

**Files:**
- Modify: `src/gtkterm.c` (after `create_main_window();` line 51, before `gtk_main();` line 76)

- [ ] **Step 1: Include the header**

In `src/gtkterm.c`, after `#include "interface.h"` (line 23), add:

```c
#include "update.h"
```

- [ ] **Step 2: Schedule the startup check**

The check must run after the main loop starts so the UI is realized. Add a one-shot idle helper. Near the top of `src/gtkterm.c` (after the includes, before `int main`), add:

```c
static gboolean startup_update_check(gpointer data)
{
	update_check(GTK_WINDOW(Fenetre), FALSE);
	return G_SOURCE_REMOVE;
}
```

Then in `main`, immediately before `gtk_main();` (line 76), add:

```c
	g_idle_add(startup_update_check, NULL);
```

Note: `Fenetre` is the global main window declared in `interface.c`. Confirm it is reachable — `interface.h` already exposes it (it is used throughout). If it is not declared `extern` in a header, add `extern GtkWidget *Fenetre;` near the top of `gtkterm.c`.

- [ ] **Step 3: Build**

Run: `ninja -C build`
Expected: compiles cleanly. If `Fenetre` is undeclared, add the `extern` declaration per the note and rebuild.

- [ ] **Step 4: Manual verification (up-to-date path)**

Ensure your local checkout matches the remote `master` (so no update should be offered). Run: `./build/src/gtkterm`.
Expected: app starts normally, **no** update dialog appears (silent because up to date). Quit.

- [ ] **Step 5: Manual verification (newer-available path)**

To simulate an available update without changing the remote: temporarily edit `build/config.h` to set `#define GIT_REVISION "0000000000000000000000000000000000000000"`, then `ninja -C build` and run `./build/src/gtkterm`.
Expected: shortly after startup, the "Update available" dialog appears with [Update now] [Later] [Skip this version]. Choose **Later** to dismiss. Quit. Then restore the real revision with: `meson setup --reconfigure build && ninja -C build`.

- [ ] **Step 6: Commit**

```bash
git add src/gtkterm.c
git commit -m "gtkterm: vérification de mise à jour au démarrage (si activée)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 7: End-to-end manual test of a real update (optional but recommended)

**Files:** none (manual).

- [ ] **Step 1: Run a real update from the button**

Run `./build/src/gtkterm`, open Help → About → "Check for updates now". If an update is offered (or force it via the config.h trick from Task 6 Step 5), choose **Update now**.
Expected: the progress dialog streams the script output; `pkexec` prompts twice (deps install, then `ninja install`); it ends with "Update finished successfully." and offers to restart.

- [ ] **Step 2: Verify install independence**

After a successful install, confirm the system binary exists and runs independent of the source folder:
Run: `command -v gtkterm && gtkterm --help | head -1`
Expected: resolves to `<prefix>/bin/gtkterm` (e.g. `/usr/local/bin/gtkterm`) and runs. The managed checkout lives at `~/.cache/gtkterm/src`; the original dev folder is not referenced by the installed binary.

- [ ] **Step 3: No commit** (verification only).

---

## Self-review notes

- **Spec coverage:** startup check (Task 6) ✓; "Check now" button in About (Task 5) ✓; configurable URL + startup toggle + prefix (Task 2) ✓; clone/deps/build/install script (Task 3) ✓; install independent of original folder (Task 3 design + Task 7 verification) ✓; GIT_REVISION detection (Task 1) ✓.
- **Type consistency:** `update_check(GtkWindow*, gboolean)` is declared in Task 4 (update.h) and called identically in Tasks 5 and 6. Config fields `config.update_url` / `config.update_check_startup` / `config.update_prefix` defined in Task 2 and consumed in Task 4.
- **Verified against source while writing:** `PACKAGE_DATA_DIR` is newly defined in Task 1 (no pre-existing `DATADIR`); the Load loop index is `i` (term_config.c:1505+); `Fenetre` is already `extern` in `interface.h:45` (so the Task 6 fallback extern is not needed, but harmless). Temp buffer name in Copy (Task 2 Step 5) still to be matched against the surrounding `cfgStoreValue` calls.
```
