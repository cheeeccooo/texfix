#include "core/Usage.h"

#include "core/Files.h"

#include <QJsonDocument>
#include <QList>
#include <QMutex>

namespace tx::usage {

namespace {
QMutex g_mutex;
State g_state;
bool g_loaded = false;
QString g_file;
QList<std::pair<QDateTime, double>> g_samples;     // la finestra di cinque ore: quando, quanto
std::function<void()> g_callback;
Rate g_rate;                                           // quanto sale ogni finestra per un dollaro
double g_cap = 0;
// da dove si misura il prossimo salto di una finestra: l'utilizzo e l'azzeramento di allora, e quanto si è speso da lì
struct Anchor {
    double used = -1;
    QDateTime resets;
    double spent = 0;
};
Anchor g_hoursFrom, g_weekFrom;

// un salto abbastanza grande, con abbastanza speso, insegna il rapporto; una finestra azzerata ricomincia
void learn(Anchor &from, const Window &now, double step, double &rate)
{
    if (!now.known())
        return;
    if (from.used < 0 || from.resets != now.resets || now.used < from.used) {
        from = {now.used, now.resets, 0};
        return;
    }
    if (now.used - from.used < step || from.spent < 0.03)
        return;
    const double seen = (now.used - from.used) / from.spent;
    rate = rate > 0 ? 0.6 * rate + 0.4 * seen : seen;   // un salto strano (la chat da un'altra parte) non sposta tutto
    from = {now.used, now.resets, 0};
}

QString file()
{
    if (!g_file.isEmpty())
        return g_file;
    return files::dataDir() + "/utilizzo.json";
}

Window windowOf(const QJsonObject &o)
{
    Window w;
    if (o.contains("utilization"))
        w.used = qBound(0.0, o.value("utilization").toDouble(), 1.0);
    if (o.contains("resetsAt"))
        w.resets = QDateTime::fromSecsSinceEpoch(qint64(o.value("resetsAt").toDouble()));
    w.status = o.value("status").toString();
    if (w.status == "rejected" && !w.known())
        w.used = 1;
    return w;
}

QJsonObject toJson(const Window &w)
{
    QJsonObject o{{"status", w.status}};
    if (w.known())
        o["utilization"] = w.used;
    if (w.resets.isValid())
        o["resetsAt"] = double(w.resets.toSecsSinceEpoch());
    return o;
}

// lo stato ricordato dall'ultima volta (con g_mutex preso)
void load()
{
    if (g_loaded)
        return;
    g_loaded = true;
    const QJsonObject saved = QJsonDocument::fromJson(files::read(file()).toUtf8()).object();
    g_state.hours = windowOf(saved.value("five_hour").toObject());
    g_state.week = windowOf(saved.value("seven_day").toObject());
    g_state.seen = QDateTime::fromString(saved.value("seen").toString(), Qt::ISODate);
    g_rate.hours = saved.value("per_dollaro").toObject().value("five_hour").toDouble();
    g_rate.week = saved.value("per_dollaro").toObject().value("seven_day").toDouble();
}
}

QDateTime State::blockedUntil() const
{
    const QDateTime now = QDateTime::currentDateTime();
    QDateTime until;
    for (const Window *w : {&hours, &week})
        if (w->status == "rejected" && w->resets.isValid() && w->resets > now && (!until.isValid() || w->resets > until))
            until = w->resets;
    return until;
}

void record(const QJsonObject &line)
{
    if (line.value("type").toString() == "result" && line.value("total_cost_usd").toDouble() > 0) {
        QMutexLocker lock(&g_mutex);                   // quanto si è speso dall'ultimo campione
        g_hoursFrom.spent += line.value("total_cost_usd").toDouble();
        g_weekFrom.spent += line.value("total_cost_usd").toDouble();
        return;
    }
    if (line.value("type").toString() != "rate_limit_event")
        return;
    const QJsonObject info = line.value("rate_limit_info").toObject();
    const QJsonObject windows = info.value("unifiedWindows").toObject();
    std::function<void()> callback;
    {
        QMutexLocker lock(&g_mutex);
        load();
        if (!windows.isEmpty()) {
            g_state.hours = windowOf(windows.value("five_hour").toObject());
            g_state.week = windowOf(windows.value("seven_day").toObject());
        } else {                                       // un CLI di prima: una finestra sola, quella che conta ora
            Window w = windowOf(info);
            (info.value("rateLimitType").toString() == "seven_day" ? g_state.week : g_state.hours) = w;
        }
        g_state.seen = QDateTime::currentDateTime();
        if (g_state.hours.known()) {
            // i campioni dell'ultima finestra; se si è azzerata, si ricomincia
            if (!g_samples.isEmpty() && g_samples.last().second > g_state.hours.used + 0.2)
                g_samples.clear();
            g_samples.append({g_state.seen, g_state.hours.used});
            while (g_samples.size() > 2 && g_samples.first().first.secsTo(g_state.seen) > 5 * 3600)
                g_samples.removeFirst();
        }
        learn(g_hoursFrom, g_state.hours, 0.03, g_rate.hours);
        learn(g_weekFrom, g_state.week, 0.01, g_rate.week);
        files::write(file(), QString::fromUtf8(QJsonDocument(QJsonObject{{"five_hour", toJson(g_state.hours)}, {"seven_day", toJson(g_state.week)},
                                                                         {"seen", g_state.seen.toString(Qt::ISODate)},
                                                                         {"per_dollaro", QJsonObject{{"five_hour", g_rate.hours}, {"seven_day", g_rate.week}}}}).toJson()));
        callback = g_callback;
    }
    if (callback)
        callback();
}

State current()
{
    QMutexLocker lock(&g_mutex);
    load();
    return g_state;
}

QDateTime runsOutAt(const QDateTime &since)
{
    QMutexLocker lock(&g_mutex);
    QList<std::pair<QDateTime, double>> recent;
    for (const auto &s : std::as_const(g_samples))
        if (s.first >= since)
            recent << s;
    if (recent.size() < 2)
        return {};
    const auto &[t0, u0] = recent.first();
    const auto &[t1, u1] = recent.last();
    const qint64 seconds = t0.secsTo(t1);
    if (seconds < 120 || u1 <= u0)                     // troppo presto per dire un ritmo, o non sale
        return {};
    const double perSecond = (u1 - u0) / double(seconds);
    return t1.addSecs(qint64((1.0 - u1) / perSecond));
}

Rate perDollar()
{
    QMutexLocker lock(&g_mutex);
    load();
    return g_rate;
}

void setCap(double fraction)
{
    QMutexLocker lock(&g_mutex);
    g_cap = fraction > 0 && fraction < 1 ? fraction : 0;
}

double cap()
{
    QMutexLocker lock(&g_mutex);
    return g_cap;
}

QDateTime cappedUntil()
{
    QMutexLocker lock(&g_mutex);
    load();
    if (g_cap <= 0)
        return {};
    const QDateTime now = QDateTime::currentDateTime();
    QDateTime until;
    for (const Window *w : {&g_state.hours, &g_state.week})   // una finestra già azzerata non conta, anche se l'ultimo numero era alto
        if (w->known() && w->used >= g_cap && w->resets.isValid() && w->resets > now && (!until.isValid() || w->resets > until))
            until = w->resets;
    return until;
}

void onChange(std::function<void()> callback)
{
    QMutexLocker lock(&g_mutex);
    g_callback = std::move(callback);
}

void setFile(const QString &path)
{
    QMutexLocker lock(&g_mutex);
    g_file = path;
    g_loaded = false;
    g_state = {};
    g_samples.clear();
    g_rate = {};
    g_hoursFrom = g_weekFrom = {};
}

}
