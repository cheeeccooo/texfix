#include "core/StyleSwitch.h"

#include "core/Files.h"
#include "core/Settings.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>

namespace tx::styleswitch {

Style current(const Project &project)
{
    const QString name = Settings::load(project).style;
    if (name.isEmpty())
        return Style::classic();
    bool ok = false;
    const Style style = Style::load(project.path(Style::descriptionFile(name)), &ok);
    return ok ? style : Style::classic();
}

Imported import(const Project &project, const QString &styPath)
{
    Imported out;
    const QFileInfo source(styPath);
    const QString name = source.completeBaseName();
    if (!source.isFile() || source.suffix() != "sty")
        return {QStringLiteral("Serve un file .sty."), {}, false};
    if (name == "texfix")
        return {QStringLiteral("«texfix.sty» è il file di Texfix: scegli un altro stile."), {}, false};

    const QString target = project.path(name + ".sty");
    if (QFileInfo(target).absoluteFilePath() != source.absoluteFilePath()) {
        QFile::remove(target);
        if (!QFile::copy(styPath, target))
            return {QStringLiteral("Non riesco a copiare lo stile nel progetto."), {}, false};
    }
    // uno stile già descritto porta la sua descrizione accanto: niente domande
    const QString described = source.absolutePath() + '/' + Style::descriptionFile(name);
    bool ok = false;
    out.style = Style::load(described, &ok);
    out.described = ok;
    if (!ok)
        out.style = Style::scan(name, files::read(styPath));
    out.style.name = name;
    if (!out.style.save(project.path(Style::descriptionFile(name))))
        out.error = QStringLiteral("Non riesco a salvare la descrizione dello stile.");
    return out;
}

Outcome apply(const Project &project, const QString &name, const QHash<QString, QString> &choices)
{
    Outcome out;
    const Style from = current(project);
    Style to = Style::classic();
    if (!name.isEmpty()) {
        bool ok = false;
        to = Style::load(project.path(Style::descriptionFile(name)), &ok);
        if (!ok || !QFileInfo::exists(project.path(name + ".sty")))
            return {QStringLiteral("Lo stile «%1» non è nel progetto: importalo prima.").arg(name), 0, {}};
    }

    // ambiente di prima → ambiente del nuovo stile, secondo i ruoli
    QHash<QString, QString> map;
    QStringList orphans;
    for (const Style::Environment &env : from.environments) {
        QString target = choices.value(env.name);
        if (target.isEmpty() && to.environment(env.name) && to.environment(env.name)->role == env.role)
            target = env.name;                          // c'è uguale anche nel nuovo
        if (target.isEmpty() && !env.role.isEmpty() && env.role != "altro")
            target = to.environmentFor(env.role);
        if (target.isEmpty())
            orphans << env.name;
        else if (target != env.name)
            map.insert(env.name, target);
    }

    static const QRegularExpression environment(R"(\\(begin|end)(\s*)\{([A-Za-z]+\*?)\})");
    for (const Lesson &lesson : project.lessons()) {
        const QString text = files::read(project.path(lesson.file));
        QString result;
        qsizetype last = 0;
        for (auto it = environment.globalMatch(text); it.hasNext();) {
            const auto m = it.next();
            const QString env = m.captured(3);
            if (orphans.contains(env) && !out.unmatched.contains(env))
                out.unmatched << env;
            const QString target = map.value(env);
            if (target.isEmpty())
                continue;
            result += text.mid(last, m.capturedStart() - last) + QStringLiteral("\\%1%2{%3}").arg(m.captured(1), m.captured(2), target);
            last = m.capturedEnd();
        }
        result += text.mid(last);
        if (from.references != to.references)           // i rimandi nella forma del nuovo stile
            result.replace(from.references == "tag" ? QLatin1String("\\tagref{") : QLatin1String("\\cref{"),
                           to.references == "tag" ? QLatin1String("\\tagref{") : QLatin1String("\\cref{"));
        if (result != text && files::write(project.path(lesson.file), result))
            ++out.lessons;
    }

    Settings settings = Settings::load(project);
    settings.style = name;
    if (!settings.save(project))
        return {QStringLiteral("Non riesco a salvare le impostazioni del progetto."), out.lessons, out.unmatched};
    project.complete();                                // texfix.sty carica il nuovo stile
    return out;
}

}
