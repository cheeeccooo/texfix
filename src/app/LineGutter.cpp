#include "LineGutter.h"

#include "Theme.h"

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QPainter>
#include <QTextBlock>
#include <QTextDocument>

LineGutter::LineGutter(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    connect(Theme::instance(), &Theme::changed, this, [this] { update(); });
}

void LineGutter::setDocument(QQuickTextDocument *document)
{
    if (m_document == document)
        return;
    m_document = document;
    if (QTextDocument *text = document ? document->textDocument() : nullptr) {
        connect(text->documentLayout(), &QAbstractTextDocumentLayout::update, this, [this] { update(); });
        connect(text, &QTextDocument::blockCountChanged, this, [this] {
            emit neededWidthChanged();
            update();
        });
    }
    emit documentChanged();
    emit neededWidthChanged();
    update();
}

void LineGutter::setContentY(qreal y)
{
    if (qFuzzyCompare(m_contentY, y))
        return;
    m_contentY = y;
    emit contentYChanged();
    update();
}

void LineGutter::setTopPadding(qreal padding)
{
    if (qFuzzyCompare(m_topPadding, padding))
        return;
    m_topPadding = padding;
    emit topPaddingChanged();
    update();
}

void LineGutter::setCurrentLine(int line)
{
    if (m_currentLine == line)
        return;
    m_currentLine = line;
    emit currentLineChanged();
    update();
}

QFont LineGutter::numberFont() const
{
    QFont font(Theme::instance()->monoFamily());
    font.setPixelSize(Theme::instance()->smallSize());
    return font;
}

qreal LineGutter::neededWidth() const
{
    const int lines = m_document && m_document->textDocument() ? m_document->textDocument()->blockCount() : 1;
    const int digits = qMax(3, int(QString::number(lines).size()));
    return QFontMetricsF(numberFont()).horizontalAdvance(QString(digits, '9')) + 2 * Theme::instance()->gap();
}

int LineGutter::lineAt(qreal y) const
{
    const QTextDocument *text = m_document ? m_document->textDocument() : nullptr;
    if (!text)
        return 1;
    const int position = text->documentLayout()->hitTest(QPointF(0, y + m_contentY - m_topPadding), Qt::FuzzyHit);
    return text->findBlock(qMax(0, position)).blockNumber() + 1;
}

void LineGutter::paint(QPainter *painter)
{
    const QTextDocument *text = m_document ? m_document->textDocument() : nullptr;
    if (!text)
        return;
    const Theme *theme = Theme::instance();
    const QAbstractTextDocumentLayout *layout = text->documentLayout();
    painter->setFont(numberFont());
    const qreal right = width() - theme->gap();
    for (QTextBlock block = text->findBlockByNumber(lineAt(0) - 1); block.isValid(); block = block.next()) {
        const QRectF rect = layout->blockBoundingRect(block);
        const qreal top = rect.top() - m_contentY + m_topPadding;
        if (top > height())
            break;
        const qreal lineHeight = block.layout() && block.layout()->lineCount() > 0 ? block.layout()->lineAt(0).height() : rect.height();
        const bool current = block.blockNumber() + 1 == m_currentLine;
        painter->setPen(current ? theme->text() : theme->textFaint());
        painter->drawText(QRectF(0, top, right, lineHeight), Qt::AlignRight | Qt::AlignVCenter, QString::number(block.blockNumber() + 1));
    }
}
