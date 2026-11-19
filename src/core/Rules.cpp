#include "core/Rules.h"

#include "core/Files.h"

#include <QRegularExpression>

namespace tx::rules {

namespace {
const Heading STRUCTURAL[] = {
    {"aggiunte", "Correzioni e aggiunte"},
    {"rimandi", "Etichette e rimandi"},
    {"figure", "Figure: dove e come"},
    {"sorgente", "Stile del sorgente"},
    {"larghezza", "Larghezza delle righe"},
    {"impaginazione", "Impaginazione"},
};
}

QList<Section> structural()
{
    QList<Section> out;
    for (const Heading &h : STRUCTURAL)
        out << Section{QString::fromUtf8(h.id), QString::fromUtf8(h.title),
                       files::read(QStringLiteral(":/regole/strutturali/%1.md").arg(QString::fromUtf8(h.id))).trimmed()};
    return out;
}

Section structuralSection(const QString &id)
{
    for (const Section &s : structural())
        if (s.id == id)
            return s;
    return {};
}

const QList<Heading> &profileSections()
{
    static const QList<Heading> sections{{"scrittura", "Come si scrive"}, {"riquadri", "Riquadri"}, {"controlli", "Controlli"},
                                         {"notazione", "Notazione"},      {"figure", "Figure"}};
    return sections;
}

QStringList allIds()
{
    return {"scrittura", "controlli", "aggiunte", "riquadri", "notazione", "rimandi", "figure", "sorgente", "larghezza", "impaginazione"};
}

Profile Profile::parse(const QString &markdown, bool *ok)
{
    Profile p;
    static const QRegularExpression title(R"(^#\s+(.+?)\s*$)"), heading(R"(^##\s+(.+?)\s*$)"), rule(R"(^\s*[-*]\s+(.+?)\s*$)");
    QString current;                                   // l'id della sezione che si sta leggendo; «+» le regole in più
    QStringList lines;
    auto close = [&] {
        if (!current.isEmpty() && current != "+")
            p.sections.insert(current, lines.join('\n').trimmed());
        lines.clear();
    };
    for (const QString &line : markdown.split('\n')) {
        if (const auto m = heading.match(line); m.hasMatch()) {
            close();
            current = "?";                             // una sezione che il profilo non conosce: si salta
            const QString name = m.captured(1);
            if (name.compare(QStringLiteral("Regole in più"), Qt::CaseInsensitive) == 0)
                current = "+";
            for (const Heading &h : profileSections())
                if (name.compare(QString::fromUtf8(h.title), Qt::CaseInsensitive) == 0)
                    current = QString::fromUtf8(h.id);
            continue;
        }
        if (const auto m = title.match(line); m.hasMatch() && current.isEmpty() && p.name.isEmpty()) {
            p.name = m.captured(1);
            continue;
        }
        if (current == "+") {
            if (const auto m = rule.match(line); m.hasMatch())
                p.extra << m.captured(1);
        } else if (!current.isEmpty() && current != "?") {
            lines << line;
        }
    }
    close();
    p.sections.remove("?");
    if (ok)
        *ok = !p.name.isEmpty();
    return p;
}

QString Profile::markdown() const
{
    QStringList out{QStringLiteral("# %1").arg(name), QString()};
    for (const Heading &h : profileSections())
        out << QStringLiteral("## %1").arg(QString::fromUtf8(h.title)) << QString() << sections.value(QString::fromUtf8(h.id)).trimmed() << QString();
    out << QStringLiteral("## Regole in più") << QString();
    for (const QString &r : extra)
        out << QStringLiteral("- %1").arg(r);
    return out.join('\n').trimmed() + '\n';
}

QString forPrompt(const QStringList &ids, const Profile &profile)
{
    QStringList parts;
    for (const QString &id : ids) {
        if (const Section s = structuralSection(id); !s.text.isEmpty())
            parts << QStringLiteral("## %1\n\n%2").arg(s.title, s.text);
        for (const Heading &h : profileSections())
            if (id == QLatin1String(h.id) && !profile.sections.value(id).trimmed().isEmpty())
                parts << QStringLiteral("## %1\n\n%2").arg(QString::fromUtf8(h.title), profile.sections.value(id).trimmed());
    }
    if (!profile.extra.isEmpty()) {
        QString block = QStringLiteral("## Regole di questo corso\n\nValgono più di tutte le altre.\n");
        for (const QString &rule : profile.extra)
            block += QStringLiteral("\n- %1").arg(rule);
        parts << block;
    }
    return parts.join("\n\n");
}

}
