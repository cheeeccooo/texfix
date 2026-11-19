#!/usr/bin/env bash
# Ricompila se serve (in pochi secondi) e apre Texfix, con la cartella di un progetto se la dai.
set -euo pipefail
args=()
for a in "$@"; do if [ -e "$a" ]; then args+=("$(cd "$(dirname "$a")" && pwd)/$(basename "$a")"); else args+=("$a"); fi; done
cd "$(dirname "$0")"
[ -f build/CMakeCache.txt ] || { echo "Prima lancia ./install.sh"; exit 1; }
./build.sh >/dev/null
if [ -d build/Texfix.app ]; then exec build/Texfix.app/Contents/MacOS/Texfix ${args[@]+"${args[@]}"}; fi
exec build/Texfix ${args[@]+"${args[@]}"}
