#pragma once
#include "core/Job.h"
#include "core/Project.h"
#include "core/Style.h"

#include <QHash>

// Importare uno stile in un progetto e passare da uno stile a un altro (o a nessuno).
namespace tx::styleswitch {

// Lo stile attuale del progetto, con la sua descrizione (senza stile: il classico).
Style current(const Project &project);

struct Imported {
    QString error;                         // vuoto se è andata
    Style style;                           // la descrizione: quella accanto al .sty se c'era, o la prima letta dal .sty
    bool described = false;                // c'era già una descrizione: niente domande
};
// Copia il .sty nella radice del progetto (così compila anche senza Texfix) e ne salva la descrizione accanto.
// Non cambia ancora lo stile del progetto: lo fa `apply`.
Imported import(const Project &project, const QString &styPath);
// La libreria: gli stili che Texfix ha con sé (oggi `styles`) e quelli che l'utente ci ha messo dalle Preferenze (fra i
// dati del programma, `stili/`), pronti e già descritti.
QStringList library();
// Mette nella libreria lo stile di un progetto (il .sty e la sua descrizione) o un .sty qualunque (la descrizione letta
// dal file). Vuoto se è andata. removeFromLibrary toglie uno stile dell'utente; quelli di Texfix restano.
QString addToLibrary(const QString &styPath);
bool removeFromLibrary(const QString &name);
bool isBundled(const QString &name);
// Lo stile del progetto è diverso da quello della libreria con lo stesso nome (o la libreria non l'ha): è dello stile
// del progetto, e si salva nella libreria con un nome suo.
bool modified(const Project &project);
// Lo stile del progetto cambia nome: il file, \ProvidesPackage, la descrizione, le impostazioni e texfix.sty. Il nome
// è una parola sola (lettere, cifre, - e _), libero nella libreria. Vuoto se è andata.
QString rename(const Project &project, const QString &name);
// Lo stile del progetto va nella libreria con il nome `name` (se è un altro, prima cambia nome nel progetto). Un nome
// della libreria si sostituisce solo con replace, e mai uno stile di Texfix. Vuoto se è andata.
QString saveToLibrary(const Project &project, const QString &name, bool replace = false);
// Il .sty e la descrizione dello stile del progetto in una cartella. Vuoto se è andata.
QString exportTo(const Project &project, const QString &dir);
void setLibraryHome(const QString &dir);              // per le prove

// Il modello legge il .sty e propone la descrizione: il ruolo di ogni ambiente, se si spezza, a che cosa servono le
// macro, come si fanno figure e rimandi, le convenzioni. La salva accanto al .sty; i ruoli restano da confermare.
// Vuoto se è andata; altrimenti il perché (e resta la descrizione letta dal file). Lancia Stopped.
QString propose(const Project &project, const QString &quality, const QString &name, JobControl *control, double *cost = nullptr);

// Gli ambienti usati nelle lezioni che nel nuovo stile non hanno un corrispondente: una domanda per ciascuno.
struct Orphan {
    QString name, role;
    QStringList options;                   // gli ambienti del nuovo stile fra cui scegliere
};
QList<Orphan> orphans(const Project &project, const QString &name);

// Lo stile del progetto è cambiato sul disco (lo ha modificato l'agente, o l'utente): la descrizione lo segue. Gli
// ambienti nuovi entrano con il ruolo che il nome suggerisce, quelli che non ci sono più escono, le macro si rileggono.
// Restituisce gli ambienti nuovi: il loro ruolo è una domanda.
QStringList refresh(const Project &project);
Imported importFromLibrary(const Project &project, const QString &name);

struct Outcome {
    QString error;
    int lessons = 0;                       // le lezioni riscritte
    QStringList unmatched;                 // gli ambienti rimasti com'erano: il nuovo stile non ha quel ruolo
    QStringList renamed, carried;          // le macro che hanno cambiato nome, e quelle la cui definizione è passata in texfix.sty
};
// Il progetto passa allo stile `name` (vuoto: nessuno): texfix.sty carica il nuovo stile e ogni lezione si riscrive
// secondo i ruoli (definizione → l'ambiente di definizione del nuovo stile), rimandi compresi. `choices` dice che cosa
// fare di un ambiente senza corrispondente: ambiente di prima → ambiente del nuovo stile («=»: resta com'è). Le macro
// dello stile di prima che il nuovo non ha: se il nuovo ne ha una con lo stesso significato cambiano nome; altrimenti
// la loro definizione passa in texfix.sty, così le lezioni compilano lo stesso.
Outcome apply(const Project &project, const QString &name, const QHash<QString, QString> &choices = {});

}
