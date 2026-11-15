#include "EditorBackend.h"

#include "LatexHighlighter.h"

#include <QTextBlock>
#include <QTextDocument>

EditorBackend::EditorBackend(QObject *parent) : QObject(parent) {}

void EditorBackend::setDocument(QQuickTextDocument *document)
{
    if (m_document == document)
        return;
    delete m_highlighter;
    m_highlighter = nullptr;
    m_document = document;
    if (QTextDocument *text = document ? document->textDocument() : nullptr) {
        m_highlighter = new LatexHighlighter(text);
        connect(text, &QTextDocument::blockCountChanged, this, &EditorBackend::lineCountChanged);
    }
    emit documentChanged();
    emit lineCountChanged();
}

int EditorBackend::lineCount() const
{
    return m_document && m_document->textDocument() ? m_document->textDocument()->blockCount() : 1;
}

int EditorBackend::lineOf(int position) const
{
    if (!m_document || !m_document->textDocument())
        return 1;
    return m_document->textDocument()->findBlock(position).blockNumber() + 1;
}

int EditorBackend::positionOf(int line) const
{
    if (!m_document || !m_document->textDocument())
        return 0;
    const QTextDocument *text = m_document->textDocument();
    const QTextBlock block = text->findBlockByNumber(qBound(0, line - 1, text->blockCount() - 1));
    return block.position();
}
