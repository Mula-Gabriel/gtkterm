#!/bin/sh
# Test de tools/publier-version.sh sur un dépôt jetable (sortie TAP). Usage : sh tests/test_publier_version.sh <chemin de tools/publier-version.sh>
set -eu
OUTIL=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1 GIT_AUTHOR_NAME=T GIT_AUTHOR_EMAIL=t@example.invalid GIT_COMMITTER_NAME=T GIT_COMMITTER_EMAIL=t@example.invalid
n=0
fails=0
ok() { n=$((n + 1)); echo "ok $n - $1"; }
ko() { n=$((n + 1)); echo "not ok $n - $1"; fails=$((fails + 1)); }
check() { d="$1"; shift; if "$@"; then ok "$d"; else ko "$d"; fi; }
refuse() { d="$1"; shift; if (cd "$T/d" && sh "$OUTIL" "$@" >/dev/null 2>&1); then ko "$d"; else ok "$d"; fi; }

mkdir -p "$T/d"
cd "$T/d"
git init -q
printf "project(\n\t'gtkterm',\n\t'c',\n\tversion: '1.3.2',\n\tmeson_version : '>= 1.0.0'\n)\n" > meson.build
mkdir data
printf 'meson setup -Dtests=false build\n' > data/gtkterm-update.sh
printf "option('tests', type : 'boolean', value : false)\n" > meson_options.txt
printf '# Nouveautés\n\n## v1.3.2 — 2026-10-12\n- un\n' > NEWS
git add -A
git commit -qm un

refuse "nom invalide" 1.3.2
refuse "nom invalide (v1.3)" v1.3
refuse "version différente de meson.build" v1.3.3
echo sale > sale.txt
refuse "arbre sale" v1.3.2
rm sale.txt
sed -i 's/## v1.3.2/## v1.3.1/' NEWS
git commit -qam "notes"
refuse "section NEWS absente" v1.3.2
sed -i 's/## v1.3.1/## v1.3.2/' NEWS
git commit -qam "notes"
git rm -q --cached meson_options.txt
mv meson_options.txt "$T/meson_options.sauve"
git commit -qm "sans option"
refuse "option -D non déclarée dans meson_options.txt" v1.3.2
mv "$T/meson_options.sauve" meson_options.txt
git add meson_options.txt
git commit -qm "avec option"
check "publication acceptée" sh -c 'sh "$1" v1.3.2 >/dev/null' _ "$OUTIL"
check "tag annoté créé" [ "$(git cat-file -t v1.3.2)" = tag ]
check "message du tag" [ "$(git tag -l --format='%(contents:subject)' v1.3.2)" = "GTKTerm v1.3.2" ]
refuse "tag déjà présent" v1.3.2
check "rien n'est poussé (aucun remote touché)" [ -z "$(git remote)" ]

echo "1..$n"
[ "$fails" -eq 0 ]
