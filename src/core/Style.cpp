#include "core/Style.h"

#include "core/Files.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

using RE = QRegularExpression;

namespace tx {

QList<Style::Role> Style::roles()
{
    return {{"definizione", "Definizione"},   {"assioma", "Assioma"},           {"teorema", "Teorema"},
            {"proposizione", "Proposizione"}, {"lemma", "Lemma"},               {"corollario", "Corollario"},
            {"dimostrazione", "Dimostrazione"}, {"osservazione", "Osservazione"}, {"esempio", "Esempio"},
            {"esercizio", "Esercizio"},       {"attenzione", "Avvertenza"},     {"formula", "Formula in evidenza"},
            {"riquadro", "Riquadro generico"}, {"altro", "Niente di questo"}};
}

Style Style::classic()
{
    Style s;
    auto env = [&](const char *name, const char *role, bool title = true) {
        s.environments << Environment{name, role, title ? "opzionale" : "nessuno", {}, true, {}};
    };
    env("definition", "definizione");
    env("theorem", "teorema");
    env("proposition", "proposizione");
    env("lemma", "lemma");
    env("corollary", "corollario");
    env("proof", "dimostrazione", false);
    env("remark", "osservazione");
    env("example", "esempio");
    env("exercise", "esercizio");
    s.conventions = "Il documento è un classico `article` con `amsthm`: nessun colore, nessun riquadro. Un'avvertenza è "
                    "un'osservazione (`remark`) che comincia con «Attenzione:». Le formule in evidenza restano formule in "
                    "display, senza cornice.";
    return s;
}

Style Style::load(const QString &path, bool *ok)
{
    bool read = false;
    const QJsonObject root = QJsonDocument::fromJson(files::read(path, &read).toUtf8()).object();
    if (ok)
        *ok = read && !root.isEmpty();
    Style s;
    s.name = root.value("nome").toString();
    for (const QJsonValue &v : root.value("ambienti").toArray()) {
        const QJsonObject o = v.toObject();
        s.environments << Environment{o.value("nome").toString(),          o.value("ruolo").toString(),
                                      o.value("titolo").toString("opzionale"), o.value("colore").toString(),
                                      o.value("si_spezza").toBool(true),    o.value("nota").toString()};
    }
    for (const QJsonValue &v : root.value("macro").toArray())
        s.macros << Macro{v.toObject().value("nome").toString(), v.toObject().value("significato").toString()};
    s.references = root.value("rimandi").toString("cref");
    s.figureEnvironment = root.value("ambiente_figura").toString();
    s.captionCommand = root.value("didascalia").toString();
    s.conventions = root.value("convenzioni").toString();
    s.classOptions = root.value("opzioni_classe").toString();
    return s;
}

bool Style::save(const QString &path) const
{
    QJsonArray envs, macroArray;
    for (const Environment &e : environments)
        envs << QJsonObject{{"nome", e.name}, {"ruolo", e.role}, {"titolo", e.title}, {"colore", e.color}, {"si_spezza", e.breaks}, {"nota", e.note}};
    for (const Macro &m : macros)
        macroArray << QJsonObject{{"nome", m.name}, {"significato", m.meaning}};
    const QJsonObject root{{"formato", 1},
                           {"nome", name},
                           {"ambienti", envs},
                           {"macro", macroArray},
                           {"rimandi", references},
                           {"ambiente_figura", figureEnvironment},
                           {"didascalia", captionCommand},
                           {"convenzioni", conventions},
                           {"opzioni_classe", classOptions}};
    return files::write(path, QString::fromUtf8(QJsonDocument(root).toJson()));
}

namespace {
// Il ruolo che il nome di un ambiente lascia capire; vuoto se non si capisce.
QString roleFromName(const QString &name)
{
    const QString n = name.toLower();
    struct Hint {
        const char *pattern, *role;
    };
    static const Hint HINTS[] = {
        {"^(def|definition|definizione)", "definizione"},
        {"^(axiom|assioma)", "assioma"},
        {"^(thm|theorem|teorema)", "teorema"},
        {"^(prop|proposition|proposizione)", "proposizione"},
        {"^(lem|lemma)", "lemma"},
        {"^(cor|corollar)", "corollario"},
        {"^(proof|dim|dimostrazione)", "dimostrazione"},
        {"^(rem|remark|oss|osservazione|note|nota)", "osservazione"},
        {"^(exercise|eserciz|exr|problem)", "esercizio"},
        {"^(ex|example|esempio)", "esempio"},
        {"^(achtung|warn|attenzione|caution|avvert)", "attenzione"},
        {"^(eq|formula)", "formula"},
        {"^(generic|box|frame|riquadro)", "riquadro"},
    };
    for (const Hint &hint : HINTS)
        if (RE(QLatin1String(hint.pattern)).match(n).hasMatch())
            return QLatin1String(hint.role);
    return {};
}

QString hex(int r, int g, int b)
{
    return QStringLiteral("#%1%2%3").arg(r, 2, 16, QChar('0')).arg(g, 2, 16, QChar('0')).arg(b, 2, 16, QChar('0'));
}
}

Style Style::scan(const QString &name, const QString &styText)
{
    Style s;
    s.name = name;
    QString text;                                      // senza i commenti
    for (QStringView line : QStringView(styText).split('\n')) {
        qsizetype end = line.size();
        for (qsizetype i = 0; i < line.size(); ++i) {
            if (line[i] == '\\')
                ++i;
            else if (line[i] == '%') {
                end = i;
                break;
            }
        }
        text += line.left(end);
        text += '\n';
    }

    // i colori definiti con i loro valori
    QHash<QString, QString> colors;
    static const RE color(R"(\\definecolor\{([^}]+)\}\{(RGB|rgb|HTML)\}\{([^}]+)\})");
    for (auto it = color.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        const QString model = m.captured(2), value = m.captured(3);
        if (model == "HTML") {
            colors.insert(m.captured(1), '#' + value.trimmed().toLower());
            continue;
        }
        const QStringList parts = value.split(',');
        if (parts.size() != 3)
            continue;
        const double scale = model == "rgb" ? 255.0 : 1.0;
        colors.insert(m.captured(1), hex(qRound(parts[0].toDouble() * scale), qRound(parts[1].toDouble() * scale), qRound(parts[2].toDouble() * scale)));
    }

    // gli ambienti: le definizioni di LaTeX, di tcolorbox, di amsthm, e i comandi propri dello stile che ne creano
    static const RE environment(
        R"(\\(?:newenvironment|renewenvironment|NewDocumentEnvironment|newtcolorbox|NewTColorBox|newtheorem\*?|newtcbtheorem|New[A-Za-z]*(?:Box|Env|Environment|Theorem)[A-Za-z]*)\*?\s*(?:\[[^\]]*\])?\s*\{([A-Za-z]+\*?)\})");
    QSet<QString> seen;
    QList<qsizetype> starts;
    for (auto it = environment.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        const QString env = m.captured(1);
        if (seen.contains(env))
            continue;
        seen.insert(env);
        s.environments << Environment{env, roleFromName(env), "opzionale", {}, true, {}};
        starts << m.capturedStart();
    }
    // di ognuno, fino alla definizione dopo: il colore che nomina, e se può spezzarsi
    static const RE word(R"([A-Za-z]+)");
    for (qsizetype i = 0; i < s.environments.size(); ++i) {
        const qsizetype end = i + 1 < starts.size() ? starts[i + 1] : qMin(text.size(), starts[i] + 1500);
        const QString body = text.mid(starts[i], qMin<qsizetype>(end - starts[i], 1500));
        Environment &env = s.environments[i];
        for (auto it = word.globalMatch(body); it.hasNext();) {
            const QString w = it.next().captured();
            if (colors.contains(w)) {
                env.color = colors.value(w);
                break;
            }
        }
        if (body.contains("unbreakable") || body.contains("minipage"))
            env.breaks = false;
    }

    // le macro: i comandi che lo stile offre (non quelli interni, con @)
    static const RE macro(
        R"(\\(?:newcommand|renewcommand|providecommand|NewDocumentCommand|DeclareMathOperator|DeclareRobustCommand|DeclarePairedDelimiter)\*?\s*\{?\s*(\\[A-Za-z]+)(?![A-Za-z@]))");
    static const QSet<QString> internal{"\\thesection", "\\thesubsection", "\\thesubsubsection", "\\theequation", "\\thefigure",
                                        "\\contentsname", "\\figurename", "\\tableofcontents", "\\headrulewidth", "\\footrulewidth",
                                        "\\sectionmark", "\\subsectionmark", "\\familydefault", "\\rmdefault", "\\numberline"};
    QSet<QString> macroSeen;
    for (auto it = macro.globalMatch(text); it.hasNext();) {
        const QString m = it.next().captured(1);
        if (macroSeen.contains(m) || internal.contains(m) || m.startsWith("\\the"))
            continue;
        macroSeen.insert(m);
        s.macros << Macro{m, {}};
    }
    if (macroSeen.contains("\\tagref"))
        s.references = "tag";
    if (macroSeen.contains("\\figcaption"))
        s.captionCommand = "\\figcaption";
    return s;
}

const Style::Environment *Style::environment(const QString &envName) const
{
    for (const Environment &e : environments)
        if (e.name == envName)
            return &e;
    return nullptr;
}

QString Style::environmentFor(const QString &role) const
{
    for (const Environment &e : environments)
        if (e.role == role)
            return e.name;
    return {};
}

QStringList Style::undecided() const
{
    QStringList out;
    for (const Environment &e : environments)
        if (e.role.isEmpty())
            out << e.name;
    return out;
}

QString Style::forPrompt() const
{
    QHash<QString, QString> labels;
    for (const Role &role : roles())
        labels.insert(role.value, role.label);

    QStringList lines{name.isEmpty() ? QStringLiteral("## Lo stile del documento: nessuno (article con amsthm)")
                                     : QStringLiteral("## Lo stile del documento: `%1`").arg(name)};
    lines << QString() << QStringLiteral("Ambienti, per ruolo (si usano solo questi):") << QString();
    for (const Environment &e : environments) {
        if (e.role.isEmpty() || e.role == "altro")
            continue;
        QString line = QStringLiteral("- %1: `%2`").arg(labels.value(e.role, e.role), e.name);
        line += e.title == "opzionale" ? QStringLiteral(", titolo opzionale `\\begin{%1}[Titolo]`").arg(e.name) : QStringLiteral(", senza titolo");
        if (!e.breaks)
            line += QStringLiteral(", non si spezza fra le pagine");
        if (!e.note.isEmpty())
            line += QStringLiteral(". %1").arg(e.note);
        lines << line;
    }
    QStringList described;
    for (const Macro &m : macros)
        if (!m.meaning.isEmpty())
            described << QStringLiteral("- `%1`: %2").arg(m.name, m.meaning);
    if (!described.isEmpty())
        lines << QString() << QStringLiteral("Macro dello stile (si usano al posto della scrittura a mano):") << QString() << described;
    lines << QString()
          << (references == "tag" ? QStringLiteral("Rimandi: `\\tagref{etichetta}` (anche per le formule: `\\eqref`).")
                                  : QStringLiteral("Rimandi: `\\cref{etichetta}`; per le formule `\\eqref{etichetta}`."));
    lines << (figureEnvironment.isEmpty() ? QStringLiteral("Figure: ambiente `figure` con `\\centering`.")
                                          : QStringLiteral("Figure: sempre dentro `%1`.").arg(figureEnvironment))
          << QStringLiteral("Didascalie: `%1`.").arg(captionCommand.isEmpty() ? QStringLiteral("\\caption{…}") : captionCommand);
    if (!conventions.trimmed().isEmpty())
        lines << QString() << conventions.trimmed();
    return lines.join('\n');
}

}

namespace tx {
QStringList Style::boxes() const
{
    QStringList out;
    for (const Environment &e : environments)
        if (!e.role.isEmpty() && e.role != "altro" && e.role != "formula")
            out << e.name;
    return out;
}

QString Style::figureWrapper() const { return figureEnvironment.isEmpty() ? QStringLiteral("center") : figureEnvironment; }
}
