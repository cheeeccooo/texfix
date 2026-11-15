#include "core/Convert.h"

#include "core/Blocks.h"
#include "core/Figures.h"
#include "core/Files.h"
#include "core/Fix.h"
#include "core/Layout.h"
#include "core/Model.h"
#include "core/Pages.h"
#include "core/Prompts.h"
#include "core/TexSource.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <future>

using RE = QRegularExpression;

namespace tx {

namespace {
const char *const WORK = "build/lavori/converti";
const char *const RUNNING = "build/lavori/converti/in-corso.json";

QString hash(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex().left(12));
}
QString dump(const QJsonValue &value)
{
    return QString::fromUtf8(value.isArray() ? QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)
                                             : QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
}
QJsonObject readJson(const QString &path) { return QJsonDocument::fromJson(files::read(path).toUtf8()).object(); }
bool writeJson(const QString &path, const QJsonObject &object) { return files::write(path, QString::fromUtf8(QJsonDocument(object).toJson())); }

QString pageList(const QJsonArray &pages)
{
    QStringList out;
    for (const QJsonValue &p : pages)
        out << QString::number(p.toInt());
    return out.join(", ");
}
}

struct Convert::Work {
    QString source;                                    // «fonti/coda/x.pdf»
    QString name, file;                                // «src4», «src/src4.tex»
    QString key;                                       // l'impronta della fonte: la cache vale finché non cambia
    Pages pages;
    QJsonObject outline;
    QList<QJsonObject> chunks;                         // inv, tex, pages
    mutable QList<figures::Figure> figures;            // le figure della lezione, da disegnare o ritagliare
    Lesson lesson;
    QString mismatch;                                  // la riga \lezione come la direbbe la fonte, se non torna con quella scelta
    QString path;                                      // il file della fonte, relativo alla radice (senza «@data»)
    bool isTex = false;                                // un .tex di un altro autore: niente pagine, blocchi di righe
    QDate texDate;                                     // la lezione del .tex, se il file ne ha più d'una
};

int Convert::Result::written() const
{
    int n = 0;
    for (const Lesson &lesson : lessons)
        n += lesson.failure.isEmpty() && !lesson.file.isEmpty();
    return n;
}

Convert::Convert(const Project &project, const Settings &settings, const Style &style, JobControl *control)
    : m_project(project), m_settings(settings), m_style(style), m_control(control)
{
}

QString Convert::workDir(const QString &rel) const { return m_project.path(rel.isEmpty() ? QString::fromUtf8(WORK) : QString::fromUtf8(WORK) + '/' + rel); }

QString Convert::doubtsFile(const QString &lessonFile)
{
    return QStringLiteral("salvataggi/dubbi/%1.json").arg(QFileInfo(lessonFile).completeBaseName());
}

QList<Convert::Source> Convert::queue(const Project &project)
{
    QList<Source> out;
    const QDir dir(project.path("fonti/coda"));
    for (const QString &name : dir.entryList(QDir::Files, QDir::Name | QDir::IgnoreCase)) {
        if (name.startsWith('.'))
            continue;
        const QString kind = Pages::kindOf(name);
        if (kind == "tex") {                           // una riga per lezione, in ordine di calendario; quelle che il corso ha già non si elencano
            const QList<texsource::Lesson> lessons = texsource::lessons(files::read(dir.filePath(name)));
            QStringList have;
            for (const tx::Lesson &lesson : project.lessons())
                have << lesson.date;
            for (const texsource::Lesson &lesson : lessons)
                if (!have.contains(lesson.date.toString("dd/MM/yyyy")))
                    out << Source{texsource::spec("fonti/coda/" + name, lesson.date), kind, lesson.lines};
            if (lessons.isEmpty())
                out << Source{"fonti/coda/" + name, kind, Pages::size(dir.filePath(name))};
            continue;
        }
        out << Source{"fonti/coda/" + name, kind, kind == "altro" ? 0 : Pages::size(dir.filePath(name))};
    }
    return out;
}

double Convert::estimate(const QString &quality, const QList<Source> &sources)
{
    // medie delle conversioni fatte con la vecchia app: una pagina di scansione, cento righe di LaTeX, cento di testo
    const double factor = quality == "veloce" ? 0.45 : quality == "massima" ? 2.5 : 1.0;
    double total = 0;
    for (const Source &s : sources)
        total += s.kind == "pagine" ? 0.12 * s.size : s.kind == "tex" ? 0.23 * s.size / 100.0 : s.kind == "testo" ? 0.08 * s.size / 100.0 : 0;
    return factor * total;
}

QStringList Convert::interrupted(const Project &project)
{
    QStringList out;
    for (const QJsonValue &v : readJson(project.path(RUNNING)).value("fonti").toArray())
        if (QFileInfo::exists(project.path(texsource::parseSpec(v.toString()).first)))
            out << v.toString();
    return out;
}

// La scaletta di una fonte: titolo, numero e data, i blocchi di pagine. Dalla cache se la fonte non è cambiata.
void Convert::plan(Work &work)
{
    const QString cache = workDir(QStringLiteral("%1/scaletta.json").arg(work.key));
    if (QFileInfo::exists(cache)) {
        work.outline = readJson(cache);
        return;
    }
    const int n = work.pages.count();
    if (work.isTex) {                                  // un .tex: blocchi di righe, tagliati fra le unità dell'autore
        const QStringList lines = work.pages.text.split('\n');
        const int count = int(lines.size());
        QString numbered;
        for (int i = 0; i < count; ++i)
            numbered += QStringLiteral("%1| %2\n").arg(i + 1, 4).arg(lines[i]);
        Model model(m_settings.quality, m_project.root(), m_control);
        for (int attempt = 0; attempt < 2 && work.outline.isEmpty() && count > 150; ++attempt) {
            try {
                const QJsonObject o = Model::json(model.ask("outline", prompts::outlineTex(), {QStringLiteral("N = %1 lines.\n\n%2").arg(count).arg(numbered)}, work.name)).toObject();
                int next = 1;
                bool ok = !o.value("chunks").toArray().isEmpty();
                for (const QJsonValue &c : o.value("chunks").toArray()) {
                    ok = ok && c.toObject().value("from").toInt() == next && c.toObject().value("to").toInt() >= next;
                    next = c.toObject().value("to").toInt() + 1;
                }
                if (ok && next == count + 1)
                    work.outline = o;
            } catch (const Stopped &) {
                throw;
            } catch (const LimitReached &) {
                throw;
            } catch (const std::exception &) {
            }
        }
        if (work.outline.isEmpty()) {                  // una lezione corta, o il ripiego: blocchi di 200 righe
            QJsonArray chunks;
            for (int from = 1; from <= count; from += 200)
                chunks.append(QJsonObject{{"from", from}, {"to", qMin(count, from + 199)}, {"subsections", QJsonArray()}});
            work.outline = QJsonObject{{"title", ""}, {"chunks", chunks}};
        }
        QJsonArray chunks = work.outline.value("chunks").toArray();
        for (qsizetype k = 0; k < chunks.size(); ++k) {
            QJsonObject c = chunks[k].toObject();
            c["pages"] = QJsonArray{int(k) + 1};
            chunks[k] = c;
        }
        work.outline["chunks"] = chunks;
        work.outline.insert("costo", model.cost());
    } else if (work.pages.isText) {
        work.outline = QJsonObject{{"title", ""}, {"chunks", QJsonArray{QJsonObject{{"pages", QJsonArray{1}}, {"subsections", QJsonArray()}}}}};
    } else {
        Model model(m_settings.quality, m_project.root(), m_control);
        QStringList parts;
        for (int i = 0; i < n; ++i)
            parts << QStringLiteral("P%1").arg(i + 1) << work.pages.thumb[i];
        parts << QStringLiteral("The file has %1 pages. Plan it.").arg(n);
        for (int attempt = 0; attempt < 2 && work.outline.isEmpty(); ++attempt) {
            try {
                const QJsonObject o = Model::json(model.ask("outline", prompts::outline(), parts, work.name)).toObject();
                QList<int> seen;                        // ogni pagina in un blocco solo, o fra quelle saltate
                for (const QJsonValue &c : o.value("chunks").toArray())
                    for (const QJsonValue &p : c.toObject().value("pages").toArray())
                        seen << p.toInt();
                for (const QJsonValue &s : o.value("skip").toArray())
                    seen << s.toObject().value("page").toInt();
                std::sort(seen.begin(), seen.end());
                bool ok = seen.size() == n && !o.value("chunks").toArray().isEmpty();
                for (int i = 0; ok && i < n; ++i)
                    ok = seen[i] == i + 1;
                if (ok)
                    work.outline = o;
            } catch (const Stopped &) {
                throw;
            } catch (const LimitReached &) {
                throw;
            } catch (const std::exception &) {
            }
        }
        work.lesson.failure.clear();
        if (work.outline.isEmpty()) {                  // ripiego: blocchi fissi di quattro pagine
            QJsonArray chunks;
            for (int i = 1; i <= n; i += 4) {
                QJsonArray pages;
                for (int p = i; p < qMin(i + 4, n + 1); ++p)
                    pages.append(p);
                chunks.append(QJsonObject{{"pages", pages}, {"subsections", QJsonArray()}});
            }
            work.outline = QJsonObject{{"title", ""}, {"chunks", chunks}};
        }
        work.outline.insert("costo", model.cost());
    }
    writeJson(cache, work.outline);
}

// Un blocco: trascrizione, verifica indipendente, correzione di ciò che la verifica ha trovato. Con la cache su disco.
QJsonObject Convert::chunk(const Work &work, int k, Model &model) const
{
    const QJsonArray chunks = work.outline.value("chunks").toArray();
    const QJsonObject spec = chunks[k].toObject();
    const QString cache = workDir(QStringLiteral("%1/blocco%2-%3.json").arg(work.key).arg(k + 1)
                                      .arg(hash((dump(spec) + m_settings.writing + m_style.name + work.file).toUtf8())));
    if (QFileInfo::exists(cache)) {
        if (m_control)
            m_control->log(QStringLiteral("%1, blocco %2: già fatto, ripreso").arg(work.lesson.source).arg(k + 1));
        return readJson(cache);
    }

    QStringList planLines;
    for (int i = 0; i < chunks.size(); ++i) {
        QStringList subs;
        for (const QJsonValue &s : chunks[i].toObject().value("subsections").toArray())
            subs << '"' + s.toString() + '"';
        planLines << QStringLiteral("- chunk %1, pages %2: subsections [%3]").arg(i + 1).arg(pageList(chunks[i].toObject().value("pages").toArray()), subs.join(", "));
    }
    auto parts = [&](const QString &request) {
        QStringList out;
        if (work.isTex) {                              // le righe del blocco, con le macro dell'autore sempre in testa
            const QStringList lines = work.pages.text.split('\n');
            QStringList head;
            for (const QString &line : lines) {
                if (!line.startsWith('%'))
                    break;
                head << line;
            }
            const int a = qMax(1, spec.value("from").toInt()), b = qMin(int(lines.size()), spec.value("to").toInt());
            out << QStringLiteral("SOURCE TEXT (lines %1-%2 of the lecture):\n").arg(a).arg(b)
                       + (a > head.size() ? head.join('\n') + "\n\n" : QString()) + lines.mid(a - 1, b - a + 1).join('\n');
        } else if (work.pages.isText)
            out << "SOURCE TEXT:\n" + work.pages.text;
        else
            for (const QJsonValue &p : spec.value("pages").toArray())
                out << QStringLiteral("P%1").arg(p.toInt()) << work.pages.full.value(p.toInt() - 1);
        out << QStringLiteral("Lecture: %1\nPlan of the whole file:\n%2\n\nYou are chunk %3: pages %4.\n%5")
                   .arg(work.outline.value("title").toString(), planLines.join('\n')).arg(k + 1).arg(pageList(spec.value("pages").toArray()), request);
        return out;
    };
    const QString system = prompts::transcribe(work.file, m_settings, m_style) + (work.isTex ? prompts::texSource(m_style) : QString());
    const QString tag = QStringLiteral("%1#%2").arg(work.name).arg(k + 1);

    QJsonObject result;
    for (int attempt = 0; attempt < 2 && result.value("tex").toString().isEmpty(); ++attempt) {
        const QString answer = model.ask("transcribe", system, parts(QStringLiteral("Write your chunk. It is chunk %1.").arg(k + 1)), tag);
        try {
            result = QJsonObject{{"inv", Model::json(answer)}, {"tex", Model::block(answer, "latex")}};
        } catch (const std::exception &) {
        }
    }
    if (result.value("tex").toString().isEmpty())
        throw std::runtime_error(QStringLiteral("il blocco %1 di %2 non è stato scritto").arg(k + 1).arg(work.lesson.source).toStdString());

    QJsonObject verdict;
    try {
        verdict = Model::json(model.ask("verify", prompts::verify(m_settings) + (work.isTex ? prompts::texVerify() : QString()),
                                        parts("Check this transcription of your pages:\n```latex\n" + result.value("tex").toString() + "\n```"), tag)).toObject();
    } catch (const Stopped &) {
        throw;
    } catch (const LimitReached &) {
        throw;
    } catch (const std::exception &) {                 // una verifica non riuscita non ferma la lezione
    }
    QJsonArray problems = verdict.value("missing").toArray();
    for (const QJsonValue &wrong : verdict.value("wrong").toArray())
        problems.append(wrong);
    if (!problems.isEmpty()) {
        try {
            const QString answer = model.ask("transcribe", system,
                                             parts("Your previous output:\n```json\n" + dump(result.value("inv")) + "\n```\n```latex\n" + result.value("tex").toString()
                                                   + "\n```\nA reviewer found these problems:\n" + dump(problems)
                                                   + "\nFix them (add missing content where it belongs, correct the errors), change nothing else, "
                                                     "and output both blocks again, complete."),
                                             tag + " correzione");
            const QString fixed = Model::block(answer, "latex");
            if (fixed.size() > 0.8 * result.value("tex").toString().size()) {   // una risposta troncata non sostituisce il blocco
                result["inv"] = Model::json(answer);
                result["tex"] = fixed;
            }
        } catch (const Stopped &) {
            throw;
        } catch (const LimitReached &) {
            throw;
        } catch (const std::exception &) {
        }
    }
    result["pages"] = spec.value("pages");
    result["problemi"] = int(problems.size());
    writeJson(cache, result);
    if (m_control)
        m_control->log(QStringLiteral("%1, blocco %2: scritto%3").arg(work.lesson.source).arg(k + 1)
                           .arg(problems.isEmpty() ? QString() : QStringLiteral(", %1 punti corretti dopo la verifica").arg(problems.size())));
    return result;
}

// I blocchi in fila diventano la lezione: figure rinumerate, marcatori di provenienza, intestazione; i dubbi a parte.
void Convert::assemble(Work &work) const
{
    work.figures.clear();
    static const RE slot(R"(\\imgslot(?:\[[^\]]*\])?\{(F\d+)\})");
    static const RE mathSplit(R"(\$[^$]*\$|\\\[.*?\\\])", RE::DotMatchesEverythingOption);
    const blocks::Units units{m_style.boxes(), {m_style.figureWrapper(), "figure"}};
    const QString sourceName = QFileInfo(work.path).fileName();
    QStringList body;
    QJsonArray doubts;
    int nextBlock = 1, figures = 0;
    for (int k = 0; k < work.chunks.size(); ++k) {
        const QJsonObject &c = work.chunks[k];
        QString tex = c.value("tex").toString();
        while (tex.startsWith('\n'))
            tex.remove(0, 1);
        while (tex.endsWith('\n') || tex.endsWith(' '))
            tex.chop(1);

        // le figure sono i segnaposto che ci sono nel testo, rinumerati in tutta la lezione: f01, f02…
        QStringList local;
        for (auto it = slot.globalMatch(tex); it.hasNext();) {
            const QString id = it.next().captured(1);
            if (!local.contains(id))
                local << id;
        }
        for (const QString &id : std::as_const(local)) {
            const QString global = QStringLiteral("\x01%1-%2").arg(work.name).arg(++figures, 2, 10, QChar('0'));   // \x01: non si confonde con un F<k> ancora da cambiare
            QJsonObject known;                          // ciò che il modello ha detto della figura: pagina, posto, descrizione
            for (const QJsonValue &f : c.value("inv").toObject().value("figures").toArray())
                if (f.toObject().value("id").toString() == id)
                    known = f.toObject();
            figures::Figure figure;
            figure.id = global.mid(1);
            figure.source = sourceName;
            figure.page = known.value("page").toInt(c.value("pages").toArray().first().toInt());
            for (const QJsonValue &v : known.value("box").toArray())
                figure.box << v.toDouble();
            const auto slotMatch = RE("\\\\imgslot(?:\\[([^\\]]*)\\])?\\{" + id + "\\}\\{[^{}]*\\}\\{([^{}]*)\\}").match(tex);
            figure.width = slotMatch.captured(1);
            figure.description = known.value("desc").toString(slotMatch.captured(2)).simplified();
            work.figures << figure;
            tex.replace(RE("(% img )" + id + "(?!\\d)"), "\\1" + global);
            tex.replace(RE("(\\\\imgslot(?:\\[[^\\]]*\\])?\\{)" + id + "\\}"), "\\1" + global + "}");
        }
        tex.remove(QChar(1));

        {                                              // \underline nel testo non va a capo: diventa grassetto
            QString out;
            qsizetype last = 0;
            for (auto it = mathSplit.globalMatch(tex); it.hasNext();) {
                const auto m = it.next();
                out += tex.mid(last, m.capturedStart() - last).replace("\\underline{", "\\textbf{") + m.captured(0);
                last = m.capturedEnd();
            }
            tex = out + tex.mid(last).replace("\\underline{", "\\textbf{");
        }

        // ogni blocco con il suo marcatore: la pagina è quella dell'unità dello stesso tipo nell'inventario del modello
        const QJsonArray inventory = c.value("inv").toObject().value("units").toArray();
        const QJsonArray pages = c.value("pages").toArray();
        int unit = 0, page = pages.isEmpty() ? 0 : pages.first().toInt();
        tex = blocks::mark(tex, work.lesson.number, units, [&](const blocks::Block &block) {
            if (work.isTex) {
                const QJsonObject range = work.outline.value("chunks").toArray().at(k).toObject();
                return QStringLiteral("%1 r.%2-%3").arg(sourceName).arg(range.value("from").toInt()).arg(range.value("to").toInt());
            }
            if (work.pages.isText || !page)
                return sourceName;
            const QString env = block.kind == "testo" ? QStringLiteral("text") : block.kind;
            for (int u = unit; u < inventory.size(); ++u) {
                if (inventory[u].toObject().value("env").toString() != env)
                    continue;
                if (const int p = inventory[u].toObject().value("page").toInt(); p > 0)
                    page = p;
                unit = u + 1;
                break;
            }
            return QStringLiteral("%1 p.%2").arg(sourceName).arg(page);
        }, &nextBlock);
        body << tex;

        for (const QJsonValue &d : c.value("inv").toObject().value("doubts").toArray()) {
            QJsonObject doubt = d.toObject();
            if (doubt.value("what").toString().isEmpty())
                continue;
            doubt.insert("id", QStringLiteral("%1-d%2").arg(work.name).arg(doubts.size() + 1));
            doubt.insert("source", sourceName);
            doubts.append(doubt);
        }
    }

    if (!work.mismatch.isEmpty()) {                    // numero e data scelti che la fonte smentisce: un dubbio sulla riga \lezione
        const QString chosen = QStringLiteral("\\lezione{%1}{%2}").arg(work.lesson.number).arg(work.lesson.date);
        doubts.append(QJsonObject{{"id", QStringLiteral("%1-d%2").arg(work.name).arg(doubts.size() + 1)}, {"source", sourceName}, {"page", 1},
                                  {"what", "Numero e data scelti non tornano con quelli scritti nella fonte: quali valgono?"}, {"latex", chosen},
                                  {"readings", QJsonArray{QJsonObject{{"text", "Quelli scelti"}, {"latex", chosen}},
                                                          QJsonObject{{"text", "Quelli della fonte"}, {"latex", work.mismatch}}}}});
    }
    QString title = work.outline.value("title").toString().trimmed();
    if (title.isEmpty())
        title = QFileInfo(work.path).completeBaseName().replace('_', ' ');
    const QString text = QStringLiteral("\\section{%1}\n\\lezione{%2}{%3}\n\n%4\n").arg(title).arg(work.lesson.number).arg(work.lesson.date, body.join("\n\n"));
    if (!files::write(m_project.path(work.file), text))
        throw std::runtime_error("non riesco a scrivere la lezione");
    if (!doubts.isEmpty())
        writeJson(m_project.path(doubtsFile(work.file)), QJsonObject{{"dubbi", doubts}});

    static const RE declared(R"(\\begin\{(?:correction|addedtext)\})");
    work.lesson.title = title;
    work.lesson.doubts = int(doubts.size());
    work.lesson.figures = figures;
    work.lesson.corrections = int(text.count(declared));
}

Convert::Result Convert::run(const QStringList &sources, const QHash<QString, Wanted> &wanted)
{
    Result result;
    QElapsedTimer timer;
    timer.start();
    Model model(m_settings.quality, m_project.root(), m_control);
    writeJson(m_project.path(RUNNING), QJsonObject{{"fonti", QJsonArray::fromStringList(sources)}});
    auto progress = [&](double fraction, const QString &label) {
        if (m_control)
            m_control->progress(fraction, label);
    };

    QList<Work> works;
    try {
        // le pagine e la scaletta di ogni fonte, insieme
        progress(0, QStringLiteral("leggo le fonti"));
        for (const QString &source : sources) {
            const auto [path, date] = texsource::parseSpec(source);
            Work work;
            work.source = source;
            work.path = path;
            work.lesson.source = QFileInfo(path).fileName();
            const QString kind = Pages::kindOf(path);
            if (kind == "altro")
                work.lesson.failure = QStringLiteral("non so leggere questo tipo di file");
            if (kind != "tex") {
                works << work;
                continue;
            }
            // un .tex: una lezione per ogni \section{Lezione del …}, o il documento intero
            work.isTex = true;
            QList<QDate> dates{date};
            if (!date.isValid()) {
                dates.clear();
                for (const texsource::Lesson &lesson : texsource::lessons(files::read(m_project.path(path))))
                    dates << lesson.date;
                if (dates.isEmpty())
                    dates << QDate();
            }
            for (const QDate &d : std::as_const(dates)) {
                work.texDate = d;
                work.source = texsource::spec(path, d);
                if (d.isValid())
                    work.lesson.source = QStringLiteral("%1 · lezione del %2").arg(QFileInfo(path).fileName(), d.toString("dd/MM/yyyy"));
                works << work;
            }
        }
        std::vector<std::future<void>> planning;
        for (Work &work : works) {
            if (!work.lesson.failure.isEmpty())
                continue;
            planning.push_back(std::async(std::launch::async, [this, &work] {
                try {
                    QFile file(m_project.path(work.path));
                    if (!file.open(QIODevice::ReadOnly))
                        throw std::runtime_error("non riesco a leggere la fonte");
                    if (work.isTex) {
                        work.pages.isText = true;
                        work.pages.text = texsource::workerText(QString::fromUtf8(file.readAll()), work.texDate);
                        work.key = hash(work.pages.text.toUtf8());
                    } else {
                        work.key = hash(file.readAll());
                        work.pages = Pages::read(m_project.path(work.path), workDir(work.key + "/pagine"));
                    }
                    plan(work);
                } catch (const Stopped &) {
                    throw;
                } catch (const LimitReached &) {
                    throw;
                } catch (const std::exception &e) {
                    work.lesson.failure = QString::fromUtf8(e.what());
                }
            }));
        }
        std::exception_ptr failure;
        for (auto &f : planning) {
            try {
                f.get();
            } catch (...) {
                if (!failure)
                    failure = std::current_exception();
            }
        }
        if (failure)
            std::rethrow_exception(failure);
        if (m_control)
            m_control->check();

        // nomi e numeri, in ordine: il file dopo l'ultimo srcN, il numero scritto sulle pagine o quello dopo l'ultimo
        static const RE numbered(R"(^src(\d+)$)");
        int lastFile = 0, lastNumber = 0;
        for (const QString &name : QDir(m_project.path("src")).entryList({"*.tex"}, QDir::Files))
            lastFile = qMax(lastFile, numbered.match(QFileInfo(name).completeBaseName()).captured(1).toInt());
        QList<int> taken;
        for (const tx::Lesson &lesson : m_project.lessons()) {
            lastNumber = qMax(lastNumber, lesson.number);
            taken << lesson.number;
        }
        int total = 0;
        for (Work &work : works) {
            if (!work.lesson.failure.isEmpty())
                continue;
            work.name = QStringLiteral("src%1").arg(++lastFile);
            work.file = QStringLiteral("src/%1.tex").arg(work.name);
            const int written = work.outline.value("lesson").toString().toInt();
            const QString writtenDate = work.outline.value("date").toString().trimmed();
            if (const Wanted chosen = wanted.value(work.source); chosen.number > 0) {
                work.lesson.number = chosen.number;    // scelti dall'utente, non dedotti
                work.lesson.date = chosen.date;
                if ((written > 0 && written != chosen.number) || (!writtenDate.isEmpty() && !chosen.date.isEmpty() && writtenDate != chosen.date))
                    work.mismatch = QStringLiteral("\\lezione{%1}{%2}").arg(written > 0 ? written : chosen.number).arg(writtenDate.isEmpty() ? chosen.date : writtenDate);
            } else {
                work.lesson.number = written > 0 && !taken.contains(written) ? written : ++lastNumber;
                work.lesson.date = writtenDate;
            }
            if (work.isTex && work.texDate.isValid() && wanted.value(work.source).number <= 0)
                work.lesson.date = work.texDate.toString("dd/MM/yyyy");
            lastNumber = qMax(lastNumber, work.lesson.number);
            taken << work.lesson.number;
            total += work.outline.value("chunks").toArray().size();
            result.cost += work.outline.value("costo").toDouble();
        }

        // i blocchi di tutte le fonti, insieme (quante chiamate alla volta lo decide Model)
        std::atomic<int> done{0};
        progress(0.1, QStringLiteral("scrivo le lezioni"));
        std::vector<std::pair<Work *, std::future<QJsonObject>>> writing;
        for (Work &work : works) {
            if (work.lesson.failure.isEmpty())
                for (int k = 0; k < work.outline.value("chunks").toArray().size(); ++k)
                    writing.emplace_back(&work, std::async(std::launch::async, [&, k, w = &work] {
                        const QJsonObject c = chunk(*w, k, model);
                        progress(0.1 + 0.85 * ++done / qMax(1, total), QStringLiteral("scrivo le lezioni: blocco %1 di %2").arg(done.load()).arg(total));
                        return c;
                    }));
        }
        for (auto &[work, future] : writing) {
            try {
                work->chunks << future.get();
            } catch (const Stopped &) {
                if (!failure)
                    failure = std::current_exception();
            } catch (const LimitReached &) {
                if (!failure)
                    failure = std::current_exception();
            } catch (const std::exception &e) {
                if (work->lesson.failure.isEmpty())
                    work->lesson.failure = QString::fromUtf8(e.what());
            }
        }
        if (failure)
            std::rethrow_exception(failure);

        // ogni lezione scritta entra nel documento, e la sua fonte va fra quelle usate
        progress(0.96, QStringLiteral("metto insieme le lezioni"));
        for (Work &work : works) {
            if (!work.lesson.failure.isEmpty())
                continue;
            try {
                // una fonte senza scaletta (un testo, una lezione corta di un .tex) non ha ancora un titolo: una chiamata leggera
                if (work.outline.value("title").toString().trimmed().isEmpty() && !work.chunks.isEmpty()) {
                    try {
                        const QString answer = model.ask("outline", prompts::title(), {work.chunks.first().value("tex").toString().left(6000)}, work.name + " titolo");
                        const QString title = Model::json(answer).toObject().value("title").toString().trimmed();
                        if (!title.isEmpty()) {
                            work.outline["title"] = title;
                            writeJson(workDir(QStringLiteral("%1/scaletta.json").arg(work.key)), work.outline);   // non si richiede due volte
                        }
                    } catch (const Stopped &) {
                        throw;
                    } catch (const LimitReached &) {
                        throw;
                    } catch (const std::exception &) {     // senza titolo resta il nome del file
                    }
                }
                assemble(work);
                work.lesson.file = work.file;
                QString main = files::read(m_project.path(Project::MAIN));
                const qsizetype end = main.lastIndexOf("\\end{document}");
                const QString input = QStringLiteral("\\input{%1}\n\\clearpage\n\n").arg(work.file);
                main = end < 0 ? main + '\n' + input : main.left(end) + input + main.mid(end);
                files::write(m_project.path(Project::MAIN), main);
                // le figure, insieme: ridisegnate o ritagliate dalla pagina; quelle che non riescono restano segnaposto
                progress(0.97, QStringLiteral("preparo le figure"));
                std::vector<std::future<QString>> drawing;
                for (const figures::Figure &figure : std::as_const(work.figures)) {
                    figures::save(m_project, figure);
                    const QString page = work.pages.full.value(figure.page - 1);
                    drawing.push_back(std::async(std::launch::async, [this, figure, page, &model] {
                        return m_settings.scanFigures == "ritaglia" ? figures::crop(m_project, figure, page)
                                                                    : figures::draw(m_project, m_settings, m_style, model, figure, page);
                    }));
                }
                std::exception_ptr interrupted;
                work.lesson.figures = 0;
                for (auto &f : drawing) {
                    try {
                        work.lesson.figures += !f.get().isEmpty();
                    } catch (...) {
                        interrupted = std::current_exception();
                    }
                }
                if (interrupted)
                    std::rethrow_exception(interrupted);
                // errori e righe fuori misura della lezione appena scritta
                progress(0.98, QStringLiteral("sistemo errori e righe"));
                const fix::Result fixed = fix::lesson(m_project, m_settings, m_style, model, work.file, m_control);
                work.lesson.errors = fixed.errorsAfter;
                work.lesson.rows = fixed.rowsAfter;
                // l'impaginazione, se il progetto la vuole da sola e la lezione compila
                if (m_settings.layout == "da_sola" && fixed.errorsAfter == 0 && fixed.failure.isEmpty()) {
                    progress(0.99, QStringLiteral("impagino"));
                    const layout::Result paged = layout::lesson(m_project, m_style, work.file, m_control);
                    work.lesson.layoutDone = paged.done;
                    work.lesson.layoutOpen = paged.proposed;
                }
                const QString used = m_project.path(QStringLiteral("fonti/usate/lezione %1").arg(work.lesson.number));
                QDir().mkpath(used);
                if (!work.isTex)                       // un .tex può avere altre lezioni: lo si archivia alla fine, se sono tutte fatte
                    QFile::rename(m_project.path(work.path), files::freeName(used + '/' + work.lesson.source));
            } catch (const Stopped &) {
                throw;
            } catch (const LimitReached &) {
                throw;
            } catch (const std::exception &e) {
                work.lesson.failure = QString::fromUtf8(e.what());
            }
        }
        // un .tex di cui sono state scritte tutte le lezioni va fra le fonti usate
        QHash<QString, bool> texDone;
        for (const Work &work : std::as_const(works))
            if (work.isTex)
                texDone[work.path] = texDone.value(work.path, true) && !work.lesson.file.isEmpty();
        for (auto it = texDone.begin(); it != texDone.end(); ++it) {
            if (!it.value())
                continue;
            bool left = false;                         // restano lezioni di quel file ancora da convertire?
            for (const Source &s : queue(m_project))
                left = left || texsource::parseSpec(s.file).first == it.key();
            if (!left) {
                QDir().mkpath(m_project.path("fonti/usate/più lezioni"));
                QFile::rename(m_project.path(it.key()), files::freeName(m_project.path("fonti/usate/più lezioni/" + QFileInfo(it.key()).fileName())));
            }
        }
        QFile::remove(m_project.path(RUNNING));        // arrivato in fondo: niente da riprendere
    } catch (const Stopped &) {
        result.stopped = true;
    } catch (const LimitReached &) {
        result.failure = QStringLiteral("Il modello ha raggiunto il limite di utilizzo: ciò che è fatto resta, il resto si riprende più tardi.");
    } catch (const std::exception &e) {
        result.failure = QString::fromUtf8(e.what());
    }
    for (const Work &work : std::as_const(works))
        result.lessons << work.lesson;
    result.cost += model.cost();
    result.minutes = timer.elapsed() / 60000.0;
    return result;
}

}
