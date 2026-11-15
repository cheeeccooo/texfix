#pragma once
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
// La libreria: gli stili che Texfix ha con sé (oggi `styles`), pronti e già descritti.
QStringList library();
Imported importFromLibrary(const Project &project, const QString &name);

struct Outcome {
    QString error;
    int lessons = 0;                       // le lezioni riscritte
    QStringList unmatched;                 // gli ambienti rimasti com'erano: il nuovo stile non ha quel ruolo
};
// Il progetto passa allo stile `name` (vuoto: nessuno): texfix.sty carica il nuovo stile e ogni lezione si riscrive
// secondo i ruoli (definizione → l'ambiente di definizione del nuovo stile), rimandi compresi. `choices` dice che cosa
// fare di un ambiente senza corrispondente: ambiente di prima → ambiente del nuovo stile.
Outcome apply(const Project &project, const QString &name, const QHash<QString, QString> &choices = {});

}
