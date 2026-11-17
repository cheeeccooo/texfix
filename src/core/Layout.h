#pragma once
#include "core/Job.h"
#include "core/Project.h"
#include "core/Style.h"

#include <QList>
#include <QRectF>

// L'impaginazione: trova ciò che si impagina male e lo sistema con mosse misurate, con qualunque stile. La misura
// viene dalla compilazione della lezione (dove cade ogni blocco nel PDF), non da comandi dello stile.
//
// Vede un'unità (riquadro, enunciato, figura) spezzata fra due pagine, un titolo rimasto in fondo senza il suo testo e un
// grande vuoto prima di un'unità che non ci stava. Le prime due si sistemano con un a capo pagina (o spezzando il riquadro
// in un punto scelto) se il vuoto che resta è piccolo; altrimenti la mossa resta proposta. Il vuoto si segnala.
// Un registro solo, salvataggi/impaginazione.json: si impagina solo ciò che è nuovo o cambiato, le decisioni restano.
namespace tx::layout {

constexpr double HOLE_OK = 0.25;           // il vuoto che un a capo pagina può lasciare da solo, in frazioni di pagina
constexpr double NEAR_END = 0.13;         // un titolo che comincia così vicino alla fine passa oltre con il suo testo
constexpr const char *SIGN = "\\newpage % impaginazione";

struct Move {
    QString lesson, block;                 // «src/src3.tex», «L3.7»
    QString what;                          // per l'utente: che cosa c'era e che cosa si è fatto o si propone
    double hole = 0;                       // il vuoto lasciato (o che lascerebbe) in fondo alla pagina
    QString state;                         // fatta | proposta | segnalata (nessuna mossa sicura) | accettata | lasciata | a_mano | agente | risolta| proposta | accettata | lasciata
    int line = 0;                          // la riga del blocco nella lezione; 0 se non c'è più
    QString sign = SIGN;                   // la riga scritta nella lezione: un a capo prima del blocco, o uno spezzamento dentro
    QString after;                         // per uno spezzamento dentro: la riga dopo cui sta
    QString until;                         // per una mossa che avvolge (la misura di una figura): la riga dopo cui chiude
    QString origin;                        // lo stato che le ha dato il programma (fatta | proposta | segnalata): «annulla la scelta» torna lì
    bool same = false;                     // la mossa non cambia ciò che si vede: le pagine del prima e del dopo sono uguali
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
// a_mano, agente (se ne occupa l'utente o l'agente: resta da rivedere), risolta: cambia solo il registro.
QString decide(const Project &project, const QString &file, const QString &block, const QString &state);
// L'ordine conta: cambiare una mossa sposta tutto ciò che segue. dropAfter toglie dalla lezione e dal registro le mosse
// non ancora decise che vengono dopo quel blocco (quante), e segna la lezione da rimpaginare: poi `lesson` le ricalcola.
int dropAfter(const Project &project, const QString &file, const QString &block);
// Toglie da una lezione tutte le mosse dell'impaginazione (le righe «% impaginazione», il registro, le pagine in piccolo):
// torna da impaginare. Restituisce quante erano.
int reset(const Project &project, const QString &file);
// Ciò che in una lezione è scritto da chi la scrive: senza le righe dell'impaginazione, i marcatori dei blocchi (che
// l'impaginazione aggiunge a un testo nuovo) e le righe vuote. Per capire se, a parte quelle, la lezione è ancora la stessa.
QString written(const QString &text);
// Annulla la scelta dell'utente su una mossa: torna com'era quando il programma l'ha fatta, proposta o segnalata (la sua
// riga torna nella lezione, o ne esce). Vuoto se è andata.
QString reopen(const Project &project, const QString &file, const QString &block);
// Le pagine in piccolo attorno a una mossa: «prima» (com'era quando il problema è stato visto) e «dopo» (com'è venuta).
// Di ogni pagina, dove sta il blocco (in frazioni della pagina): il contorno da disegnarci sopra.
struct Shot {
    QString path;
    QList<QRectF> rects;
};
QList<Shot> shots(const Project &project, const QString &file, const QString &block, const QString &which);
// Rifà le pagine in piccolo del «dopo» di una mossa dalla lezione com'è adesso (dopo una modifica a mano): la compila.
void retake(const Project &project, const Style &style, const QString &file, const QString &block, JobControl *control = nullptr);

}
