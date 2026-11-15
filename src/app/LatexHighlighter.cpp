#include "LatexHighlighter.h"

#include "Theme.h"

#include "Workspace.h"

#include <QRegularExpression>

namespace {
enum State { Text = -1, DisplayMath = 1 };            // lo stato di un blocco: dentro \[ … \] che continua
}

LatexHighlighter::LatexHighlighter(QTextDocument *document) : QSyntaxHighlighter(document)
{
    connect(Theme::instance(), &Theme::changed, this, &QSyntaxHighlighter::rehighlight);
    connect(Workspace::instance(), &Workspace::styleChanged, this, &QSyntaxHighlighter::rehighlight);
}

void LatexHighlighter::highlightBlock(const QString &text)
{
    const Theme *theme = Theme::instance();
    static const QRegularExpression command(R"(\\(?:[A-Za-z@]+\*?|.))");
    static const QRegularExpression environment(R"(\\(?:begin|end)\s*\{([^}]*)\})");

    // dove finisce il codice: un % non preceduto da \ apre il commento
    qsizetype codeEnd = text.size();
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text[i] == '\\') {
            ++i;
        } else if (text[i] == '%') {
            codeEnd = i;
            break;
        }
    }

    // la matematica: $…$ sulla riga, \[ … \] anche su più righe
    bool display = previousBlockState() == DisplayMath;
    qsizetype mathStart = display ? 0 : -1;
    bool inline_ = false;
    for (qsizetype i = 0; i < codeEnd; ++i) {
        const QChar c = text[i];
        if (c == '\\' && i + 1 < codeEnd) {
            const QChar next = text[i + 1];
            if (!inline_ && !display && next == '[') {
                display = true;
                mathStart = i;
            } else if (display && next == ']') {
                setFormat(mathStart, i + 2 - mathStart, theme->synMath());
                display = false;
                mathStart = -1;
            }
            ++i;
        } else if (c == '$' && !display) {
            if (inline_)
                setFormat(mathStart, i + 1 - mathStart, theme->synMath());
            else
                mathStart = i;
            inline_ = !inline_;
        }
    }
    if ((display || inline_) && mathStart >= 0)
        setFormat(mathStart, codeEnd - mathStart, theme->synMath());
    setCurrentBlockState(display ? DisplayMath : Text);

    for (auto it = command.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        if (m.capturedStart() >= codeEnd)
            break;
        if (m.capturedLength() > 2 || text[m.capturedStart() + 1].isLetter())
            setFormat(m.capturedStart(), m.capturedLength(), theme->synCommand());
    }
    for (qsizetype i = 0; i < codeEnd; ++i) {
        if ((text[i] == '{' || text[i] == '}') && (i == 0 || text[i - 1] != '\\'))
            setFormat(i, 1, theme->synBrace());
    }
    for (auto it = environment.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        if (m.capturedStart() >= codeEnd)
            break;
        QTextCharFormat format;
        // begin ed end di un ambiente dello stile nel colore che ha nel PDF, reso leggibile sullo sfondo dell'editor
        const tx::Style::Environment *env = Workspace::instance()->style().environment(m.captured(1));
        format.setForeground(env && !env->color.isEmpty() ? theme->readable(QColor(env->color)) : theme->synEnvironment());
        format.setFontWeight(QFont::DemiBold);
        setFormat(m.capturedStart(), m.capturedLength(), format);
    }
    if (codeEnd < text.size()) {
        QTextCharFormat format;
        format.setForeground(theme->synComment());
        format.setFontItalic(true);
        setFormat(codeEnd, text.size() - codeEnd, format);
    }
}
