#include "core/Layout.h"

#include "core/Blocks.h"
#include "core/Compiler.h"
#include "core/Files.h"
#include "core/SyncTex.h"
#include "core/Tools.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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
    return {{"blocco", m.block}, {"che_cosa", m.what}, {"vuoto", m.hole}, {"stato", m.state}, {"segno", m.sign}, {"dopo", m.after}, {"fino", m.until}};
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
Where whereIs(const Project &project, const Geometry &g, const QString &lesson, int line)
{
    const synctex::Spot s = synctex::toPdf(project.root(), g.pdf, lesson, line);
    if (!s.ok)
        return {};
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

    // dove cade una riga del sorgente; se quella non lascia segno nel PDF, una vicina
    auto spot = [&](int line, int step, int until) {
        for (int k = 0; k < 4 && line >= 1 && line != until + step; ++k, line += step) {
            if (control)
                control->check();
            const synctex::Spot s = synctex::toPdf(project.root(), built.pdf, lesson, line);
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
    return registry(project).value(file).toObject().value("impronta").toString() != contentHash(text);
}

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
                    where.value(o.value("blocco").toString()), o.value("segno").toString(QString::fromUtf8(SIGN)), o.value("dopo").toString(), o.value("fino").toString()};
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
    int seam = 0, depth = 0;
    for (int line = block.line; line < low; ++line) {
        const QString code = uncommented(lines[line - 1]).toString();
        depth += int(code.count(QLatin1String("\\begin{")) - code.count(QLatin1String("\\end{")));
        if (depth == 1 && lines[line - 1].trimmed().isEmpty() && line - block.line >= qMax(2, int(0.07 * (block.endLine - block.line))))
            seam = line;
    }
    if (!seam)
        return false;
    int last = seam - 1;                               // l'ultima riga piena prima della cucitura
    while (last > block.line && lines[last - 1].trimmed().isEmpty())
        --last;
    static const QRegularExpression lead(R"(^\s*)");
    const QString sign = lead.match(lines[last - 1]).captured(0) + style.breakCommand + " % impaginazione";
    lines.insert(seam, sign);                          // fra due righe vuote
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
        return false;
    }
    move->hole = hole;
    move->sign = sign;
    move->after = lines[last - 1];
    return true;
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
            if (!s.ok)
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
    for (const QString &old : dir.entryList({QFileInfo(base).fileName() + "-*.png"}, QDir::Files))
        QFile::remove(dir.filePath(old));
    if (page > 0)
        tools::run("pdftoppm", {"-png", "-r", "60", "-f", QString::number(page), "-l", QString::number(page + 1), g.pdf, base}, project.root(), 30000);
    // i contorni: dove sta il blocco in ognuna delle due pagine, in frazioni della pagina
    const QStringList made = dir.entryList({QFileInfo(base).fileName() + "-*.png"}, QDir::Files, QDir::Name);
    const double pageHeight = made.isEmpty() ? 1 : qMax(1.0, QImageReader(dir.filePath(made.first())).size().height() * 72.0 / 60.0);
    QJsonObject rects;
    for (const Piece &p : std::as_const(pieces)) {
        if (p.page < page || p.page > page + 1)
            continue;
        const double x0 = p.column == 0 ? margin - 5 : p.x - 5;
        const double x1 = g.twoColumns && p.column == 0 ? g.pageWidth / 2 - 6 : g.pageWidth - margin + 5;
        QJsonArray list = rects.value(QString::number(p.page - page)).toArray();
        list.append(QJsonObject{{"x", x0 / g.pageWidth}, {"y", (p.top - 3) / pageHeight}, {"w", (x1 - x0) / g.pageWidth}, {"h", (p.bottom - p.top + 7) / pageHeight}});
        rects[QString::number(p.page - page)] = list;
    }
    files::write(base + ".json", QString::fromUtf8(QJsonDocument(rects).toJson()));
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
                const Where start = whereIs(project, g, file, block.line);
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
}

Result lesson(const Project &project, const Style &style, const QString &file, JobControl *control)
{
    Result result;
    const QString path = project.path(file);
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
        if (m.state == "lasciata" || m.state == "a_mano" || m.state == "agente" || m.state == "risolta")
            left << m.block;                       // decise in altro modo: non si riprovano
    }
    QList<Move> fresh;
    QStringList tried = left;
    for (int round = 0; round < 40; ++round) {
        const QList<Placed> placed = measure(project, style, file, control, &result.failure);
        if (!result.failure.isEmpty())
            return result;
        const Placed *problem = nullptr;
        for (const Placed &p : placed)
            if (p.kind != "testo" && p.split() && !p.block.isEmpty() && !tried.contains(p.block)) {
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
            move.what = QStringLiteral("%1 cadeva a cavallo di %2 in un punto qualunque: ora si spezza fra due paragrafi (vuoto lasciato: %3%).").arg(name, two).arg(qRound(move.hole * 100));
            fresh << move;
            ++result.done;
            return true;
        };
        if (problem->lastPage - problem->firstPage > 1 || problem->top < 0.02) {       // più alto di una colonna: si spezza comunque
            if (!inside())
                fresh << Move{file, problem->block, QStringLiteral("%1 è più lungo di %2: resta spezzato.").arg(name, style.twoColumns() ? QStringLiteral("una colonna") : QStringLiteral("una pagina")), hole, "lasciata", 0};
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
                          hole, whole ? "proposta" : "lasciata", 0};
            result.proposed += whole;
        }
    }
    // poi ciò che non è un'unità spezzata: un titolo rimasto in fondo senza il suo testo, un grande vuoto prima di un'unità
    QStringList settled = tried;
    for (const Move &m : kept + fresh)
        settled << m.block;
    static const QRegularExpression heading(R"(^\s*\\(?:sub)*section\*?[\[{])");
    for (int round = 0; round < 12; ++round) {
        if (control)
            control->check();
        const QString before = files::read(path);
        QStringList lines = before.split('\n');
        const QList<blocks::Block> parsed = blocks::parse(before, unitsOf(style));
        const Geometry g = geometryOf(project, style, file);
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
            if (wt.ok && wc.ok && wc.slot > wt.slot) {
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
                together = wt.ok && wc.ok && wt.slot == wc.slot;
            }
        } catch (...) {
            files::write(path, before);
            throw;
        }
        if (together && hole <= HOLE_OK) {
            fresh << Move{file, id, QStringLiteral("Un titolo restava in fondo senza il suo testo: ora passa %1 dopo con lui (vuoto lasciato: %2%).").arg(where).arg(percent), hole, "fatta", 0};
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
    for (int round = 0; round < 6; ++round) {
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
            auto report = [&](int line, const QString &what) {
                const QString id = owner(line);
                if (id.isEmpty() || settled.contains(id))
                    return;
                snap(project, style, file, id, "prima");
                settled << id;
                ++result.found;
                ++result.proposed;
                fresh << Move{file, id, what, 0, "segnalata", 0};
            };
            static const QRegularExpression displayEnd(R"(^\s*(\\\]|\\end\{(align|equation|gather|multline)\*?\}))");
            static const QRegularExpression listBegin(R"(^\s*\\begin\{(itemize|enumerate)\})"), listEnd(R"(^\s*\\end\{(itemize|enumerate)\})");
            const QString side = style.twoColumns() ? QStringLiteral("colonna") : QStringLiteral("pagina");
            for (int i = 1; i + 1 < lines.size(); ++i) {
                if (control)
                    control->check();
                if (displayEnd.match(lines[i]).hasMatch()) {
                    const QString next = lines[i + 1].trimmed();
                    if (next.isEmpty() || next.startsWith('%') || next.startsWith("\\begin") || next.startsWith("\\end") || next.startsWith("\\["))
                        continue;                      // dopo la formula comincia altro: non è il suo commento
                    Where formula;
                    for (int k = i; k >= i - 3 && k >= 1 && !formula.ok; --k)
                        formula = whereIs(project, g, file, k);
                    const Where comment = whereIs(project, g, file, i + 2);
                    if (formula.ok && comment.ok && comment.slot > formula.slot)
                        report(i + 1, QStringLiteral("Una formula resta in fondo alla %1, separata dalla frase che la commenta. Si può sistemare a mano o affidare all'agente.").arg(side));
                } else if (listBegin.match(lines[i]).hasMatch()) {
                    QList<int> places;                  // dove cade ogni voce del primo livello
                    int depth = 0, j = i;
                    for (; j < lines.size(); ++j) {
                        if (listBegin.match(lines[j]).hasMatch())
                            ++depth;
                        else if (listEnd.match(lines[j]).hasMatch() && --depth == 0)
                            break;
                        else if (depth == 1 && lines[j].trimmed().startsWith("\\item"))
                            if (const Where w = whereIs(project, g, file, j + 1); w.ok)
                                places << w.slot;
                    }
                    if (places.size() >= 3 && places.first() != places.last()
                        && (places.count(places.first()) == 1 || places.count(places.last()) == 1))
                        report(i + 1, QStringLiteral("Un elenco lascia una voce sola dall'altra parte della %1. Si può sistemare a mano o affidare all'agente.").arg(side));
                    i = j;
                }
            }
        }
        for (qsizetype i = 1; i < parsed.size(); ++i) {
            const blocks::Block &b = parsed[i];
            if (b.kind == "testo" || b.id.isEmpty() || settled.contains(b.id))
                continue;
            const Where start = whereIs(project, g, file, b.line);
            Where end;
            for (int line = parsed[i - 1].endLine; line >= parsed[i - 1].line && !end.ok; --line)
                end = whereIs(project, g, file, line);
            if (!start.ok || !end.ok || start.slot <= end.slot || 1.0 - end.bottom <= HOLE_OK)
                continue;
            snap(project, style, file, b.id, "prima");
            settled << b.id;
            if (b.kind == "figura") {
                Move move{file, b.id, {}, 1.0 - end.bottom, "fatta", 0};
                if (shrink(project, style, file, b, parsed[i - 1], control, &move)) {
                    ++result.found;
                    ++result.done;
                    fresh << move;
                    shrunk = true;                     // le righe sono cambiate: si rimisura da capo
                    break;
                }
            }
            Placed p;
            p.kind = b.kind;
            p.title = b.title;
            ++result.found;
            ++result.proposed;
            fresh << Move{file, b.id, QStringLiteral("%1 non ci stava ed è passato oltre: prima resta vuoto il %2% %3. Si può accorciare o spostare a mano, o affidare all'agente.")
                                          .arg(nameOf(p, style)).arg(qRound((1.0 - end.bottom) * 100)).arg(style.twoColumns() ? QStringLiteral("della colonna") : QStringLiteral("della pagina")),
                          1.0 - end.bottom, "segnalata", 0};
        }
        if (!shrunk)
            break;
    }
    {   // com'è venuta ogni mossa fatta
        bool any = false;
        for (const Move &m : std::as_const(fresh))
            any = any || m.state == "fatta";
        if (any) {
            Compiler::compileSync(project, file, control);
            for (const Move &m : std::as_const(fresh))
                if (m.state == "fatta")
                    snap(project, style, file, m.block, "dopo");
        }
    }
    QJsonArray list;
    for (const Move &m : kept + fresh)
        list.append(toJson(m));
    QJsonObject all = registry(project);
    all[file] = QJsonObject{{"impronta", contentHash(files::read(path))}, {"mosse", list}};
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
        if (state != "accettata" && state != "lasciata") {         // a_mano, agente, risolta: cambia solo ciò che si ricorda
            move["stato"] = state;
            list[i] = move;
            entry["mosse"] = list;
            entry["impronta"] = contentHash(files::read(project.path(file)));
            all[file] = entry;
            saveRegistry(project, all);
            return {};
        }
        const QString path = project.path(file);
        QStringList lines = files::read(path).split('\n');
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
                lines.insert(after + 1, QString());
                lines.insert(after + 2, written);
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
        entry["impronta"] = contentHash(lines.join('\n'));
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
