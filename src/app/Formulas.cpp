// Le formule: MicroTeX sopra QPainter, come Studify lo mette sopra raylib. Font, TextLayout e Graphics2D di MicroTeX
// fatti con QRawFont e QPainter; i glifi si disegnano alla loro dimensione vera (non rimpiccioliti e poi ingranditi:
// sarebbero sfocati), il resto con la trasformazione di MicroTeX. Una memoria delle formule lette.
#include "Formulas.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGlyphRun>
#include <QGuiApplication>
#include <QHash>
#include <QPainter>
#include <QRawFont>

#include <cmath>
#include <memory>

#include "core/formula.h"
#include "latex.h"
#include "render.h"

namespace {

// I font: quelli di MicroTeX dai loro file, il testo dentro le formule (\text) con il carattere dell'interfaccia
QRawFont rawFont(const QString &file, bool bold)
{
    static QHash<QString, QRawFont> loaded;
    const QString key = file + (bold ? "#b" : "");
    auto it = loaded.find(key);
    if (it == loaded.end()) {
        QRawFont font;
        if (file.isEmpty()) {
            QFont ui = QGuiApplication::font();
            ui.setBold(bold);
            ui.setPixelSize(64);
            font = QRawFont::fromFont(ui);
        } else {
            font = QRawFont(file, 64);
        }
        it = loaded.insert(key, font);
    }
    return it.value();
}

class QtFont final : public tex::Font {
public:
    QtFont(QString file, float size, int style = tex::PLAIN) : m_file(std::move(file)), m_size(size), m_style(style) {}
    float getSize() const override { return m_size; }
    tex::sptr<tex::Font> deriveFont(int style) const override { return std::make_shared<QtFont>(m_file, m_size, style); }
    bool operator==(const tex::Font &other) const override
    {
        const auto &o = static_cast<const QtFont &>(other);
        return o.m_file == m_file && o.m_size == m_size && o.m_style == m_style;
    }
    bool operator!=(const tex::Font &other) const override { return !(*this == other); }
    // il font alla dimensione `pixels`
    QRawFont at(qreal pixels) const
    {
        QRawFont f = rawFont(m_file, m_file.isEmpty() && (m_style & tex::BOLD));
        f.setPixelSize(pixels);
        return f;
    }

private:
    QString m_file;                                    // vuoto: il carattere dell'interfaccia
    float m_size;
    int m_style;
};

class QtGraphics final : public tex::Graphics2D {
public:
    explicit QtGraphics(QPainter *painter) : m_painter(painter), m_base(painter->transform()) {}

    void setColor(tex::color c) override { m_color = c; }
    tex::color getColor() const override { return m_color; }
    void setStroke(const tex::Stroke &s) override { m_stroke = s; }
    const tex::Stroke &getStroke() const override { return m_stroke; }
    void setStrokeWidth(float w) override { m_stroke.lineWidth = w; }
    const tex::Font *getFont() const override { return m_font; }
    void setFont(const tex::Font *f) override { m_font = static_cast<const QtFont *>(f); }

    void translate(float dx, float dy) override { m_local.translate(dx, dy); }
    void scale(float sx, float sy) override
    {
        m_local.scale(sx, sy);
        m_sx *= sx, m_sy *= sy;
    }
    void rotate(float angle) override { m_local.rotateRadians(angle); }
    void rotate(float angle, float px, float py) override
    {
        m_local.translate(px, py);
        m_local.rotateRadians(angle);
        m_local.translate(-px, -py);
    }
    void reset() override
    {
        m_local.reset();
        m_sx = m_sy = 1;
    }
    float sx() const override { return m_sx; }
    float sy() const override { return m_sy; }

    void drawChar(wchar_t c, float x, float y) override { glyphs(QString::fromStdWString(std::wstring(1, c)), x, y); }
    void drawText(const std::wstring &t, float x, float y) override { glyphs(QString::fromStdWString(t), x, y); }
    void drawLine(float x1, float y1, float x2, float y2) override
    {
        prepare(false);
        m_painter->drawLine(QPointF(x1, y1), QPointF(x2, y2));
    }
    void drawRect(float x, float y, float w, float h) override
    {
        prepare(false);
        m_painter->drawRect(QRectF(x, y, w, h));
    }
    void fillRect(float x, float y, float w, float h) override
    {
        // le linee di frazione sottili restano visibili: almeno un pixel vero
        const qreal scale = std::hypot(m_local.m12(), m_local.m22()) * std::hypot(m_base.m12(), m_base.m22());
        const qreal least = scale > 0 ? 1.0 / scale : 0;
        prepare(true);
        m_painter->fillRect(QRectF(x, y, std::max<qreal>(w, least), std::max<qreal>(h, least)), qcolor());
    }
    void drawRoundRect(float x, float y, float w, float h, float rx, float ry) override
    {
        prepare(false);
        m_painter->drawRoundedRect(QRectF(x, y, w, h), rx, ry);
    }
    void fillRoundRect(float x, float y, float w, float h, float rx, float ry) override
    {
        prepare(true);
        m_painter->drawRoundedRect(QRectF(x, y, w, h), rx, ry);
    }
    void finish() { m_painter->setTransform(m_base); }
    // per TextLayout: il testo con un font dato
    void text(const QString &text, const QtFont &font, float x, float y)
    {
        const QtFont *old = m_font;
        m_font = &font;
        glyphs(text, x, y);
        m_font = old;
    }

private:
    QColor qcolor() const { return QColor(tex::color_r(m_color), tex::color_g(m_color), tex::color_b(m_color), tex::color_a(m_color)); }
    void prepare(bool fill)
    {
        m_painter->setTransform(m_local * m_base);
        QPen pen(qcolor(), m_stroke.lineWidth);
        pen.setCapStyle(m_stroke.cap == tex::CAP_ROUND ? Qt::RoundCap : m_stroke.cap == tex::CAP_SQUARE ? Qt::SquareCap : Qt::FlatCap);
        m_painter->setPen(fill ? QPen(Qt::NoPen) : pen);
        m_painter->setBrush(fill ? QBrush(qcolor()) : QBrush());
    }
    // i glifi alla loro dimensione vera: la trasformazione di MicroTeX dà il punto e la scala, il font si prende a
    // quella scala e si disegna senza ingrandire
    void glyphs(const QString &text, float x, float y)
    {
        if (!m_font || text.isEmpty())
            return;
        const qreal scale = std::hypot(m_local.m21(), m_local.m22());
        const QRawFont font = m_font->at(m_font->getSize() * scale);
        if (!font.isValid())
            return;
        const QList<quint32> indexes = font.glyphIndexesForString(text);
        const QList<QPointF> advances = font.advancesForGlyphIndexes(indexes);
        QList<QPointF> positions;
        QPointF at;
        for (const QPointF &a : advances) {
            positions << at;
            at += a;
        }
        QGlyphRun run;
        run.setRawFont(font);
        run.setGlyphIndexes(indexes);
        run.setPositions(positions);
        const QPointF origin = m_local.map(QPointF(x, y));
        QTransform place;
        place.translate(origin.x(), origin.y());
        place.rotateRadians(std::atan2(m_local.m12(), m_local.m11()));
        m_painter->setTransform(place * m_base);
        m_painter->setPen(qcolor());
        m_painter->drawGlyphRun(QPointF(0, 0), run);
    }

    QPainter *m_painter;
    QTransform m_base, m_local;
    float m_sx = 1, m_sy = 1;
    tex::color m_color = tex::black;
    tex::Stroke m_stroke;
    const QtFont *m_font = nullptr;
};

class QtTextLayout final : public tex::TextLayout {
public:
    QtTextLayout(const std::wstring &text, const tex::sptr<tex::Font> &font)
        : m_text(QString::fromStdWString(text)), m_font(std::static_pointer_cast<QtFont>(font)) {}
    void getBounds(tex::Rect &r) override
    {
        const QRawFont f = m_font->at(m_font->getSize());
        qreal width = 0;
        for (const QPointF &a : f.advancesForGlyphIndexes(f.glyphIndexesForString(m_text)))
            width += a.x();
        r = tex::Rect(0, -float(f.ascent()), float(width), float(f.ascent() + f.descent()));
    }
    void draw(tex::Graphics2D &g2, float x, float y) override { static_cast<QtGraphics &>(g2).text(m_text, *m_font, x, y); }

private:
    QString m_text;
    tex::sptr<QtFont> m_font;
};

// Le risorse di MicroTeX: nel pacchetto, accanto al programma, o quelle scaricate
QString resources()
{
    const QDir app(QCoreApplication::applicationDirPath());
    for (const QString &dir : {app.filePath("../Resources/microtex"), app.filePath("microtex"), QStringLiteral(TEXFIX_MICROTEX_RES)})
        if (QFileInfo::exists(dir + "/fonts"))
            return QDir::cleanPath(dir);
    return {};
}

bool ready()
{
    static const bool ok = [] {
        const QString dir = resources();
        if (dir.isEmpty())
            return false;
        try {
            tex::LaTeX::init(dir.toStdString());
            return true;
        } catch (const std::exception &) {
            return false;
        }
    }();
    return ok;
}

// Le macro degli appunti e i simboli Unicode del modello che MicroTeX non conosce; nelle formule a sé i limiti sotto
// \lim, \max… come in TeX
QString expand(const QString &in, bool display)
{
    static const QHash<QChar, QString> unicode{
        {u'α', "\\alpha "}, {u'β', "\\beta "}, {u'γ', "\\gamma "}, {u'δ', "\\delta "}, {u'ε', "\\varepsilon "}, {u'ζ', "\\zeta "},
        {u'η', "\\eta "}, {u'θ', "\\theta "}, {u'λ', "\\lambda "}, {u'μ', "\\mu "}, {u'ν', "\\nu "}, {u'ξ', "\\xi "}, {u'π', "\\pi "},
        {u'ρ', "\\rho "}, {u'σ', "\\sigma "}, {u'τ', "\\tau "}, {u'φ', "\\varphi "}, {u'χ', "\\chi "}, {u'ψ', "\\psi "}, {u'ω', "\\omega "},
        {u'Γ', "\\Gamma "}, {u'Δ', "\\Delta "}, {u'Θ', "\\Theta "}, {u'Λ', "\\Lambda "}, {u'Π', "\\Pi "}, {u'Σ', "\\Sigma "},
        {u'Φ', "\\Phi "}, {u'Ψ', "\\Psi "}, {u'Ω', "\\Omega "}, {u'∞', "\\infty "}, {u'→', "\\to "}, {u'≤', "\\le "}, {u'≥', "\\ge "},
        {u'≠', "\\ne "}, {u'±', "\\pm "}, {u'·', "\\cdot "}, {u'×', "\\times "}, {u'∈', "\\in "}, {u'≈', "\\approx "},
        {u'≃', "\\simeq "}, {u'∼', "\\sim "}, {u'≫', "\\gg "}, {u'≪', "\\ll "}, {u'−', "-"}, {u'∂', "\\partial "}, {u'∇', "\\nabla "},
        {u'∥', "\\parallel "}, {u'⇒', "\\Rightarrow "}, {u'⇔', "\\Leftrightarrow "}, {u'∑', "\\sum "}, {u'∫', "\\int "},
        {u'√', "\\surd "}, {u'′', "'"}, {u'ℏ', "\\hbar "}, {u'ℓ', "\\ell "}, {u'∝', "\\propto "}, {u'≡', "\\equiv "}, {u'µ', "\\mu "}};
    auto group = [&](qsizetype &i) {                   // l'argomento in in[i]: {gruppo} o un carattere
        while (i < in.size() && in[i] == u' ')
            ++i;
        if (i < in.size() && in[i] == u'{') {
            int depth = 0;
            const qsizetype from = i;
            for (; i < in.size(); ++i) {
                if (in[i] == u'{')
                    ++depth;
                else if (in[i] == u'}' && --depth == 0)
                    break;
            }
            ++i;
            return in.mid(from + 1, i - from - 2);
        }
        return i < in.size() ? QString(in[i++]) : QString();
    };
    QString out;
    int depth = 0;
    for (qsizetype i = 0; i < in.size();) {
        const QChar c = in[i];
        if (const auto u = unicode.constFind(c); u != unicode.constEnd()) {
            out += u.value();
            ++i;
            continue;
        }
        if (c == u'{')
            ++depth;
        if (c == u'}')
            --depth;
        if (c != u'\\' || i + 1 >= in.size() || !in[i + 1].isLetter()) {
            out += c;
            if (c == u'\\' && i + 1 < in.size())
                out += in[++i];
            ++i;
            continue;
        }
        qsizetype j = i + 1;
        while (j < in.size() && in[j].isLetter())
            ++j;
        const QString cmd = in.mid(i + 1, j - i - 1);
        if (cmd == "R" || cmd == "N" || cmd == "Z" || cmd == "Q" || cmd == "C") {
            out += "\\mathbb{" + cmd + "}";
            i = j;
        } else if (cmd == "norm" || cmd == "abs") {
            const QString a = expand(group(j), display);
            out += cmd == "norm" ? "\\left\\|" + a + "\\right\\|" : "\\left|" + a + "\\right|";
            i = j;
        } else if (cmd == "vv" || cmd == "bm") {
            const QString a = group(j);
            out += (cmd == "vv" && !a.startsWith(u'\\') ? "\\mathbf{" : "\\boldsymbol{") + expand(a, display) + "}";
            i = j;
        } else if (cmd == "dd" || cmd == "diff") {
            out += "\\mathrm{d}";
            i = j;
        } else if (cmd == "rot" || cmd == "diver" || cmd == "grad" || cmd == "Erf" || cmd == "sgn" || cmd == "tr") {
            out += "\\operatorname{" + (cmd == "diver" ? QStringLiteral("div") : cmd) + "}";
            i = j;
        } else if (cmd == "dfrac" || cmd == "tfrac") {
            out += cmd == "dfrac" ? "\\displaystyle\\frac" : "\\frac";
            i = j;
        } else {
            out += in.mid(i, j - i);
            i = j;
            static const QStringList ops{"lim", "max", "min", "sup", "inf", "limsup", "liminf"};
            qsizetype k = i;
            while (k < in.size() && in[k] == u' ')
                ++k;
            if (display && ops.contains(cmd) && depth == 0 && k < in.size() && in[k] == u'_')
                out += "\\limits";
        }
    }
    return out;
}

// la memoria delle formule lette: per dimensione, stile e testo
std::shared_ptr<tex::TeXRender> get(const QString &latex, qreal size, bool display)
{
    if (!ready())
        return nullptr;
    static QHash<QString, std::shared_ptr<tex::TeXRender>> formulas;
    const QString key = QString::number(qRound(size * 10)) + (display ? u'D' : u'T') + latex;
    if (const auto it = formulas.constFind(key); it != formulas.constEnd())
        return it.value();
    if (formulas.size() > 1500)
        formulas.clear();
    std::shared_ptr<tex::TeXRender> render;
    try {
        tex::Formula formula;
        formula.setLaTeX(expand(latex, display).toStdWString());
        tex::TeXRenderBuilder builder;
        render.reset(builder.setStyle(display ? tex::TexStyle::display : tex::TexStyle::text).setTextSize(float(size)).build(formula));
        render->setInsets(tex::Insets(), true);        // senza il bordo che MicroTeX mette intorno: si allinea al testo
    } catch (const std::exception &) {
        render.reset();
    }
    formulas.insert(key, render);
    return render;
}

}

namespace math {

Size measure(const QString &latex, qreal size, bool display)
{
    Size s;
    const auto r = get(latex, size, display);
    if (!r)
        return s;
    s.ok = true;
    s.width = r->getWidth();
    s.height = r->getHeight();
    s.ascent = s.height - r->getDepth();
    return s;
}

void draw(QPainter *painter, const QString &latex, qreal x, qreal baseline, qreal size, const QColor &color, bool display)
{
    const auto r = get(latex, size, display);
    if (!r)
        return;
    painter->save();
    QtGraphics g(painter);
    r->setForeground(tex::argb(color.alpha(), color.red(), color.green(), color.blue()));
    const qreal top = baseline - (r->getHeight() - r->getDepth());
    // draw vuole interi: la parte frazionaria va nella trasformazione
    const qreal ix = std::floor(x), iy = std::floor(top);
    g.translate(float(x - ix), float(top - iy));
    r->draw(g, int(ix), int(iy));
    g.finish();
    painter->restore();
}

}

// Le fabbriche che MicroTeX chiama
namespace tex {

Font *Font::create(const std::string &file, float size) { return new QtFont(QString::fromStdString(file), size); }

sptr<Font> Font::_create(const std::string &, int style, float size)
{
    // Serif, SansSerif…: il testo nelle formule usa il carattere dell'interfaccia
    return std::make_shared<QtFont>(QString(), size, style);
}

sptr<TextLayout> TextLayout::create(const std::wstring &src, const sptr<Font> &font) { return std::make_shared<QtTextLayout>(src, font); }

}
