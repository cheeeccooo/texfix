#!/usr/bin/env bash
# Installer di Texfix per macOS e Linux. Controlla che cosa c'è già (anche installato a mano o con un altro
# gestore di pacchetti), dice che cosa installerebbe e dove, e solo dopo la conferma installa ciò che manca,
# compila il programma e lancia le prove. Si può rilanciare quante volte si vuole.
set -euo pipefail
cd "$(dirname "$0")"

QT_VERSION=6.12.0
# aqtinstall al commit che conosce la disposizione dei file di Qt 6.11 e successivi (la 3.3.0 non la conosce)
AQT_SOURCE="git+https://github.com/miurahr/aqtinstall.git@5a49f2b391ae8ee1d3c0dd87f8b33c1d49a4deb7"
TOOLS="$PWD/.strumenti"

say() { printf '\n\033[1;33m==> %s\033[0m\n' "$*"; }
have() { command -v "$1" >/dev/null 2>&1; }
SUDO=""
[ "$(id -u)" -ne 0 ] && SUDO="sudo"

# Due giri sugli stessi passi: nel primo (plan=1) si descrivono, che cosa e dove; dopo la conferma si eseguono.
# Ciò che installa Texfix si annota in .installati (REC="tipo argomenti"): uninstall.sh propone di togliere
# solo quello, mai ciò che c'era già.
plan=1
steps=0
MANIFEST="$PWD/.installati"
step() { # $1 che cosa, $2 dove, poi il comando
    local what=$1 where=$2
    shift 2
    if [ "$plan" = 1 ]; then
        steps=$((steps + 1))
        printf '  %d. %s\n     \033[2min: %s\033[0m\n' "$steps" "$what" "$where"
    else
        say "$what"
        "$@" || { echo "Non riuscito: $what"; exit 1; }
        if [ -n "${REC:-}" ]; then printf '%s\t%s\n' "${REC%% *}" "${REC#* }" >>"$MANIFEST"; fi
    fi
    return 0
}

# Strumenti già installati ma fuori dal PATH di questo terminale (Homebrew, TeX Live, Claude Code)
add_path() { [ -d "$1" ] && case ":$PATH:" in *":$1:"*) ;; *) PATH="$PATH:$1" ;; esac; return 0; }
add_path "$HOME/.local/bin"
add_path /opt/homebrew/bin
add_path /usr/local/bin
add_path /Library/TeX/texbin
newest_tl=$(ls -d /usr/local/texlive/*/bin/* 2>/dev/null | sort -r | head -n 1 || true)
[ -n "$newest_tl" ] && add_path "$newest_tl"
export PATH

cxx_ok() { { have c++ || have g++ || have clang++; } && { have make || have ninja; }; }
cmake_ok() { # CMake 3.21 o più recente
    have cmake || return 1
    local v; v=$(cmake --version 2>/dev/null || true)
    [[ $v =~ ([0-9]+)\.([0-9]+) ]] || return 1
    [ "${BASH_REMATCH[1]}" -gt 3 ] || { [ "${BASH_REMATCH[1]}" -eq 3 ] && [ "${BASH_REMATCH[2]}" -ge 21 ]; }
}
# Qt 6.8 o più recente, con QtPdf e Qt Quick: quello scaricato in .strumenti/qt, di Homebrew o del sistema
local_qt() { ls -d "$TOOLS"/qt/*/macos/lib/cmake/Qt6Pdf "$TOOLS"/qt/*/gcc_64/lib/cmake/Qt6Pdf "$TOOLS"/qt/*/gcc_arm64/lib/cmake/Qt6Pdf 2>/dev/null | head -n 1; }
qt_version_ok() { # $1: una cartella lib/cmake con Qt6Core e Qt6Pdf
    local v
    [ -d "$1/Qt6Core" ] && [ -d "$1/Qt6Pdf" ] && [ -d "$1/Qt6QuickControls2" ] || return 1
    v=$(cat "$1"/Qt6Core/Qt6CoreConfigVersion*.cmake 2>/dev/null | sed -n 's/^ *set(PACKAGE_VERSION "\([0-9.]*\)").*/\1/p' | head -n 1)
    [[ $v =~ ^6\.([0-9]+) ]] && [ "${BASH_REMATCH[1]}" -ge 8 ]
}
qt_ok() {
    [ -n "$(local_qt)" ] && return 0
    local d
    for d in "$(brew --prefix 2>/dev/null || echo /nessuno)/lib/cmake" /usr/lib/cmake /usr/lib/*-linux-gnu/cmake /usr/lib64/cmake; do
        qt_version_ok "$d" && return 0
    done
    return 1
}
pdftoppm_ok() { have pdftoppm && have pdfunite && have pdfseparate; }
# File LaTeX che usano Texfix e lo stile «styles», ciascuno con il pacchetto TeX Live che lo contiene
TEX_FILES="amsthm.sty:amscls amssymb.sty:amsfonts mathtools.sty:mathtools cleveref.sty:cleveref hyperref.sty:hyperref
           italian.ldf:babel-italian tikz.sty:pgf pgfplots.sty:pgfplots varwidth.sty:varwidth preview.sty:preview
           tcolorbox.sty:tcolorbox environ.sty:environ trimspaces.sty:trimspaces tikzfill.image.sty:tikzfill pdfcol.sty:pdfcol
           enumitem.sty:enumitem titlesec.sty:titlesec fancyhdr.sty:fancyhdr stackengine.sty:stackengine
           listofitems.sty:listofitems cancel.sty:cancel contour.sty:contour truncate.sty:truncate microtype.sty:microtype
           caption.sty:caption float.sty:float bm.sty:tools raleway.sty:raleway plex-serif.sty:plex fontaxes.sty:fontaxes
           mweights.sty:mweights bookmark.sty:bookmark geometry.sty:geometry ecrm1000.tfm:ec"
tex_missing() { # pacchetti TeX Live che mancano, separati da spazi
    local out="" pair
    for pair in $TEX_FILES; do
        if ! have kpsewhich || [ -z "$(kpsewhich "${pair%%:*}" 2>/dev/null)" ]; then out="$out ${pair##*:}"; fi
    done
    have latexmk || out="$out latexmk"
    have synctex || out="$out synctex"
    echo "${out# }"
}
# TeX Live installato dal suo installer (MacTeX, BasicTeX, install-tl): i pacchetti si aggiungono con tlmgr.
# Quello delle distribuzioni Linux invece si aggiorna con il gestore di pacchetti.
tlmgr_root() {
    have tlmgr && have kpsewhich || return 1
    local root; root=$(kpsewhich -var-value SELFAUTOPARENT 2>/dev/null)
    [ -f "$root/tlpkg/texlive.tlpdb" ] && echo "$root"
}
tlmgr_add() { # $1: dove sta TeX Live (se non è ancora installato), poi i pacchetti
    local root s="" fallback=$1
    shift
    root=$(tlmgr_root) || root=""
    if [ -z "$root" ] && [ "$plan" = 0 ]; then return 1; fi
    if [ -n "$root" ] && [ ! -w "$root" ]; then s="$SUDO"; fi
    [ -z "$root" ] && s="$SUDO"
    # shellcheck disable=SC2086
    REC="tlmgr $*" step "Pacchetti LaTeX che mancano: $*" "${root:-$fallback}/texmf-dist (con tlmgr${s:+, serve la password})" \
        $s tlmgr install "$@"
}
# Qt dal server di Qt, con aqtinstall in un ambiente Python suo: tutto dentro .strumenti, niente nel sistema
qt_download() { # $1: architettura di aqt (clang_64, linux_gcc_64, linux_gcc_arm64), $2 host
    REC="cartella $TOOLS" step "Qt $QT_VERSION con QtPdf (circa 1 GB), scaricato con aqtinstall" "$TOOLS/qt" \
        bash -c "python3 -m venv '$TOOLS/aqt' && '$TOOLS/aqt/bin/pip' install --quiet '$AQT_SOURCE' &&
                 '$TOOLS/aqt/bin/aqt' install-qt $2 desktop $QT_VERSION $1 -m qtpdf -O '$TOOLS/qt'"
}

status() { if "$@" >/dev/null 2>&1; then printf '  \033[32mc'"'"'è\033[0m     %s\n' "$label"; else printf '  \033[31mmanca\033[0m   %s\n' "$label"; fi; }
show_status() {
    label="Compilatore C++" status cxx_ok
    label="CMake 3.21+" status cmake_ok
    label="Qt 6.8+ con QtPdf e Qt Quick" status qt_ok
    label="Git" status have git
    label="TeX (pdflatex)" status have pdflatex
    label="latexmk, synctex e pacchetti LaTeX (amsthm, tcolorbox, TikZ, ...)" status test -z "$(tex_missing)"
    label="Poppler (pdftoppm, pdfunite, pdfseparate)" status pdftoppm_ok
    label="Claude Code" status have claude
    [ "$(uname -s)" = Darwin ] || { label="pandoc (facoltativo: legge i .docx, .odt, .rtf)" status have pandoc; }
}

install_macos() {
    if ! xcode-select -p >/dev/null 2>&1; then
        step "Strumenti da riga di comando di Xcode (compilatore C++ e git)" \
            "/Library/Developer/CommandLineTools (si apre una finestra di macOS)" bash -c 'xcode-select --install || true'
        if [ "$plan" = 0 ]; then
            echo "Completa l'installazione nella finestra che si è aperta, poi rilancia ./install.sh"
            exit 1
        fi
    fi
    # Homebrew solo se serve per qualcosa che manca
    local brew_pkgs="" need_tex=0 prefix
    cmake_ok || brew_pkgs="$brew_pkgs cmake"
    qt_ok || brew_pkgs="$brew_pkgs qtbase qtdeclarative qtwebengine"
    pdftoppm_ok || brew_pkgs="$brew_pkgs poppler"
    have pdflatex || need_tex=1
    if have brew; then prefix=$(brew --prefix)
    elif [ "$(uname -m)" = arm64 ]; then prefix=/opt/homebrew
    else prefix=/usr/local
    fi
    if [ -n "$brew_pkgs" ] || [ "$need_tex" = 1 ]; then
        if ! have brew; then
            REC="homebrew $prefix" step "Homebrew (serve per installare il resto)" "$prefix" \
                bash -c '/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"'
            [ "$plan" = 0 ] && for b in /opt/homebrew/bin/brew /usr/local/bin/brew; do [ -x "$b" ] && eval "$("$b" shellenv)" && break; done
        fi
        # shellcheck disable=SC2086
        [ -n "$brew_pkgs" ] && REC="brew${brew_pkgs}" step "Con Homebrew:$brew_pkgs" "$prefix" brew install $brew_pkgs
        if [ "$need_tex" = 1 ]; then
            # BasicTeX (circa 100 MB) più i pacchetti che servono, invece dei 5 GB di MacTeX
            REC="cask basictex" step "TeX Live in versione BasicTeX (circa 100 MB, serve la password)" \
                "/usr/local/texlive/<anno>basic, comandi in /Library/TeX/texbin" \
                bash -c "brew install --cask basictex && $SUDO /Library/TeX/texbin/tlmgr update --self"
            add_path /Library/TeX/texbin
        fi
    fi
    local missing; missing=$(tex_missing)
    if [ -n "${missing// /}" ]; then
        # shellcheck disable=SC2086
        tlmgr_add "/usr/local/texlive/<anno>basic" $missing ||
            echo "Non riesco ad aggiungere a questo TeX i pacchetti: $missing. Installali con il gestore della tua distribuzione TeX."
    fi
    return 0
}

install_linux() {
    local mgr pkgs=""
    if have apt-get; then mgr=apt
    elif have dnf; then mgr=dnf
    elif have pacman; then mgr=pacman
    else mgr=""
    fi
    # Qt della distribuzione se è abbastanza nuovo (Debian 13, Ubuntu 25.04, Fedora, Arch); se no lo si scarica
    local qt_from=system
    if ! qt_ok && [ "$mgr" = apt ]; then
        local v; v=$(apt-cache policy qt6-base-dev 2>/dev/null | sed -n 's/^ *Candidate: *\([0-9]*:\)\{0,1\}\([0-9.]*\).*/\2/p')
        [[ $v =~ ^6\.([0-9]+) ]] && [ "${BASH_REMATCH[1]}" -ge 8 ] || qt_from=download
    fi
    pkg_for() { # $1: requisito
        case "$mgr:$1" in
            :cxx) echo compilatore-C++ ;; :cmake) echo CMake-3.21+ ;; :tex) echo TeX-Live ;; :poppler) echo Poppler ;;
            :qt) echo Qt-6.8+-con-QtPdf ;; :*) echo "$1" ;;
            apt:cxx) echo build-essential ;; dnf:cxx) echo "gcc-c++ make" ;; pacman:cxx) echo base-devel ;;
            *:cmake) echo cmake ;; *:git) echo git ;; *:curl) echo curl ;; *:pandoc) echo pandoc ;;
            apt:poppler | dnf:poppler) echo poppler-utils ;; pacman:poppler) echo poppler ;;
            apt:qt) echo "qt6-base-dev qt6-declarative-dev qt6-pdf-dev libgl-dev qml6-module-qtquick qml6-module-qtquick-controls
                         qml6-module-qtquick-layouts qml6-module-qtquick-templates qml6-module-qtquick-dialogs qml6-module-qtquick-effects
                         qml6-module-qtquick-window qml6-module-qtqml-models qml6-module-qtqml-workerscript qml6-module-qtcore" ;;
            dnf:qt) echo "qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtpdf-devel" ;;
            pacman:qt) echo "qt6-base qt6-declarative qt6-webengine" ;;
            # per il Qt scaricato: Python per aqtinstall e le librerie che il suo xcb vuole
            apt:qtdownload) echo "python3-venv libgl-dev libegl1 libxkbcommon-x11-0 libxcb-cursor0 libxcb-icccm4 libxcb-image0
                                  libxcb-keysyms1 libxcb-randr0 libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 libxcb-xkb1
                                  libfontconfig1 libdbus-1-3 libnss3" ;;
            apt:tex) echo "texlive-latex-extra texlive-pictures texlive-fonts-extra texlive-lang-italian texlive-extra-utils latexmk" ;;
            dnf:tex) echo "texlive-scheme-medium latexmk" ;;
            pacman:tex) echo "texlive-basic texlive-latex texlive-latexrecommended texlive-latexextra texlive-pictures texlive-fontsrecommended texlive-fontsextra texlive-langitalian texlive-binextra" ;;
        esac
    }
    cxx_ok || pkgs="$pkgs $(pkg_for cxx)"
    cmake_ok || pkgs="$pkgs $(pkg_for cmake)"
    have git || pkgs="$pkgs $(pkg_for git)"
    have curl || have claude || pkgs="$pkgs $(pkg_for curl)" # per l'installer di Claude Code
    pdftoppm_ok || pkgs="$pkgs $(pkg_for poppler)"
    have pandoc || pkgs="$pkgs $(pkg_for pandoc)"
    if ! qt_ok; then
        if [ "$qt_from" = system ]; then pkgs="$pkgs $(pkg_for qt)"; else pkgs="$pkgs $(pkg_for qtdownload)"; fi
    fi
    have pdflatex || pkgs="$pkgs $(pkg_for tex)"

    # Pacchetti LaTeX singoli che mancano a un TeX già presente
    local tex_pkgs=""
    if have pdflatex; then
        tex_pkgs=$(tex_missing)
        if [ -n "${tex_pkgs// /}" ] && ! tlmgr_root >/dev/null; then
            # TeX della distribuzione: i pacchetti LaTeX arrivano dal gestore di sistema
            pkgs="$pkgs $(pkg_for tex)"
            tex_pkgs=""
        fi
    fi

    # shellcheck disable=SC2086
    pkgs=$(echo $pkgs | tr ' ' '\n' | awk 'NF && !seen[$0]++' | tr '\n' ' ')
    pkgs=${pkgs% }
    if [ -n "$pkgs" ]; then
        if [ -z "$mgr" ]; then
            echo "  Gestore di pacchetti non riconosciuto: installa a mano ciò che manca ($pkgs),"
            echo "  poi rilancia ./install.sh"
            exit 1
        fi
        case $mgr in
            apt) REC="apt $pkgs" step "Pacchetti di sistema (apt): $pkgs" "/usr (con sudo)" bash -c "$SUDO apt-get update && $SUDO apt-get install -y $pkgs" ;;
            dnf) REC="dnf $pkgs" step "Pacchetti di sistema (dnf): $pkgs" "/usr (con sudo)" bash -c "$SUDO dnf install -y $pkgs" ;;
            pacman) REC="pacman $pkgs" step "Pacchetti di sistema (pacman): $pkgs" "/usr (con sudo)" bash -c "$SUDO pacman -S --needed --noconfirm $pkgs" ;;
        esac
    fi
    if ! qt_ok && [ "$qt_from" = download ]; then
        case "$(uname -m)" in
            aarch64 | arm64) qt_download linux_gcc_arm64 linux_arm64 ;;
            *) qt_download linux_gcc_64 linux ;;
        esac
    fi
    # shellcheck disable=SC2086
    if [ -n "${tex_pkgs// /}" ]; then tlmgr_add /usr/local/texlive $tex_pkgs; fi
    if [ "$plan" = 0 ] && ! cmake_ok; then
        echo "La CMake del sistema è più vecchia della 3.21: installane una recente (per esempio 'pip install --user cmake')"
        echo "e rilancia ./install.sh"
        exit 1
    fi
    return 0
}

install_all() {
    case "$(uname -s)" in
        Darwin) install_macos ;;
        Linux) install_linux ;;
        *) echo "Sistema non supportato da questo script: su Windows usa install.bat"; exit 1 ;;
    esac
    if ! have claude; then
        REC="claude $HOME/.local/bin/claude" step "Claude Code (installer ufficiale)" "$HOME/.local/bin/claude" bash -c 'curl -fsSL https://claude.ai/install.sh | bash'
        add_path "$HOME/.local/bin"
    fi
}

say "Controllo che cosa c'è già"
show_status

say "Che cosa farei"
install_all
[ "$steps" = 0 ] && echo "  Non manca niente: non installo nulla."
echo "  $((steps + 1)). Compilo Texfix e lancio le prove del motore (qualche minuto)"
printf '     \033[2min: %s/build\033[0m\n' "$PWD"

printf '\nProcedo? [s/N] '
answer=""
read -r answer || true
case "$answer" in
    s | S | si | Si | sì | Sì | y | Y | yes) ;;
    *) echo "Non ho toccato niente."; exit 0 ;;
esac

plan=0
install_all

say "Compilo Texfix"
# Qt appena arrivato: la configurazione di prima puntava altrove
if [ -n "$(local_qt)" ] && [ -f build/CMakeCache.txt ] && ! grep -q "$TOOLS/qt" build/CMakeCache.txt; then rm -f build/CMakeCache.txt; fi
./build.sh
say "Le prove del motore"
build/texfix_tests -silent || echo "Qualche prova non è passata: il programma si apre lo stesso, ma dimmelo."

say "Riepilogo"
show_status
echo
echo "Per aprire Texfix: ./avvia.sh [cartella]"
echo "Se non l'hai mai fatto, apri un terminale e lancia 'claude' una volta per fare il login."
