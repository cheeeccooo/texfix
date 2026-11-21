#pragma once

#include <QList>
#include <QString>

// I piccoli pezzi dei pannelli Rivedi (una domanda, una lettura, un'aggiunta, una didascalia), pronti per essere
// disegnati nel programma come in Studify: il testo nel carattere dell'interfaccia, le formule disegnate da MicroTeX
// (src/app/Math, src/app/LatexText). Qui solo ciò che non disegna: la prosa del modello resa LaTeX e il testo
// spezzato in pezzi.
namespace tx::snippets {

// Come va letto un pezzo: prosa del modello (con la matematica fra $…$; «ω0» e «V_CB» diventano matematica) o LaTeX
// com'è scritto in una lezione.
enum class Kind { Prose, Latex };

// La prosa pronta per LaTeX.
QString prose(const QString &text);

// Un pezzo di testo: una parola (con grassetto e corsivo), uno spazio, una formula in linea o a sé, un a capo, un
// capoverso, uno stacco, una voce d'elenco con la sua etichetta, l'inizio e la fine di un elenco.
struct Token {
    enum Type { Word, Space, Math, Display, Break, Para, VSpace, Item, ListBegin, ListEnd };
    Type type = Word;
    QString text;                                      // la parola, la formula, l'etichetta della voce
    bool bold = false, italic = false;
    float amount = 0;                                  // Space: larghezza in em (0: uno spazio); VSpace: in righe
    bool operator==(const Token &) const = default;
};

// Il testo spezzato in pezzi. Le formule restano LaTeX ($…$, \(…\), \[…\], $$…$$, equation, align…: align e simili
// diventano aligned); il resto si legge come lo leggerebbe LaTeX per quanto serve a vederlo: grassetto e corsivo,
// elenchi, a capo e capoversi, i commenti tolti, i riquadri con il loro nome e il titolo in grassetto, i titoli delle
// sezioni, i rimandi con la loro etichetta, i disegni e le immagini come «[disegno]», le unità di siunitx come
// formule. Un comando che non si conosce si toglie e ne restano gli argomenti.
QList<Token> tokens(const QString &text, Kind kind);

}
