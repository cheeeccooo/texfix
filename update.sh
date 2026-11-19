#!/usr/bin/env bash
# Aggiorna Texfix (macOS e Linux): controlla su GitHub se c'è una versione nuova, dice cosa cambia e, se
# confermi, la scarica e la installa con install.sh (che controlla anche i requisiti nuovi).
# I tuoi progetti e le tue preferenze non stanno in questa cartella: l'aggiornamento non li tocca.
set -euo pipefail
cd "$(dirname "$0")"

say() { printf '\n\033[1;33m==> %s\033[0m\n' "$*"; }
ask() {
    local a=""
    printf '\n%s [s/N] ' "$1"
    read -r a || true
    case "$a" in s | S | si | Si | sì | Sì | y | Y | yes) return 0 ;; *) return 1 ;; esac
}

if ! command -v git >/dev/null 2>&1 || [ ! -d .git ]; then
    echo "Questa copia di Texfix non è un clone git, quindi non posso aggiornarla da qui."
    echo "Scarica di nuovo Texfix e lancia install.sh: i tuoi progetti e le preferenze stanno altrove e restano."
    exit 1
fi
branch=$(git rev-parse --abbrev-ref HEAD)
if [ "$branch" = HEAD ] || ! git rev-parse --abbrev-ref '@{u}' >/dev/null 2>&1; then
    echo "Il branch attuale ($branch) non segue un branch su GitHub: aggiorna a mano con git."
    exit 1
fi

say "Controllo se c'è una versione nuova"
if ! git fetch --quiet; then
    echo "Non riesco a contattare GitHub: controlla la connessione e riprova."
    exit 1
fi
behind=$(git rev-list --count 'HEAD..@{u}')
ahead=$(git rev-list --count '@{u}..HEAD')
current=$(git log -1 --format='%h del %ad' --date=format:'%d/%m/%Y')
if [ "$behind" = 0 ]; then
    echo "Hai già l'ultima versione ($current)."
    exit 0
fi
latest=$(git log -1 --format='%h del %ad' --date=format:'%d/%m/%Y' '@{u}')
echo "C'è una versione nuova: hai la $current, l'ultima è la $latest ($behind modifiche)."
echo
echo "Cosa cambia:"
git log -n 25 --format='  - %s' 'HEAD..@{u}' # (niente pipe: con pipefail head lo farebbe fallire)
[ "$behind" -gt 25 ] && echo "  ... e altre $((behind - 25))"

if [ "$ahead" != 0 ]; then
    echo
    echo "Hai $ahead commit tuoi che su GitHub non ci sono: per non perderli non aggiorno da qui."
    echo "Uniscili a mano con 'git pull' e poi lancia ./install.sh"
    exit 1
fi
if pgrep -f "build/Texfix" >/dev/null 2>&1; then
    echo
    echo "Texfix è aperto: chiudilo e rilancia ./update.sh"
    exit 1
fi
changed=$(git status --porcelain --untracked-files=no)
if [ -n "$changed" ]; then
    echo
    echo "Hai modificato questi file di Texfix:"
    echo "$changed" | sed 's/^/  /'
    echo "Prima di aggiornare li metto da parte con git stash: li ritrovi con 'git stash pop'."
fi
echo
echo "Poi l'installer controlla i requisiti (la versione nuova può chiederne altri), ti dice cosa farebbe e"
echo "ti chiede conferma prima di ricompilare."

if ! ask "Scarico la versione nuova?"; then
    echo "Non ho toccato niente."
    exit 0
fi
if [ -n "$changed" ]; then
    git stash push --quiet -m "Texfix: modifiche locali prima dell'aggiornamento del $(date +%d/%m/%Y)"
fi
git merge --ff-only --quiet '@{u}'
echo "Aggiornato alla $latest."

say "Installo la versione nuova"
exec ./install.sh
