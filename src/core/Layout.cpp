#include "core/Layout.h"

#include "core/Blocks.h"
#include "core/Compiler.h"
#include "core/Files.h"
#include "core/SyncTex.h"
#include "core/Tools.h"

#include <QCryptographicHash>
#include <functional>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace tx::layout {

namespace {
const char *const REGISTRY = "salvataggi/impaginazione.json";

QJsonObject registry(const Project &project) { return QJsonDocument::fromJson(files::read(project.path(REGISTRY)).toUtf8()).object(); }
void saveRegistry(const Project &project, const QJsonObject &o) { files::write(project.path(REGISTRY), QString::fromUtf8(QJsonDocument(o).toJson())); }

QStringList withoutSigns(const QStringList &lines)
{
    QStringList out;
    for (const QString &line : lines)
        if (!line.trimmed().endsWith(QLatin1String("% impaginazione")))
            out << line;
    return out;
}
// L'impronta della lezione senza le mosse dell'impaginazione: cambia solo se cambia il contenuto.
QString contentHash(const QString &text)
{
    return QString::fromLatin1(QCryptographicHash::hash(withoutSigns(text.split('\n')).join('\n').toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}

// La lezione com'era quando è stata misurata (senza le mosse), con la sua impronta nel nome: quando cambia dice da dove.
// Si tengono le ultime, perché annullare una scelta può riportare il registro a un'impronta di prima. Sono fra ciò che si
// rifà: se manca, la lezione si considera cambiata tutta.
QString measuredPath(const Project &project, const QString &file, const QString &hash)
{
    return project.path(QStringLiteral("build/impaginazione/%1/misurata-%2.tex").arg(QFileInfo(file).completeBaseName(), hash));
}
// La lezione è misurata così com'è: l'impronta nel registro, il testo accanto.
void stamp(const Project &project, const QString &file, QJsonObject *entry, const QString &text)
{
    const QString hash = contentHash(text);
    (*entry)["impronta"] = hash;
    entry->remove("da_rifare");
    const QString path = measuredPath(project, file, hash);
    files::write(path, withoutSigns(text.split('\n')).join('\n'));
    const QDir dir = QFileInfo(path).dir();
    const QStringList all = dir.entryList({"misurata-*.tex"}, QDir::Files, QDir::Time);
    for (qsizetype i = 12; i < all.size(); ++i)
        QFile::remove(dir.filePath(all[i]));
}

blocks::Units unitsOf(const Style &style) { return {style.boxes(), {style.figureWrapper(), "figure"}}; }

// La riga in cui mettere l'a capo pagina per portare il blocco in cima: prima del suo marcatore e dei titoli che lo aprono.
int groupStart(const QStringList &lines, const blocks::Block &block)
{
    int at = (block.marker ? block.marker : block.line) - 1;           // indice da 0
    while (at > 0) {
        const QString above = lines[at - 1].trimmed();
        if (above.isEmpty() || above.startsWith("\\subsection") || above.startsWith("\\subsubsection") || above.startsWith("\\paragraph") || above.startsWith("\\label"))
            --at;
        else
            break;
    }
    while (at < lines.size() && lines[at].trimmed().isEmpty())
        ++at;
    return at;
}

QString nameOf(const Placed &p, const Style &style)
{
    QString role = p.kind == "figura" ? QStringLiteral("La figura") : QStringLiteral("Il riquadro");
    if (const Style::Environment *env = style.environment(p.kind))
        for (const Style::Role &r : Style::roles())
            if (r.value == env->role)
                role = r.label;
    return p.title.isEmpty() ? role : QStringLiteral("%1 «%2»").arg(role, p.title);
}

QJsonObject toJson(const Move &m)
{
    return {{"blocco", m.block}, {"che_cosa", m.what}, {"vuoto", m.hole}, {"stato", m.state}, {"segno", m.sign}, {"dopo", m.after}, {"fino", m.until}, {"origine", m.origin.isEmpty() ? m.state : m.origin}, {"invariata", m.same}};
}

// Un'unità che non si spezza (una figura, un riquadro che lo stile dice intero) sta tutta dove sta la sua ultima riga:
// la sua prima riga può lasciare un segno in fondo alla colonna di prima (lo spazio che la precede), e non vuol dire niente.
bool unbreakable(const Style &style, const QString &kind)
{
    if (kind == "figura")
        return true;
    const Style::Environment *env = style.environment(kind);
    return env && !env->breaks;
}

// Dove sta il testo nelle pagine di una lezione compilata, e dove cade una sua riga.
struct Geometry {
    QString pdf;
    double textTop = 0, textHeight = 1, pageWidth = 1;
    bool twoColumns = false;
};
Geometry geometryOf(const Project &project, const Style &style, const QString &lesson)
{
    Geometry g;
    g.pdf = Compiler::pdfOf(project, lesson);
    g.twoColumns = style.twoColumns();
    static const QRegularExpression size(R"(Page size:\s+([\d.]+) x ([\d.]+))"), text(R"(TEXFIX pagina: alto (-?[\d.]+)pt, testo ([\d.]+)pt)");
    const auto s = size.match(tools::run("pdfinfo", {g.pdf}, project.root()).text);
    g.pageWidth = qMax(1.0, s.captured(1).toDouble());
    g.textHeight = qMax(1.0, s.captured(2).toDouble());
    if (const auto m = text.match(files::read(g.pdf.chopped(4) + ".log")); m.hasMatch()) {
        g.textTop = m.captured(1).toDouble() * 72.0 / 72.27;
        g.textHeight = qMax(1.0, m.captured(2).toDouble() * 72.0 / 72.27);
    }
    return g;
}
struct Where {
    bool ok = false;
    int slot = 0;                                      // la pagina, o la colonna
    double bottom = 0;                                 // quanto è scesa la riga nella colonna, fra 0 e 1
};
// Il segno che una riga lascia nel testo. Una riga durante la quale la pagina si chiude si prende anche l'intestazione e il
// piè di pagina: quei segni, fuori dall'area del testo, non contano.
synctex::Spot spotOf(const Project &project, const Geometry &g, const QString &lesson, int line)
{
    for (const synctex::Spot &s : synctex::allToPdf(project.root(), g.pdf, lesson, line))
        if (s.y >= g.textTop - 2 && s.y + s.height <= g.textTop + g.textHeight + 2)
            return s;
    return {};
}
Where whereIs(const Project &project, const Geometry &g, const QString &lesson, int line)
{
    const synctex::Spot s = spotOf(project, g, lesson, line);
    if (!s.ok)
        return {};
    return {true, g.twoColumns ? s.page * 2 + (s.x > g.pageWidth / 2 ? 1 : 0) : s.page, qBound(0.0, (s.y + s.height - g.textTop) / g.textHeight, 1.0)};
}
// Dove finisce ciò che una riga lascia nel PDF: il suo segno più avanti (una formula lascia anche, in fondo alla colonna di
// prima, lo spazio che la precede).
Where whereEnds(const Project &project, const Geometry &g, const QString &lesson, int line)
{
    Where best;
    for (const synctex::Spot &s : synctex::allToPdf(project.root(), g.pdf, lesson, line)) {
        if (s.y < g.textTop - 2 || s.y + s.height > g.textTop + g.textHeight + 2)
            continue;
        const Where w{true, g.twoColumns ? s.page * 2 + (s.x > g.pageWidth / 2 ? 1 : 0) : s.page, qBound(0.0, (s.y + s.height - g.textTop) / g.textHeight, 1.0)};
        if (!best.ok || w.slot > best.slot || (w.slot == best.slot && w.bottom > best.bottom))
            best = w;
    }
    return best;
}
// Il segno più alto che le righe di un'unità lasciano nel PDF: per ciò che non si spezza è l'unità stessa (il disegno, il
// riquadro), dovunque siano finiti gli spazi che la precedono.
synctex::Spot tallest(const Project &project, const Geometry &g, const QString &lesson, int line, int lastLine)
{
    synctex::Spot best;
    const int step = qMax(1, (lastLine - line + 1) / 24);
    for (int k = line; k <= lastLine; k += step)
        for (const synctex::Spot &s : synctex::allToPdf(project.root(), g.pdf, lesson, k))
            if ((!best.ok || s.height > best.height) && s.y >= g.textTop - 2 && s.y + s.height <= g.textTop + g.textHeight + 2)
                best = s;
    return best;
}
// Dove comincia un'unità: per ciò che non si spezza, dove sta lei per intero (`lastLine`: la riga che la chiude).
Where unitWhere(const Project &project, const Style &style, const Geometry &g, const QString &lesson, const QString &kind, int line, int lastLine)
{
    if (!unbreakable(style, kind))
        return whereIs(project, g, lesson, line);
    const synctex::Spot s = tallest(project, g, lesson, line, lastLine);
    if (!s.ok)
        return whereIs(project, g, lesson, line);
    return {true, g.twoColumns ? s.page * 2 + (s.x > g.pageWidth / 2 ? 1 : 0) : s.page, qBound(0.0, (s.y + s.height - g.textTop) / g.textHeight, 1.0)};
}
}

QList<Placed> measure(const Project &project, const Style &style, const QString &lesson, JobControl *control, QString *failure)
{
    QList<Placed> out;
    const BuildResult built = Compiler::compileSync(project, lesson, control);
    if (failure)
        *failure = built.failure.isEmpty() && built.hasErrors() ? QStringLiteral("La lezione ha errori di compilazione: prima vanno sistemati.") : built.failure;
    if (!built.failure.isEmpty() || built.hasErrors())
        return out;
    static const QRegularExpression size(R"(Page size:\s+([\d.]+) x ([\d.]+))");
    const auto sizeMatch = size.match(tools::run("pdfinfo", {built.pdf}, project.root()).text);
    const double pageHeight = qMax(1.0, sizeMatch.captured(2).toDouble()), pageWidth = qMax(1.0, sizeMatch.captured(1).toDouble());
    // su due colonne ciò che conta è la colonna: la prima e la seconda di ogni pagina, in fila
    auto slot = [&](const synctex::Spot &s) { return style.twoColumns() ? s.page * 2 + (s.x > pageWidth / 2 ? 1 : 0) : s.page; };
    const QStringList lines = files::read(project.path(lesson)).split('\n');

    const Geometry geometry0 = geometryOf(project, style, lesson);
    // dove cade una riga del sorgente; se quella non lascia segno nel PDF, una vicina
    auto spot = [&](int line, int step, int until) {
        for (int k = 0; k < 4 && line >= 1 && line != until + step; ++k, line += step) {
            if (control)
                control->check();
            const synctex::Spot s = spotOf(project, geometry0, lesson, line);
            if (s.ok)
                return s;
        }
        return synctex::Spot();
    };
    struct Raw {
        Placed placed;
        double y = 0;
    };
    QList<Raw> raw;
    double textTop = pageHeight, textBottom = 0;
    for (const blocks::Block &block : blocks::parse(lines.join('\n'), unitsOf(style))) {
        const bool unit = block.kind != "testo";
        const synctex::Spot first = spot(block.line, +1, block.endLine);
        const synctex::Spot last = spot(unit && block.endLine > block.line ? block.endLine - 1 : block.endLine, -1, block.line);
        if (!first.ok || !last.ok)
            continue;
        textTop = qMin(textTop, first.y);
        textBottom = qMax(textBottom, last.y + last.height);
        // ciò che non si spezza sta tutto dove sta la sua fine
        if (unbreakable(style, block.kind)) {
            const synctex::Spot whole = tallest(project, geometry0, lesson, block.line, block.endLine);
            if (whole.ok)
                raw << Raw{Placed{block.id, block.kind, block.title, block.line, slot(whole), slot(whole), 0}, whole.y};
            continue;
        }
        raw << Raw{Placed{block.id, block.kind, block.title, block.line, slot(first), slot(last), 0}, first.y};
    }
    // dove sta il testo nella pagina lo dice il documento stesso, nel log (texfix.sty); senza, ciò che si è visto
    static const QRegularExpression geometry(R"(TEXFIX pagina: alto (-?[\d.]+)pt, testo ([\d.]+)pt)");
    if (const auto m = geometry.match(files::read(built.pdf.chopped(4) + ".log")); m.hasMatch()) {
        textTop = m.captured(1).toDouble() * 72.0 / 72.27;   // da punti di TeX a punti del PDF
        textBottom = textTop + m.captured(2).toDouble() * 72.0 / 72.27;
    }
    const double textHeight = qMax(1.0, textBottom - textTop);
    for (Raw &r : raw) {
        r.placed.top = qBound(0.0, (r.y - textTop) / textHeight, 1.0);
        out << r.placed;
    }
    return out;
}

bool needed(const Project &project, const QString &file)
{
    const QString text = files::read(project.path(file));
    if (!text.contains("% @L") && !text.trimmed().isEmpty())
        return true;                                   // nessun marcatore: non è mai stata misurata davvero
    const QJsonObject entry = registry(project).value(file).toObject();
    return entry.value("da_rifare").toBool() || entry.value("impronta").toString() != contentHash(text);
}

bool known(const Project &project, const QString &file) { return registry(project).contains(file); }

QList<Move> moves(const Project &project, const QString &file)
{
    QList<Move> out;
    QHash<QString, int> where;
    const QStringList lines = files::read(project.path(file)).split('\n');
    static const QRegularExpression marker(R"(^\s*%\s*@(L\d+\.\d+))");
    for (int i = 0; i < lines.size(); ++i)
        if (const auto m = marker.match(lines[i]); m.hasMatch())
            where.insert(m.captured(1), i + 1);
    for (const QJsonValue &v : registry(project).value(file).toObject().value("mosse").toArray()) {
        const QJsonObject o = v.toObject();
        out << Move{file, o.value("blocco").toString(), o.value("che_cosa").toString(), o.value("vuoto").toDouble(), o.value("stato").toString(),
                    where.value(o.value("blocco").toString()), o.value("segno").toString(QString::fromUtf8(SIGN)), o.value("dopo").toString(), o.value("fino").toString(), o.value("origine").toString(o.value("stato").toString()), o.value("invariata").toBool()};
    }
    return out;
}

int toReview(const Project &project, const QString &file)
{
    int n = 0;
    for (const Move &m : moves(project, file))
        n += m.state == "proposta" || m.state == "fatta" || m.state == "segnalata" || m.state == "a_mano" || m.state == "agente";
    return n;
}

namespace {
// Spezzare un riquadro in un punto scelto: l'ultima cucitura (una riga vuota fra due paragrafi, al primo livello del
// riquadro) prima del punto in cui il riquadro passa alla colonna dopo. Resta se, ricompilata, ciò che segue la
// cucitura comincia nella colonna nuova e il vuoto lasciato è piccolo. Scrive la mossa in `move` e dice se è rimasta.
bool breakInside(const Project &project, const Style &style, const QString &file, const QString &blockId, JobControl *control, Move *move)
{
    const QString path = project.path(file), before = files::read(path);
    QStringList lines = before.split('\n');
    blocks::Block block;
    for (const blocks::Block &b : blocks::parse(before, unitsOf(style)))
        if (b.id == blockId)
            block = b;
    const Style::Environment *env = style.environment(block.kind);
    if (block.id.isEmpty() || !env || !env->breaks || style.breakCommand.isEmpty() || block.endLine - block.line < 4)
        return false;
    // il PDF dev'essere quello di queste righe: una prova fatta prima (l'a capo, poi tolto) l'ha lasciato diverso
    if (Compiler::compileSync(project, file, control).hasErrors())
        return false;
    const Geometry g = geometryOf(project, style, file);
    const Where start = whereIs(project, g, file, block.line);
    if (!start.ok)
        return false;
    // la prima riga del riquadro che cade nella colonna dopo (le colonne crescono con le righe: si cerca a metà)
    int low = block.line, high = block.endLine - 1;
    while (low < high) {
        if (control)
            control->check();
        const int middle = (low + high) / 2;
        Where w;
        for (int line = middle; line >= low && !w.ok; --line)      // una riga che non lascia segno: quella prima
            w = whereIs(project, g, file, line);
        if (w.ok && w.slot > start.slot)
            high = middle;
        else
            low = middle + 1;
    }
    // le cuciture: righe vuote al primo livello del riquadro, non nel suo primo 7%
    QList<int> seams;
    int depth = 0;
    for (int line = block.line; line < low; ++line) {
        const QString code = uncommented(lines[line - 1]).toString();
        depth += int(code.count(QLatin1String("\\begin{")) - code.count(QLatin1String("\\end{")));
        if (depth == 1 && lines[line - 1].trimmed().isEmpty() && !lines[line - 2].trimmed().isEmpty()
            && line - block.line >= qMax(2, int(0.07 * (block.endLine - block.line))))
            seams << line;
    }
    // dall'ultima cucitura all'indietro: se quella non regge (ciò che segue non passa oltre) si prova quella prima
    const QStringList original = lines;
    for (int attempt = 0; attempt < 3 && !seams.isEmpty(); ++attempt) {
        const int seam = seams.takeLast();
        lines = original;
        int last = seam - 1;                           // l'ultima riga piena prima della cucitura
        while (last > block.line && lines[last - 1].trimmed().isEmpty())
            --last;
        static const QRegularExpression lead(R"(^\s*)");
        const QString sign = lead.match(lines[last - 1]).captured(0) + style.breakCommand + " % impaginazione";
        lines.insert(seam, sign);                      // fra due righe vuote
        lines.insert(seam + 1, QString());
        files::write(path, lines.join('\n'));
        bool good = false;
        double hole = 1;
        try {
            if (!Compiler::compileSync(project, file, control).hasErrors()) {
                const Geometry after = geometryOf(project, style, file);
                const Where tail = whereIs(project, after, file, last);
                Where head;
                for (int line = seam + 3; line <= seam + 6 && line <= lines.size() && !head.ok; ++line)
                    head = whereIs(project, after, file, line);
                hole = tail.ok ? 1 - tail.bottom : 1;
                good = tail.ok && head.ok && head.slot > tail.slot && hole <= HOLE_OK;
            }
        } catch (...) {
            files::write(path, before);
            throw;
        }
        if (!good) {
            files::write(path, before);
            if (hole > HOLE_OK && hole < 1)
                break;                                 // già troppo vuoto: più indietro è peggio
            continue;
        }
        move->hole = hole;
        move->sign = sign;
        move->after = lines[last - 1];
        return true;
    }
    return false;
}
}

namespace {
QString shotBase(const Project &project, const QString &file, const QString &block, const QString &which)
{
    return project.path(QStringLiteral("build/impaginazione/%1/%2-%3").arg(QFileInfo(file).completeBaseName(), block, which));
}
// La pagina in cui comincia il blocco e quella dopo, in piccolo, dal PDF della lezione com'è adesso.
void snap(const Project &project, const Style &style, const QString &file, const QString &blockId, const QString &which)
{
    const Geometry g = geometryOf(project, style, file);
    int page = 0;
    // dove sta il blocco: per ogni pagina e colonna, dalla prima all'ultima sua riga
    struct Piece { int page, column; double x, top, bottom; };
    QList<Piece> pieces;
    double margin = g.pageWidth;
    for (const blocks::Block &b : blocks::parse(files::read(project.path(file)), unitsOf(style))) {
        if (b.id != blockId)
            continue;
        const int step = qMax(1, (b.endLine - b.line + 1) / 150);
        for (int line = b.line; line <= b.endLine; line += step) {
            const synctex::Spot s = synctex::toPdf(project.root(), g.pdf, file, line);
            // una riga che non lascia segno nel testo (un comando, un'etichetta) può finire fuori dall'area del testo: non conta
            if (!s.ok || s.y < g.textTop - 2 || s.y + s.height > g.textTop + g.textHeight + 2)
                continue;
            if (page == 0)
                page = s.page;
            const int column = g.twoColumns && s.x > g.pageWidth / 2 ? 1 : 0;
            if (column == 0)
                margin = qMin(margin, s.x);
            bool found = false;
            for (Piece &p : pieces)
                if (p.page == s.page && p.column == column) {
                    p.x = qMin(p.x, s.x);
                    p.top = qMin(p.top, s.y);
                    p.bottom = qMax(p.bottom, s.y + s.height);
                    found = true;
                }
            if (!found)
                pieces << Piece{s.page, column, s.x, s.y, s.y + s.height};
        }
    }
    if (margin >= g.pageWidth / 2)
        margin = g.pageWidth * 0.08;
    const QString base = shotBase(project, file, blockId, which);
    const QDir dir = QFileInfo(base).dir();
    QDir().mkpath(dir.path());
    // il dopo mostra le stesse pagine del prima: se il blocco è passato alla pagina dopo, si deve vedere anche quella
    // che ha lasciato (e dove stava), non solo quella in cui è arrivato
    if (which == "dopo" && page > 0) {
        const QString first = QFileInfo(shotBase(project, file, blockId, "prima")).fileName();
        const QStringList before = dir.entryList({first + "-*.png"}, QDir::Files, QDir::Name);
        if (!before.isEmpty())
            if (const int was = QFileInfo(before.first()).completeBaseName().section('-', -1).toInt(); was > 0)
                page = qMin(page, was);
    }
    for (const QString &old : dir.entryList({QFileInfo(base).fileName() + "-*.png"}, QDir::Files))
        QFile::remove(dir.filePath(old));
    if (page > 0)
        tools::run("pdftoppm", {"-png", "-r", "60", "-f", QString::number(page), "-l", QString::number(page + 1), g.pdf, base}, project.root(), 30000);
    // i contorni: dove sta il blocco in ognuna delle due pagine, in frazioni della pagina
    const QStringList made = dir.entryList({QFileInfo(base).fileName() + "-*.png"}, QDir::Files, QDir::Name);
    const double pageHeight = made.isEmpty() ? 1 : qMax(1.0, QImageReader(dir.filePath(made.first())).size().height() * 72.0 / 60.0);
    QJsonObject rects;
    for (const Piece &p : std::as_const(pieces)) {
        if (p.page < page || p.page > page + 1 || p.bottom - p.top < 9)
            continue;                                  // fuori dalle due pagine, o un filo: non è un pezzo del blocco
        const double x0 = p.column == 0 ? margin - 5 : p.x - 5;
        const double x1 = g.twoColumns && p.column == 0 ? g.pageWidth / 2 - 6 : g.pageWidth - margin + 5;
        QJsonArray list = rects.value(QString::number(p.page - page)).toArray();
        list.append(QJsonObject{{"x", x0 / g.pageWidth}, {"y", (p.top - 3) / pageHeight}, {"w", (x1 - x0) / g.pageWidth}, {"h", (p.bottom - p.top + 7) / pageHeight}});
        rects[QString::number(p.page - page)] = list;
    }
    files::write(base + ".json", QString::fromUtf8(QJsonDocument(rects).toJson()));
}
// Le pagine in piccolo del prima e del dopo sono la stessa immagine: la mossa non ha cambiato ciò che si vede.
bool sameShots(const Project &project, const QString &file, const QString &block)
{
    const QString first = shotBase(project, file, block, "prima"), second = shotBase(project, file, block, "dopo");
    const QDir dir = QFileInfo(first).dir();
    const QStringList before = dir.entryList({QFileInfo(first).fileName() + "-*.png"}, QDir::Files, QDir::Name);
    const QStringList after = dir.entryList({QFileInfo(second).fileName() + "-*.png"}, QDir::Files, QDir::Name);
    if (before.isEmpty() || before.size() != after.size())
        return false;
    for (qsizetype i = 0; i < before.size(); ++i) {
        if (before[i].section('-', -1) != after[i].section('-', -1))
            return false;                              // non le stesse pagine
        const QImage a = QImage(dir.filePath(before[i])).convertToFormat(QImage::Format_Grayscale8);
        const QImage b = QImage(dir.filePath(after[i])).convertToFormat(QImage::Format_Grayscale8);
        if (a.isNull() || a.size() != b.size())
            return false;
        qsizetype different = 0;
        for (int y = 0; y < a.height(); ++y) {
            const uchar *x = a.constScanLine(y), *z = b.constScanLine(y);
            for (int k = 0; k < a.width(); ++k)
                different += qAbs(int(x[k]) - int(z[k])) > 48;
        }
        if (different > a.width() * a.height() / 2000)  // qualche punto sì (un bordo spostato di un niente), una riga no
            return false;
    }
    return true;
}
int wideLines(const Project &project, const QString &file)
{
    return int(files::read(Compiler::pdfOf(project, file).chopped(4) + ".log").count("Overfull \\hbox"));
}
}

namespace {
const char *const CLOSE_SIGN = "}% impaginazione";

// La misura di una figura: il disegno rimpicciolito quanto basta perché la figura stia con ciò che la precede invece di
// passare oltre lasciando un grande vuoto. Resta se, ricompilata, la figura comincia dove finisce il blocco di prima.
bool shrink(const Project &project, const Style &style, const QString &file, const blocks::Block &block, const blocks::Block &previous,
            JobControl *control, Move *move)
{
    const QString path = project.path(file), before = files::read(path);
    const QStringList original = before.split('\n');
    // il disegno dentro la figura: da dove comincia a dove finisce (indici da 0)
    static const QRegularExpression drawing(R"(\\begin\{(tikzpicture|circuitikz)\}|\\includegraphics|\\resizebox\{)");
    int first = -1, last = -1;
    for (int i = block.line; i < block.endLine - 1 && i < original.size() && first < 0; ++i)
        if (drawing.match(original[i]).hasMatch())
            first = i;
    if (first < 0 || original[first].contains("scalebox"))
        return false;
    for (int i = block.endLine - 2; i >= first && last < 0; --i) {
        const QString t = original[i].trimmed();
        if (t.contains("\\end{tikzpicture}") || t.contains("\\end{circuitikz}") || t.contains("\\includegraphics") || t == "}" || t == "%\n}")
            last = i;
    }
    if (last < 0)
        return false;
    const int wideBefore = wideLines(project, file);   // la mossa non deve far uscire niente dalla colonna
    static const QRegularExpression lead(R"(^\s*)");
    const QString indent = lead.match(original[first]).captured(0);
    for (const double scale : {0.85, 0.7, 0.55}) {
        QStringList lines = original;
        const QString open = indent + QStringLiteral("\\scalebox{%1}{% impaginazione").arg(scale);
        lines.insert(last + 1, indent + QString::fromUtf8(CLOSE_SIGN));
        lines.insert(first, open);
        files::write(path, lines.join('\n'));
        bool good = false;
        try {
            if (!Compiler::compileSync(project, file, control).hasErrors()) {
                const Geometry g = geometryOf(project, style, file);
                const Where start = unitWhere(project, style, g, file, block.kind, block.line, block.endLine + 2);   // due righe in più
                Where end;
                for (int line = previous.endLine; line >= previous.line && !end.ok; --line)
                    end = whereIs(project, g, file, line);
                good = start.ok && end.ok && start.slot == end.slot && wideLines(project, file) <= wideBefore;
            }
        } catch (...) {
            files::write(path, before);
            throw;
        }
        if (good) {
            Placed p;
            p.kind = block.kind;
            p.title = block.title;
            move->what = QStringLiteral("%1 non ci stava e lasciava vuoto il %2% %3: rimpicciolita al %4%, ora sta con ciò che la precede.")
                             .arg(nameOf(p, style)).arg(qRound(move->hole * 100))
                             .arg(style.twoColumns() ? QStringLiteral("della colonna") : QStringLiteral("della pagina")).arg(qRound(scale * 100));
            move->sign = open;
            move->after = original[first - 1];
            move->until = original[last];
            return true;
        }
    }
    files::write(path, before);
    return false;
}

// Un a capo dentro un blocco, in un punto scelto: prima del paragrafo che contiene la riga `line` (o, con `exact`, proprio
// prima di quella riga). Nel testo è un a capo pagina; in un riquadro che si spezza, il comando dello stile su una cucitura
// al primo livello; se il paragrafo è il primo del blocco, l'a capo va sopra il blocco. Resta se il vuoto che lascia è
// piccolo e, ricompilata, `holds` dice che il problema non c'è più (riceve le misure nuove e dove sono finite le righe).
bool breakBefore(const Project &project, const Style &style, const QString &file, const blocks::Block &block, int line, bool exact,
                 JobControl *control, const std::function<bool(const Geometry &, const std::function<int(int)> &)> &holds, Move *move)
{
    const QString path = project.path(file), before = files::read(path);
    QStringList lines = before.split('\n');
    const bool box = block.kind != "testo";
    if (box && exact)
        return false;
    int at = line;                                     // la riga (da 1) prima della quale va l'a capo
    bool above = false, mid = false;
    if (!exact) {
        // il paragrafo comincia dopo una riga vuota, o dopo la formula di prima: lì la frase che introduce questa è già
        // un capoverso a sé nel PDF, anche se nel sorgente il paragrafo continua
        static const QRegularExpression displayEnd(R"(^\s*(\\\]|\\end\{(align|equation|gather|multline)\*?\}))");
        while (at > block.line && !lines[at - 2].trimmed().isEmpty()) {
            if (at < line && displayEnd.match(lines[at - 2]).hasMatch()) {
                mid = true;
                break;
            }
            --at;
        }
        above = !mid && at <= block.line + (box ? 1 : 0);
        if (box && !above) {                           // in un riquadro solo al primo livello, e se lo stile sa spezzarlo
            const Style::Environment *env = style.environment(block.kind);
            int depth = 0;
            for (int k = block.line; k < at; ++k) {
                const QString code = uncommented(lines[k - 1]).toString();
                depth += int(code.count(QLatin1String("\\begin{")) - code.count(QLatin1String("\\end{")));
            }
            if (!env || !env->breaks || style.breakCommand.isEmpty() || depth != 1)
                above = true, mid = false;
        }
    }
    int index = at - 1, added = 1;                     // dove si inserisce (da 0), quante righe
    QString sign = QString::fromUtf8(SIGN), after;
    if (above) {
        index = groupStart(lines, block);
    } else {
        int last = at - 1;                             // l'ultima riga piena prima
        while (last > 1 && lines[last - 1].trimmed().isEmpty())
            --last;
        static const QRegularExpression lead(R"(^\s*)");
        // dopo una formula il paragrafo continuava: ciò che segue l'a capo non deve prendere il rientro
        sign = lead.match(lines[at - 1]).captured(0) + (box ? style.breakCommand : QStringLiteral("\\par\\newpage"))
               + (mid ? QStringLiteral("\\noindent") : QString()) + " % impaginazione";
        after = lines[last - 1];
    }
    // il vuoto che lascerebbe: da dove finisce ciò che resta di qua
    const Geometry g = geometryOf(project, style, file);
    Where tail;
    for (int k = index; k >= 1 && k > index - 6 && !tail.ok; --k)
        if (!lines[k - 1].trimmed().isEmpty() && !lines[k - 1].trimmed().startsWith('%'))
            tail = whereIs(project, g, file, k);
    const double hole = tail.ok ? 1 - tail.bottom : 1;
    if (hole > HOLE_OK)
        return false;
    const int wideBefore = wideLines(project, file);
    lines.insert(index, sign);
    if (!above && !exact && !mid) {
        lines.insert(index + 1, QString());
        added = 2;
    }
    files::write(path, lines.join('\n'));
    bool good = false;
    try {
        if (!Compiler::compileSync(project, file, control).hasErrors())
            good = wideLines(project, file) <= wideBefore
                   && holds(geometryOf(project, style, file), [&](int old) { return old > index ? old + added : old; });
    } catch (...) {
        files::write(path, before);
        throw;
    }
    if (!good) {
        files::write(path, before);
        return false;
    }
    move->hole = hole;
    move->sign = sign;
    move->after = after;
    return true;
}
}

Result lesson(const Project &project, const Style &style, const QString &file, JobControl *control)
{
    Result result;
    const QString path = project.path(file);
    dropChanged(project, file);                        // cambiata dall'ultima volta: ciò che sta dal punto cambiato in giù si rifà
    {   // i blocchi senza marcatore (lezioni scritte a mano, o portate da fuori) lo prendono ora: senza, non si misurano
        const QString text = files::read(path);
        const QString marked = blocks::mark(text, Project::lessonOf(file, text).number, unitsOf(style));
        if (marked != text)
            files::write(path, marked);
    }
    // le decisioni prese restano: gli a capo accettati non si toccano, i blocchi lasciati com'erano non si riprovano
    QList<Move> kept;
    QStringList left;
    for (const Move &m : moves(project, file)) {
        if (m.state != "proposta" && m.state != "segnalata")
            kept << m;                             // le decisioni prese restano; ciò che era solo proposto si rimisura
        if (m.state == "lasciata" || m.state == "a_mano" || m.state == "agente" || m.state == "risolta" || (m.state == "accettata" && m.sign.isEmpty()))
            left << m.block;                       // decise in altro modo: non si riprovano
    }
    QList<Move> fresh;
    QStringList tried = left;
    for (const Move &m : std::as_const(kept))          // una mossa per blocco: chi ne ha già una (fatta e da rivedere, o
        tried << m.block;                              // decisa) non si riprova, o la sua riga finirebbe scritta due volte
    QStringList settled;
    // Ogni mossa sposta ciò che segue: dopo i titoli e i vuoti si riguarda da capo se qualcosa è finito a cavallo, finché
    // un giro non cambia più niente.
    const int SWEEPS = 8;
    for (int sweep = 0; sweep < SWEEPS; ++sweep) {
    for (int round = 0; round < 40; ++round) {
        const QList<Placed> placed = measure(project, style, file, control, &result.failure);
        if (!result.failure.isEmpty())
            return result;
        const Placed *problem = nullptr;
        for (const Placed &p : placed)
            if (p.kind != "testo" && p.split() && !p.block.isEmpty() && !tried.contains(p.block) && !settled.contains(p.block)) {
                problem = &p;
                break;                                 // dall'alto in basso: ogni mossa sposta ciò che segue
            }
        if (!problem)
            break;
        ++result.found;
        tried << problem->block;
        snap(project, style, file, problem->block, "prima");
        const double hole = 1.0 - problem->top;
        const QString name = nameOf(*problem, style);
        const QString two = style.twoColumns() ? QStringLiteral("due colonne") : QStringLiteral("due pagine");
        const QString fresh1 = style.twoColumns() ? QStringLiteral("a colonna nuova") : QStringLiteral("a pagina nuova");
        const QString whole1 = style.twoColumns() ? QStringLiteral("della colonna") : QStringLiteral("della pagina");
        const int percent = qRound(hole * 100);
        // spezzato in un punto scelto, fra due paragrafi: per ciò che è più lungo di una colonna, o lascerebbe troppo vuoto
        auto inside = [&] {
            Move move{file, problem->block, {}, 0, "fatta", 0};
            if (!breakInside(project, style, file, problem->block, control, &move))
                return false;
            snap(project, style, file, problem->block, "dopo");
            move.same = sameShots(project, file, problem->block);
            move.what = move.same
                ? QStringLiteral("%1 cadeva a cavallo di %2 già fra due paragrafi: il punto ora è fissato, la pagina resta uguale.").arg(name, two)
                : QStringLiteral("%1 cadeva a cavallo di %2 in un punto qualunque: ora si spezza fra due paragrafi (vuoto lasciato: %3%).").arg(name, two).arg(qRound(move.hole * 100));
            fresh << move;
            ++result.done;
            return true;
        };
        if (problem->lastPage - problem->firstPage > 1 || problem->top < 0.02) {       // più alto di una colonna: si spezza comunque
            if (!inside())
                fresh << Move{file, problem->block, QStringLiteral("%1 è più lungo di %2: resta spezzato.").arg(name, style.twoColumns() ? QStringLiteral("una colonna") : QStringLiteral("una pagina")), hole, "segnalata", 0};
            continue;
        }
        // prima lo spazio: se dall'altra parte finiscono solo poche righe, la pagina si allunga di quel poco e l'unità resta
        // intera senza lasciare vuoti (su una colonna sola: su due l'altra colonna si allungherebbe con lei)
        if (!style.twoColumns()) {
            const QString start = files::read(path);
            QStringList lines = start.split('\n');
            const blocks::Block *block = nullptr;
            const QList<blocks::Block> parsed = blocks::parse(start, unitsOf(style));
            for (const blocks::Block &b : parsed)
                if (b.id == problem->block)
                    block = &b;
            Where tail;
            if (block) {
                const Geometry g = geometryOf(project, style, file);
                for (int line = block->endLine - 1; line >= block->line && !tail.ok; --line)
                    tail = whereIs(project, g, file, line);
            }
            if (block && tail.ok && tail.bottom <= 0.07) {
                const QString sign = QStringLiteral("\\enlargethispage{3\\baselineskip} % impaginazione");
                lines.insert(groupStart(lines, *block), sign);
                files::write(path, lines.join('\n'));
                bool whole = false;
                try {
                    QString failure;
                    for (const Placed &p : measure(project, style, file, control, &failure))
                        if (p.block == problem->block)
                            whole = !p.split();
                } catch (...) {
                    files::write(path, start);
                    throw;
                }
                if (whole) {
                    Move move{file, problem->block, QStringLiteral("%1 finiva con poche righe sulla pagina dopo: la pagina è stata allungata di quel poco, e ora sta intero.").arg(name), 0, "fatta", 0};
                    move.sign = sign;
                    fresh << move;
                    ++result.done;
                    continue;
                }
                files::write(path, start);
            }
        }
        // la mossa: a capo pagina prima del blocco; resta se, ricompilata, il blocco è intero
        const QString before = files::read(path);
        QStringList lines = before.split('\n');
        for (const blocks::Block &block : blocks::parse(before, unitsOf(style))) {
            if (block.id != problem->block)
                continue;
            lines.insert(groupStart(lines, block), QString::fromUtf8(SIGN));
            break;
        }
        files::write(path, lines.join('\n'));
        bool whole = false;
        try {
            QString failure;
            for (const Placed &p : measure(project, style, file, control, &failure))
                if (p.block == problem->block)
                    whole = !p.split();
        } catch (...) {
            files::write(path, before);
            throw;
        }
        if (whole && hole <= HOLE_OK) {
            fresh << Move{file, problem->block, QStringLiteral("%1 cadeva a cavallo di %3: ora comincia %4 (vuoto lasciato: %2%).").arg(name).arg(percent).arg(two, fresh1), hole, "fatta", 0};
            ++result.done;
        } else {
            files::write(path, before);
            if (inside())
                continue;
            fresh << Move{file, problem->block,
                          whole ? QStringLiteral("%1 cade a cavallo di %3. Portarlo %4 lascerebbe vuoto il %2% %5.").arg(name).arg(percent).arg(two, fresh1, whole1)
                                : QStringLiteral("%1 cade a cavallo di %2 e un a capo non basta: va sistemato a mano.").arg(name, two),
                          hole, whole ? "proposta" : "segnalata", 0};
            result.proposed += whole;
        }
    }
    // poi ciò che non è un'unità spezzata: un titolo rimasto in fondo senza il suo testo, un grande vuoto prima di un'unità
    const QString afterUnits = files::read(path);
    settled += tried;
    for (const Move &m : kept + fresh)
        settled << m.block;
    static const QRegularExpression heading(R"(^\s*\\(?:sub)*section\*?[\[{])");
    for (int round = 0; round < 12; ++round) {
        if (control)
            control->check();
        const QString before = files::read(path);
        QStringList lines = before.split('\n');
        const QList<blocks::Block> parsed = blocks::parse(before, unitsOf(style));
        Compiler::compileSync(project, file, control);  // una prova tolta lascia il PDF com'era con la prova
        const Geometry g = geometryOf(project, style, file);
        bool near = false;                             // non è rimasto solo, ma comincia troppo vicino alla fine
        int title = -1, content = -1;                  // da 0
        const blocks::Block *owner = nullptr;
        Where top;
        for (int i = 0; i < lines.size() && title < 0; ++i) {
            if (!heading.match(uncommented(lines[i]).toString()).hasMatch())
                continue;
            int j = i + 1;                             // la prima riga di testo sotto il titolo
            for (; j < lines.size(); ++j) {
                const QString t = lines[j].trimmed();
                if (!(t.isEmpty() || t.startsWith('%') || t.startsWith("\\label") || t.startsWith("\\lezione") || heading.match(t).hasMatch()))
                    break;
            }
            const blocks::Block *next = nullptr;
            for (const blocks::Block &b : parsed)
                if (!next && b.line >= i + 1)
                    next = &b;
            if (j >= lines.size() || !next || next->id.isEmpty() || settled.contains(next->id))
                continue;
            const Where wt = whereIs(project, g, file, i + 1);
            Where wc;
            for (int k = j; k < j + 3 && k < lines.size() && !wc.ok; ++k)
                wc = whereIs(project, g, file, k + 1);
            // rimasto in fondo senza il suo testo, o con così poco spazio sotto (meno del 13% della colonna, cinque o sei
            // righe con il titolo) che il suo testo comincia appena
            if (wt.ok && wc.ok && (wc.slot > wt.slot || wt.bottom > 1 - NEAR_END)) {
                near = wc.slot == wt.slot;
                title = i;
                content = j;
                owner = next;
                top = wt;
            }
        }
        if (title < 0)
            break;
        ++result.found;
        const QString id = owner->id;
        settled << id;
        snap(project, style, file, id, "prima");
        const double hole = 1.0 - top.bottom;
        const int percent = qRound(hole * 100);
        const QString where = style.twoColumns() ? QStringLiteral("alla colonna") : QStringLiteral("alla pagina");
        lines.insert(groupStart(lines, *owner), QString::fromUtf8(SIGN));
        files::write(path, lines.join('\n'));
        bool together = false;
        try {
            if (!Compiler::compileSync(project, file, control).hasErrors()) {
                const Geometry after = geometryOf(project, style, file);
                const Where wt = whereIs(project, after, file, title + 2);
                Where wc;
                for (int k = content + 1; k < content + 4 && k < lines.size() && !wc.ok; ++k)
                    wc = whereIs(project, after, file, k + 1);
                together = wt.ok && wc.ok && wt.slot == wc.slot && wt.slot > top.slot;
            }
        } catch (...) {
            files::write(path, before);
            throw;
        }
        if (together && hole <= HOLE_OK) {
            fresh << Move{file, id, (near ? QStringLiteral("Un titolo cominciava troppo vicino alla fine: ora passa %1 dopo con il suo testo (vuoto lasciato: %2%).")
                                          : QStringLiteral("Un titolo restava in fondo senza il suo testo: ora passa %1 dopo con lui (vuoto lasciato: %2%).")).arg(where).arg(percent), hole, "fatta", 0};
            ++result.done;
        } else {
            files::write(path, before);
            fresh << Move{file, id,
                          together ? QStringLiteral("Un titolo resta in fondo senza il suo testo. Portarlo %1 dopo lascerebbe vuoto il %2%.").arg(where).arg(percent)
                                   : QStringLiteral("Un titolo resta in fondo senza il suo testo e un a capo non basta: va sistemato a mano."),
                          hole, together ? "proposta" : "segnalata", 0};
            result.proposed += 1;
        }
    }
    for (int round = 0; round < 40; ++round) {
        bool shrunk = false;
        // un grande vuoto: un'unità che non ci stava è passata oltre da sola. Una figura si prova a rimpicciolirla; per il
        // resto non c'è una mossa sicura: si segnala.
        Compiler::compileSync(project, file, control);
        const QString text = files::read(path);
        const QList<blocks::Block> parsed = blocks::parse(text, unitsOf(style));
        const Geometry g = geometryOf(project, style, file);
        // una formula in fondo, separata dalla frase che la commenta; un elenco che lascia una voce sola dall'altra parte
        {
            const QStringList lines = text.split('\n');
            auto owner = [&](int line) -> QString {       // il blocco che contiene la riga (da 1)
                for (const blocks::Block &b : parsed)
                    if (line >= b.line && line <= b.endLine)
                        return b.id;
                return {};
            };
            bool again = false;                        // una mossa ha cambiato le righe: si rimisura da capo
            // prova la mossa (un a capo prima del paragrafo di `target`, o proprio prima di quella riga); se non regge segnala
            auto settle = [&](int line, int target, bool exact, const QString &done, const QString &flagged,
                              const std::function<bool(const Geometry &, const std::function<int(int)> &)> &holds) {
                const blocks::Block *block = nullptr;
                for (const blocks::Block &b : parsed)
                    if (line >= b.line && line <= b.endLine)
                        block = &b;
                if (!block || block->id.isEmpty() || settled.contains(block->id))
                    return;
                snap(project, style, file, block->id, "prima");
                settled << block->id;
                ++result.found;
                Move move{file, block->id, {}, 0, "fatta", 0};
                if (breakBefore(project, style, file, *block, target, exact, control, holds, &move)) {
                    move.what = done.arg(qRound(move.hole * 100));
                    snap(project, style, file, block->id, "dopo");
                    move.same = sameShots(project, file, block->id);
                    fresh << move;
                    ++result.done;
                } else {
                    ++result.proposed;
                    fresh << Move{file, block->id, flagged, 0, "segnalata", 0};
                }
                again = true;                          // riuscita o no, si è ricompilato
            };
            static const QRegularExpression displayEnd(R"(^\s*(\\\]|\\end\{(align|equation|gather|multline)\*?\}))");
            static const QRegularExpression listBegin(R"(^\s*\\begin\{(itemize|enumerate)\})"), listEnd(R"(^\s*\\end\{(itemize|enumerate)\})");
            const QString side = style.twoColumns() ? QStringLiteral("colonna") : QStringLiteral("pagina");
            for (int i = 1; i + 1 < lines.size() && !again; ++i) {
                if (control)
                    control->check();
                if (displayEnd.match(lines[i]).hasMatch()) {
                    const QString next = lines[i + 1].trimmed();
                    if (next.isEmpty() || next.startsWith('%') || next.startsWith("\\begin") || next.startsWith("\\end") || next.startsWith("\\["))
                        continue;                      // dopo la formula comincia altro: non è il suo commento
                    Where formula;
                    for (int k = i; k >= i - 3 && k >= 1 && !formula.ok; --k)
                        formula = whereEnds(project, g, file, k);
                    const Where comment = whereIs(project, g, file, i + 2);
                    if (formula.ok && comment.ok && comment.slot > formula.slot)
                        settle(i + 1, i + 1, false,
                               QStringLiteral("Una formula restava in fondo alla %1, separata dalla frase che la commenta: ora passa alla %1 dopo con il paragrafo che la introduce (vuoto lasciato: %2%).").arg(side),
                               QStringLiteral("Una formula resta in fondo alla %1, separata dalla frase che la commenta. Portarla oltre con il suo paragrafo lascerebbe troppo vuoto: si può sistemare a mano o affidare all'agente.").arg(side),
                               [&, i](const Geometry &after, const std::function<int(int)> &now) {
                                   Where f;
                                   for (int k = i; k >= i - 3 && k >= 1 && !f.ok; --k)
                                       f = whereEnds(project, after, file, now(k));
                                   const Where c = whereIs(project, after, file, now(i + 2));
                                   return f.ok && c.ok && f.slot == c.slot;
                               });
                } else if (listBegin.match(lines[i]).hasMatch()) {
                    QList<int> places, items;           // dove cade ogni voce del primo livello, e la sua riga (da 1)
                    int depth = 0, j = i;
                    for (; j < lines.size(); ++j) {
                        if (listBegin.match(lines[j]).hasMatch())
                            ++depth;
                        else if (listEnd.match(lines[j]).hasMatch() && --depth == 0)
                            break;
                        else if (depth == 1 && lines[j].trimmed().startsWith("\\item"))
                            if (const Where w = whereIs(project, g, file, j + 1); w.ok) {
                                places << w.slot;
                                items << j + 1;
                            }
                    }
                    if (places.size() >= 3 && places.first() != places.last()
                        && (places.count(places.first()) == 1 || places.count(places.last()) == 1)) {
                        // l'ultima da sola di là: la penultima la raggiunge. La prima da sola di qua: l'elenco passa oltre con
                        // la frase che lo introduce.
                        const bool lastAlone = places.count(places.last()) == 1;
                        const int a = lastAlone ? items[items.size() - 2] : items[0], b = lastAlone ? items.last() : items[1];
                        settle(i + 1, lastAlone ? a : i + 1, lastAlone,
                               (lastAlone ? QStringLiteral("Un elenco lasciava l'ultima voce da sola nella %1 dopo: ora la penultima passa con lei (vuoto lasciato: %2%).")
                                          : QStringLiteral("Un elenco lasciava la prima voce da sola in fondo alla %1: ora comincia nella %1 dopo con la frase che lo introduce (vuoto lasciato: %2%).")).arg(side),
                               QStringLiteral("Un elenco lascia una voce sola dall'altra parte della %1, e un a capo non lo sistema: si può sistemare a mano o affidare all'agente.").arg(side),
                               [&, a, b](const Geometry &after, const std::function<int(int)> &now) {
                                   const Where first = whereIs(project, after, file, now(a)), second = whereIs(project, after, file, now(b));
                                   return first.ok && second.ok && first.slot == second.slot;
                               });
                    }
                    i = j;
                }
            }
            if (again) {
                shrunk = true;
                continue;
            }
        }
        for (qsizetype i = 1; i < parsed.size(); ++i) {
            const blocks::Block &b = parsed[i];
            if (b.kind == "testo" || b.id.isEmpty() || settled.contains(b.id))
                continue;
            const Where start = unitWhere(project, style, g, file, b.kind, b.line, b.endLine);
            Where end;
            for (int line = parsed[i - 1].endLine; line >= parsed[i - 1].line && !end.ok; --line)
                end = whereIs(project, g, file, line);
            if (!start.ok || !end.ok || start.slot <= end.slot)
                continue;
            const double hole = 1.0 - end.bottom;
            const bool whole = unbreakable(style, b.kind);
            // poco vuoto: ciò che non si spezza è passato oltre da solo, e va bene così; un a capo fissa il punto.
            // Sotto il 2% la colonna è semplicemente piena: non c'è niente da fissare.
            if (hole <= HOLE_OK && (!whole || hole <= 0.02))
                continue;
            Placed named;
            named.kind = b.kind;
            named.title = b.title;
            const QString side = style.twoColumns() ? QStringLiteral("della colonna") : QStringLiteral("della pagina");
            const QString next = style.twoColumns() ? QStringLiteral("alla colonna dopo") : QStringLiteral("alla pagina dopo");
            // l'a capo prima dell'unità: resta se, ricompilata, lei e ciò che la precede stanno dove stavano
            auto pin = [&](const QString &what) {
                QStringList lines = text.split('\n');
                lines.insert(groupStart(lines, b), QString::fromUtf8(SIGN));
                files::write(path, lines.join('\n'));
                bool good = false;
                try {
                    if (!Compiler::compileSync(project, file, control).hasErrors()) {
                        const Geometry after = geometryOf(project, style, file);
                        const Where now = unitWhere(project, style, after, file, b.kind, b.line + 1, b.endLine + 1);
                        Where before;
                        for (int line = parsed[i - 1].endLine; line >= parsed[i - 1].line && !before.ok; --line)
                            before = whereIs(project, after, file, line);
                        good = now.ok && before.ok && now.slot == start.slot && before.slot == end.slot;
                    }
                } catch (...) {
                    files::write(path, text);
                    throw;
                }
                if (!good) {
                    files::write(path, text);
                    return false;
                }
                Move move{file, b.id, what, hole, "fatta", 0};
                snap(project, style, file, b.id, "dopo");
                move.same = sameShots(project, file, b.id);
                fresh << move;
                ++result.found;
                ++result.done;
                return true;
            };
            snap(project, style, file, b.id, "prima");
            settled << b.id;
            if (hole <= HOLE_OK) {
                pin(QStringLiteral("%1 passava %2 lasciando vuoto il %3% %4: un a capo ora fissa il punto.").arg(nameOf(named, style), next).arg(qRound(hole * 100)).arg(side));
                shrunk = true;                         // fissata o no, si è ricompilato: si rimisura da capo
                break;
            }
            if (b.kind == "figura") {
                Move move{file, b.id, {}, hole, "fatta", 0};
                if (shrink(project, style, file, b, parsed[i - 1], control, &move)) {
                    ++result.found;
                    ++result.done;
                    fresh << move;
                    shrunk = true;                     // le righe sono cambiate: si rimisura da capo
                    break;
                }
                // nemmeno rimpicciolita ci sta: resta dov'è con un a capo, e il vuoto diventa voluto
                if (pin(QStringLiteral("%1 non ci stava nemmeno rimpicciolita al 55%: comincia %2 con un a capo, e il vuoto che lascia (%3% %4) è voluto.")
                            .arg(nameOf(named, style), next).arg(qRound(hole * 100)).arg(side))) {
                    shrunk = true;
                    break;
                }
            } else if (pin(QStringLiteral("%1 non ci stava ed è passato oltre: un a capo ora fissa il punto, e il vuoto che lascia (%2% %3) è voluto.")
                               .arg(nameOf(named, style)).arg(qRound(hole * 100)).arg(side))) {
                shrunk = true;
                break;
            }
            Placed p;
            p.kind = b.kind;
            p.title = b.title;
            ++result.found;
            ++result.proposed;
            fresh << Move{file, b.id, QStringLiteral("%1 non ci stava ed è passato oltre: prima resta vuoto il %2% %3. Si può accorciare o spostare a mano, o affidare all'agente.")
                                          .arg(nameOf(p, style)).arg(qRound((1.0 - end.bottom) * 100)).arg(style.twoColumns() ? QStringLiteral("della colonna") : QStringLiteral("della pagina")),
                          1.0 - end.bottom, "segnalata", 0};
            shrunk = true;                             // le prove hanno ricompilato: ciò che segue si rimisura
            break;
        }
        if (!shrunk)
            break;
    }
    // L'ordine conta: una mossa fatta dopo, ma più in alto nella lezione, ha spostato tutto ciò che segue. Le mosse fatte
    // prima di lei e che stanno più in basso guardavano una pagina che non c'è più: si tolgono e il giro dopo le rifà.
    // Una mossa che non cambia la pagina («invariata») non sposta niente.
    bool dropped = false;
    if (sweep + 1 < SWEEPS) {
        QHash<QString, int> lineOf;
        for (const blocks::Block &b : blocks::parse(files::read(path), unitsOf(style)))
            lineOf.insert(b.id, b.line);
        QList<qsizetype> stale;
        for (qsizetype i = 0; i < fresh.size(); ++i)
            for (qsizetype j = i + 1; j < fresh.size(); ++j)
                if (fresh[j].state == "fatta" && !fresh[j].same && lineOf.value(fresh[j].block) < lineOf.value(fresh[i].block)) {
                    stale << i;
                    break;
                }
        static const QRegularExpression marker(R"(^\s*%\s*@(L\d+\.\d+))");
        for (qsizetype n = stale.size() - 1; n >= 0; --n) {
            const Move m = fresh.takeAt(stale[n]);
            tried.removeAll(m.block);
            settled.removeAll(m.block);
            --result.found;
            if (m.state == "fatta")
                --result.done;
            else
                result.proposed = qMax(0, result.proposed - 1);
            if (m.state != "fatta" || m.sign.isEmpty())
                continue;
            QStringList lines = files::read(path).split('\n');
            int at = -1;
            for (int k = 0; k < lines.size() && at < 0; ++k)
                if (marker.match(lines[k]).captured(1) == m.block)
                    at = k;
            if (at < 0)
                continue;
            const bool above = m.sign.trimmed().startsWith("\\newpage") || m.sign.trimmed().startsWith("\\enlargethispage");
            int end = at + 1;
            while (end < lines.size() && !marker.match(lines[end]).hasMatch())
                ++end;
            int sign = -1;
            for (int k = above ? qMax(0, at - 10) : at; k < (above ? at : end) && sign < 0; ++k)
                if (lines[k].trimmed() == m.sign.trimmed())
                    sign = k;
            if (sign < 0)
                continue;
            lines.removeAt(sign);
            for (int k = sign; m.sign.contains("\\scalebox") && k < end - 1 && k < lines.size(); ++k)
                if (lines[k].trimmed() == QLatin1String(CLOSE_SIGN)) {
                    lines.removeAt(k);
                    break;
                }
            if (!above && sign < lines.size() && sign > 0 && lines[sign].trimmed().isEmpty() && lines[sign - 1].trimmed().isEmpty())
                lines.removeAt(sign);
            files::write(path, lines.join('\n'));
            dropped = true;
        }
        dropped = dropped || !stale.isEmpty();
    }
    if (!dropped && files::read(path) == afterUnits)
        break;
    }
    {   // com'è venuta ogni mossa fatta; e quelle di prima, che restano: la lezione è cambiata, e il loro «dopo» con lei
        bool any = !kept.isEmpty();
        for (const Move &m : std::as_const(fresh))
            any = any || m.state == "fatta";
        if (any && !Compiler::compileSync(project, file, control).hasErrors()) {
            for (const Move &m : std::as_const(fresh))
                if (m.state == "fatta")
                    snap(project, style, file, m.block, "dopo");
            for (const Move &m : std::as_const(kept)) {
                if (control)
                    control->check();
                snap(project, style, file, m.block, "dopo");
            }
        }
    }
    for (Move &m : fresh)
        if (m.state == "segnalata")
            m.sign.clear();                            // nessuna mossa del programma: niente da mettere o togliere
    QJsonArray list;
    for (const Move &m : kept + fresh)
        list.append(toJson(m));
    QJsonObject all = registry(project);
    QJsonObject entry{{"mosse", list}};
    stamp(project, file, &entry, files::read(path));
    all[file] = entry;
    saveRegistry(project, all);
    return result;
}

QString decide(const Project &project, const QString &file, const QString &block, const QString &state)
{
    QJsonObject all = registry(project);
    QJsonObject entry = all.value(file).toObject();
    QJsonArray list = entry.value("mosse").toArray();
    for (qsizetype i = 0; i < list.size(); ++i) {
        QJsonObject move = list[i].toObject();
        if (move.value("blocco").toString() != block)
            continue;
        // a_mano, agente, risolta, o una mossa che il programma non ha fatto (niente da mettere o togliere): cambia solo
        // ciò che si ricorda
        if ((state != "accettata" && state != "lasciata") || move.value("segno").toString(QString::fromUtf8(SIGN)).isEmpty()) {
            move["stato"] = state;
            list[i] = move;
            entry["mosse"] = list;
            all[file] = entry;
            saveRegistry(project, all);
            return {};
        }
        const QString path = project.path(file);
        QStringList lines = files::read(path).split('\n');
        // l'impronta segue la decisione solo se la lezione era ancora quella misurata: una lezione cambiata a mano resta da
        // rimpaginare (decidere una mossa non la misura di nuovo)
        const bool measured = entry.value("impronta").toString() == contentHash(lines.join('\n'));
        static const QRegularExpression marker(R"(^\s*%\s*@(L\d+\.\d+))");
        int at = -1;                                   // il marcatore del blocco
        for (int k = 0; k < lines.size() && at < 0; ++k)
            if (marker.match(lines[k]).captured(1) == block)
                at = k;
        if (at < 0)
            return QStringLiteral("Il blocco non è più nella lezione.");
        // la riga di questa mossa, se c'è: un a capo sopra il blocco (prima dei titoli che lo aprono), o uno spezzamento dentro
        const QString written = move.value("segno").toString(QString::fromUtf8(SIGN));
        const bool above = written.trimmed().startsWith("\\newpage") || written.trimmed().startsWith("\\enlargethispage");
        const bool wraps = written.contains("\\scalebox");          // la misura di una figura: una riga apre, una chiude
        int end = at + 1;                              // dove finisce il blocco: al marcatore dopo
        while (end < lines.size() && !marker.match(lines[end]).hasMatch())
            ++end;
        int sign = -1;
        for (int k = above ? qMax(0, at - 10) : at; k < (above ? at : end) && sign < 0; ++k)
            if (lines[k].trimmed() == written.trimmed())
                sign = k;
        if (state == "accettata" && sign < 0) {
            if (above) {
                blocks::Block b;
                b.marker = at + 1;
                b.line = at + 2;
                lines.insert(groupStart(lines, b), written);
            } else if (wraps) {
                int after = -1, until = -1;            // dalla riga dopo cui apriva a quella dopo cui chiudeva
                for (int k = at; k < end && after < 0; ++k)
                    if (lines[k] == move.value("dopo").toString())
                        after = k;
                for (int k = after + 1; after >= 0 && k < end && until < 0; ++k)
                    if (lines[k] == move.value("fino").toString())
                        until = k;
                if (until < 0)
                    return QStringLiteral("Non trovo più il disegno da rimpicciolire: la figura è cambiata.");
                lines.insert(until + 1, QString(CLOSE_SIGN));
                lines.insert(after + 1, written);
            } else {
                int after = -1;                        // la riga dopo cui stava
                for (int k = at; k < end && after < 0; ++k)
                    if (lines[k] == move.value("dopo").toString())
                        after = k;
                if (after < 0)
                    return QStringLiteral("Non trovo più il punto in cui spezzare: il riquadro è cambiato.");
                if (after + 1 < lines.size() && !lines[after + 1].trimmed().isEmpty()) {
                    lines.insert(after + 1, written);  // in mezzo a righe piene (fra due voci di un elenco): la riga sola
                } else {
                    lines.insert(after + 1, QString());
                    lines.insert(after + 2, written);
                }
            }
        } else if (state == "lasciata" && sign >= 0) {
            lines.removeAt(sign);
            for (int k = sign; wraps && k < end - 1 && k < lines.size(); ++k)
                if (lines[k].trimmed() == QLatin1String(CLOSE_SIGN)) {       // anche la riga che chiudeva
                    lines.removeAt(k);
                    break;
                }
            if (!above && sign < lines.size() && sign > 0 && lines[sign].trimmed().isEmpty() && lines[sign - 1].trimmed().isEmpty())
                lines.removeAt(sign);                  // delle due righe vuote attorno ne resta una
        }
        if (!files::write(path, lines.join('\n')))
            return QStringLiteral("Non riesco a scrivere la lezione.");
        move["stato"] = state;
        list[i] = move;
        entry["mosse"] = list;
        if (measured)                                  // togliere una riga può portarsi via una riga vuota
            stamp(project, file, &entry, lines.join('\n'));
        all[file] = entry;
        saveRegistry(project, all);
        return {};
    }
    return QStringLiteral("Questa mossa non c'è più.");
}

}

namespace tx::layout {
QList<Shot> shots(const Project &project, const QString &file, const QString &block, const QString &which)
{
    const QString base = shotBase(project, file, block, which);
    const QDir dir = QFileInfo(base).dir();
    const QJsonObject rects = QJsonDocument::fromJson(files::read(base + ".json").toUtf8()).object();
    QList<Shot> out;
    for (const QString &name : dir.entryList({QFileInfo(base).fileName() + "-*.png"}, QDir::Files, QDir::Name)) {
        Shot shot{dir.filePath(name), {}};
        for (const QJsonValue &v : rects.value(QString::number(out.size())).toArray())
            shot.rects << QRectF(v.toObject().value("x").toDouble(), v.toObject().value("y").toDouble(), v.toObject().value("w").toDouble(), v.toObject().value("h").toDouble());
        out << shot;
    }
    return out;
}
}

namespace tx::layout {
void retake(const Project &project, const Style &style, const QString &file, const QString &block, JobControl *control)
{
    if (!Compiler::compileSync(project, file, control).hasErrors())
        snap(project, style, file, block, "dopo");
}

// Toglie delle mosse: la loro riga dalla lezione, la voce dal registro, le pagine in piccolo.
static void drop(const Project &project, const QString &file, const QStringList &blocks)
{
    for (const QString &block : blocks)
        decide(project, file, block, "lasciata");                  // la sua riga, se c'è, esce dalla lezione
    QJsonObject all = registry(project);
    QJsonObject entry = all.value(file).toObject();
    QJsonArray kept;
    for (const QJsonValue &v : entry.value("mosse").toArray())
        if (!blocks.contains(v.toObject().value("blocco").toString()))
            kept.append(v);
    entry["mosse"] = kept;
    all[file] = entry;
    saveRegistry(project, all);
    const QDir dir(project.path("build/impaginazione/" + QFileInfo(file).completeBaseName()));
    for (const QString &block : blocks)
        for (const QString &name : dir.entryList({block + "-prima*", block + "-dopo*"}, QDir::Files))
            QFile::remove(dir.filePath(name));
}

int dropAfter(const Project &project, const QString &file, const QString &block, bool edited)
{
    int line = 0;
    const QList<Move> all = moves(project, file);
    for (const Move &m : all)
        if (m.block == block)
            line = m.line;
    QStringList gone;
    for (const Move &m : all)
        if (m.line > line && (edited || m.state == "fatta" || m.state == "proposta" || m.state == "segnalata"))
            gone << m.block;
    if (gone.isEmpty() && !edited)
        return 0;
    drop(project, file, gone);
    QJsonObject registryNow = registry(project);
    QJsonObject entry = registryNow.value(file).toObject();
    // ciò che resta vale per la lezione com'è adesso, se era quella misurata o se la modifica è stata fatta qui; una lezione
    // cambiata altrove resta com'è, perché `lesson` veda da dove. Il seguito è da rimisurare.
    const QString text = files::read(project.path(file));
    if (edited || entry.value("impronta").toString() == contentHash(text))
        stamp(project, file, &entry, text);
    entry["da_rifare"] = true;
    registryNow[file] = entry;
    saveRegistry(project, registryNow);
    return int(gone.size());
}

QStringList changedBlocks(const Project &project, const QString &file)
{
    const QString was = registry(project).value(file).toObject().value("impronta").toString();
    const QString text = files::read(project.path(file));
    const QList<Move> all = moves(project, file);
    if (all.isEmpty() || was.isEmpty() || was == contentHash(text))
        return {};
    // la prima riga diversa da quando è stata misurata (le mosse non contano)
    const QStringList now = withoutSigns(text.split('\n'));
    const QString measured = files::read(measuredPath(project, file, was));
    qsizetype first = 0;
    if (!measured.isEmpty() && contentHash(measured) == was) {
        const QStringList then = measured.split('\n');
        while (first < now.size() && first < then.size() && now[first] == then[first])
            ++first;
    }
    // i blocchi dal punto cambiato in giù: quello che lo contiene e i successivi
    static const QRegularExpression marker(R"(^\s*%\s*@(L\d+\.\d+))");
    QStringList before;                                // i blocchi che cominciano sopra
    for (qsizetype i = 0; i < now.size() && i <= first; ++i)
        if (const auto m = marker.match(now[i]); m.hasMatch())
            before << m.captured(1);
    if (!before.isEmpty())
        before.removeLast();                           // quello che contiene il punto cambiato
    QStringList out;
    for (const Move &m : all)
        if (!before.contains(m.block))                 // anche le mosse di un blocco che non c'è più
            out << m.block;
    return out;
}

int dropChanged(const Project &project, const QString &file)
{
    const QStringList gone = changedBlocks(project, file);
    if (!gone.isEmpty())
        drop(project, file, gone);
    return int(gone.size());
}
}

namespace tx::layout {
int reset(const Project &project, const QString &file)
{
    const QString path = project.path(file);
    QStringList lines = files::read(path).split('\n');
    int removed = 0;
    for (int i = int(lines.size()) - 1; i >= 0; --i) {
        if (!lines[i].trimmed().endsWith(QLatin1String("% impaginazione")))
            continue;
        lines.removeAt(i);
        ++removed;
        if (i > 0 && i < lines.size() && lines[i].trimmed().isEmpty() && lines[i - 1].trimmed().isEmpty())
            lines.removeAt(i);                         // delle due righe vuote attorno ne resta una
    }
    if (removed)
        files::write(path, lines.join('\n'));
    QJsonObject all = registry(project);
    const int moves = int(all.value(file).toObject().value("mosse").toArray().size());
    all.remove(file);
    saveRegistry(project, all);
    QDir(project.path("build/impaginazione/" + QFileInfo(file).completeBaseName())).removeRecursively();
    return qMax(removed, moves);
}
}

namespace tx::layout {
QString written(const QString &text)
{
    static const QRegularExpression marker(R"(^\s*%\s*@L\d+\.\d+)");
    QStringList out;
    for (const QString &line : text.split('\n'))
        if (!line.trimmed().isEmpty() && !line.trimmed().endsWith(QLatin1String("% impaginazione")) && !marker.match(line).hasMatch())
            out << line;
    return out.join('\n');
}
}

namespace tx::layout {
QString reopen(const Project &project, const QString &file, const QString &block)
{
    QString origin;
    for (const Move &m : moves(project, file))
        if (m.block == block)
            origin = m.origin;
    if (origin.isEmpty())
        return QStringLiteral("Questa mossa non c'è più.");
    // la riga della mossa: c'è se il programma l'aveva fatta, non c'è se l'aveva solo proposta
    QString error;
    if (origin == "fatta")
        error = decide(project, file, block, "accettata");
    else if (origin == "proposta")
        error = decide(project, file, block, "lasciata");
    if (!error.isEmpty())
        return error;
    return decide(project, file, block, origin == "fatta" ? QStringLiteral("fatta") : origin == "proposta" ? QStringLiteral("proposta") : QStringLiteral("segnalata"));
}
}
