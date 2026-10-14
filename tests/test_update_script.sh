#!/bin/sh
# Test du script de mise à jour (data/gtkterm-update.sh), sans réseau, sans root, sans aucune demande de mot de passe : dépôt amont local à versions
# taguées (petit projet meson qui installe des fichiers), préfixe et cache temporaires, pkexec remplacé par un compteur qui exécute sans privilège,
# dépendances sautées.
# Usage : sh tests/test_update_script.sh <chemin de data/gtkterm-update.sh>   (sortie TAP)
set -eu
[ $# -eq 1 ] || { echo "usage: $0 <gtkterm-update.sh>" >&2; exit 2; }
SCRIPT=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
# Garde-fou : un script sans échappatoire de test appellerait pkexec (demande de mot de passe) ; on refuse de le lancer.
grep -q 'GTKTERM_UPDATE_SUDO' "$SCRIPT" || { echo "Bail out! $SCRIPT n'a pas l'échappatoire GTKTERM_UPDATE_SUDO : refus de le lancer"; exit 1; }
REPO=$(cd "$(dirname "$SCRIPT")/.." && pwd)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
# Compteur d'autorisations : remplace pkexec, journalise puis exécute la commande sans privilège
printf '#!/bin/sh\necho autorisation >> "%s/autorisations.log"\nexec "$@"\n' "$T" > "$T/faux-pkexec"
chmod +x "$T/faux-pkexec"
# Refus d'autorisation simulé (« Annuler ») : journalise, n'exécute rien, échoue comme pkexec (126)
printf '#!/bin/sh\necho refus >> "%s/autorisations.log"\nexit 126\n' "$T" > "$T/refus-pkexec"
chmod +x "$T/refus-pkexec"
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1 XDG_CACHE_HOME="$T/cache" GTKTERM_UPDATE_SUDO="$T/faux-pkexec" GTKTERM_UPDATE_DEPS=0 HOME="$T/home"
mkdir -p "$HOME"
PREFIX="$T/prefix"
UP="$T/amont"
URL="file://$UP"
DATA="$PREFIX/share/faux"
REG="$XDG_CACHE_HOME/gtkterm/installed-files.txt"
LOG="$T/autorisations.log"
LOCKDIR="$XDG_CACHE_HOME/gtkterm/update.lock"
n=0
fails=0
ok() { n=$((n + 1)); echo "ok $n - $1"; }
ko() { n=$((n + 1)); echo "not ok $n - $1"; fails=$((fails + 1)); }
check() { d="$1"; shift; if "$@"; then ok "$d"; else ko "$d"; fi; }
autorisations() { if [ -f "$LOG" ]; then wc -l < "$LOG" | tr -d ' '; else echo 0; fi; }

# version <tag> <variante du script, vide : copie exacte> <fichiers installés…>
version() {
	tag="$1"; variante="$2"; shift 2
	rm -rf "$UP/files"
	mkdir -p "$UP/files" "$UP/data" "$UP/bin"
	{
		echo "project('faux', version : '${tag#v}')"
		printf 'install_data('
		for f in "$@"; do printf "'files/%s', " "$f"; done
		echo "install_dir : get_option('datadir') / 'faux')"
		echo "install_data('bin/gtkterm', install_dir : get_option('bindir'), install_mode : 'rwxr-xr-x')"
	} > "$UP/meson.build"
	cp "$REPO/meson_options.txt" "$UP/meson_options.txt"
	[ -z "${CASSE:-}" ] || echo "ceci n'est pas du meson (" >> "$UP/meson.build"
	for f in "$@"; do echo "$tag" > "$UP/files/$f"; done
	printf '#!/bin/sh\nenv > "%s/gtkterm-env.txt"\nexit 0\n' "$T" > "$UP/bin/gtkterm"
	chmod +x "$UP/bin/gtkterm"
	cp "$SCRIPT" "$UP/data/gtkterm-update.sh"
	[ -z "$variante" ] || echo "# variante $variante" >> "$UP/data/gtkterm-update.sh"
	git -C "$UP" add -A
	git -C "$UP" -c user.name=T -c user.email=t@example.invalid commit -qm "$tag"
	git -C "$UP" -c user.name=T -c user.email=t@example.invalid tag -a "$tag" -m "$tag"
}

runslash() { sh "$SCRIPT" "$URL" "$PREFIX/" "" "$1" > "$T/sortie.txt" 2>&1; }
attend_env() { i=0; while [ ! -s "$T/gtkterm-env.txt" ] && [ "$i" -lt 50 ]; do sleep 0.1; i=$((i + 1)); done; [ -s "$T/gtkterm-env.txt" ]; }
run() { sh "$SCRIPT" "$URL" "$PREFIX" "" "$1" > "$T/sortie.txt" 2>&1; }
contient() { grep -Fq -- "$1" "$T/sortie.txt"; }
vaut() { [ -f "$1" ] && [ "$(cat "$1")" = "$2" ]; }
absent() { [ ! -e "$1" ]; }
une_autorisation() { avant="$1"; [ "$(autorisations)" -eq $((avant + 1)) ]; }

mkdir -p "$UP"
git -C "$UP" init -q
version v1.0.0 "" a.txt ancien.txt
version v1.1.0 "1.1" a.txt b.txt
git -C "$UP" tag essai

# 1. Installation de v1.0.0 (copie de travail neuve)
a=$(autorisations)
check "installation v1.0.0" run v1.0.0
check "une seule autorisation pour v1.0.0" une_autorisation "$a"
check "a.txt de v1.0.0" vaut "$DATA/a.txt" v1.0.0
check "ancien.txt installé" vaut "$DATA/ancien.txt" v1.0.0
check "registre créé" grep -Fqx "$DATA/ancien.txt" "$REG"
check "verrou retiré après la mise à jour" absent "$LOCKDIR"
check "build/ appartient à l'utilisateur" [ -O "$XDG_CACHE_HOME/gtkterm/src/build/build.ninja" ]

# 2. Montée en v1.1.0 : relance sur le script de v1.1.0, ménage de ancien.txt, toujours une seule autorisation
attend_env || true
rm -f "$T/gtkterm-env.txt"
a=$(autorisations)
check "installation v1.1.0" run v1.1.0
check "une seule autorisation pour v1.1.0 (relance comprise)" une_autorisation "$a"
check "relance sur le script de la version visée" contient "Continuing with the update script of v1.1.0"
check "b.txt installé" vaut "$DATA/b.txt" v1.1.0
check "a.txt de v1.1.0" vaut "$DATA/a.txt" v1.1.0
check "ancien.txt retiré" absent "$DATA/ancien.txt"
check "message de retrait" contient "Removing $DATA/ancien.txt"
check "GTKTerm relancé" attend_env
check "GTKTERM_UPDATE_REEXEC non transmis au GTKTerm relancé" sh -c '! grep -q GTKTERM_UPDATE_REEXEC "$1"' sh "$T/gtkterm-env.txt"

# 3. Retour à v1.0.0 : b.txt retiré, ancien.txt revenu
check "retour v1.0.0 (préfixe avec barre finale)" runslash v1.0.0
check "b.txt retiré au retour" absent "$DATA/b.txt"
check "ancien.txt revenu" vaut "$DATA/ancien.txt" v1.0.0
check "copie de travail sur v1.0.0" [ "$(git -C "$XDG_CACHE_HOME/gtkterm/src" describe --tags --exact-match)" = v1.0.0 ]

# 4. Tags refusés, tag absent, URL refusée : arrêt sans rien installer ni demander d'autorisation
a=$(autorisations)
for mauvais in "" 1.0.0 v1.0 "v1.0.0;touch $T/pirate" "v01.0.0" "essai" "-v1.0.0"; do
	check "tag refusé « $mauvais »" sh -c '! sh "$1" "$2" "$3" "" "$4" >/dev/null 2>&1' sh "$SCRIPT" "$URL" "$PREFIX" "$mauvais"
done
check "aucune commande injectée" absent "$T/pirate"
check "tag absent de la source" sh -c '! sh "$1" "$2" "$3" "" v9.9.9 > "$4" 2>&1 && grep -q "not found" "$4"' sh "$SCRIPT" "$URL" "$PREFIX" "$T/s.txt"
check "URL en forme d'option refusée" sh -c '! sh "$1" "--upload-pack=touch $2" "$3" "" v1.0.0 >/dev/null 2>&1' sh "$SCRIPT" "$T/pirate2" "$PREFIX"
check "aucune commande injectée par l'URL" absent "$T/pirate2"
check "aucune autorisation demandée pour un refus" [ "$(autorisations)" -eq "$a" ]

# 5. Registre altéré : chemin hors du préfixe, chemin avec « .. », dossier : jamais supprimés
echo dehors > "$T/dehors.txt"
mkdir -p "$PREFIX/share/dossier"
{ echo "$T/dehors.txt"; echo "$PREFIX/../dehors.txt"; echo "$PREFIX/share/dossier"; } >> "$REG"
check "réinstallation avec registre altéré" run v1.0.0
check "fichier hors préfixe gardé" vaut "$T/dehors.txt" dehors
check "dossier gardé" [ -d "$PREFIX/share/dossier" ]

# 6. Mise à jour interrompue (build/ supprimé, registre présent) puis relancée : ménage d'après le registre
rm -rf "$XDG_CACHE_HOME/gtkterm/src/build"
check "relance après interruption (v1.1.0)" run v1.1.0
check "ancien.txt retiré d'après le registre" absent "$DATA/ancien.txt"

# 7. Sans registre ni journal (installation d'avant le registre) : ancien lanceur retiré s'il n'est pas fourni par la version installée
rm -f "$REG"
rm -rf "$XDG_CACHE_HOME/gtkterm/src/build"
mkdir -p "$PREFIX/share/applications"
echo ancien > "$PREFIX/share/applications/gtkterm.desktop"
check "installation sans registre" run v1.0.0
check "ancien lanceur retiré" absent "$PREFIX/share/applications/gtkterm.desktop"
check "registre recréé" [ -s "$REG" ]

# 8. Autorisation refusée (« Annuler ») : arrêt immédiat, message clair, rien d'installé, aucune nouvelle demande
# b.txt (v1.1.0 du cas 6) reste après le cas 7 faute de registre : on le retire pour ne juger que la version refusée
rm -f "$DATA/b.txt"
a=$(autorisations)
check "refus : le script échoue" sh -c '! GTKTERM_UPDATE_SUDO="$1" sh "$2" "$3" "$4" "" v1.1.0 > "$5" 2>&1' sh "$T/refus-pkexec" "$SCRIPT" "$URL" "$PREFIX" "$T/sortie.txt"
check "refus : une seule demande, jamais répétée" une_autorisation "$a"
check "refus : message" contient "GTKTerm was not updated"
check "refus : version en place intacte" vaut "$DATA/a.txt" v1.0.0
check "refus : rien de la version refusée" absent "$DATA/b.txt"

# 9. Interruption entre l'installation et la copie du registre (installe puis échoue) : message d'échec, puis la mise à jour suivante retire b.txt
printf '#!/bin/sh\necho autorisation >> "%s/autorisations.log"\n"$@"\nexit 1\n' "$T" > "$T/installe-puis-echoue"
chmod +x "$T/installe-puis-echoue"
check "installe puis échoue : le script échoue" sh -c '! GTKTERM_UPDATE_SUDO="$1" sh "$2" "$3" "$4" "" v1.1.0 > "$5" 2>&1' sh "$T/installe-puis-echoue" "$SCRIPT" "$URL" "$PREFIX" "$T/sortie.txt"
check "installe puis échoue : message d'échec" contient "Update failed (see the messages above)"
check "installe puis échoue : b.txt installé" vaut "$DATA/b.txt" v1.1.0
check "retour v1.0.0 après l'interruption" run v1.0.0
check "b.txt retiré grâce à la fusion du journal" absent "$DATA/b.txt"

# 10. Construction qui échoue (meson.build invalide) : message « avant l'installation », rien d'installé, verrou retiré
CASSE=1 version v1.2.0 "" a.txt c.txt
a=$(autorisations)
check "construction cassée : le script échoue" sh -c '! sh "$1" "$2" "$3" "" v1.2.0 > "$4" 2>&1' sh "$SCRIPT" "$URL" "$PREFIX" "$T/sortie.txt"
check "construction cassée : message avant installation" contient "Update failed before installation"
check "construction cassée : pas de « partiellement »" sh -c '! grep -q "partially" "$1"' sh "$T/sortie.txt"
check "construction cassée : rien d'installé" absent "$DATA/c.txt"
check "construction cassée : version en place intacte" vaut "$DATA/a.txt" v1.0.0
check "construction cassée : verrou retiré" absent "$LOCKDIR"

# 11. Verrou. Un verrou tenu par un processus vivant bloque la mise à jour, sans demande d'autorisation, et n'est jamais retiré par la perdante
sleep 30 &
vivant=$!
mkdir "$LOCKDIR"
echo "$vivant" > "$LOCKDIR/pid"
a=$(autorisations)
check "verrou vivant : le script échoue" sh -c '! sh "$1" "$2" "$3" "" v1.0.0 > "$4" 2>&1' sh "$SCRIPT" "$URL" "$PREFIX" "$T/sortie.txt"
check "verrou vivant : message" contient "Another GTKTerm update is already running"
check "verrou vivant : aucune autorisation" [ "$(autorisations)" -eq "$a" ]
check "verrou vivant : gardé" [ -d "$LOCKDIR" ]
# Relance (GTKTERM_UPDATE_REEXEC) : le verrou d'une autre mise à jour n'est pas repris non plus
check "relance sur le verrou d'un autre : refusée" sh -c '! GTKTERM_UPDATE_REEXEC=1 sh "$1" "$2" "$3" "" v1.0.0 > "$4" 2>&1' sh "$SCRIPT" "$URL" "$PREFIX" "$T/sortie.txt"
check "relance sur le verrou d'un autre : gardé" [ -d "$LOCKDIR" ]
kill "$vivant" 2>/dev/null || true
wait "$vivant" 2>/dev/null || true
rm -rf "$LOCKDIR"

# 11b. Verrou périmé (processus mort, ou PID absent) : retiré puis repris, la mise à jour réussit
sh -c 'exit 0' &
mort=$!
wait "$mort" || true
mkdir "$LOCKDIR"
echo "$mort" > "$LOCKDIR/pid"
check "verrou d'un processus mort : mise à jour réussie" run v1.0.0
check "verrou périmé : message" contient "Removing a stale update lock"
check "verrou périmé : retiré à la fin" absent "$LOCKDIR"
mkdir "$LOCKDIR"
check "verrou sans PID : mise à jour réussie" run v1.0.0
check "verrou sans PID : message" contient "Removing a stale update lock"
# Sortie anticipée en mode relance : le verrou périmé repris n'est pas laissé
mkdir "$LOCKDIR"
echo "$mort" > "$LOCKDIR/pid"
check "relance, tag refusé : échec" sh -c '! GTKTERM_UPDATE_REEXEC=1 sh "$1" "$2" "$3" "" "essai" > "$4" 2>&1' sh "$SCRIPT" "$URL" "$PREFIX" "$T/sortie.txt"
check "relance, tag refusé : verrou libéré" absent "$LOCKDIR"

# 11c. SIGHUP en cours de mise à jour (fenêtre du terminal fermée) : le verrou est libéré. L'autorisation factice attend (sleep) que le test envoie HUP
printf '#!/bin/sh\necho $$ > "%s/lent.pid"\nexec sleep 30\n' "$T" > "$T/lent-pkexec"
chmod +x "$T/lent-pkexec"
rm -f "$T/lent.pid"
GTKTERM_UPDATE_SUDO="$T/lent-pkexec" sh "$SCRIPT" "$URL" "$PREFIX" "" v1.0.0 > "$T/sortie.txt" 2>&1 &
sp=$!
i=0; while [ ! -s "$T/lent.pid" ] && [ "$i" -lt 100 ]; do sleep 0.1; i=$((i + 1)); done
check "SIGHUP : le script tient le verrou en attendant l'autorisation" [ "$(cat "$LOCKDIR/pid" 2>/dev/null)" = "$sp" ]
kill -HUP "$sp" 2>/dev/null || true
kill "$(cat "$T/lent.pid")" 2>/dev/null || true
wait "$sp" 2>/dev/null || true
check "SIGHUP : verrou libéré" absent "$LOCKDIR"

# 12. Chaque -D<nom>= passé à meson par le script est déclaré dans le vrai meson_options.txt (sinon « Unknown options » : aucune installation possible)
for opt in $(grep -o -e '-D[A-Za-z0-9_]*=' "$SCRIPT" | sed 's/^-D//; s/=$//' | sort -u); do
	check "option -D$opt déclarée dans meson_options.txt" grep -Eq "^[[:space:]]*option[[:space:]]*\\([[:space:]]*'$opt'" "$REPO/meson_options.txt"
done

echo "1..$n"
[ "$fails" -eq 0 ]
