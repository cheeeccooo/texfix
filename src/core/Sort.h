#pragma once
#include "core/Convert.h"
#include "core/Job.h"
#include "core/Project.h"
#include "core/Settings.h"

#include <QHash>
#include <QJsonObject>

// Aggiungi materiale, la porta sola del materiale nuovo (SPEC §3.4). Riconoscere: ogni file diviso in unità (le pagine
// di un PDF o di una scansione, le lezioni di un .tex, un testo intero), e ogni unità con un esito: già nel corso
// (senza modello: lo stesso file fra le fonti usate, o il testo che c'è già), versione nuova di una fonte usata, che
// si sovrappone a una lezione (o a un argomento), lezione nuova (o argomento nuovo), da saltare. Il modello serve per
// le pagine (una chiamata per file, tenuta in build/lavori/smistamento/) e per il piano (una chiamata con il corso), che
// propone anche il ruolo di ogni fonte: principale, integrativa, esercizi.
//
// Il risultato è sempre un piano (core/Plans, tipo «materiale») che fa Texfix, non l'agente: una sezione per
// destinazione, una voce per pezzo con le istruzioni sotto, una scelta dove c'è una sovrapposizione o una versione, una
// domanda per il ruolo di ogni fonte.
namespace tx::sort {

struct Outcome {
    QString plan;                          // il nome del lavoro con il piano; vuoto se non è andata
    QString failure;                       // perché non c'è un piano (il modello non risponde, niente da leggere…)
    QHash<QString, QString> unread;        // i file che non si leggono, con il perché (relativi alla radice)
    double cost = 0;
};
// previous: il piano di prima con le istruzioni, le scelte e i ruoli dell'utente («Rivedi piano»). target: la lezione
// (o l'argomento) a cui l'utente ha aggiunto il materiale; vuoto se non l'ha detto.
Outcome plan(const Project &project, const Settings &settings, const QStringList &files, JobControl *control,
             const QJsonObject &previous = {}, const QString &target = {});

// «Avvia»: i pezzi diventano file fra quelli in arrivo. Le lezioni nuove di una fonte sola e senza istruzioni si
// convertono (con numero, data e, per argomento, dove vanno); il resto passa dall'agente, con un piano suo.
struct Prepared {
    QStringList sources;                               // da convertire…
    QHash<QString, Convert::Wanted> wanted;            // …con numero, data e argomento
    QStringList agent;                                 // i messaggi delle procedure dell'agente, uno alla volta
    QStringList done;                                  // ciò che è già fatto (un doppione tolto, un .tex archiviato)
    QString failure;
};
Prepared prepare(const Project &project, const QJsonObject &planDocument);

// Il piano di prima come lo legge il modello, con le istruzioni dell'utente pezzo per pezzo.
QString previousPlan(const QJsonObject &planDocument);

}
