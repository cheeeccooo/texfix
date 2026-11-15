#pragma once
#include <QDate>
#include <QList>
#include <QString>
#include <QStringList>

// Le fonti .tex: appunti di un altro autore, spesso un documento solo con una \section{Lezione del …} per lezione. Si
// convertono lezione per lezione; una lezione di un file si indica con «file.tex@AAAA-MM-GG».
namespace tx::texsource {

struct Lesson {
    QDate date;
    QString heading;                       // il titolo della \section, com'è nella fonte
    QString text;                          // dalla \section (esclusa) alla successiva
    int lines = 0;
};
QString preamble(const QString &document);             // fino a \begin{document} escluso
QString body(const QString &document);                 // fra \begin{document} e \end{document}
QList<Lesson> lessons(const QString &document);        // vuoto: il documento non è diviso in lezioni
QStringList macros(const QString &preamble);           // le definizioni dell'autore (\newcommand, \DeclareMathOperator…)
QDate dateIn(const QString &heading);                  // «Lezione del 20 ottobre 2026», «20/10/2026»; non valida se non c'è

QString spec(const QString &file, const QDate &date);
std::pair<QString, QDate> parseSpec(const QString &spec);

// Ciò che il modello riceve per una lezione (o per tutto il documento, se la data non è valida): le macro dell'autore
// in testa come commenti, poi il testo.
QString workerText(const QString &document, const QDate &date);

}
