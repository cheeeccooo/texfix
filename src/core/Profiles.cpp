#include "core/Profiles.h"

#include "core/Files.h"
#include "core/Settings.h"
#include "core/Style.h"
#include "core/StyleSwitch.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
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
QString dirOf(const QString &name) { return home() + '/' + name; }
}

void setHome(const QString &dir) { g_home = dir; }

QStringList list() { return QDir(home()).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase); }

QString save(const Project &project, const QString &name)
{
    const QString clean = name.trimmed();
    if (clean.isEmpty() || clean.contains('/') || clean.startsWith('.'))
        return QStringLiteral("Serve un nome per il profilo, senza barre.");
    Settings settings = Settings::load(project);
    settings.done.clear();                             // ciò che riguarda le lezioni di questo progetto non è una scelta da riusare
    settings.topics = {};
    const QString dir = dirOf(clean);
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
    if (!settings.style.isEmpty()) {
        for (const QString &file : {settings.style + ".sty", Style::descriptionFile(settings.style)})
            if (!QFile::copy(project.path(file), dir + '/' + file))
                return QStringLiteral("Non riesco a copiare lo stile nel profilo.");
    }
    // le impostazioni nella stessa forma del progetto: un profilo si legge come un progetto senza lezioni
    return settings.save(Project(dir)) ? QString() : QStringLiteral("Non riesco a salvare il profilo.");
}

QString apply(const Project &project, const QString &name)
{
    const QString dir = dirOf(name);
    if (!QFileInfo::exists(dir + '/' + Project::STATE))
        return QStringLiteral("Il profilo «%1» non c'è più.").arg(name);
    Settings chosen = Settings::load(Project(dir));
    if (!chosen.style.isEmpty()) {
        for (const QString &file : {chosen.style + ".sty", Style::descriptionFile(chosen.style)}) {
            QFile::remove(project.path(file));
            if (!QFile::copy(dir + '/' + file, project.path(file)))
                return QStringLiteral("Non riesco a portare lo stile nel progetto.");
        }
    }
    const QString style = chosen.style;
    Settings settings = Settings::load(project);       // le lezioni fatte e ciò che è del progetto restano
    chosen.done = settings.done;
    chosen.topics = settings.topics;
    chosen.style = settings.style;                     // lo stile lo cambia chi sa riscrivere le lezioni
    if (!chosen.save(project))
        return QStringLiteral("Non riesco a scrivere le impostazioni del progetto.");
    return styleswitch::apply(project, style).error;
}

void remove(const QString &name)
{
    if (!name.trimmed().isEmpty() && !name.contains('/'))
        QDir(dirOf(name)).removeRecursively();
}

}
