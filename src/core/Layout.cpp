#include "core/Layout.h"

#include "core/Blocks.h"
#include "core/Compiler.h"
#include "core/Files.h"
#include "core/SyncTex.h"
#include "core/Tools.h"

#include <QCryptographicHash>
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
        if (line.trimmed() != QLatin1String(SIGN))
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
        if (above.isEmpty() || above.startsWith("\\subsection") || above.startsWith("\\paragraph") || above.startsWith("\\label"))
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
    return {{"blocco", m.block}, {"che_cosa", m.what}, {"vuoto", m.hole}, {"stato", m.state}};
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
    return registry(project).value(file).toObject().value("impronta").toString() != contentHash(files::read(project.path(file)));
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
                    where.value(o.value("blocco").toString())};
    }
    return out;
}

int toReview(const Project &project, const QString &file)
{
    int n = 0;
    for (const Move &m : moves(project, file))
        n += m.state == "proposta" || m.state == "fatta";
    return n;
}

Result lesson(const Project &project, const Style &style, const QString &file, JobControl *control)
{
    Result result;
    const QString path = project.path(file);
    // le decisioni prese restano: gli a capo accettati non si toccano, i blocchi lasciati com'erano non si riprovano
    QList<Move> kept;
    QStringList left;
    for (const Move &m : moves(project, file)) {
        if (m.state == "accettata" || m.state == "lasciata" || m.state == "fatta")
            kept << m;
        if (m.state == "lasciata")
            left << m.block;
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
        const double hole = 1.0 - problem->top;
        const QString name = nameOf(*problem, style);
        const QString two = style.twoColumns() ? QStringLiteral("due colonne") : QStringLiteral("due pagine");
        const QString fresh1 = style.twoColumns() ? QStringLiteral("a colonna nuova") : QStringLiteral("a pagina nuova");
        const QString whole1 = style.twoColumns() ? QStringLiteral("della colonna") : QStringLiteral("della pagina");
        const int percent = qRound(hole * 100);
        if (problem->lastPage - problem->firstPage > 1 || problem->top < 0.02) {       // più alto di una pagina: si spezza comunque
            fresh << Move{file, problem->block, QStringLiteral("%1 è più lungo di %2: resta spezzato.").arg(name, style.twoColumns() ? QStringLiteral("una colonna") : QStringLiteral("una pagina")), hole, "lasciata", 0};
            continue;
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
            fresh << Move{file, problem->block,
                          whole ? QStringLiteral("%1 cade a cavallo di %3. Portarlo %4 lascerebbe vuoto il %2% %5.").arg(name).arg(percent).arg(two, fresh1, whole1)
                                : QStringLiteral("%1 cade a cavallo di %2 e un a capo non basta: va sistemato a mano.").arg(name, two),
                          hole, whole ? "proposta" : "lasciata", 0};
            result.proposed += whole;
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
        const QString path = project.path(file);
        QStringList lines = files::read(path).split('\n');
        static const QRegularExpression marker(R"(^\s*%\s*@(L\d+\.\d+))");
        int at = -1;                                   // il marcatore del blocco
        for (int k = 0; k < lines.size() && at < 0; ++k)
            if (marker.match(lines[k]).captured(1) == block)
                at = k;
        if (at < 0)
            return QStringLiteral("Il blocco non è più nella lezione.");
        // l'a capo pagina di questa mossa, se c'è: sopra il blocco, prima dei titoli che lo aprono
        int sign = -1;
        for (int k = at - 1; k >= 0 && k >= at - 6 && sign < 0; --k)
            if (lines[k].trimmed() == QLatin1String(SIGN))
                sign = k;
        if (state == "accettata" && sign < 0) {
            blocks::Block b;
            b.marker = at + 1;
            b.line = at + 2;
            lines.insert(groupStart(lines, b), QString::fromUtf8(SIGN));
        } else if (state == "lasciata" && sign >= 0) {
            lines.removeAt(sign);
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
