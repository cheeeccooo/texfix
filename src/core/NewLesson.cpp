#include "core/NewLesson.h"

#include "core/Blocks.h"
#include "core/Files.h"
#include "core/Model.h"
#include "core/Pages.h"
#include "core/TexSource.h"
#include "core/Tools.h"
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>

#include <QDate>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

using RE = QRegularExpression;

namespace tx::newlesson {

int nextNumber(const Project &project)
{
    int last = 0;
    for (const Lesson &lesson : project.lessons())
        last = qMax(last, lesson.number);
    return last + 1;
}

Marks readOne(const QString &name, const QString &text)
{
    Marks marks;
    static const RE numberRe(R"((?:lezione|lez|lecture)[\s._-]*n?[°.]?\s*(\d{1,3})(?!\d))", RE::CaseInsensitiveOption);
    static const RE dmy(R"((?<!\d)(\d{1,2})[./_-](\d{1,2})[./_-](\d{4}|\d{2})(?!\d))"), ymd(R"((?<!\d)(\d{4})[./_-](\d{1,2})[./_-](\d{1,2})(?!\d))");
    for (const QString &where : {QFileInfo(name).completeBaseName(), text.left(600)}) {
        if (!marks.number)
            marks.number = numberRe.match(where).captured(1).toInt();
        if (marks.date.isEmpty()) {
            QDate date;
            if (const auto m = ymd.match(where); m.hasMatch())
                date = QDate(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
            else if (const auto m = dmy.match(where); m.hasMatch())
                date = QDate(m.captured(3).size() == 2 ? 2000 + m.captured(3).toInt() : m.captured(3).toInt(), m.captured(2).toInt(), m.captured(1).toInt());
            if (date.isValid())
                marks.date = date.toString("dd/MM/yyyy");
        }
    }
    return marks;
}

Marks read(const QStringList &paths)
{
    Marks all;
    QStringList numbers, dates;
    for (const QString &path : paths) {
        const Marks one = readOne(path, Pages::kindOf(path) == "testo" || Pages::kindOf(path) == "tex" ? files::read(path) : QString());
        if (one.number && !numbers.contains(QString::number(one.number)))
            numbers << QString::number(one.number);
        if (!one.date.isEmpty() && !dates.contains(one.date))
            dates << one.date;
    }
    all.number = numbers.value(0).toInt();
    all.date = dates.value(0);
    if (numbers.size() > 1)
        all.conflict = QStringLiteral("Le fonti dicono numeri diversi: %1.").arg(numbers.join(", "));
    else if (dates.size() > 1)
        all.conflict = QStringLiteral("Le fonti dicono date diverse: %1.").arg(dates.join(", "));
    return all;
}

QStringList toQueue(const Project &project, const QStringList &paths)
{
    QStringList out;
    const QString queue = project.path("fonti/coda");
    QDir().mkpath(queue);
    for (const QString &path : paths) {
        const QFileInfo info(path);
        if (!info.isFile())
            continue;
        if (info.absolutePath() == QFileInfo(queue).absoluteFilePath()) {
            out << project.relative(info.absoluteFilePath());
            continue;
        }
        const QString target = files::freeName(queue + '/' + info.fileName());
        if (QFile::copy(path, target))
            out << project.relative(target);
    }
    return out;
}

QStringList settle(const Project &project, const QStringList &boxes, const QString &figureEnv, const QString &lesson, const QStringList &sources)
{
    QStringList did;
    const QString text = files::read(project.path(lesson));
    if (text.isEmpty())
        return {QStringLiteral("«%1» non esiste ancora: scrivila prima.").arg(lesson)};
    const Lesson info = Project::lessonOf(lesson, text);
    const QString marked = blocks::mark(text, info.number, blocks::Units{boxes, {figureEnv, "figure"}});
    if (marked != text && files::write(project.path(lesson), marked))
        did << QStringLiteral("aggiunti i marcatori dei blocchi che mancavano");
    if (!project.inputs().contains(lesson)) {
        QString main = files::read(project.path(Project::MAIN));
        const qsizetype end = main.lastIndexOf("\\end{document}");
        const QString input = QStringLiteral("\\input{%1}\n\\clearpage\n\n").arg(lesson);
        files::write(project.path(Project::MAIN), end < 0 ? main + '\n' + input : main.left(end) + input + main.mid(end));
        did << QStringLiteral("lezione aggiunta al documento");
    }
    const QString used = project.path(QStringLiteral("fonti/usate/lezione %1").arg(info.number));
    for (const QString &source : sources) {
        const QString file = texsource::parseSpec(source).path, from = project.path(file), name = QFileInfo(file).fileName();
        if (!file.startsWith("fonti/coda/") || !QFileInfo::exists(from))
            continue;
        QString to = used;
        if (Pages::kindOf(file) == "tex") {            // un .tex con più lezioni resta finché il corso non le ha tutte
            const QList<TexLecture> lectures = texLectures(project, file);         // con la lezione appena aggiunta
            if (std::any_of(lectures.begin(), lectures.end(), [](const TexLecture &t) { return t.in.isEmpty() && t.sameDate.isEmpty(); })) {
                did << QStringLiteral("%1 resta fra le fonti da convertire: ha altre lezioni").arg(name);
                continue;
            }
            if (lectures.size() > 1)
                to = project.path("fonti/usate/più lezioni");
        }
        QDir().mkpath(to);
        if (QFile::rename(from, files::freeName(to + '/' + name)))
            did << QStringLiteral("%1 portata fra le fonti usate").arg(name);
    }
    return did.isEmpty() ? QStringList{QStringLiteral("era già tutto a posto")} : did;
}

QList<TexLecture> texLectures(const Project &project, const QString &file)
{
    const QList<texsource::Lesson> all = texsource::lessons(files::read(project.path(file)));
    if (all.isEmpty())
        return {};
    // il corso: di ogni lezione le fonti che i marcatori nominano e il testo, per confronto
    static const QRegularExpression marker(R"(^% @L\d+\.\d+ · (.+?)(?: (?:pp?\.|r\.)\s*[\d-]+)?\s*$)", QRegularExpression::MultilineOption);
    struct Course {
        Lesson info;
        QSet<QString> named, words;
    };
    QList<Course> course;
    for (const Lesson &lesson : project.lessons()) {
        const QString text = files::read(project.path(lesson.file));
        Course c{lesson, {}, texsource::shingles(text)};
        for (auto it = marker.globalMatch(text); it.hasNext();)
            c.named << files::bare(it.next().captured(1));
        course << c;
    }
    const QString bare = files::bare(QFileInfo(file).fileName());
    QList<TexLecture> out;
    for (const texsource::Lesson &lecture : all) {
        TexLecture t{lecture, texsource::spec(file, lecture.date, lecture.nth), {}, {}};
        QList<const Course *> sameDay, named;
        for (const Course &c : std::as_const(course))
            if (c.info.date == lecture.date.toString("dd/MM/yyyy")) {
                sameDay << &c;
                if (c.named.contains(bare))
                    named << &c;
            }
        if (named.size() >= lecture.nth) {
            t.in = named[lecture.nth - 1]->info.file;
        } else {                                       // senza marcatori che lo dicano: il testo
            const QSet<QString> words = texsource::shingles(lecture.text);
            double best = texsource::CONTAINED;
            for (const Course &c : std::as_const(course))
                if (const double share = texsource::contained(words, c.words); share >= best) {
                    best = share;
                    t.in = c.info.file;
                }
        }
        if (t.in.isEmpty() && sameDay.size() >= lecture.nth)
            t.sameDate = sameDay[lecture.nth - 1]->info.file;
        out << t;
    }
    return out;
}

QString readable(const Project &project, const QString &file)
{
    static const QStringList documents{"docx", "doc", "odt", "rtf"}, images{"heic", "heif", "tif", "tiff"};
    const QFileInfo source(project.path(file));
    const QString ext = source.suffix().toLower();
    if (!documents.contains(ext) && !images.contains(ext))
        return {};
    const QString copy = QStringLiteral("build/lavori/leggibili/%1.%2").arg(source.fileName(), documents.contains(ext) ? "txt" : "png");
    const QFileInfo made(project.path(copy));
    if (made.exists() && made.lastModified() >= source.lastModified())
        return copy;
    QDir().mkpath(made.path());
    const bool ok = documents.contains(ext)
        ? tools::run("textutil", {"-convert", "txt", "-output", made.filePath(), source.filePath()}, made.path(), 60000).ok
        : tools::run("sips", {"-s", "format", "png", "-Z", "2000", source.filePath(), "--out", made.filePath()}, made.path(), 60000).ok;
    return ok && QFileInfo::exists(made.filePath()) ? copy : QString();
}

QStringList forAgent(const Project &project, const QStringList &sources)
{
    QStringList out;
    for (const QString &source : sources) {
        const texsource::Ref ref = texsource::parseSpec(source);
        QString named = ref.path;
        if (ref.date.isValid()) {                      // una lezione di un .tex: il file e la sua \section
            for (const texsource::Lesson &lesson : texsource::lessons(files::read(project.path(ref.path))))
                if (lesson.date == ref.date && lesson.nth == ref.nth)
                    named = QStringLiteral("`%1` (solo la parte `\\section{%2}`%3)")
                                .arg(ref.path, lesson.heading, ref.nth > 1 ? QStringLiteral(", la %1 con questo titolo").arg(texsource::label(ref).section(' ', 0, 0)) : QString());
        } else if (const QString copy = readable(project, source); !copy.isEmpty()) {
            named = QStringLiteral("`%1` (non lo puoi aprire: leggi la copia `%2`)").arg(source, copy);
        }
        out << named;
    }
    return out;
}

QString fuseMessage(const QString &file, int number, const QString &date, const QStringList &sources, const QStringList &important)
{
    const QString weight = important.isEmpty() ? QString()
        : QStringLiteral("\n- Fonti importanti: %1. Dove le fonti dicono cose diverse, decide una fonte importante, senza farne una domanda.").arg(important.join(", "));
    return QStringLiteral("Crea la lezione %1%2 fondendo queste fonti, che parlano della stessa lezione: %3.\n"
                          "- Leggile tutte per intero. Non c'è una fonte che comanda: la struttura la dà la più ricca, e dove due fonti dicono cose "
                          "diverse non scegliere tu: fanne una domanda del piano.\n"
                          "- Prima consegna un piano con consegna_piano: che cosa prendi da ogni fonte, in che ordine, le domande.\n"
                          "- Quando il piano è approvato scrivi `%4` (comincia con `\\section{…}` e sotto `\\lezione{%1}{%5}`), compilala finché è pulita, "
                          "poi chiama chiudi_lezione con la lezione e le fonti, e consegna l'esito.")
        .arg(number).arg(date.isEmpty() ? QString() : " del " + date, sources.join(", "), file, date) + weight;
}

QString integrateMessage(const QString &lesson, const QStringList &sources)
{
    return QStringLiteral("Integra in `%1` queste fonti nuove: %2.\n"
                          "- Leggi la lezione e le fonti. Porta nella lezione solo ciò che manca (o le correzioni), vicino al blocco a cui appartiene; "
                          "ciò che c'è già non si riscrive.\n"
                          "- Prima consegna un piano con consegna_piano: una voce per ogni aggiunta, con il blocco vicino a cui va.\n"
                          "- Quando il piano è approvato fai le aggiunte, compila finché è pulita, chiama chiudi_lezione con la lezione e le fonti, e "
                          "consegna l'esito.")
        .arg(lesson, sources.join(", "));
}


Version versionOf(const Project &project, const QString &path)
{
    Version out;
    const QFileInfo info(path);
    if (!info.isFile() || info.absoluteFilePath().startsWith(project.path("fonti/")))
        return out;
    const QString bare = files::bare(info.fileName()), suffix = info.suffix().toLower();
    for (QDirIterator it(project.path("fonti/usate"), QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
        const QString used = it.next();
        if (used.contains("/precedenti/") || files::bare(it.fileName()) != bare || QFileInfo(used).suffix().toLower() != suffix)
            continue;
        out.used = project.relative(used);
        QFile a(path), b(used);
        out.identical = a.size() == b.size() && a.open(QIODevice::ReadOnly) && b.open(QIODevice::ReadOnly) && a.readAll() == b.readAll();
        if (out.identical)
            break;                                     // una identica vince: non si aggiunge due volte
    }
    return out;
}

QString replaceVersion(const Project &project, const QString &used, const QString &path)
{
    const QString old = project.path(used);
    if (!QFileInfo::exists(old) || !QFileInfo(path).isFile())
        return {};
    const QString before = QFileInfo(old).path() + "/precedenti";
    QDir().mkpath(before);
    const QString kept = files::freeName(before + '/' + QDate::currentDate().toString("yyyy-MM-dd") + ' ' + QFileInfo(old).fileName());
    if (!QFile::rename(old, kept))
        return {};
    if (!QFile::copy(path, old)) {
        QFile::rename(kept, old);
        return {};
    }
    QDir().mkpath(project.path("fonti/coda"));
    const QString copy = files::freeName(project.path("fonti/coda/" + QFileInfo(old).fileName()));
    return QFile::copy(path, copy) ? project.relative(copy) : QString();
}

QString updateMessage(const QString &lesson, const QString &source, const QString &previous)
{
    return QStringLiteral("La fonte %1 è una versione nuova di una fonte già usata in `%2` (la vecchia è in `%3`).\n"
                          "- Confronta la versione nuova con la lezione: porta nella lezione le parti nuove e le correzioni, vicino al blocco a cui "
                          "appartengono; ciò che non è cambiato non si riscrive.\n"
                          "- Prima consegna un piano con consegna_piano: una voce per ogni parte nuova o corretta.\n"
                          "- Quando il piano è approvato fai le modifiche, compila finché è pulita, chiama chiudi_lezione con la lezione e la fonte, "
                          "e consegna l'esito.")
        .arg(source, lesson, previous);
}

Overlap overlap(const Project &project, const QString &quality, const QStringList &sources, JobControl *control)
{
    Overlap out;
    QStringList parts;
    try {
        for (qsizetype i = 0; i < sources.size(); ++i) {
            if (Pages::kindOf(sources[i]) != "pagine")
                return out;                            // tipi diversi: le fonde l'agente
            const Pages pages = Pages::read(project.path(sources[i]), project.path("build/lavori/fonti/" + QFileInfo(sources[i]).fileName()));
            parts << QStringLiteral("SOURCE %1: %2 (%3 pages). Its pages, small:").arg(i).arg(QFileInfo(sources[i]).fileName()).arg(pages.count());
            const int step = qMax(1, pages.count() / 6);       // al più sei pagine per fonte: l'inizio, la fine e qualcosa in mezzo
            for (int p = 0; p < pages.count(); p += step)
                parts << pages.thumb.value(p);
        }
        Model model(quality, project.root(), control);
        const QString system = QStringLiteral(
            "Several sources (scans or PDFs) are given for ONE lecture of a course. Decide whether they overlap: do two of them cover the same "
            "material (the same lecture written twice, e.g. the teacher's notes and a student's notes), or are they consecutive pieces of one "
            "lecture with nothing in common? Return only a JSON object: {\"overlap\": true|false, \"order\": [source indices in reading order], "
            "\"richest\": <index of the most complete source>, \"why\": \"<Italian, one short sentence; name the sources by file name, never by index>\"}. When unsure, answer overlap true.");
        const QJsonObject answer = Model::json(model.ask("outline", system, parts, "fonti")).toObject();
        out.cost = model.cost();
        QList<int> order;
        for (const QJsonValue &v : answer.value("order").toArray())
            if (v.toInt(-1) >= 0 && v.toInt() < sources.size() && !order.contains(v.toInt()))
                order << v.toInt();
        if (order.size() != sources.size() || !answer.contains("overlap"))
            return out;
        out.known = true;
        out.overlap = answer.value("overlap").toBool(true);
        out.order = order;
        out.why = answer.value("why").toString().trimmed();
    } catch (const Stopped &) {
        throw;
    } catch (const std::exception &) {
    }
    return out;
}

QString unite(const Project &project, const QStringList &sources, const QList<int> &order, int number)
{
    const QString work = project.path("build/lavori/fonti/unite");
    QDir().mkpath(work);
    QStringList pieces;
    for (int i : order) {
        const QString source = project.path(sources.value(i));
        if (source.endsWith(".pdf", Qt::CaseInsensitive)) {
            pieces << source;
            continue;
        }
        const QString pdf = work + QStringLiteral("/%1.pdf").arg(i);       // un'immagine: una pagina
        QFile::remove(pdf);
        if (!tools::run("sips", {"-s", "format", "pdf", source, "--out", pdf}, work, 60000).ok || !QFileInfo::exists(pdf))
            return {};
        pieces << pdf;
    }
    const QString target = files::freeName(project.path(QStringLiteral("fonti/coda/lezione %1.pdf").arg(number)));
    if (!tools::run("pdfunite", pieces + QStringList{target}, work, 120000).ok || !QFileInfo::exists(target))
        return {};
    for (const QString &source : sources)              // le fonti sono dentro il file unito: quelle in coda non servono più
        if (source.startsWith("fonti/coda/"))
            QFile::remove(project.path(source));
    return project.relative(target);
}

}
