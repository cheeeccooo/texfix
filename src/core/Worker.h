#pragma once
#include "core/Job.h"
#include "core/Project.h"
#include "core/Settings.h"
#include "core/Style.h"

#include <QHash>
#include <QJsonArray>

// L'agente dentro un lavoro: il CLI `claude` con i file del progetto e gli strumenti di Texfix (WORKER_TOOLS, serviti da
// `Texfix --strumenti-progetto`), senza chat e senza nessuno che risponda. Esegue un compito di un piano già approvato
// (completare una lezione con fonti nuove, portare una versione nuova, scrivere una lezione fondendo più fonti) e la
// sua ultima risposta è l'esito. Gira nel thread di chi lo chiama.
//
// Per chi guarda: una chiamata (fase «integrate») con il suo flusso, riga per riga (che cosa scrive, che file legge o
// cambia, che strumento usa). Il limite di utilizzo ferma il lavoro intero (JobControl::pause); il compito si riprende
// continuando la stessa conversazione (session). «Ferma» chiude il processo.
namespace tx::worker {

// L'inizio dell'ultimo messaggio di un agente che non riesce ad aprire una fonte: il compito non è riuscito e il lavoro
// si ferma (non si scrive una lezione con una fonte sola quando ne aveva due).
inline constexpr const char *UNREADABLE = "FONTE ILLEGGIBILE";

struct Result {
    bool ok = false;
    QString text;                          // l'esito, come l'ha scritto l'agente
    QString failure;                       // perché non è andata
    QString session;                       // la conversazione: un compito interrotto la riprende
    bool limited = false;                  // fermato dal limite di utilizzo: si riprende con session
    bool paused = false;                   // messo in pausa («Ferma», o il programma chiuso) a conversazione cominciata: idem
    double cost = 0;
};
// title: come si chiama il compito per chi guarda («Aggiunte alla lezione 3»).
Result run(const Project &project, const Settings &settings, const Style &style, const QString &task, const QString &title,
           JobControl *control, const QString &session = {});

// Le righe del flusso da una riga dell'uscita del CLI: {tipo: testo | strumento | risultato, testo}. Il testo dell'agente,
// uno strumento con il suo file («Legge src/src3.tex»), la risposta di uno strumento di Texfix (accorciata). names: gli
// strumenti chiesti finora (id → nome), per riconoscere le risposte.
QJsonArray lines(const QJsonObject &output, const QString &root, QHash<QString, QString> *names);

}
