#pragma once
#include "core/Project.h"

#include <QJsonObject>
#include <QList>

// I lavori con un punto di controllo: l'agente consegna un piano (con uno strumento), l'utente lo sistema e lo
// approva, «Avvia» lo esegue come un lavoro (core/Run: un compito dell'agente per sezione) e l'esito resta nel piano. Ogni lavoro è una cartella salvataggi/lavori/<nome>/ con piano.json;
// chiuso, resta solo un file in salvataggi/esiti/. Ogni modifica dell'utente al piano è salvata subito.
//
// piano.json: titolo, riepilogo, stato (piano | in_corso | esito), sezioni [{titolo, voci [{testo, fare, nota}], scelta}],
// domande [{testo, opzioni [..], risposta, proposta}], commento, esito. La scelta di una sezione (facoltativa) è
// {opzioni [..], valori [..], valore, rifai [..]}: un valore in «rifai» chiede di rifare il piano, come una risposta
// diversa dalla «proposta».
namespace tx::plans {

struct Info {
    QString id, title, state;
};
QList<Info> open(const Project &project);
QJsonObject load(const Project &project, const QString &id);
bool save(const Project &project, const QString &id, const QJsonObject &plan);
// Un piano nuovo da ciò che consegna l'agente (titolo, riepilogo, sezioni con le voci, domande con le opzioni).
QString create(const Project &project, const QJsonObject &delivered, QString *error = nullptr);
void discard(const Project &project, const QString &id);
// Chiude un lavoro eseguito: l'esito e sotto il piano seguito, in salvataggi/esiti/. Restituisce il file.
QString archive(const Project &project, const QString &id);

bool hasInstructions(const QJsonObject &plan);         // una nota, un commento, una risposta cambiata: «Rivedi piano»
QString markdown(const QJsonObject &plan);             // il piano da leggere
// Il compito di una sezione con voci da fare, per l'agente di un lavoro (core/Run, core/Worker): «Avvia» la esegue lì.
QString taskMessage(const QJsonObject &plan, int section);
// Ciò che si dice all'agente della chat per rifare il piano con le istruzioni dell'utente.
QString reviseMessage(const QString &id, const QJsonObject &plan);

}
