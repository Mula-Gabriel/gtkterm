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
