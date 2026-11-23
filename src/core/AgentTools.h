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
// only: gli strumenti che chi chiama vede e può usare (vuoto: tutti).
QByteArray handle(const Project &project, const Style &style, const QByteArray &request, JobControl *control = nullptr, const QStringList &only = {});
// apri e avvia li esegue la finestra (app/Chat): qui si controllano gli argomenti. Restituisce il perché non va (vuoto se
// va); *title dice all'utente che cosa si fa, *costly se usa il modello (allora, salvo «Autonomo», si chiede conferma).
QString checkAction(const Project &project, const QString &name, const QJsonObject &arguments, QString *title = nullptr, bool *costly = nullptr);
// Il prompt di sistema dell'agente: che cosa può fare e come, le regole editoriali, lo stile.
QString systemPrompt(const Settings &settings, const Style &style);

// L'agente dentro un lavoro (un piano già approvato, senza chat): fa il compito fino in fondo, senza piani né domande; la sua
// ultima risposta è l'esito per l'utente. Ha i file e gli strumenti di WORKER_TOOLS.
QString workerPrompt(const Settings &settings, const Style &style);
inline const QStringList WORKER_TOOLS{"stato_progetto", "compila_lezione", "dubbi", "chiudi_lezione"};
// L'agente della chat non scrive niente: legge, spiega e porta l'utente ai flussi di Texfix (apre, avvia, mette in arrivo,
// consegna un piano che gli agenti di un lavoro eseguono, propone profilo e stile). Più la conferma.
inline const QStringList CHAT_TOOLS{"stato_progetto", "dubbi", "avvisi", "apri", "avvia", "aggiungi_materiale", "consegna_piano",
                                    "proponi_profilo", "proponi_stile", "conferma"};

// Una richiesta di permesso dell'agente (lo strumento e i suoi argomenti) tocca un file fuori dal progetto?
bool outside(const QString &root, const QJsonObject &input);
// Tocca il profilo o lo stile del progetto (salvataggi/profilo.md, un .sty, una descrizione, le proposte)? Quelli
// l'agente non li scrive mai: li propone (proponi_profilo, proponi_stile) e l'utente decide.
bool guarded(const QString &root, const QJsonObject &input);
// I file che l'agente della chat non scrive, come regole di permesso del CLI (--disallowedTools).
QString guardedRules();

// Le procedure della chat su profilo e stile: «crea-profilo», «migliora-profilo», «crea-stile», «migliora-stile».
QString procedureMessage(const Project &project, const QString &kind);
// Le proposte che l'agente ha consegnato e che l'utente non ha ancora deciso (in salvataggi/proposte/).
constexpr const char *PROPOSALS = "salvataggi/proposte";

// Gli strumenti senza la finestra: `Texfix --strumenti-progetto <cartella>` legge le richieste da stdin e risponde su
// stdout, sul progetto su disco. Li usa l'agente di un lavoro: solo WORKER_TOOLS, e alle richieste di permesso risponde
// da sé (sì dentro il progetto, no fuori).
int serve(const QString &root);

}
