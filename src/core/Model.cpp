#include "core/Model.h"

#include "core/Files.h"
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
#include <QTimeZone>

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

void JobControl::pause(const QDateTime &until, const QString &message)
{
    const qint64 at = until.isValid() ? until.toMSecsSinceEpoch() : QDateTime::currentMSecsSinceEpoch() + 30 * 60 * 1000;
    qint64 before = m_pausedUntil.load();
    while (at > before && !m_pausedUntil.compare_exchange_weak(before, at)) {
    }
    if (at <= before)
        return;                                        // un'altra chiamata l'ha già detto
    log(QStringLiteral("Limite di utilizzo raggiunto: finisco ciò che è in corso e mi fermo; si può riprendere %1 (%2)")
            .arg(Model::when(QDateTime::fromMSecsSinceEpoch(at)), message.simplified()));
    if (onPause)
        onPause(QDateTime::fromMSecsSinceEpoch(at));
}

void JobControl::checkLimit() const
{
    if (m_pausedUntil.load() > 0)
        throw LimitPause();
}

QDateTime JobControl::pausedUntil() const
{
    const qint64 at = m_pausedUntil.load();
    return at > 0 ? QDateTime::fromMSecsSinceEpoch(at) : QDateTime();
}

namespace {
thread_local QString t_site;
}

Site::Site(const QString &key) : m_previous(t_site) { t_site = key; }
Site::~Site() { t_site = m_previous; }
QString Site::current() { return t_site; }

// «You've hit your 5-hour limit · resets 8:50pm (Europe/Rome)», «resets Nov 20, 7:30pm»: l'ora, nel fuso detto fra
// parentesi (altrimenti quello del computer), più un minuto di margine. Se l'ora non si legge, fra mezz'ora.
QString Model::when(const QDateTime &at)
{
    const QDateTime local = at.toLocalTime();
    const QDate today = QDate::currentDate();
    const QString time = local.toString("HH:mm");
    if (local.date() == today)
        return QStringLiteral("alle %1").arg(time);
    if (local.date() == today.addDays(1))
        return QStringLiteral("domani alle %1").arg(time);
    return QStringLiteral("il %1 alle %2").arg(local.toString("dd/MM"), time);
}

bool Model::limit(const QString &text, QDateTime *until)
{
    static const QRegularExpression said(R"((hit your [^.\n]*limit|usage limit|session limit|weekly limit|rate.?limit|limit reached))",
                                         QRegularExpression::CaseInsensitiveOption);
    if (!said.match(text).hasMatch())
        return false;
    if (!until)
        return true;
    static const QRegularExpression when(R"(resets?\s+(?:(?:on\s+)?([A-Za-z]{3})[a-z]*\.?\s+(\d{1,2}),?\s+(?:at\s+)?)?(\d{1,2})(?::(\d{2}))?\s*(am|pm)(?:\s*\(([A-Za-z_]+(?:/[A-Za-z_+-]+)+)\))?)",
                                         QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = when.match(text);
    const QDateTime now = QDateTime::currentDateTime();
    if (!m.hasMatch()) {
        *until = now.addSecs(30 * 60);
        return true;
    }
    const QTimeZone zone = m.captured(6).isEmpty() ? QTimeZone::systemTimeZone() : QTimeZone(m.captured(6).toUtf8());
    const QDateTime here = now.toTimeZone(zone.isValid() ? zone : QTimeZone::systemTimeZone());
    int hour = m.captured(3).toInt() % 12;
    if (m.captured(5).toLower() == "pm")
        hour += 12;
    QDate day = here.date();
    if (!m.captured(1).isEmpty()) {
        static const QStringList months{"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};
        const int month = int(months.indexOf(m.captured(1).toLower())) + 1;
        if (month > 0)
            day = QDate(day.year(), month, m.captured(2).toInt());
    }
    QDateTime at(day, QTime(hour, m.captured(4).toInt()), here.timeZone());
    if (at <= here)
        at = m.captured(1).isEmpty() ? at.addDays(1) : at.addYears(1);
    *until = at.addSecs(60).toLocalTime();
    return true;
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
    if (quality != "veloce" && stage == "map")         // la mappa dei concetti è un giudizio su tutto il corso: il più forte
        return OPUS;
    return SONNET;
}

QString Model::effortFor(const QString &quality, const QString &stage)
{
    if (quality == "veloce")
        return stage == "figure" ? "medium" : "low";
    if (quality == "massima")
        return stage == "fix2" || stage == "fix3" || stage == "concepts" || stage == "map" || stage == "integrate" ? "high" : "medium";
    if (stage == "transcribe" || stage == "figure" || stage == "fix3" || stage == "concepts" || stage == "map" || stage == "integrate")
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
    QJsonArray content, recorded;
    for (const QString &part : parts) {
        if (part.endsWith(".png") && !part.contains('\n')) {
            QFile image(part);
            if (image.open(QIODevice::ReadOnly))
                content.append(QJsonObject{{"type", "image"},
                                           {"source", QJsonObject{{"type", "base64"}, {"media_type", "image/png"},
                                                                  {"data", QString::fromLatin1(image.readAll().toBase64())}}}});
            recorded.append(QJsonObject{{"immagine", part}});
        } else {
            content.append(QJsonObject{{"type", "text"}, {"text", part}});
            recorded.append(QJsonObject{{"testo", part}});
        }
    }
    const QByteArray message = QJsonDocument(QJsonObject{{"type", "user"}, {"message", QJsonObject{{"role", "user"}, {"content", content}}}})
                                   .toJson(QJsonDocument::Compact) + '\n';
    const QStringList arguments{"-p", "--input-format", "stream-json", "--output-format", "stream-json", "--verbose",
                                "--model", model, "--effort", effort, "--no-session-persistence", "--system-prompt", system,
                                "--tools", "", "--setting-sources", "", "--strict-mcp-config", "--disable-slash-commands"};

    // chi guarda il lavoro vede la chiamata partire e finire; il registro ha istruzioni, parti e risposta
    const int id = m_control ? m_control->nextCall() : 0;
    const qint64 started = QDateTime::currentMSecsSinceEpoch();
    QJsonObject event{{"tipo", "chiamata"}, {"id", id}, {"cantiere", Site::current()}, {"etichetta", tag}, {"fase", stage},
                      {"modello", model}, {"impegno", effort}, {"inizio", double(started)}};
    const QString record = m_control && !m_control->records.isEmpty() ? QStringLiteral("%1/%2.json").arg(m_control->records).arg(id) : QString();
    auto report = [&](const QString &state, const QJsonObject &extra = {}) {
        if (!m_control)
            return;
        QJsonObject e = event;
        e["stato"] = state;
        for (auto it = extra.begin(); it != extra.end(); ++it)
            e[it.key()] = it.value();
        m_control->event(e);
        if (!record.isEmpty()) {
            e["istruzioni"] = system;
            e["parti"] = recorded;
            files::write(record, QString::fromUtf8(QJsonDocument(e).toJson()));
        }
    };

    QElapsedTimer timer;
    timer.start();
    QJsonObject result;
    bool got = false;
    QString reason;                                    // perché non ha risposto, se non risponde
    for (int attempt = 0; attempt < 3 && !(got && !result.value("is_error").toBool()); ++attempt) {
        if (m_control) {
            m_control->check();
            m_control->checkLimit();                   // il limite è stato raggiunto: nessuna chiamata nuova parte
        }
        if (attempt > 0)
            QThread::sleep(5);
        Slot slot;
        if (m_control) {
            m_control->check();
            m_control->checkLimit();                   // il limite è arrivato mentre aspettava il suo posto
        }
        report(attempt == 0 ? QStringLiteral("in_corso") : QStringLiteral("riprova"));
        QProcess process;
        tools::ownGroup(process);
        process.setProcessEnvironment(tools::environment());
        process.setWorkingDirectory(m_directory);
        process.start(program(), arguments);
        if (!process.waitForStarted(10000)) {
            report(QStringLiteral("fallita"), {{"motivo", "Non trovo il programma «claude»"}});
            throw std::runtime_error("Non trovo il programma «claude»: serve Claude Code installato.");
        }
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
            if (m_control->stopped())
                report(QStringLiteral("fermata"), {{"secondi", timer.elapsed() / 1000.0}});
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
        const QStringList errors = QString::fromUtf8(process.readAllStandardError()).trimmed().split('\n', Qt::SkipEmptyParts);
        QDateTime until;
        if ((got && text.size() < 300 && limit(text, &until)) || (!got && limit(errors.join('\n'), &until))) {
            const QString said = got ? text.trimmed() : errors.join(' ').trimmed();
            if (!m_control)
                throw LimitReached(said.toStdString(), until);
            // non è un errore: il lavoro finisce ciò che è in corso e si ferma; questa chiamata si rifà alla ripresa
            report(QStringLiteral("limite"), {{"motivo", said}, {"fino", until.toString(Qt::ISODate)}});
            m_control->pause(until, said);
            throw LimitPause();
        }
        if (text.isEmpty())
            got = false;
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
    report(call.ok ? QStringLiteral("fatta") : QStringLiteral("fallita"),
           {{"secondi", call.seconds}, {"costo", call.cost}, {"risposta", result.value("result").toString()}, {"motivo", reason}});
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
