#pragma once
#include <QList>
#include <QString>

namespace tx {

// Un avviso della compilazione, con il file e la riga a cui porta.
struct Issue {
    enum Kind { Error, WideLine, LooseLine };
    Kind kind = Error;
    QString file;                          // «src/src3.tex»; vuoto se il log non lo dice
    int line = 0, lastLine = 0;            // 0 se il log non la dice
    QString text;                          // l'errore, o il testo della riga fuori misura
    QString amount;                        // «12.3pt too wide», «badness 10000»

    bool operator==(const Issue &) const = default;
};

namespace texlog {

// Gli avvisi di un .log di LaTeX compilato con -file-line-error e righe intere (max_print_line).
QList<Issue> parse(const QString &log);
// Il testo di una riga come lo stampa il log, senza i nomi dei font e i segni di TeX.
QString cleanRow(QString excerpt);
// Il numero di pagine scritto in fondo al log (0 se non c'è).
int pages(const QString &log);
// Un file ausiliario troncato da una compilazione interrotta: LaTeX si ferma leggendolo, a \begin{document}.
bool brokenAux(const QString &log);

}
}
