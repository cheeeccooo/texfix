#include "core/TexLog.h"

#include <QRegularExpression>
#include <QStringList>

using RE = QRegularExpression;

namespace tx::texlog {

QString cleanRow(QString t)
{
    static const RE font(R"(\\[A-Za-z0-9]+/[^ ]*/\d+(\.\d+)? ?)"), plus(R"(\(\+\d+\))"),
        hyphen(R"((?<=\w)-(?=\w))", RE::UseUnicodePropertiesOption), space(R"(\s+)");
    t.remove(font);
    t.replace("[]", " ").remove('$');
    t.remove(plus);
    t.remove(hyphen);                                  // punti in cui la parola si potrebbe spezzare, non trattini
    return t.replace(space, " ").trimmed();
}

QList<Issue> parse(const QString &log)
{
    static const RE opened(R"(\((?:\./)?([^\s()]+\.tex))");
    static const RE fileError(R"(^(?:\./)?([^:\s]+\.(?:tex|sty|cls)):(\d+): (.*))");
    static const RE paragraph(R"(^(Overfull|Underfull) \\hbox \((.*?)\) (?:in paragraph|in alignment) at lines (\d+)--(\d+))");
    static const RE single(R"(^(Overfull|Underfull) \\hbox \((.*?)\) detected at line (\d+)())");

    QList<Issue> issues;
    QString current;                                   // l'ultimo .tex aperto: lì stanno le righe fuori misura
    const QStringList lines = log.split('\n');
    for (qsizetype k = 0; k < lines.size(); ++k) {
        const QString &line = lines[k];
        for (auto it = opened.globalMatch(line); it.hasNext();)
            current = it.next().captured(1);

        if (const auto m = fileError.match(line); m.hasMatch()) {
            issues << Issue{Issue::Error, m.captured(1), m.captured(2).toInt(), m.captured(2).toInt(), m.captured(3).trimmed(), {}};
            continue;
        }
        if (line.startsWith('!')) {
            issues << Issue{Issue::Error, current, 0, 0, line.mid(1).trimmed(), {}};
            continue;
        }
        auto m = paragraph.match(line);
        if (!m.hasMatch())
            m = single.match(line);
        if (m.hasMatch()) {
            const int first = m.captured(3).toInt();
            const int last = m.captured(4).isEmpty() ? first : m.captured(4).toInt();
            const QString row = k + 1 < lines.size() ? cleanRow(lines[k + 1]) : QString();
            issues << Issue{m.captured(1) == "Overfull" ? Issue::WideLine : Issue::LooseLine, current, first, last, row, m.captured(2)};
        }
    }
    return issues;
}

int pages(const QString &log)
{
    static const RE re(R"(\((\d+) pages?[,)])");
    return re.match(log).captured(1).toInt();
}

bool brokenAux(const QString &log)
{
    static const RE re(R"(\.aux\)\s*\n(?:Runaway argument\?|!)[\s\S]{0,600}?l\.\d+ \\begin\{document\})");
    return re.match(log).hasMatch();
}

}
