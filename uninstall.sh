#!/usr/bin/env bash
# Disinstalla Texfix (macOS e Linux). Chiede pezzo per pezzo che cosa togliere e che cosa tenere, riepiloga e solo
# dopo la conferma esegue. Dei programmi esterni propone di togliere solo ciò che ha installato Texfix
# (annotato da install.sh in .installati), mai ciò che c'era già. I tuoi progetti non li tocca mai.
set -euo pipefail
cd "$(dirname "$0")"
ROOT=$PWD

say() { printf '\n\033[1;33m==> %s\033[0m\n' "$*"; }
ask() { # $1 domanda: s per sì, tutto il resto (anche solo Invio) per no
    local a=""
    printf '%s [s/N] ' "$1"
    read -r a || true
    case "$a" in s | S | si | Si | sì | Sì | y | Y | yes) return 0 ;; *) return 1 ;; esac
}
size() { du -shc "$@" 2>/dev/null | tail -n 1 | awk '{print $1}' || true; }
SUDO=""
[ "$(id -u)" -ne 0 ] && SUDO="sudo"

if pgrep -f "build/Texfix" >/dev/null 2>&1; then
    echo "Texfix è aperto: chiudilo e rilancia ./uninstall.sh"
    exit 1
fi

# Le scelte: che cosa togliere (descrizione, comando, riga di .installati da cancellare dopo) e che cosa resta
remove_what=() remove_cmd=() remove_line=() keep_what=()
plan_remove() { remove_what+=("$1"); remove_cmd+=("$2"); remove_line+=("${3:-}"); }
plan_keep() { keep_what+=("$1"); }
q() { printf '%q' "$1"; }

# ---------------------------------------------------------------- programmi installati da Texfix
say "Programmi che Texfix ha installato"
homebrew_line="" homebrew_entry=""
if [ -s .installati ]; then
    # Prima si legge tutto: dentro il ciclo le domande leggono le risposte dalla tastiera
    lines=()
    while IFS= read -r line; do [ -n "$line" ] && lines+=("$line"); done <.installati
    for line in "${lines[@]}"; do
        kind=${line%%$'\t'*} args=${line#*$'\t'}
        case $kind in
            homebrew) homebrew_line=$args homebrew_entry=$line ;; # per ultimo: dopo Homebrew non si potrebbero togliere i suoi pacchetti
            brew)
                d="Con Homebrew: $args"
                if ask "  $d. Lo tolgo?"; then plan_remove "$d" "brew uninstall $args" "$line"; else plan_keep "$d"; fi ;;
            cask)
                d="TeX (BasicTeX), con tutti i pacchetti LaTeX"
                if ask "  $d. Lo tolgo?"; then plan_remove "$d" "brew uninstall --cask $args" "$line"; else plan_keep "$d"; fi ;;
            tlmgr)
                d="Pacchetti LaTeX aggiunti a TeX: $args"
                if ask "  $d. Li tolgo? (servono solo a Texfix se il TeX lo usi per altro)"; then
                    plan_remove "$d" "if command -v tlmgr >/dev/null; then $SUDO tlmgr remove $args; fi" "$line"
                else plan_keep "$d"; fi ;;
            apt | dnf | pacman)
                d="Pacchetti di sistema ($kind): $args"
                case $kind in
                    apt) cmd="$SUDO apt-get remove -y $args" ;;
                    dnf) cmd="$SUDO dnf remove -y $args" ;;
                    pacman) cmd="$SUDO pacman -R --noconfirm $args" ;;
                esac
                if ask "  $d. Li tolgo? (attenzione se li usi anche per altro)"; then plan_remove "$d" "$cmd" "$line"; else plan_keep "$d"; fi ;;
            cartella)
                d="Qt e aqtinstall scaricati per Texfix ($args, $(size "$args"))"
                if ask "  $d. Li tolgo?"; then plan_remove "$d" "rm -rf $(q "$args")" "$line"; else plan_keep "$d"; fi ;;
            claude)
                d="Claude Code ($args)"
                if ask "  $d. Lo tolgo? (login e impostazioni in ~/.claude restano)"; then
                    plan_remove "$d" "rm -f $(q "$args"); rm -rf $(q "$HOME/.local/share/claude")" "$line"
                else plan_keep "$d"; fi ;;
        esac
    done
    if [ -n "$homebrew_line" ]; then
        d="Homebrew ($homebrew_line), con tutto ciò che ci hai installato, non solo per Texfix"
        if ask "  $d. Lo tolgo?"; then
            plan_remove "$d" '/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/uninstall.sh)"' "$homebrew_entry"
        else plan_keep "$d"; fi
    fi
else
    echo "  Nessuno: compilatore, CMake, Qt, TeX, Poppler e Claude Code c'erano già prima di Texfix (o li hai"
    echo "  installati prima che l'installer tenesse il conto). Non li tocco."
fi

# ---------------------------------------------------------------- le preferenze di Texfix
say "Preferenze, regole, stili e profili di partenza"
if [ "$(uname -s)" = Darwin ]; then
    data=("$HOME/Library/Application Support/Texfix/Texfix (nuovo)")
    for f in "$HOME"/Library/Preferences/*exfix*.plist; do [ -e "$f" ] && data+=("$f"); done
else
    data=("${XDG_DATA_HOME:-$HOME/.local/share}/Texfix/Texfix (nuovo)" "${XDG_CONFIG_HOME:-$HOME/.config}/Texfix/Texfix (nuovo).conf")
fi
present=()
for f in "${data[@]}"; do [ -e "$f" ] && present+=("$f"); done
if [ ${#present[@]} != 0 ]; then
    d="Le preferenze di Texfix, le regole cambiate, gli stili e i profili salvati ($(size "${present[@]}"))"
    cmd="rm -rf"
    for f in "${present[@]}"; do cmd="$cmd $(q "$f")"; done
    if ask "  $d. Li cancello? (non si recuperano)"; then plan_remove "$d" "$cmd"; else plan_keep "$d"; fi
else
    echo "  Nessuna: Texfix non è mai stato aperto, o non ha salvato niente."
fi
echo "  I tuoi progetti (le cartelle che hai aperto con Texfix) restano dove sono."

# ---------------------------------------------------------------- Texfix
say "Texfix"
if ask "  Tutta la cartella di Texfix ($ROOT, $(size .)): codice e programma compilato. La cancello?"; then
    plan_remove "Tutta la cartella di Texfix ($ROOT)" "cd / && rm -rf $(q "$ROOT")"
else
    plan_keep "La cartella di Texfix con il codice ($ROOT)"
    if [ -d build ]; then
        d="Il programma compilato (build/, $(size build)): si rifà con ./install.sh"
        if ask "  $d. Lo cancello?"; then plan_remove "$d" "rm -rf $(q "$ROOT/build")"; else plan_keep "$d"; fi
    fi
fi

# ---------------------------------------------------------------- riepilogo e conferma
say "Riepilogo"
if [ ${#remove_what[@]} = 0 ]; then
    echo "  Non hai scelto niente da togliere: non faccio nulla."
    exit 0
fi
echo "  Tolgo:"
for d in "${remove_what[@]}"; do echo "    - $d"; done
if [ ${#keep_what[@]} != 0 ]; then
    echo "  Tengo:"
    for d in "${keep_what[@]}"; do echo "    - $d"; done
fi
echo
if ! ask "Procedo?"; then
    echo "Non ho toccato niente."
    exit 0
fi

for i in "${!remove_cmd[@]}"; do
    say "${remove_what[$i]}"
    if bash -c "${remove_cmd[$i]}"; then
        # Tolto: non si ripropone la prossima volta
        if [ -n "${remove_line[$i]}" ] && [ -f "$ROOT/.installati" ]; then
            grep -vxF -- "${remove_line[$i]}" "$ROOT/.installati" >"$ROOT/.installati.tmp" || true
            mv "$ROOT/.installati.tmp" "$ROOT/.installati"
        fi
    else
        echo "  Non riuscito: lo puoi togliere a mano."
    fi
done
say "Fatto"
exit 0
