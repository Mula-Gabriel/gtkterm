#!/bin/sh
# Publie une version de GTKTerm : vérifie l'arbre de travail, meson.build et NEWS, puis crée le tag annoté vX.Y.Z sur HEAD. Ne pousse rien.
# Usage : tools/publier-version.sh vX.Y.Z
set -eu

TAG="${1:-}"

die() {
	echo "publier-version : $*" >&2
	exit 1
}

case "$TAG" in
	''|*[!v0-9.]*) die "version invalide « $TAG » (attendu vX.Y.Z)" ;;
esac
printf '%s\n' "$TAG" | grep -Eqx 'v(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})' || die "version invalide « $TAG » (attendu vX.Y.Z)"
cd "$(git rev-parse --show-toplevel)"
[ -z "$(git status --porcelain)" ] || die "l'arbre de travail n'est pas propre"
if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
	die "le tag $TAG existe déjà"
fi
VERSION=$(sed -n "s/^[[:space:]]*version[[:space:]]*:[[:space:]]*'\([^']*\)'.*/\1/p" meson.build | head -n 1)
[ "$VERSION" = "${TAG#v}" ] || die "meson.build porte la version « $VERSION », pas « ${TAG#v} »"
MOTIF=$(printf '%s' "$TAG" | sed 's/\./\\./g')
grep -Eq "^## $MOTIF([[:space:]]|\$)" NEWS || die "NEWS n'a pas de section « ## $TAG »"
# Le script de mise à jour ne doit passer à meson que des options déclarées (sinon « Unknown options » et aucune installation possible)
if [ -r data/gtkterm-update.sh ]; then
	for opt in $(grep -o -e '-D[A-Za-z0-9_]*=' data/gtkterm-update.sh | sed 's/^-D//; s/=$//' | sort -u); do
		grep -Eq "^[[:space:]]*option[[:space:]]*\\([[:space:]]*'$opt'" meson_options.txt 2>/dev/null ||
			die "data/gtkterm-update.sh passe -D$opt, absent de meson_options.txt"
	done
fi
git tag -a "$TAG" -m "GTKTerm $TAG"
echo "Tag $TAG créé sur $(git rev-parse --short HEAD)."
echo "À pousser (sur feu vert) : git push origin $TAG && git push Maison $TAG"
