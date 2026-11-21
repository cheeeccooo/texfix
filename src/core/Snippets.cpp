#include "core/Snippets.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

namespace tx::snippets {

namespace {
using RE = QRegularExpression;

const QHash<QChar, QString> &greek()
{
    static const QHash<QChar, QString> map{
        {u'α', "alpha"}, {u'β', "beta"}, {u'γ', "gamma"}, {u'δ', "delta"}, {u'ε', "varepsilon"}, {u'ζ', "zeta"}, {u'η', "eta"}, {u'θ', "theta"},
        {u'λ', "lambda"}, {u'μ', "mu"}, {u'ν', "nu"}, {u'ξ', "xi"}, {u'π', "pi"}, {u'ρ', "rho"}, {u'σ', "sigma"}, {u'τ', "tau"}, {u'φ', "varphi"},
        {u'χ', "chi"}, {u'ψ', "psi"}, {u'ω', "omega"}, {u'Γ', "Gamma"}, {u'Δ', "Delta"}, {u'Θ', "Theta"}, {u'Λ', "Lambda"}, {u'Π', "Pi"},
        {u'Σ', "Sigma"}, {u'Φ', "Phi"}, {u'Ψ', "Psi"}, {u'Ω', "Omega"}, {u'∞', "infty"}, {u'→', "to"}, {u'≤', "le"}, {u'≥', "ge"}, {u'≠', "ne"},
        {u'±', "pm"}, {u'·', "cdot"}, {u'×', "times"}, {u'∈', "in"}, {u'∑', "sum"}, {u'∫', "int"}, {u'√', "surd"}, {u'∂', "partial"}};
    return map;
}

// Un tratto di prosa fuori dalla matematica: i caratteri che LaTeX non vuole, le lettere greche, «V_CB».
QString plain(const QString &text)
{
    static const RE token(R"(((?:(?<![A-Za-z0-9])-)?\\?[A-Za-z0-9\x{0370}-\x{03FF}]+)((?:[_^](?:\{[^{}$]*\}|\([^()$]{1,6}\)|[A-Za-z0-9]+|[\x{0370}-\x{03FF}]))+))");     // «V_CB», «\beta_F», «R_{in}», «i_C^Q»
    static const RE script(R"(([_^])(\{[^{}$]*\}|\([^()$]{1,6}\)|[A-Za-z0-9]+|[\x{0370}-\x{03FF}]))");     // «R_o^(1)»: la parentesi resta nell'esponente
    QString out;
    qsizetype at = 0;
    auto escaped = [](const QString &s) {
        QString o;
        for (qsizetype i = 0; i < s.size(); ++i) {
            const QChar c = s[i];
            if (c == u'\\') {                     // un comando: la matematica che il modello scrive senza $ (\\beta, \\infty)
                qsizetype end = i + 1;
                while (end < s.size() && s[end].isLetter())
                    ++end;
                const QString name = s.mid(i + 1, end - i - 1);
                bool known = false;
                for (const QString &v : greek())
                    known = known || v == name || v.toLower() == name;
                known = known || QStringList{"varepsilon", "varphi", "approx", "parallel", "omega", "infty"}.contains(name);
                if (known) {
                    o += "$\\" + name + " $";   // lo spazio: unita a una formula vicina, il comando non si attacca alla lettera dopo
                    i = end - 1;
                } else {
                    o += "\\textbackslash{}";
                }
            } else if (greek().contains(c)) {
                QString sub;                           // «ω0»: il numero che segue è un pedice
                while (i + 1 < s.size() && s[i + 1].isDigit())
                    sub += s[++i];
                o += "$\\" + greek().value(c) + (sub.isEmpty() ? QStringLiteral(" ") : "_{" + sub + "}") + "$";
            } else if (QStringLiteral("%&#_").contains(c)) {
                o += '\\' + QString(c);
            } else if (c == u'^') {
                o += "\\^{}";
            } else if (c == u'~') {
                o += "$\\sim $";                      // «~» nella prosa del modello: circa
            } else if (c == u'{' || c == u'}') {
                o += '\\' + QString(c);
            } else {
                o += c;
            }
        }
        return o;
    };
    for (auto it = token.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        out += escaped(text.mid(at, m.capturedStart() - at));
        QString math = m.captured(1);
        QString last;                                  // due pedici di fila («k_BTr_e»): il secondo dopo un gruppo vuoto
        for (auto s = script.globalMatch(m.captured(2)); s.hasNext();) {
            const auto part = s.next();
            const QString sub = part.captured(2);
            if (part.captured(1) == last)
                math += "{}";
            last = part.captured(1);
            math += part.captured(1) + (sub.startsWith(u'{') ? sub : u'{' + sub + u'}');
        }
        out += u'$' + math + u'$';
        at = m.capturedEnd();
    }
    out += escaped(text.mid(at));
    // due formule separate solo da segni, spazi e numeri («$A_{v}$ $Z_{in}$/($R_{s}$+$Z_{in}$)») sono una formula sola:
    // altrimenti =, + e / escono nel carattere del testo
    static const RE between(R"(^[ =+\-/()\[\]0-9.'<>|·×−≃≈≫≪∥]*$)");
    const QStringList parts = out.split(u'$');
    if (parts.size() % 2 == 0)
        return out;
    QString merged = parts[0];
    QString formula;                                   // la formula che si sta scrivendo
    static const RE opening(R"([(\[][0-9.+\-−/=·× ]*$)");   // «perché (1+» davanti a una formula: la parentesi entra con lei
    for (qsizetype i = 1; i < parts.size(); i += 2) {
        if (formula.isEmpty()) {
            const auto m = opening.match(merged);
            if (m.hasMatch()) {
                formula = m.captured();
                merged.chop(formula.size());
            }
        }
        formula += parts[i];
        QString gap = parts[i + 1];
        if (i + 2 < parts.size() && between.match(gap).hasMatch()) {
            formula += gap;
            continue;
        }
        // una parentesi aperta nella formula si chiude nella formula
        while (!gap.isEmpty() && (gap[0] == u')' || gap[0] == u']') && formula.count(u'(') + formula.count(u'[') > formula.count(u')') + formula.count(u']')) {
            formula += gap[0];
            gap.remove(0, 1);
        }
        merged += u'$' + formula + u'$' + gap;
        formula.clear();
    }
    return merged;
}

const QSet<QString> &mathEnvironments()
{
    static const QSet<QString> names{"equation", "equation*", "align", "align*", "gather", "gather*", "multline", "multline*",
                                     "eqnarray", "eqnarray*", "displaymath", "flalign", "flalign*", "alignat", "alignat*", "math"};
    return names;
}

// le unità di siunitx in una formula: \kilo\ohm → k\Omega
QString unitLatex(const QString &unit)
{
    static const QHash<QString, QString> map{
        {"kilo", "k"}, {"mega", "M"}, {"giga", "G"}, {"milli", "m"}, {"micro", "\\mu "}, {"nano", "n"}, {"pico", "p"}, {"centi", "c"},
        {"ohm", "\\Omega"}, {"volt", "V"}, {"ampere", "A"}, {"watt", "W"}, {"hertz", "Hz"}, {"farad", "F"}, {"henry", "H"},
        {"second", "s"}, {"metre", "m"}, {"meter", "m"}, {"gram", "g"}, {"kilogram", "kg"}, {"newton", "N"}, {"joule", "J"},
        {"pascal", "Pa"}, {"kelvin", "K"}, {"celsius", "{}^\\circ C"}, {"degreeCelsius", "{}^\\circ C"}, {"mole", "mol"},
        {"coulomb", "C"}, {"siemens", "S"}, {"tesla", "T"}, {"weber", "Wb"}, {"decibel", "dB"}, {"bel", "B"}, {"litre", "L"},
        {"liter", "L"}, {"minute", "min"}, {"hour", "h"}, {"day", "d"}, {"degree", "^\\circ"}, {"radian", "rad"}, {"percent", "\\%"},
        {"per", "/"}, {"squared", "^2"}, {"cubed", "^3"}};
    QString out;
    for (qsizetype i = 0; i < unit.size();) {
        if (unit[i] == u'\\') {
            qsizetype j = i + 1;
            while (j < unit.size() && unit[j].isLetter())
                ++j;
            const QString name = unit.mid(i + 1, j - i - 1);
            out += map.value(name, name);
            i = j;
        } else if (unit[i] == u'.' || unit[i] == u'~') {
            out += "\\,";
            ++i;
        } else {
            out += unit[i++];
        }
    }
    return "\\mathrm{" + out + "}";
}

struct Tokenizer {
    const QString &s;
    QList<Token> &out;
    bool bold = false, italic = false;
    QList<int> &lists;                                 // gli elenchi aperti: -1 puntato, se no il contatore
    bool table = false;                                // dentro una tabella: & separa le celle
    QString word;                                      // la parola in corso
    qsizetype i = 0;

    void flush()
    {
        if (!word.isEmpty())
            out << Token{Token::Word, word, bold, italic};
        word.clear();
    }
    void push(Token::Type type, const QString &text = {}, float amount = 0)
    {
        flush();
        out << Token{type, text, bold, italic, amount};
    }
    void put(const QString &text, bool b, bool it)    // una parola fatta, con il suo stile
    {
        flush();
        out << Token{Token::Word, text, b, it};
    }
    void space(float em = 0)
    {
        flush();
        if (!out.isEmpty() && out.back().type == Token::Space && em == 0)
            return;
        out << Token{Token::Space, {}, false, false, em};
    }
    bool starts(QLatin1StringView w) const { return QStringView(s).mid(i).startsWith(w); }
    void mathUntil(const QString &close, Token::Type type)
    {
        qsizetype end = s.indexOf(close, i);
        if (end < 0)
            end = s.size();
        push(type, s.mid(i, end - i));
        i = qMin(s.size(), end + close.size());
    }
    // {…} in s[i], con le graffe annidate; vuoto se non comincia lì
    QString group()
    {
        if (i >= s.size() || s[i] != u'{')
            return {};
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
                return s.mid(from, i++ - from);
        }
        return s.mid(from);
    }
    // un argomento: un gruppo, un comando, un carattere
    QString arg()
    {
        while (i < s.size() && s[i] == u' ')
            ++i;
        if (i >= s.size())
            return {};
        if (s[i] == u'{')
            return group();
        if (s[i] == u'\\') {
            const qsizetype from = i++;
            while (i < s.size() && s[i].isLetter())
                ++i;
            if (i == from + 1 && i < s.size())
                ++i;
            return s.mid(from, i - from);
        }
        return s.mid(i++, 1);
    }
    // un argomento facoltativo […], se c'è
    QString bracket()
    {
        qsizetype j = i;
        while (j < s.size() && s[j] == u' ')
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
    void inner(const QString &text, bool b, bool it)
    {
        flush();
        Tokenizer t{text, out, b, it, lists};
        t.table = table;
        t.run();
    }
    // \begin{…} e \end{…}
    void environment(const QString &cmd)
    {
        const QString env = group();
        const QString base = QString(env).remove(u'*');
        const bool begin = cmd == "begin";
        if (begin && mathEnvironments().contains(env)) {
            const QString close = "\\end{" + env + "}";
            qsizetype end = s.indexOf(close, i);
            if (end < 0)
                end = s.size();
            const QString body = s.mid(i, end - i);
            // gli ambienti a sé diventano quelli dentro una formula: align aligned, gather e multline gathered
            const bool aligned = base.startsWith("align") || base.startsWith("flalign") || base.startsWith("eqnarray");
            const bool gathered = base.startsWith("gather") || base.startsWith("multline");
            push(Token::Display, aligned ? "\\begin{aligned}" + body + "\\end{aligned}" : gathered ? "\\begin{gathered}" + body + "\\end{gathered}" : body);
            i = qMin(s.size(), end + close.size());
            return;
        }
        if (base == "itemize" || base == "enumerate" || base == "description") {
            if (begin) {
                lists << (base == "enumerate" ? 0 : -1);
                push(Token::ListBegin);
                bracket();                             // enumitem: [label=…]
            } else {
                if (!lists.isEmpty())
                    lists.removeLast();
                push(Token::ListEnd);
            }
            return;
        }
        static const QSet<QString> drawings{"tikzpicture", "circuitikz", "pgfpicture", "axis", "picture", "forest", "tikzcd"};
        if (begin && drawings.contains(base)) {        // un disegno: il nome al posto suo
            const QString close = "\\end{" + env + "}";
            const qsizetype end = s.indexOf(close, i);
            i = end < 0 ? s.size() : end + close.size();
            put(QStringLiteral("[disegno]"), false, true);
            return;
        }
        if (base == "tabular" || base == "tabularx" || base == "longtable" || base == "tabulary") {
            if (begin) {
                if (base != "tabular" && base != "longtable")
                    arg();                             // la larghezza
                bracket();
                arg();                                 // le colonne
            }
            table = begin;
            push(Token::Break);
            return;
        }
        static const QSet<QString> plainBlocks{"center", "flushleft", "flushright", "quote", "quotation", "verse", "document",
                                               "figure", "table", "wrapfigure", "minipage", "varwidth", "multicols", "small", "footnotesize"};
        if (plainBlocks.contains(base)) {
            if (begin) {
                bracket();
                if (base == "minipage" || base == "varwidth" || base == "multicols" || base == "wrapfigure")
                    arg();
                if (base == "wrapfigure")
                    arg();
            }
            push(Token::Para);
            return;
        }
        if (!begin) {
            push(Token::Para);
            return;
        }
        push(Token::Para);
        if (base == "proof") {
            put(QStringLiteral("Dimostrazione."), false, true);
            space();
            return;
        }
        // un riquadro: il suo nome e il titolo, in grassetto
        QString label = base;
        if (label.endsWith("box") && label.size() > 3)
            label.chop(3);
        label[0] = label[0].toUpper();
        const QString title = bracket();
        inner(label + (title.isEmpty() ? QString() : " (" + title + ")") + ".", true, italic);
        space();
    }
    void item()
    {
        const QString raw = bracket();
        QString label;
        if (!raw.isEmpty() || (i > 0 && s[i - 1] == u']')) {
            // l'etichetta com'è: via comandi di stile, graffe e dollari («\textbf{i)}» → «i)»)
            for (qsizetype k = 0; k < raw.size(); ++k) {
                if (raw[k] == u'\\') {
                    while (k + 1 < raw.size() && raw[k + 1].isLetter())
                        ++k;
                } else if (raw[k] != u'{' && raw[k] != u'}' && raw[k] != u'$') {
                    label += raw[k];
                }
            }
        } else if (!lists.isEmpty() && lists.last() >= 0) {
            label = QString::number(++lists.last()) + ".";
        } else {
            label = QStringLiteral("•");
        }
        push(Token::Item, label.trimmed());
        while (i < s.size() && s[i].isSpace())
            ++i;
    }
    // un comando (s[i] è la barra)
    void command()
    {
        qsizetype j = i + 1;
        while (j < s.size() && s[j].isLetter())
            ++j;
        const QString cmd = s.mid(i + 1, j - i - 1);
        if (cmd.isEmpty()) {                           // \\, \%, \&, \,, …
            const QChar c = j < s.size() ? s[j] : u' ';
            i = j + 1;
            if (c == u'\\') {
                push(Token::Break);
                bracket();                             // \\[2mm]
            } else if (c == u',' || c == u';' || c == u':' || c == u' ') {
                space(c == u' ' ? 0 : 0.2f);
            } else if (c != u'!' && c != u'-' && c != u'/') {
                word += c;
            }
            return;
        }
        i = j;
        if (i < s.size() && s[i] == u'*' && (cmd.startsWith("section") || cmd.startsWith("sub") || cmd == "chapter" || cmd == "paragraph"))
            ++i;
        if (cmd == "begin" || cmd == "end")
            return environment(cmd);
        if (cmd == "item")
            return item();
        static const QSet<QString> styled{"textbf", "textit", "emph", "textsl", "text", "textrm", "textsf", "textnormal", "textup",
                                          "mbox", "hbox", "underline", "uline", "textsc", "texttt", "intertext"};
        if (styled.contains(cmd)) {
            const QString text = arg();
            inner(text, bold || cmd == "textbf", (italic && cmd != "textup" && cmd != "textnormal") || cmd == "textit" || cmd == "emph" || cmd == "textsl");
            return;
        }
        static const QSet<QString> headings{"section", "subsection", "subsubsection", "paragraph", "subparagraph", "chapter", "part"};
        if (headings.contains(cmd)) {
            bracket();
            push(Token::Para);
            inner(arg(), true, italic);
            push(Token::Break);
            return;
        }
        if (cmd == "caption") {
            bracket();
            push(Token::Break);
            inner(arg(), bold, true);
            push(Token::Break);
            return;
        }
        static const QSet<QString> refs{"cref", "Cref", "ref", "autoref", "pageref", "nameref", "vref", "Vref"};
        if (refs.contains(cmd)) {
            bracket();
            put(arg(), bold, true);
            return;
        }
        if (cmd == "eqref") {
            put("(" + arg() + ")", bold, italic);
            return;
        }
        if (cmd == "cite" || cmd == "citep" || cmd == "citet") {
            bracket();
            put("[" + arg() + "]", bold, italic);
            return;
        }
        if (cmd == "footnote") {
            space();
            word += u'(';
            inner(arg(), bold, italic);
            word += u')';
            return;
        }
        if (cmd == "includegraphics") {
            bracket();
            arg();
            put(QStringLiteral("[immagine]"), false, true);
            return;
        }
        if (cmd == "SI" || cmd == "qty") {
            bracket();
            const QString number = arg(), unit = arg();
            push(Token::Math, number + "\\," + unitLatex(unit));
            return;
        }
        if (cmd == "si" || cmd == "unit") {
            bracket();
            push(Token::Math, unitLatex(arg()));
            return;
        }
        if (cmd == "num") {
            bracket();
            push(Token::Math, arg());
            return;
        }
        if (cmd == "textcolor" || cmd == "colorbox") {
            bracket();
            arg();
            inner(arg(), bold, italic);
            return;
        }
        static const QHash<QString, int> silent{{"label", 1}, {"index", 1}, {"vspace", 1}, {"hspace", 1}, {"setlength", 2},
                                                {"addtolength", 2}, {"setcounter", 2}, {"addtocounter", 2}, {"enlargethispage", 1},
                                                {"phantom", 1}, {"hphantom", 1}, {"vphantom", 1}, {"input", 1}, {"include", 1},
                                                {"cline", 1}, {"color", 1}, {"lezione", 2}, {"scalebox", 1}, {"resizebox", 2},
                                                {"rule", 2}, {"multicolumn", 2}, {"multirow", 2}};
        if (const auto it = silent.constFind(cmd); it != silent.constEnd()) {
            bracket();
            if (i < s.size() && s[i] == u'*')
                ++i;
            for (int n = 0; n < it.value(); ++n)
                arg();
            return;
        }
        if (cmd == "par")
            push(Token::Para);
        else if (cmd == "newline" || cmd == "linebreak")
            push(Token::Break);
        else if (cmd == "smallskip")
            push(Token::VSpace, {}, 0.25f);
        else if (cmd == "medskip")
            push(Token::VSpace, {}, 0.5f);
        else if (cmd == "bigskip")
            push(Token::VSpace, {}, 1.0f);
        else if (cmd == "quad")
            space(1);
        else if (cmd == "qquad")
            space(2);
        else {
            static const QHash<QString, QString> symbols{
                {"ldots", "…"}, {"dots", "…"}, {"S", "§"}, {"textendash", "–"}, {"textemdash", "—"}, {"LaTeX", "LaTeX"},
                {"TeX", "TeX"}, {"textbackslash", "\\"}, {"textasciitilde", "~"}, {"textasciicircum", "^"}, {"euro", "€"},
                {"textdegree", "°"}, {"ss", "ß"}, {"copyright", "©"}, {"dag", "†"}, {"textbullet", "•"}, {"guillemotleft", "«"},
                {"guillemotright", "»"}, {"textquoteright", "’"}, {"P", "¶"}};
            if (const auto it = symbols.constFind(cmd); it != symbols.constEnd())
                word += it.value();
        }
        if (i < s.size() && s[i] == u' ')
            ++i;                                       // lo spazio dopo un comando non conta, come in LaTeX
    }
    // un a capo solo è uno spazio; una riga vuota un capoverso
    void newline()
    {
        qsizetype j = i + 1;
        while (j < s.size() && (s[j] == u' ' || s[j] == u'\t'))
            ++j;
        if (j < s.size() && s[j] == u'\n') {
            push(Token::Para);
            while (j < s.size() && s[j].isSpace())
                ++j;
        } else {
            space();
        }
        i = j;
    }
    void run()
    {
        while (i < s.size()) {
            const QChar c = s[i];
            if (starts(QLatin1StringView("$$")))
                i += 2, mathUntil("$$", Token::Display);
            else if (c == u'$')
                i += 1, mathUntil("$", Token::Math);
            else if (starts(QLatin1StringView("\\[")))
                i += 2, mathUntil("\\]", Token::Display);
            else if (starts(QLatin1StringView("\\(")))
                i += 2, mathUntil("\\)", Token::Math);
            else if (c == u'\\')
                command();
            else if (c == u'{' || c == u'}')
                ++i;
            else if (c == u'\n')
                newline();
            else if (c == u' ' || c == u'\t')
                space(), ++i;
            else if (c == u'~')
                word += QChar(0x00A0), ++i;
            else if (c == u'&' && table) {
                space();
                put(QStringLiteral("|"), false, false);
                space();
                ++i;
            } else if (starts(QLatin1StringView("---")))
                word += u'—', i += 3;
            else if (starts(QLatin1StringView("--")))
                word += u'–', i += 2;
            else if (starts(QLatin1StringView("``")))
                word += u'“', i += 2;
            else if (starts(QLatin1StringView("''")))
                word += u'”', i += 2;
            else if (c == u'%') {                      // un commento, fino alla fine della riga
                while (i < s.size() && s[i] != u'\n')
                    ++i;
                if (i < s.size())
                    ++i;                               // l'a capo dopo un commento non conta
            } else
                word += c, ++i;
        }
        flush();
    }
};

// Da una formula: via \label, \nonumber, \notag (MicroTeX non li conosce); \tag{x} diventa «(x)» in fondo
QString cleanFormula(QString f)
{
    static const RE label(R"(\\label\s*\{[^{}]*\})"), skip(R"(\\(?:nonumber|notag)\b)"), tag(R"(\\tag\*?\s*\{([^{}]*)\})");
    f.remove(label);
    f.remove(skip);
    QString tags;
    for (auto it = tag.globalMatch(f); it.hasNext();)
        tags += "\\qquad(" + it.next().captured(1) + ")";
    f.remove(tag);
    return tags.isEmpty() ? f : f + tags;
}

}

QString prose(const QString &text)
{
    // la matematica fra $…$ resta com'è; il resto è prosa
    QString out;
    const QStringList parts = text.split('$');
    if (parts.size() % 2 == 0)                         // un $ spaiato: tutto prosa
        return plain(QString(text).remove('$'));
    for (qsizetype i = 0; i < parts.size(); ++i)
        out += i % 2 ? '$' + parts[i] + '$' : plain(parts[i]);
    return out;
}

QList<Token> tokens(const QString &text, Kind kind)
{
    QList<Token> out;
    QList<int> lists;
    const QString source = kind == Kind::Prose ? prose(text) : text;
    Tokenizer{source, out, false, false, lists}.run();
    for (Token &t : out)
        if (t.type == Token::Math || t.type == Token::Display)
            t.text = cleanFormula(t.text);
    // la punteggiatura dopo una formula a sé va dentro la formula: da sola finirebbe su una riga vuota
    auto punctuation = [](const Token &t) {
        static const RE only(R"(^[.,;:]+$)");
        return t.type == Token::Word && only.match(t.text).hasMatch();
    };
    for (qsizetype i = 0; i < out.size(); ++i) {
        if (out[i].type != Token::Display)
            continue;
        qsizetype j = i + 1;
        while (j < out.size() && out[j].type == Token::Space)
            ++j;
        if (j >= out.size() || !punctuation(out[j]))
            continue;
        QString &f = out[i].text;
        const QString mark = QString(out[j].text).replace(":", "{:}");
        const qsizetype end = f.endsWith(u'}') && f.lastIndexOf("\\end{") >= 0 ? f.lastIndexOf("\\end{") : f.size();
        f.insert(end, mark);
        out.remove(i + 1, j - i);
    }
    // niente spazi in testa e in coda, né doppi capoversi
    while (!out.isEmpty() && (out.first().type == Token::Space || out.first().type == Token::Para || out.first().type == Token::Break))
        out.removeFirst();
    while (!out.isEmpty() && (out.last().type == Token::Space || out.last().type == Token::Para || out.last().type == Token::Break))
        out.removeLast();
    return out;
}

}
