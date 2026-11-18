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
// si rifà la mappa. target: la lezione (o l'argomento) a cui l'utente ha aggiunto il materiale; vuoto se non l'ha detto.
Outcome plan(const Project &project, const Settings &settings, const QStringList &files, JobControl *control,
             const QJsonObject &previous = {}, const QString &target = {});

// «Avvia»: di ogni fonte si prendono le righe (o le pagine) dei concetti che vanno in un posto, e diventano file fra
// quelli in arrivo. Una lezione nuova di una fonte sola, o di soli .tex e testi, la scrive la conversione; ciò che va in
// una lezione che c'è (un'aggiunta, una versione sola di un concetto in comune) e le lezioni nuove che mescolano pagine
// e testi passano dall'agente, una procedura alla volta. Le fonti smistate vanno fra le usate.
struct Prepared {
    QStringList sources;                               // da convertire…
    QHash<QString, Convert::Wanted> wanted;            // …con numero, data e argomento
    QStringList agent;                                 // i messaggi delle procedure dell'agente, uno alla volta
    QStringList done;                                  // ciò che è già fatto (un doppione tolto, un file archiviato)
    QString failure;
    // per argomento: dove vanno le lezioni che scrive l'agente (file → argomento, titolo, dopo); le colloca chiudi_lezione
    QJsonObject placements;
};
Prepared prepare(const Project &project, const QJsonObject &planDocument);

// Le decisioni e le istruzioni del piano come le legge il modello che rifà la mappa.
QString previousPlan(const QJsonObject &planDocument);

}
