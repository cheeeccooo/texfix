#include "core/NewLesson.h"

#include "core/Blocks.h"
#include "core/Files.h"
#include "core/Pages.h"

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
        const QString from = project.path(source);
        if (!source.startsWith("fonti/coda/") || !QFileInfo::exists(from))
            continue;
        QDir().mkpath(used);
        if (QFile::rename(from, files::freeName(used + '/' + QFileInfo(source).fileName())))
            did << QStringLiteral("%1 portata fra le fonti usate").arg(QFileInfo(source).fileName());
    }
    return did.isEmpty() ? QStringList{QStringLiteral("era già tutto a posto")} : did;
}

QString fuseMessage(const QString &file, int number, const QString &date, const QStringList &sources)
{
    return QStringLiteral("Crea la lezione %1%2 fondendo queste fonti, che parlano della stessa lezione: %3.\n"
                          "- Leggile tutte per intero. Non c'è una fonte che comanda: la struttura la dà la più ricca, e dove due fonti dicono cose "
                          "diverse non scegliere tu: fanne una domanda del piano.\n"
                          "- Prima consegna un piano con consegna_piano: che cosa prendi da ogni fonte, in che ordine, le domande.\n"
                          "- Quando il piano è approvato scrivi `%4` (comincia con `\\section{…}` e sotto `\\lezione{%1}{%5}`), compilala finché è pulita, "
                          "poi chiama chiudi_lezione con la lezione e le fonti, e consegna l'esito.")
        .arg(number).arg(date.isEmpty() ? QString() : " del " + date, sources.join(", "), file, date);
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

}
