#pragma once
#include "core/Job.h"
#include "core/Project.h"
#include "core/TexSource.h"

#include <QStringList>

// Le lezioni nuove e le fonti: numeri e nomi liberi, la coda, la chiusura di una lezione dell'agente, le versioni, le
// lezioni dei .tex rispetto al corso, i messaggi delle procedure dell'agente.
namespace tx::newlesson {

int nextNumber(const Project &project);
// Il numero dell'ultimo file srcN o argN, anche di quelli che ora stanno dentro un argomento (le lezioni che il progetto per
// argomento ricorda per il ritorno per data): il prossimo file nuovo è quello dopo.
int lastFile(const Project &project);
// Porta dei file fra le fonti da convertire (un nome già preso diventa «nome (2)»). Restituisce dove sono finiti,
// relativi alla radice; ciò che è già in fonti/coda resta dov'è.
QStringList toQueue(const Project &project, const QStringList &paths);
// Le procedure dell'agente che aspettano il turno («in_attesa») e, in un progetto per argomento, dove vanno le lezioni
// che scrivono («collocazioni»: file → argomento, titolo, dopo). Restano alla chiusura.
constexpr const char *PROCEDURES = "salvataggi/procedure.json";
void addPlacements(const Project &project, const QJsonObject &placements);
// Chiude una lezione scritta dall'agente: la mette nel documento se manca, aggiunge i marcatori dei blocchi che
// mancano, porta le fonti fra quelle usate e, se ha una collocazione, la mette nel suo argomento
// (reorganize::place). Restituisce che cosa ha fatto.
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

// Più scansioni o PDF di una lezione che si seguono (lo dice il piano di Aggiungi materiale): unite fa di loro un PDF
// solo, nell'ordine dato, fra quelli in arrivo, e toglie quelli che ha unito.
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

// Ciò che si chiede all'agente: fondere più fonti in una lezione nuova, o integrare fonti nuove in una lezione. `main`:
// le fonti principali, quando ce ne sono anche di integrative: danno la struttura e decidono dove le fonti non concordano.
QString fuseMessage(const QString &file, int number, const QString &date, const QStringList &sources, const QStringList &main = {});
QString integrateMessage(const QString &lesson, const QStringList &sources);

}
