#include "core/Sort.h"

#include "core/Files.h"
#include "core/Model.h"
#include "core/NewLesson.h"
#include "core/Pages.h"
#include "core/Plans.h"
#include "core/TexSource.h"
#include "core/Tools.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

namespace tx::sort {

namespace {
const char *const PAGES_PROMPT =
    R"(You read the pages of a file of university lecture material (handwritten notes, slides or a handout, usually in Italian). You get a thumbnail of every page, labelled P1..Pn.
For every page say the lecture date written on it, if any, and its topic. Return only a JSON object:
{"pages": [{"page": k, "date": "<GG/MM/AAAA if a lecture date is written on this page, else empty>", "topic": "<the topic in Italian, at most 8 words>"}]}
Every page appears exactly once. A date counts only when it marks a lecture (a heading such as "Lezione del 20/10", a date at the top of a page), not dates inside exercises or examples.)";

const char *const SORT_PROMPT =
    R"(You sort new material of a university course into lectures. You get the lectures the course already has (number, date, title, sections) and the new material: for each file, its pages with date and topic (or, for a text, its beginning; for LaTeX notes by another author, its lectures, each one a whole lecture labelled P like a page).
Decide where every piece goes:
- "new": a lecture the course does not have yet. One group per lecture: pages with the same lecture date, or that continue the same topic, stay together; a file that covers several lectures is split.
- "lesson": material that belongs to a lecture the course already has (same date, or the same content of that lecture): it will complete that lecture.
- "skip": only pages with nothing to keep (blank, covers, indexes, duplicated pages).
Return only a JSON object:
{"groups": [{"dest": "new", "date": "<GG/MM/AAAA or empty>", "title": "<Italian topic, at most 40 characters>", "pieces": [{"file": "<name>", "from": <first page>, "to": <last page>}], "why": "<Italian, at most 15 words>"},
            {"dest": "lesson", "lesson": <number of the existing lecture>, "pieces": [...], "why": "..."}],
 "skip": [{"file": "<name>", "from": <k>, "to": <k>, "why": "..."}]}
Rules: every page of every file is in exactly one piece of one group or in skip; pieces are ranges of consecutive pages of one file; a text file is one piece with "from": 1, "to": 1. A lecture of LaTeX notes is never split, and two of them go in the same group only if they are the same lecture (two lectures on the same day are usually two lectures). Groups of new lectures in calendar order.
If the request ends with YOUR PREVIOUS PLAN, WITH THE USER'S INSTRUCTIONS: make the plan again following them. The user's instructions win over your own reading (they may move pieces to another lecture, merge or split groups, change dates or titles, skip pages); what they do not mention stays as before.)";

QString pageRange(int from, int to) { return from == to ? QStringLiteral("pagina %1").arg(from) : QStringLiteral("pagine %1–%2").arg(from).arg(to); }

// Le lezioni di un .tex come le dice il piano: «lezione del 4/11/2026, seconda lezione del 10/11/2026».
QString lectureLabels(const QJsonArray &specs)
{
    QStringList labels;
    for (const QJsonValue &spec : specs)
        labels << texsource::label(texsource::parseSpec(spec.toString()));
    return labels.join(", ");
}

// Una lezione di un .tex come la legge l'agente: il file e la sua \section.
QString forAgent(const Project &project, const QString &source)
{
    const texsource::Ref ref = texsource::parseSpec(source);
    if (!ref.date.isValid())
        return source;
    for (const texsource::Lesson &lesson : texsource::lessons(files::read(project.path(ref.path))))
        if (lesson.date == ref.date && lesson.nth == ref.nth)
            return QStringLiteral("`%1` (solo la parte `\\section{%2}`%3)").arg(ref.path, lesson.heading, ref.nth > 1 ? QStringLiteral(", la %1 con questo titolo").arg(texsource::label(ref).section(' ', 0, 0)) : QString());
    return ref.path;
}

// Ogni pagina di ogni file in un pezzo solo, o fra quelle lasciate.
bool coversOnce(const QJsonObject &answer, const QHash<QString, int> &sizes)
{
    QHash<QString, QList<int>> seen;
    auto take = [&](const QJsonObject &piece) {
        for (int p = piece.value("from").toInt(); p <= piece.value("to").toInt(); ++p)
            seen[piece.value("file").toString()] << p;
    };
    for (const QJsonValue &g : answer.value("groups").toArray())
        for (const QJsonValue &p : g.toObject().value("pieces").toArray())
            take(p.toObject());
    for (const QJsonValue &s : answer.value("skip").toArray())
        take(s.toObject());
    if (answer.value("groups").toArray().isEmpty() || seen.size() != sizes.size())
        return false;
    for (auto it = sizes.begin(); it != sizes.end(); ++it) {
        QList<int> pages = seen.value(it.key());
        std::sort(pages.begin(), pages.end());
        if (pages.size() != it.value())
            return false;
        for (int i = 0; i < pages.size(); ++i)
            if (pages[i] != i + 1)
                return false;
    }
    return true;
}
}

Outcome plan(const Project &project, const Settings &settings, const QStringList &files, JobControl *control, const QString &previous)
{
    Outcome out;
    Model model(settings.quality, project.root(), control);
    try {
        // il corso com'è: una riga per lezione
        QStringList course;
        static const QRegularExpression sub(R"(\\subsection\*?\{([^}]*)\})");
        QHash<int, QString> titles;
        QStringList have;                              // le date del corso: le lezioni di un .tex che ha già non si smistano
        for (const Lesson &lesson : project.lessons()) {
            have << lesson.date;
            QStringList sections;
            for (auto it = sub.globalMatch(files::read(project.path(lesson.file))); it.hasNext();)
                sections << it.next().captured(1);
            course << QStringLiteral("- lecture %1, date %2, \"%3\". Sections: %4").arg(lesson.number).arg(lesson.date.isEmpty() ? "?" : lesson.date, lesson.title, sections.join("; "));
            titles.insert(lesson.number, lesson.title);
        }
        // il materiale: di ogni file a pagine, data e argomento di ogni pagina (una chiamata, tenuta); di un testo, l'inizio
        QStringList material;
        QHash<QString, int> sizes;
        QHash<QString, QJsonArray> lectures;           // di un .tex: le sue lezioni, una per «pagina»
        QStringList already;                           // i .tex di cui il corso ha già tutte le lezioni
        for (int i = 0; i < files.size(); ++i) {
            if (control)
                control->progress(0.8 * i / files.size(), QStringLiteral("leggo %1").arg(QFileInfo(files[i]).fileName()));
            const QString name = QFileInfo(files[i]).fileName(), path = project.path(files[i]);
            const QString kind = Pages::kindOf(name);
            if (kind == "tex") {                       // le lezioni del file, senza modello: data, titolo, sezioni, l'inizio
                const QString document = files::read(path);
                const QList<texsource::Lesson> all = texsource::lessons(document), missing = texsource::missing(all, have);
                if (all.isEmpty()) {                   // non diviso in lezioni: un pezzo, come un testo
                    sizes.insert(name, 1);
                    material << QStringLiteral("FILE %1 (LaTeX notes, one piece)\n%2").arg(name, texsource::body(document).left(1200));
                } else if (missing.isEmpty()) {
                    already << name;
                } else {
                    static const QRegularExpression heading(R"(\\(?:sub)*section\*?\{([^}]*)\})");
                    QStringList lines{QStringLiteral("FILE %1 (LaTeX notes by another author: %2 lectures the course does not have yet, each P is a whole lecture)")
                                          .arg(name).arg(missing.size())};
                    QJsonArray specs;
                    for (qsizetype k = 0; k < missing.size(); ++k) {
                        const texsource::Lesson &lesson = missing[k];
                        QStringList sections;
                        for (auto it = heading.globalMatch(lesson.text); it.hasNext();)
                            sections << it.next().captured(1);
                        const qsizetype first = lesson.text.indexOf("\\sub");
                        const QString start = lesson.text.left(first < 0 ? 400 : qMin<qsizetype>(400, first)).simplified();
                        lines << QStringLiteral("- P%1: date %2, \"%3\". Sections: %4. Beginning: %5")
                                     .arg(k + 1).arg(lesson.date.toString("dd/MM/yyyy"), lesson.heading, sections.join("; "), start);
                        specs << texsource::spec(files[i], lesson.date, lesson.nth);
                    }
                    sizes.insert(name, int(missing.size()));
                    lectures.insert(name, specs);
                    material << lines.join('\n');
                }
                continue;
            }
            if (kind == "testo") {
                sizes.insert(name, 1);
                material << QStringLiteral("FILE %1 (text, one piece)\n%2").arg(name, files::read(path).left(1200));
                continue;
            }
            if (kind != "pagine")
                continue;
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                continue;
            const QString key = QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha1).toHex().left(12));
            const QString cache = project.path("build/lavori/smistamento/" + key + ".json");
            const Pages pages = Pages::read(path, project.path("build/lavori/converti/" + key + "/pagine"));
            QJsonObject described = QJsonDocument::fromJson(files::read(cache).toUtf8()).object();
            if (described.value("pages").toArray().size() != pages.count()) {
                QStringList parts;
                for (int p = 0; p < pages.count(); ++p)
                    parts << QStringLiteral("P%1").arg(p + 1) << pages.thumb[p];
                parts << QStringLiteral("The file has %1 pages.").arg(pages.count());
                described = Model::json(model.ask("outline", QString::fromUtf8(PAGES_PROMPT), parts, name)).toObject();
                if (described.value("pages").toArray().size() == pages.count())
                    files::write(cache, QString::fromUtf8(QJsonDocument(described).toJson()));
            }
            sizes.insert(name, pages.count());
            QStringList lines{QStringLiteral("FILE %1 (%2 pages)").arg(name).arg(pages.count())};
            for (const QJsonValue &p : described.value("pages").toArray())
                lines << QStringLiteral("- P%1: date %2, %3").arg(p.toObject().value("page").toInt()).arg(p.toObject().value("date").toString("").isEmpty() ? "-" : p.toObject().value("date").toString(),
                                                                 p.toObject().value("topic").toString());
            material << lines.join('\n');
        }
        if (sizes.isEmpty())
            return {{}, already.isEmpty() ? QStringLiteral("Fra questi file non c'è materiale che so leggere.")
                                          : QStringLiteral("Il corso ha già tutte le lezioni di %1: ciò che ancora manca lo porta l'agente con «Fai il piano» in Converti.")
                                                .arg(already.join(", ")),
                    model.cost()};

        if (control)
            control->progress(0.85, QStringLiteral("divido il materiale fra le lezioni"));
        QString request = QStringLiteral("COURSE LECTURES\n%1\n\nNEW MATERIAL\n%2").arg(course.isEmpty() ? QStringLiteral("(none yet)") : course.join('\n'), material.join("\n\n"));
        if (!previous.isEmpty())
            request += "\n\nYOUR PREVIOUS PLAN, WITH THE USER'S INSTRUCTIONS\n" + previous;
        QJsonObject answer;
        for (int attempt = 0; attempt < 2 && answer.isEmpty(); ++attempt) {
            try {
                answer = Model::json(model.ask("outline", QString::fromUtf8(SORT_PROMPT), {request}, "smistamento")).toObject();
            } catch (const Stopped &) {
                throw;
            } catch (const LimitReached &) {
                throw;
            } catch (const std::exception &) {
            }
            if (!coversOnce(answer, sizes))
                answer = {};
        }
        bool fallback = answer.isEmpty();
        if (fallback) {                                // ripiego: ogni file una lezione nuova, ogni lezione di un .tex la sua
            QJsonArray groups;
            for (auto it = sizes.begin(); it != sizes.end(); ++it) {
                if (lectures.contains(it.key())) {
                    for (int k = 1; k <= it.value(); ++k)
                        groups.append(QJsonObject{{"dest", "new"}, {"title", ""}, {"why", "una lezione del file"},
                                                  {"date", texsource::parseSpec(lectures[it.key()][k - 1].toString()).date.toString("dd/MM/yyyy")},
                                                  {"pieces", QJsonArray{QJsonObject{{"file", it.key()}, {"from", k}, {"to", k}}}}});
                    continue;
                }
                groups.append(QJsonObject{{"dest", "new"}, {"title", ""}, {"date", ""}, {"why", "un file, una lezione"},
                                          {"pieces", QJsonArray{QJsonObject{{"file", it.key()}, {"from", 1}, {"to", it.value()}}}}});
            }
            answer = QJsonObject{{"groups", groups}};
        }

        // il piano da leggere: una sezione per lezione, una voce per pezzo
        QJsonArray sections;
        for (const QJsonValue &g : answer.value("groups").toArray()) {
            const QJsonObject group = g.toObject();
            const bool fresh = group.value("dest").toString() != "lesson";
            const int number = group.value("lesson").toInt();
            QString title = fresh ? QStringLiteral("Lezione nuova · %1").arg(group.value("title").toString().isEmpty() ? QStringLiteral("senza titolo") : group.value("title").toString())
                                  : QStringLiteral("Lezione %1 · %2: da completare").arg(number).arg(titles.value(number));
            if (fresh && !group.value("date").toString().isEmpty())
                title += QStringLiteral(" · lezione del %1").arg(group.value("date").toString());
            if (!group.value("why").toString().isEmpty())
                title += QStringLiteral(" — %1").arg(group.value("why").toString());
            QJsonArray entries;
            for (const QJsonValue &p : group.value("pieces").toArray()) {
                QJsonObject piece = p.toObject();
                const QString name = piece.value("file").toString();
                const int from = piece.value("from").toInt(), to = piece.value("to").toInt();
                QString text = sizes.value(name) <= 1 ? name : QStringLiteral("%1 · %2").arg(name, pageRange(from, to));
                if (lectures.contains(name)) {         // le lezioni di un .tex, per data
                    QJsonArray specs;
                    for (int k = from; k <= to && k - 1 < lectures[name].size(); ++k)
                        specs << lectures[name][k - 1];
                    piece["lezioni"] = specs;
                    text = QStringLiteral("%1 · %2").arg(name, lectureLabels(specs));
                }
                entries.append(QJsonObject{{"testo", text}, {"pezzo", piece}});
            }
            QJsonObject section{{"titolo", title}, {"voci", entries}, {"gruppo", QJsonObject{{"dest", fresh ? "new" : "lesson"}, {"lesson", number},
                                                                                           {"date", group.value("date")}, {"title", group.value("title")}}}};
            sections.append(section);
        }
        QJsonObject delivered{{"titolo", "Aggiungi materiale"},
                              {"riepilogo", fallback ? QStringLiteral("Non sono riuscito a dividere il materiale: propongo un file per lezione. Correggi con le istruzioni sotto le voci.")
                                                     : QStringLiteral("Il materiale nuovo, diviso fra lezioni nuove e lezioni da completare. Togli la spunta a ciò che non vuoi; "
                                                                      "scrivi un'istruzione sotto una voce per spostarla o cambiarla.")},
                              {"sezioni", sections}};
        out.plan = plans::create(project, delivered, &out.failure);
        if (!out.plan.isEmpty()) {                     // ciò che serve ad «Avvia»: il tipo, i file, i pezzi e i gruppi
            QJsonObject document = plans::load(project, out.plan);
            QJsonArray saved = document.value("sezioni").toArray();
            for (qsizetype s = 0; s < saved.size() && s < sections.size(); ++s) {
                QJsonObject section = saved[s].toObject();
                section["gruppo"] = sections[s].toObject().value("gruppo");
                QJsonArray entries = section.value("voci").toArray();
                for (qsizetype v = 0; v < entries.size(); ++v) {
                    QJsonObject entry = entries[v].toObject();
                    entry["pezzo"] = sections[s].toObject().value("voci").toArray()[v].toObject().value("pezzo");
                    entries[v] = entry;
                }
                section["voci"] = entries;
                saved[s] = section;
            }
            document["sezioni"] = saved;
            document["tipo"] = "materiale";
            document["file"] = QJsonArray::fromStringList(files);
            plans::save(project, out.plan, document);
        }
    } catch (const Stopped &) {
        out.failure = QStringLiteral("Lavoro fermato.");
    } catch (const std::exception &e) {
        out.failure = QString::fromUtf8(e.what());
    }
    out.cost = model.cost();
    return out;
}

QString previousPlan(const QJsonObject &document)
{
    QStringList lines;
    for (const QJsonValue &s : document.value("sezioni").toArray()) {
        const QJsonObject group = s.toObject().value("gruppo").toObject();
        lines << (group.value("dest").toString() == "lesson" ? QStringLiteral("GROUP lesson %1").arg(group.value("lesson").toInt())
                                                             : QStringLiteral("GROUP new, date \"%1\", title \"%2\"").arg(group.value("date").toString(), group.value("title").toString()));
        for (const QJsonValue &v : s.toObject().value("voci").toArray()) {
            const QJsonObject entry = v.toObject(), piece = entry.value("pezzo").toObject();
            QString line = QStringLiteral("- %1 %2 %3-%4").arg(piece.value("file").toString(), piece.contains("lezioni") ? "lectures" : "pages")
                               .arg(piece.value("from").toInt()).arg(piece.value("to").toInt());
            if (!entry.value("fare").toBool())
                line += QStringLiteral(" — USER: skip this piece");
            if (!entry.value("nota").toString().trimmed().isEmpty())
                line += QStringLiteral(" — USER: %1").arg(entry.value("nota").toString().trimmed());
            lines << line;
        }
    }
    if (!document.value("commento").toString().trimmed().isEmpty())
        lines << QStringLiteral("USER, about the whole plan: %1").arg(document.value("commento").toString().trimmed());
    return lines.join('\n');
}

Prepared prepare(const Project &project, const QJsonObject &document)
{
    Prepared out;
    QHash<QString, QString> paths;                     // nome → fonte in coda
    for (const QJsonValue &f : document.value("file").toArray())
        paths.insert(QFileInfo(f.toString()).fileName(), f.toString());
    int number = newlesson::nextNumber(project);
    static const QRegularExpression numbered(R"(^src(\d+)$)");
    int lastFile = 0;
    for (const QString &name : QDir(project.path("src")).entryList({"*.tex"}, QDir::Files))
        lastFile = qMax(lastFile, numbered.match(QFileInfo(name).completeBaseName()).captured(1).toInt());
    QHash<int, QString> lessonFiles;
    for (const Lesson &lesson : project.lessons())
        lessonFiles.insert(lesson.number, lesson.file);

    // un pezzo di un PDF diventa un file suo fra le fonti da convertire; se qualcosa non va, i pezzi fatti si tolgono
    int pieceCount = 0;
    QStringList made;
    QSet<QString> cutUp;                               // i PDF divisi: alla fine vanno fra le fonti usate
    auto cut = [&](const QString &name, const QString &source, int from, int to) -> QString {
        const QString work = project.path(QStringLiteral("build/lavori/smistamento/pezzo%1").arg(++pieceCount));
        QDir(work).removeRecursively();
        QDir().mkpath(work);
        const QString target = files::freeName(project.path(QStringLiteral("fonti/coda/%1 p%2-%3.pdf").arg(QFileInfo(name).completeBaseName()).arg(from).arg(to)));
        if (!tools::run("pdfseparate", {"-f", QString::number(from), "-l", QString::number(to), project.path(source), work + "/p%04d.pdf"}, work, 120000).ok) {
            out.failure = QStringLiteral("Non riesco a dividere %1 (serve poppler).").arg(name);
            return {};
        }
        QStringList parts;
        for (const QString &p : QDir(work).entryList({"p*.pdf"}, QDir::Files, QDir::Name))
            parts << work + '/' + p;
        if (parts.size() == 1 ? !QFile::copy(parts.first(), target) : !tools::run("pdfunite", parts + QStringList{target}, work, 120000).ok) {
            out.failure = QStringLiteral("Non riesco a mettere insieme le pagine di %1.").arg(name);
            return {};
        }
        made << target;
        cutUp.insert(source);
        return project.relative(target);
    };
    auto fail = [&] {
        for (const QString &file : std::as_const(made))
            QFile::remove(file);
        return Prepared{{}, {}, {}, out.failure};
    };

    for (const QJsonValue &s : document.value("sezioni").toArray()) {
        const QJsonObject group = s.toObject().value("gruppo").toObject();
        QStringList sources, notes;
        for (const QJsonValue &v : s.toObject().value("voci").toArray()) {
            const QJsonObject entry = v.toObject(), piece = entry.value("pezzo").toObject();
            const QString name = piece.value("file").toString(), source = paths.value(name);
            if (source.isEmpty() || !QFileInfo::exists(project.path(source)))
                continue;
            const int from = piece.value("from").toInt(), to = piece.value("to").toInt();
            const bool part = !piece.contains("lezioni") && Pages::kindOf(name) == "pagine" && name.endsWith(".pdf", Qt::CaseInsensitive)
                              && (from > 1 || to < Pages::size(project.path(source)));
            if (!entry.value("fare").toBool()) {      // tolto dal piano: un pezzo di un PDF resta da convertire, da solo
                if (part && cut(name, source, from, to).isEmpty())
                    return fail();
                continue;
            }
            if (!entry.value("nota").toString().trimmed().isEmpty())
                notes << QStringLiteral("%1: %2").arg(name, entry.value("nota").toString().trimmed());
            if (piece.contains("lezioni")) {          // le lezioni di un .tex: si indicano, il file resta com'è
                for (const QJsonValue &spec : piece.value("lezioni").toArray())
                    sources << spec.toString();
                continue;
            }
            if (!part) {
                sources << source;                     // il file intero
                continue;
            }
            const QString target = cut(name, source, from, to);
            if (target.isEmpty())
                return fail();
            sources << target;
        }
        if (sources.isEmpty())
            continue;
        QStringList named;                             // per l'agente: una lezione di un .tex è il file e la sua \section
        for (const QString &source : std::as_const(sources))
            named << forAgent(project, source);
        if (group.value("dest").toString() == "lesson") {
            const QString lesson = lessonFiles.value(group.value("lesson").toInt());
            if (!lesson.isEmpty())
                out.agent << newlesson::integrateMessage(lesson, named) + (notes.isEmpty() ? QString() : "\nIstruzioni dell'utente: " + notes.join("; "));
            continue;
        }
        const int n = number++;
        QString date = group.value("date").toString();
        if (const texsource::Ref ref = texsource::parseSpec(sources.first()); date.isEmpty() && ref.date.isValid())
            date = ref.date.toString("dd/MM/yyyy");
        if (sources.size() == 1 && notes.isEmpty()) {  // una fonte sola (anche una lezione di un .tex): la converte Converti
            out.sources << sources.first();
            out.wanted.insert(sources.first(), Convert::Wanted{n, date});
        } else {                                       // più pezzi, o con istruzioni: la scrive l'agente, con un piano
            out.agent << newlesson::fuseMessage(QStringLiteral("src/src%1.tex").arg(++lastFile + int(out.sources.size())), n, date, named)
                             + (notes.isEmpty() ? QString() : "\nIstruzioni dell'utente: " + notes.join("; "));
        }
    }
    // un PDF diviso non resta fra quelle da convertire accanto ai suoi pezzi
    for (const QString &source : std::as_const(cutUp)) {
        QDir().mkpath(project.path("fonti/usate/divise"));
        QFile::rename(project.path(source), files::freeName(project.path("fonti/usate/divise/" + QFileInfo(source).fileName())));
    }
    return out;
}

}
