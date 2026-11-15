#include "core/TexSource.h"

#include <QRegularExpression>

using RE = QRegularExpression;

namespace tx::texsource {

QString preamble(const QString &document)
{
    const qsizetype at = document.indexOf("\\begin{document}");
    return at < 0 ? QString() : document.left(at);
}

QString body(const QString &document)
{
    const qsizetype begin = document.indexOf("\\begin{document}");
    if (begin < 0)
        return document;
    const qsizetype start = begin + 16, end = document.lastIndexOf("\\end{document}");
    return document.mid(start, end < 0 ? -1 : end - start).trimmed();
}

QDate dateIn(const QString &heading)
{
    static const RE numeric(R"((\d{1,2})[./-](\d{1,2})[./-](\d{4}|\d{2}))");
    static const RE worded(R"((\d{1,2})\s*°?\s+(gennaio|febbraio|marzo|aprile|maggio|giugno|luglio|agosto|settembre|ottobre|novembre|dicembre)\s+(\d{4}))",
                           RE::CaseInsensitiveOption);
    static const QStringList months{"gennaio", "febbraio", "marzo", "aprile", "maggio", "giugno", "luglio", "agosto", "settembre", "ottobre", "novembre", "dicembre"};
    if (const auto m = worded.match(heading); m.hasMatch())
        return QDate(m.captured(3).toInt(), int(months.indexOf(m.captured(2).toLower())) + 1, m.captured(1).toInt());
    if (const auto m = numeric.match(heading); m.hasMatch())
        return QDate(m.captured(3).size() == 2 ? 2000 + m.captured(3).toInt() : m.captured(3).toInt(), m.captured(2).toInt(), m.captured(1).toInt());
    return {};
}

QList<Lesson> lessons(const QString &document)
{
    static const RE section(R"(^[ \t]*\\section\*?\s*\{([^}]*)\}[^\n]*$)", RE::MultilineOption);
    const QString text = body(document);
    QList<Lesson> out;
    QList<std::pair<qsizetype, qsizetype>> heads;      // inizio e fine della riga del \section
    QStringList titles;
    for (auto it = section.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        heads.append({m.capturedStart(), m.capturedEnd()});
        titles << m.captured(1).trimmed();
    }
    for (qsizetype i = 0; i < heads.size(); ++i) {
        const QDate date = dateIn(titles[i]);
        if (!date.isValid())
            return {};                                 // una sezione senza data: il documento non è diviso in lezioni
        Lesson lesson{date, titles[i], text.mid(heads[i].second, (i + 1 < heads.size() ? heads[i + 1].first : text.size()) - heads[i].second).trimmed(), 0};
        lesson.lines = int(lesson.text.count('\n')) + 1;
        out << lesson;
    }
    std::stable_sort(out.begin(), out.end(), [](const Lesson &a, const Lesson &b) { return a.date < b.date; });   // in ordine di calendario
    return out;
}

QStringList macros(const QString &preambleText)
{
    static const RE definition(R"(^[ \t]*\\(?:newcommand|renewcommand|providecommand|DeclareMathOperator|NewDocumentCommand|def|let)\b[^\n]*$)", RE::MultilineOption);
    QStringList out;
    for (auto it = definition.globalMatch(preambleText); it.hasNext();)
        out << it.next().captured(0).trimmed();
    return out;
}

QString spec(const QString &file, const QDate &date) { return date.isValid() ? file + '@' + date.toString(Qt::ISODate) : file; }

std::pair<QString, QDate> parseSpec(const QString &specText)
{
    const qsizetype at = specText.lastIndexOf('@');
    if (at < 0)
        return {specText, {}};
    const QDate date = QDate::fromString(specText.mid(at + 1), Qt::ISODate);
    return date.isValid() ? std::pair{specText.left(at), date} : std::pair{specText, QDate()};
}

QString workerText(const QString &document, const QDate &date)
{
    QString text = body(document);
    for (const Lesson &lesson : lessons(document))
        if (lesson.date == date)
            text = lesson.text;
    QStringList head;
    for (const QString &macro : macros(preamble(document)))
        head << "% " + macro;
    return head.isEmpty() ? text : "% Macro dell'autore (non si copiano: si scrive nella notazione del progetto)\n" + head.join('\n') + "\n\n" + text;
}

}
