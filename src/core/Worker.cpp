#include "core/Worker.h"

#include "core/AgentTools.h"
#include "core/Files.h"
#include "core/Model.h"
#include "core/Tools.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QProcess>

namespace tx::worker {

namespace {
QString program()
{
    const QString custom = qEnvironmentVariable("TEXFIX_CLAUDE");     // per le prove: un finto agente
    return custom.isEmpty() ? QStringLiteral("claude") : custom;
}
}

QJsonArray lines(const QJsonObject &output, const QString &root, QHash<QString, QString> *names)
{
    QJsonArray out;
    const QString type = output.value("type").toString();
    const QJsonArray content = output.value("message").toObject().value("content").toArray();
    auto rel = [&](const QString &path) { return path.startsWith(root + '/') ? path.mid(root.size() + 1) : path; };
    if (type == "assistant") {
        for (const QJsonValue &v : content) {
            const QJsonObject block = v.toObject();
            if (block.value("type").toString() == "text" && !block.value("text").toString().trimmed().isEmpty()) {
                out.append(QJsonObject{{"tipo", "testo"}, {"testo", block.value("text").toString().trimmed()}});
                continue;
            }
            if (block.value("type").toString() != "tool_use")
                continue;
            const QString name = block.value("name").toString();
            const QJsonObject input = block.value("input").toObject();
            if (names)
                names->insert(block.value("id").toString(), name);
            const QString file = rel(input.value("file_path").toString()), lesson = input.value("lezione").toString();
            QString said;
            if (name == "Read")
                said = QStringLiteral("Legge %1").arg(file);
            else if (name == "Edit" || name == "MultiEdit")
                said = QStringLiteral("Modifica %1").arg(file);
            else if (name == "Write")
                said = QStringLiteral("Scrive %1").arg(file);
            else if (name == "Grep")
                said = QStringLiteral("Cerca «%1»").arg(input.value("pattern").toString());
            else if (name == "Glob")
                said = QStringLiteral("Cerca i file %1").arg(input.value("pattern").toString());
            else if (name == "mcp__texfix__compila_lezione")
                said = QStringLiteral("Compila %1").arg(lesson);
            else if (name == "mcp__texfix__chiudi_lezione")
                said = QStringLiteral("Chiude %1").arg(lesson);
            else if (name == "mcp__texfix__stato_progetto")
                said = QStringLiteral("Guarda lo stato del progetto");
            else if (name == "mcp__texfix__dubbi")
                said = QStringLiteral("Legge i dubbi di %1").arg(lesson);
            else
                said = name;
            out.append(QJsonObject{{"tipo", "strumento"}, {"testo", said}});
        }
    } else if (type == "user" && names) {
        for (const QJsonValue &v : content) {          // le risposte degli strumenti di Texfix: che cosa dice la compilazione
            const QJsonObject block = v.toObject();
            if (block.value("type").toString() != "tool_result" || !names->value(block.value("tool_use_id").toString()).startsWith("mcp__texfix__"))
                continue;
            QString text;
            const QJsonValue c = block.value("content");
            if (c.isString())
                text = c.toString();
            for (const QJsonValue &part : c.toArray())
                text += part.toObject().value("text").toString();
            text = text.trimmed();
            if (text.size() > 400)
                text = text.left(400) + "…";
            if (!text.isEmpty())
                out.append(QJsonObject{{"tipo", "risultato"}, {"testo", text}});
        }
    }
    return out;
}

Result run(const Project &project, const Settings &settings, const Style &style, const QString &task, const QString &title,
           JobControl *control, const QString &session)
{
    Result out;
    out.session = session;
    const QString model = Model::modelFor(settings.quality, "integrate"), effort = Model::effortFor(settings.quality, "integrate");
    const QString system = agent::workerPrompt(settings, style);
    const QJsonObject server{{"command", QCoreApplication::applicationFilePath()}, {"args", QJsonArray{"--strumenti-progetto", project.root()}}};
    QStringList allowed;
    for (const QString &tool : agent::WORKER_TOOLS)
        allowed << "mcp__texfix__" + tool;
    // chiuso nel progetto: i file sì, una shell no; i permessi li dà `--strumenti-progetto` (dentro sì, fuori no)
    const QStringList base{"-p", "--input-format", "stream-json", "--output-format", "stream-json", "--verbose",
                           "--model", model, "--effort", effort, "--system-prompt", system,
                           "--tools", "Read,Edit,Write,Grep,Glob", "--mcp-config",
                           QString::fromUtf8(QJsonDocument(QJsonObject{{"mcpServers", QJsonObject{{"texfix", server}}}}).toJson(QJsonDocument::Compact)),
                           "--strict-mcp-config", "--allowedTools", allowed.join(','), "--setting-sources", "", "--disable-slash-commands",
                           "--permission-mode", "manual", "--permission-prompt-tool", "mcp__texfix__conferma"};

    const int id = control ? control->nextCall() : 0;
    const QJsonObject event{{"tipo", "chiamata"}, {"id", id}, {"cantiere", Site::current()}, {"etichetta", title}, {"fase", "integrate"},
                            {"modello", model}, {"impegno", effort}, {"inizio", double(QDateTime::currentMSecsSinceEpoch())}};
    const QString record = control && !control->records.isEmpty() ? QStringLiteral("%1/%2.json").arg(control->records).arg(id) : QString();
    QJsonArray stream;
    QElapsedTimer timer;
    timer.start();
    auto report = [&](const QString &state, const QJsonObject &extra = {}) {
        if (!control)
            return;
        QJsonObject e = event;
        e["stato"] = state;
        e["secondi"] = timer.elapsed() / 1000.0;
        e["costo"] = out.cost;
        for (auto it = extra.begin(); it != extra.end(); ++it)
            e[it.key()] = it.value();
        control->event(e);
        if (!record.isEmpty()) {
            e["istruzioni"] = system;
            e["parti"] = QJsonArray{QJsonObject{{"testo", task}}};
            e["flusso"] = stream;
            files::write(record, QString::fromUtf8(QJsonDocument(e).toJson()));
        }
    };

    QString message = session.isEmpty() ? task : QStringLiteral("Eri stato interrotto (il limite di utilizzo, o la connessione): continua il compito da dove eri rimasto. Il compito era:\n\n") + task;
    int failures = 0, drops = 0;
    report(QStringLiteral("in_corso"));
    while (true) {
        if (control) {
            control->check();
            control->checkLimit();
        }
        QStringList arguments = base;
        if (!out.session.isEmpty())
            arguments << "--resume" << out.session;
        QProcess process;
        tools::ownGroup(process);
        process.setProcessEnvironment(tools::environment());
        process.setWorkingDirectory(project.root());
        tools::start(process, program(), arguments);
        if (!process.waitForStarted(10000)) {
            out.failure = QStringLiteral("Non trovo il programma «claude»: serve Claude Code installato.");
            report(QStringLiteral("fallita"), {{"motivo", out.failure}});
            return out;
        }
        const qint64 pid = process.processId();
        if (control)
            control->track(pid);
        process.write(QJsonDocument(QJsonObject{{"type", "user"}, {"message", QJsonObject{{"role", "user"}, {"content", message}}}}).toJson(QJsonDocument::Compact) + '\n');
        process.closeWriteChannel();

        QByteArray buffer;
        QJsonObject result;
        QHash<QString, QString> names;
        int written = 0;
        auto drain = [&] {
            buffer += process.readAllStandardOutput();
            for (qsizetype end; (end = buffer.indexOf('\n')) >= 0;) {
                const QJsonObject line = QJsonDocument::fromJson(buffer.left(end)).object();
                buffer.remove(0, end + 1);
                if (!line.value("session_id").toString().isEmpty())
                    out.session = line.value("session_id").toString();
                if (line.value("type").toString() == "result")
                    result = line;
                for (const QJsonValue &said : lines(line, project.root(), &names)) {
                    stream.append(said);
                    if (control)
                        control->event(QJsonObject{{"tipo", "flusso"}, {"id", id}, {"riga", said}});
                    ++written;
                }
            }
            if (written >= 8) {                        // il registro segue il flusso, a pezzi
                written = 0;
                report(QStringLiteral("in_corso"));
            }
        };
        while (!process.waitForFinished(200)) {        // a piccoli passi: «Ferma» arriva subito
            drain();
            if ((control && control->stopped()) || timer.elapsed() > 90 * 60 * 1000) {
                tools::stopGroup(pid);
                process.waitForFinished(2000);
                break;
            }
        }
        drain();
        if (control) {
            control->untrack(pid);
            if (control->stopped()) {
                report(QStringLiteral("fermata"));
                throw Stopped();
            }
        }
        out.cost += result.value("total_cost_usd").toDouble();
        const QString text = result.value("result").toString().trimmed();
        const QString errors = QString::fromUtf8(process.readAllStandardError()).trimmed();
        QDateTime until;
        if ((text.size() < 300 && Model::limit(text, &until)) || (text.isEmpty() && Model::limit(errors, &until))) {
            const QString said = text.isEmpty() ? errors.simplified().left(300) : text;
            if (!control)
                throw LimitReached(said.toStdString(), until);
            // non è un errore: il lavoro si ferma, e alla ripresa l'agente continua la stessa conversazione
            report(QStringLiteral("limite"), {{"motivo", said}, {"fino", until.toString(Qt::ISODate)}});
            control->pause(until, said);
            out.limited = true;
            return out;
        }
        if (!result.isEmpty() && !result.value("is_error").toBool() && !text.isEmpty()) {
            out.ok = true;
            out.text = text;
            break;
        }
        // la connessione che manca: si aspetta che torni e l'agente continua la sua conversazione; se non torna, il
        // compito si ferma come per il limite (con la conversazione, per la ripresa)
        if (const QString said = (text + '\n' + errors).trimmed(); control && drops < 6 && (Model::offline(said) || !Model::online())) {
            ++drops;
            report(QStringLiteral("connessione"), {{"motivo", said.isEmpty() ? QStringLiteral("nessuna risposta") : said.simplified().left(200)}});
            if (Model::awaitOnline(control)) {
                message = out.session.isEmpty() ? task : QStringLiteral("La connessione si era interrotta: continua il compito da dove eri.");
                report(QStringLiteral("in_corso"));
                continue;
            }
            control->check();
            control->pause(QDateTime::currentDateTime(), said, true);
            report(QStringLiteral("limite"), {{"motivo", QStringLiteral("manca la connessione")}, {"connessione", true}});
            out.limited = true;
            return out;
        }
        const QString why = !text.isEmpty() ? text.left(200) : !errors.isEmpty() ? errors.split('\n').last().left(200)
                                                                               : QStringLiteral("il programma è uscito senza risposta (codice %1)").arg(process.exitCode());
        if (++failures >= 2) {
            out.failure = QStringLiteral("L'agente non ha finito: %1").arg(why);
            break;
        }
        report(QStringLiteral("riprova"), {{"motivo", why}});
        message = out.session.isEmpty() ? task : QStringLiteral("Ti eri fermato senza finire: continua il compito da dove eri.");
    }
    report(out.ok ? QStringLiteral("fatta") : QStringLiteral("fallita"), {{"risposta", out.text}, {"motivo", out.failure}});
    return out;
}

}
