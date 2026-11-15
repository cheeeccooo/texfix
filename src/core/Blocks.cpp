#include "core/Blocks.h"

#include "core/Project.h"

#include <QRegularExpression>

using RE = QRegularExpression;

namespace tx::blocks {

namespace {
const RE &markerRe()
{
    static const RE re(R"(^\s*%\s*@(L\d+\.\d+)\s*(?:·\s*(.*?))?\s*$)");
    return re;
}

// Le righe che non sono contenuto: titoli, la lezione, i cambi di pagina.
bool structural(const QString &line)
{
    static const QStringList heads{"\\section", "\\subsection", "\\subsubsection", "\\lezione", "\\clearpage", "\\newpage",
                                   "\\pagebreak", "\\vspace"};
    for (const QString &head : heads)
        if (line.startsWith(head))
            return true;
    return false;
}

QString unitOf(const QString &line, const Units &units)
{
    static const RE begin(R"(^\\begin\{([A-Za-z]+\*?)\})");
    const QString env = begin.match(line).captured(1);
    if (units.boxes.contains(env))
        return env;
    return units.figures.contains(env) ? QStringLiteral("figura") : QString();
}

int depthChange(const QString &line)
{
    // (su una QString: contare in una vista più corta di ciò che si cerca dà -1)
    const QString code = uncommented(line).toString();
    return int(code.count(QLatin1String("\\begin{")) - code.count(QLatin1String("\\end{")));
}
}

QList<int> Block::pages() const
{
    static const RE re(R"(\bpp?\.\s*(\d+)(?:\s*[-–]\s*(\d+))?)");
    QList<int> out;
    for (auto it = re.globalMatch(source); it.hasNext();) {
        const auto m = it.next();
        const int first = m.captured(1).toInt(), last = m.captured(2).isEmpty() ? first : m.captured(2).toInt();
        for (int p = first; p <= last && p - first < 50; ++p)
            out << p;
    }
    return out;
}

QList<Block> parse(const QString &tex, const Units &units)
{
    static const RE titleRe(R"(^\\begin\{[A-Za-z]+\*?\}\[\{?([^\]]*?)\}?\])");
    const QStringList lines = tex.split('\n');
    QList<Block> out;
    int pending = 0;                                   // un marcatore che aspetta il suo blocco
    QString pendingId, pendingSource;
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines[i].trimmed();
        if (const auto m = markerRe().match(lines[i]); m.hasMatch()) {
            pending = i + 1;
            pendingId = m.captured(1);
            pendingSource = m.captured(2);
            continue;
        }
        if (line.isEmpty() || line.startsWith('%') || structural(line))
            continue;
        Block block;
        block.line = i + 1;
        if (pending) {
            block.marker = pending;
            block.id = pendingId;
            block.source = pendingSource;
            pending = 0;
        }
        const QString unit = unitOf(line, units);
        int last = i;
        if (!unit.isEmpty()) {                         // un riquadro o una figura: fino al suo \end (e alla didascalia)
            block.kind = unit;
            block.title = titleRe.match(line).captured(1).trimmed();
            int depth = 0;
            for (last = i; last < lines.size(); ++last) {
                depth += depthChange(lines[last]);
                if (depth <= 0)
                    break;
            }
            last = qMin(last, int(lines.size()) - 1);
            while (unit == "figura" && last + 1 < lines.size() && lines[last + 1].trimmed().contains("caption"))
                ++last;
        } else {                                       // testo: fino al prossimo riquadro, figura, titolo o marcatore
            block.kind = "testo";
            block.title = line.left(60);
            int depth = 0;
            for (int j = i; j < lines.size(); ++j) {
                const QString t = lines[j].trimmed();
                if (j > i && depth == 0 && (markerRe().match(lines[j]).hasMatch() || structural(t) || !unitOf(t, units).isEmpty()))
                    break;
                depth = qMax(0, depth + depthChange(lines[j]));
                if (!t.isEmpty() && !t.startsWith('%'))
                    last = j;
            }
        }
        block.endLine = last + 1;
        i = last;
        out << block;
    }
    return out;
}

QString markerLine(const QString &id, const QString &source)
{
    return source.trimmed().isEmpty() ? QStringLiteral("% @%1").arg(id) : QStringLiteral("% @%1 · %2").arg(id, source.trimmed());
}

QString mark(const QString &tex, int lesson, const Units &units, const std::function<QString(const Block &)> &source, int *next)
{
    const QList<Block> all = parse(tex, units);
    int n = next ? *next : 0;
    if (!next) {                                       // dopo l'ultimo della lezione già nel file
        static const RE own(R"(^L(\d+)\.(\d+)$)");
        for (const Block &block : all)
            if (const auto m = own.match(block.id); m.hasMatch() && m.captured(1).toInt() == lesson)
                n = qMax(n, m.captured(2).toInt());
        ++n;
    }
    QStringList lines = tex.split('\n');
    static const RE lead(R"(^\s*)");
    QList<std::pair<int, QString>> inserts;            // dal fondo, così le righe sopra non si spostano
    for (const Block &block : all) {
        if (block.marker)
            continue;
        const QString indent = lead.match(lines[block.line - 1]).captured(0);
        inserts.prepend({block.line - 1, indent + markerLine(QStringLiteral("L%1.%2").arg(lesson).arg(n++), source ? source(block) : QString())});
    }
    for (const auto &[at, marker] : inserts)
        lines.insert(at, marker);
    if (next)
        *next = n;
    return lines.join('\n');
}

}
