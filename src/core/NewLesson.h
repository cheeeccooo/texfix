#pragma once
#include "core/Job.h"
#include "core/Project.h"
#include "core/TexSource.h"

#include <QStringList>

// Una lezione nuova: il numero e la data proposti, o cercati senza modello nei nomi e nel testo delle fonti.
namespace tx::newlesson {

struct Marks {
    int number = 0;                        // 0: non trovato
    QString date;                          // GG/MM/AAAA; vuoto: non trovata
    QString conflict;                      // le fonti non concordano: che cosa dicono
};
int nextNumber(const Project &project);
// Ciò che le fonti dicono di sé: «Lezione 4», «lez_04», una data nel nome o nelle prime righe del testo.
Marks read(const QStringList &paths);
Marks readOne(const QString &name, const QString &text);
// Porta dei file fra le fonti da convertire (un nome già preso diventa «nome (2)»). Restituisce dove sono finiti,
// relativi alla radice; ciò che è già in fonti/coda resta dov'è.
QStringList toQueue(const Project &project, const QStringList &paths);
// Chiude una lezione scritta dall'agente: la mette nel documento se manca, aggiunge i marcatori dei blocchi che
// mancano, porta le fonti fra quelle usate. Restituisce che cosa ha fatto.
QStringList settle(const Project &project, const QStringList &boxes, const QString &figureEnv, const QString &lesson, const QStringList &sources);
// Le versioni delle fonti. versionOf: la fonte già usata che ha lo stesso nome (senza maiuscole, punteggiatura e « (2)»)
// di un file che arriva, e se è identica. replaceVersion: la vecchia va in «precedenti/» con la data, la nuova prende il
// suo posto e una copia va fra le fonti da convertire; restituisce la copia (vuoto se non è andata).
struct Version {
    QString used;                          // relativo alla radice; vuoto: nessuna fonte usata con quel nome
    bool identical = false;
};
Version versionOf(const Project &project, const QString &path);
QString replaceVersion(const Project &project, const QString &used, const QString &path);
QString updateMessage(const QString &lesson, const QString &source, const QString &previous);

// Più fonti dello stesso tipo per una lezione: una chiamata leggera dice se si sovrappongono, in che ordine vanno e
// qual è la più ricca. Se non hanno parti in comune, unite fa di loro un PDF solo fra le fonti da convertire.
struct Overlap {
    bool known = false, overlap = true;    // known: il modello ha risposto
    QList<int> order;                      // gli indici delle fonti, nell'ordine in cui vanno lette
    QString why;                           // una riga per l'esito
    double cost = 0;
};
Overlap overlap(const Project &project, const QString &quality, const QStringList &sources, JobControl *control);
QString unite(const Project &project, const QStringList &sources, const QList<int> &order, int number);

// Le lezioni di un .tex di un altro autore rispetto al corso. `in` è la lezione del corso che viene da lei: i suoi
// marcatori nominano il file e ha la sua data, o ne contiene il testo (texsource::contained). `sameDate`: il corso ha
// una lezione di quel giorno che non viene da lei (la seconda lezione del giorno nel file guarda la seconda del corso).
struct TexLecture {
    texsource::Lesson lesson;
    QString spec;                          // «fonti/coda/a.tex@2026-11-10.2»
    QString in;                            // la lezione del corso che la contiene; vuoto se nessuna
    QString sameDate;                      // la lezione del corso dello stesso giorno, se non è `in`
};
QList<TexLecture> texLectures(const Project &project, const QString &file);

// Le fonti come le legge l'agente, che apre testo, PDF, PNG e JPEG ma non .docx, .odt, .doc, .rtf, HEIC o TIFF: di
// questi fa una copia leggibile in build/lavori/leggibili/ e la nomina accanto; di una lezione di un .tex nomina la sua
// \section. Il nome resta quello della fonte, che è ciò che si passa a chiudi_lezione.
QString readable(const Project &project, const QString &file);   // la copia leggibile, relativa; vuoto se non serve o non riesce
QStringList forAgent(const Project &project, const QStringList &sources);

// Ciò che si chiede all'agente: fondere più fonti in una lezione nuova, o integrare fonti nuove in una lezione.
QString fuseMessage(const QString &file, int number, const QString &date, const QStringList &sources, const QStringList &important = {});
QString integrateMessage(const QString &lesson, const QStringList &sources);

}
