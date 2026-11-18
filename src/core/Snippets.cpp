#include "core/Snippets.h"

#include "core/Files.h"
#include "core/Tools.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QRegularExpression>

namespace tx::snippets {

namespace {
using RE = QRegularExpression;

QString dirOf(const Project &project) { return project.path("build/resa"); }

QString keyOf(const Project &project, const QString &text, Kind kind, const QString &color)
{
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(text.toUtf8());
    hash.addData(kind == Kind::Prose ? "p" : "l");
    hash.addData(color.toUtf8());
    hash.addData("3");                         // le rese fallite prima che i simboli Unicode avessero una definizione si riprovano
    // il preambolo: cambia lo stile, cambiano le rese
    const QFileInfo package(project.path(Project::PACKAGE));
    hash.addData(QByteArray::number(package.lastModified().toSecsSinceEpoch()) + QByteArray::number(package.size()));
    return QString::fromLatin1(hash.result().toHex().left(20));
}

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
    static const RE token(R"(((?:(?<![A-Za-z0-9])-)?\\?[A-Za-z0-9]+)((?:[_^](?:\{[^{}$]*\}|[A-Za-z0-9]+))+))");     // «V_CB», «\beta_F», «R_{in}», «i_C^Q»
    static const RE script(R"(([_^])(\{[^{}$]*\}|[A-Za-z0-9]+))");
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
                    o += "$\\" + name + "$";
                    i = end - 1;
                } else {
                    o += "\\textbackslash{}";
                }
            } else if (greek().contains(c)) {
                QString sub;                           // «ω0»: il numero che segue è un pedice
                while (i + 1 < s.size() && s[i + 1].isDigit())
                    sub += s[++i];
                o += "$\\" + greek().value(c) + (sub.isEmpty() ? QString() : "_{" + sub + "}") + "$";
            } else if (QStringLiteral("%&#_").contains(c)) {
                o += '\\' + QString(c);
            } else if (c == u'^') {
                o += "\\^{}";
            } else if (c == u'~') {
                o += "\\~{}";
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
        for (auto s = script.globalMatch(m.captured(2)); s.hasNext();) {
            const auto part = s.next();
            const QString sub = part.captured(2);
            math += part.captured(1) + (sub.startsWith(u'{') ? sub : u'{' + sub + u'}');
        }
        out += u'$' + math + u'$';
        at = m.capturedEnd();
    }
    return out + escaped(text.mid(at));
}

// I simboli che il modello scrive in Unicode (−, ≃, ≫, le lettere greche, anche dentro $…$) e che pdflatex non conosce:
// ognuno con il suo comando; un carattere che resta sconosciuto è un «?», non un pezzo che non compila.
QString unicodeSymbols()
{
    static const QString preamble = [] {
        QHash<QChar, QString> symbols = greek();
        const QList<std::pair<char16_t, const char *>> more{
            {u'−', "-"}, {u'≃', "\\simeq"}, {u'≅', "\\cong"}, {u'≈', "\\approx"}, {u'∼', "\\sim"}, {u'≫', "\\gg"}, {u'≪', "\\ll"},
            {u'′', "'"}, {u'″', "''"}, {u'∥', "\\parallel"}, {u'∝', "\\propto"}, {u'≡', "\\equiv"}, {u'←', "\\leftarrow"},
            {u'⇒', "\\Rightarrow"}, {u'⇔', "\\Leftrightarrow"}, {u'↔', "\\leftrightarrow"}, {u'↑', "\\uparrow"}, {u'↓', "\\downarrow"},
            {u'∘', "\\circ"}, {u'∠', "\\angle"}, {u'∆', "\\Delta"}, {u'Ω', "\\Omega"}, {u'µ', "\\mu"}, {u'⋅', "\\cdot"},
            {u'∀', "\\forall"}, {u'∃', "\\exists"}, {u'∉', "\\notin"}, {u'⊂', "\\subset"}, {u'∩', "\\cap"}, {u'∪', "\\cup"},
            {u'∇', "\\nabla"}, {u'ℏ', "\\hbar"}, {u'ℓ', "\\ell"}, {u'∗', "\\ast"}, {u'∓', "\\mp"}, {u'≲', "\\lesssim"}, {u'≳', "\\gtrsim"}};
        const auto hex = [](int c) { return QString::number(c, 16).toUpper().rightJustified(4, u'0'); };
        QString out = "\\makeatletter\\def\\UTFviii@undefined@err#1{?}\\makeatother\n";
        for (auto it = symbols.cbegin(); it != symbols.cend(); ++it)
            out += QStringLiteral("\\DeclareUnicodeCharacter{%1}{\\ensuremath{\\%2}}\n").arg(hex(it.key().unicode()), it.value());
        for (const auto &[c, command] : more)
            out += QStringLiteral("\\DeclareUnicodeCharacter{%1}{\\ensuremath{%2}}\n").arg(hex(c), QString::fromLatin1(command));
        return out;
    }();
    return preamble;
}

QString document(const QStringList &bodies, const QString &color)
{
    QString tex = "\\documentclass[10pt]{article}\n\\usepackage{texfix}\n\\usepackage{varwidth}\n\\usepackage[active,tightpage]{preview}\n"
                  "\\setlength\\PreviewBorder{1.5pt}\n\\pagestyle{empty}\n" + unicodeSymbols() + "\\begin{document}\n";
    for (const QString &body : bodies)
        tex += "\\begin{preview}\\begin{varwidth}{13cm}\\raggedright\\color[HTML]{" + color + "}%\n" + body + "%\n\\end{varwidth}\\end{preview}\n";
    return tex + "\\end{document}\n";
}

// Compila i corpi in un documento, una pagina ciascuno, e ne fa immagini trasparenti. Vero se sono venute tutte.
bool compile(const Project &project, const QStringList &bodies, const QStringList &keys, const QString &color, const QString &job)
{
    const QString dir = dirOf(project);
    QDir().mkpath(dir);
    const QString base = dir + '/' + job;
    files::write(base + ".tex", document(bodies, color));
    QFile::remove(base + ".pdf");
    // percorsi relativi alla cartella del progetto, da cui pdflatex parte: TeX non legge un percorso con «~» (iCloud:
    // «com~apple~CloudDocs»)
    const QDir root(project.root());
    const tools::Output out = tools::run("pdflatex", {"-interaction=nonstopmode", "-halt-on-error", "-output-directory=" + root.relativeFilePath(dir),
                                                      root.relativeFilePath(base + ".tex")},
                                         project.root(), 90000);
    bool good = out.ok && QFileInfo::exists(base + ".pdf");
    if (good) {
        tools::run("pdftocairo", {"-png", "-transp", "-r", "200", base + ".pdf", base}, dir, 60000);
        // «base-1.png», o «base-01.png» se le pagine sono tante
        const int digits = int(QString::number(bodies.size()).size());
        for (qsizetype i = 0; i < keys.size(); ++i) {
            const QString page = QStringLiteral("%1-%2.png").arg(base).arg(i + 1, digits, 10, QChar('0'));
            const QString to = dir + '/' + keys[i] + ".png";
            QFile::remove(to);
            // la resa è larga quanto la riga: se ne tiene solo ciò che è scritto
            QImage image(page);
            QFile::remove(page);
            if (image.isNull()) {
                good = false;
                continue;
            }
            image = image.convertToFormat(QImage::Format_ARGB32);
            int right = 0;
            for (int y = 0; y < image.height(); ++y) {
                const QRgb *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
                for (int x = image.width() - 1; x > right; --x)
                    if (qAlpha(row[x]) > 8) {
                        right = x;
                        break;
                    }
            }
            good = image.copy(0, 0, qMin(image.width(), right + 5), image.height()).save(to) && good;
        }
    }
    for (const char *ext : {".tex", ".pdf", ".log", ".aux", ".out"})
        QFile::remove(base + ext);
    return good;
}

QStringList bodiesOf(const QString &text, Kind kind)
{
    if (kind == Kind::Prose)
        return {prose(text)};
    // un pezzo di una lezione: com'è; se da solo non compila (un tratto di formula), come formula
    static const RE mathy(R"(\\[a-zA-Z]+|[_^=<>+])");
    const bool hasMode = text.contains('$') || text.contains("\\[") || text.contains("\\begin{") || text.contains("\\(");
    const QString formula = "$\\displaystyle " + text + "$";
    return !hasMode && mathy.match(text).hasMatch() ? QStringList{formula, text} : QStringList{text, formula};
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

QString pathOf(const Project &project, const QString &text, Kind kind, const QString &color)
{
    return dirOf(project) + '/' + keyOf(project, text, kind, color) + ".png";
}

QStringList render(const Project &project, const QStringList &texts, Kind kind, const QString &color)
{
    QStringList todo, keys, done;
    for (const QString &text : texts) {
        const QString key = keyOf(project, text, kind, color);
        const QString base = dirOf(project) + '/' + key;
        if (text.trimmed().isEmpty() || keys.contains(key) || QFileInfo::exists(base + ".png") || QFileInfo::exists(base + ".no"))
            continue;
        todo << text;
        keys << key;
    }
    if (todo.isEmpty())
        return done;
    const QString job = "lavoro-" + keys.first();
    QStringList first;
    for (const QString &text : todo)
        first << bodiesOf(text, kind).first();
    if (!compile(project, first, keys, color, job)) {
        // qualcosa non compila: uno alla volta, ognuno con le sue forme
        for (qsizetype i = 0; i < todo.size(); ++i) {
            bool good = false;
            for (const QString &body : bodiesOf(todo[i], kind))
                if (!good)
                    good = compile(project, {body}, {keys[i]}, color, job);
            if (!good)
                files::write(dirOf(project) + '/' + keys[i] + ".no", QString());
        }
    }
    for (const QString &key : keys)
        if (QFileInfo::exists(dirOf(project) + '/' + key + ".png"))
            done << dirOf(project) + '/' + key + ".png";
    return done;
}

}
