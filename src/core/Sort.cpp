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
#include <QSet>

namespace tx::sort {

namespace {
const char *const PAGES_PROMPT =
    R"(You read the pages of a file of university lecture material (handwritten notes, slides or a handout, usually in Italian). You get a thumbnail of every page, labelled P1..Pn.
For every page say the lecture date written on it, if any, and its topic. Return only a JSON object:
{"pages": [{"page": k, "date": "<GG/MM/AAAA if a lecture date is written on this page, else empty>", "topic": "<the topic in Italian, at most 8 words>"}]}
Every page appears exactly once. A date counts only when it marks a lecture (a heading such as "Lezione del 20/10", a date at the top of a page), not dates inside exercises or examples.)";

const char *const SORT_PROMPT =
    R"(You sort new material of a university course. You get the course as it is, made of UNITS (lectures with number, date, title and sections, or topics with number, title and sections), and the new material: for each file, its pages with date and topic (or, for a text, its beginning; for LaTeX notes by another author, its lectures, each one a whole lecture labelled P like a page).
Decide where every piece goes:
- "new": a unit the course does not have yet. In a course by lectures: one group per lecture; pages with the same lecture date, or that continue the same topic, stay together; a file that covers several lectures is split; material with no date at all is a lecture of its own with an empty date. In a course by topics: one group per new topic, with "after": the number of the existing topic it should follow (0: before all of them).
- "unit": material that belongs to a unit the course already has (same date, or the same content): "unit" is its number.
- "skip": only pages with nothing to keep (blank, covers, indexes, duplicated pages).
Say also the role of every file:
- "main": notes that make lectures (handwritten notes, a colleague's LaTeX notes);
- "supplementary": slides, a book chapter, a handout: they complete the units they lean on, and make a new unit only when nothing in the course matches;
- "exercises": exercise sheets: they go with the unit whose topic they practise, or into a new unit of exercises.
Return only a JSON object:
{"roles": {"<file name>": "main|supplementary|exercises"},
 "groups": [{"dest": "new", "date": "<GG/MM/AAAA or empty>", "title": "<Italian, at most 40 characters>", "after": <topic number, only in a course by topics>, "pieces": [{"file": "<name>", "from": <first page>, "to": <last page>}], "overlap": <only for a group with pieces of more than one file: true if they tell the same things twice (two sets of notes of the same lecture), false if they are parts that follow each other; when unsure, true>, "repeats": [{"unit": <number of an existing unit that this new one explains again, in part: a recap, the same derivation, the same definitions or examples, even if it then goes further; not a unit it merely continues>, "what": "<Italian, what it explains again, at most 12 words>"}], "why": "<Italian, at most 15 words>"},
            {"dest": "unit", "unit": <number of the existing unit>, "pieces": [...], "why": "..."}],
 "skip": [{"file": "<name>", "from": <k>, "to": <k>, "why": "..."}]}
Rules: every page of every file is in exactly one piece of one group or in skip; pieces are ranges of consecutive pages of one file; a text file is one piece with "from": 1, "to": 1. A lecture of LaTeX notes is never split, and two of them go in the same group only if they are the same lecture (two lectures on the same day are usually two lectures). Groups of new lectures in calendar order; inside a group, pieces in reading order. The roles under USER ROLES are fixed by the user: keep them and sort accordingly.
If the request says THE USER ADDED THIS MATERIAL TO a unit, it goes there, unless a page clearly belongs to another unit or to none.
If the request ends with YOUR PREVIOUS PLAN, WITH THE USER'S INSTRUCTIONS: make the plan again following them. The user's instructions win over your own reading (they may move pieces, merge or split groups, change dates or titles, skip pages); what they do not mention stays as before.)";

// I ruoli: come li dice il modello, come li legge l'utente.
const QStringList ROLES{"main", "supplementary", "exercises"};
const QStringList ROLE_NAMES{"Principale", "Integrativa", "Esercizi"};

QString pageRange(int from, int to) { return from == to ? QStringLiteral("pagina %1").arg(from) : QStringLiteral("pagine %1–%2").arg(from).arg(to); }

// Le lezioni di un .tex come le dice il piano: «lezione del 4/11/2026, seconda lezione del 10/11/2026».
QString lectureLabels(const QJsonArray &specs)
{
    QStringList labels;
    for (const QJsonValue &spec : specs)
        labels << texsource::label(texsource::parseSpec(spec.toString()));
    return labels.join(", ");
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

// La lezione che ha una fonte usata: quella della sua cartella «lezione N».
QString ownerOf(const Project &project, const QString &used)
{
    static const QRegularExpression folder(R"(fonti/usate/lezione (\d+)/)");
    const int number = folder.match(used).captured(1).toInt();
    for (const Lesson &lesson : project.lessons())
        if (number > 0 && lesson.number == number)
            return lesson.file;
    return {};
}

QJsonObject entry(const QString &text, const QJsonObject &piece, bool doIt = true)
{
    return QJsonObject{{"testo", text}, {"fare", doIt}, {"nota", ""}, {"pezzo", piece}};
}

QJsonObject choice(const QStringList &options, const QStringList &values, const QString &value, const QStringList &redo = {})
{
    return QJsonObject{{"opzioni", QJsonArray::fromStringList(options)}, {"valori", QJsonArray::fromStringList(values)}, {"valore", value},
                       {"rifai", QJsonArray::fromStringList(redo)}};
}

QString settingsLine(const Settings &settings)
{
    const QList<std::pair<QString, QString>> keys{{"scrittura", "Scrittura"}, {"qualita", "Qualità"}, {"figure_scansioni", "Figure delle scansioni"},
                                                  {"figure_tex", "Figure dei .tex"}, {"impaginazione", "Impaginazione"}};
    QStringList parts;
    for (const auto &[key, name] : keys)
        for (const Settings::Choice &c : Settings::choices(key))
            if (c.value == settings.value(key))
                parts << QStringLiteral("%1: %2").arg(name, c.label);
    return parts.join(" · ");
}
}

Outcome plan(const Project &project, const Settings &settings, const QStringList &files, JobControl *control, const QJsonObject &previous, const QString &target)
{
    Outcome out;
    Model model(settings.quality, project.root(), control);
    const bool byTopic = settings.byTopic();
    try {
        // il corso com'è: le sue unità (lezioni, o argomenti), con il testo per riconoscere ciò che c'è già
        const QList<Lesson> lessons = project.lessons();
        QStringList course;
        QHash<int, QString> unitFile;                  // il numero che vede il modello → il file
        QHash<QString, int> unitNumber;
        QHash<QString, QString> unitLabel;             // il file → come lo dice il piano
        QHash<QString, QSet<QString>> unitWords;
        for (qsizetype i = 0; i < lessons.size(); ++i) {
            const Lesson &lesson = lessons[i];
            const QString text = files::read(project.path(lesson.file));
            const QStringList sections = texsource::gist(text);   // il titolo e l'inizio di ogni sezione
            const int n = byTopic ? int(i + 1) : lesson.number;
            unitFile.insert(n, lesson.file);
            unitNumber.insert(lesson.file, n);
            unitLabel.insert(lesson.file, byTopic ? QStringLiteral("argomento «%1»").arg(lesson.title) : QStringLiteral("lezione %1 · %2").arg(lesson.number).arg(lesson.title));
            unitWords.insert(lesson.file, texsource::shingles(text));
            course << (byTopic ? QStringLiteral("- topic %1, \"%2\". Sections: %3").arg(n).arg(lesson.title, sections.join(" | "))
                               : QStringLiteral("- lecture %1, date %2, \"%3\". Sections: %4").arg(n).arg(lesson.date.isEmpty() ? "?" : lesson.date, lesson.title, sections.join(" | ")));
        }
        // con la preposizione giusta: «alla lezione 3 · Titolo», «nell'argomento «Titolo»»
        auto with = [&](const QString &preposition, const QString &file) {
            static const QHash<QString, std::pair<QString, QString>> forms{{"a", {"alla ", "all'"}}, {"in", {"nella ", "nell'"}}, {"per", {"per la ", "per l'"}}, {"dopo", {"dopo la ", "dopo l'"}}};
            return (byTopic ? forms.value(preposition).second : forms.value(preposition).first) + unitLabel.value(file);
        };
        auto containedIn = [&](const QString &text) -> QString {    // l'unità del corso che contiene già questo testo
            const QSet<QString> words = texsource::shingles(text);
            QString best;
            double share = texsource::CONTAINED;
            for (auto it = unitWords.begin(); it != unitWords.end(); ++it)
                if (const double s = texsource::contained(words, it.value()); s >= share) {
                    share = s;
                    best = it.key();
                }
            return best;
        };

        // ciò che l'utente ha deciso nel piano di prima: i ruoli, le versioni da smistare come materiale nuovo
        QHash<QString, QString> userRoles;
        for (const QJsonValue &q : previous.value("domande").toArray())
            if (q.toObject().value("chiave").toString() == "ruolo" && ROLE_NAMES.contains(q.toObject().value("risposta").toString()))
                userRoles.insert(q.toObject().value("file").toString(), q.toObject().value("risposta").toString());
        QHash<QString, QString> answered;              // le risposte già date su ciò che una lezione nuova riprende
        for (const QJsonValue &s : previous.value("sezioni").toArray()) {
            const QJsonObject crossing = s.toObject().value("intreccio").toObject();
            if (!crossing.value("valore").toString().isEmpty())
                answered.insert(crossing.value("unita").toString() + '|' + s.toObject().value("voci").toArray().first().toObject().value("testo").toString(),
                                crossing.value("valore").toString());
        }
        QSet<QString> asNew;
        for (const QJsonValue &s : previous.value("sezioni").toArray())
            if (s.toObject().value("gruppo").toObject().value("dest").toString() == "version" && s.toObject().value("scelta").toObject().value("valore").toString() == "nuovo")
                asNew.insert(s.toObject().value("gruppo").toObject().value("fonte").toString());

        // il materiale, file per file
        QStringList material;
        QHash<QString, int> sizes;                     // nome → pagine (o lezioni di un .tex, o 1 per un testo)
        QHash<QString, int> lines;                     // nome → righe, per la stima
        QHash<QString, QJsonArray> lectures;           // di un .tex: le sue lezioni, una per «pagina»
        QJsonArray known, versions;
        for (int i = 0; i < files.size(); ++i) {
            if (control)
                control->progress(0.8 * i / files.size(), QStringLiteral("leggo %1").arg(QFileInfo(files[i]).fileName()));
            const QString rel = files[i], name = QFileInfo(rel).fileName(), path = project.path(rel), kind = Pages::kindOf(name);
            if (!QFileInfo::exists(path)) {
                out.unread.insert(rel, QStringLiteral("il file non c'è più"));
                continue;
            }
            // una fonte già usata: identica (un doppione), o una versione nuova
            const newlesson::Version version = newlesson::versionOf(project, path);
            if (!version.used.isEmpty() && version.identical) {
                const QString owner = ownerOf(project, version.used);
                known.append(entry(QStringLiteral("%1: è già fra le fonti usate").arg(name) + (owner.isEmpty() ? QString() : ", " + with("in", owner)),
                                   QJsonObject{{"file", name}, {"identico", true}, {"in", owner}}, false));
                continue;
            }
            if (!version.used.isEmpty() && !asNew.contains(rel)) {
                const QString owner = ownerOf(project, version.used);
                const QString title = QStringLiteral("Versione nuova di %1").arg(QFileInfo(version.used).fileName())
                                      + (owner.isEmpty() ? QString() : ", usata " + with("per", owner));
                versions.append(QJsonObject{{"titolo", title}, {"voci", QJsonArray{entry(name, QJsonObject{{"file", name}})}},
                                            {"scelta", choice({"Porta le differenze", "Smistala come materiale nuovo", "Salta"}, {"porta", "nuovo", "salta"}, "porta", {"nuovo"})},
                                            {"gruppo", QJsonObject{{"dest", "version"}, {"fonte", rel}, {"usata", version.used}, {"file", owner}}}});
                continue;
            }
            if (kind == "tex") {                       // le lezioni del file, senza modello: data, titolo, sezioni, l'inizio
                const QString document = files::read(path);
                const QList<newlesson::TexLecture> all = newlesson::texLectures(project, rel);
                if (all.isEmpty()) {                   // non diviso in lezioni: un pezzo, come un testo
                    if (const QString in = containedIn(texsource::body(document)); !in.isEmpty()) {
                        known.append(entry(QStringLiteral("%1: il testo c'è già, %2").arg(name, with("in", in)), QJsonObject{{"file", name}, {"in", in}}, false));
                        continue;
                    }
                    sizes.insert(name, 1);
                    lines.insert(name, int(document.count('\n')));
                    material << QStringLiteral("FILE %1 (LaTeX notes, one piece)\n%2").arg(name, texsource::body(document).left(1200));
                    continue;
                }
                QStringList described;
                QJsonArray specs;
                int count = 0, size = 0;
                for (const newlesson::TexLecture &t : all) {
                    const texsource::Ref ref = texsource::parseSpec(t.spec);
                    if (!t.in.isEmpty()) {             // il corso la contiene già
                        known.append(entry(QStringLiteral("%1 · %2: c'è già, %3").arg(name, texsource::label(ref), with("in", t.in)),
                                           QJsonObject{{"file", name}, {"lezioni", QJsonArray{t.spec}}, {"in", t.in}}, false));
                        continue;
                    }
                    described << QStringLiteral("- P%1: date %2, \"%3\". Sections: %4")
                                     .arg(++count).arg(t.lesson.date.toString("dd/MM/yyyy"), t.lesson.heading, texsource::gist(t.lesson.text).join(" | "));
                    if (!t.sameDate.isEmpty())
                        described.last() += QStringLiteral(" (the course has unit %1 on the same day, not written from this one)").arg(unitNumber.value(t.sameDate));
                    specs << t.spec;
                    size += t.lesson.lines;
                }
                if (count == 0)
                    continue;
                described.prepend(QStringLiteral("FILE %1 (LaTeX notes by another author: %2 lectures the course does not contain yet, each P is a whole lecture)").arg(name).arg(count));
                sizes.insert(name, count);
                lines.insert(name, size);
                lectures.insert(name, specs);
                material << described.join('\n');
                continue;
            }
            if (kind == "testo") {
                QString text;
                try {
                    text = Pages::read(path, project.path("build/lavori/smistamento/testi")).text;
                } catch (const std::exception &e) {
                    out.unread.insert(rel, QString::fromUtf8(e.what()));
                    continue;
                }
                if (const QString in = containedIn(text); !in.isEmpty()) {
                    known.append(entry(QStringLiteral("%1: il testo c'è già, %2").arg(name, with("in", in)), QJsonObject{{"file", name}, {"in", in}}, false));
                    continue;
                }
                sizes.insert(name, 1);
                lines.insert(name, int(text.count('\n')) + 1);
                material << QStringLiteral("FILE %1 (text, one piece)\n%2").arg(name, text.left(1200));
                continue;
            }
            if (kind != "pagine") {
                out.unread.insert(rel, QStringLiteral("Texfix non sa leggere questo tipo di file"));
                continue;
            }
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                out.unread.insert(rel, QStringLiteral("non riesco ad aprire il file"));
                continue;
            }
            const QString key = QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha1).toHex().left(12));
            const QString cache = project.path("build/lavori/smistamento/" + key + ".json");
            Pages pages;
            try {
                pages = Pages::read(path, project.path("build/lavori/converti/" + key + "/pagine"));
            } catch (const std::exception &e) {
                out.unread.insert(rel, QString::fromUtf8(e.what()));
                continue;
            }
            QJsonObject described = QJsonDocument::fromJson(files::read(cache).toUtf8()).object();
            if (described.value("pages").toArray().size() != pages.count()) {
                QStringList parts;
                for (int p = 0; p < pages.count(); ++p)
                    parts << QStringLiteral("P%1").arg(p + 1) << pages.thumb[p];
                parts << QStringLiteral("The file has %1 pages.").arg(pages.count());
                const QString reply = model.ask("outline", QString::fromUtf8(PAGES_PROMPT), parts, name);   // se il modello non risponde, si dice
                try {
                    described = Model::json(reply).toObject();
                } catch (const std::exception &) {     // una risposta che non si legge: le pagine senza data né argomento
                    described = {};
                }
                if (described.value("pages").toArray().size() == pages.count())
                    files::write(cache, QString::fromUtf8(QJsonDocument(described).toJson()));
            }
            sizes.insert(name, pages.count());
            lines.insert(name, pages.count());
            QStringList pageLines{QStringLiteral("FILE %1 (%2 pages)").arg(name).arg(pages.count())};
            for (const QJsonValue &p : described.value("pages").toArray())
                pageLines << QStringLiteral("- P%1: date %2, %3").arg(p.toObject().value("page").toInt()).arg(p.toObject().value("date").toString("").isEmpty() ? "-" : p.toObject().value("date").toString(),
                                                                     p.toObject().value("topic").toString());
            material << pageLines.join('\n');
        }

        QStringList unreadLines;
        for (auto it = out.unread.begin(); it != out.unread.end(); ++it)
            unreadLines << QStringLiteral("%1 (%2)").arg(QFileInfo(it.key()).fileName(), it.value());
        if (material.isEmpty() && known.isEmpty() && versions.isEmpty()) {
            out.failure = unreadLines.isEmpty() ? QStringLiteral("Non c'è materiale da aggiungere.") : QStringLiteral("Non so leggere: %1.").arg(unreadLines.join("; "));
            out.cost = model.cost();
            return out;
        }

        // il piano del modello: dove va ogni pezzo, e il ruolo di ogni fonte
        QJsonObject answer;
        bool fallback = false;
        if (!material.isEmpty()) {
            if (control)
                control->progress(0.85, QStringLiteral("divido il materiale"));
            QString request = QStringLiteral("COURSE BY %1\n%2\n\nNEW MATERIAL\n%3").arg(byTopic ? "TOPICS" : "LECTURES", course.isEmpty() ? QStringLiteral("(none yet)") : course.join('\n'), material.join("\n\n"));
            if (unitNumber.contains(target))
                request += QStringLiteral("\n\nTHE USER ADDED THIS MATERIAL TO %1 %2").arg(byTopic ? "topic" : "lecture").arg(unitNumber.value(target));
            QStringList fixed;
            for (auto it = userRoles.begin(); it != userRoles.end(); ++it)
                if (sizes.contains(it.key()))
                    fixed << QStringLiteral("%1: %2").arg(it.key(), ROLES.value(ROLE_NAMES.indexOf(it.value())));
            fixed.sort();                              // sempre nello stesso ordine
            if (!fixed.isEmpty())
                request += "\n\nUSER ROLES\n" + fixed.join('\n');
            if (!previous.isEmpty())
                request += "\n\nYOUR PREVIOUS PLAN, WITH THE USER'S INSTRUCTIONS\n" + previousPlan(previous);
            // due risposte al più; se il modello non risponde (Model riprova già da sé) lo si dice, non si ripiega
            for (int attempt = 0; attempt < 2 && answer.isEmpty(); ++attempt) {
                const QString reply = model.ask("outline", QString::fromUtf8(SORT_PROMPT), {request}, "smistamento");
                try {
                    answer = Model::json(reply).toObject();
                } catch (const std::exception &) {
                    answer = {};
                }
                if (!coversOnce(answer, sizes))
                    answer = {};
            }
            fallback = answer.isEmpty();
            if (fallback) {                            // ripiego: ogni file una lezione nuova, ogni lezione di un .tex la sua
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
        }

        // le sezioni del piano: una per destinazione, una voce per pezzo
        QJsonArray sections;
        QList<Convert::Source> estimated;
        for (const QJsonValue &g : answer.value("groups").toArray()) {
            const QJsonObject group = g.toObject();
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
                const int share = sizes.value(name) > 0 ? lines.value(name) * (to - from + 1) / sizes.value(name) : 0;
                estimated << Convert::Source{name, Pages::kindOf(name), qMax(share, 1)};
                entries.append(entry(text, piece));
            }
            const QString why = group.value("why").toString().trimmed();
            const QString unit = unitFile.value(group.value("unit").toInt(group.value("lesson").toInt()));
            if ((group.value("dest").toString() == "unit" || group.value("dest").toString() == "lesson") && !unit.isEmpty()) {
                QString title = QStringLiteral("Si sovrappone %1").arg(with("a", unit));
                if (!why.isEmpty())
                    title += " — " + why;
                const QString number = byTopic ? QString() : QStringLiteral(" %1").arg(unitNumber.value(unit));
                sections.append(QJsonObject{{"titolo", title}, {"voci", entries},
                                            {"scelta", byTopic ? choice({"Fondi nell'argomento", "In fondo all'argomento, a parte", "Salta"}, {"fondi", "a_parte", "salta"}, "fondi")
                                                               : choice({"Fondi nella lezione" + number, "Lezione a parte", "Salta"}, {"fondi", "a_parte", "salta"}, "fondi")},
                                            {"gruppo", QJsonObject{{"dest", "unit"}, {"file", unit}, {"unit", unitNumber.value(unit)}}}});
                continue;
            }
            const QString name = group.value("title").toString().trimmed(), date = group.value("date").toString().trimmed();
            QString title;
            QString after;                             // per argomento: dopo quale argomento va
            if (byTopic) {
                const int k = group.value("after").toInt(-1);
                after = k == 0 ? QStringLiteral("^") : unitFile.value(k);
                title = QStringLiteral("Argomento nuovo · %1").arg(name.isEmpty() ? QStringLiteral("senza titolo") : name);
                if (k == 0)
                    title += QStringLiteral(" · in testa");
                else if (!after.isEmpty())
                    title += " · " + with("dopo", after);
            } else {
                title = QStringLiteral("Lezione nuova · %1").arg(name.isEmpty() ? QStringLiteral("senza titolo") : name)
                        + (date.isEmpty() ? QStringLiteral(" · senza data: va in fondo") : QStringLiteral(" · lezione del %1").arg(date));
            }
            if (!why.isEmpty())
                title += " — " + why;
            QJsonObject section{{"titolo", title}, {"voci", entries}, {"gruppo", QJsonObject{{"dest", "new"}, {"date", date}, {"title", name}, {"after", after}}}};
            // pezzi di più file, tutti pagine: si uniscono e si convertono se si seguono, li fonde l'agente se si ripetono
            QSet<QString> from;
            bool pages = true;
            for (const QJsonValue &p : group.value("pieces").toArray()) {
                from.insert(p.toObject().value("file").toString());
                pages = pages && Pages::kindOf(p.toObject().value("file").toString()) == "pagine";
            }
            if (from.size() > 1 && pages) {
                const bool repeat = group.value("overlap").toBool(true);   // se non lo dice: il percorso sicuro
                section["scelta"] = choice({"Unisci e converti, in quest'ordine", "Fai fondere all'agente"}, {"unisci", "fondi"}, repeat ? "fondi" : "unisci");
                section["titolo"] = title + (repeat ? QStringLiteral(" · si ripetono") : QStringLiteral(" · si seguono"));
            }
            // riprende in parte lezioni che ci sono: si chiede che cosa farne, senza una risposta già scelta
            QStringList again, known;
            QString first;
            for (const QJsonValue &r : group.value("repeats").toArray()) {
                const QString unit = unitFile.value(r.toObject().value("unit").toInt());
                const QString what = r.toObject().value("what").toString().trimmed();
                if (unit.isEmpty() || what.isEmpty())
                    continue;
                if (first.isEmpty())
                    first = unit;
                again << QStringLiteral("%1%2 (%3)").arg(byTopic ? "l'" : "la ", unitLabel.value(unit), what);
                known << QStringLiteral("%1 \"%2\" (%3): %4").arg(byTopic ? "topic" : QStringLiteral("lecture %1").arg(unitNumber.value(unit)),
                                                                  Project::lessonOf(unit, files::read(project.path(unit))).title, unit, what);
            }
            if (!first.isEmpty()) {
                const QString shortName = byTopic ? unitLabel.value(first) : QStringLiteral("lezione %1").arg(unitNumber.value(first));
                const QString fresh = byTopic ? QStringLiteral("Argomento nuovo") : QStringLiteral("Lezione nuova");
                QJsonObject crossing = choice({fresh + ", con tutto", fresh + ", senza ripetere: rimanda " + (byTopic ? "all'" : "alla ") + shortName,
                                               "Fondi " + QString(byTopic ? "nell'" : "nella ") + shortName},
                                              {"tieni", "rimanda", "fondi"}, answered.value(first + '|' + entries.first().toObject().value("testo").toString()));
                crossing["testo"] = QStringLiteral("Riprende in parte %1. Che cosa ne faccio?").arg(again.join("; "));
                crossing["unita"] = first;
                crossing["nota"] = known.join("; ");
                section["intreccio"] = crossing;
                QString titled = section.value("titolo").toString();   // prima del perché del modello, se c'è
                const qsizetype dash = titled.indexOf(" — ");
                titled.insert(dash < 0 ? titled.size() : dash, QStringLiteral(" · riprende ") + (byTopic ? "l'" : "la ") + unitLabel.value(first));
                section["titolo"] = titled;
            }
            sections.append(section);
        }
        for (const QJsonValue &v : std::as_const(versions))
            sections.append(v);
        if (!known.isEmpty())
            sections.append(QJsonObject{{"titolo", "Già nel corso: con la spunta si porta nella lezione che lo contiene"}, {"voci", known}, {"gruppo", QJsonObject{{"dest", "known"}}}});

        // le domande: il ruolo di ogni fonte che il modello ha letto
        QJsonArray questions;
        const QJsonObject roles = answer.value("roles").toObject();
        QStringList names = sizes.keys();
        names.sort();
        for (const QString &name : std::as_const(names)) {
            QString role = userRoles.value(name);
            if (role.isEmpty())
                role = ROLE_NAMES.value(qMax(0, ROLES.indexOf(roles.value(name).toString())));
            questions.append(QJsonObject{{"testo", QStringLiteral("Che ruolo ha %1?").arg(name)}, {"opzioni", QJsonArray::fromStringList(ROLE_NAMES)},
                                         {"risposta", role}, {"proposta", role}, {"chiave", "ruolo"}, {"file", name}});
        }

        QStringList summary;
        summary << (fallback ? QStringLiteral("Il modello non ha diviso il materiale in modo che torni (pagine perse o contate due volte): propongo un file per "
                                              "lezione. Correggi con le istruzioni sotto le voci.")
                             : QStringLiteral("Il materiale nuovo e dove va. Togli la spunta a ciò che non vuoi; scrivi un'istruzione sotto una voce per "
                                              "spostarla o cambiarla."));
        if (!questions.isEmpty())
            summary << QStringLiteral("Ruoli: una fonte principale crea le lezioni, un'integrativa completa quelle a cui si appoggia, gli esercizi vanno "
                                      "con la lezione che esercitano. Cambiare un ruolo rifà il piano.");
        QStringList skipped;
        for (const QJsonValue &s : answer.value("skip").toArray())
            skipped << QStringLiteral("%1 %2%3").arg(s.toObject().value("file").toString(), pageRange(s.toObject().value("from").toInt(), s.toObject().value("to").toInt()),
                                                      s.toObject().value("why").toString().isEmpty() ? QString() : " (" + s.toObject().value("why").toString() + ")");
        if (!skipped.isEmpty())
            summary << QStringLiteral("Salto: %1.").arg(skipped.join("; "));
        if (!unreadLines.isEmpty())
            summary << QStringLiteral("Non so leggere: %1. Restano in arrivo.").arg(unreadLines.join("; "));

        QJsonObject document{{"titolo", "Aggiungi materiale"}, {"riepilogo", summary.join("\n\n")}, {"stato", "piano"}, {"sezioni", sections},
                             {"domande", questions}, {"commento", ""}, {"tipo", "materiale"}, {"file", QJsonArray::fromStringList(files)}, {"lezione_scelta", target},
                             {"impostazioni", settingsLine(settings)}, {"stima", Convert::estimate(settings.quality, estimated)}};
        // un nome per il lavoro, poi il piano intero
        out.plan = plans::create(project, QJsonObject{{"titolo", "Aggiungi materiale"}, {"sezioni", QJsonArray{QJsonObject{{"titolo", "-"}, {"voci", QJsonArray{"-"}}}}}}, &out.failure);
        if (!out.plan.isEmpty())
            plans::save(project, out.plan, document);
    } catch (const Stopped &) {
        out.failure = QStringLiteral("Lavoro fermato.");
    } catch (const LimitReached &e) {
        out.failure = QStringLiteral("Il modello ha raggiunto il limite di utilizzo; si riprova più tardi (%1).").arg(QString::fromUtf8(e.what()));
    } catch (const std::exception &e) {
        out.failure = QString::fromUtf8(e.what());     // dice già che cosa non è andato, e perché
    }
    out.cost = model.cost();
    return out;
}

QString previousPlan(const QJsonObject &document)
{
    QStringList lines;
    for (const QJsonValue &s : document.value("sezioni").toArray()) {
        const QJsonObject group = s.toObject().value("gruppo").toObject();
        const QString dest = group.value("dest").toString(), chosen = s.toObject().value("scelta").toObject().value("valore").toString();
        if (dest == "unit" || dest == "lesson")
            lines << QStringLiteral("GROUP unit %1").arg(group.value("unit").toInt(group.value("lesson").toInt()))
                         + (chosen == "a_parte" ? QStringLiteral(" — USER: not merged, a unit of its own") : chosen == "salta" ? QStringLiteral(" — USER: skip it") : QString());
        else if (dest == "new")
            lines << QStringLiteral("GROUP new, date \"%1\", title \"%2\"").arg(group.value("date").toString(), group.value("title").toString());
        else
            continue;                                  // versioni e ciò che c'è già non sono del modello
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
    const bool byTopic = Settings::load(project).byTopic();
    QHash<QString, QString> paths;                     // nome → fonte in arrivo
    for (const QJsonValue &f : document.value("file").toArray())
        paths.insert(QFileInfo(f.toString()).fileName(), f.toString());
    int number = newlesson::nextNumber(project);
    const int lastFile = newlesson::lastFile(project);

    // un pezzo di un PDF diventa un file suo fra quelli in arrivo; se qualcosa non va, i pezzi fatti si tolgono
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
        return Prepared{{}, {}, {}, {}, out.failure};
    };
    auto withNotes = [](const QString &message, const QStringList &notes) {
        return notes.isEmpty() ? message : message + "\nIstruzioni dell'utente: " + notes.join("; ");
    };

    struct Fuse {                                      // una lezione (o un argomento) nuova che scrive l'agente
        QStringList named, notes;
        int number;
        QString date;
        Convert::Wanted where;
        QStringList main;                              // le fonti principali, se ce ne sono anche di integrative
    };
    QList<Fuse> fuses;
    QHash<QString, QString> roles;                     // file → ruolo, dalle domande del piano
    for (const QJsonValue &q : document.value("domande").toArray())
        if (q.toObject().value("chiave").toString() == "ruolo")
            roles.insert(q.toObject().value("file").toString(), q.toObject().value("risposta").toString());
    QHash<QString, QStringList> integrate;             // lezione → fonti già nel corso da riportarci
    for (const QJsonValue &s : document.value("sezioni").toArray()) {
        const QJsonObject group = s.toObject().value("gruppo").toObject();
        const QString dest = group.value("dest").toString(), chosen = s.toObject().value("scelta").toObject().value("valore").toString();
        if (dest == "version") {                       // la vecchia in «precedenti», la nuova al suo posto, l'agente porta le differenze
            if (chosen != "porta")
                continue;
            const QString source = group.value("fonte").toString(), owner = group.value("file").toString(), used = group.value("usata").toString();
            if (owner.isEmpty()) {
                out.done << QStringLiteral("%1 resta in arrivo: non so quale lezione usava %2").arg(QFileInfo(source).fileName(), QFileInfo(used).fileName());
                continue;
            }
            const QString copy = newlesson::replaceVersion(project, used, project.path(source));
            if (copy.isEmpty()) {
                out.done << QStringLiteral("non riesco a mettere %1 al posto della versione vecchia").arg(QFileInfo(source).fileName());
                continue;
            }
            out.agent << newlesson::updateMessage(owner, newlesson::forAgent(project, {copy}).first(), QFileInfo(used).path() + "/precedenti/");
            continue;
        }
        if (dest == "known") {                         // con la spunta: nella lezione che lo contiene; senza, è un doppione
            for (const QJsonValue &v : s.toObject().value("voci").toArray()) {
                const QJsonObject entry = v.toObject(), piece = entry.value("pezzo").toObject();
                const QString name = piece.value("file").toString(), source = paths.value(name), in = piece.value("in").toString();
                if (source.isEmpty() || !QFileInfo::exists(project.path(source)))
                    continue;
                if (entry.value("fare").toBool()) {
                    if (in.isEmpty())
                        out.done << QStringLiteral("%1 resta in arrivo: non so in quale lezione portarlo").arg(name);
                    else if (piece.contains("lezioni"))
                        for (const QJsonValue &spec : piece.value("lezioni").toArray())
                            integrate[in] << spec.toString();
                    else
                        integrate[in] << source;
                } else if (piece.value("identico").toBool()) {
                    QFile::remove(project.path(source));
                    out.done << QStringLiteral("tolto %1, già fra le fonti usate").arg(name);
                } else if (!piece.contains("lezioni")) {
                    QDir().mkpath(project.path("fonti/usate/già nel corso"));
                    QFile::rename(project.path(source), files::freeName(project.path("fonti/usate/già nel corso/" + name)));
                    out.done << QStringLiteral("%1 è già nel corso: va fra le fonti usate").arg(name);
                }
            }
            continue;
        }
        if (chosen == "salta")
            continue;
        QStringList sources, notes, origins;           // origins: per ogni fonte, il file da cui viene
        for (const QJsonValue &v : s.toObject().value("voci").toArray()) {
            const QJsonObject entry = v.toObject(), piece = entry.value("pezzo").toObject();
            const QString name = piece.value("file").toString(), source = paths.value(name);
            if (source.isEmpty() || !QFileInfo::exists(project.path(source)))
                continue;
            const int from = piece.value("from").toInt(), to = piece.value("to").toInt();
            const bool part = !piece.contains("lezioni") && Pages::kindOf(name) == "pagine" && name.endsWith(".pdf", Qt::CaseInsensitive)
                              && (from > 1 || to < Pages::size(project.path(source)));
            if (!entry.value("fare").toBool()) {      // tolto dal piano: un pezzo di un PDF resta in arrivo, da solo
                if (part && cut(name, source, from, to).isEmpty())
                    return fail();
                continue;
            }
            if (!entry.value("nota").toString().trimmed().isEmpty())
                notes << QStringLiteral("%1: %2").arg(name, entry.value("nota").toString().trimmed());
            if (piece.contains("lezioni")) {          // le lezioni di un .tex: si indicano, il file resta com'è
                for (const QJsonValue &spec : piece.value("lezioni").toArray()) {
                    sources << spec.toString();
                    origins << name;
                }
                continue;
            }
            origins << name;
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
        const QStringList named = newlesson::forAgent(project, sources);   // come le legge l'agente
        const QString unit = group.value("file").toString();
        if (dest == "unit" && chosen != "a_parte") {   // si fonde con ciò che c'è
            out.agent << withNotes(newlesson::integrateMessage(unit, named), notes);
            continue;
        }
        // una lezione nuova che riprende in parte una che c'è: con tutto, senza ripetere (rimanda), o fusa in quella
        const QJsonObject crossing = s.toObject().value("intreccio").toObject();
        const QString crossed = crossing.value("valore").toString();
        if (crossed == "fondi") {
            out.agent << withNotes(newlesson::integrateMessage(crossing.value("unita").toString(), named), notes);
            continue;
        }
        const int n = number++;
        QString date = group.value("date").toString();
        if (const texsource::Ref ref = texsource::parseSpec(sources.first()); date.isEmpty() && ref.date.isValid())
            date = ref.date.toString("dd/MM/yyyy");
        // dove va, per argomento: in fondo a un argomento che c'è («a parte»), o un argomento nuovo dopo un altro
        Convert::Wanted where{n, date};
        QString remind;                                // per l'agente: ciò che non si riscrive
        if (crossed == "rimanda") {
            where.note = crossing.value("nota").toString();
            remind = QStringLiteral("il corso spiega già %1: non riscriverlo, richiamalo in una frase con un rimando a quella lezione; scrivi per intero solo ciò che è nuovo")
                         .arg(crossing.value("nota").toString());
        }
        if (byTopic && dest == "unit") {
            where.topic = unit;
        } else if (byTopic) {
            where.title = group.value("title").toString().trimmed().isEmpty() ? QStringLiteral("Argomento %1").arg(n) : group.value("title").toString().trimmed();
            where.after = group.value("after").toString();
        }
        // pezzi di più file che si seguono: un PDF solo, nell'ordine del piano, che si converte come una fonte
        if (sources.size() > 1 && chosen == "unisci" && notes.isEmpty()) {
            QList<int> order;
            QStringList names;
            for (int i = 0; i < sources.size(); ++i) {
                order << i;
                names << QFileInfo(sources[i]).fileName();
            }
            if (const QString united = newlesson::unite(project, sources, order, n); !united.isEmpty()) {
                out.done << QStringLiteral("%1 uniti in %2").arg(names.join(", "), QFileInfo(united).fileName());
                sources = {united};
            } else {
                out.done << QStringLiteral("non sono riuscito a unire %1: li fonde l'agente").arg(names.join(", "));
            }
        }
        if (sources.size() == 1 && notes.isEmpty()) {  // una fonte sola (anche una lezione di un .tex): la conversione
            out.sources << sources.first();
            out.wanted.insert(sources.first(), where);
        } else {                                       // più pezzi, o con istruzioni: la scrive l'agente, con un piano
            QStringList main;
            bool mixed = false;
            for (qsizetype i = 0; i < named.size(); ++i) {
                if (roles.value(origins.value(i), "Principale") == "Principale")
                    main << named[i];
                else
                    mixed = true;
            }
            fuses << Fuse{named, remind.isEmpty() ? notes : notes + QStringList{remind}, n, date, where, mixed ? main : QStringList()};
        }
    }
    // i file che scrive l'agente vengono dopo quelli della conversione, che li prende per primi; sono sempre lezioni,
    // e per argomento le colloca Texfix quando l'agente le chiude (chiudi_lezione), come fa la conversione
    int next = lastFile + int(out.sources.size());
    for (const Fuse &f : std::as_const(fuses)) {
        const QString file = QStringLiteral("src/src%1.tex").arg(++next);
        out.agent << withNotes(newlesson::fuseMessage(file, f.number, f.date, f.named, f.main), f.notes);
        if (f.where.byTopic())
            out.placements.insert(file, QJsonObject{{"argomento", f.where.topic}, {"titolo", f.where.title}, {"dopo", f.where.after}});
    }
    for (auto it = integrate.begin(); it != integrate.end(); ++it)
        out.agent << newlesson::integrateMessage(it.key(), newlesson::forAgent(project, it.value()));
    // un PDF diviso non resta in arrivo accanto ai suoi pezzi
    for (const QString &source : std::as_const(cutUp)) {
        QDir().mkpath(project.path("fonti/usate/divise"));
        QFile::rename(project.path(source), files::freeName(project.path("fonti/usate/divise/" + QFileInfo(source).fileName())));
    }
    // un .tex di cui il corso ha già tutte le lezioni va fra le fonti usate
    for (const QString &source : std::as_const(paths)) {
        if (Pages::kindOf(source) != "tex" || !QFileInfo::exists(project.path(source)))
            continue;
        const QList<newlesson::TexLecture> lectures = newlesson::texLectures(project, source);
        if (lectures.isEmpty() || !std::all_of(lectures.begin(), lectures.end(), [](const newlesson::TexLecture &t) { return !t.in.isEmpty(); }))
            continue;
        QDir().mkpath(project.path("fonti/usate/più lezioni"));
        QFile::rename(project.path(source), files::freeName(project.path("fonti/usate/più lezioni/" + QFileInfo(source).fileName())));
        out.done << QStringLiteral("il corso ha già tutte le lezioni di %1: va fra le fonti usate").arg(QFileInfo(source).fileName());
    }
    return out;
}

}
