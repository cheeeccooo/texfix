#include "core/Snippets.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

// Il pezzo da leggere subito: LaTeX in testo ricco di Qt. Non è un compilatore: ciò che non conosce lo lascia cadere
// e ne tiene gli argomenti, così il testo resta tutto.
namespace tx::snippets {

namespace {

const QHash<QString, QString> &symbols()
{
    static const QHash<QString, QString> map{
        {"alpha", "α"}, {"beta", "β"}, {"gamma", "γ"}, {"delta", "δ"}, {"epsilon", "ϵ"}, {"varepsilon", "ε"}, {"zeta", "ζ"},
        {"eta", "η"}, {"theta", "θ"}, {"vartheta", "ϑ"}, {"iota", "ι"}, {"kappa", "κ"}, {"lambda", "λ"}, {"mu", "μ"}, {"nu", "ν"},
        {"xi", "ξ"}, {"pi", "π"}, {"varpi", "ϖ"}, {"rho", "ρ"}, {"varrho", "ϱ"}, {"sigma", "σ"}, {"varsigma", "ς"}, {"tau", "τ"},
        {"upsilon", "υ"}, {"phi", "ϕ"}, {"varphi", "φ"}, {"chi", "χ"}, {"psi", "ψ"}, {"omega", "ω"}, {"Gamma", "Γ"}, {"Delta", "Δ"},
        {"Theta", "Θ"}, {"Lambda", "Λ"}, {"Xi", "Ξ"}, {"Pi", "Π"}, {"Sigma", "Σ"}, {"Upsilon", "Υ"}, {"Phi", "Φ"}, {"Psi", "Ψ"},
        {"Omega", "Ω"}, {"infty", "∞"}, {"partial", "∂"}, {"nabla", "∇"}, {"hbar", "ℏ"}, {"ell", "ℓ"}, {"Re", "ℜ"}, {"Im", "ℑ"},
        {"aleph", "ℵ"}, {"emptyset", "∅"}, {"varnothing", "∅"}, {"forall", "∀"}, {"exists", "∃"}, {"nexists", "∄"}, {"neg", "¬"},
        {"lnot", "¬"}, {"sum", "∑"}, {"prod", "∏"}, {"coprod", "∐"}, {"int", "∫"}, {"iint", "∬"}, {"iiint", "∭"}, {"oint", "∮"},
        {"bigcup", "⋃"}, {"bigcap", "⋂"}, {"bigoplus", "⨁"}, {"bigotimes", "⨂"}, {"cdot", "·"}, {"cdots", "⋯"}, {"ldots", "…"},
        {"dots", "…"}, {"dotsc", "…"}, {"dotsb", "⋯"}, {"vdots", "⋮"}, {"ddots", "⋱"}, {"times", "×"}, {"div", "÷"}, {"pm", "±"},
        {"mp", "∓"}, {"ast", "∗"}, {"star", "⋆"}, {"circ", "∘"}, {"bullet", "•"}, {"oplus", "⊕"}, {"ominus", "⊖"}, {"otimes", "⊗"},
        {"odot", "⊙"}, {"wedge", "∧"}, {"land", "∧"}, {"vee", "∨"}, {"lor", "∨"}, {"cap", "∩"}, {"cup", "∪"}, {"setminus", "∖"},
        {"leq", "≤"}, {"le", "≤"}, {"geq", "≥"}, {"ge", "≥"}, {"leqslant", "⩽"}, {"geqslant", "⩾"}, {"neq", "≠"}, {"ne", "≠"},
        {"approx", "≈"}, {"simeq", "≃"}, {"sim", "∼"}, {"cong", "≅"}, {"equiv", "≡"}, {"propto", "∝"}, {"ll", "≪"}, {"gg", "≫"},
        {"lesssim", "≲"}, {"gtrsim", "≳"}, {"prec", "≺"}, {"succ", "≻"}, {"in", "∈"}, {"notin", "∉"}, {"ni", "∋"}, {"subset", "⊂"},
        {"supset", "⊃"}, {"subseteq", "⊆"}, {"supseteq", "⊇"}, {"perp", "⊥"}, {"parallel", "∥"}, {"mid", "∣"}, {"nmid", "∤"},
        {"to", "→"}, {"rightarrow", "→"}, {"leftarrow", "←"}, {"gets", "←"}, {"leftrightarrow", "↔"}, {"Rightarrow", "⇒"},
        {"Leftarrow", "⇐"}, {"Leftrightarrow", "⇔"}, {"implies", "⟹"}, {"iff", "⟺"}, {"mapsto", "↦"}, {"longrightarrow", "⟶"},
        {"longmapsto", "⟼"}, {"uparrow", "↑"}, {"downarrow", "↓"}, {"rightleftharpoons", "⇌"}, {"langle", "⟨"}, {"rangle", "⟩"},
        {"lfloor", "⌊"}, {"rfloor", "⌋"}, {"lceil", "⌈"}, {"rceil", "⌉"}, {"vert", "|"}, {"Vert", "‖"}, {"lvert", "|"}, {"rvert", "|"},
        {"lVert", "‖"}, {"rVert", "‖"}, {"|", "‖"}, {"angle", "∠"}, {"degree", "°"}, {"prime", "′"}, {"surd", "√"}, {"top", "⊤"},
        {"bot", "⊥"}, {"vdash", "⊢"}, {"models", "⊨"}, {"triangle", "△"}, {"square", "□"}, {"Box", "□"}, {"checkmark", "✓"},
        {"S", "§"}, {"P", "¶"}, {"dag", "†"}, {"copyright", "©"}, {"textbackslash", "\\"}, {"textasciitilde", "~"},
        {"textasciicircum", "^"}, {"textendash", "–"}, {"textemdash", "—"}, {"textdegree", "°"}, {"euro", "€"}, {"EUR", "€"},
        {"ss", "ß"}, {"o", "ø"}, {"O", "Ø"}, {"ae", "æ"}, {"AE", "Æ"}, {"aa", "å"}, {"i", "ı"}, {"l", "ł"}, {"LaTeX", "LaTeX"}, {"TeX", "TeX"},
        {"quad", " "}, {"qquad", "  "}, {"enspace", " "}, {"ohm", "Ω"}, {"micro", "µ"}, {"celsius", "°C"}};
    return map;
}

const QSet<QString> &functions()
{
    static const QSet<QString> names{"sin", "cos", "tan", "cot", "sec", "csc", "arcsin", "arccos", "arctan", "sinh", "cosh", "tanh",
                                     "coth", "log", "ln", "lg", "exp", "lim", "limsup", "liminf", "sup", "inf", "max", "min", "det", "dim",
                                     "ker", "deg", "gcd", "arg", "Pr", "mod", "bmod", "hom", "tr", "rank", "sgn", "span", "Tr", "id"};
    return names;
}

// i comandi che non si vedono, con quanti argomenti si portano via
const QHash<QString, int> &silent()
{
    static const QHash<QString, int> map{
        {"label", 1}, {"index", 1}, {"vspace", 1}, {"hspace", 1}, {"phantom", 1}, {"hphantom", 1}, {"vphantom", 1}, {"input", 1},
        {"include", 1}, {"enlargethispage", 1}, {"setlength", 2}, {"addtolength", 2}, {"setcounter", 2}, {"addtocounter", 2},
        {"nonumber", 0}, {"notag", 0}, {"linebreak", 0}, {"nolinebreak", 0}, {"newpage", 0}, {"pagebreak", 0}, {"clearpage", 0},
        {"noindent", 0}, {"indent", 0}, {"centering", 0}, {"raggedright", 0}, {"raggedleft", 0}, {"displaystyle", 0},
        {"textstyle", 0}, {"scriptstyle", 0}, {"limits", 0}, {"nolimits", 0}, {"protect", 0}, {"relax", 0}, {"tcbbreak", 0},
        {"left", 0}, {"right", 0}, {"middle", 0}, {"big", 0}, {"Big", 0}, {"bigg", 0}, {"Bigg", 0}, {"bigl", 0}, {"bigr", 0},
        {"Bigl", 0}, {"Bigr", 0}, {"biggl", 0}, {"biggr", 0}, {"Biggl", 0}, {"Biggr", 0}, {"small", 0}, {"footnotesize", 0},
        {"scriptsize", 0}, {"tiny", 0}, {"normalsize", 0}, {"large", 0}, {"Large", 0}, {"LARGE", 0}, {"huge", 0}, {"Huge", 0},
        {"normalfont", 0}, {"bfseries", 0}, {"itshape", 0}, {"rmfamily", 0}, {"sffamily", 0}, {"ttfamily", 0}, {"upshape", 0},
        {"selectfont", 0}, {"allowbreak", 0}, {"hfill", 0}, {"vfill", 0}, {"null", 0}, {"leavevmode", 0}, {"makebox", 0},
        {"maketitle", 0}, {"bf", 0}, {"it", 0}, {"em", 0}, {"rm", 0}, {"sf", 0}, {"tt", 0}, {"tableofcontents", 0}, {"hline", 0}, {"toprule", 0}, {"midrule", 0}, {"bottomrule", 0}, {"cline", 1}};
    return map;
}

const QSet<QString> &mathEnvironments()
{
    static const QSet<QString> names{"equation", "equation*", "align", "align*", "gather", "gather*", "multline", "multline*",
                                     "eqnarray", "eqnarray*", "displaymath", "flalign", "flalign*", "alignat", "alignat*", "math"};
    return names;
}

QString escaped(QChar c)
{
    if (c == u'&')
        return QStringLiteral("&amp;");
    if (c == u'<')
        return QStringLiteral("&lt;");
    if (c == u'>')
        return QStringLiteral("&gt;");
    return QString(c);
}

class Reader
{
public:
    explicit Reader(bool comments) : m_comments(comments) {}

    // un tratto di testo; `math` dentro una formula, `upright` dentro \mathrm e simili
    QString html(const QString &s, bool math, bool upright = false, int *items = nullptr)
    {
        QString out;
        qsizetype i = 0;
        while (i < s.size()) {
            const QChar c = s[i];
            if (c == u'\\') {
                out += command(s, i, math, upright, items);
                continue;
            }
            if (c == u'{') {
                out += html(group(s, i), math, upright, items);
                continue;
            }
            if (c == u'}') {
                ++i;
                continue;
            }
            if (!math && c == u'$') {                  // una formula: $…$ o $$…$$
                const bool display = i + 1 < s.size() && s[i + 1] == u'$';
                const qsizetype from = i + (display ? 2 : 1);
                qsizetype end = from;
                while (end < s.size() && !(s[end] == u'$' && s[end - 1] != u'\\'))
                    ++end;
                const QString inner = s.mid(from, end - from);
                i = qMin(s.size(), end + (display ? 2 : 1));
                out += display ? block(html(inner, true)) : html(inner, true);
                continue;
            }
            if (c == u'%' && m_comments) {             // un commento, fino alla fine della riga
                while (i < s.size() && s[i] != u'\n')
                    ++i;
                ++i;
                continue;
            }
            if (math && (c == u'_' || c == u'^')) {
                ++i;
                const QString inner = html(argument(s, i), true, upright);
                out += c == u'_' ? "<sub>" + inner + "</sub>" : "<sup>" + inner + "</sup>";
                continue;
            }
            if (math) {
                ++i;
                if (c.isLetter() && c.unicode() < 0x370 && !upright) {
                    out += "<i>" + QString(c) + "</i>";
                } else if (c == u'-') {
                    out += u'−';
                } else if (c == u'\'') {
                    out += u'′';
                } else if (c == u'*') {
                    out += u'∗';
                } else if (c == u'&') {
                    out += QStringLiteral(" ");    // l'allineamento
                } else if (c == u'=' || c == u'<' || c == u'>') {
                    out += " " + escaped(c) + " ";
                } else if (c == u'~') {
                    out += u' ';
                } else if (!c.isSpace()) {
                    out += escaped(c);
                }
                continue;
            }
            // il testo
            if (c == u'\n') {
                qsizetype j = i + 1;
                while (j < s.size() && (s[j] == u' ' || s[j] == u'\t'))
                    ++j;
                if (j < s.size() && s[j] == u'\n') {   // una riga vuota: un capoverso
                    while (j < s.size() && s[j].isSpace())
                        ++j;
                    out += "<br><br>";
                    i = j;
                } else {
                    out += u' ';
                    ++i;
                }
                continue;
            }
            if (c == u'~' && m_comments) {
                out += u' ';
                ++i;
            } else if (s.mid(i, 3) == "---") {
                out += u'—';
                i += 3;
            } else if (s.mid(i, 2) == "--") {
                out += u'–';
                i += 2;
            } else if (s.mid(i, 2) == "``") {
                out += u'“';
                i += 2;
            } else if (s.mid(i, 2) == "''") {
                out += u'”';
                i += 2;
            } else {
                out += escaped(c);
                ++i;
            }
        }
        return out;
    }

private:
    bool m_comments;

    static QString block(const QString &inner) { return "<div align=\"center\">" + inner + "</div>"; }

    // il contenuto di {…} che comincia in s[i]; i va dopo la graffa che lo chiude
    static QString group(const QString &s, qsizetype &i)
    {
        int depth = 0;
        const qsizetype from = i + 1;
        for (; i < s.size(); ++i) {
            if (s[i] == u'\\') {
                ++i;
                continue;
            }
            if (s[i] == u'{')
                ++depth;
            else if (s[i] == u'}' && --depth == 0)
                break;
        }
        const QString inner = s.mid(from, i - from);
        i = qMin(s.size(), i + 1);
        return inner;
    }
    // un argomento: un gruppo, un comando o un carattere
    static QString argument(const QString &s, qsizetype &i)
    {
        while (i < s.size() && s[i].isSpace())
            ++i;
        if (i >= s.size())
            return {};
        if (s[i] == u'{')
            return group(s, i);
        if (s[i] == u'\\') {
            const qsizetype from = i++;
            if (i < s.size() && s[i].isLetter())
                while (i < s.size() && s[i].isLetter())
                    ++i;
            else
                ++i;
            return s.mid(from, i - from);
        }
        return s.mid(i++, 1);
    }
    // un argomento facoltativo […], se c'è
    static QString optional(const QString &s, qsizetype &i)
    {
        qsizetype j = i;
        while (j < s.size() && s[j].isSpace())
            ++j;
        if (j >= s.size() || s[j] != u'[')
            return {};
        int depth = 0;
        const qsizetype from = j + 1;
        for (; j < s.size(); ++j) {
            if (s[j] == u'{')
                ++depth;
            else if (s[j] == u'}')
                --depth;
            else if (s[j] == u']' && depth == 0)
                break;
        }
        i = qMin(s.size(), j + 1);
        return s.mid(from, j - from);
    }
    // il corpo di un ambiente, fino al suo \end (anche con lo stesso ambiente dentro)
    static QString body(const QString &s, qsizetype &i, const QString &name)
    {
        const QString open = "\\begin{" + name + "}", close = "\\end{" + name + "}";
        int depth = 1;
        const qsizetype from = i;
        qsizetype j = i;
        while (j < s.size()) {
            if (s.mid(j, open.size()) == open) {
                ++depth;
                j += open.size();
            } else if (s.mid(j, close.size()) == close) {
                if (--depth == 0)
                    break;
                j += close.size();
            } else {
                ++j;
            }
        }
        i = qMin(s.size(), j + close.size());
        return s.mid(from, j - from);
    }
    // un argomento che non chiede parentesi: una lettera, un numero, un comando, o ciò che resta un pezzo solo una volta
    // scritto (√n, x², α₀), senza operatori né spazi
    static bool simple(const QString &arg, const QString &shown)
    {
        static const QRegularExpression one(R"(^\s*(\\[A-Za-z]+|[A-Za-z]|[0-9.]+)\s*$)");
        if (one.match(arg).hasMatch())
            return true;
        static const QRegularExpression tags("<[^>]*>"), operators(QStringLiteral("[\\s+−=/·×<>≤≥,]|&lt;|&gt;"));
        return !QString(shown).remove(tags).contains(operators);
    }
    // un accento: il segno combinante subito dopo la lettera, anche dentro il suo <i>
    static QString accent(const QString &inner, QChar mark)
    {
        if (inner.endsWith("</i>"))
            return inner.chopped(4) + mark + "</i>";
        return inner + mark;
    }

    QString environment(const QString &s, qsizetype &i, bool math, bool upright, const QString &name)
    {
        const QString base = QString(name).remove(u'*');
        if (mathEnvironments().contains(name)) {
            const QString inner = body(s, i, name);
            return block(html(QString(inner).replace("\\\\", "\n\\newline "), true));
        }
        if (QStringList{"pmatrix", "bmatrix", "Bmatrix", "vmatrix", "matrix", "smallmatrix", "cases", "aligned", "split", "gathered", "array", "alignedat"}.contains(base)) {
            if (base == "array" || base == "alignedat")
                argument(s, i);                        // le colonne
            const QString rows = html(body(s, i, name).replace("\\\\", "; "), true, upright);
            if (base == "pmatrix")
                return "(" + rows + ")";
            if (base == "bmatrix")
                return "[" + rows + "]";
            if (base == "vmatrix")
                return "|" + rows + "|";
            if (base == "cases")
                return "{ " + rows;
            return rows;
        }
        if (base == "itemize" || base == "enumerate" || base == "description") {
            int counter = base == "enumerate" ? 1 : 0;
            return html(body(s, i, name), math, upright, &counter) + "<br>";
        }
        if (QStringList{"tikzpicture", "circuitikz", "pgfpicture", "axis", "picture"}.contains(base)) {
            body(s, i, name);
            return "<br><i>[disegno]</i><br>";
        }
        if (base == "tabular" || base == "tabularx" || base == "longtable") {
            if (base == "tabularx")
                argument(s, i);
            argument(s, i);                            // le colonne
            return "<br>" + html(body(s, i, name).replace("\\\\", "\n\n").replace("&", " | "), false, upright) + "<br>";
        }
        const QString title = optional(s, i);
        const QString inner = html(body(s, i, name), math, upright, nullptr);
        if (QStringList{"center", "flushleft", "flushright", "minipage", "figure", "table", "quote", "quotation", "document", "varwidth"}.contains(base))
            return "<br>" + inner + "<br>";
        if (base == "proof")
            return "<br><i>Dimostrazione.</i> " + inner + "<br>";
        // un riquadro: il suo nome, il titolo se c'è
        QString label = base;
        label.replace(QRegularExpression("box$"), QString());
        if (!label.isEmpty())
            label[0] = label[0].toUpper();
        return "<br><b>" + label + (title.isEmpty() ? QString() : " (" + html(title, false) + ")") + ".</b> " + inner + "<br>";
    }

    QString command(const QString &s, qsizetype &i, bool math, bool upright, int *items)
    {
        ++i;                                           // la barra
        if (i >= s.size())
            return {};
        if (!s[i].isLetter()) {                        // un simbolo: \\, \%, \{, \,
            const QChar c = s[i++];
            if (c == u'\\') {
                optional(s, i);                        // «\\[2mm]»
                return "<br>";
            }
            if (c == u',' || c == u':' || c == u';' || c == u' ')
                return QStringLiteral(" ");
            if (c == u'!' || c == u'-' || c == u'/')
                return {};
            if (c == u'|')
                return QStringLiteral("‖");
            if (c == u'(' || c == u'[') {              // \( … \) e \[ … \]
                const QString close = c == u'(' ? QStringLiteral("\\)") : QStringLiteral("\\]");
                const qsizetype end = s.indexOf(close, i);
                const QString inner = s.mid(i, (end < 0 ? s.size() : end) - i);
                i = end < 0 ? s.size() : end + 2;
                return c == u'(' ? html(inner, true) : block(html(inner, true));
            }
            if (c == u')' || c == u']')
                return {};
            return escaped(c);
        }
        const qsizetype from = i;
        while (i < s.size() && s[i].isLetter())
            ++i;
        QString name = s.mid(from, i - from);
        if (i < s.size() && s[i] == u'*' && !math)     // \section*
            ++i;
        if (name == "newline")
            return "<br>";
        if (name == "begin") {
            const QString env = argument(s, i);
            return environment(s, i, math, upright, env);
        }
        if (name == "end") {
            argument(s, i);
            return "<br>";
        }
        if (name == "item") {
            const QString label = optional(s, i);
            while (i < s.size() && s[i].isSpace())
                ++i;
            if (!label.isEmpty())
                return "<br><b>" + html(label, false) + "</b> ";
            if (items && *items > 0)
                return "<br>" + QString::number((*items)++) + ". ";
            return "<br>• ";
        }
        if (const auto it = silent().constFind(name); it != silent().constEnd()) {
            optional(s, i);
            for (int n = 0; n < it.value(); ++n)
                argument(s, i);
            if (name == "left" || name == "right" || name == "middle" || name.startsWith("big") || name.startsWith("Big")) {
                qsizetype j = i;
                while (j < s.size() && s[j].isSpace())
                    ++j;
                if (j < s.size() && s[j] == u'.')      // «\left.»
                    i = j + 1;
            }
            return {};
        }
        if (const auto it = symbols().constFind(name); it != symbols().constEnd()) {
            if (math && (name == "leq" || name == "le" || name == "geq" || name == "ge" || name == "neq" || name == "ne" || name == "approx"
                         || name == "simeq" || name == "sim" || name == "equiv" || name == "to" || name == "rightarrow" || name == "Rightarrow"
                         || name == "implies" || name == "iff" || name == "in" || name == "propto" || name == "ll" || name == "gg"))
                return " " + it.value() + " ";
            return it.value();
        }
        if (math && functions().contains(name))
            return name + (name == "lim" ? QString() : QStringLiteral(" "));
        if (name == "frac" || name == "dfrac" || name == "tfrac" || name == "cfrac") {
            const QString a = argument(s, i), b = argument(s, i);
            const QString up = html(a, true, upright), down = html(b, true, upright);
            return (simple(a, up) ? up : "(" + up + ")") + "/" + (simple(b, down) ? down : "(" + down + ")");
        }
        if (name == "sqrt") {
            const QString n = optional(s, i), a = argument(s, i);
            const QString inner = html(a, true, upright);
            return (n.isEmpty() ? QString() : "<sup>" + html(n, true) + "</sup>") + "√" + (simple(a, inner) ? inner : "(" + inner + ")");
        }
        static const QHash<QString, QChar> accents{{"hat", QChar(0x0302)}, {"widehat", QChar(0x0302)}, {"bar", QChar(0x0304)},
                                                   {"overline", QChar(0x0305)}, {"vec", QChar(0x20D7)}, {"dot", QChar(0x0307)},
                                                   {"ddot", QChar(0x0308)}, {"tilde", QChar(0x0303)}, {"widetilde", QChar(0x0303)},
                                                   {"check", QChar(0x030C)}, {"breve", QChar(0x0306)}, {"acute", QChar(0x0301)},
                                                   {"grave", QChar(0x0300)}};
        if (const auto it = accents.constFind(name); it != accents.constEnd())
            return accent(html(argument(s, i), math, upright), it.value());
        if (name == "mathbb") {
            static const QHash<QChar, QString> sets{{u'R', "ℝ"}, {u'N', "ℕ"}, {u'Z', "ℤ"}, {u'Q', "ℚ"}, {u'C', "ℂ"}, {u'P', "ℙ"}, {u'H', "ℍ"}, {u'E', "𝔼"}};
            const QString a = argument(s, i).trimmed();
            return a.size() == 1 && sets.contains(a[0]) ? sets.value(a[0]) : "<b>" + html(a, true, true) + "</b>";
        }
        if (QStringList{"mathrm", "operatorname", "mathsf", "mathup", "unit", "si"}.contains(name))
            return html(argument(s, i), math, true);
        if (QStringList{"text", "textrm", "textnormal", "textup", "mbox", "hbox", "textsf", "intertext"}.contains(name))
            return html(argument(s, i), false);
        if (QStringList{"textbf", "mathbf", "boldsymbol", "bm", "pmb"}.contains(name))
            return "<b>" + html(argument(s, i), math, upright || name == "mathbf") + "</b>";
        if (QStringList{"emph", "textit", "textsl", "mathit", "mathcal", "mathscr", "mathfrak"}.contains(name))
            return "<i>" + html(argument(s, i), math, true) + "</i>";
        if (name == "texttt" || name == "mathtt" || name == "verb")
            return "<tt>" + html(argument(s, i), false) + "</tt>";
        if (name == "underline" || name == "uline")
            return "<u>" + html(argument(s, i), math, upright) + "</u>";
        if (name == "textsc")
            return html(argument(s, i), false).toUpper();
        if (QStringList{"overbrace", "underbrace", "boxed", "fbox", "colorbox"}.contains(name)) {
            if (name == "colorbox")
                argument(s, i);
            return html(argument(s, i), math, upright);
        }
        if (name == "color" || name == "textcolor") {
            optional(s, i);
            argument(s, i);
            return name == "textcolor" ? html(argument(s, i), math, upright) : QString();
        }
        if (QStringList{"cref", "Cref", "ref", "autoref", "pageref", "nameref", "vref"}.contains(name)) {
            optional(s, i);
            return "<i>" + escapedText(argument(s, i)) + "</i>";
        }
        if (name == "eqref")
            return "(<i>" + escapedText(argument(s, i)) + "</i>)";
        if (name == "cite" || name == "citep" || name == "citet") {
            optional(s, i);
            return "[" + escapedText(argument(s, i)) + "]";
        }
        if (name == "footnote")
            return " (" + html(argument(s, i), false) + ")";
        if (name == "includegraphics") {
            optional(s, i);
            argument(s, i);
            return "<i>[immagine]</i>";
        }
        if (name == "caption") {
            optional(s, i);
            return "<br><i>" + html(argument(s, i), false) + "</i><br>";
        }
        if (QStringList{"section", "subsection", "subsubsection", "paragraph", "subparagraph", "chapter", "part"}.contains(name)) {
            optional(s, i);
            return "<br><b>" + html(argument(s, i), false) + "</b><br>";
        }
        if (name == "par" || name == "medskip" || name == "bigskip" || name == "smallskip")
            return "<br>";
        if (name == "tag")
            return " (" + html(argument(s, i), false) + ")";
        if (name == "stackrel" || name == "overset" || name == "underset") {
            const QString top = argument(s, i), base = argument(s, i);
            return html(base, true, upright) + (name == "underset" ? "<sub>" : "<sup>") + html(top, true, upright) + (name == "underset" ? "</sub>" : "</sup>");
        }
        if (name == "lezione") {                       // \lezione{N}{data}
            argument(s, i);
            argument(s, i);
            return {};
        }
        return {};                                     // un comando che non si conosce: restano i suoi argomenti
    }

    static QString escapedText(const QString &s)
    {
        QString out;
        for (const QChar c : s)
            out += escaped(c);
        return out;
    }
};

}

QString preview(const QString &text, Kind kind)
{
    // la prosa del modello passa prima da `prose`, che riconosce «ω0» e «V_CB»: poi è LaTeX come l'altro, senza commenti
    const QString source = kind == Kind::Prose ? prose(text) : text;
    QString out = Reader(kind == Kind::Latex).html(source, false);
    static const QRegularExpression breaks("(<br>\\s*){3,}");
    out.replace(breaks, "<br><br>");
    static const QRegularExpression edges("^(\\s|<br>)+|(\\s|<br>)+$");
    out.remove(edges);
    out.replace("<br><div", "<div").replace("</div><br>", "</div>");
    return out;
}

}
