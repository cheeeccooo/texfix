#pragma once
#include "core/Job.h"
#include "core/Project.h"
#include "core/Style.h"

#include <QList>

// L'impaginazione: trova ciò che si impagina male e lo sistema con mosse misurate, con qualunque stile. La misura
// viene dalla compilazione della lezione (dove cade ogni blocco nel PDF), non da comandi dello stile.
//
// Oggi vede un'unità (riquadro, enunciato, figura) spezzata fra due pagine e la sistema con un a capo pagina prima del
// suo blocco, se il vuoto che lascia è piccolo; altrimenti la mossa resta proposta, da rivedere.
// Un registro solo, salvataggi/impaginazione.json: si impagina solo ciò che è nuovo o cambiato, le decisioni restano.
namespace tx::layout {

constexpr double HOLE_OK = 0.25;           // il vuoto che un a capo pagina può lasciare da solo, in frazioni di pagina
constexpr const char *SIGN = "\\newpage % impaginazione";

struct Move {
    QString lesson, block;                 // «src/src3.tex», «L3.7»
    QString what;                          // per l'utente: che cosa c'era e che cosa si è fatto o si propone
    double hole = 0;                       // il vuoto lasciato (o che lascerebbe) in fondo alla pagina
    QString state;                         // fatta | proposta | accettata | lasciata
    int line = 0;                          // la riga del blocco nella lezione; 0 se non c'è più
};
struct Result {
    int found = 0, done = 0, proposed = 0;
    QString failure;
};

// Dove cade un blocco nel PDF della lezione.
struct Placed {
    QString block, kind, title;
    int line = 0, firstPage = 0, lastPage = 0;
    double top = 0;                        // quanto è sceso nella pagina il suo inizio, fra 0 e 1
    bool split() const { return lastPage > firstPage; }
};
QList<Placed> measure(const Project &project, const Style &style, const QString &lesson, JobControl *control = nullptr, QString *failure = nullptr);

Result lesson(const Project &project, const Style &style, const QString &file, JobControl *control = nullptr);
// La lezione non è nel registro o è cambiata dopo l'ultima impaginazione.
bool needed(const Project &project, const QString &file);
QList<Move> moves(const Project &project, const QString &file);
int toReview(const Project &project, const QString &file);
// accettata: la mossa proposta si applica, o quella fatta si conferma. lasciata: l'a capo pagina si toglie.
QString decide(const Project &project, const QString &file, const QString &block, const QString &state);

}
