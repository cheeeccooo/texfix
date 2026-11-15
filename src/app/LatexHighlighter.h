#pragma once
#include <QSyntaxHighlighter>

// L'evidenziazione del LaTeX nell'editor, con i colori del tema.
class LatexHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit LatexHighlighter(QTextDocument *document);

protected:
    void highlightBlock(const QString &text) override;
};
