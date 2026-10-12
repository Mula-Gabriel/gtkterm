#!/bin/sh
# Mise à jour de GTKTerm vers une version publiée (tag vX.Y.Z) : copie de travail dans le cache, dépendances, construction, installation,
# ménage des fichiers de l'ancienne version, redémarrage. L'installation ne dépend d'aucune copie de travail d'origine.
#
# Usage : gtkterm-update.sh <url-du-dépôt> <préfixe> <pid-à-redémarrer> <tag>
# Une seule autorisation (pkexec) par mise à jour : dépendances, construction (redescendue à l'utilisateur par runuser), installation et ménage.
# Pour les tests seulement : GTKTERM_UPDATE_SUDO remplace pkexec, GTKTERM_UPDATE_DEPS=0 saute l'installation des dépendances.
# Contrat entre versions : ce script passe sur le tag demandé puis se relance sur le script de CETTE version, avec les mêmes arguments ; c'est la
# version visée qui choisit ses dépendances et sa construction (GTK 3 pour v1.3.x, GTK 4 pour v2.x). Le verrou $CACHE/update.lock (avec le PID) est pris
# avant la relance et hérité par le script de la version visée, qui le libère à sa sortie.

set -eu

REPO_URL="${1:-}"
PREFIX="${2:-/usr/local}"
OLD_PID="${3:-}"
TAG="${4:-}"
SUDO="${GTKTERM_UPDATE_SUDO:-pkexec}"
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/gtkterm"
WORKDIR="$CACHE/src"
REGISTRY="$CACHE/installed-files.txt"
# Préfixe sans barre oblique finale (la comparaison « $prefix/* » du ménage en dépend), sauf la racine
while [ "${PREFIX%/}" != "$PREFIX" ] && [ "$PREFIX" != "/" ]; do PREFIX="${PREFIX%/}"; done

die() {
	echo "!!! $*" >&2
	exit 2
}

# Verrou : une seule mise à jour à la fois (deux clics, deux instances), pris avant toute validation pour qu'une sortie anticipée le libère. Dossier
# $CACHE/update.lock contenant le PID du script ; le script relancé sur la version visée (GTKTERM_UPDATE_REEXEC) a le même PID (exec) et le garde. Un
# verrou dont le PID est absent ou n'existe plus (fenêtre du terminal tuée, coupure) est périmé : il est retiré puis repris, une seule fois.
LOCK="$CACHE/update.lock"
lock_pid() { cat "$LOCK/pid" 2>/dev/null || true; }
lock_take() { mkdir "$LOCK" 2>/dev/null && echo "$$" > "$LOCK/pid"; }
lock_release() { if [ "$(lock_pid)" = "$$" ]; then rm -rf "$LOCK"; fi; }
mkdir -p "$CACHE"
if ! lock_take; then
	holder=$(lock_pid)
	if [ -n "${GTKTERM_UPDATE_REEXEC:-}" ] && [ "$holder" = "$$" ]; then
		:	# verrou hérité du script précédent (même processus après exec)
	elif [ -z "$holder" ] || ! kill -0 "$holder" 2>/dev/null; then
		echo ">>> Removing a stale update lock."
		rm -rf "$LOCK"
		lock_take || die "Another GTKTerm update is already running (remove $LOCK if not)."
	else
		die "Another GTKTerm update is already running (remove $LOCK if not)."
	fi
fi
trap lock_release EXIT
trap 'exit 1' HUP INT TERM

# L'URL vient de la configuration (modifiable) : jamais une option, jamais un transport « ext:: » qui lancerait une commande.
case "$REPO_URL" in
	'') die "Missing repository URL." ;;
	-*|ext::*) die "Refusing repository URL: $REPO_URL" ;;
esac
# Seule une version publiée vX.Y.Z (le premier filtre écarte aussi les retours à la ligne, que grep -x lirait ligne par ligne)
case "$TAG" in
	''|*[!v0-9.]*) die "Missing or invalid version tag '$TAG' (expected vX.Y.Z)." ;;
esac
printf '%s\n' "$TAG" | grep -Eqx 'v(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})' ||
	die "Missing or invalid version tag '$TAG' (expected vX.Y.Z)."

echo ">>> GTKTerm update to $TAG"
echo ">>> repo:    $REPO_URL"
echo ">>> prefix:  $PREFIX"
echo ">>> workdir: $WORKDIR"

# --- 1. source : copie de travail sur le tag demandé ---------------------
if [ -d "$WORKDIR/.git" ]; then
	git -C "$WORKDIR" remote set-url origin -- "$REPO_URL"
else
	echo ">>> Cloning..."
	rm -rf "$WORKDIR"
	mkdir -p "$CACHE"
	git -c protocol.ext.allow=never clone --no-checkout -- "$REPO_URL" "$WORKDIR"
fi
git -C "$WORKDIR" -c protocol.ext.allow=never fetch --force origin '+refs/tags/*:refs/tags/*'
git -C "$WORKDIR" rev-parse -q --verify "refs/tags/$TAG^{commit}" >/dev/null || die "Version $TAG not found in $REPO_URL."
git -C "$WORKDIR" checkout --quiet --force --detach "refs/tags/$TAG"
# Aucun fichier d'une autre version ne reste ; build/ est gardé jusqu'à l'étape 4 (journal d'installation de la version en place)
git -C "$WORKDIR" clean -ffdxq -e /build

# --- 2. continuer avec le script de la version obtenue --------------------
NEW_SCRIPT="$WORKDIR/data/gtkterm-update.sh"
if [ -z "${GTKTERM_UPDATE_REEXEC:-}" ] && [ -r "$NEW_SCRIPT" ] && ! cmp -s "$0" "$NEW_SCRIPT"; then
	echo ">>> Continuing with the update script of $TAG..."
	GTKTERM_UPDATE_REEXEC=1 exec sh "$NEW_SCRIPT" "$REPO_URL" "$PREFIX" "$OLD_PID" "$TAG"
fi
# Variable interne : ne pas la transmettre au GTKTerm relancé (il la passerait à la mise à jour suivante, qui sauterait la relance)
unset GTKTERM_UPDATE_REEXEC

# --- 3. commande d'installation des dépendances (exécutée plus bas, dans l'unique autorisation) ---
INSTALL=""
if [ "${GTKTERM_UPDATE_DEPS:-1}" != 0 ]; then
	PM=""
	if [ -r /etc/os-release ]; then
		. /etc/os-release
	fi
	ID_ALL="${ID:-} ${ID_LIKE:-}"
	case " $ID_ALL " in
		*arch*) PM="pacman" ;;
		*debian*|*ubuntu*) PM="apt" ;;
		*fedora*|*rhel*) PM="dnf" ;;
	esac
	if command -v pacman >/dev/null 2>&1 && [ -z "$PM" ]; then PM="pacman"; fi
	if command -v apt-get >/dev/null 2>&1 && [ -z "$PM" ]; then PM="apt"; fi
	if command -v dnf >/dev/null 2>&1 && [ -z "$PM" ]; then PM="dnf"; fi

	# >>> dépendances de cette ligne (GTK 3) >>>
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
			echo "!!!   gtk3, vte-2.91, gudev-1.0, lua5.4, gtksourceview-4, meson, ninja, gcc, pkg-config, git" >&2
			exit 3 ;;
	esac
	# <<< dépendances de cette ligne <<<
fi

# --- 4. registre des fichiers installés (utilisateur) ---------------------
# Fusionné avec le journal de la version en place (installation d'avant le registre, ou mise à jour interrompue entre l'installation et la copie
# finale), avant que build/ ne soit supprimé
OLDLOG="$WORKDIR/build/meson-logs/install-log.txt"
if [ -r "$OLDLOG" ]; then
	mkdir -p "$CACHE"
	if [ -r "$REGISTRY" ]; then
		cat "$REGISTRY" "$OLDLOG" | grep -v '^#' | sort -u > "$REGISTRY.new" || true
	else
		grep -v '^#' "$OLDLOG" | sort -u > "$REGISTRY.new" || true
	fi
	mv -f "$REGISTRY.new" "$REGISTRY"
fi
# Si meson ou ninja manquent côté utilisateur, le nom nu est résolu dans l'autorisation, après l'installation des dépendances
MESON=$(command -v meson) || MESON=meson
NINJA=$(command -v ninja) || NINJA=$(command -v ninja-build) || NINJA=ninja

# --- 5. une seule autorisation : dépendances, construction, installation, ménage ---
# Code de sortie 10 : échec avant « meson install » (rien n'est installé).
# Script fixe : toutes les valeurs arrivent en $1…$9, jamais dans la chaîne. La construction redescend à l'utilisateur (runuser) : build/ lui
# appartient. Sans root (tests), elle se fait directement. Fichiers du registre absents du nouveau journal : retirés seulement sous le préfixe,
# sans « .. », fichiers ou liens (jamais un dossier), n'appartenant à aucun paquet ; sans registre, ancien lanceur d'avant GTK 4 retiré s'il n'est
# pas fourni par la version installée.
ROOT_SCRIPT='
set -e
umask 022
install="$1"; user="$2"; home="$3"; workdir="$4"; prefix="$5"; meson="$6"; ninja="$7"; registry="$8"; legacy="$9"
if [ -n "$install" ]; then
	echo ">>> Installing build dependencies..."
	sh -c "$install" || exit 10
fi
meson=$(command -v "$meson") || { echo "!!! meson not found." >&2; exit 10; }
ninja=$(command -v "$ninja") || { echo "!!! ninja not found." >&2; exit 10; }
as_user() {
	if [ "$(id -u)" = 0 ] && [ "$(id -u "$user")" != 0 ]; then
		command -v runuser >/dev/null 2>&1 || { echo "!!! runuser not found (util-linux)." >&2; exit 10; }
		runuser -u "$user" -- env HOME="$home" "$@"
	else
		"$@"
	fi
}
echo ">>> Configuring & building..."
as_user rm -rf "$workdir/build" || exit 10
as_user "$meson" setup --prefix="$prefix" -Dtests=false "$workdir/build" "$workdir" || exit 10
as_user "$ninja" -C "$workdir/build" || exit 10
echo ">>> Installing..."
"$meson" install -C "$workdir/build" --no-rebuild
newlog="$workdir/build/meson-logs/install-log.txt"
owned() {
	{ command -v dpkg >/dev/null 2>&1 && dpkg -S "$1" >/dev/null 2>&1; } ||
	{ command -v rpm >/dev/null 2>&1 && rpm -qf "$1" >/dev/null 2>&1; } ||
	{ command -v pacman >/dev/null 2>&1 && pacman -Qo "$1" >/dev/null 2>&1; }
}
removable() {
	case "$1" in "$prefix"/*) ;; *) return 1 ;; esac
	case "$1" in */../*|*/..) return 1 ;; esac
	grep -Fqx -- "$1" "$newlog" && return 1
	[ -L "$1" ] || [ -f "$1" ] || return 1
	owned "$1" && return 1
	return 0
}
if [ -r "$registry" ]; then
	while IFS= read -r f; do
		case "$f" in ""|"#"*) continue ;; esac
		if removable "$f"; then
			echo ">>> Removing $f (not part of this version)"
			rm -f -- "$f"
		fi
	done < "$registry"
elif [ -n "$legacy" ] && removable "$legacy"; then
	echo ">>> Removing $legacy (old launcher)"
	rm -f -- "$legacy"
fi
'
echo ">>> Building and installing $TAG (one authorization for the whole update)..."
status=0
$SUDO sh -c "$ROOT_SCRIPT" sh "$INSTALL" "$(id -un)" "$HOME" "$WORKDIR" "$PREFIX" "$MESON" "$NINJA" "$REGISTRY" "$PREFIX/share/applications/gtkterm.desktop" || status=$?
case "$status" in
	0) ;;
	126|127) die "Authorization refused: GTKTerm was not updated." ;;
	10) die "Update failed before installation (see the messages above): GTKTerm was not updated." ;;
	*) die "Update failed (see the messages above): GTKTerm may be partially updated." ;;
esac

# --- 6. registre mis à jour (utilisateur) ----------------------------------
cp "$WORKDIR/build/meson-logs/install-log.txt" "$REGISTRY"

echo ">>> Update to $TAG complete."

# --- 7. redémarrage -------------------------------------------------------
if [ -n "$OLD_PID" ]; then
	echo ">>> Redémarrage de GTKTerm..."
	kill "$OLD_PID" 2>/dev/null || true
	sleep 1
fi
# Relancer l'exécutable qui vient d'être installé (pas le premier gtkterm du PATH). nohup le protège du SIGHUP envoyé à la fermeture du terminal
# de mise à jour ; « disown » n'existe pas sous /bin/sh (dash).
nohup "$PREFIX/bin/gtkterm" >/dev/null 2>&1 &
