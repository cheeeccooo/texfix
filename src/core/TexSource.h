#pragma once
#include <QDate>
#include <QList>
#include <QSet>
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

struct Ref {
    QString path;                          // il file, senza «@…»
    QDate date;                            // non valida: tutto il file
    int nth = 1;
};
QString spec(const QString &file, const QDate &date, int nth = 1);
Ref parseSpec(const QString &spec);
QString label(const Ref &ref);                         // «lezione del 10/11/2026», «seconda lezione del 10/11/2026»; vuoto senza data

// Di che cosa parla un pezzo di LaTeX, in breve: per ogni \subsection il titolo e l'inizio della sua prosa (senza
// commenti, figure, disegni, etichette), al più `chars` caratteri ciascuna; ciò che precede la prima \subsection è
// «(inizio)». Per chi deve capire se due lezioni dicono le stesse cose.
QStringList gist(const QString &latex, int chars = 160);

// Quanto di un testo sta in un altro, senza modello: le sequenze di quattro parole di `part` (senza comandi, formule e
// commenti) che si ritrovano in `whole`, da 0 a 1. Una lezione convertita da un .tex ne tiene il 30-60%; due lezioni
// diverse, sullo stesso argomento, meno del 3%.
QSet<QString> shingles(const QString &latex);
double contained(const QSet<QString> &part, const QSet<QString> &whole);
constexpr double CONTAINED = 0.15;                     // da qui in su il testo c'è già

// Ciò che il modello riceve per una lezione (o per tutto il documento, se la data non è valida): le macro dell'autore
// in testa come commenti, poi il testo.
QString workerText(const QString &document, const QDate &date, int nth = 1);

}
