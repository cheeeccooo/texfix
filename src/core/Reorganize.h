#pragma once
#include "core/Job.h"
#include "core/Project.h"
#include "core/Settings.h"

#include <QList>

// Riorganizza: le sezioni del corso (ciò che sta fra un \subsection e il successivo; i \paragraph restano dentro)
// raggruppate per argomento, un file per argomento, e il ritorno a un file per lezione. Tutte e due le strade sono
// del programma: lo stesso contenuto esce riga per riga, e i marcatori dei blocchi seguono la loro sezione.
namespace tx::reorganize {

struct Section {
    QString id;                            // «src/src3.tex#2»: il file e il posto nel file
    int lesson = 0;
    QString date, title;
    QStringList lines;                     // dal \subsection (compreso) fino alla sezione dopo
};
struct Topic {
    QString title;
    QStringList sections;                  // gli id, nell'ordine in cui vanno scritte
};

QList<Section> sections(const Project &project);
// Gli argomenti proposti dal modello: ogni sezione in un argomento solo. Senza modello (o se sbaglia): uno per lezione.
QList<Topic> propose(const Project &project, const Settings &settings, JobControl *control, double *cost = nullptr);
// Scrive un file per argomento (src/arg1.tex…) al posto delle lezioni. Vuoto se è andata; altrimenti il perché, e
// niente è stato toccato.
QString toTopics(const Project &project, const QList<Topic> &topics);
// Torna a un file per lezione, con i nomi e i titoli di prima.
QString toLessons(const Project &project);

}
