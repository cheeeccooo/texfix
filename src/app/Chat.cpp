#include "Chat.h"

#include "Workspace.h"
#include "core/AgentTools.h"
#include "core/Files.h"
#include "core/Tools.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
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
                    m_asking = socket;
                    m_askingId = message.value("id");
                    m_askingInput = input;
                    add("ask", asked.value("tool_name").toString() + (target.isEmpty() ? QString() : " · " + workspace->project().relative(target)));
                    emit waitingChanged();
                    continue;
                }
                if (message.value("method").toString() == "tools/call")
                    add("tool", params.value("name").toString());
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

void Chat::add(const QString &role, const QString &text)
{
    m_messages << QVariantMap{{"role", role}, {"text", text}};
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

void Chat::send(const QString &text)
{
    Workspace *workspace = Workspace::instance();
    if (text.trimmed().isEmpty() || running() || !workspace->hasProject())
        return;
    if (workspace->busy())
        return workspace->say(QStringLiteral("Aspetta la fine del lavoro in corso."));
    workspace->checkpoint(QStringLiteral("Modifiche a mano"));
    if (m_id.isEmpty())
        m_id = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    add("user", text.trimmed());

    const QJsonObject server{{"command", QCoreApplication::applicationFilePath()}, {"args", QJsonArray{"--strumenti", m_tools.serverName()}}};
    QStringList arguments{"-p", "--input-format", "stream-json", "--output-format", "stream-json", "--verbose", "--include-partial-messages",
                          "--model", m_model, "--system-prompt", agent::systemPrompt(workspace->settings_(), workspace->style()),
                          // chiuso nel progetto: i file sì, una shell no; solo gli strumenti di Texfix, nessuna impostazione dell'utente
                          "--tools", "Read,Edit,Write,Grep,Glob", "--mcp-config",
                          QString::fromUtf8(QJsonDocument(QJsonObject{{"mcpServers", QJsonObject{{"texfix", server}}}}).toJson(QJsonDocument::Compact)),
                          "--strict-mcp-config", "--allowedTools",
                          "mcp__texfix__stato_progetto,mcp__texfix__compila_lezione,mcp__texfix__compila_documento,mcp__texfix__dubbi,mcp__texfix__impagina,mcp__texfix__consegna_piano,mcp__texfix__consegna_esito",
                          "--setting-sources", "", "--disable-slash-commands"};
    if (m_mode == "autonomo")
        arguments << "--permission-mode" << "bypassPermissions";
    else                                               // ciò che vuole una conferma arriva alla finestra, non viene negato in silenzio
        arguments << "--permission-mode" << (m_mode == "chiedi" ? "manual" : "auto") << "--permission-prompt-tool" << "mcp__texfix__conferma";
    if (!m_session.isEmpty())
        arguments << "--resume" << m_session;
    const QString custom = qEnvironmentVariable("TEXFIX_CLAUDE");     // per le prove: un finto agente
    m_buffer.clear();
    m_streamed = false;
    tools::ownGroup(m_process);
    m_process.setProcessEnvironment(tools::environment());
    m_process.setWorkingDirectory(workspace->root());
    m_process.start(custom.isEmpty() ? QStringLiteral("claude") : custom, arguments);
    const QJsonObject message{{"type", "user"}, {"message", QJsonObject{{"role", "user"}, {"content", text.trimmed()}}}};
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
        const QString type = line.value("type").toString();
        if (!line.value("session_id").toString().isEmpty())
            m_session = line.value("session_id").toString();
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
                    add("tool", block.value("name").toString() + (target.isEmpty() ? QString() : " · " + Workspace::instance()->project().relative(target)));
                }
            }
            m_streamed = false;
        } else if (type == "result" && line.value("is_error").toBool()) {
            add("note", line.value("result").toString());
        }
    }
}

void Chat::finished()
{
    readOutput();
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
    emit messagesChanged();
}

void Chat::save() const
{
    const Workspace *workspace = Workspace::instance();
    if (!workspace->hasProject() || m_id.isEmpty())
        return;
    const QJsonObject chat{{"sessione", m_session}, {"modello", m_model}, {"messaggi", QJsonArray::fromVariantList(m_messages)}};
    files::write(workspace->project().path("salvataggi/chat/" + m_id + ".json"), QString::fromUtf8(QJsonDocument(chat).toJson()));
}

// All'apertura del progetto torna l'ultima chat, per riprenderla.
void Chat::load()
{
    const Workspace *workspace = Workspace::instance();
    if (!workspace->hasProject())
        return;
    const QStringList chats = QDir(workspace->project().path("salvataggi/chat")).entryList({"*.json"}, QDir::Files, QDir::Name);
    if (chats.isEmpty())
        return;
    m_id = QFileInfo(chats.last()).completeBaseName();
    const QJsonObject chat = QJsonDocument::fromJson(files::read(workspace->project().path("salvataggi/chat/" + chats.last())).toUtf8()).object();
    m_session = chat.value("sessione").toString();
    m_messages = chat.value("messaggi").toArray().toVariantList();
    if (!chat.value("modello").toString().isEmpty())
        m_model = chat.value("modello").toString();
    emit modelChanged();
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
