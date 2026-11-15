#pragma once
#include "core/Project.h"

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
// Ciò che si chiede all'agente: fondere più fonti in una lezione nuova, o integrare fonti nuove in una lezione.
QString fuseMessage(const QString &file, int number, const QString &date, const QStringList &sources);
QString integrateMessage(const QString &lesson, const QStringList &sources);

}
