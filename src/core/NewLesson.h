#pragma once
#include "core/Job.h"
#include "core/Project.h"
#include "core/TexSource.h"

#include <QHash>
#include <QStringList>

// Le lezioni nuove e le fonti: numeri e nomi liberi, la coda, la chiusura di una lezione dell'agente, le versioni, le
// lezioni dei .tex rispetto al corso, i messaggi delle procedure dell'agente.
namespace tx::newlesson {

int nextNumber(const Project &project);
// Il numero dell'ultimo file srcN o argN, anche di quelli che ora stanno dentro un argomento (le lezioni che il progetto per
// argomento ricorda per il ritorno per data): il prossimo file nuovo è quello dopo.
int lastFile(const Project &project);
// Porta dei file fra le fonti da convertire (un nome già preso diventa «nome (2)»). Restituisce dove sono finiti,
// relativi alla radice; ciò che è già in fonti/coda resta dov'è. Una cartella si scende fino in fondo, senza i nascosti, e
// se ne prende ciò che Texfix sa leggere (Pages::kindOf); skipped: quanti file ha lasciato. Non riconosce niente: lo
// fa «Smista». lesson: la lezione a cui l'utente li ha aggiunti («Aggiungi materiale a questa lezione…»), ricordata
// finché sono in arrivo.
QStringList toQueue(const Project &project, const QStringList &paths, const QString &lesson = {}, int *skipped = nullptr);
// La lezione scelta per i file in arrivo (file → lezione, relativi alla radice), in salvataggi/in-arrivo.json: solo
// quelli che ci sono ancora. Smista la dice al modello.
constexpr const char *CHOSEN = "salvataggi/in-arrivo.json";
QHash<QString, QString> chosenLessons(const Project &project);
void chooseLesson(const Project &project, const QStringList &files, const QString &lesson);
// In un progetto per argomento, dove vanno le lezioni che scrive l'agente di un lavoro («collocazioni»: file → argomento,
// titolo, dopo): le colloca chiudi_lezione. Restano alla chiusura.
constexpr const char *PROCEDURES = "salvataggi/procedure.json";
void addPlacements(const Project &project, const QJsonObject &placements);
// Mette una lezione nel documento, se non c'è. Per data: prima della prima lezione con una data più tardi (a parità di
// data, con un numero più alto), perché le lezioni nuove di un lavoro sono pronte in un ordine qualsiasi; una lezione
// senza data va dopo `after` (la lezione che il piano le indica), altrimenti in fondo. Per argomento: in fondo (poi la
// colloca reorganize::place). Vero se l'ha messa.
bool addToMain(const Project &project, const QString &lesson, const QString &after = {});
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
QString updateMessage(const QString &lesson, const QString &source, const QString &previous);   // un compito dell'agente di un lavoro
// La lezione del corso i cui marcatori dei blocchi nominano un file (con files::bare): un file in arrivo che è già
// diventato quella lezione (rimasto in coda da un lavoro di prima). Vuoto se nessuna.
Lesson namedBy(const Project &project, const QString &file);

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

// Ciò che si chiede all'agente di un lavoro (core/Worker), dentro un piano già approvato: fondere più fonti in una lezione
// nuova, integrare fonti nuove in una lezione, portare una versione nuova di una fonte. `main`:
// le fonti principali, quando ce ne sono anche di integrative: danno la struttura e decidono dove le fonti non concordano.
QString fuseMessage(const QString &file, int number, const QString &date, const QStringList &sources, const QStringList &main = {},
                    const QString &topic = {});    // con topic: per argomento, «Crea l'argomento»
QString integrateMessage(const QString &lesson, const QStringList &sources);

}
