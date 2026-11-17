#include "core/Figures.h"

#include "core/Files.h"
#include "core/Rules.h"
#include "core/TexLog.h"
#include "core/Tools.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QPainter>
#include <QRegularExpression>

using RE = QRegularExpression;

namespace tx::figures {

namespace {
const char *const REGISTRY = "salvataggi/figure.json";
QMutex g_lock;                                         // il registro e le lezioni si scrivono uno alla volta

QJsonObject toJson(const Figure &f)
{
    QJsonArray box;
    for (const double v : f.box)
        box.append(v);
    QJsonArray erase;
    for (const QList<double> &zone : f.erase) {
        QJsonArray z;
        for (const double v : zone)
            z.append(v);
        erase.append(z);
    }
    QJsonObject o{{"nome", f.id}, {"fonte", f.source}, {"pagina", f.page}, {"riquadro", box}, {"descrizione", f.description},
                  {"larghezza", f.width}, {"stato", f.state}, {"nota", f.note}, {"costo", f.cost}};
    if (!f.decision.isEmpty())
        o["decisione"] = f.decision;
    if (!erase.isEmpty())
        o["cancellino"] = erase;
    if (f.boxChosen)
        o["riquadro_scelto"] = true;
    return o;
}
Figure fromJson(const QJsonObject &o)
{
    Figure f;
    f.id = o.value("nome").toString();
    f.source = o.value("fonte").toString();
    f.page = o.value("pagina").toInt();
    for (const QJsonValue &v : o.value("riquadro").toArray())
        f.box << v.toDouble();
    f.description = o.value("descrizione").toString();
    f.width = o.value("larghezza").toString();
    f.state = o.value("stato").toString("da_inserire");
    f.note = o.value("nota").toString();
    f.cost = o.value("costo").toDouble();
    f.decision = o.value("decisione").toString();
    for (const QJsonValue &z : o.value("cancellino").toArray()) {
        QList<double> zone;
        for (const QJsonValue &v : z.toArray())
            zone << v.toDouble();
        if (zone.size() == 4)
            f.erase << zone;
    }
    f.boxChosen = o.value("riquadro_scelto").toBool();
    return f;
}
QString workDir(const Project &project, const QString &id) { return project.path("build/lavori/figure/" + id); }
QString mark(const QString &id) { return "% img " + id; }
// Ciò che di una figura si tiene (costa, o è una scelta): l'ultimo disegno, il codice dell'autore.
QString drawingFile(const Project &project, const QString &id) { return project.path("salvataggi/figure/" + id + ".tex"); }
QString authorFile(const Project &project, const QString &id) { return project.path("salvataggi/figure/" + id + "-autore.tex"); }

// Le righe della figura nella lezione: quella del \begin con il segno e quella del suo \end.
bool span(const QStringList &lines, const QString &id, int *begin, int *end, QString *indent = nullptr)
{
    static const RE open(R"(^(\s*)\\begin\{([A-Za-z]+\*?)\})");
    const RE sign(RE::escape(mark(id)) + R"((?![\w-]))");
    for (int i = 0; i < lines.size(); ++i) {
        if (!sign.match(lines[i]).hasMatch())
            continue;
        const auto m = open.match(lines[i]);
        if (!m.hasMatch())
            return false;
        const QString close = "\\end{" + m.captured(2) + "}";
        int last = i + 1;
        while (last < lines.size() && !lines[last].trimmed().startsWith(close))
            ++last;
        if (last >= lines.size())
            return false;
        *begin = i;
        *end = last;
        if (indent)
            *indent = m.captured(1);
        return true;
    }
    return false;
}

// Compila il disegno da solo, con il preambolo del progetto. Vuoto se compila; altrimenti il primo errore.
QString compile(const Project &project, const QString &id, const QString &code, QString *png, bool *tooWide = nullptr)
{
    const QString dir = workDir(project, id);
    QDir().mkpath(dir);
    // il preambolo del documento, com'è (i pacchetti che l'utente carica in main.tex, come circuitikz, servono), più
    // quelli che il disegno chiede con «% package:»
    QString preamble = files::read(project.path("main.tex"));
    const qsizetype start = preamble.indexOf("\\begin{document}");
    preamble = start > 0 ? preamble.left(start) : QStringLiteral("\\documentclass[11pt,a4paper]{article}\n\\usepackage{texfix}\n");
    static const RE asked(R"(^\s*%\s*package:\s*(.+)$)", RE::MultilineOption);
    for (auto it = asked.globalMatch(code); it.hasNext();)
        for (const QString &name : it.next().captured(1).split(',', Qt::SkipEmptyParts))
            if (!preamble.contains("{" + name.trimmed() + "}"))
                preamble += "\\usepackage{" + name.trimmed() + "}\n";
    files::write(dir + "/fig.tex", preamble + "\\pagestyle{empty}\n\\begin{document}\n\\setlength{\\columnseprule}{0pt}\\thispagestyle{empty}\n\\begin{center}\n" + code + "\n\\end{center}\n\\end{document}\n");
    QFile::remove(dir + "/fig.pdf");
    // percorsi relativi alla cartella del progetto, da cui pdflatex parte: TeX non legge un percorso con «~» (iCloud)
    const QDir root(project.root());
    tools::run("pdflatex", {"-interaction=nonstopmode", "-halt-on-error", "-file-line-error", "-output-directory=" + root.relativeFilePath(dir),
                            root.relativeFilePath(dir + "/fig.tex")},
               project.root(), 120000);
    if (!QFileInfo::exists(dir + "/fig.pdf")) {
        for (const Issue &issue : texlog::parse(files::read(dir + "/fig.log")))
            if (issue.kind == Issue::Error)
                return issue.text.isEmpty() ? QStringLiteral("errore di LaTeX") : issue.text;
        return QStringLiteral("il disegno non compila");
    }
    tools::run("pdftoppm", {"-png", "-r", "110", "-singlefile", dir + "/fig.pdf", dir + "/fig"}, dir, 60000);
    if (tooWide)
        *tooWide = files::read(dir + "/fig.log").contains("Overfull \\hbox");
    // la resa è una pagina intera: se ne tiene solo il disegno, con un po' di margine
    QImage page(dir + "/fig.png");
    if (!page.isNull()) {
        QRect ink;
        for (int y = 0; y < page.height(); ++y)
            for (int x = 0; x < page.width(); ++x)
                if (qGray(page.pixel(x, y)) < 245)
                    ink |= QRect(x, y, 1, 1);
        if (!ink.isEmpty())
            page.copy(ink.adjusted(-12, -12, 12, 12).intersected(page.rect())).save(dir + "/fig.png");
    }
    if (png)
        *png = dir + "/fig.png";
    return {};
}

QString picture(const QString &answer)
{
    QString code = Model::block(answer, "latex").trimmed();
    static const RE body(R"(\\begin\{tikzpicture\}.*\\end\{tikzpicture\})", RE::DotMatchesEverythingOption);
    const auto m = body.match(code);
    return m.hasMatch() ? m.captured(0) : QString();
}
}

QList<Figure> all(const Project &project)
{
    QList<Figure> out;
    for (const QJsonValue &v : QJsonDocument::fromJson(files::read(project.path(REGISTRY)).toUtf8()).object().value("figure").toArray())
        out << fromJson(v.toObject());
    return out;
}

Figure find(const Project &project, const QString &id)
{
    for (const Figure &f : all(project))
        if (f.id == id)
            return f;
    return {};
}

void save(const Project &project, const Figure &figure)
{
    QMutexLocker lock(&g_lock);
    QJsonArray list = QJsonDocument::fromJson(files::read(project.path(REGISTRY)).toUtf8()).object().value("figure").toArray();
    bool found = false;
    for (qsizetype i = 0; i < list.size() && !found; ++i) {
        if (list[i].toObject().value("nome").toString() == figure.id) {
            list[i] = toJson(figure);
            found = true;
        }
    }
    if (!found)
        list.append(toJson(figure));
    files::write(project.path(REGISTRY), QString::fromUtf8(QJsonDocument(QJsonObject{{"figure", list}}).toJson()));
}

QString lessonOf(const Project &project, const QString &id)
{
    const RE sign(RE::escape(mark(id)) + R"((?![\w-]))");
    for (const QString &file : project.inputs())
        if (sign.match(files::read(project.path(file))).hasMatch())
            return file;
    return {};
}

int pending(const Project &project, const QString &lesson)
{
    const QString text = files::read(project.path(lesson));
    int n = 0;
    for (const Figure &f : all(project))
        n += (f.state == "da_inserire" || f.decision.isEmpty()) && text.contains(mark(f.id));       // da inserire, o da rivedere
    return n;
}

QString place(const Project &project, const QString &id, const QString &content)
{
    QMutexLocker lock(&g_lock);
    const QString lesson = lessonOf(project, id);
    if (lesson.isEmpty())
        return QStringLiteral("La figura non è più in nessuna lezione.");
    QStringList lines = files::read(project.path(lesson)).split('\n');
    int begin = -1, end = -1;
    QString indent;
    if (!span(lines, id, &begin, &end, &indent))
        return QStringLiteral("Non trovo il posto della figura nella lezione.");
    QStringList inner;
    for (const QString &line : content.split('\n'))
        inner << (line.trimmed().isEmpty() ? QString() : indent + "    " + line);
    lines = lines.mid(0, begin + 1) + inner + lines.mid(end);
    return files::write(project.path(lesson), lines.join('\n')) ? QString() : QStringLiteral("Non riesco a scrivere la lezione.");
}

QString prompt(const Settings &settings, const Style &style)
{
    return QStringLiteral(
               R"(You redraw one hand-drawn figure from lecture notes as TikZ/pgfplots code. The drawing is a quick freehand sketch: it is the conceptual basis of the figure, not a model to trace. Work out what it is meant to show, from the sketch together with the surrounding LaTeX, then draw that figure as a careful textbook illustration.
- Keep everything the sketch means: every object, axis, curve, region, point, arrow and label, and the relations between them.
- Never reproduce the imperfections of the hand: draw the ideal object. Whenever the text or the formulas say what a curve is, plot the real function (`plot[domain=..., samples=...]` or pgfplots), never a curve through points read off by eye.
- Every arrow starts from something and points at something visible. A tick, a guide line or a dot sits exactly on the place it marks. Labels use the notation of the surrounding LaTeX, touch what they name and overlap nothing.
- Circuits are drawn with `circuitikz` (`to[R, l=$R$]`, `node[ground]{}`…), never with hand-made symbols; wires horizontal and vertical.
- Do not invent content the sketch and the text do not support, and do not drop anything the sketch shows.
What the program needs (binding):
- Output only one ```latex block containing exactly one tikzpicture environment, at most %1 wide.
- The document loads TikZ and pgfplots with the libraries arrows.meta, calc, angles, quotes, intersections: no positioning, decorations, patterns or shapes. Never write \usepackage or \usetikzlibrary. If the figure needs a TikZ-based package of its own (a circuit: circuitikz), declare it with a comment line `% package: circuitikz` as the first line inside the tikzpicture.
)").arg("0.95\\linewidth")
           + "\nEDITORIAL RULES\n\n" + rules::forPrompt({"figure"}, settings.rules) + "\n\nDOCUMENT STYLE\n\n" + style.forPrompt();
}

QString draw(const Project &project, const Settings &settings, const Style &style, Model &model, Figure figure, const QString &pageImage,
             const QString &instruction)
{
    auto fail = [&](const QString &why) {
        figure.note = why;
        save(project, figure);
        return why;
    };
    // ciò che sta attorno alla figura nella lezione: dice che cosa deve mostrare
    const QString lesson = lessonOf(project, figure.id);
    QString around;
    if (!lesson.isEmpty()) {
        const QStringList lines = files::read(project.path(lesson)).split('\n');
        for (int i = 0; i < lines.size(); ++i)
            if (lines[i].contains(mark(figure.id)))
                around = lines.mid(qMax(0, i - 25), 45).join('\n');
    }
    const QString system = prompt(settings, style);
    QStringList parts;
    if (QFileInfo::exists(pageImage))
        parts << QStringLiteral("The page with the sketch:") << pageImage;
    parts << QStringLiteral("The figure to redraw: %1%2\n\nSurrounding LaTeX (the figure goes where `%3` is):\n```latex\n%4\n```")
                 .arg(figure.description,
                      figure.box.size() == 4 ? QStringLiteral("\nIts position on the page, as fractions from the top-left corner: x %1-%2, y %3-%4.")
                                                   .arg(figure.box[0]).arg(figure.box[2]).arg(figure.box[1]).arg(figure.box[3])
                                             : QString(),
                      mark(figure.id), around);
    // «Rigenera»: il disegno com'è adesso, e che cosa cambiare. Senza una fonte, il disegno com'è è tutto ciò che c'è.
    if (!instruction.trimmed().isEmpty() || !QFileInfo::exists(pageImage)) {
        if (!QFileInfo::exists(pageImage))
            parts << QStringLiteral("There is no source sketch: the figure is already drawn in the notes. Redraw it keeping everything it shows.");
        if (const QString now = rendered(project, figure.id); !now.isEmpty())
            parts << QStringLiteral("How the figure is drawn now in the notes:") << now;
        if (const QString current = code(project, figure.id); !current.trimmed().isEmpty())
            parts << "Its current code:\n```latex\n" + current + "\n```";
        if (!instruction.trimmed().isEmpty())
            parts << "What the author of the notes asks to change (follow it):\n" + instruction.trimmed();
    }
    // un disegno già fatto e pagato per questa stessa richiesta si riusa (un lavoro ripreso)
    const QString cache = workDir(project, figure.id) + "/disegno-"
                          + QString::fromLatin1(QCryptographicHash::hash((system + parts.join(QChar(0))).toUtf8(), QCryptographicHash::Sha1).toHex().left(12)) + ".tex";
    QString code = files::read(cache), error, png;
    try {
        const bool reused = !code.isEmpty() && compile(project, figure.id, code, &png).isEmpty();
        if (!reused)
            code = picture(model.ask("figure", system, parts, figure.id));
        if (!reused)
            error = code.isEmpty() ? QStringLiteral("il modello non ha dato un disegno") : compile(project, figure.id, code, &png);
        if (!error.isEmpty()) {                        // una seconda prova, con l'errore
            const QString again = picture(model.ask("figure", system,
                                                    parts + QStringList{"Your code:\n```latex\n" + code + "\n```\ndoes not compile: " + error
                                                                        + "\nOutput the corrected code, complete."},
                                                    figure.id + " errore"));
            if (!again.isEmpty()) {
                code = again;
                error = compile(project, figure.id, code, &png);
            }
        }
        if (!error.isEmpty())
            return fail(QStringLiteral("Il disegno non compila (%1).").arg(error));

        // la revisione: il modello guarda la resa del suo disegno accanto allo schizzo
        const QString verdict = reused ? QStringLiteral("OK") : model.ask("figure", system,
                                          parts + QStringList{png, "The last image is how your code renders:\n```latex\n" + code + "\n```\n"
                                              "Compare it with what the sketch means: every object and label is there, each point, segment and arrow is "
                                              "where it is meant to be, curves have the shape the formulas give them, labels overlap nothing, nothing "
                                              "points at nothing. The sketch is freehand: a rendering more regular than the sketch is correct.\n"
                                              "If everything matches answer exactly OK. Otherwise answer with the complete corrected code in one "
                                              "```latex block."},
                                          figure.id + " revisione");
        if (const QString revised = picture(verdict); !revised.isEmpty() && revised != code) {
            const QString previous = code;
            if (compile(project, figure.id, revised, &png).isEmpty())
                code = revised;
            else
                compile(project, figure.id, previous, &png);                           // resta quello che compilava
        }
    } catch (const Stopped &) {
        throw;
    } catch (const LimitReached &) {
        throw;
    } catch (const std::exception &e) {
        return fail(QString::fromUtf8(e.what()));
    }

    // i pacchetti che il disegno chiede entrano fra quelli del progetto
    static const RE package(R"(^\s*%\s*package:\s*(.+)$)", RE::MultilineOption);
    if (const auto m = package.match(code); m.hasMatch()) {
        QMutexLocker lock(&g_lock);
        Settings saved = Settings::load(project);
        for (const QString &name : m.captured(1).split(',', Qt::SkipEmptyParts))
            if (!saved.packages.contains(name.trimmed()))
                saved.packages << name.trimmed();
        saved.save(project);
        project.complete();
    }
    // un disegno più largo della colonna si restringe quanto basta (le proporzioni restano)
    bool tooWide = false;
    compile(project, figure.id, code, &png, &tooWide);
    if (tooWide && !code.startsWith("\\resizebox")) {
        const QString fitted = "\\resizebox{\\linewidth}{!}{%\n" + code + "%\n}";
        if (compile(project, figure.id, fitted, &png, &tooWide).isEmpty() && !tooWide)
            code = fitted;
    }
    files::write(cache, code);
    if (const QString error = place(project, figure.id, code); !error.isEmpty())
        return fail(error);
    files::write(drawingFile(project, figure.id), code);       // l'ultimo disegno resta, anche passando al ritaglio
    figure.state = "disegnata";
    figure.note.clear();
    figure.decision.clear();                                   // una figura nuova è da rivedere
    save(project, figure);
    return {};
}

QString crop(const Project &project, Figure figure, const QString &pageImage)
{
    QImage page(pageImage);
    if (page.isNull() || figure.box.size() != 4) {
        figure.note = QStringLiteral("Non so dov'è la figura nella pagina.");
        save(project, figure);
        return figure.note;
    }
    // il cancellino: le zone coperte di bianco
    if (!figure.erase.isEmpty()) {
        page = page.convertToFormat(QImage::Format_RGB32);
        QPainter painter(&page);
        for (const QList<double> &z : std::as_const(figure.erase))
            painter.fillRect(QRectF(QPointF(z[0] * page.width(), z[1] * page.height()), QPointF(z[2] * page.width(), z[3] * page.height())).normalized(), Qt::white);
    }
    // un po' di margine attorno se il riquadro l'ha detto il modello (approssimato); esatto se l'ha scelto l'utente
    const double margin = figure.boxChosen ? 0.0 : 0.02;
    const QRectF unit = QRectF(QPointF(figure.box[0] - margin, figure.box[1] - margin), QPointF(figure.box[2] + margin, figure.box[3] + margin))
                            .normalized().intersected(QRectF(0, 0, 1, 1));
    const QRect pixels(qRound(unit.x() * page.width()), qRound(unit.y() * page.height()), qRound(unit.width() * page.width()),
                       qRound(unit.height() * page.height()));
    const QString file = QStringLiteral("imgs/%1.png").arg(figure.id);
    QDir().mkpath(project.path("imgs"));
    if (pixels.isEmpty() || !page.copy(pixels).save(project.path(file))) {
        figure.note = QStringLiteral("Non riesco a ritagliare la figura.");
        save(project, figure);
        return figure.note;
    }
    if (figure.state == "disegnata")                           // il disegno che c'era resta, per tornarci
        if (const QString drawn = code(project, figure.id); !drawn.trimmed().isEmpty())
            files::write(drawingFile(project, figure.id), drawn);
    const QString width = figure.width.isEmpty() ? QStringLiteral("0.8\\linewidth") : figure.width;
    if (const QString error = place(project, figure.id, QStringLiteral("\\includegraphics[width=%1]{%2}").arg(width, file)); !error.isEmpty()) {
        figure.note = error;
        save(project, figure);
        return error;
    }
    figure.state = "ritagliata";
    figure.note.clear();
    figure.decision.clear();
    save(project, figure);
    return {};
}

QString preview(const Project &project, const Figure &figure)
{
    const QString cropped = project.path(QStringLiteral("imgs/%1.png").arg(figure.id));
    if (figure.state == "ritagliata" && QFileInfo::exists(cropped))
        return cropped;
    const QString drawn = workDir(project, figure.id) + "/fig.png";
    return QFileInfo::exists(drawn) ? drawn : QString();
}

}

namespace tx::figures {

Authored authored(const Project &project, const Settings &settings, const Style &style, Model &model, const QString &lesson, const QString &source)
{
    Authored out;
    const QString path = project.path(lesson), stem = QFileInfo(lesson).completeBaseName();
    const QStringList wrappers{style.figureWrapper(), "figure", "center"};
    static const RE drawing(R"(\\begin\{(tikzpicture|circuitikz)\})");
    int from = 0;
    for (int guard = 0; guard < 200; ++guard) {
        QStringList lines = files::read(path).split('\n');
        // la prossima figura dell'autore: un disegno dentro l'ambiente delle figure, senza il segno di una figura di Texfix
        int begin = -1, end = -1, first = -1, last = -1;
        QString wrapper, kind;
        for (int i = from; i < lines.size() && begin < 0; ++i) {
            for (const QString &w : wrappers) {
                if (!lines[i].trimmed().startsWith("\\begin{" + w + "}") || lines[i].contains("% img "))
                    continue;
                int e = i + 1;
                while (e < lines.size() && !lines[e].trimmed().startsWith("\\end{" + w + "}"))
                    ++e;
                if (e >= lines.size())
                    continue;
                for (int k = i + 1; k < e && first < 0; ++k)
                    if (const auto m = drawing.match(lines[k]); m.hasMatch()) {
                        first = k;
                        kind = m.captured(1);
                    }
                if (first < 0)
                    continue;
                for (int k = e - 1; k >= first && last < 0; --k)
                    if (lines[k].contains("\\end{" + kind + "}"))
                        last = k;
                if (last < 0) {
                    first = -1;
                    continue;
                }
                begin = i;
                end = e;
                wrapper = w;
                break;
            }
        }
        if (begin < 0)
            break;
        from = end + 1;
        QString code = lines.mid(first, last - first + 1).join('\n');
        const QString work = QStringLiteral("%1-autore-%2").arg(stem).arg(guard + 1);
        QString png;
        bool tooWide = false;
        if (!compile(project, work, code, &png, &tooWide).isEmpty()) {
            ++out.failed;                              // non compila da sola: resta com'è, la vedranno gli errori della lezione
            continue;
        }
        if (settings.texFigures == "ridisegna") {
            // il disegnatore la rifà nello stile del progetto, guardando la resa del codice dell'autore
            int next = 0;
            for (const Figure &f : all(project))
                if (f.id.startsWith(stem + '-'))
                    next = qMax(next, f.id.section('-', -1).toInt());
            Figure figure;
            figure.id = QStringLiteral("%1-%2").arg(stem).arg(next + 1, 2, 10, QChar('0'));
            figure.source = source;
            figure.description = QStringLiteral("Figura dell'autore: va ridisegnata uguale (stessi elementi, etichette e proporzioni) nello stile del progetto.");
            lines[begin] += " " + mark(figure.id);
            files::write(path, lines.join('\n'));
            save(project, figure);
            files::write(authorFile(project, figure.id), code);     // l'originale, da mettere accanto in Rivedi figure
            if (draw(project, settings, style, model, figure, png).isEmpty()) {
                ++out.redrawn;
            } else {                                   // non è riuscita: resta il disegno dell'autore, senza segno
                QStringList back = files::read(path).split('\n');
                for (QString &line : back)
                    if (line.endsWith(" " + mark(figure.id)))
                        line.chop(mark(figure.id).size() + 1);
                files::write(path, back.join('\n'));
                figure.state = "disegnata";
                figure.note = QStringLiteral("È rimasto il disegno dell'autore: ridisegnarlo non è riuscito.");
                save(project, figure);
                ++out.kept;
            }
            from = begin + 1;                          // il contenuto è cambiato: si riparte da dopo il suo inizio
            continue;
        }
        // «tieni»: il codice dell'autore resta, asciugato (via i suoi commenti e le righe vuote) solo se la resa è identica
        {
            QStringList dry;
            for (const QString &line : code.split('\n')) {
                QString kept = line.contains("% package:") ? line : uncommented(line).toString();
                while (kept.endsWith(' ') || kept.endsWith('\t'))
                    kept.chop(1);
                if (!kept.trimmed().isEmpty())
                    dry << kept;
            }
            const QString tidy = dry.join('\n');
            QString other;
            if (tidy != code && !tidy.isEmpty() && compile(project, work + "-asciutta", tidy, &other).isEmpty() && !QImage(png).isNull()
                && QImage(other) == QImage(png)) {
                for (int k = last; k >= first; --k)
                    lines.removeAt(k);
                for (qsizetype k = 0; k < dry.size(); ++k)
                    lines.insert(first + k, dry[k]);
                last = first + int(dry.size()) - 1;
                code = tidy;
                files::write(path, lines.join('\n'));
                from = last + 2;
                ++out.tidied;
            }
        }
        // se è più largo della colonna si restringe quanto basta
        const bool alreadyFitted = first > 0 && lines[first - 1].trimmed().startsWith("\\resizebox");
        if (tooWide && !alreadyFitted && !code.trimmed().startsWith("\\resizebox")) {
            const QString fitted = "\\resizebox{\\linewidth}{!}{%\n" + code + "%\n}";
            if (compile(project, work, fitted, &png, &tooWide).isEmpty() && !tooWide) {
                for (int k = last; k >= first; --k)
                    lines.removeAt(k);
                lines.insert(first, fitted);
                files::write(path, lines.join('\n'));
                from = first + int(fitted.count('\n')) + 2;
                ++out.fitted;
                continue;
            }
        }
        ++out.kept;
    }
    return out;
}

}

namespace tx::figures {

namespace {
// Gli ambienti in cui sta una figura, e ciò che dice che dentro c'è davvero una figura.
QStringList wrappers(const Style &style)
{
    QStringList out{style.figureWrapper(), "figure"};
    out.removeDuplicates();
    return out;
}
// Le figure di una lezione senza segno: le righe del loro \begin.
QList<int> unmarked(const QStringList &lines, const Style &style)
{
    static const RE drawn(R"(\\begin\{(tikzpicture|circuitikz|axis)\}|\\includegraphics)");
    QList<int> out;
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines[i].trimmed();
        for (const QString &w : wrappers(style)) {
            if (!line.startsWith("\\begin{" + w + "}"))
                continue;
            int e = i + 1;
            while (e < lines.size() && !lines[e].trimmed().startsWith("\\end{" + w + "}"))
                ++e;
            if (e >= lines.size())
                break;
            bool inside = false;
            for (int k = i + 1; k < e && !inside; ++k)
                inside = drawn.matchView(uncommented(lines[k])).hasMatch();
            if (inside && !line.contains("% img "))
                out << i;
            if (inside)
                i = e;                                 // una figura dentro un'altra (figure con tikzgraph) è la stessa
            break;
        }
    }
    return out;
}
// Il testo fra le graffe che cominciano a `from` (bilanciate); -1 in *next se non ci sono.
QString braced(const QString &text, qsizetype from, qsizetype *next)
{
    *next = -1;
    if (from >= text.size() || text[from] != '{')
        return {};
    int depth = 0;
    for (qsizetype k = from; k < text.size(); ++k) {
        if (text[k] == '\\') {
            ++k;
            continue;
        }
        if (text[k] == '{')
            ++depth;
        else if (text[k] == '}' && --depth == 0) {
            *next = k + 1;
            return text.mid(from + 1, k - from - 1);
        }
    }
    return {};
}
QString keyOf(const Project &project, const QString &id)
{
    const QString c = code(project, id);
    if (c.trimmed().isEmpty())
        return {};
    QByteArray h = c.toUtf8();
    // le immagini incluse e gli stili: se cambiano, cambia la resa
    static const RE image(R"(\\includegraphics\s*(?:\[[^\]]*\])?\s*\{([^}]+)\})");
    for (auto it = image.globalMatch(c); it.hasNext();) {
        const QString name = it.next().captured(1).trimmed();
        for (const QString &ext : {QString(), QStringLiteral(".png"), QStringLiteral(".pdf"), QStringLiteral(".jpg")})
            if (const QFileInfo f(project.path(name + ext)); f.exists())
                h += f.lastModified().toString(Qt::ISODateWithMs).toUtf8();
    }
    for (const QFileInfo &f : QDir(project.root()).entryInfoList({"*.sty"}, QDir::Files))
        h += f.fileName().toUtf8() + f.lastModified().toString(Qt::ISODateWithMs).toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(h, QCryptographicHash::Sha1).toHex().left(12));
}
}

int unadopted(const Project &project, const Style &style)
{
    int n = 0;
    for (const QString &file : project.inputs())
        n += int(unmarked(files::read(project.path(file)).split('\n'), style).size());
    return n;
}

int adopt(const Project &project, const Style &style)
{
    int n = 0;
    const QList<Figure> known = all(project);
    for (const QString &file : project.inputs()) {
        QStringList lines = files::read(project.path(file)).split('\n');
        const QList<int> found = unmarked(lines, style);
        if (found.isEmpty())
            continue;
        const QString stem = QFileInfo(file).completeBaseName();
        int next = 0;                                  // il numero dopo l'ultimo usato, nel registro o nella lezione
        for (const Figure &f : known)
            if (f.id.startsWith(stem + '-'))
                next = qMax(next, f.id.section('-', -1).toInt());
        static const RE used(R"(% img ([\w.]+)-(\d+))");
        for (auto it = used.globalMatch(lines.join('\n')); it.hasNext();)
            if (const auto m = it.next(); m.captured(1) == stem)
                next = qMax(next, m.captured(2).toInt());
        QList<Figure> fresh;
        for (const int at : found) {
            Figure f;
            f.id = QStringLiteral("%1-%2").arg(stem).arg(++next, 2, 10, QChar('0'));
            f.state = "scritta";
            lines[at] += " " + mark(f.id);
            fresh << f;
        }
        files::write(project.path(file), lines.join('\n'));
        for (const Figure &f : std::as_const(fresh))
            save(project, f);
        n += int(fresh.size());
    }
    return n;
}

QString code(const Project &project, const QString &id)
{
    const QString lesson = lessonOf(project, id);
    if (lesson.isEmpty())
        return {};
    const QStringList lines = files::read(project.path(lesson)).split('\n');
    int begin = -1, end = -1;
    if (!span(lines, id, &begin, &end))
        return {};
    QStringList inner = lines.mid(begin + 1, end - begin - 1);
    // senza il rientro comune
    qsizetype common = -1;
    for (const QString &line : std::as_const(inner)) {
        if (line.trimmed().isEmpty())
            continue;
        qsizetype lead = 0;
        while (lead < line.size() && line[lead].isSpace())
            ++lead;
        common = common < 0 ? lead : qMin(common, lead);
    }
    for (QString &line : inner)
        line = line.trimmed().isEmpty() ? QString() : line.mid(qMax<qsizetype>(common, 0));
    return inner.join('\n');
}

QString caption(const Project &project, const Style &style, const QString &id)
{
    const QString lesson = lessonOf(project, id);
    if (lesson.isEmpty())
        return {};
    const QStringList lines = files::read(project.path(lesson)).split('\n');
    int begin = -1, end = -1;
    if (!span(lines, id, &begin, &end))
        return {};
    // dentro la figura, o subito dopo (fino a una riga vuota, o a un'altra figura)
    QStringList near = lines.mid(begin, end - begin + 1);
    for (int k = end + 1; k < lines.size() && k <= end + 8; ++k) {
        if (lines[k].trimmed().startsWith("\\begin{") || (lines[k].trimmed().isEmpty() && k > end + 1))
            break;
        near << lines[k];
    }
    QStringList clean;
    for (const QString &line : std::as_const(near))
        clean << uncommented(line).toString();
    const QString text = clean.join('\n');
    // il comando della didascalia: la descrizione dello stile può dirlo con un esempio («\\figcaption[…]{…} sulla riga…»)
    static const RE name(R"(^\s*(\\[A-Za-z@]+))");
    const QString styled = name.match(style.captionCommand).captured(1);
    for (const QString &command : {styled, QStringLiteral("\\caption")}) {
        if (command.isEmpty())
            continue;
        const RE at(RE::escape(command) + R"((?![A-Za-z])\s*(\[[^\]]*\])?\s*)");
        const auto m = at.match(text);
        if (!m.hasMatch())
            continue;
        qsizetype next = -1;
        const QString found = braced(text, m.capturedEnd(), &next);
        if (next > 0)
            return found.simplified();
    }
    return {};
}

QString origin(const Project &project, const Figure &figure)
{
    if (QFileInfo::exists(authorFile(project, figure.id)))
        return QStringLiteral("autore");
    if (!figure.source.isEmpty() && figure.page > 0 && figure.box.size() == 4)
        return QStringLiteral("fonte");
    return {};
}

QString rendered(const Project &project, const QString &id)
{
    const QString key = keyOf(project, id);
    if (key.isEmpty())
        return {};
    const QString file = workDir(project, id) + "/resa/resa-" + key + ".png";
    return QFileInfo::exists(file) ? file : QString();
}

bool render(const Project &project, const QString &id)
{
    if (!rendered(project, id).isEmpty())
        return true;
    const QString key = keyOf(project, id);
    QString png;
    if (key.isEmpty() || !compile(project, id + "/resa", code(project, id), &png).isEmpty())
        return false;
    const QDir dir(workDir(project, id) + "/resa");
    for (const QString &old : dir.entryList({"resa-*.png"}, QDir::Files))
        QFile::remove(dir.filePath(old));
    return QFile::copy(png, dir.filePath("resa-" + key + ".png"));
}

QString originalPath(const Project &project, const Figure &figure)
{
    const QString file = workDir(project, figure.id) + "/originale.png";
    return !origin(project, figure).isEmpty() && QFileInfo::exists(file) ? file : QString();
}

bool original(const Project &project, const Figure &figure, const QString &pageImage)
{
    const QString kind = origin(project, figure), file = workDir(project, figure.id) + "/originale.png";
    if (kind.isEmpty() || QFileInfo::exists(file))
        return !kind.isEmpty();
    QDir().mkpath(workDir(project, figure.id));
    if (kind == "autore") {
        QString png;
        return compile(project, figure.id + "/autore", files::read(authorFile(project, figure.id)), &png).isEmpty() && QFile::copy(png, file);
    }
    const QImage page(pageImage);
    if (page.isNull())
        return false;
    // il pezzo di pagina com'era, con il margine del riquadro detto dal modello: senza cancellino
    const QRectF unit = QRectF(QPointF(figure.box[0] - 0.02, figure.box[1] - 0.02), QPointF(figure.box[2] + 0.02, figure.box[3] + 0.02))
                            .normalized().intersected(QRectF(0, 0, 1, 1));
    const QRect pixels(qRound(unit.x() * page.width()), qRound(unit.y() * page.height()), qRound(unit.width() * page.width()),
                       qRound(unit.height() * page.height()));
    return !pixels.isEmpty() && page.copy(pixels).save(file);
}

QString drawing(const Project &project, const QString &id) { return files::read(drawingFile(project, id)); }

QString useDrawing(const Project &project, const QString &id)
{
    Figure figure = find(project, id);
    const QString drawn = drawing(project, id);
    if (drawn.trimmed().isEmpty())
        return QStringLiteral("Questa figura non ha ancora un disegno.");
    if (const QString error = place(project, id, drawn); !error.isEmpty())
        return error;
    figure.state = "disegnata";
    figure.decision.clear();
    figure.note.clear();
    save(project, figure);
    return {};
}

void decide(const Project &project, const QString &id, bool accepted)
{
    Figure figure = find(project, id);
    if (figure.id.isEmpty())
        return;
    figure.decision = accepted ? QStringLiteral("accettata") : QString();
    save(project, figure);
}

}
