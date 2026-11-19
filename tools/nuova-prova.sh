#!/bin/sh
# Ricrea da zero il progetto di prova della nuova Texfix (../prova-texfix-nuovo, o la cartella data): un progetto
# vuoto con lo stile «styles» della libreria.
#   tools/nuova-prova.sh [cartella]
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
P="${1:-$(dirname "$HERE")/prova-texfix-nuovo}"
case "$P" in *prova*) ;; *) echo "per prudenza la cartella deve contenere «prova» nel nome: $P"; exit 1;; esac
[ -x "$HERE/build/Texfix.app/Contents/MacOS/Texfix" ] || "$HERE/build.sh"
rm -rf "$P"
GESTI="nuovo $P; libreria styles"
# preferenze a parte: il progetto di prova non entra fra i recenti veri
TEXFIX_PREFERENZE="${TMPDIR:-/tmp}/texfix-prova.ini" "$HERE/build/Texfix.app/Contents/MacOS/Texfix" --foto "${TMPDIR:-/tmp}/texfix-prova.png" --gesti "$GESTI" >/dev/null
echo "progetto di prova pronto in $P"
