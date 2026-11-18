#include "core/Reorganize.h"

#include "core/Files.h"
#include "core/Model.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

using RE = QRegularExpression;

namespace tx::reorganize {

namespace {
const RE &lessonLine()
{
    static const RE re(R"(^\s*\\lezione\s*\{\s*(\d*)\s*\}\s*\{([^}]*)\})");
    return re;
}
bool isSection(const QString &line) { return line.trimmed().startsWith("\\section"); }
bool isSubsection(const QString &line) { return line.trimmed().startsWith("\\subsection"); }

QString titleOf(const QString &line)
{
    static const RE re(R"(\\subsection\*?\s*(?:\[[^\]]*\])?\s*\{(.*)\}\s*$)");
    return re.match(line.trimmed()).captured(1);
}

// Le righe che contano: tutto tranne le righe vuote, i \section e i \lezione, che cambiano per forza.
QStringList contentOf(const QStringList &lines)
{
    QStringList out;
    for (const QString &line : lines) {
        const QString t = line.trimmed();
        if (!t.isEmpty() && !isSection(t) && !lessonLine().match(t).hasMatch() && t != "\\clearpage")
            out << t;
    }
    out.sort();
    return out;
}

// Le sezioni di un file, seguendo i \lezione che incontra (in un file per argomenti ce n'è più d'uno).
QList<Section> sectionsOf(const QString &file, const QStringList &lines)
{
    QList<Section> out;
    int lesson = 0, index = 0;
    QString date;
    for (const QString &line : lines) {               // ciò che sta prima del primo \lezione è già di quella lezione
        if (const auto m = lessonLine().match(line); m.hasMatch()) {
            lesson = m.captured(1).toInt();
            date = m.captured(2).trimmed();
            break;
        }
    }
    Section current;
    bool open = false;
    auto close = [&] {
        while (!current.lines.isEmpty() && current.lines.last().trimmed().isEmpty())
            current.lines.removeLast();
        bool content = false;
        for (const QString &line : std::as_const(current.lines))
            content = content || !line.trimmed().isEmpty();
        if (open && content)
            out << current;
        open = false;
    };
    for (const QString &line : lines) {
        if (isSection(line))
            continue;
        if (const auto m = lessonLine().match(line); m.hasMatch()) {
            close();                                   // una lezione nuova chiude la sezione di prima
            lesson = m.captured(1).toInt();
            date = m.captured(2).trimmed();
            continue;
        }
        if (isSubsection(line) || !open) {
            if (!open && line.trimmed().isEmpty())
                continue;
            close();
            current = Section{QStringLiteral("%1#%2").arg(file).arg(++index), lesson, date,
                              isSubsection(line) ? titleOf(line) : QStringLiteral("(inizio della lezione)"), {}};
            open = true;
        }
        current.lines << line;
    }
    close();
    return out;
}

// Mette in sicurezza il cambio: i file nuovi, il documento con i loro \input al posto di quelli vecchi, via i vecchi.
QString commit(const Project &project, const QStringList &oldFiles, const QList<std::pair<QString, QString>> &newFiles, const QStringList &before)
{
    QStringList after;
    for (const auto &[file, text] : newFiles)
        after += text.split('\n');
    if (contentOf(before) != contentOf(after))
        return QStringLiteral("Il contenuto non tornerebbe uguale riga per riga: non ho toccato niente.");
    QStringList main = files::read(project.path(Project::MAIN)).split('\n'), out;
    static const RE input(R"(\\input\{\s*(src/[^}\s]+)\s*\})");
    bool placed = false, dropped = false;
    for (const QString &line : std::as_const(main)) {
        const auto m = input.match(uncommented(line).toString());
        QString name = m.captured(1);
        if (m.hasMatch() && !name.endsWith(".tex"))
            name += ".tex";
        if (m.hasMatch() && oldFiles.contains(name)) {
            if (!placed)                               // i file nuovi stanno dove stava il primo dei vecchi
                for (const auto &[file, text] : newFiles)
                    out << QStringLiteral("\\input{%1}").arg(file) << QStringLiteral("\\clearpage") << QString();
            placed = dropped = true;
            continue;
        }
        if (dropped && (line.trimmed() == "\\clearpage" || line.trimmed().isEmpty()))
            continue;                                  // ciò che seguiva un \input tolto
        dropped = false;
        out << line;
    }
    if (!placed)
        return QStringLiteral("Non trovo le lezioni nel documento.");
    for (const QString &file : oldFiles)
        QFile::remove(project.path(file));
    for (const auto &[file, text] : newFiles)
        if (!files::write(project.path(file), text))
            return QStringLiteral("Non riesco a scrivere %1.").arg(file);
    files::write(project.path(Project::MAIN), out.join('\n'));
    return {};
}
}

QList<Section> sections(const Project &project)
{
    QList<Section> out;
    for (const QString &file : project.inputs())
        if (QFileInfo::exists(project.path(file)))
            out += sectionsOf(file, files::read(project.path(file)).split('\n'));
    return out;
}

QList<Topic> propose(const Project &project, const Settings &settings, JobControl *control, double *cost)
{
    const QList<Section> all = sections(project);
    QStringList list;
    QSet<QString> ids;
    for (const Section &s : all) {
        list << QStringLiteral("- %1 (lecture %2): %3").arg(s.id).arg(s.lesson).arg(s.title);
        ids.insert(s.id);
    }
    QList<Topic> out;
    Model model(settings.quality, project.root(), control);
    try {
        const QJsonObject answer = Model::json(model.ask("outline", QStringLiteral(
            R"(You group the sections of a university course's lecture notes (Italian) by topic, to reorganise the notes from one file per lecture into one file per topic. You get every section with its id, its lecture and its title.
Return only a JSON object: {"topics": [{"title": "<Italian topic title, at most 40 characters>", "sections": ["<id>", ...]}]}
Rules: every section id appears in exactly one topic; inside a topic the sections are in the order a student should read them; topics follow the course's logical order; a topic gathers what belongs together even when it was taught in different lectures; do not make one topic per lecture unless the lectures really are separate topics.)"),
                                                           {list.join('\n')}, "argomenti")).toObject();
        QSet<QString> seen;
        bool valid = true;
        for (const QJsonValue &t : answer.value("topics").toArray()) {
            Topic topic{t.toObject().value("title").toString().trimmed(), {}};
            for (const QJsonValue &id : t.toObject().value("sections").toArray()) {
                valid = valid && ids.contains(id.toString()) && !seen.contains(id.toString());
                seen.insert(id.toString());
                topic.sections << id.toString();
            }
            if (!topic.sections.isEmpty())
                out << topic;
        }
        if (!valid || seen.size() != ids.size())
            out.clear();
    } catch (const Stopped &) {
        throw;
    } catch (const std::exception &) {
        out.clear();
    }
    if (cost)
        *cost = model.cost();
    if (out.isEmpty()) {                               // ripiego: un argomento per lezione, com'è adesso
        const QList<Lesson> lessons = project.lessons();
        for (const Lesson &lesson : lessons) {
            Topic topic{lesson.title, {}};
            for (const Section &s : all)
                if (s.id.startsWith(lesson.file + '#'))
                    topic.sections << s.id;
            if (!topic.sections.isEmpty())
                out << topic;
        }
    }
    return out;
}

QString toTopics(const Project &project, const QList<Topic> &topics)
{
    Settings settings = Settings::load(project);
    if (settings.byTopic())
        return QStringLiteral("Il progetto è già per argomenti.");
    const QList<Section> all = sections(project);
    QHash<QString, Section> byId;
    for (const Section &s : all)
        byId.insert(s.id, s);
    QSet<QString> used;
    QList<std::pair<QString, QString>> written;
    for (const Topic &topic : topics) {
        if (topic.sections.isEmpty())
            continue;
        QStringList lines{QStringLiteral("\\section{%1}").arg(topic.title.trimmed().isEmpty() ? QStringLiteral("Argomento") : topic.title.trimmed())};
        int lesson = -1;
        for (const QString &id : topic.sections) {
            if (!byId.contains(id) || used.contains(id))
                return QStringLiteral("La scaletta non torna: ogni sezione deve stare in un argomento solo.");
            used.insert(id);
            const Section &s = byId[id];
            if (s.lesson != lesson)                    // da quale lezione viene ciò che segue
                lines << QStringLiteral("\\lezione{%1}{%2}").arg(s.lesson ? QString::number(s.lesson) : QString(), s.date);
            lesson = s.lesson;
            lines << QString();
            lines += s.lines;
        }
        written.append({QStringLiteral("src/arg%1.tex").arg(written.size() + 1), lines.join('\n') + '\n'});
    }
    if (used.size() != all.size())
        return QStringLiteral("La scaletta non torna: qualche sezione è rimasta fuori.");

    QStringList before;
    QJsonObject original;
    const QStringList oldFiles = project.inputs();
    for (const Lesson &lesson : project.lessons()) {
        before += files::read(project.path(lesson.file)).split('\n');
        original.insert(QString::number(lesson.number), QJsonObject{{"file", lesson.file}, {"titolo", lesson.title}, {"data", lesson.date}});
    }
    if (const QString error = commit(project, oldFiles, written, before); !error.isEmpty())
        return error;
    settings.topics = QJsonObject{{"lezioni", original}};
    settings.done.clear();                             // «fatta» valeva per i file di prima
    settings.save(project);
    return {};
}

QString toLessons(const Project &project)
{
    Settings settings = Settings::load(project);
    if (!settings.byTopic())
        return QStringLiteral("Il progetto è già per data di lezione.");
    const QJsonObject original = settings.topics.value("lezioni").toObject();
    QMap<int, QList<Section>> byLesson;                // in ordine di numero
    QStringList before;
    const QStringList oldFiles = project.inputs();
    for (const QString &file : oldFiles) {
        const QStringList lines = files::read(project.path(file)).split('\n');
        before += lines;
        for (const Section &s : sectionsOf(file, lines))
            byLesson[s.lesson] << s;
    }
    // dentro una lezione le sezioni tornano nell'ordine in cui erano nate: quello dei loro marcatori
    static const RE marker(R"(%\s*@L\d+\.(\d+))");
    auto firstBlock = [](const Section &s) {
        for (const QString &line : s.lines)
            if (const auto m = marker.match(line); m.hasMatch())
                return m.captured(1).toInt();
        return 1 << 30;
    };
    QList<std::pair<QString, QString>> written;
    for (auto it = byLesson.begin(); it != byLesson.end(); ++it) {
        std::stable_sort(it->begin(), it->end(), [&](const Section &a, const Section &b) { return firstBlock(a) < firstBlock(b); });
        const QJsonObject was = original.value(QString::number(it.key())).toObject();
        QStringList lines{QStringLiteral("\\section{%1}").arg(was.value("titolo").toString(QStringLiteral("Lezione %1").arg(it.key()))),
                          QStringLiteral("\\lezione{%1}{%2}").arg(it.key() ? QString::number(it.key()) : QString(), it->first().date)};
        for (const Section &s : std::as_const(*it)) {
            lines << QString();
            lines += s.lines;
        }
        written.append({was.value("file").toString(QStringLiteral("src/src%1.tex").arg(it.key())), lines.join('\n') + '\n'});
    }
    if (const QString error = commit(project, oldFiles, written, before); !error.isEmpty())
        return error;
    settings.topics = {};
    settings.done.clear();
    settings.save(project);
    return {};
}

void startByTopic(const Project &project)
{
    Settings settings = Settings::load(project);
    if (settings.byTopic())
        return;
    settings.topics = QJsonObject{{"lezioni", QJsonObject{}}};
    settings.save(project);
}

QString place(const Project &project, const QString &lesson, const QString &topic, const QString &title, const QString &after)
{
    Settings settings = Settings::load(project);
    if (!settings.byTopic())
        return QStringLiteral("Il progetto non è per argomenti.");
    const QString text = files::read(project.path(lesson));
    const Lesson info = Project::lessonOf(lesson, text);
    if (text.isEmpty())
        return QStringLiteral("Non trovo %1.").arg(lesson);
    // il contenuto: tutto ciò che segue il \section (la riga \lezione resta in testa, o la si scrive)
    QStringList lines = text.split('\n');
    const qsizetype head = std::find_if(lines.begin(), lines.end(), isSection) - lines.begin();
    if (head < lines.size())
        lines.remove(0, head + 1);
    static const RE mark(R"(^\s*\\lezione\s*\{)");
    if (!std::any_of(lines.begin(), lines.end(), [](const QString &l) { return mark.match(l).hasMatch(); }))
        lines.prepend(QStringLiteral("\\lezione{%1}{%2}").arg(info.number).arg(info.date));
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();

    QStringList main = files::read(project.path(Project::MAIN)).split('\n');
    static const RE input(R"(\\input\{\s*(src/[^}\s]+)\s*\})");
    auto inputAt = [&](const QString &file) -> qsizetype {
        for (qsizetype i = 0; i < main.size(); ++i) {
            const auto m = input.match(uncommented(main[i]).toString());
            if (m.hasMatch() && (m.captured(1) == file || m.captured(1) + ".tex" == file))
                return i;
        }
        return -1;
    };
    QString target = topic;
    if (target.isEmpty()) {                            // un argomento nuovo, con il primo nome libero
        int k = 1;
        while (QFileInfo::exists(project.path(QStringLiteral("src/arg%1.tex").arg(k))))
            ++k;
        target = QStringLiteral("src/arg%1.tex").arg(k);
        if (!files::write(project.path(target), QStringLiteral("\\section{%1}\n\n%2\n").arg(title.trimmed().isEmpty() ? info.title : title.trimmed(), lines.join('\n'))))
            return QStringLiteral("Non riesco a scrivere %1.").arg(target);
        const QStringList inputs = project.inputs();
        qsizetype at = after.isEmpty() ? -1 : after == "^" ? (inputs.isEmpty() ? -1 : inputAt(inputs.first())) : inputAt(after);
        if (after == "^" && at >= 0) {
            // in testa: prima del primo argomento
        } else if (at >= 0) {
            ++at;
            if (at < main.size() && main[at].trimmed() == "\\clearpage")
                ++at;
        } else {
            at = std::find_if(main.begin(), main.end(), [](const QString &l) { return l.trimmed() == "\\end{document}"; }) - main.begin();
        }
        main.insert(at, QStringLiteral("\\input{%1}").arg(target));
        main.insert(at + 1, QStringLiteral("\\clearpage"));
    } else {
        const QString existing = files::read(project.path(target));
        if (existing.isEmpty())
            return QStringLiteral("Non trovo l'argomento %1.").arg(target);
        if (!files::write(project.path(target), existing.trimmed() + "\n\n" + lines.join('\n') + '\n'))
            return QStringLiteral("Non riesco a scrivere %1.").arg(target);
    }
    // via la lezione dal documento
    if (const qsizetype at = inputAt(lesson); at >= 0) {
        main.removeAt(at);
        if (at < main.size() && main[at].trimmed() == "\\clearpage")
            main.removeAt(at);
    }
    files::write(project.path(Project::MAIN), main.join('\n'));
    QFile::remove(project.path(lesson));
    QJsonObject remembered = settings.topics.value("lezioni").toObject();
    remembered.insert(QString::number(info.number), QJsonObject{{"file", lesson}, {"titolo", info.title}, {"data", info.date}});
    settings.topics["lezioni"] = remembered;
    settings.save(project);
    return {};
}

}
