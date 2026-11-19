#pragma once
#include "core/Job.h"
#include "core/Model.h"
#include "core/Pages.h"
#include "core/Project.h"

#include <QJsonObject>
#include <QList>
#include <QString>

// I concetti del materiale (SPEC §3.4): l'unità con cui si smista non è la lezione né il file, ma il concetto (una
// definizione, una derivazione, un circuito e la sua analisi, un esempio, un esercizio). Il modello li ricava leggendo
// tutto, una chiamata per lezione del corso o per fonte nuova, tenuta in build/lavori/concetti/ finché il testo non
// cambia; poi una chiamata fa la mappa: che cosa il materiale nuovo ha in comune con il corso e fra sé, che cosa resta di
// chi si interseca, che cosa è tutto nuovo. Lo stile e l'impaginazione della fonte non contano: solo il contenuto.
namespace tx::concepts {

struct Concept {
    QString id;                            // «L3.2» per una lezione del corso, «A.4» per una fonte nuova: lo dà Texfix
    QString title;                         // poche parole
    QString what;                          // una frase: che cosa dice o fa, con le cose precise
    QString where;                         // dove sta: la sezione, o le pagine
    int from = 0, to = 0;                  // le righe del testo (da 1), o le pagine
};

// Una lezione del corso o una fonte nuova, con i suoi concetti.
struct Holder {
    QString key;                           // «L3», «A»
    QString name;                          // come lo dice il piano: «Lezione 3 · Filtri…», «labss1 · 20 ottobre»
    QString source;                        // la lezione (src/…) o la fonte (fonti/coda/…, anche «file.tex@data»)
    bool course = false;
    QList<Concept> concepts;
    QString failure;                       // perché non si sono letti i concetti
};

// Il testo di una fonte come lo legge il modello (le righe dei concetti si contano lì): di una lezione di un .tex il suo
// testo con le macro dell'autore in testa, di un .tex non diviso il corpo, di un documento il testo; vuoto per le pagine.
QString textOf(const Project &project, const QString &source);
// Le righe di alcuni concetti (from-to, da 1), in ordine, allargate ai paragrafi che toccano (fino alle righe vuote): i
// numeri del modello possono sbagliare di una riga. Solo se il testo ha paragrafi, cioè almeno due blocchi separati da
// righe vuote e nessuno più lungo di PARAGRAPH_LINES righe: il testo di un .docx ha una riga per paragrafo e spesso
// nessuna riga vuota, e allargando un concetto si porterebbe dietro tutto il file.
constexpr int PARAGRAPH_LINES = 15;
QString extract(const QString &text, const QList<std::pair<int, int>> &ranges);

// I concetti di un testo, letto per intero (le righe numerate, per sapere dove sta ogni concetto). Lancia ciò che lancia
// Model se il modello non risponde.
Holder fromText(const Project &project, Model &model, const QString &key, const QString &name, const QString &source,
                const QString &text, bool course);
// I concetti di una fonte a pagine (scansioni, PDF): le pagine intere.
Holder fromPages(const Project &project, Model &model, const QString &key, const QString &name, const QString &source,
                 const Pages &pages);

// La mappa, come la restituisce il modello e la controlla Texfix:
// shared [{name, items [id…], note, proposal: one|both|skip, keep: id}], place [{concepts [id…], into: «L3» | «new»,
// title, date, after, why}], skip [{concepts [id…], why}], order [«L2», «new:titolo», …] (per argomento, solo se un
// altro ordine è più logico). Ogni concetto nuovo sta in place o in skip; quelli in comune anche in shared. byTopic: il
// corso è fatto di argomenti, non di lezioni. previous: la mappa di prima con le decisioni e le istruzioni dell'utente
// («Rivedi piano»), come la legge il modello. program: i temi del programma del corso, in ordine (program()).
QJsonObject map(Model &model, const QList<Holder> &course, const QList<Holder> &fresh, bool byTopic, QString *failure,
                const QString &previous = {}, const QStringList &program = {});

// I temi del programma del corso (Settings::program), in ordine, una riga ciascuno («1. Titolo: che cosa tratta»). Il
// modello lo legge una volta: la risposta resta in build/lavori/concetti/. Vuoto se il corso non ne ha uno.
QStringList program(const Project &project, Model &model);

// Tutto il giro per del materiale in arrivo: i concetti del corso e delle fonti (in parallelo), ciò che è già nel corso
// tale e quale (senza modello: le lezioni di un .tex che il corso contiene), la mappa. Il risultato va anche in
// build/lavori/concetti/ultima-mappa.json.
struct Survey {
    QList<Holder> course, fresh;
    QStringList known;                     // «labss1 · 29 settembre: già nel corso, nella lezione 1 · …»
    QJsonObject map;
    QString failure;
    double cost = 0;
    QJsonObject toJson() const;
};
Survey survey(const Project &project, const QString &quality, const QStringList &files, JobControl *control, const QString &previous = {});

}
