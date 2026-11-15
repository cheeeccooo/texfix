#!/bin/sh
# Configura (la prima volta) e compila in build/.
#   ./build.sh                 compila
#   ./build.sh prove           compila e lancia le prove del motore
#   ./build.sh avvia [cartella] compila e apre il programma
set -e
cd "$(dirname "$0")"
[ -f build/CMakeCache.txt ] || cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_PREFIX_PATH="$(brew --prefix)"
# al massimo 4 processi: una build da zero con di più blocca un Mac da 8 GB
JOBS="${TEXFIX_JOBS:-$(n=$(sysctl -n hw.ncpu 2>/dev/null || echo 4); [ "$n" -gt 4 ] && echo 4 || echo "$n")}"
cmake --build build --parallel "$JOBS"
case "$1" in
    prove) exec build/texfix_tests ;;
    avvia) shift; exec build/Texfix.app/Contents/MacOS/Texfix "$@" ;;
esac
