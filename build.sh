#!/bin/sh
# Configura (la prima volta) e compila in build/. macOS e Linux; su Windows install.bat e avvia.bat.
#   ./build.sh                 compila
#   ./build.sh prove           compila e lancia le prove del motore
#   ./build.sh misure [--scrivi] compila e misura i tempi di ciò che l'interfaccia rifà spesso
#   ./build.sh avvia [cartella] compila e apre il programma
set -e
cd "$(dirname "$0")"
# Dove sta Qt: quello scaricato da install.sh in .strumenti/qt, o quello di Homebrew, o quello del sistema
qt_prefix() {
    for d in .strumenti/qt/*/macos .strumenti/qt/*/gcc_64 .strumenti/qt/*/gcc_arm64; do
        [ -d "$d/lib/cmake/Qt6" ] && { echo "$PWD/$d"; return; }
    done
    command -v brew >/dev/null 2>&1 && brew --prefix
    return 0
}
if [ ! -f build/CMakeCache.txt ]; then
    prefix=$(qt_prefix)
    cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo ${prefix:+-DCMAKE_PREFIX_PATH="$prefix"}
fi
# al massimo 4 processi: una build da zero con di più blocca un Mac da 8 GB
cpus=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
JOBS="${TEXFIX_JOBS:-$([ "$cpus" -gt 4 ] && echo 4 || echo "$cpus")}"
cmake --build build --parallel "$JOBS"
if [ -x build/Texfix.app/Contents/MacOS/Texfix ]; then APP=build/Texfix.app/Contents/MacOS/Texfix; else APP=build/Texfix; fi
case "$1" in
    prove) exec build/texfix_tests ;;
    misure) shift; exec build/texfix_bench "$@" ;;
    avvia) shift; exec "$APP" "$@" ;;
esac
