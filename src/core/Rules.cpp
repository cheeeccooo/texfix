#include "core/Rules.h"

#include "core/Files.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace tx::rules {

namespace {
struct Known {
    const char *id, *title;
};
const Known KNOWN[] = {
    {"scrittura", "Come si scrive"},
    {"aggiunte", "Correzioni e aggiunte"},
    {"riquadri", "Riquadri: quale e quando"},
    {"notazione", "Notazione"},
    {"rimandi", "Etichette e rimandi"},
    {"figure", "Figure"},
    {"sorgente", "Stile del sorgente"},
    {"larghezza", "Larghezza delle righe"},
};

QString g_home;

QString home()
{
    return g_home.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/regole" : g_home;
}
QString customPath(const QString &id) { return home() + '/' + id + ".md"; }
}

void setHome(const QString &dir) { g_home = dir; }

QString defaultText(const QString &id)
{
    return files::read(QStringLiteral(":/regole/editoriali/%1.md").arg(id)).trimmed();
}

Section section(const QString &id)
{
    for (const Known &known : KNOWN) {
        if (id != QLatin1String(known.id))
            continue;
        Section s{id, QString::fromUtf8(known.title), defaultText(id), false};
        bool ok = false;
        const QString custom = files::read(customPath(id), &ok).trimmed();
        if (ok && custom != s.text) {
            s.text = custom;
            s.custom = true;
        }
        return s;
    }
    return {};
}

QList<Section> sections()
{
    QList<Section> out;
    for (const Known &known : KNOWN)
        out << section(QLatin1String(known.id));
    return out;
}

bool setText(const QString &id, const QString &text)
{
    if (section(id).id.isEmpty())
        return false;
    if (text.trimmed() == defaultText(id)) {
        reset(id);
        return true;
    }
    return files::write(customPath(id), text.trimmed() + '\n');
}

void reset(const QString &id) { QFile::remove(customPath(id)); }

QString forPrompt(const QStringList &ids, const QStringList &projectRules)
{
    QStringList parts;
    for (const QString &id : ids) {
        const Section s = section(id);
        if (!s.text.isEmpty())
            parts << QStringLiteral("## %1\n\n%2").arg(s.title, s.text);
    }
    if (!projectRules.isEmpty()) {
        QString block = QStringLiteral("## Regole di questo progetto\n\nValgono più di tutte le altre.\n");
        for (const QString &rule : projectRules)
            block += QStringLiteral("\n- %1").arg(rule);
        parts << block;
    }
    return parts.join("\n\n");
}

}
