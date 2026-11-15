#pragma once
#include "core/Project.h"

#include <QJsonObject>
#include <QList>

// I lavori con un punto di controllo: l'agente consegna un piano (con uno strumento), l'utente lo sistema e lo
// approva, l'agente lo esegue e consegna l'esito. Ogni lavoro è una cartella salvataggi/lavori/<nome>/ con piano.json;
// chiuso, resta solo un file in salvataggi/esiti/. Ogni modifica dell'utente al piano è salvata subito.
//
// piano.json: titolo, riepilogo, stato (piano | in_corso | esito), sezioni [{titolo, voci [{testo, fare, nota}]}],
// domande [{testo, opzioni [..], risposta}], commento, esito.
namespace tx::plans {

struct Info {
    QString id, title, state;
};
QList<Info> open(const Project &project);
QJsonObject load(const Project &project, const QString &id);
bool save(const Project &project, const QString &id, const QJsonObject &plan);
// Un piano nuovo da ciò che consegna l'agente (titolo, riepilogo, sezioni con le voci, domande con le opzioni).
QString create(const Project &project, const QJsonObject &delivered, QString *error = nullptr);
QString setOutcome(const Project &project, const QString &id, const QString &text);    // l'esito, dall'agente
void discard(const Project &project, const QString &id);
// Chiude un lavoro eseguito: l'esito e sotto il piano seguito, in salvataggi/esiti/. Restituisce il file.
QString archive(const Project &project, const QString &id);

bool hasInstructions(const QJsonObject &plan);         // una nota sotto una voce o un commento: «Rivedi piano»
QString markdown(const QJsonObject &plan);             // il piano da leggere
// Ciò che si dice all'agente: esegui il piano com'è ora, oppure rifallo con le istruzioni dell'utente.
QString startMessage(const QString &id, const QJsonObject &plan);
QString reviseMessage(const QString &id, const QJsonObject &plan);

}
