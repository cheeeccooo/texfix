#include "core/StyleSwitch.h"

#include "core/Files.h"
#include "core/Settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include "core/Model.h"

namespace tx::styleswitch {
namespace {
QString libraryHome();
}
}

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
    if (!ok)                                           // gli stili che Texfix conosce già (la sua libreria)
        out.style = Style::load(":/stili/" + Style::descriptionFile(name), &ok);
    out.described = ok;
    if (!ok)
        out.style = Style::scan(name, files::read(styPath));
    out.style.name = name;
    if (!out.style.save(project.path(Style::descriptionFile(name))))
        out.error = QStringLiteral("Non riesco a salvare la descrizione dello stile.");
    return out;
}

namespace {
// La definizione di una macro in un .sty: dal comando che la definisce fino a dove le graffe si chiudono.
QString definitionOf(const QString &sty, const QString &macro)
{
    const QRegularExpression start(R"(\\(?:newcommand|renewcommand|providecommand|NewDocumentCommand|DeclareMathOperator|DeclareRobustCommand|DeclarePairedDelimiter)\*?\s*\{?\s*)"
                                   + QRegularExpression::escape(macro) + "(?![A-Za-z@])");
    const auto m = start.match(sty);
    if (!m.hasMatch())
        return {};
    int depth = 0;
    bool opened = false;
    qsizetype i = m.capturedStart();
    for (; i < sty.size(); ++i) {
        const QChar c = sty[i];
        if (c == '\\') {
            ++i;
            continue;
        }
        if (c == '{') {
            ++depth;
            opened = true;
        } else if (c == '}') {
            --depth;
        } else if (c == '\n' && depth == 0 && opened) {
            break;                                     // la riga è finita e le graffe sono chiuse
        }
    }
    return sty.mid(m.capturedStart(), i - m.capturedStart()).trimmed();
}
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
        if (target == "=") {                          // l'utente ha scelto di lasciarlo com'è
            orphans << env.name;
            continue;
        }
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
        QString text = files::read(project.path(lesson.file));
        const QString original = text;
        // le figure passano nell'ambiente delle figure del nuovo stile: quelle con il loro segno «% img»
        if (from.figureWrapper() != to.figureWrapper()) {
            QStringList lines = text.split('\n');
            const QString begin = "\\begin{" + from.figureWrapper() + "}", end = "\\end{" + from.figureWrapper() + "}";
            bool inside = false;
            for (QString &line : lines) {
                if (!inside && line.contains(begin) && line.contains("% img")) {
                    line.replace(begin, "\\begin{" + to.figureWrapper() + "}");
                    inside = true;
                } else if (inside && line.trimmed() == end) {
                    line.replace(end, "\\end{" + to.figureWrapper() + "}");
                    inside = false;
                }
            }
            text = lines.join('\n');
        }
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
        if (result != original && files::write(project.path(lesson.file), result))
            ++out.lessons;
    }

    // le macro dello stile di prima che il nuovo non ha
    QStringList carried;
    {
        const QString oldSty = from.name.isEmpty() ? QString() : files::read(project.path(from.name + ".sty"));
        QStringList defined;
        for (const Style::Macro &m : to.macros)
            defined << m.name;
        Settings before = Settings::load(project);
        static const QRegularExpression guard(R"(^\\ifdefined(\\[A-Za-z]+)\\else )");
        for (const QString &line : std::as_const(before.carried))     // quelle portate da prima, se il nuovo stile non le ha
            if (!defined.contains(guard.match(line).captured(1)))
                carried << line;
        for (const Style::Macro &macro : from.macros) {
            if (defined.contains(macro.name))
                continue;
            const QRegularExpression use(QRegularExpression::escape(macro.name) + "(?![A-Za-z@])");
            QString same;                               // una macro del nuovo stile con lo stesso significato
            for (const Style::Macro &m : to.macros)
                if (!macro.meaning.trimmed().isEmpty() && m.meaning.trimmed().compare(macro.meaning.trimmed(), Qt::CaseInsensitive) == 0)
                    same = m.name;
            bool used = false;
            for (const Lesson &lesson : project.lessons()) {
                QString text = files::read(project.path(lesson.file));
                if (!use.match(text).hasMatch())
                    continue;
                used = true;
                if (!same.isEmpty()) {
                    text.replace(use, QString(same).replace("\\", "\\\\"));
                    files::write(project.path(lesson.file), text);
                }
            }
            if (!used)
                continue;
            if (!same.isEmpty()) {
                out.renamed << macro.name + " → " + same;
                continue;
            }
            const QString definition = definitionOf(oldSty, macro.name);
            if (definition.isEmpty())
                continue;                              // non si trova: la vedranno gli errori della lezione
            const QString guarded = QStringLiteral("\\ifdefined%1\\else %2\\fi").arg(macro.name, definition);
            if (!carried.contains(guarded))
                carried << guarded;
            out.carried << macro.name;
        }
    }

    // le opzioni della classe che lo stile vuole (due colonne…): via quelle dello stile di prima, dentro quelle del nuovo
    {
        QString main = files::read(project.path(Project::MAIN));
        static const QRegularExpression documentClass(R"(\\documentclass(?:\[([^\]]*)\])?\{)");
        if (const auto m = documentClass.match(main); m.hasMatch() && from.classOptions != to.classOptions) {
            QStringList options;
            for (const QString &o : m.captured(1).split(',', Qt::SkipEmptyParts))
                if (!from.classOptions.split(',').contains(o.trimmed()))
                    options << o.trimmed();
            for (const QString &o : to.classOptions.split(',', Qt::SkipEmptyParts))
                if (!options.contains(o.trimmed()))
                    options << o.trimmed();
            main.replace(m.capturedStart(), m.capturedLength(), options.isEmpty() ? QStringLiteral("\\documentclass{") : QStringLiteral("\\documentclass[%1]{").arg(options.join(',')));
            files::write(project.path(Project::MAIN), main);
        }
    }
    Settings settings = Settings::load(project);
    settings.style = name;
    settings.carried = carried;
    if (!settings.save(project))
        return {QStringLiteral("Non riesco a salvare le impostazioni del progetto."), out.lessons, out.unmatched};
    project.complete();                                // texfix.sty carica il nuovo stile
    return out;
}

}

namespace tx::styleswitch {

QStringList library()
{
    QStringList out;
    for (const QString &file : QDir(":/stili").entryList({"*.sty"}, QDir::Files, QDir::Name))
        out << QFileInfo(file).completeBaseName();
    for (const QString &file : QDir(libraryHome()).entryList({"*.sty"}, QDir::Files, QDir::Name))    // quelli dell'utente
        if (!out.contains(QFileInfo(file).completeBaseName()))
            out << QFileInfo(file).completeBaseName();
    return out;
}

Imported importFromLibrary(const Project &project, const QString &name)
{
    Imported out;
    const QString home = isBundled(name) ? QStringLiteral(":/stili/") : libraryHome() + '/';
    QFile sty(home + name + ".sty");
    bool ok = false;
    out.style = Style::load(home + Style::descriptionFile(name), &ok);
    if (!ok || !sty.open(QIODevice::ReadOnly))
        return {QStringLiteral("Lo stile «%1» non è nella libreria.").arg(name), {}, false};
    out.described = true;
    out.style.name = name;
    if (!files::writeBytes(project.path(name + ".sty"), sty.readAll()) || !out.style.save(project.path(Style::descriptionFile(name))))
        out.error = QStringLiteral("Non riesco a portare lo stile nel progetto.");
    return out;
}

}

namespace tx::styleswitch {

namespace {
QString g_libraryHome;
QString libraryHome()
{
    if (!g_libraryHome.isEmpty())
        return g_libraryHome;
    const QString data = qEnvironmentVariable("TEXFIX_DATI");          // per le prove dell'interfaccia: dati a parte
    return (data.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : data) + "/stili";
}
}

void setLibraryHome(const QString &dir) { g_libraryHome = dir; }

bool isBundled(const QString &name) { return QFile::exists(QStringLiteral(":/stili/%1.sty").arg(name)); }

QString addToLibrary(const QString &styPath)
{
    const QFileInfo source(styPath);
    const QString name = source.completeBaseName();
    if (!source.isFile() || source.suffix() != "sty")
        return QStringLiteral("Serve un file .sty.");
    if (name == "texfix" || isBundled(name))
        return QStringLiteral("«%1» è già uno stile di Texfix: scegli un altro nome.").arg(name);
    QDir().mkpath(libraryHome());
    const QString target = libraryHome() + '/' + name + ".sty";
    QFile::remove(target);
    if (!QFile::copy(styPath, target))
        return QStringLiteral("Non riesco a copiare lo stile nella libreria.");
    // la descrizione accanto al file, se c'è; altrimenti quella letta dal file (i ruoli si confermano quando lo si usa)
    bool ok = false;
    Style style = Style::load(source.absolutePath() + '/' + Style::descriptionFile(name), &ok);
    if (!ok)
        style = Style::scan(name, files::read(styPath));
    style.name = name;
    return style.save(libraryHome() + '/' + Style::descriptionFile(name)) ? QString() : QStringLiteral("Non riesco a salvare la descrizione dello stile.");
}

bool removeFromLibrary(const QString &name)
{
    if (isBundled(name))
        return false;
    QFile::remove(libraryHome() + '/' + Style::descriptionFile(name));
    return QFile::remove(libraryHome() + '/' + name + ".sty");
}

QList<Orphan> orphans(const Project &project, const QString &name)
{
    QList<Orphan> out;
    const Style from = current(project);
    Style to = Style::classic();
    if (!name.isEmpty()) {
        bool ok = false;
        to = Style::load(project.path(Style::descriptionFile(name)), &ok);
        if (!ok)
            return out;
    }
    QString all;
    for (const Lesson &lesson : project.lessons())
        all += files::read(project.path(lesson.file));
    QStringList options;
    for (const Style::Environment &env : to.environments)
        if (!env.role.isEmpty() && env.role != "altro")
            options << env.name;
    for (const Style::Environment &env : from.environments) {
        if (env.role.isEmpty() || env.role == "altro" || !all.contains("\\begin{" + env.name + "}"))
            continue;
        if ((to.environment(env.name) && to.environment(env.name)->role == env.role) || !to.environmentFor(env.role).isEmpty())
            continue;
        out << Orphan{env.name, env.role, options};
    }
    return out;
}

QStringList refresh(const Project &project)
{
    QStringList fresh;
    const QString name = Settings::load(project).style;
    if (name.isEmpty())
        return fresh;
    bool ok = false;
    const QString path = project.path(Style::descriptionFile(name));
    Style known = Style::load(path, &ok);
    const QString sty = files::read(project.path(name + ".sty"));
    if (!ok || sty.isEmpty())
        return fresh;
    const Style seen = Style::scan(name, sty);
    const Style before = known;
    // gli ambienti: quelli che non ci sono più escono, quelli nuovi entrano con ciò che il file dice
    for (qsizetype i = known.environments.size() - 1; i >= 0; --i)
        if (!seen.environment(known.environments[i].name))
            known.environments.removeAt(i);
    for (const Style::Environment &env : seen.environments) {
        if (known.environment(env.name))
            continue;
        known.environments << env;
        fresh << env.name;
    }
    // le macro: quelle di prima tengono il loro significato
    QList<Style::Macro> macros;
    for (const Style::Macro &macro : seen.macros) {
        Style::Macro kept = macro;
        for (const Style::Macro &old : before.macros)
            if (old.name == macro.name && !old.meaning.isEmpty())
                kept.meaning = old.meaning;
        macros << kept;
    }
    known.macros = macros;
    if (!(known == before))
        known.save(path);
    return fresh;
}

QString propose(const Project &project, const QString &quality, const QString &name, JobControl *control, double *cost)
{
    const QString path = project.path(Style::descriptionFile(name));
    bool ok = false;
    Style style = Style::load(path, &ok);
    const QString sty = files::read(project.path(name + ".sty"));
    if (!ok || sty.isEmpty())
        return QStringLiteral("Lo stile «%1» non è nel progetto.").arg(name);
    QStringList roles, environments, macros;
    for (const Style::Role &role : Style::roles())
        roles << QStringLiteral("%1 (%2)").arg(role.value, role.label);
    for (const Style::Environment &env : style.environments)
        environments << env.name;
    for (const Style::Macro &macro : style.macros)
        macros << macro.name;
    const QString system = QStringLiteral(
        "You describe a LaTeX style file (.sty) for a program that writes lecture notes with it. You get the file, the environments and the "
        "macros found in it. Return only a JSON object:\n"
        "{\"environments\": [{\"name\": \"…\", \"role\": \"<one of the roles>\", \"breaks\": true|false, \"note\": \"<Italian, short: what one must "
        "know to use it (arguments, nesting); empty if nothing>\"}],\n"
        " \"macros\": [{\"name\": \"\\\\R\", \"meaning\": \"<Italian, a few words>\"}],\n"
        " \"references\": \"cref\"|\"tag\", \"figure_environment\": \"<the environment figures go in, or empty>\", "
        "\"caption\": \"<the caption command, or empty for \\\\caption>\",\n"
        " \"conventions\": \"<Italian markdown, short: what the style asks of the writer beyond the catalogue; empty if nothing>\"}\n"
        "Roles: %1. Use «altro» for service environments that notes never use directly. \"breaks\" is false when the environment cannot "
        "split across pages. \"references\" is \"tag\" only if the style defines \\tagref. Describe only the listed environments and macros.")
                               .arg(roles.join(", "));
    Model model(quality, project.root(), control);
    QJsonObject answer;
    try {
        answer = Model::json(model.ask("outline", system,
                                       {QStringLiteral("ENVIRONMENTS: %1\nMACROS: %2\n\nTHE FILE %3.sty:\n%4")
                                            .arg(environments.join(", "), macros.join(", "), name, sty.left(60000))},
                                       "stile")).toObject();
    } catch (const Stopped &) {
        throw;
    } catch (const std::exception &e) {
        return QStringLiteral("Il modello non è riuscito a leggere lo stile (%1): i ruoli vengono dai nomi.").arg(QString::fromUtf8(e.what()));
    }
    if (cost)
        *cost = model.cost();
    QStringList valid;
    for (const Style::Role &role : Style::roles())
        valid << role.value;
    for (const QJsonValue &v : answer.value("environments").toArray()) {
        const QJsonObject o = v.toObject();
        for (Style::Environment &env : style.environments) {
            if (env.name != o.value("name").toString())
                continue;
            if (valid.contains(o.value("role").toString()))
                env.role = o.value("role").toString();
            if (o.contains("breaks"))
                env.breaks = o.value("breaks").toBool(true);
            env.note = o.value("note").toString().trimmed();
        }
    }
    for (const QJsonValue &v : answer.value("macros").toArray())
        for (Style::Macro &macro : style.macros)
            if (macro.name == v.toObject().value("name").toString())
                macro.meaning = v.toObject().value("meaning").toString().trimmed();
    if (answer.value("references").toString() == "tag" || answer.value("references").toString() == "cref")
        style.references = answer.value("references").toString();
    if (style.environment(answer.value("figure_environment").toString()))
        style.figureEnvironment = answer.value("figure_environment").toString();
    if (answer.value("caption").toString().startsWith('\\'))
        style.captionCommand = answer.value("caption").toString();
    if (style.conventions.isEmpty())
        style.conventions = answer.value("conventions").toString().trimmed();
    return style.save(path) ? QString() : QStringLiteral("Non riesco a salvare la descrizione dello stile.");
}

}
