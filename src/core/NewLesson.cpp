#include "core/NewLesson.h"

#include "core/Blocks.h"
#include "core/Files.h"
#include "core/Pages.h"
#include "core/Reorganize.h"
#include "core/TexSource.h"
#include "core/Tools.h"
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <QDate>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

using RE = QRegularExpression;

namespace tx::newlesson {

int nextNumber(const Project &project)
{
    // ogni \lezione di ogni file: in un progetto per argomento un file ne ha più d'una
    static const RE mark(R"(\\lezione\s*\{\s*(\d+)\s*\})");
    int last = 0;
    for (const QString &file : project.inputs())
        for (auto it = mark.globalMatch(files::read(project.path(file))); it.hasNext();)
            last = qMax(last, it.next().captured(1).toInt());
    return last + 1;
}

int lastFile(const Project &project)
{
    static const RE numbered(R"(^(?:src|arg)(\d+)$)");
    QStringList names = QDir(project.path("src")).entryList({"*.tex"}, QDir::Files);
    for (const QJsonValue &v : Settings::load(project).topics.value("lezioni").toObject())
        names << QFileInfo(v.toObject().value("file").toString()).fileName();
    int last = 0;
    for (const QString &name : std::as_const(names))
        last = qMax(last, numbered.match(QFileInfo(name).completeBaseName()).captured(1).toInt());
    return last;
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
                did << QStringLiteral("%1 resta in arrivo: ha altre lezioni").arg(name);
                continue;
            }
            if (lectures.size() > 1)
                to = project.path("fonti/usate/più lezioni");
        }
        QDir().mkpath(to);
        if (QFile::rename(from, files::freeName(to + '/' + name)))
            did << QStringLiteral("%1 portata fra le fonti usate").arg(name);
    }
    // per argomento: la lezione va dove l'ha messa il piano, come quelle della conversione
    QJsonObject saved = QJsonDocument::fromJson(files::read(project.path(PROCEDURES)).toUtf8()).object();
    QJsonObject placements = saved.value("collocazioni").toObject();
    if (const QJsonObject where = placements.value(lesson).toObject(); !where.isEmpty()) {
        const QString error = reorganize::place(project, lesson, where.value("argomento").toString(), where.value("titolo").toString(), where.value("dopo").toString());
        if (!error.isEmpty())
            return did << QStringLiteral("la lezione resta a sé: %1").arg(error);
        did << (where.value("argomento").toString().isEmpty() ? QStringLiteral("la lezione è diventata l'argomento «%1»").arg(where.value("titolo").toString())
                                                               : QStringLiteral("la lezione è andata in fondo a %1").arg(where.value("argomento").toString()));
        placements.remove(lesson);
        saved["collocazioni"] = placements;
        files::write(project.path(PROCEDURES), QString::fromUtf8(QJsonDocument(saved).toJson()));
    }
    return did.isEmpty() ? QStringList{QStringLiteral("era già tutto a posto")} : did;
}

void addPlacements(const Project &project, const QJsonObject &added)
{
    if (added.isEmpty())
        return;
    QJsonObject saved = QJsonDocument::fromJson(files::read(project.path(PROCEDURES)).toUtf8()).object();
    QJsonObject placements = saved.value("collocazioni").toObject();
    for (auto it = added.begin(); it != added.end(); ++it)
        placements.insert(it.key(), it.value());
    saved["collocazioni"] = placements;
    files::write(project.path(PROCEDURES), QString::fromUtf8(QJsonDocument(saved).toJson()));
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

QString fuseMessage(const QString &file, int number, const QString &date, const QStringList &sources, const QStringList &main)
{
    const QString weight = main.isEmpty() ? QString()
        : QStringLiteral("\n- Fonti principali: %1. La struttura la danno loro, e dove le fonti dicono cose diverse decidono loro, senza farne una "
                         "domanda; le altre sono integrative: completano, non riordinano.").arg(main.join(", "));
    return QStringLiteral("Crea la lezione %1%2 fondendo queste fonti, che parlano della stessa lezione: %3.\n"
                          "- Leggile tutte per intero. Se non è detto altro sotto, non c'è una fonte che comanda: la struttura la dà la più ricca, e "
                          "dove due fonti dicono cose diverse scegli la più fedele e dillo nell'esito.\n"
                          "- Scrivi `%4` (comincia con `\\section{…}` e sotto `\\lezione{%1}{%5}`), compilala finché è pulita, poi chiama "
                          "chiudi_lezione con la lezione e le fonti.")
        .arg(number).arg(date.isEmpty() ? QString() : " del " + date, sources.join(", "), file, date) + weight;
}

QString integrateMessage(const QString &lesson, const QStringList &sources)
{
    return QStringLiteral("Integra in `%1` queste fonti nuove: %2.\n"
                          "- Leggi la lezione e le fonti. Porta nella lezione solo ciò che manca (o le correzioni), vicino al blocco a cui appartiene; "
                          "ciò che c'è già non si riscrive.\n"
                          "- Fai le aggiunte, compila finché è pulita, poi chiama chiudi_lezione con la lezione e le fonti.")
        .arg(lesson, sources.join(", "));
}


Version versionOf(const Project &project, const QString &path)
{
    Version out;
    const QFileInfo info(path);
    if (!info.isFile() || info.absoluteFilePath().startsWith(project.path("fonti/usate/")))
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
    if (QFileInfo(path).absoluteFilePath().startsWith(project.path("fonti/coda/")))
        return project.relative(QFileInfo(path).absoluteFilePath());    // già fra quelle in arrivo: resta lei
    QDir().mkpath(project.path("fonti/coda"));
    const QString copy = files::freeName(project.path("fonti/coda/" + QFileInfo(old).fileName()));
    return QFile::copy(path, copy) ? project.relative(copy) : QString();
}

QString updateMessage(const QString &lesson, const QString &source, const QString &previous)
{
    return QStringLiteral("La fonte %1 è una versione nuova di una fonte già usata in `%2` (la vecchia è in `%3`).\n"
                          "- Confronta la versione nuova con la lezione: porta nella lezione le parti nuove e le correzioni, vicino al blocco a cui "
                          "appartengono; ciò che non è cambiato non si riscrive.\n"
                          "- Fai le modifiche, compila finché è pulita, poi chiama chiudi_lezione con la lezione e la fonte.")
        .arg(source, lesson, previous);
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
