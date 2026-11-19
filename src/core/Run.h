#pragma once
#include "core/Convert.h"
#include "core/Job.h"
#include "core/Project.h"
#include "core/Settings.h"
#include "core/Style.h"

#include <QJsonObject>

// Il lavoro di «Avvia» di Aggiungi materiale, dall'inizio alla fine, in un lavoro solo: Texfix fa da orchestratore.
// Prepara i pezzi una volta (sort::prepare), poi le lezioni nuove le scrive la conversione e, nello stesso momento, le
// aggiunte alle lezioni del corso le fa l'agente del lavoro (core/Worker), due compiti alla volta e mai due sulla stessa
// lezione; le lezioni nuove dell'agente (fusioni) vengono dopo la conversione. Il piano approvato resta, con lo stato
// «in_corso», e alla fine ha l'esito.
//
// Su disco: salvataggi/lavori/<piano>/esecuzione.json (fatto, fonti e scelte della conversione, compiti con lo stato, la
// pausa per il limite); build/lavori/esecuzione/<piano>/: prima/ (com'era il progetto prima di «Avvia»: «Annulla tutto il
// lavoro»), chiamate/ (il registro di ogni chiamata), vista.json (ciò che la finestra del lavoro mostra). Un lavoro
// fermato (da «Ferma» o dal limite di utilizzo) o interrotto dalla chiusura si riprende senza rifare ciò che è fatto; la cartella in
// build/ si butta quando il lavoro si archivia o si annulla.
namespace tx::run {

QString folder(const Project &project, const QString &id);         // build/lavori/esecuzione/<id> (assoluta)
QString recordsDir(const Project &project, const QString &id);
QJsonObject load(const Project &project, const QString &id);        // esecuzione.json; vuoto se il piano non è stato avviato
bool save(const Project &project, const QString &id, const QJsonObject &state);
// I lavori avviati e non ancora archiviati, il più recente per primo.
QStringList started(const Project &project);

struct Result {
    QStringList lines;                                 // l'esito, riga per riga
    Convert::Result converted;
    int tasksDone = 0, tasksFailed = 0;
    double cost = 0, minutes = 0;
    bool stopped = false;
    QString failure;                                   // il lavoro non è partito, o si è interrotto
};
// Avvia o riprende il lavoro del piano id. Gira nel thread di chi lo chiama.
Result execute(const Project &project, const Settings &settings, const Style &style, const QString &id, JobControl *control);

// «Annulla tutto il lavoro»: il progetto torna com'era prima di «Avvia» (anche ciò che è stato cambiato dopo), il piano
// torna da avviare. Restituisce i file scritti o tolti, o vuoto con *error.
QStringList undo(const Project &project, const QString &id, QString *error = nullptr);
// Il lavoro è finito e letto: via la copia di com'era e i registri.
void forget(const Project &project, const QString &id);

}
