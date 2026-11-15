#include "core/Settings.h"

#include "core/Files.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace tx {

namespace {
QString Settings::*member(const QString &key)
{
    if (key == "scrittura")
        return &Settings::writing;
    if (key == "qualita")
        return &Settings::quality;
    if (key == "figure_scansioni")
        return &Settings::scanFigures;
    if (key == "figure_tex")
        return &Settings::texFigures;
    if (key == "impaginazione")
        return &Settings::layout;
    return nullptr;
}
const char *const KEYS[] = {"scrittura", "qualita", "figure_scansioni", "figure_tex", "impaginazione"};
}

QList<Settings::Choice> Settings::choices(const QString &key)
{
    if (key == "scrittura")
        return {{"studio", "Da studiare"}, {"fedele", "Fedele"}};
    if (key == "qualita")
        return {{"veloce", "Veloce"}, {"bilanciata", "Bilanciata"}, {"massima", "Massima"}};
    if (key == "figure_scansioni")
        return {{"ridisegna", "Ridisegna"}, {"ritaglia", "Ritaglia dalla fonte"}};
    if (key == "figure_tex")
        return {{"tieni", "Tieni il codice"}, {"ridisegna", "Ridisegna"}};
    if (key == "impaginazione")
        return {{"da_sola", "Da sola"}, {"a_richiesta", "Quando lo chiedo"}};
    return {};
}

QString Settings::value(const QString &key) const
{
    const auto m = member(key);
    return m ? this->*m : QString();
}

bool Settings::setValue(const QString &key, const QString &value)
{
    const auto m = member(key);
    if (!m)
        return false;
    for (const Choice &choice : choices(key)) {
        if (choice.value == value) {
            this->*m = value;
            return true;
        }
    }
    return false;
}

void Settings::setDone(const QString &lesson, bool on)
{
    if (on)
        done.insert(lesson, QDateTime::currentDateTime().toString(Qt::ISODate));
    else
        done.remove(lesson);
}

Settings Settings::load(const Project &project)
{
    Settings s;
    const QJsonObject root = QJsonDocument::fromJson(files::read(project.path(Project::STATE)).toUtf8()).object();
    const QJsonObject choices = root.value("impostazioni").toObject();
    for (const char *key : KEYS)
        s.setValue(key, choices.value(QLatin1String(key)).toString());   // un valore sconosciuto lascia il predefinito
    for (const QJsonValue &rule : root.value("regole").toArray())
        if (!rule.toString().trimmed().isEmpty())
            s.rules << rule.toString().trimmed();
    s.style = root.value("stile").toString();
    s.topics = root.value("argomenti").toObject();
    for (const QJsonValue &name : root.value("pacchetti").toArray())
        s.packages << name.toString();
    const QJsonObject done = root.value("fatte").toObject();
    for (auto it = done.begin(); it != done.end(); ++it)
        s.done.insert(it.key(), it.value().toString());
    return s;
}

bool Settings::save(const Project &project) const
{
    QJsonObject choices, doneObject;
    for (const char *key : KEYS)
        choices.insert(QLatin1String(key), value(key));
    for (auto it = done.begin(); it != done.end(); ++it)
        doneObject.insert(it.key(), it.value());
    QJsonObject root{{"formato", Project::FORMAT}, {"impostazioni", choices}, {"regole", QJsonArray::fromStringList(rules)}, {"fatte", doneObject}};
    if (!style.isEmpty())
        root.insert("stile", style);
    if (!topics.isEmpty())
        root.insert("argomenti", topics);
    if (!packages.isEmpty())
        root.insert("pacchetti", QJsonArray::fromStringList(packages));
    return files::write(project.path(Project::STATE), QString::fromUtf8(QJsonDocument(root).toJson()));
}

}
