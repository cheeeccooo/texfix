# Texfix

Appunti di un corso in LaTeX, da scansioni, PDF e file di altri autori: Texfix li converte in lezioni, le impagina, ne
tiene i dubbi e le correzioni, con un agente (Claude Code) che lavora dentro il progetto. Che cosa fa: [SPEC.md](SPEC.md);
com'è fatto il codice: [CLAUDE.md](CLAUDE.md).

## Installare

Ogni installer dice prima che cosa c'è già, che cosa installerebbe e dove; tocca qualcosa solo dopo la conferma. Si può
rilanciare quando si vuole, e annota in `.installati` ciò che ha messo, così la disinstallazione toglie solo quello.

| | macOS | Linux | Windows |
|---|---|---|---|
| Installare | `./install.sh` | `./install.sh` | doppio clic su `install.bat` |
| Aprire | `./avvia.sh [cartella]` | `./avvia.sh [cartella]` | doppio clic su `avvia.bat` |
| Aggiornare | `./update.sh` | `./update.sh` | `update.bat` |
| Disinstallare | `./uninstall.sh` | `./uninstall.sh` | `uninstall.bat` |

Che cosa serve, e da dove arriva se manca:

| | macOS | Linux | Windows |
|---|---|---|---|
| Compilatore C++, CMake | strumenti di Xcode, Homebrew | apt, dnf o pacman | Visual Studio Build Tools (winget) |
| Qt 6.8+ con QtPdf | Homebrew | la distribuzione se è abbastanza nuova (Debian 13, Ubuntu 25.04, Fedora, Arch), se no Qt 6.12 scaricato in `.strumenti/` | Qt 6.12 scaricato in `.strumenti\` |
| TeX (latexmk, synctex, i pacchetti dello stile) | BasicTeX e tlmgr | la distribuzione, o tlmgr | TeX Live (o MiKTeX) |
| Poppler | Homebrew | la distribuzione | winget |
| Claude Code | installer ufficiale | installer ufficiale | winget |
| pandoc (legge .docx, .odt, .rtf) | non serve: c'è textutil | la distribuzione | winget |

Qt non ha un pacchetto per Windows né per le distribuzioni Linux più vecchie: lì l'installer lo scarica dal server di Qt
con [aqtinstall](https://github.com/miurahr/aqtinstall), che gira in un ambiente Python suo dentro `.strumenti/`. Python
serve solo a questo.

Dopo l'installazione, se non l'hai mai fatto, apri un terminale e lancia `claude` una volta per il login.

## Per chi sviluppa

`./build.sh` compila (macOS e Linux), `./build.sh prove` lancia le prove del motore. La CI
([.github/workflows/build.yml](.github/workflows/build.yml)) compila e prova su macOS, Linux e Windows a ogni push, prova
gli installer rispondendo «no» e apre il programma senza mani su un progetto nuovo: la foto è fra i risultati.
