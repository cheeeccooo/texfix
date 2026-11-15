#include "core/Git.h"

#include "core/Tools.h"

#include <QFileInfo>

namespace tx::git {

namespace {
tools::Output run(const Project &project, const QStringList &arguments) { return tools::run("git", arguments, project.root(), 30000); }
}

bool hasRepository(const Project &project) { return QFileInfo::exists(project.path(".git")); }

bool init(const Project &project) { return run(project, {"init", "--quiet"}).ok; }

QList<Change> changes(const Project &project)
{
    QList<Change> out;
    if (!hasRepository(project))
        return out;
    const QString text = run(project, {"-c", "core.quotepath=off", "status", "--porcelain", "--untracked-files=all"}).text;
    for (const QString &line : text.split('\n', Qt::SkipEmptyParts)) {
        if (line.size() < 4)
            continue;
        const QString code = line.left(2);
        QString file = line.mid(3);
        if (file.startsWith('"') && file.endsWith('"'))
            file = file.mid(1, file.size() - 2);
        const QString state = code.contains('?') || code.contains('A') ? QStringLiteral("nuovo") : code.contains('D') ? QStringLiteral("tolto")
                              : code.contains('R') ? QStringLiteral("rinominato") : QStringLiteral("modificato");
        out << Change{file.section(" -> ", -1), state};
    }
    return out;
}

QString diff(const Project &project, const QString &file)
{
    const QString tracked = run(project, {"diff", "HEAD", "--", file}).text;
    if (!tracked.isEmpty())
        return tracked;
    // un file nuovo non è ancora seguito: lo si mostra tutto come aggiunto
    return tools::run("git", {"diff", "--no-index", "--", "/dev/null", file}, project.root(), 30000).text;
}

QString show(const Project &project, const QString &hash) { return run(project, {"show", "--stat", "--patch", "--format=%h  %ad  %s%n", "--date=format:%d/%m/%Y %H:%M", hash}).text; }

QString commit(const Project &project, const QString &message, const QStringList &files)
{
    if (message.trimmed().isEmpty())
        return QStringLiteral("Scrivi che cosa hai cambiato.");
    if (!files.isEmpty())
        run(project, {"reset", "--quiet"});         // solo i file scelti: niente altro resta pronto per il commit
    if (!run(project, files.isEmpty() ? QStringList{"add", "--all"} : QStringList{"add", "--all", "--"} + files).ok)
        return QStringLiteral("Non riesco a preparare il commit.");
    if (!run(project, {"commit", "--quiet", "-m", message.trimmed()}).ok)
        return QStringLiteral("Il commit non è riuscito (non c'è niente da salvare, o manca nome ed email di git).");
    return {};
}

QList<Entry> log(const Project &project, int limit)
{
    QList<Entry> out;
    const QString text = run(project, {"log", QStringLiteral("-%1").arg(limit), "--format=%h%x09%ad%x09%s", "--date=format:%d/%m/%Y %H:%M"}).text;
    for (const QString &line : text.split('\n', Qt::SkipEmptyParts))
        out << Entry{line.section('\t', 0, 0), line.section('\t', 1, 1), line.section('\t', 2)};
    return out;
}

}
