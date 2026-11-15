#pragma once
#include <QQuickPaintedItem>
#include <QQuickTextDocument>

// I numeri di riga accanto all'editor: disegna solo quelli che si vedono.
class LineGutter : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickTextDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(qreal contentY READ contentY WRITE setContentY NOTIFY contentYChanged)   // quanto è scorso il testo
    Q_PROPERTY(qreal topPadding READ topPadding WRITE setTopPadding NOTIFY topPaddingChanged)
    Q_PROPERTY(int currentLine READ currentLine WRITE setCurrentLine NOTIFY currentLineChanged)
    Q_PROPERTY(qreal neededWidth READ neededWidth NOTIFY neededWidthChanged)

public:
    explicit LineGutter(QQuickItem *parent = nullptr);

    QQuickTextDocument *document() const { return m_document; }
    void setDocument(QQuickTextDocument *document);
    qreal contentY() const { return m_contentY; }
    void setContentY(qreal y);
    qreal topPadding() const { return m_topPadding; }
    void setTopPadding(qreal padding);
    int currentLine() const { return m_currentLine; }
    void setCurrentLine(int line);
    qreal neededWidth() const;

    // La riga (da 1) all'altezza y dell'elemento.
    Q_INVOKABLE int lineAt(qreal y) const;

    void paint(QPainter *painter) override;

signals:
    void documentChanged();
    void contentYChanged();
    void topPaddingChanged();
    void currentLineChanged();
    void neededWidthChanged();

private:
    QFont numberFont() const;

    QQuickTextDocument *m_document = nullptr;
    qreal m_contentY = 0, m_topPadding = 0;
    int m_currentLine = 1;
};
