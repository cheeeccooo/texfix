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

// Un progetto per argomento che nasce così: niente lezioni da ricordare, ma il progetto è già per argomenti.
void startByTopic(const Project &project);
// Una lezione appena scritta (un file nel documento) in un progetto per argomento: il suo contenuto va in fondo
// all'argomento `topic`, o diventa un argomento nuovo con il titolo `title` subito dopo `after` (vuoto: in fondo), con
// `\lezione{N}{data}` davanti, e la lezione si ricorda per il ritorno per data. `after` «^»: prima di tutti. Vuoto se è
// andata.
QString place(const Project &project, const QString &lesson, const QString &topic, const QString &title, const QString &after = {});
// Dove va un argomento nuovo che fa parte di un gruppo (gli argomenti nuovi di uno stesso piano, con lo stesso `after`,
// nell'ordine del piano: `group` i titoli, `rank` il suo posto): subito dopo il più vicino prima di lui già collocato,
// o subito prima del più vicino dopo, se no `after`. Così l'ordine è quello del piano qualunque sia l'ordine in cui le
// lezioni sono pronte. Non contano come collocate le lezioni che aspettano ancora un posto (`src/srcN.tex`).
QString anchor(const Project &project, const QStringList &group, int rank, const QString &after);

}
