#include "core/Concepts.h"

#include "core/Files.h"
#include "core/NewLesson.h"
#include "core/Settings.h"
#include "core/TexSource.h"

#include <QCryptographicHash>
#include <QLocale>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

#include <atomic>
#include <future>

namespace tx::concepts {

namespace {
const char *const READ_PROMPT =
    R"(You read study material of a university course (usually in Italian): one lecture, one document or some pages. List its CONCEPTS: the units of content a student studies one at a time, such as a definition, a theorem with its proof, a derivation, a circuit with its analysis, a worked example, an exercise. Start from the sections of the material; split a section when it treats two different things, keep together what is one thing.
Only the content counts: ignore layout and style (spacing, colours, boxes, environments, macros, preamble, comments) and administrative notes.
Return only a JSON object:
{"concepts": [{"title": "<Italian, at most 8 words>", "what": "<Italian, one sentence: what it says or does, with the precise things (formulas, circuits, results, the example used)>", "where": "<Italian: the section it belongs to>", "from": <first line, or page>, "to": <last line, or page>}]}
Concepts in reading order; every part of the material belongs to one of them.)";

const char *const MAP_PROMPT =
    R"(You compare the concepts of a university course with the concepts of new material, to decide where the new material goes. A concept is a unit of content (a definition, a derivation, a circuit with its analysis, an example, an exercise), with an id. The course is made of UNITS (lectures, or topics) whose concepts have ids like L3.2; the new material is made of SOURCES whose concepts have ids like A.4.
1. "shared": every concept that appears in more than one place, in the course and in the new material, or in two new sources. Two concepts are shared when they explain the same thing (the same definition, derivation, circuit, example), even with other words or another depth; not when one only uses or continues the other. For each: "name" (Italian, at most 8 words), "items" (the ids, at least two), "note" (Italian, at most 25 words: what one adds to the other, what differs), "proposal": "one" when a single version is enough (then "keep": the id of the version whose place should host it, preferably the course's), "both" when they are really different treatments worth keeping, "skip" when the new versions add nothing.
2. "place": where every new concept goes as its own content: into an existing unit ("into": its key, like "L3": it completes it) or into a new unit ("into": "new", with "title", Italian, at most 40 characters; in a course by lectures, "date" GG/MM/AAAA if the sources give it, else empty; in a course by topics, "after": the key of the unit it should follow, "first" to put it before all, empty for the end). One entry per destination; concepts that belong together go in the same new unit even when they come from different sources; concepts in reading order. "why": Italian, at most 15 words.
3. "skip": new concepts with nothing to keep (title pages, indexes, administrative notes), with "why".
Every new concept is in exactly one entry of "place" or "skip"; a shared new concept too (its place is where it would go if kept).
The ids are only for you: never write them in "note" or "why" (name the material instead, like "la lezione 5" or "gli appunti del 20 ottobre").
Return only a JSON object: {"shared": [...], "place": [{"concepts": [ids], "into": "L3" | "new", "title": "...", "date": "...", "why": "..."}], "skip": [{"concepts": [ids], "why": "..."}]})";

QString hashOf(const QByteArray &bytes) { return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex().left(16)); }

// La risposta del modello, con gli id dati da Texfix in ordine.
Holder parse(Holder h, const QJsonObject &answer)
{
    int n = 0;
    for (const QJsonValue &v : answer.value("concepts").toArray()) {
        const QJsonObject c = v.toObject();
        if (c.value("title").toString().trimmed().isEmpty())
            continue;
        h.concepts << Concept{QStringLiteral("%1.%2").arg(h.key).arg(++n), c.value("title").toString().trimmed(), c.value("what").toString().trimmed(),
                              c.value("where").toString().trimmed(), c.value("from").toInt(), c.value("to").toInt()};
    }
    if (h.concepts.isEmpty())
        h.failure = QStringLiteral("il modello non ha trovato concetti");
    return h;
}

// Chiede e tiene: la stessa domanda sullo stesso materiale non si ripaga.
QJsonObject askKept(const Project &project, Model &model, const QString &cacheKey, const QStringList &parts, const QString &tag)
{
    const QString cache = project.path("build/lavori/concetti/" + cacheKey + ".json");
    const QJsonObject kept = QJsonDocument::fromJson(files::read(cache).toUtf8()).object();
    if (!kept.value("concepts").toArray().isEmpty())
        return kept;
    QJsonObject answer;
    for (int attempt = 0; attempt < 2 && answer.value("concepts").toArray().isEmpty(); ++attempt) {
        const QString reply = model.ask("concepts", QString::fromUtf8(READ_PROMPT), parts, tag);
        try {
            answer = Model::json(reply).toObject();
        } catch (const std::exception &) {
            answer = {};
        }
    }
    if (!answer.value("concepts").toArray().isEmpty())
        files::write(cache, QString::fromUtf8(QJsonDocument(answer).toJson()));
    return answer;
}
}

QString textOf(const Project &project, const QString &source)
{
    const texsource::Ref ref = texsource::parseSpec(source);
    const QString path = project.path(ref.path), kind = Pages::kindOf(ref.path);
    if (kind == "tex") {
        const QString document = files::read(path);
        return ref.date.isValid() ? texsource::workerText(document, ref.date, ref.nth) : texsource::body(document);
    }
    if (kind == "testo")
        return Pages::read(path, project.path("build/lavori/concetti/testi")).text;
    return {};
}

QString extract(const QString &text, const QList<std::pair<int, int>> &ranges)
{
    const QStringList lines = text.split('\n');
    const int n = int(lines.size());
    QList<bool> take(n, false);
    for (auto [from, to] : ranges) {
        if (from <= 0 || to <= 0) {                    // dove sta non si sa: tutto
            take.fill(true);
            break;
        }
        int a = qBound(1, from, n) - 1, b = qBound(a + 1, to, n) - 1;
        while (a < b && lines[a].trimmed().isEmpty())  // le righe vuote ai bordi non sono del concetto
            ++a;
        while (b > a && lines[b].trimmed().isEmpty())
            --b;
        if (lines[a].trimmed().isEmpty())              // solo righe vuote: niente
            continue;
        while (a > 0 && !lines[a - 1].trimmed().isEmpty())
            --a;                                       // dall'inizio del paragrafo
        while (b + 1 < n && !lines[b].trimmed().isEmpty() && !lines[b + 1].trimmed().isEmpty())
            ++b;                                       // fino alla sua fine
        for (int i = a; i <= b; ++i)
            take[i] = true;
    }
    QStringList out;
    bool gap = false;
    for (int i = 0; i < n; ++i) {
        if (take[i]) {
            if (gap && !out.isEmpty())
                out << QString();
            out << lines[i];
            gap = false;
        } else {
            gap = true;
        }
    }
    return out.join('\n');
}

Holder fromText(const Project &project, Model &model, const QString &key, const QString &name, const QString &source, const QString &text, bool course)
{
    QStringList numbered;
    const QStringList lines = text.split('\n');
    for (qsizetype i = 0; i < lines.size(); ++i)
        numbered << QStringLiteral("%1| %2").arg(i + 1, 4).arg(lines[i]);
    const QString body = QStringLiteral("MATERIAL «%1» (%2 lines, numbered)\n%3").arg(name).arg(lines.size()).arg(numbered.join('\n'));
    const QJsonObject answer = askKept(project, model, hashOf(QByteArray(READ_PROMPT) + body.toUtf8()), {body}, key);
    return parse(Holder{key, name, source, course, {}, {}}, answer);
}

Holder fromPages(const Project &project, Model &model, const QString &key, const QString &name, const QString &source, const Pages &pages)
{
    QStringList parts;
    QByteArray seen = READ_PROMPT;
    for (int p = 0; p < pages.count(); ++p) {
        parts << QStringLiteral("P%1").arg(p + 1) << pages.full.value(p);
        QFile f(pages.full.value(p));
        if (f.open(QIODevice::ReadOnly))
            seen += f.readAll();
    }
    parts << QStringLiteral("MATERIAL «%1»: %2 pages; \"from\" and \"to\" are pages.").arg(name).arg(pages.count());
    const QJsonObject answer = askKept(project, model, hashOf(seen), parts, key);
    return parse(Holder{key, name, source, false, {}, {}}, answer);
}

QJsonObject map(Model &model, const QList<Holder> &course, const QList<Holder> &fresh, bool byTopic, QString *failure, const QString &previous)
{
    QStringList request{QStringLiteral("THE COURSE, BY %1").arg(byTopic ? "TOPICS" : "LECTURES")};
    for (const Holder &h : course) {
        request << QStringLiteral("UNIT %1: %2").arg(h.key, h.name);
        for (const Concept &c : h.concepts)
            request << QStringLiteral("- %1 %2: %3").arg(c.id, c.title, c.what);
    }
    if (course.isEmpty())
        request << "(no units yet)";
    request << QString() << "THE NEW MATERIAL";
    QSet<QString> fresh_ids, all_ids;
    for (const Holder &h : course)
        for (const Concept &c : h.concepts)
            all_ids.insert(c.id);
    for (const Holder &h : fresh) {
        request << QStringLiteral("SOURCE %1: %2").arg(h.key, h.name);
        for (const Concept &c : h.concepts) {
            request << QStringLiteral("- %1 %2: %3").arg(c.id, c.title, c.what);
            fresh_ids.insert(c.id);
            all_ids.insert(c.id);
        }
    }
    // la risposta torna se ogni concetto nuovo ha un posto solo, e gli id esistono; altrimenti si chiede di nuovo, dicendo che cosa
    auto check = [&](const QJsonObject &answer) -> QString {
        QHash<QString, int> placed;
        for (const char *part : {"place", "skip"})
            for (const QJsonValue &e : answer.value(part).toArray())
                for (const QJsonValue &id : e.toObject().value("concepts").toArray())
                    ++placed[id.toString()];
        QStringList wrong;
        for (const QString &id : std::as_const(fresh_ids))
            if (placed.value(id) != 1)
                wrong << id;
        for (const QJsonValue &s : answer.value("shared").toArray())
            for (const QJsonValue &id : s.toObject().value("items").toArray())
                if (!all_ids.contains(id.toString()))
                    wrong << id.toString();
        if (answer.value("place").toArray().isEmpty() && !fresh_ids.isEmpty())
            return QStringLiteral("no place");
        return wrong.isEmpty() ? QString() : QStringLiteral("these ids are missing, repeated or unknown: %1").arg(wrong.join(", "));
    };
    QJsonObject answer;
    QString problem;
    for (int attempt = 0; attempt < 2; ++attempt) {
        QStringList parts{request.join('\n')};
        if (!previous.isEmpty())
            parts << "YOUR PREVIOUS MAP, WITH THE USER'S DECISIONS AND INSTRUCTIONS (they win over your own reading; what they do not mention stays as before)\n" + previous;
        if (!problem.isEmpty())
            parts << QStringLiteral("Your previous answer did not hold: %1. Answer again, with every new concept in exactly one entry of place or skip.").arg(problem);
        const QString reply = model.ask("map", QString::fromUtf8(MAP_PROMPT), parts, "mappa");
        try {
            answer = Model::json(reply).toObject();
        } catch (const std::exception &) {
            answer = {};
        }
        problem = answer.isEmpty() ? QStringLiteral("not a JSON object") : check(answer);
        if (problem.isEmpty())
            return answer;
    }
    if (failure)
        *failure = QStringLiteral("la mappa dei concetti non torna (%1)").arg(problem);
    return answer;
}

QJsonObject Survey::toJson() const
{
    auto holders = [](const QList<Holder> &list) {
        QJsonArray out;
        for (const Holder &h : list) {
            QJsonArray concepts;
            for (const Concept &c : h.concepts)
                concepts.append(QJsonObject{{"id", c.id}, {"title", c.title}, {"what", c.what}, {"where", c.where}, {"from", c.from}, {"to", c.to}});
            out.append(QJsonObject{{"key", h.key}, {"name", h.name}, {"source", h.source}, {"concepts", concepts}, {"failure", h.failure}});
        }
        return out;
    };
    return QJsonObject{{"course", holders(course)}, {"fresh", holders(fresh)}, {"known", QJsonArray::fromStringList(known)},
                       {"map", map}, {"failure", failure}, {"cost", cost}};
}

Survey survey(const Project &project, const QString &quality, const QStringList &files, JobControl *control, const QString &previous)
{
    Survey out;
    Model model(quality, project.root(), control);
    const bool byTopic = Settings::load(project).byTopic();
    const QLocale italian(QLocale::Italian);
    // che cosa leggere: le lezioni del corso, e di ogni fonte ciò che il corso non contiene già tale e quale
    struct Job {
        QString key, name, source, text;
        Pages pages;
        bool course = false, isPages = false;
    };
    QList<Job> jobs;
    QHash<QString, QString> lessonName;
    const QList<Lesson> lessons = project.lessons();
    for (qsizetype i = 0; i < lessons.size(); ++i) {
        const QString key = QStringLiteral("L%1").arg(byTopic ? int(i + 1) : lessons[i].number);
        const QString name = byTopic ? QStringLiteral("Argomento %1 · %2").arg(i + 1).arg(lessons[i].title)
                                     : QStringLiteral("Lezione %1 · %2").arg(lessons[i].number).arg(lessons[i].title);
        lessonName.insert(lessons[i].file, name);
        jobs << Job{key, name, lessons[i].file, files::read(project.path(lessons[i].file)), {}, true, false};
    }
    int letter = 0;
    auto nextKey = [&] {
        QString key;
        for (int n = letter++; ; n = n / 26 - 1) {
            key.prepend(QChar('A' + n % 26));
            if (n < 26)
                break;
        }
        return key;
    };
    for (const QString &file : files) {
        const QString path = project.path(file), base = QFileInfo(file).completeBaseName(), kind = Pages::kindOf(file);
        try {
            if (kind == "tex") {
                const QString document = files::read(path);
                const QList<newlesson::TexLecture> lectures = newlesson::texLectures(project, file);
                if (lectures.isEmpty()) {
                    jobs << Job{nextKey(), base, file, textOf(project, file), {}, false, false};
                    continue;
                }
                for (const newlesson::TexLecture &t : lectures) {
                    const QString name = QStringLiteral("%1 · %2%3").arg(base, italian.toString(t.lesson.date, "d MMMM"),
                                                                         t.lesson.nth > 1 ? QStringLiteral(" (la %1ª)").arg(t.lesson.nth) : QString());
                    if (!t.in.isEmpty())
                        out.known << QStringLiteral("%1: già nel corso, tale e quale (%2)").arg(name, lessonName.value(t.in));
                    else
                        jobs << Job{nextKey(), name, t.spec, textOf(project, t.spec), {}, false, false};
                }
            } else if (kind == "testo") {
                jobs << Job{nextKey(), base, file, textOf(project, file), {}, false, false};
            } else if (kind == "pagine") {
                QFile f(path);
                if (!f.open(QIODevice::ReadOnly))
                    throw std::runtime_error("non riesco ad aprire il file");
                Job job{nextKey(), base, file, {}, {}, false, true};
                job.pages = Pages::read(path, project.path("build/lavori/converti/" + hashOf(f.readAll()).left(12) + "/pagine"));
                jobs << job;
            }
        } catch (const std::exception &e) {
            out.failure += QStringLiteral("%1: %2. ").arg(QFileInfo(file).fileName(), QString::fromUtf8(e.what()));
        }
    }
    // tutti insieme (Model ne fa girare al più PARALLEL)
    QList<Holder> read(jobs.size());
    std::vector<std::future<void>> running;
    std::atomic<int> done{0};
    for (qsizetype i = 0; i < jobs.size(); ++i)
        running.push_back(std::async(std::launch::async, [&, i] {
            const Job &j = jobs[i];
            try {
                read[i] = j.isPages ? fromPages(project, model, j.key, j.name, j.source, j.pages)
                                    : fromText(project, model, j.key, j.name, j.source, j.text, j.course);
            } catch (const Stopped &) {
                read[i] = Holder{j.key, j.name, j.source, j.course, {}, QStringLiteral("fermato")};
            } catch (const std::exception &e) {
                read[i] = Holder{j.key, j.name, j.source, j.course, {}, QString::fromUtf8(e.what())};
            }
            if (control)
                control->progress(0.85 * ++done / jobs.size(), QStringLiteral("leggo i concetti: %1").arg(j.name));
        }));
    for (auto &r : running)
        r.get();
    if (control)
        control->check();
    for (const Holder &h : std::as_const(read)) {
        if (!h.failure.isEmpty())
            out.failure += QStringLiteral("%1: %2. ").arg(h.name, h.failure);
        (h.course ? out.course : out.fresh) << h;
    }
    if (!out.fresh.isEmpty() && out.failure.isEmpty()) {
        if (control)
            control->progress(0.9, QStringLiteral("cerco le intersezioni"));
        QString failure;
        out.map = map(model, out.course, out.fresh, byTopic, &failure, previous);
        out.failure = failure;
    }
    out.cost = model.cost();
    files::write(project.path("build/lavori/concetti/ultima-mappa.json"), QString::fromUtf8(QJsonDocument(out.toJson()).toJson()));
    return out;
}

}
