#pragma once
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

// I blocchi di una lezione: ogni unità del .tex (un riquadro, una figura, un tratto di testo fra gli uni e gli altri)
// ha un marcatore su una riga a sé, subito prima:
//
//     % @L6.3 · appunti.pdf p.2
//
// L6.3 è il terzo blocco nato nella lezione 6 e resta suo anche se il blocco cambia file; dopo il «·» da dove viene
// (fonte e pagina, o righe di un .tex), vuoto se non si sa. È un commento: TeX non lo vede. Regola strutturale.
namespace tx::blocks {

struct Block {
    QString id, source;                    // «L6.3»; «appunti.pdf p.2»
    QString kind, title;                   // il nome dell'ambiente, «figura» o «testo»; il titolo del riquadro
    int line = 0, endLine = 0;             // le righe del blocco (da 1), marcatore escluso
    int marker = 0;                        // la riga del marcatore; 0 se non ce l'ha
    QList<int> pages() const;              // le pagine dette nella fonte (p.2, pp.2-3)
};

// Quali ambienti sono unità: i riquadri dello stile e l'ambiente delle figure.
struct Units {
    QStringList boxes;
    QStringList figures{"figure"};
};

QList<Block> parse(const QString &tex, const Units &units);
QString markerLine(const QString &id, const QString &source);
// Un marcatore a ogni blocco che non ce l'ha, con il numero dopo l'ultimo della lezione. `source` dice da dove viene
// il blocco; *next è il prossimo numero (entra ed esce), per marcare più pezzi di fila.
QString mark(const QString &tex, int lesson, const Units &units, const std::function<QString(const Block &)> &source = {},
             int *next = nullptr);

}
