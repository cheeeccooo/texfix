#pragma once
#include "core/Job.h"
#include "core/Project.h"
#include "core/Settings.h"
#include "core/Style.h"

#include <QJsonArray>
#include <QJsonObject>

// Ciò che l'app dà all'agente (Claude Code locale): strumenti veri, non comandi da terminale né righe convenute.
// L'agente li chiama con il protocollo degli strumenti (MCP, JSON-RPC a righe); li esegue il programma, sul progetto
// aperto. L'agente non ha una shell: niente Python, pdflatex o git.
namespace tx::agent {

QJsonArray tools();                                    // nome, descrizione e forma degli argomenti di ogni strumento
// Esegue uno strumento. Restituisce il testo per l'agente; *error dice se non è andata.
QString call(const Project &project, const Style &style, const QString &name, const QJsonObject &arguments, JobControl *control, bool *error);
// Una riga del protocollo: la risposta da mandare indietro, o vuoto se la riga non ne vuole (una notifica).
QByteArray handle(const Project &project, const Style &style, const QByteArray &request, JobControl *control = nullptr);
// Il prompt di sistema dell'agente: che cosa può fare e come, le regole editoriali, lo stile.
QString systemPrompt(const Settings &settings, const Style &style);

}
