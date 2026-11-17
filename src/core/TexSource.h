#pragma once
#include <QDate>
#include <QList>
#include <QString>
#include <QStringList>

// Le fonti .tex: appunti di un altro autore, spesso un documento solo con una \section{Lezione del …} per lezione. Si
// convertono lezione per lezione; una lezione di un file si indica con «file.tex@AAAA-MM-GG», e se quel giorno ce n'è più
// d'una la seconda è «file.tex@AAAA-MM-GG.2».
namespace tx::texsource {

struct Lesson {
    QDate date;
    QString heading;                       // il titolo della \section, com'è nella fonte
    QString text;                          // dalla \section (esclusa) alla successiva
    int lines = 0;
    int nth = 1;                           // la prima, la seconda… lezione di quel giorno nel file
};
QString preamble(const QString &document);             // fino a \begin{document} escluso
QString body(const QString &document);                 // fra \begin{document} e \end{document}
QList<Lesson> lessons(const QString &document);        // vuoto: il documento non è diviso in lezioni
QStringList macros(const QString &preamble);           // le definizioni dell'autore (\newcommand, \DeclareMathOperator…)
QDate dateIn(const QString &heading);                  // «Lezione del 20 ottobre 2026», «20/10/2026»; non valida se non c'è

// Le lezioni del file che il corso non ha ancora: have sono le date delle lezioni del corso (GG/MM/AAAA), una per
// lezione; se il corso ha una lezione del 10/11 e il file due, manca la seconda.
QList<Lesson> missing(const QList<Lesson> &lessons, const QStringList &have);

struct Ref {
    QString path;                          // il file, senza «@…»
    QDate date;                            // non valida: tutto il file
    int nth = 1;
};
QString spec(const QString &file, const QDate &date, int nth = 1);
Ref parseSpec(const QString &spec);
QString label(const Ref &ref);                         // «lezione del 10/11/2026», «seconda lezione del 10/11/2026»; vuoto senza data

// Ciò che il modello riceve per una lezione (o per tutto il documento, se la data non è valida): le macro dell'autore
// in testa come commenti, poi il testo.
QString workerText(const QString &document, const QDate &date, int nth = 1);

}
