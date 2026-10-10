#!/bin/sh
# Lance un scénario de la suite graphique GTK 4 dans un environnement isolé : bus de session privé sans services (dbus-run-session), aucun affichage
# réel (DISPLAY, WAYLAND_DISPLAY retirés ; chaque Env de gtktest.py lance son gtk4-broadwayd sur 127.0.0.1), HOME / XDG_* privés,
# XDG_RUNTIME_DIR court, garde-fou fichiers/ports (LD_PRELOAD, posé par gtktest.py). Sauté (code 77) si un prérequis manque.
command -v gtk4-broadwayd >/dev/null 2>&1 || { echo "gtk4-broadwayd absent"; exit 77; }
command -v dbus-run-session >/dev/null 2>&1 || { echo "dbus-run-session absent"; exit 77; }
python3 -c 'import gi; gi.require_version("Gio", "2.0"); from gi.repository import Gio' 2>/dev/null || { echo "python3-gi absent"; exit 77; }
: "${GTKTERM_GUI_RUNS:?GTKTERM_GUI_RUNS non défini}"
SCENARIO=$1
WORK="$GTKTERM_GUI_RUNS/$(basename "$SCENARIO" .py)-session"
rm -rf "$WORK" && mkdir -p "$WORK/home" || exit 1
RUNDIR=$(mktemp -d /tmp/gt4s-XXXXXX) || exit 1
# Bus de session privé SANS dossier de services : rien n'y est activable (ni xdg-desktop-portal, ni gvfs, ni at-spi). GTK 4 ne passe
# pas non plus par les portails : GDK_DEBUG=no-portals (GTK 4 ignore GTK_USE_PORTAL).
cat > "$WORK/session.conf" <<EOF
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN" "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>session</type>
  <listen>unix:tmpdir=$RUNDIR</listen>
  <auth>EXTERNAL</auth>
  <policy context="default">
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/>
    <allow own="*"/>
  </policy>
</busconfig>
EOF
export GTKTERM_GUARD_REAL_HOME="$HOME"
env -u DISPLAY -u WAYLAND_DISPLAY -u XAUTHORITY -u DBUS_SESSION_BUS_ADDRESS -u AT_SPI_BUS_ADDRESS -u GTK_A11Y -u NO_AT_BRIDGE -u GTK_USE_PORTAL \
  XDG_RUNTIME_DIR="$RUNDIR" HOME="$WORK/home" XDG_CONFIG_HOME="$WORK/home/.config" XDG_CACHE_HOME="$WORK/home/.cache" \
  XDG_DATA_HOME="$WORK/home/.local/share" XDG_DATA_DIRS=/usr/local/share:/usr/share GIO_USE_VFS=local GIO_USE_VOLUME_MONITOR=unix GDK_DEBUG=no-portals \
  PYTHONPATH="$(dirname "$SCENARIO")" PYTHONDONTWRITEBYTECODE=1 \
  timeout "${GTKTERM_GUI_TIMEOUT:-240}" dbus-run-session --config-file="$WORK/session.conf" -- python3 "$SCENARIO" &
# timeout (sans --foreground) se met dans son propre groupe, chef de groupe : l'arrêt du test par meson (Ctrl+C, délai dépassé : SIGTERM au groupe
# de ce sh) ne l'atteindrait pas, et le scénario continuerait en orphelin, à côté d'une suite relancée. Le signal est donc relayé à tout le
# groupe de timeout (le scénario y fait son nettoyage : gtkterm et broadwayd, dans leurs propres groupes, sont tués par gtktest.py).
PID=$!
trap 'kill -TERM -$PID 2>/dev/null' INT TERM HUP	# « kill -SIG -PGID » : dash refuse « -- »
wait $PID
STATUS=$?
# Fin réelle de tout le groupe, 10 s au plus : wait interrompu par un signal relayé, ou timeout rendu avant la fin du nettoyage du scénario
N=0
while kill -0 -$PID 2>/dev/null && [ $N -lt 100 ]; do
	sleep 0.1
	N=$((N + 1))
done
rm -rf "$RUNDIR"
exit $STATUS
