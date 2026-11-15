#include "core/Fix.h"

#include "core/Compiler.h"
#include "core/Files.h"
#include "core/Rules.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

using RE = QRegularExpression;

namespace tx::fix {

namespace {
struct Measure {
    int errors = 0, rows = 0;
    QList<Issue> issues;                               // quelli della lezione
    bool better(const Measure &than) const { return errors < than.errors || (errors == than.errors && rows < than.rows); }
};

Measure measure(const Project &project, const QString &file, JobControl *control, QString *failure = nullptr)
{
    const BuildResult built = Compiler::compileSync(project, file, control);
    if (failure)
        *failure = built.failure;
    Measure m;
    for (const Issue &issue : built.issues) {
        if (issue.kind == Issue::Error) {              // un errore conta anche se il log non sa di quale file è
            ++m.errors;
            m.issues << issue;
        } else if (issue.file == file) {
            ++m.rows;
            m.issues << issue;
        }
    }
    return m;
}

// Una riga che chiude un paragrafo di testo: vuota, un marcatore, l'inizio o la fine di un ambiente, un titolo, un display.
bool boundary(const QString &line)
{
    const QString t = line.trimmed();
    static const RE structure(R"(^(\\begin\{|\\end\{|\\\[|\\\]|\\(sub)*section|\\paragraph|\\lezione|\\item\b|\\label\{|\\caption|\\includegraphics|\\imgslot|\$\$))");
    return t.isEmpty() || t.startsWith('%') || structure.match(t).hasMatch();
}

// Prova un testo nuovo per la lezione: resta se migliora, altrimenti torna quello di prima.
bool attempt(const Project &project, const QString &file, const QString &text, Measure &current, JobControl *control)
{
    const QString path = project.path(file), before = files::read(path);
    if (text == before)
        return false;
    files::write(path, text);
    try {
        const Measure now = measure(project, file, control);
        if (now.better(current)) {
            current = now;
            return true;
        }
    } catch (...) {
        files::write(path, before);                    // fermato a metà di una prova: la lezione torna com'era
        throw;
    }
    files::write(path, before);
    return false;
}

QString errorsPrompt()
{
    return QStringLiteral(
        R"(You fix LaTeX compilation errors in a lecture-notes file. Each card gives the error, its line and the source around it. Return only JSON, a list of exact replacements:
[{"old": "<exact text copied from the source, unique in the file>", "new": "..."}]
Change only what the error requires. Titles of environments containing [ or ] must be wrapped as [{...}]. Never change mathematics beyond the fix. Lines starting with `% @` or containing `% img` are markers: never include them in `old`, never remove them.)");
}

QString rowsPrompt(const Settings &settings)
{
    return QStringLiteral(
               R"(You remove Overfull/Underfull \hbox warnings from a LaTeX lecture-notes file by rewording. Each card shows the source paragraph (`src`) and the output line TeX had to stretch or overflow (`riga`).
Return only JSON: [{"card": <n>, "old": "<the whole text paragraph of the card, copied exactly>", "new": ["<variant 1>", "<variant 2>", "<variant 3>"]}]
- TeX re-breaks the whole paragraph: a change in one line moves the problem to a later line. So every variant is the whole paragraph rewritten so that ALL its lines are full.
- Variants ordered by fidelity to the original, least invasive first: the first one that clears the warning is kept.
- Mathematics stays identical: same formulas and symbols. You may move an inline formula into a display or split a display into align*, never change it.
- Never use \newpage, \linebreak, \vspace, \hspace, font-size commands or \resizebox. Keep the subject of the sentence and the connectives that introduce a formula.
- `old` must be copied exactly from src and occur once in the file. Never include marker lines (`% @…`).
)") + "\nEDITORIAL RULES\n\n" + rules::forPrompt({"larghezza"}, settings.rules);
}

// Le sostituzioni esatte di una risposta: applicate solo se il testo vecchio c'è una volta sola e non tocca un marcatore.
QString applyReplacements(QString text, const QJsonArray &list)
{
    for (const QJsonValue &v : list) {
        const QString old = v.toObject().value("old").toString(), fresh = v.toObject().value("new").toString();
        if (old.isEmpty() || old.contains("% @") || old.contains("% img") || text.count(old) != 1)
            continue;
        text.replace(old, fresh);
    }
    return text;
}
}

std::pair<int, int> paragraph(const QStringList &allLines, int first, int last)
{
    // ciò che sta dentro una formula in display non è testo: per questa ricerca vale come una riga vuota
    static const RE open(R"(\\\[|\\begin\{(equation|align|gather|multline|eqnarray|cases|tikzpicture)\*?\})"), close(R"(\\\]|\\end\{(equation|align|gather|multline|eqnarray|cases|tikzpicture)\*?\})");
    QStringList lines = allLines;
    bool inside = false;
    for (QString &line : lines) {
        const bool opens = open.match(line).hasMatch(), closes = close.match(line).hasMatch();
        if (inside || opens)
            line.clear();
        inside = (inside || opens) && !closes;
    }
    first = qBound(1, first, int(lines.size()));
    last = qBound(first, last, int(lines.size()));
    int a = first, b = last;
    while (a <= b && boundary(lines[a - 1]))            // l'avviso può cominciare su una riga di struttura
        ++a;
    while (b >= a && boundary(lines[b - 1]))
        --b;
    if (a > b)
        return {0, 0};
    while (a > 1 && !boundary(lines[a - 2]))
        --a;
    while (b < lines.size() && !boundary(lines[b]))
        ++b;
    return {a, b};
}

Result lesson(const Project &project, const Settings &settings, const Style &style, Model &model, const QString &file, JobControl *control)
{
    Q_UNUSED(style)
    Result result;
    const QString path = project.path(file);
    Measure current = measure(project, file, control, &result.failure);
    if (!result.failure.isEmpty())
        return result;
    result.errorsBefore = current.errors;
    result.rowsBefore = current.rows;
    auto log = [&](const QString &line) {
        if (control)
            control->log(QFileInfo(file).fileName() + ": " + line);
    };

    // gli errori: il modello, due giri al più
    for (int round = 0; round < 2 && current.errors > 0; ++round) {
        const QStringList lines = files::read(path).split('\n');
        QStringList cards;
        for (const Issue &issue : std::as_const(current.issues)) {
            if (issue.kind != Issue::Error || cards.size() >= 8)
                continue;
            const int at = issue.file == file ? issue.line : 0;
            cards << QStringLiteral("Error: %1%2\n```latex\n%3\n```")
                         .arg(issue.text, at ? QStringLiteral(" (line %1)").arg(at) : QString(),
                              at ? lines.mid(qMax(0, at - 7), 13).join('\n') : lines.mid(0, 40).join('\n'));
        }
        try {
            const QJsonArray fixes = Model::json(model.ask(round ? "fix2" : "fix", errorsPrompt(), {cards.join("\n\n")}, QFileInfo(file).completeBaseName())).toArray();
            if (!attempt(project, file, applyReplacements(lines.join('\n'), fixes), current, control))
                break;
            log(QStringLiteral("errori rimasti %1").arg(current.errors));
        } catch (const Stopped &) {
            throw;
        } catch (const LimitReached &) {
            throw;
        } catch (const std::exception &) {
            break;
        }
    }

    // le righe: prima spezzare il paragrafo a una fine di frase, misurando
    if (current.errors == 0) {
        for (int pass = 0; pass < 12 && current.rows > 0; ++pass) {
            bool fixed = false;
            for (const Issue &issue : std::as_const(current.issues)) {
                if (issue.kind == Issue::Error)
                    continue;
                const QStringList lines = files::read(path).split('\n');
                const auto [a, b] = paragraph(lines, issue.line, issue.lastLine);
                if (!a)
                    continue;
                static const RE sentenceEnd(R"((?<=[.;:!?])\s+(?=\p{Lu}))", RE::UseUnicodePropertiesOption);
                const QString indent = RE(R"(^\s*)").match(lines[a - 1]).captured(0);
                const QString text = lines.mid(a - 1, b - a + 1).join('\n');
                int tried = 0;
                for (auto it = sentenceEnd.globalMatch(text); it.hasNext() && tried < 4 && !fixed; ++tried) {
                    const auto m = it.next();
                    if (text.left(m.capturedStart()).count('$') % 2)   // dentro una formula non si spezza
                        continue;
                    const QString split = text.left(m.capturedStart()) + "\n\n" + indent + text.mid(m.capturedEnd());
                    const QStringList changed = lines.mid(0, a - 1) + split.split('\n') + lines.mid(b);
                    fixed = attempt(project, file, changed.join('\n'), current, control);
                }
                if (fixed) {
                    ++result.byMoves;
                    break;                             // le righe sono cambiate: si riparte dagli avvisi nuovi
                }
            }
            if (!fixed)
                break;
        }
    }

    // poi il modello: il paragrafo riscritto in più varianti, la prima che toglie l'avviso resta
    for (int round = 0; round < 2 && current.errors == 0 && current.rows > 0; ++round) {
        const QStringList lines = files::read(path).split('\n');
        QStringList cards;
        QList<std::pair<int, int>> seen;
        for (const Issue &issue : std::as_const(current.issues)) {
            if (issue.kind == Issue::Error || cards.size() >= 6)
                continue;
            const auto span = paragraph(lines, issue.line, issue.lastLine);
            if (!span.first || seen.contains(span))
                continue;
            seen << span;
            cards << QStringLiteral("Card %1 (%2: %3)\nriga: %4\nsrc:\n```latex\n%5\n```")
                         .arg(cards.size() + 1).arg(issue.kind == Issue::WideLine ? "Overfull" : "Underfull", issue.amount, issue.text,
                                                    lines.mid(span.first - 1, span.second - span.first + 1).join('\n'));
        }
        if (cards.isEmpty())
            break;
        const int before = current.rows;
        try {
            const QJsonArray proposals = Model::json(model.ask(round ? "fix3" : "fix2", rowsPrompt(settings), {cards.join("\n\n")},
                                                               QFileInfo(file).completeBaseName() + " righe")).toArray();
            for (const QJsonValue &p : proposals) {
                const QString old = p.toObject().value("old").toString();
                for (const QJsonValue &variant : p.toObject().value("new").toArray()) {
                    const QString text = files::read(path);
                    if (old.isEmpty() || old.contains("% @") || text.count(old) != 1)
                        break;
                    if (attempt(project, file, QString(text).replace(old, variant.toString()), current, control))
                        break;
                }
            }
        } catch (const Stopped &) {
            throw;
        } catch (const LimitReached &) {
            throw;
        } catch (const std::exception &) {
            break;
        }
        result.byModel += before - current.rows;
        if (current.rows == before)
            break;
    }
    result.errorsAfter = current.errors;
    result.rowsAfter = current.rows;
    log(QStringLiteral("errori %1 → %2, righe fuori misura %3 → %4").arg(result.errorsBefore).arg(result.errorsAfter).arg(result.rowsBefore).arg(result.rowsAfter));
    return result;
}

}
