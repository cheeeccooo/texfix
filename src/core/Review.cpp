#include "core/Review.h"

#include "core/Convert.h"
#include "core/Files.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace tx::review {

namespace {
const char *const DECLARED = "salvataggi/correzioni.json";

QJsonObject readJson(const QString &path) { return QJsonDocument::fromJson(files::read(path).toUtf8()).object(); }
bool writeJson(const QString &path, const QJsonObject &o) { return files::write(path, QString::fromUtf8(QJsonDocument(o).toJson())); }

QString hashOf(const QString &text)
{
    return QString::fromLatin1(QCryptographicHash::hash(text.simplified().toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
}

// La riga (da 1) in cui `excerpt` compare, se compare una volta sola; altrimenti 0.
int lineOnce(const QString &text, const QString &excerpt)
{
    if (excerpt.isEmpty())
        return 0;
    const qsizetype at = text.indexOf(excerpt);
    if (at < 0 || text.indexOf(excerpt, at + 1) >= 0)
        return 0;
    return int(text.left(at).count('\n')) + 1;
}

// Cambia un dubbio nel suo file. `change` restituisce il perché se non si può.
QString edit(const Project &project, const QString &lesson, const QString &id, const std::function<QString(QJsonObject &, QString &)> &change)
{
    const QString path = project.path(Convert::doubtsFile(lesson));
    QJsonObject root = readJson(path);
    QJsonArray all = root.value("dubbi").toArray();
    for (qsizetype i = 0; i < all.size(); ++i) {
        QJsonObject doubt = all[i].toObject();
        if (doubt.value("id").toString() != id)
            continue;
        QString text = files::read(project.path(lesson));
        const QString before = text;
        if (const QString error = change(doubt, text); !error.isEmpty())
            return error;
        if (text != before && !files::write(project.path(lesson), text))
            return QStringLiteral("Non riesco a scrivere la lezione.");
        all[i] = doubt;
        root["dubbi"] = all;
        return writeJson(path, root) ? QString() : QStringLiteral("Non riesco a salvare la decisione.");
    }
    return QStringLiteral("Questo dubbio non c'è più.");
}
}

QList<Doubt> doubts(const Project &project, const QString &lesson)
{
    QList<Doubt> out;
    const QString text = files::read(project.path(lesson));
    for (const QJsonValue &v : readJson(project.path(Convert::doubtsFile(lesson))).value("dubbi").toArray()) {
        const QJsonObject o = v.toObject();
        Doubt d;
        d.id = o.value("id").toString();
        d.lesson = lesson;
        d.what = o.value("what").toString();
        d.latex = o.value("ora").toString(o.value("latex").toString());
        d.source = o.value("source").toString();
        d.page = o.value("page").toInt();
        for (const QJsonValue &r : o.value("readings").toArray())
            d.readings << Reading{r.toObject().value("text").toString(), r.toObject().value("latex").toString()};
        d.open = o.value("stato").toString("aperto") == "aperto";
        d.chosen = o.value("scelta").toInt(-1);
        d.how = o.value("come").toString();
        d.line = lineOnce(text, d.latex);
        out << d;
    }
    return out;
}

QString choose(const Project &project, const QString &lesson, const QString &id, int reading)
{
    return edit(project, lesson, id, [&](QJsonObject &doubt, QString &text) -> QString {
        const QJsonArray readings = doubt.value("readings").toArray();
        if (reading < 0 || reading >= readings.size())
            return QStringLiteral("Questa lettura non c'è.");
        const QString now = doubt.value("ora").toString(doubt.value("latex").toString());
        const QString wanted = readings[reading].toObject().value("latex").toString();
        if (!wanted.isEmpty() && wanted != now) {
            if (!lineOnce(text, now))
                return QStringLiteral("Non trovo il passo nel testo una volta sola: correggilo a mano nella lezione.");
            text.replace(now, wanted);
            doubt["ora"] = wanted;
        }
        doubt["stato"] = "deciso";
        doubt["scelta"] = reading;
        doubt["come"] = reading == 0 ? "confermata" : "scelta";
        return {};
    });
}

QString reopen(const Project &project, const QString &lesson, const QString &id, bool revert)
{
    return edit(project, lesson, id, [&](QJsonObject &doubt, QString &text) -> QString {
        const QString now = doubt.value("ora").toString(), original = doubt.value("latex").toString();
        if (revert && !now.isEmpty() && now != original) {
            if (!lineOnce(text, now))
                return QStringLiteral("Non trovo più il passo nel testo: non posso rimetterlo com'era.");
            text.replace(now, original);
            doubt.remove("ora");
        }
        doubt["stato"] = "aperto";
        doubt.remove("scelta");
        doubt.remove("come");
        return {};
    });
}

QList<Declared> declared(const Project &project, const QString &lesson)
{
    static const QRegularExpression env(R"(\\begin\{(addedtext|correction)\}(.*?)\\end\{\1\})", QRegularExpression::DotMatchesEverythingOption);
    QList<Declared> out;
    const QJsonObject saved = readJson(project.path(DECLARED));
    const QJsonArray kept = saved.value("tenute").toObject().value(lesson).toArray();
    const QString text = files::read(project.path(lesson));
    for (auto it = env.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        Declared d{lesson, m.captured(1), m.captured(2).simplified(), hashOf(m.captured(2)), int(text.left(m.capturedStart()).count('\n')) + 1, "aperta"};
        if (kept.contains(d.hash))
            d.state = "tenuta";
        out << d;
    }
    for (const QJsonValue &v : saved.value("tolte").toArray()) {
        const QJsonObject o = v.toObject();
        if (o.value("lezione").toString() == lesson)
            out << Declared{lesson, o.value("tipo").toString(), o.value("testo").toString(), o.value("impronta").toString(), 0, "tolta"};
    }
    return out;
}

void keep(const Project &project, const QString &lesson, const QString &hash, bool on)
{
    QJsonObject saved = readJson(project.path(DECLARED));
    QJsonObject kept = saved.value("tenute").toObject();
    QJsonArray list = kept.value(lesson).toArray();
    for (qsizetype i = list.size() - 1; i >= 0; --i)
        if (list[i].toString() == hash)
            list.removeAt(i);
    if (on)
        list.append(hash);
    kept[lesson] = list;
    saved["tenute"] = kept;
    writeJson(project.path(DECLARED), saved);
}

void keepAll(const Project &project, const QString &lesson)
{
    for (const Declared &d : declared(project, lesson))
        if (d.state == "aperta")
            keep(project, lesson, d.hash);
}

QString drop(const Project &project, const QString &lesson, const QString &hash)
{
    for (const Declared &d : declared(project, lesson)) {
        if (d.hash != hash || d.line == 0)
            continue;
        QStringList lines = files::read(project.path(lesson)).split('\n');
        int first = d.line - 1, last = first;
        while (last < lines.size() && !lines[last].contains("\\end{" + d.kind + "}"))
            ++last;
        if (last >= lines.size() || !lines[first].trimmed().startsWith("\\begin{"))
            return QStringLiteral("Non riesco a togliere questo pezzo: toglilo a mano nella lezione.");
        while (first > 0 && lines[first - 1].contains("% @L"))                         // il suo marcatore se ne va con lui
            --first;
        const QString removed = lines.mid(first, last - first + 1).join('\n');
        const QString before = first > 0 ? lines[first - 1] : QString();
        lines.remove(first, last - first + 1);
        if (!files::write(project.path(lesson), lines.join('\n')))
            return QStringLiteral("Non riesco a scrivere la lezione.");
        QJsonObject saved = readJson(project.path(DECLARED));
        QJsonArray dropped = saved.value("tolte").toArray();
        dropped.append(QJsonObject{{"lezione", lesson}, {"tipo", d.kind}, {"impronta", hash}, {"testo", d.text}, {"righe", removed},
                                   {"dopo_riga", first}, {"dopo", before}});
        saved["tolte"] = dropped;
        writeJson(project.path(DECLARED), saved);
        keep(project, lesson, hash, false);
        return {};
    }
    return QStringLiteral("Questo pezzo non c'è più nella lezione.");
}

QString restore(const Project &project, const QString &lesson, const QString &hash)
{
    QJsonObject saved = readJson(project.path(DECLARED));
    QJsonArray dropped = saved.value("tolte").toArray();
    for (qsizetype i = 0; i < dropped.size(); ++i) {
        const QJsonObject o = dropped[i].toObject();
        if (o.value("lezione").toString() != lesson || o.value("impronta").toString() != hash)
            continue;
        QStringList lines = files::read(project.path(lesson)).split('\n');
        // dov'era: dopo la riga che lo precedeva, cercata vicino al posto di prima; se non c'è più, in fondo
        int at = qMin(o.value("dopo_riga").toInt(), int(lines.size()));
        const QString before = o.value("dopo").toString();
        if (at > 0 && lines.value(at - 1) != before) {
            const qsizetype found = lines.indexOf(before);
            at = before.trimmed().isEmpty() || found < 0 ? at : int(found) + 1;
        }
        const QStringList back = o.value("righe").toString().split('\n');
        for (qsizetype k = 0; k < back.size(); ++k)
            lines.insert(at + k, back[k]);
        if (!files::write(project.path(lesson), lines.join('\n')))
            return QStringLiteral("Non riesco a scrivere la lezione.");
        dropped.removeAt(i);
        saved["tolte"] = dropped;
        writeJson(project.path(DECLARED), saved);
        return {};
    }
    return QStringLiteral("Questo pezzo non è fra quelli tolti.");
}

Counts counts(const Project &project, const QString &lesson)
{
    Counts c;
    for (const Doubt &d : doubts(project, lesson))
        c.doubts += d.open;
    for (const Declared &d : declared(project, lesson))
        c.declared += d.state == "aperta";
    return c;
}

}
