#include "core/Provenance.h"

#include "core/Blocks.h"
#include "core/Compiler.h"
#include "core/Files.h"
#include "core/SyncTex.h"

#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>

namespace tx::provenance {

namespace {
// «appunti.pdf p.2», «dispensa.tex r.10-80», «note.md» → il nome della fonte
QString sourceName(const QString &text)
{
    static const QRegularExpression tail(R"(\s+(?:pp?\.|r\.)\s*\d.*$)");
    return QString(text).remove(tail).trimmed();
}
QString markOf(int index)
{
    return index < 26 ? QString(QChar('A' + index)) : QStringLiteral("%1%2").arg(QChar('A' + index / 26 - 1)).arg(QChar('A' + index % 26));
}
blocks::Units unitsOf(const Style &style) { return {style.boxes(), {style.figureWrapper(), "figure"}}; }
}

QList<Source> sources(const Project &project, const Style &style)
{
    QList<Source> out;
    QStringList seen;
    for (const QString &lesson : project.inputs()) {
        for (const blocks::Block &block : blocks::parse(files::read(project.path(lesson)), unitsOf(style))) {
            const QString name = sourceName(block.source);
            if (name.isEmpty() || seen.contains(name))
                continue;
            seen << name;
            QString path;
            for (QDirIterator it(project.path("fonti"), {name}, QDir::Files, QDirIterator::Subdirectories); it.hasNext() && path.isEmpty();)
                path = project.relative(it.next());
            out << Source{markOf(int(out.size())), name, path};
        }
    }
    return out;
}

QList<Tag> tags(const Project &project, const Style &style, const QString &pdf, JobControl *control)
{
    QList<Tag> out;
    QHash<QString, QString> marks;
    for (const Source &s : sources(project, style))
        marks.insert(s.name, s.mark);
    // quali lezioni stanno in questo PDF: tutte nel documento, una sola nel PDF di una lezione
    QStringList lessons = project.inputs();
    for (const QString &lesson : project.inputs())
        if (pdf == Compiler::pdfOf(project, lesson))
            lessons = {lesson};
    for (const QString &lesson : std::as_const(lessons)) {
        for (const blocks::Block &block : blocks::parse(files::read(project.path(lesson)), unitsOf(style))) {
            const QString name = sourceName(block.source);
            if (name.isEmpty())
                continue;
            if (control)
                control->check();
            synctex::Spot spot;
            for (int line = block.line; line <= qMin(block.endLine, block.line + 3) && !spot.ok; ++line)
                spot = synctex::toPdf(project.root(), pdf, lesson, line);
            if (!spot.ok)
                continue;
            const QList<int> pages = block.pages();
            out << Tag{spot.page, spot.y, marks.value(name), name, pages.isEmpty() ? 0 : pages.first(), block.id};
        }
    }
    return out;
}

}
