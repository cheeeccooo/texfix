#pragma once
#include <QProcessEnvironment>
#include <QString>

class QProcess;

// I programmi esterni (latexmk, git, claude) e i loro processi.
namespace tx::tools {

// Un programma lanciato dal Finder (o da un menu) non ha TeX, Homebrew e claude nel PATH: questo ambiente sì.
QProcessEnvironment environment();
// Lo stesso PATH per il processo stesso: Qt cerca il programma da lanciare nel PATH di chi lancia.
void adoptPath();

// Il percorso del programma nel PATH di `environment` (su Windows anche .exe, .cmd, .bat); il nome com'è se non c'è.
QString program(const QString &name);
// Avvia un programma esterno: su Windows uno script senza estensione passa da sh, un .cmd da cmd.
void start(QProcess &process, const QString &name, const QStringList &arguments);

// Il processo parte a capo di un gruppo suo, così `stopGroup` ferma lui e tutti i suoi figli
// (pdflatex è figlio di latexmk). Su Windows il gruppo è un Job Object e il processo parte sospeso:
// dopo `ownGroup` si avvia sempre con `start`, che lo mette nel contenitore e lo fa ripartire.
void ownGroup(QProcess &process);
void stopGroup(qint64 pid);
// Il processo non c'è più, o è finito e nessuno l'ha ancora raccolto.
bool gone(qint64 pid);
void reap(qint64 pid);

}

namespace tx::tools {
// Un programma breve, aspettando che finisca: ciò che ha scritto, o niente se non è partito o non ha finito in tempo.
struct Output {
    bool ok = false;                       // partito, finito in tempo, uscita 0
    QString text;
};
Output run(const QString &program, const QStringList &arguments, const QString &directory, int timeoutMs = 20000);

// Immagini e documenti per il modello: sul Mac sips e textutil, altrove Qt e pandoc (o LibreOffice).
bool imageToPng(const QString &source, const QString &target, int maxSide);
bool imageToPdf(const QString &source, const QString &target);
QString documentText(const QString &source);
// Un documento (.docx, .odt, .rtf, .doc) in markdown con pandoc: le tabelle come tabelle, le formule di Word come
// $…$, le immagini estratte in `media` (relativa a `root`, da dove si lancia pandoc: i riferimenti nel testo sono
// relativi a root). Vuoto se pandoc non c'è o non lo legge.
QString documentMarkdown(const QString &source, const QString &root, const QString &media);
// /dev/null, o NUL su Windows.
QString nullDevice();
}
