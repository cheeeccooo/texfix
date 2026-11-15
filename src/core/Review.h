#pragma once
#include "core/Project.h"

#include <QList>
#include <QString>
#include <QStringList>

// Ciò che resta da decidere in una lezione dopo una conversione: i dubbi (una lettura incerta, con le letture fra cui
// scegliere) e le aggiunte e correzioni dichiarate dal modello (da tenere o togliere). Ogni decisione è salvata subito:
// i dubbi in salvataggi/dubbi/<lezione>.json, le aggiunte in salvataggi/correzioni.json.
namespace tx::review {

struct Reading {
    QString text, latex;
};
struct Doubt {
    QString id, lesson;                    // «src4-d2», «src/src4.tex»
    QString what, latex, source;           // la domanda; il passo com'è scritto ora; il nome della fonte
    int page = 0;
    QList<Reading> readings;               // la prima è quella scritta dal modello
    bool open = true;
    int chosen = -1;                       // la lettura scelta
    QString how;                           // scelta | confermata
    int line = 0;                          // la riga del passo nella lezione; 0 se non si trova una volta sola
};
QList<Doubt> doubts(const Project &project, const QString &lesson);
// Sceglie una lettura. Quella già scritta chiude e basta; un'altra riscrive il passo, se si trova una volta sola.
// Vuoto se è andata; altrimenti il perché.
QString choose(const Project &project, const QString &lesson, const QString &id, int reading);
// Rimette il dubbio da decidere; con revert rimette anche il passo com'era.
QString reopen(const Project &project, const QString &lesson, const QString &id, bool revert);

struct Declared {                          // un'aggiunta o una correzione del modello
    QString lesson, kind;                  // addedtext | correction
    QString text, hash;                    // il contenuto, e la sua impronta
    int line = 0;                          // la riga del \begin; 0 se è stata tolta
    QString state;                         // aperta | tenuta | tolta
};
QList<Declared> declared(const Project &project, const QString &lesson);
void keep(const Project &project, const QString &lesson, const QString &hash, bool on = true);
void keepAll(const Project &project, const QString &lesson);
QString drop(const Project &project, const QString &lesson, const QString &hash);      // toglie l'ambiente dal file
QString restore(const Project &project, const QString &lesson, const QString &hash);   // lo rimette dov'era

struct Counts {
    int doubts = 0, declared = 0;          // quanti ne restano da decidere
};
Counts counts(const Project &project, const QString &lesson);

}
