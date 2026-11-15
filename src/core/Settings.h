#pragma once
#include "core/Project.h"

#include <QMap>
#include <QString>
#include <QStringList>

namespace tx {

// Ciò che il progetto ricorda di sé, in salvataggi/progetto.json: le scelte che valgono per ogni lavoro, le regole
// del progetto, lo stile, le lezioni fatte. Un file solo; lo segue la cronologia.
struct Settings {
    QString writing = "studio";            // come si scrive: studio (da studiare) | fedele (uno a uno)
    QString quality = "bilanciata";        // veloce | bilanciata | massima
    QString scanFigures = "ridisegna";     // figure delle scansioni: ridisegna | ritaglia
    QString texFigures = "tieni";          // figure dei .tex: tieni | ridisegna
    QString layout = "da_sola";            // impaginazione: da_sola (dopo Converti) | a_richiesta
    QStringList rules;                     // le regole del progetto, una per voce
    QString style;                         // lo stile importato (<nome>.sty nella radice); vuoto: nessuno
    QStringList packages;                  // i pacchetti che le figure chiedono oltre lo stile (circuitikz…)
    QMap<QString, QString> done;           // lezione fatta → quando

    static Settings load(const Project &project);
    bool save(const Project &project) const;

    bool isDone(const QString &lesson) const { return done.contains(lesson); }
    void setDone(const QString &lesson, bool on);

    // Le scelte possibili di una voce: valore e nome per l'utente.
    struct Choice {
        QString value, label;
    };
    static QList<Choice> choices(const QString &key);
    QString value(const QString &key) const;
    bool setValue(const QString &key, const QString &value);   // false se la voce o il valore non esistono
};

}
