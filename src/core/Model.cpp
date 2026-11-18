#include "core/Model.h"

#include "core/Tools.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSemaphore>
#include <QThread>

namespace tx {

namespace {
QSemaphore g_slots(Model::PARALLEL);

struct Slot {                                          // un posto fra le chiamate in corso, lasciato comunque
    Slot() { g_slots.acquire(); }
    ~Slot() { g_slots.release(); }
};

const char *const SONNET = "claude-sonnet-5-5";
const char *const OPUS = "claude-opus-5-5";

QString program()
{
    const QString custom = qEnvironmentVariable("TEXFIX_CLAUDE");   // per le prove: un finto modello
    return custom.isEmpty() ? QStringLiteral("claude") : custom;
}
}

void JobControl::stop()
{
    m_stopped.store(true);
    QMutexLocker lock(&m_mutex);
    for (const qint64 pid : std::as_const(m_pids))
        tools::stopGroup(pid);
}

void JobControl::track(qint64 pid)
{
    QMutexLocker lock(&m_mutex);
    m_pids << pid;
    if (m_stopped.load())
        tools::stopGroup(pid);
}

void JobControl::untrack(qint64 pid)
{
    QMutexLocker lock(&m_mutex);
    m_pids.removeAll(pid);
}

Model::Model(const QString &quality, const QString &directory, JobControl *control)
    : m_quality(quality), m_directory(directory), m_control(control)
{
}

QString Model::modelFor(const QString &quality, const QString &stage)
{
    if (quality == "massima")
        return OPUS;
    if (quality == "bilanciata" && stage == "figure")  // le figure della qualità bilanciata le disegna il modello più forte
        return OPUS;
    return SONNET;
}

QString Model::effortFor(const QString &quality, const QString &stage)
{
    if (quality == "veloce")
        return stage == "figure" ? "medium" : "low";
    if (quality == "massima")
        return stage == "fix2" || stage == "fix3" ? "high" : "medium";
    if (stage == "transcribe" || stage == "figure" || stage == "fix3")
        return "high";
    return stage == "fix" ? "low" : "medium";            // anche «decide»: un dubbio alla volta, con la pagina
}

QString Model::block(const QString &text, const QString &lang)
{
    const QRegularExpression re("```" + lang + R"([^\n]*\n(.*?)```)", QRegularExpression::DotMatchesEverythingOption);
    const auto m = re.match(text);
    return m.hasMatch() ? m.captured(1) : QString();
}

QJsonValue Model::json(const QString &text)
{
    const qsizetype brace = text.indexOf('{'), bracket = text.indexOf('[');
    const QStringList candidates{block(text, "json"), text, brace < 0 ? QString() : text.mid(brace, text.lastIndexOf('}') + 1 - brace),
                                 bracket < 0 ? QString() : text.mid(bracket, text.lastIndexOf(']') + 1 - bracket)};
    for (const QString &candidate : candidates) {
        if (candidate.trimmed().isEmpty())
            continue;
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(candidate.toUtf8(), &error);
        if (error.error == QJsonParseError::NoError)
            return doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array());
    }
    throw std::runtime_error(("risposta non leggibile: " + text.left(200)).toStdString());
}

QString Model::ask(const QString &stage, const QString &system, const QStringList &parts, const QString &tag)
{
    const QString model = modelFor(m_quality, stage), effort = effortFor(m_quality, stage);
    QJsonArray content;
    for (const QString &part : parts) {
        if (part.endsWith(".png") && !part.contains('\n')) {
            QFile image(part);
            if (image.open(QIODevice::ReadOnly))
                content.append(QJsonObject{{"type", "image"},
                                           {"source", QJsonObject{{"type", "base64"}, {"media_type", "image/png"},
                                                                  {"data", QString::fromLatin1(image.readAll().toBase64())}}}});
        } else {
            content.append(QJsonObject{{"type", "text"}, {"text", part}});
        }
    }
    const QByteArray message = QJsonDocument(QJsonObject{{"type", "user"}, {"message", QJsonObject{{"role", "user"}, {"content", content}}}})
                                   .toJson(QJsonDocument::Compact) + '\n';
    const QStringList arguments{"-p", "--input-format", "stream-json", "--output-format", "stream-json", "--verbose",
                                "--model", model, "--effort", effort, "--no-session-persistence", "--system-prompt", system,
                                "--tools", "", "--setting-sources", "", "--strict-mcp-config", "--disable-slash-commands"};
    static const QRegularExpression limit(R"((hit your (session|usage|weekly) limit|usage limit|rate limit|resets \d))",
                                          QRegularExpression::CaseInsensitiveOption);

    QElapsedTimer timer;
    timer.start();
    QJsonObject result;
    bool got = false;
    QString reason;                                    // perché non ha risposto, se non risponde
    for (int attempt = 0; attempt < 3 && !(got && !result.value("is_error").toBool()); ++attempt) {
        if (m_control)
            m_control->check();
        if (attempt > 0)
            QThread::sleep(5);
        Slot slot;
        if (m_control)
            m_control->check();
        QProcess process;
        tools::ownGroup(process);
        process.setProcessEnvironment(tools::environment());
        process.setWorkingDirectory(m_directory);
        process.start(program(), arguments);
        if (!process.waitForStarted(10000))
            throw std::runtime_error("Non trovo il programma «claude»: serve Claude Code installato.");
        const qint64 pid = process.processId();
        if (m_control)
            m_control->track(pid);
        process.write(message);
        process.closeWriteChannel();
        QElapsedTimer running;
        running.start();
        while (!process.waitForFinished(200)) {        // a piccoli passi: «Ferma» arriva subito
            if ((m_control && m_control->stopped()) || running.elapsed() > 25 * 60 * 1000) {
                tools::stopGroup(pid);
                process.waitForFinished(2000);
                break;
            }
        }
        if (m_control) {
            m_control->untrack(pid);
            m_control->check();
        }
        got = false;
        for (const QByteArray &line : process.readAllStandardOutput().split('\n')) {
            const QJsonObject object = QJsonDocument::fromJson(line).object();
            if (object.value("type").toString() == "result") {
                result = object;
                got = true;
                break;
            }
        }
        const QString text = result.value("result").toString();
        if (text.size() < 300 && limit.match(text).hasMatch())
            throw LimitReached(text.trimmed().toStdString());
        if (text.isEmpty())
            got = false;
        const QStringList errors = QString::fromUtf8(process.readAllStandardError()).trimmed().split('\n', Qt::SkipEmptyParts);
        if (got && result.value("is_error").toBool() && !text.trimmed().isEmpty())
            reason = text.trimmed().left(200);
        else if (!errors.isEmpty())
            reason = errors.last().trimmed().left(200);
        else if (!got)
            reason = process.exitStatus() == QProcess::CrashExit ? QStringLiteral("il programma si è chiuso da sé")
                                                                  : QStringLiteral("il programma è uscito senza risposta (codice %1)").arg(process.exitCode());
    }
    const QJsonObject usage = result.value("usage").toObject();
    Call call{stage, tag, model, effort, timer.elapsed() / 1000.0, result.value("total_cost_usd").toDouble(),
              usage.value("input_tokens").toInt() + usage.value("cache_creation_input_tokens").toInt() + usage.value("cache_read_input_tokens").toInt(),
              usage.value("output_tokens").toInt(), got && !result.value("is_error").toBool()};
    {
        QMutexLocker lock(&m_mutex);
        m_calls << call;
    }
    if (!call.ok)
        throw std::runtime_error(QStringLiteral("Il modello non ha risposto%1").arg(reason.isEmpty() ? QStringLiteral(".") : ": " + reason).toStdString());
    return result.value("result").toString();
}

QList<Model::Call> Model::calls() const
{
    QMutexLocker lock(&m_mutex);
    return m_calls;
}

double Model::cost() const
{
    double total = 0;
    for (const Call &call : calls())
        total += call.cost;
    return total;
}

}
