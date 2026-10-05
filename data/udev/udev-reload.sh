#!/bin/sh
# Après ninja install : udev relit ses règles et renomme les ports déjà branchés. Rien à faire dans un DESTDIR (paquet) ou sans udev.
[ -n "${DESTDIR:-}" ] && exit 0
command -v udevadm >/dev/null 2>&1 || exit 0
udevadm control --reload && udevadm trigger --subsystem-match=tty || true
