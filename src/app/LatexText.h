#pragma once
#include <QColor>
#include <QFont>
#include <QQuickPaintedItem>

#include "Formulas.h"
#include "core/Snippets.h"

// Un pezzo di LaTeX disegnato subito, come in Studify: le parole nel carattere dell'interfaccia, le formule disegnate
// da MicroTeX sulla stessa linea di base, grassetto e corsivo, elenchi, a capo e capoversi; una formula a sé centrata,
// rimpicciolita se è più larga della riga; una formula che non si legge resta il suo sorgente. L'altezza si sa appena
// si sa la larghezza: niente si sposta dopo.
class LatexText : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    Q_PROPERTY(bool latex READ latex WRITE setLatex NOTIFY textChanged)        // LaTeX di una lezione; se no prosa del modello
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY styleChanged)
    Q_PROPERTY(QColor brokenColor READ brokenColor WRITE setBrokenColor NOTIFY styleChanged)
    Q_PROPERTY(qreal fontSize READ fontSize WRITE setFontSize NOTIFY styleChanged)
    Q_PROPERTY(bool bold READ bold WRITE setBold NOTIFY styleChanged)

public:
    explicit LatexText(QQuickItem *parent = nullptr);

    QString text() const { return m_text; }
    void setText(const QString &text);
    bool latex() const { return m_latex; }
    void setLatex(bool latex);
    QColor color() const { return m_color; }
    void setColor(const QColor &color);
    QColor brokenColor() const { return m_broken; }
    void setBrokenColor(const QColor &color);
    qreal fontSize() const { return m_fontSize; }
    void setFontSize(qreal size);
    bool bold() const { return m_bold; }
    void setBold(bool bold);

    void paint(QPainter *painter) override;

signals:
    void textChanged();
    void styleChanged();

protected:
    void geometryChange(const QRectF &now, const QRectF &before) override;

private:
    using Token = tx::snippets::Token;
    struct Placed {
        int token;                                     // in m_tokens
        qreal x = 0, w = 0;
        math::Size m;                                  // le formule
        qreal scale = 1;                               // una formula a sé più larga della riga, rimpicciolita
    };
    struct Line {
        QList<Placed> items;
        qreal ascent = 0, descent = 0, gap = 0, indent = 0;
        bool display = false;
        QString label;                                 // la voce di un elenco
    };

    void retokenize();
    void relayout();
    // le righe dentro width; restituisce l'altezza e, in `widest`, la riga più larga
    qreal layout(qreal width, QList<Line> &lines, qreal *widest) const;
    QFont font(bool bold, bool italic) const;
    qreal mathSize() const { return m_fontSize * 1.1; }

    QString m_text;
    bool m_latex = false;
    QColor m_color = Qt::black, m_broken = QColor(200, 90, 60);
    qreal m_fontSize = 14;
    bool m_bold = false;
    QList<Token> m_tokens;
    QList<Line> m_lines;
    qreal m_laidWidth = -1;
};
