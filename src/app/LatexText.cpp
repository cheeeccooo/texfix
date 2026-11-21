#include "LatexText.h"

#include <QFontMetricsF>
#include <QGuiApplication>
#include <QPainter>

#include <cmath>

LatexText::LatexText(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
}

void LatexText::setText(const QString &text)
{
    if (text == m_text)
        return;
    m_text = text;
    retokenize();
    emit textChanged();
}

void LatexText::setLatex(bool latex)
{
    if (latex == m_latex)
        return;
    m_latex = latex;
    retokenize();
    emit textChanged();
}

void LatexText::setColor(const QColor &color)
{
    if (color == m_color)
        return;
    m_color = color;
    update();
    emit styleChanged();
}

void LatexText::setBrokenColor(const QColor &color)
{
    if (color == m_broken)
        return;
    m_broken = color;
    update();
    emit styleChanged();
}

void LatexText::setFontSize(qreal size)
{
    if (qFuzzyCompare(size, m_fontSize) || size <= 0)
        return;
    m_fontSize = size;
    relayout();
    emit styleChanged();
}

void LatexText::setBold(bool bold)
{
    if (bold == m_bold)
        return;
    m_bold = bold;
    relayout();
    emit styleChanged();
}

void LatexText::geometryChange(const QRectF &now, const QRectF &before)
{
    QQuickPaintedItem::geometryChange(now, before);
    if (!qFuzzyCompare(now.width(), before.width()))
        relayout();
}

void LatexText::retokenize()
{
    m_tokens = tx::snippets::tokens(m_text, m_latex ? tx::snippets::Kind::Latex : tx::snippets::Kind::Prose);
    relayout();
}

QFont LatexText::font(bool bold, bool italic) const
{
    QFont f = QGuiApplication::font();
    f.setPixelSize(std::max(1, int(std::lround(m_fontSize))));
    f.setBold(bold || m_bold);
    f.setItalic(italic);
    return f;
}

void LatexText::relayout()
{
    // la larghezza senza andare a capo è la misura naturale; l'altezza è quella alla larghezza che si ha
    QList<Line> lines;
    qreal widest = 0;
    layout(1e6, lines, &widest);
    setImplicitWidth(std::ceil(widest));
    m_lines.clear();
    const qreal w = width() > 0 ? width() : widest;
    const qreal h = layout(w, m_lines, nullptr);
    m_laidWidth = w;
    setImplicitHeight(std::ceil(h));
    update();
}

qreal LatexText::layout(qreal width, QList<Line> &lines, qreal *widest) const
{
    const qreal size = m_fontSize, msize = mathSize();
    const QFontMetricsF plain(font(false, false));
    const qreal textAscent = plain.ascent(), textDescent = plain.descent();
    const qreal lineGap = size * 0.28;
    const qreal spaceWidth = plain.horizontalAdvance(u' ');
    auto measureWord = [&](const Token &t) { return QFontMetricsF(font(t.bold, t.italic)).horizontalAdvance(t.text); };

    lines = {Line{}};
    qreal indent = 0, cursor = 0;
    auto newLine = [&](qreal gap = 0) {
        if (!lines.last().items.isEmpty() || !lines.last().label.isEmpty() || lines.last().display)
            lines << Line{};
        lines.last().gap = std::max(lines.last().gap, gap);
        lines.last().indent = indent;
        cursor = 0;
    };
    // i pezzi senza spazio in mezzo (una formula e la virgola dopo) vanno a capo insieme: `unit` è il primo della
    // riga che appartiene alla parola in corso
    qsizetype unit = 0;
    auto place = [&](int index, qreal w, const math::Size &m) {
        Line &line = lines.last();
        if (cursor > 0 && cursor + w > width - line.indent) {
            QList<Placed> carry;
            if (unit > 0 && unit < line.items.size()) {
                carry = line.items.mid(unit);
                line.items.resize(unit);
            }
            newLine();
            const qreal shift = carry.isEmpty() ? 0 : carry.first().x;
            for (Placed &p : carry) {
                p.x -= shift;
                lines.last().items << p;
            }
            cursor = carry.isEmpty() ? 0 : carry.last().x + carry.last().w;
            unit = 0;
        }
        lines.last().items << Placed{index, cursor, w, m, 1};
        cursor += w;
    };

    for (int k = 0; k < m_tokens.size(); ++k) {
        const Token &t = m_tokens[k];
        switch (t.type) {
        case Token::Space:
            if (cursor > 0)
                cursor += t.amount > 0 ? t.amount * size : spaceWidth;
            break;
        case Token::Word: {
            // la punteggiatura attaccata a una formula: un filo di spazio, se no la virgola tocca l'indice
            const Line &line = lines.last();
            if (!line.items.isEmpty() && m_tokens[line.items.last().token].type == Token::Math
                && std::fabs(cursor - (line.items.last().x + line.items.last().w)) < 0.01 && !t.text.isEmpty()
                && QStringLiteral(",.;:!?)").contains(t.text[0]))
                cursor += size * 0.1;
            place(k, measureWord(t), {});
            break;
        }
        case Token::Math: {
            const math::Size m = math::measure(t.text, msize, false);
            place(k, m.ok ? m.width : QFontMetricsF(font(false, false)).horizontalAdvance("$" + t.text + "$"), m);
            break;
        }
        case Token::Display: {
            newLine(size * 0.6);
            math::Size m = math::measure(t.text, msize, true);
            qreal scale = 1;
            const qreal room = width - indent - size * 0.6;
            if (m.ok && m.width > room && room > 0) {  // più larga della riga: rimpicciolita fin che ci sta
                scale = std::max(0.5, room / m.width);
                const math::Size small = math::measure(t.text, msize * scale, true);
                if (small.ok)
                    m = small;
            }
            lines.last().display = true;
            lines.last().items << Placed{k, 0, m.ok ? m.width : QFontMetricsF(font(false, false)).horizontalAdvance(t.text), m, scale};
            newLine(size * 0.6);
            break;
        }
        case Token::Break:
            newLine();
            break;
        case Token::Para:
            newLine(size * 0.65);
            break;
        case Token::VSpace:
            newLine(size * t.amount * 1.2);
            break;
        case Token::ListBegin:
            indent += size * 1.4;
            newLine(size * 0.15);
            break;
        case Token::ListEnd:
            indent = std::max<qreal>(0, indent - size * 1.4);
            newLine(size * 0.15);
            break;
        case Token::Item:
            newLine(size * 0.1);
            lines.last().label = t.text;
            break;
        }
        if (t.type != Token::Word && t.type != Token::Math)
            unit = lines.last().items.size();          // dopo uno spazio: una parola nuova
    }

    // le righe con qualcosa dentro; le formule possono essere più alte del testo
    QList<Line> used;
    for (Line &line : lines)
        if (!line.items.isEmpty() || !line.label.isEmpty())
            used << line;
    qreal h = 0, wide = 0;
    for (int n = 0; n < used.size(); ++n) {
        Line &line = used[n];
        line.ascent = textAscent;
        line.descent = textDescent;
        for (const Placed &p : line.items) {
            const Token::Type type = m_tokens[p.token].type;
            if ((type == Token::Math || type == Token::Display) && p.m.ok) {
                line.ascent = std::max(line.ascent, p.m.ascent);
                line.descent = std::max(line.descent, p.m.height - p.m.ascent);
            }
        }
        if (n > 0)
            h += line.gap + lineGap;
        h += line.ascent + line.descent;
        const qreal end = line.items.isEmpty() ? 0 : line.items.last().x + line.items.last().w;
        wide = std::max(wide, line.indent + (line.display ? (line.items.isEmpty() ? 0 : line.items.first().w) : end));
    }
    lines = used;
    if (widest)
        *widest = wide;
    return h;
}

void LatexText::paint(QPainter *painter)
{
    if (!qFuzzyCompare(m_laidWidth, width()) && width() > 0) {
        m_lines.clear();
        layout(width(), m_lines, nullptr);
        m_laidWidth = width();
    }
    painter->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    const qreal size = m_fontSize, msize = mathSize();
    const qreal lineGap = size * 0.28;
    qreal y = 0;
    for (int n = 0; n < m_lines.size(); ++n) {
        const Line &line = m_lines[n];
        if (n > 0)
            y += line.gap + lineGap;
        const qreal base = y + line.ascent;
        const qreal left = line.indent;
        if (!line.label.isEmpty()) {
            painter->setFont(font(false, false));
            painter->setPen(m_color);
            const qreal w = QFontMetricsF(font(false, false)).horizontalAdvance(line.label);
            painter->drawText(QPointF(left - w - size * 0.4, base), line.label);
        }
        for (const Placed &p : line.items) {
            const Token &t = m_tokens[p.token];
            const qreal x = line.display ? left + std::max<qreal>(0, (width() - left - p.w) / 2) : left + p.x;
            if (t.type == Token::Word) {
                painter->setFont(font(t.bold, t.italic));
                painter->setPen(m_color);
                painter->drawText(QPointF(x, base), t.text);
            } else if (p.m.ok) {
                math::draw(painter, t.text, x, base, msize * p.scale, m_color, t.type == Token::Display);
            } else {                                   // non si legge: il sorgente, così com'è
                painter->setFont(font(false, false));
                painter->setPen(m_broken);
                painter->drawText(QPointF(x, base), t.type == Token::Math ? "$" + t.text + "$" : t.text);
            }
        }
        y += line.ascent + line.descent;
    }
}
