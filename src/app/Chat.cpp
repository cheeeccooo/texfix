#include "Chat.h"

#include "Workspace.h"
#include "core/AgentTools.h"
#include "core/Files.h"
#include "core/Model.h"
#include "core/NewLesson.h"
#include "core/Tools.h"
#include "core/Usage.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QLocale>
#include <QFile>
#include <QPointer>
#include <QThreadPool>

#include <iostream>
#include <string>

using namespace tx;

Chat *Chat::instance()
{
    static Chat *chat = new Chat;
    return chat;
}

Chat *Chat::create(QQmlEngine *, QJSEngine *)
{
    QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
    return instance();
}

int Chat::relay(const QString &serverName)
{
    QLocalSocket socket;
    socket.connectToServer(serverName);
    if (!socket.waitForConnected(3000))
        return 1;
    std::string line;
    while (std::getline(std::cin, line)) {
        const QByteArray request = QByteArray::fromStdString(line);
        socket.write(request + '\n');
        if (!socket.waitForBytesWritten(5000))
            return 1;
        if (!QJsonDocument::fromJson(request).object().contains("id"))
            continue;                                  // una notifica: nessuna risposta da aspettare
        while (!socket.canReadLine())
            if (!socket.waitForReadyRead(30 * 60 * 1000))
                return 1;
        std::cout << socket.readLine().toStdString() << std::flush;
    }
    return 0;
}

Chat::Chat()
{
    Workspace *workspace = Workspace::instance();
    // gli strumenti: ogni richiesta la esegue il motore in un thread di lavoro, così la finestra non si ferma
    QLocalServer::removeServer(QStringLiteral("texfix-strumenti-%1").arg(QCoreApplication::applicationPid()));
    m_tools.listen(QStringLiteral("texfix-strumenti-%1").arg(QCoreApplication::applicationPid()));
    connect(&m_tools, &QLocalServer::newConnection, this, [this, workspace] {
        QLocalSocket *socket = m_tools.nextPendingConnection();
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket, workspace] {
            while (socket->canReadLine()) {
                const QByteArray request = socket->readLine();
                const QJsonObject message = QJsonDocument::fromJson(request).object();
                const QJsonObject params = message.value("params").toObject();
                if (message.value("method").toString() == "tools/call" && params.value("name").toString() == "conferma") {
                    // una richiesta di conferma: risponde l'utente, dalla scheda nella chat
                    const QJsonObject asked = params.value("arguments").toObject();
                    const QJsonObject input = asked.value("input").toObject();
                    const QString target = input.value("file_path").toString(input.value("command").toString(input.value("pattern").toString()));
                    // l'agente è chiuso nel progetto: ciò che sta fuori si nega senza chiedere, in ogni modo
                    auto reply = [socket, id = message.value("id")](const QJsonObject &decision) {
                        const QJsonObject result{{"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", QString::fromUtf8(QJsonDocument(decision).toJson(QJsonDocument::Compact))}}}}};
                        socket->write(QJsonDocument(QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}}).toJson(QJsonDocument::Compact) + '\n');
                    };
                    if (agent::outside(workspace->root(), input)) {
                        add("note", QStringLiteral("L'agente ha provato a uscire dal progetto (%1): negato.").arg(target));
                        reply(QJsonObject{{"behavior", "deny"}, {"message", "Fuori dal progetto: non è permesso. Lavora solo sui file del progetto."}});
                        continue;
                    }
                    if (agent::guarded(workspace->root(), input) && asked.value("tool_name").toString() != "Read"
                        && asked.value("tool_name").toString() != "Grep" && asked.value("tool_name").toString() != "Glob") {
                        add("note", QStringLiteral("L'agente ha provato a scrivere %1: negato (il profilo e lo stile si propongono).").arg(workspace->project().relative(target)));
                        reply(QJsonObject{{"behavior", "deny"}, {"message", "Il profilo e lo stile non si scrivono a mano: consegna la proposta con "
                                                                              "proponi_profilo o proponi_stile, e l'utente decide."}});
                        continue;
                    }
                    if (m_mode == "autonomo") {        // dentro il progetto non chiede mai
                        reply(QJsonObject{{"behavior", "allow"}, {"updatedInput", input}});
                        continue;
                    }
                    m_asking = socket;
                    m_askingId = message.value("id");
                    m_askingInput = input;
                    add("ask", asked.value("tool_name").toString() + (target.isEmpty() ? QString() : " · " + workspace->project().relative(target)));
                    emit waitingChanged();
                    continue;
                }
                if (message.value("method").toString() == "tools/call")
                    add("tool", params.value("name").toString(),
                        QString::fromUtf8(QJsonDocument(params.value("arguments").toObject()).toJson(QJsonDocument::Indented)).trimmed());
                QPointer<QLocalSocket> target(socket);
                QThreadPool::globalInstance()->start([request, target, project = workspace->project(), style = workspace->style()] {
                    const QByteArray reply = agent::handle(project, style, request);
                    QMetaObject::invokeMethod(qApp, [reply, target] {
                        if (target && !reply.isEmpty())
                            target->write(reply);
                    });
                });
            }
        });
    });

    m_mode = Workspace::settings().value("agente/modo", "auto").toString();
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &Chat::readOutput);
    connect(&m_process, &QProcess::finished, this, &Chat::finished);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            add("note", "Non trovo il programma «claude»: serve Claude Code installato.");
            emit runningChanged();
        }
    });
    connect(workspace, &Workspace::projectChanged, this, [this] {
        stop();
        m_messages.clear();
        m_session.clear();
        m_id.clear();
        m_title.clear();
        load();
        emit messagesChanged();
    });
}

void Chat::setModel(const QString &model)
{
    if (model == m_model || running())
        return;
    m_model = model;
    emit modelChanged();
}

void Chat::add(const QString &role, const QString &text, const QString &detail)
{
    m_messages << QVariantMap{{"role", role}, {"text", text}, {"detail", detail}};
    emit messagesChanged();
}

void Chat::appendToAssistant(const QString &text)
{
    if (m_messages.isEmpty() || m_messages.last().toMap().value("role") != "assistant")
        m_messages << QVariantMap{{"role", "assistant"}, {"text", QString()}};
    QVariantMap last = m_messages.last().toMap();
    last["text"] = last.value("text").toString() + text;
    m_messages.last() = last;
    emit messagesChanged();
}

void Chat::send(const QString &typed, const QStringList &attachments)
{
    Workspace *workspace = Workspace::instance();
    if ((typed.trimmed().isEmpty() && attachments.isEmpty()) || running() || !workspace->hasProject())
        return;
    if (workspace->busy())
        return workspace->say(QStringLiteral("Aspetta la fine del lavoro in corso."));
    // il limite di utilizzo già raggiunto: il messaggio parte da sé alla ripresa
    if (workspace->limited(QStringLiteral("Il messaggio all'agente"), [this, typed, attachments] { send(typed, attachments); }, "chat:" + typed))
        return add("note", QStringLiteral("Limite di utilizzo di Claude raggiunto: il messaggio parte da sé %1 (in Attività: «Adesso» o «Lascia stare»).")
                               .arg(Model::when(tx::usage::current().blockedUntil())));
    workspace->checkpoint(QStringLiteral("Modifiche a mano"));
    if (m_id.isEmpty())
        m_id = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    // gli allegati entrano nel progetto, fra i salvataggi della chat: l'agente li legge da lì
    QString text = typed.trimmed();
    QStringList kept;
    for (const QString &path : attachments) {
        if (!QFileInfo(path).isFile())
            continue;
        const QString to = files::freeName(workspace->project().path("salvataggi/chat/allegati/" + m_id + '/' + QFileInfo(path).fileName()));
        QDir().mkpath(QFileInfo(to).path());
        if (QFile::copy(path, to))
            kept << workspace->project().relative(to);
    }
    if (!kept.isEmpty())
        text += QStringLiteral("\n\nFile allegati (leggili):\n- ") + newlesson::forAgent(workspace->project(), kept).join("\n- ");
    if (m_title.isEmpty())
        m_title = typed.simplified().left(48);
    m_messages << QVariantMap{{"role", "user"}, {"text", typed.trimmed()}, {"attachments", kept}};
    emit messagesChanged();
    m_retried = false;
    launch(text);
}

void Chat::resend(const QString &id, const QString &text)
{
    Workspace *workspace = Workspace::instance();
    if (id != m_id || !workspace->hasProject())
        return workspace->say(QStringLiteral("Il messaggio fermato dal limite era di un'altra chat: non lo rimando."));
    if (running() || workspace->busy())
        return workspace->say(QStringLiteral("Aspetta la fine del lavoro in corso."));
    if (workspace->limited(QStringLiteral("Il messaggio all'agente"), [this, id, text] { resend(id, text); }))
        return;
    add("note", QStringLiteral("Riprendo il messaggio fermato dal limite."));
    m_retried = false;
    launch(text);
}

void Chat::launch(const QString &text)
{
    Workspace *workspace = Workspace::instance();
    m_lastText = text;
    m_answered = false;

    const QJsonObject server{{"command", QCoreApplication::applicationFilePath()}, {"args", QJsonArray{"--strumenti", m_tools.serverName()}}};
    QStringList arguments{"-p", "--input-format", "stream-json", "--output-format", "stream-json", "--verbose", "--include-partial-messages",
                          "--model", m_model, "--system-prompt", agent::systemPrompt(workspace->settings_(), workspace->style()),
                          // chiuso nel progetto: i file sì, una shell no; solo gli strumenti di Texfix, nessuna impostazione dell'utente
                          "--tools", "Read,Edit,Write,Grep,Glob", "--mcp-config",
                          QString::fromUtf8(QJsonDocument(QJsonObject{{"mcpServers", QJsonObject{{"texfix", server}}}}).toJson(QJsonDocument::Compact)),
                          "--strict-mcp-config", "--allowedTools",
                          "mcp__texfix__stato_progetto,mcp__texfix__compila_lezione,mcp__texfix__compila_documento,mcp__texfix__dubbi,mcp__texfix__impagina,mcp__texfix__chiudi_lezione,mcp__texfix__aggiungi_materiale,mcp__texfix__consegna_piano,mcp__texfix__consegna_esito,mcp__texfix__proponi_profilo,mcp__texfix__proponi_stile",
                          // il profilo e lo stile non si scrivono: si propongono, e decide l'utente
                          "--disallowedTools", agent::guardedRules(),
                          "--setting-sources", "", "--disable-slash-commands"};
    // ciò che vuole una conferma arriva al programma: fuori dal progetto si nega sempre; dentro, «Autonomo» consente da sé
    // e gli altri due modi chiedono all'utente
    arguments << "--permission-mode" << (m_mode == "auto" ? "auto" : "manual") << "--permission-prompt-tool" << "mcp__texfix__conferma";
    if (!m_session.isEmpty())
        arguments << "--resume" << m_session;
    const QString custom = qEnvironmentVariable("TEXFIX_CLAUDE");     // per le prove: un finto agente
    m_buffer.clear();
    m_streamed = false;
    tools::ownGroup(m_process);
    m_process.setProcessEnvironment(tools::environment());
    m_process.setWorkingDirectory(workspace->root());
    tools::start(m_process, custom.isEmpty() ? QStringLiteral("claude") : custom, arguments);
    const QJsonObject message{{"type", "user"}, {"message", QJsonObject{{"role", "user"}, {"content", text}}}};
    m_process.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
    m_process.closeWriteChannel();
    workspace->setAgentRunning(true);
    emit runningChanged();
}

void Chat::readOutput()
{
    m_buffer += m_process.readAllStandardOutput();
    qsizetype end;
    while ((end = m_buffer.indexOf('\n')) >= 0) {
        const QJsonObject line = QJsonDocument::fromJson(m_buffer.left(end)).object();
        m_buffer.remove(0, end + 1);
        tx::usage::record(line);                       // la barra dell'utilizzo
        const QString type = line.value("type").toString();
        if (!line.value("session_id").toString().isEmpty())
            m_session = line.value("session_id").toString();
        if (type == "assistant" || type == "stream_event")
            m_answered = true;
        if (type == "stream_event") {                  // il testo, a pezzi mentre arriva
            const QJsonObject event = line.value("event").toObject();
            if (event.value("type").toString() == "content_block_delta" && event.value("delta").toObject().value("type").toString() == "text_delta") {
                m_streamed = true;
                appendToAssistant(event.value("delta").toObject().value("text").toString());
            }
        } else if (type == "assistant") {              // il messaggio intero: il testo se non è arrivato a pezzi, gli strumenti dei file
            for (const QJsonValue &v : line.value("message").toObject().value("content").toArray()) {
                const QJsonObject block = v.toObject();
                if (block.value("type").toString() == "text" && !m_streamed)
                    appendToAssistant(block.value("text").toString());
                if (block.value("type").toString() == "tool_use" && !block.value("name").toString().startsWith("mcp__")) {
                    const QJsonObject input = block.value("input").toObject();
                    const QString target = input.value("file_path").toString(input.value("pattern").toString());
                    QJsonObject shown = input;                 // il dettaglio: che cosa ha chiesto, senza i testi lunghi
                    for (const QString &key : shown.keys())
                        if (shown.value(key).isString() && shown.value(key).toString().size() > 600)
                            shown[key] = shown.value(key).toString().left(600) + "…";
                    add("tool", block.value("name").toString() + (target.isEmpty() ? QString() : " · " + Workspace::instance()->project().relative(target)),
                        QString::fromUtf8(QJsonDocument(shown).toJson(QJsonDocument::Indented)).trimmed());
                }
            }
            m_streamed = false;
        } else if (type == "result" && line.value("is_error").toBool()) {
            const QString said = line.value("result").toString();
            QDateTime until;
            if (Model::limit(said, &until)) {          // il limite di utilizzo: detto con l'ora in cui si può riprendere
                if (!m_messages.isEmpty() && m_messages.last().toMap().value("role") == "assistant"
                    && m_messages.last().toMap().value("text").toString().trimmed() == said.trimmed())
                    m_messages.removeLast();
                m_answered = true;                     // ha risposto: non è una chat da riprendere da capo
                if (const QDateTime known = tx::usage::current().blockedUntil(); known.isValid())
                    until = known;                     // l'ora detta dal CLI, più esatta di quella letta dal messaggio
                // non è stato eseguito: riparte da sé alla ripresa, in questa stessa chat
                Workspace::instance()->later(QStringLiteral("Il messaggio all'agente"), [this, id = m_id, text = m_lastText] { resend(id, text); }, until);
                add("note", QStringLiteral("Limite di utilizzo di Claude raggiunto: il messaggio non è stato eseguito, e riparte da sé %1 "
                                           "(in Attività: «Adesso» o «Lascia stare»).").arg(Model::when(until)));
            } else if (Model::offline(said)) {
                add("note", QStringLiteral("Manca la connessione a internet: il messaggio non è stato eseguito. Rimandalo quando torna."));
            } else {
                add("note", said);
            }
        }
    }
}

void Chat::finished()
{
    readOutput();
    // la chat di prima non si riprende più (la sua memoria non c'è): riparte con la conversazione nel messaggio
    if (!m_answered && !m_session.isEmpty() && !m_retried && !m_lastText.isEmpty()) {
        m_retried = true;
        m_session.clear();
        if (!m_messages.isEmpty() && m_messages.last().toMap().value("role") == "note")
            m_messages.removeLast();
        QStringList talk;
        for (qsizetype i = 0; i + 1 < m_messages.size(); ++i) {
            const QVariantMap m = m_messages[i].toMap();
            if (m.value("role") == "user" || m.value("role") == "assistant")
                talk << (m.value("role") == "user" ? QStringLiteral("Utente: ") : QStringLiteral("Tu: ")) + m.value("text").toString();
        }
        return launch(QStringLiteral("La conversazione fin qui:\n\n%1\n\nIl messaggio nuovo:\n%2").arg(talk.join("\n\n").right(30000), m_lastText));
    }
    Workspace *workspace = Workspace::instance();
    workspace->setAgentRunning(false);
    workspace->agentTurnDone();                        // il turno dell'agente è un passo della cronologia
    save();
    emit runningChanged();
}

void Chat::stop()
{
    if (!running())
        return;
    m_asking.clear();
    emit waitingChanged();
    tools::stopGroup(m_process.processId());
    m_process.waitForFinished(2000);
    add("note", "Fermato.");
}

void Chat::newChat()
{
    stop();
    m_messages.clear();
    m_session.clear();
    m_id.clear();
    m_title.clear();
    emit messagesChanged();
}

void Chat::save() const
{
    const Workspace *workspace = Workspace::instance();
    if (!workspace->hasProject() || m_id.isEmpty())
        return;
    const QJsonObject chat{{"sessione", m_session}, {"modello", m_model}, {"titolo", m_title}, {"messaggi", QJsonArray::fromVariantList(m_messages)}};
    files::write(workspace->project().path("salvataggi/chat/" + m_id + ".json"), QString::fromUtf8(QJsonDocument(chat).toJson()));
}

// All'apertura del progetto torna l'ultima chat, per riprenderla.
void Chat::load()
{
    const Workspace *workspace = Workspace::instance();
    if (!workspace->hasProject())
        return;
    const QStringList chats = QDir(workspace->project().path("salvataggi/chat")).entryList({"*.json"}, QDir::Files, QDir::Name);
    if (!chats.isEmpty())
        openChat(QFileInfo(chats.last()).completeBaseName());
}

void Chat::openChat(const QString &id)
{
    const Workspace *workspace = Workspace::instance();
    const QString path = workspace->project().path("salvataggi/chat/" + id + ".json");
    if (running() || !QFileInfo::exists(path))
        return;
    const QJsonObject chat = QJsonDocument::fromJson(files::read(path).toUtf8()).object();
    m_id = id;
    m_session = chat.value("sessione").toString();
    m_title = chat.value("titolo").toString();
    m_messages = chat.value("messaggi").toArray().toVariantList();
    if (!chat.value("modello").toString().isEmpty())
        m_model = chat.value("modello").toString();
    emit modelChanged();
    emit messagesChanged();
}

QVariantList Chat::chats() const
{
    QVariantList out;
    const Workspace *workspace = Workspace::instance();
    if (!workspace->hasProject())
        return out;
    const QDir dir(workspace->project().path("salvataggi/chat"));
    for (const QString &name : dir.entryList({"*.json"}, QDir::Files, QDir::Name | QDir::Reversed)) {
        const QString id = QFileInfo(name).completeBaseName();
        const QJsonObject chat = QJsonDocument::fromJson(files::read(dir.filePath(name)).toUtf8()).object();
        QString title = chat.value("titolo").toString();
        if (title.isEmpty())                           // le chat di prima: la prima cosa detta
            title = chat.value("messaggi").toArray().first().toObject().value("text").toString().simplified().left(48);
        const QDateTime when = QDateTime::fromString(id, "yyyyMMdd-HHmmss");
        out << QVariantMap{{"id", id}, {"title", title.isEmpty() ? QStringLiteral("Chat") : title},
                           {"when", QLocale(QLocale::Italian).toString(when, "d MMM, HH:mm")}, {"current", id == m_id}};
    }
    return out;
}

void Chat::removeChat(const QString &id)
{
    const Workspace *workspace = Workspace::instance();
    if (!workspace->hasProject() || (id == m_id && running()))
        return;
    QFile::remove(workspace->project().path("salvataggi/chat/" + id + ".json"));
    QDir(workspace->project().path("salvataggi/chat/allegati/" + id)).removeRecursively();
    if (id == m_id)
        newChat();
    else
        emit messagesChanged();
}

void Chat::startProcedure(const QString &title, const QString &message)
{
    if (running())
        return;
    newChat();
    m_title = title;
    send(message);
}

QString Chat::shotPath() const
{
    const QString dir = Workspace::instance()->project().path("build/foto");
    QDir().mkpath(dir);
    return dir + "/Foto " + QDateTime::currentDateTime().toString("HH.mm.ss") + ".png";
}

void Chat::setMode(const QString &mode)
{
    if (mode == m_mode || running() || !QStringList{"auto", "chiedi", "autonomo"}.contains(mode))
        return;
    m_mode = mode;
    Workspace::settings().setValue("agente/modo", mode);
    emit modeChanged();
}

void Chat::answer(bool allow)
{
    if (!m_asking)
        return;
    const QJsonObject decision = allow ? QJsonObject{{"behavior", "allow"}, {"updatedInput", m_askingInput}}
                                       : QJsonObject{{"behavior", "deny"}, {"message", "L'utente ha detto di no."}};
    const QJsonObject result{{"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", QString::fromUtf8(QJsonDocument(decision).toJson(QJsonDocument::Compact))}}}}};
    m_asking->write(QJsonDocument(QJsonObject{{"jsonrpc", "2.0"}, {"id", m_askingId}, {"result", result}}).toJson(QJsonDocument::Compact) + '\n');
    m_asking.clear();
    // la scheda della domanda resta, con la risposta data
    for (qsizetype i = m_messages.size() - 1; i >= 0; --i) {
        QVariantMap message = m_messages[i].toMap();
        if (message.value("role") != "ask")
            continue;
        message["role"] = "tool";
        message["text"] = message.value("text").toString() + (allow ? " · consentito" : " · negato");
        m_messages[i] = message;
        break;
    }
    emit messagesChanged();
    emit waitingChanged();
}
