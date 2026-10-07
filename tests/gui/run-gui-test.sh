#!/bin/sh
# Lance un scénario graphique dans un environnement isolé : affichage broadway privé, bus D-Bus et AT-SPI privés,
# HOME / XDG_* dans le répertoire de travail du test, garde-fou contre les vrais ports et la vraie configuration.
# Sauté (code 77) si un prérequis manque.
command -v broadwayd >/dev/null 2>&1 || { echo "broadwayd absent"; exit 77; }
command -v dbus-run-session >/dev/null 2>&1 || { echo "dbus-run-session absent"; exit 77; }
python3 -c 'import gi; gi.require_version("Atspi", "2.0"); from gi.repository import Atspi' 2>/dev/null || { echo "python3-gi / Atspi absent"; exit 77; }
: "${GTKTERM_GUI_RUNS:?GTKTERM_GUI_RUNS non défini}"
SCENARIO=$1
WORK="$GTKTERM_GUI_RUNS/$(basename "$SCENARIO" .py)-session"
rm -rf "$WORK" && mkdir -p "$WORK/run" "$WORK/home" && chmod 700 "$WORK/run"
export GTKTERM_GUARD_REAL_HOME="$HOME"
exec env -u DISPLAY -u WAYLAND_DISPLAY -u XAUTHORITY -u DBUS_SESSION_BUS_ADDRESS -u AT_SPI_BUS_ADDRESS \
  XDG_RUNTIME_DIR="$WORK/run" HOME="$WORK/home" XDG_CONFIG_HOME="$WORK/home/.config" XDG_CACHE_HOME="$WORK/home/.cache" \
  XDG_DATA_HOME="$WORK/home/.local/share" XDG_DATA_DIRS=/usr/local/share:/usr/share GIO_USE_VFS=local GIO_USE_VOLUME_MONITOR=unix GTK_USE_PORTAL=0 \
  PYTHONPATH="$(dirname "$SCENARIO")" PYTHONDONTWRITEBYTECODE=1 \
  timeout "${GTKTERM_GUI_TIMEOUT:-240}" dbus-run-session -- python3 "$SCENARIO"
