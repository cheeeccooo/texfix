#pragma once
#include "core/Convert.h"
#include "core/Job.h"
#include "core/Project.h"
#include "core/Settings.h"

#include <QHash>
#include <QJsonObject>

// Aggiungi materiale, la porta sola del materiale nuovo (SPEC §3.4), per concetti. Riconoscere: senza modello ciò che
// è meccanico e sicuro (un doppione di una fonte usata, una versione nuova, un file che non si legge, le lezioni di un
// .tex che il corso contiene tali e quali); il resto lo legge il modello e ne fa la mappa dei concetti (core/Concepts):
// che cosa compare in più punti, dove va ogni concetto nuovo, che cosa saltare.
//
// Il piano (core/Plans, tipo «materiale», formato «concetti»): fonti, corso, concetti, comuni [{name, items, note,
// proposta, valore, ospite}], destinazioni [{into, title, date, concepts, why, fare, nota}], salta, gia, versioni,
// illeggibili, commento, impostazioni, stima. Lo fa e lo esegue Texfix, non l'agente.
namespace tx::sort {

struct Outcome {
    QString plan;                          // il nome del lavoro con il piano; vuoto se non è andata
    QString failure;                       // perché non c'è un piano (il modello non risponde, niente da leggere…)
    QHash<QString, QString> unread;        // i file che non si leggono, con il perché (relativi alla radice)
    double cost = 0;
};
// previous: il piano di prima con le decisioni e le istruzioni dell'utente («Rivedi piano»): i concetti letti restano,
// si rifà la mappa. La lezione a cui l'utente ha aggiunto un file (newlesson::chosenLessons) la si dice al modello.
Outcome plan(const Project &project, const Settings &settings, const QStringList &files, JobControl *control,
             const QJsonObject &previous = {});

// «Avvia»: di ogni fonte si prendono le righe (o le pagine) dei concetti che vanno in un posto, e diventano file fra
// quelli in arrivo. Una lezione nuova di una fonte sola, o di soli .tex e testi, la scrive la conversione; ciò che va in
// una lezione che c'è (un'aggiunta, una versione sola di un concetto in comune, una versione nuova) e le lezioni nuove
// che mescolano pagine e testi sono compiti dell'agente del lavoro (core/Worker), uno per lezione: le aggiunte alla
// stessa lezione sono un compito solo. Le fonti smistate vanno fra le usate.
struct Task {
    QString kind;                                      // integra | versione | fondi
    QString lesson;                                    // la lezione che tocca, o quella nuova che scrive
    QString title;                                     // per chi guarda: «Aggiunte alla lezione 3»
    QStringList sources;                               // i file in arrivo che usa (relativi alla radice)
    QString message;                                   // che cosa si chiede all'agente
};
struct Prepared {
    QStringList sources;                               // da convertire…
    QHash<QString, Convert::Wanted> wanted;            // …con numero, data e argomento
    QList<Task> tasks;                                 // i compiti dell'agente
    QStringList done;                                  // ciò che è già fatto (un doppione tolto, un file archiviato)
    QString failure;
    // per argomento: dove vanno le lezioni che scrive l'agente (file → argomento, titolo, dopo); le colloca chiudi_lezione
    QJsonObject placements;
    QStringList order;                                 // per argomento: l'ordine nuovo, per titolo (reorganize::reorder)
};
Prepared prepare(const Project &project, const QJsonObject &planDocument);

// Le decisioni e le istruzioni del piano come le legge il modello che rifà la mappa.
QString previousPlan(const QJsonObject &planDocument);

// Quanto costerà «Avvia», in dollari, per ciò che il piano farà davvero (le voci con la spunta, le decisioni sui
// concetti in comune e sulle versioni): le lezioni nuove scritte dalla conversione e il lavoro dell'agente (aggiunte,
// fusioni, versioni). Dalle righe (o pagine) dei concetti, con le medie per qualità corrette da ciò che sono costati
// davvero i lavori di prima (learn, in stime.json fra i dati del programma).
struct Estimate {
    double lessons = 0;                                // le lezioni nuove della conversione
    double agent = 0;                                  // i compiti dell'agente
    double total() const { return lessons + agent; }
};
Estimate estimate(const Project &project, const QJsonObject &planDocument, const QString &quality);
// Alla fine di un lavoro: quanto era stimato e quanto è costato davvero, per parte; le stime dopo si correggono.
void learn(const QString &quality, const Estimate &estimated, const Estimate &spent);

}
