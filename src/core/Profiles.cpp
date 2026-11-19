#include "core/Profiles.h"

#include "core/Files.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

namespace tx::profiles {

namespace {
QString g_home;
QString home()
{
    if (!g_home.isEmpty())
        return g_home;
    const QString data = qEnvironmentVariable("TEXFIX_DATI");          // per le prove dell'interfaccia: dati a parte
    return (data.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : data) + "/profili";
}
const char *const BUILTIN[] = {"neutro", "scientifico", "umanistico"};
constexpr const char *SUFFIX = ".profilo.md";

// Il nome di un file della libreria: il nome del profilo senza ciò che un nome di file non può avere.
QString fileOf(const QString &name)
{
    QString clean = name.trimmed();
    clean.replace(QRegularExpression(R"([/\\:*?"<>|])"), "-");
    return home() + '/' + clean + SUFFIX;
}
QString builtinFile(const QString &name)
{
    for (const char *id : BUILTIN)
        if (rules::Profile::parse(files::read(QStringLiteral(":/profili/%1%2").arg(QLatin1String(id), QLatin1String(SUFFIX)))).name.compare(name, Qt::CaseInsensitive) == 0)
            return QStringLiteral(":/profili/%1%2").arg(QLatin1String(id), QLatin1String(SUFFIX));
    return {};
}
bool taken(const QString &name)
{
    for (const QString &other : builtin() + library())
        if (other.compare(name.trimmed(), Qt::CaseInsensitive) == 0)
            return true;
    return false;
}
QString checkName(const QString &name)
{
    if (name.trimmed().isEmpty() || name.trimmed().startsWith('.'))
        return QStringLiteral("Serve un nome per il profilo.");
    return {};
}
}

void setHome(const QString &dir) { g_home = dir; }

QStringList builtin()
{
    QStringList out;
    for (const char *id : BUILTIN)
        out << rules::Profile::parse(files::read(QStringLiteral(":/profili/%1%2").arg(QLatin1String(id), QLatin1String(SUFFIX)))).name;
    return out;
}

QStringList library()
{
    QStringList out;
    for (const QString &file : QDir(home()).entryList({QStringLiteral("*") + SUFFIX}, QDir::Files, QDir::Name | QDir::IgnoreCase)) {
        bool ok = false;
        const rules::Profile p = rules::Profile::parse(files::read(home() + '/' + file), &ok);
        out << (ok ? p.name : file.chopped(int(qstrlen(SUFFIX))));
    }
    return out;
}

bool exists(const QString &name) { return taken(name); }

rules::Profile load(const QString &name, bool *ok)
{
    QString file = builtinFile(name);
    if (file.isEmpty())
        for (const QString &f : QDir(home()).entryList({QStringLiteral("*") + SUFFIX}, QDir::Files))
            if (rules::Profile::parse(files::read(home() + '/' + f)).name.compare(name, Qt::CaseInsensitive) == 0)
                file = home() + '/' + f;
    bool read = false;
    rules::Profile p = rules::Profile::parse(file.isEmpty() ? QString() : files::read(file), &read);
    if (ok)
        *ok = read;
    return p;
}

rules::Profile ofProject(const Project &project) { return rules::Profile::parse(files::read(project.path(Project::PROFILE))); }

QString setForProject(const Project &project, const QString &name)
{
    bool ok = false;
    const rules::Profile p = load(name, &ok);
    if (!ok)
        return QStringLiteral("Il profilo «%1» non c'è.").arg(name);
    return saveToProject(project, p);
}

QString saveToProject(const Project &project, const rules::Profile &profile)
{
    if (profile.name.trimmed().isEmpty())
        return QStringLiteral("Un profilo ha bisogno di un nome.");
    QDir().mkpath(project.path("salvataggi"));
    return files::write(project.path(Project::PROFILE), profile.markdown()) ? QString() : QStringLiteral("Non riesco a scrivere il profilo del progetto.");
}

QString saveToLibrary(const Project &project, const QString &name, bool replace)
{
    if (const QString error = checkName(name); !error.isEmpty())
        return error;
    if (!builtinFile(name).isEmpty())
        return QStringLiteral("«%1» è un profilo di Texfix: scegli un altro nome.").arg(name.trimmed());
    if (taken(name) && !replace)
        return QStringLiteral("C'è già un profilo «%1»: scegli un altro nome.").arg(name.trimmed());
    rules::Profile p = ofProject(project);
    p.name = name.trimmed();
    QDir().mkpath(home());
    if (replace)                                       // il file di prima, anche se aveva un nome scritto diverso
        for (const QString &f : QDir(home()).entryList({QStringLiteral("*") + SUFFIX}, QDir::Files))
            if (rules::Profile::parse(files::read(home() + '/' + f)).name.compare(p.name, Qt::CaseInsensitive) == 0)
                QFile::remove(home() + '/' + f);
    if (!files::write(fileOf(p.name), p.markdown()))
        return QStringLiteral("Non riesco a salvare il profilo nella libreria.");
    return saveToProject(project, p);
}

QString remove(const QString &name)
{
    if (!builtinFile(name).isEmpty())
        return QStringLiteral("I profili di Texfix non si tolgono.");
    for (const QString &f : QDir(home()).entryList({QStringLiteral("*") + SUFFIX}, QDir::Files))
        if (rules::Profile::parse(files::read(home() + '/' + f)).name.compare(name, Qt::CaseInsensitive) == 0)
            return QFile::remove(home() + '/' + f) ? QString() : QStringLiteral("Non riesco a togliere il profilo.");
    return QStringLiteral("Il profilo «%1» non c'è.").arg(name);
}

QString importFile(const QString &path)
{
    bool read = false;
    const QString text = files::read(path, &read);
    if (!read)
        return QStringLiteral("Non riesco a leggere %1.").arg(QFileInfo(path).fileName());
    bool ok = false;
    rules::Profile p = rules::Profile::parse(text, &ok);
    if (!ok)
        p.name = QFileInfo(path).fileName().section('.', 0, 0);
    if (p.sections.isEmpty() && p.extra.isEmpty())
        return QStringLiteral("%1 non è un profilo di Texfix.").arg(QFileInfo(path).fileName());
    if (taken(p.name))
        return QStringLiteral("C'è già un profilo «%1»: rinomina il file o il titolo dentro.").arg(p.name);
    QDir().mkpath(home());
    return files::write(fileOf(p.name), p.markdown()) ? QString() : QStringLiteral("Non riesco a salvare il profilo nella libreria.");
}

QString exportFile(const Project &project, const QString &path)
{
    const rules::Profile p = ofProject(project);
    if (p.name.isEmpty())
        return QStringLiteral("Il progetto non ha un profilo.");
    return files::write(path, p.markdown()) ? QString() : QStringLiteral("Non riesco a scrivere %1.").arg(QFileInfo(path).fileName());
}

void keepMine(const Project &project)
{
    const rules::Profile p = ofProject(project);
    if (p.name == QLatin1String("myprofile") && !taken(p.name)) {
        QDir().mkpath(home());
        files::write(fileOf(p.name), rules::Profile::parse(files::read(":/profili/myprofile.profilo.md")).markdown());
    }
}

}
