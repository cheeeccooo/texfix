#pragma once
#include "core/Convert.h"
#include "core/Job.h"
#include "core/Project.h"
#include "core/Settings.h"

#include <QJsonObject>

// Aggiungi materiale: il materiale nuovo diviso fra lezioni nuove e lezioni da completare. Due passi con il modello:
// di che cosa parla ogni pagina (una chiamata per file a pagine, tenuta in build/: non si ripaga), poi una chiamata
// leggera con le lezioni del corso. Il risultato è un piano (core/Plans, tipo «materiale») che fa Texfix, non l'agente:
// una sezione per lezione, una voce per pezzo, con le istruzioni dell'utente sotto.
namespace tx::sort {

struct Outcome {
    QString plan;                          // il nome del lavoro con il piano; vuoto se non è andata
    QString failure;
    double cost = 0;
};
// previous: il piano di prima con le istruzioni dell'utente («Rivedi piano»): si rifà solo l'ultima chiamata.
Outcome plan(const Project &project, const Settings &settings, const QStringList &files, JobControl *control, const QString &previous = {});

// «Avvia»: i pezzi diventano file fra le fonti da convertire. Le lezioni nuove fatte di un file solo e senza
// istruzioni si convertono (con numero e data); il resto passa dall'agente, con un piano suo.
struct Prepared {
    QStringList sources;                               // da convertire…
    QHash<QString, Convert::Wanted> wanted;            // …con il loro numero e la loro data
    QStringList agent;                                 // i messaggi delle procedure dell'agente, uno alla volta
    QString failure;
};
Prepared prepare(const Project &project, const QJsonObject &planDocument);

// Il piano di prima come lo legge il modello, con le istruzioni dell'utente pezzo per pezzo.
QString previousPlan(const QJsonObject &planDocument);

}
