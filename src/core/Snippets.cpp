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

#include <functional>

namespace tx::snippets {

namespace {
using RE = QRegularExpression;

QString dirOf(const Project &project) { return project.path("build/resa"); }

// Gli stili del progetto (texfix.sty e lo stile che carica, con i loro compagni): cambiano loro, cambiano le rese.
QByteArray stylesStamp(const Project &project)
{
    QByteArray out;
    for (const QFileInfo &sty : QDir(project.root()).entryInfoList({"*.sty"}, QDir::Files, QDir::Name))
        out += sty.fileName().toUtf8() + QByteArray::number(sty.lastModified().toMSecsSinceEpoch()) + QByteArray::number(sty.size()) + '\n';
    return out;
}

QString keyOf(const Project &project, const QString &text, Kind kind, const QString &color)
{
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(text.toUtf8());
    hash.addData(kind == Kind::Prose ? "p" : "l");
    hash.addData(color.toUtf8());
    hash.addData("6");                         // le rese fallite prima che la prosa del modello si leggesse bene si riprovano
    hash.addData(stylesStamp(project));        // il preambolo: cambia lo stile, cambiano le rese
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
            {u'∇', "\\nabla"}, {u'ℏ', "\\hbar"}, {u'ℓ', "\\ell"}, {u'∗', "\\ast"}, {u'⟨', "\\langle"}, {u'⟩', "\\rangle"}, {u'²', "^{2}"}, {u'³', "^{3}"}, {u'√', "\\surd"}, {u'∓', "\\mp"}, {u'≲', "\\lesssim"}, {u'≳', "\\gtrsim"}};
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

QString preamble()
{
    return "\\documentclass[10pt]{article}\n\\usepackage{texfix}\n\\usepackage{varwidth}\n\\usepackage[active,tightpage]{preview}\n"
           "\\setlength\\PreviewBorder{1.5pt}\n\\pagestyle{empty}\n" + unicodeSymbols();
}

// Il preambolo precompilato (mylatexformat): caricarlo costa un terzo che rileggerlo. Un nome per ogni versione degli
// stili; se non si fa (manca mylatexformat, uno stile non si lascia salvare) resta un «.no» e si compila come prima.
QString format(const Project &project)
{
    const QString dir = dirOf(project);
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(preamble().toUtf8());
    hash.addData(stylesStamp(project));
    const QString name = "preambolo-" + QString::fromLatin1(hash.result().toHex().left(12));
    const QString base = dir + '/' + name;
    if (QFileInfo::exists(base + ".fmt"))
        return base;
    if (QFileInfo::exists(base + ".no"))
        return {};
    QDir().mkpath(dir);
    for (const QFileInfo &old : QDir(dir).entryInfoList({"preambolo-*"}, QDir::Files))
        QFile::remove(old.absoluteFilePath());         // quelli degli stili di prima
    files::write(base + ".tex", preamble() + "\\begin{document}\n\\end{document}\n");
    const QDir root(project.root());
    const tools::Output out = tools::run("pdftex", {"-ini", "-interaction=nonstopmode", "-halt-on-error", "-jobname=" + name,
                                                    "-output-directory=" + root.relativeFilePath(dir), "&pdflatex", "mylatexformat.ltx",
                                                    root.relativeFilePath(base + ".tex")},
                                         project.root(), 90000);
    for (const char *ext : {".tex", ".log"})
        QFile::remove(base + ext);
    if (out.ok && QFileInfo::exists(base + ".fmt"))
        return base;
    files::write(base + ".no", QString());
    return {};
}

QString document(const QList<std::pair<QString, QString>> &bodies, bool precompiled)
{
    QString tex = precompiled ? QString() : preamble();
    tex += "\\begin{document}\n";
    for (const auto &[body, color] : bodies)
        tex += "\\begin{preview}\\begin{varwidth}{13cm}\\raggedright\\color[HTML]{" + color + "}%\n" + body + "%\n\\end{varwidth}\\end{preview}\n";
    return tex + "\\end{document}\n";
}

// Compila i corpi (con il loro colore) in un documento, una pagina ciascuno, e ne fa immagini trasparenti. Vero se sono
// venute tutte.
bool compile(const Project &project, const QList<std::pair<QString, QString>> &bodies, const QStringList &keys, const QString &fmt, const QString &job)
{
    const QString dir = dirOf(project);
    QDir().mkpath(dir);
    const QString base = dir + '/' + job;
    files::write(base + ".tex", document(bodies, !fmt.isEmpty()));
    QFile::remove(base + ".pdf");
    // percorsi relativi alla cartella del progetto, da cui pdflatex parte: TeX non legge un percorso con «~» (iCloud:
    // «com~apple~CloudDocs»)
    const QDir root(project.root());
    QStringList args{"-interaction=nonstopmode", "-halt-on-error", "-output-directory=" + root.relativeFilePath(dir)};
    if (!fmt.isEmpty())
        args << "-fmt=" + root.relativeFilePath(fmt);
    const tools::Output out = tools::run("pdflatex", args << root.relativeFilePath(base + ".tex"), project.root(), 90000);
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
    // tre parole di fila, tolti i comandi, sono prosa («Questi dati si collegano a \\emph{…}»), non una formula
    static const RE command(R"(\\[a-zA-Z]+)"), words(R"((?:\p{L}{3,}\s+){2}\p{L}{3,})");
    const bool prose = words.match(QString(text).remove(command)).hasMatch();
    return !hasMode && !prose && mathy.match(text).hasMatch() ? QStringList{formula, text} : QStringList{text, formula};
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

QStringList render(const Project &project, const QList<Piece> &pieces)
{
    QList<Piece> todo;
    QStringList keys, done;
    for (const Piece &piece : pieces) {
        const QString key = keyOf(project, piece.text, piece.kind, piece.color);
        const QString base = dirOf(project) + '/' + key;
        if (piece.text.trimmed().isEmpty() || keys.contains(key) || QFileInfo::exists(base + ".png") || QFileInfo::exists(base + ".no"))
            continue;
        todo << piece;
        keys << key;
    }
    if (todo.isEmpty())
        return done;
    QString fmt = format(project);
    // un gruppo di pezzi, nella prima forma di ognuno: se non compila si divide a metà; un pezzo solo prova le sue
    // altre forme, e se nessuna compila resta senza resa
    std::function<void(qsizetype, qsizetype)> attempt = [&](qsizetype from, qsizetype to) {
        QList<std::pair<QString, QString>> bodies;
        for (qsizetype i = from; i < to; ++i)
            bodies << std::pair{bodiesOf(todo[i].text, todo[i].kind).first(), todo[i].color};
        const QString job = "lavoro-" + keys[from];
        if (compile(project, bodies, keys.mid(from, to - from), fmt, job))
            return;
        if (from == 0 && to == todo.size() && !fmt.isEmpty() && compile(project, bodies, keys, QString(), job)) {
            files::write(fmt + ".no", QString());     // il preambolo precompilato non va (TeX aggiornato?): senza
            QFile::remove(fmt + ".fmt");
            fmt.clear();
            return;
        }
        if (to - from > 1) {
            const qsizetype middle = from + (to - from) / 2;
            attempt(from, middle);
            attempt(middle, to);
            return;
        }
        const QStringList forms = bodiesOf(todo[from].text, todo[from].kind);
        for (qsizetype f = 1; f < forms.size(); ++f)
            if (compile(project, {std::pair{forms[f], todo[from].color}}, {keys[from]}, fmt, job))
                return;
        files::write(dirOf(project) + '/' + keys[from] + ".no", QString());
    };
    attempt(0, todo.size());
    for (const QString &key : keys)
        if (QFileInfo::exists(dirOf(project) + '/' + key + ".png"))
            done << dirOf(project) + '/' + key + ".png";
    return done;
}

QStringList render(const Project &project, const QStringList &texts, Kind kind, const QString &color)
{
    QList<Piece> pieces;
    for (const QString &text : texts)
        pieces << Piece{text, kind, color};
    return render(project, pieces);
}

}
