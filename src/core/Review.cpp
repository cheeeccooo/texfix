#include "core/Review.h"

#include "core/Convert.h"
#include "core/Files.h"
#include "core/Model.h"
#include "core/Pages.h"

#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QHash>
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
        d.open = o.value("stato").toString("aperto") != "deciso";
        d.pending = o.value("stato").toString() == "da_confermare";
        d.why = o.value("perche").toString();
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
        doubt.remove("perche");
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

namespace tx::review {

namespace {
QString snapshotPath(const Project &project, const QString &id) { return project.path("salvataggi/dubbi/prima/" + id + ".tex"); }
}

QString startCorrection(const Project &project, const QString &lesson, const QString &id)
{
    return edit(project, lesson, id, [&](QJsonObject &doubt, QString &text) -> QString {
        if (!files::write(snapshotPath(project, id), text))
            return QStringLiteral("Non riesco a ricordare com'era la lezione.");
        doubt["stato"] = "da_confermare";
        doubt["come"] = "corretta";
        doubt.remove("scelta");
        return {};
    });
}

Change change(const Project &project, const QString &lesson, const QString &id)
{
    Change out;
    bool ok = false;
    const QStringList before = files::read(snapshotPath(project, id), &ok).split('\n');
    if (!ok)
        return out;
    const QStringList after = files::read(project.path(lesson)).split('\n');
    // le righe che c'erano e non ci sono più, e quelle nuove, nell'ordine in cui stanno
    QHash<QString, int> balance;
    for (const QString &line : after)
        ++balance[line];
    for (const QString &line : before)
        if (--balance[line] < 0 && !line.trimmed().isEmpty())
            out.before << line.trimmed();
    balance.clear();
    for (const QString &line : before)
        ++balance[line];
    for (const QString &line : after)
        if (--balance[line] < 0 && !line.trimmed().isEmpty())
            out.after << line.trimmed();
    return out;
}

QString confirm(const Project &project, const QString &lesson, const QString &id)
{
    const QString error = edit(project, lesson, id, [&](QJsonObject &doubt, QString &) -> QString {
        if (doubt.value("stato").toString() != "da_confermare")
            return QStringLiteral("Questo dubbio non aspetta una conferma.");
        doubt["stato"] = "deciso";
        return {};
    });
    if (error.isEmpty())
        QFile::remove(snapshotPath(project, id));
    return error;
}

QString decide(const Project &project, const Settings &settings, Model &model, const QString &lesson, const QString &id)
{
    Doubt doubt;
    for (const Doubt &d : doubts(project, lesson))
        if (d.id == id)
            doubt = d;
    if (doubt.id.isEmpty())
        return QStringLiteral("Questo dubbio non c'è più.");
    // la fonte: la pagina in immagine, o il testo
    QStringList parts;
    QString source;
    for (QDirIterator it(project.path("fonti"), {doubt.source}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();)
        source = it.next();
    if (!source.isEmpty()) {
        try {
            const Pages pages = Pages::read(source, project.path("build/lavori/dubbi/" + doubt.source));
            if (pages.isText)
                parts << "SOURCE TEXT:\n" + pages.text.left(20000);
            else if (!pages.full.value(doubt.page - 1).isEmpty())
                parts << QStringLiteral("The page of the source:") << pages.full.value(doubt.page - 1);
        } catch (const std::exception &) {
        }
    }
    const QStringList lines = files::read(project.path(lesson)).split('\n');
    QStringList readings;
    for (int i = 0; i < doubt.readings.size(); ++i)
        readings << QStringLiteral("%1. %2 → `%3`").arg(i).arg(doubt.readings[i].text, doubt.readings[i].latex);
    parts << QStringLiteral("The doubt: %1\nAs written now: `%2`\nCandidate readings:\n%3\n\nThe LaTeX around it:\n```latex\n%4\n```")
                 .arg(doubt.what, doubt.latex, readings.join('\n'), lines.mid(qMax(0, doubt.line - 8), 16).join('\n'));
    QString system = QStringLiteral(
        "You settle one doubt left while turning lecture notes (Italian) into LaTeX: an uncertain reading. Look at the source and at the "
        "mathematics around the passage, and decide what is written. Return only a JSON object: {\"latex\": \"<the excerpt as it should be "
        "written: one of the candidate readings, another one if none fits, or exactly the current one if it is already right>\", "
        "\"why\": \"<Italian, one short sentence: what you saw that decides it>\"}. Change nothing but the uncertain symbols.");
    if (!settings.rules.isEmpty())
        system += "\nRules of this project, which win over everything else:\n- " + settings.rules.join("\n- ");
    const QJsonObject answer = Model::json(model.ask("decide", system, parts, id)).toObject();
    const QString wanted = answer.value("latex").toString(), why = answer.value("why").toString().trimmed();
    if (wanted.isEmpty())
        return QStringLiteral("Il modello non ha saputo decidere.");
    return edit(project, lesson, id, [&](QJsonObject &o, QString &text) -> QString {
        const QString now = o.value("ora").toString(o.value("latex").toString());
        if (wanted != now) {
            if (!lineOnce(text, now))
                return QStringLiteral("Non trovo il passo nel testo una volta sola: va corretto a mano.");
            text.replace(now, wanted);
            o["ora"] = wanted;
        }
        o["stato"] = "da_confermare";              // la lettura è scritta, ma il dubbio aspetta la conferma
        o["come"] = "modello";
        o["perche"] = wanted == now ? QStringLiteral("Nessuna modifica. %1").arg(why) : why;
        o.remove("scelta");
        return {};
    });
}

}
