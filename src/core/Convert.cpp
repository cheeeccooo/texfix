#include "core/Convert.h"

#include "core/Blocks.h"
#include "core/Figures.h"
#include "core/Files.h"
#include "core/Fix.h"
#include "core/Layout.h"
#include "core/Model.h"
#include "core/NewLesson.h"
#include "core/Pages.h"
#include "core/Prompts.h"
#include "core/Reorganize.h"
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
    int texNth = 1;                                    // la prima, la seconda… lezione di quel giorno
    QString note;                                      // dal piano: ciò che il corso spiega già e non si riscrive
    bool assembled = false;                            // già nel documento da un lavoro interrotto: si riprende dalle figure
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
        if (kind == "tex") {                           // una riga per lezione, in ordine di calendario; quelle che il corso ha già
            const QList<newlesson::TexLecture> lectures = newlesson::texLectures(project, "fonti/coda/" + name);   // o del giorno, no
            for (const newlesson::TexLecture &t : lectures)
                if (t.in.isEmpty() && t.sameDate.isEmpty())
                    out << Source{t.spec, kind, t.lesson.lines};
            if (lectures.isEmpty())
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

QStringList Convert::interrupted(const Project &project, QHash<QString, Wanted> *wanted)
{
    QStringList out;
    const QJsonObject running = readJson(project.path(RUNNING)), chosen = running.value("scelte").toObject();
    for (const QJsonValue &v : running.value("fonti").toArray()) {
        if (!QFileInfo::exists(project.path(texsource::parseSpec(v.toString()).path)))
            continue;
        out << v.toString();
        if (const QJsonObject w = chosen.value(v.toString()).toObject(); wanted && !w.isEmpty())
            wanted->insert(v.toString(), Wanted{w.value("numero").toInt(), w.value("data").toString(), w.value("argomento").toString(),
                                                w.value("titolo").toString(), w.value("dopo").toString(), w.value("nota").toString()});
    }
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
        // ciò che il corso spiega già (dal piano): chi scrive non lo riscrive, chi verifica non lo cerca
        const QString known = work.note.isEmpty() ? QString()
            : QStringLiteral("\n\nTHE COURSE ALREADY EXPLAINS: %1\nWhere this source repeats those things, do not write them again: one sentence that "
                             "recalls them and a reference to that lecture; write in full only what is new. When checking, these omissions are "
                             "not missing content.").arg(work.note);
        out << QStringLiteral("Lecture: %1\nPlan of the whole file:\n%2\n\nYou are chunk %3: pages %4.\n%5")
                   .arg(work.outline.value("title").toString(), planLines.join('\n')).arg(k + 1).arg(pageList(spec.value("pages").toArray()), request) + known;
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
    QJsonObject chosen;                                // numero, data e argomento scelti nel piano: valgono anche alla ripresa
    for (auto it = wanted.begin(); it != wanted.end(); ++it)
        chosen.insert(it.key(), QJsonObject{{"numero", it->number}, {"data", it->date}, {"argomento", it->topic}, {"titolo", it->title}, {"dopo", it->after}, {"nota", it->note}});
    // un lavoro ripreso: i nomi dati alle lezioni e quelle già messe nel documento restano (niente lezioni scritte due volte)
    const QJsonObject before = readJson(m_project.path(RUNNING));
    QJsonObject names = before.value("nomi").toObject();
    QStringList assembled;
    for (const QJsonValue &v : before.value("scritte").toArray())
        assembled << v.toString();
    QMutex runningMutex;
    auto saveRunning = [&] {
        QMutexLocker lock(&runningMutex);
        writeJson(m_project.path(RUNNING), QJsonObject{{"fonti", QJsonArray::fromStringList(sources)}, {"scelte", chosen}, {"nomi", names},
                                                       {"scritte", QJsonArray::fromStringList(assembled)}});
    };
    saveRunning();
    auto progress = [&](double fraction, const QString &label) {
        if (m_control)
            m_control->progress(fraction, label);
    };
    // per chi guarda: ogni lezione da scrivere è un cantiere, con le sue fasi (Scaletta, Blocchi, Lezione, Figure,
    // Errori e righe, Impaginazione)
    auto site = [&](const Work &work, const QString &phase, const QString &state, const QJsonObject &extra = {}) {
        if (!m_control)
            return;
        QJsonObject e{{"tipo", "cantiere"}, {"chiave", work.source}, {"genere", "converti"}, {"fase", phase}, {"stato", state}};
        if (!work.file.isEmpty())
            e["file"] = work.file;
        if (work.lesson.number > 0)
            e["numero"] = work.lesson.number;
        if (!work.outline.value("title").toString().trimmed().isEmpty())
            e["titolo"] = work.outline.value("title").toString().trimmed();
        for (auto it = extra.begin(); it != extra.end(); ++it)
            e[it.key()] = it.value();
        m_control->event(e);
    };

    QList<Work> works;
    try {
        // le pagine e la scaletta di ogni fonte, insieme
        progress(0, QStringLiteral("leggo le fonti"));
        for (const QString &source : sources) {
            const texsource::Ref ref = texsource::parseSpec(source);
            const QString path = ref.path;
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
            QList<texsource::Ref> refs{ref};
            if (!ref.date.isValid()) {
                refs.clear();
                for (const texsource::Lesson &lesson : texsource::lessons(files::read(m_project.path(path))))
                    refs << texsource::Ref{path, lesson.date, lesson.nth};
                if (refs.isEmpty())
                    refs << ref;
            }
            for (const texsource::Ref &r : std::as_const(refs)) {
                work.texDate = r.date;
                work.texNth = r.nth;
                work.source = texsource::spec(path, r.date, r.nth);
                if (r.date.isValid())
                    work.lesson.source = QStringLiteral("%1 · %2").arg(QFileInfo(path).fileName(), texsource::label(r));
                works << work;
            }
        }
        std::vector<std::future<void>> planning;
        for (Work &work : works) {
            if (!work.lesson.failure.isEmpty())
                continue;
            planning.push_back(std::async(std::launch::async, [this, &work, &site] {
                Site here(work.source);
                site(work, QStringLiteral("Scaletta"), QStringLiteral("in_corso"));
                try {
                    QFile file(m_project.path(work.path));
                    if (!file.open(QIODevice::ReadOnly))
                        throw std::runtime_error("non riesco a leggere la fonte");
                    if (work.isTex) {
                        work.pages.isText = true;
                        work.pages.text = texsource::workerText(QString::fromUtf8(file.readAll()), work.texDate, work.texNth);
                        work.key = hash(work.pages.text.toUtf8());
                    } else {
                        work.key = hash(file.readAll());
                        work.pages = Pages::read(m_project.path(work.path), workDir(work.key + "/pagine"));
                    }
                    plan(work);
                    site(work, QStringLiteral("Scaletta"), QStringLiteral("fatta"));
                } catch (const Stopped &) {
                    throw;
                } catch (const LimitReached &) {
                    throw;
                } catch (const std::exception &e) {
                    work.lesson.failure = QString::fromUtf8(e.what());
                    site(work, QStringLiteral("Scaletta"), QStringLiteral("fallita"), {{"motivo", work.lesson.failure}});
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

        // nomi e numeri, in ordine: il file dopo l'ultimo (anche di quelli che ora stanno in un argomento), il numero
        // scritto sulle pagine o quello dopo l'ultimo
        int lastFile = newlesson::lastFile(m_project), lastNumber = newlesson::nextNumber(m_project) - 1;
        QList<int> taken;
        for (const tx::Lesson &lesson : m_project.lessons()) {
            lastNumber = qMax(lastNumber, lesson.number);
            taken << lesson.number;
        }
        int total = 0;
        for (Work &work : works) {
            if (!work.lesson.failure.isEmpty())
                continue;
            // un lavoro ripreso: la lezione già messa nel documento resta com'è, con il suo nome, numero e data
            const QString kept = names.value(work.source).toString();
            if (!kept.isEmpty() && assembled.contains(work.source) && m_project.inputs().contains(kept)) {
                work.file = kept;
                work.name = QFileInfo(kept).completeBaseName();
                work.assembled = true;
                const tx::Lesson there = Project::lessonOf(kept, files::read(m_project.path(kept)));
                work.lesson.number = there.number;
                work.lesson.date = there.date;
                work.lesson.title = there.title;
                work.outline["title"] = there.title;
                site(work, QStringLiteral("Blocchi"), QStringLiteral("fatta"));
                site(work, QStringLiteral("Lezione"), QStringLiteral("fatta"));
                continue;
            }
            if (!kept.isEmpty() && !QFileInfo::exists(m_project.path(kept))) {
                work.file = kept;                      // lo stesso nome di prima: le schede e la cache lo riconoscono
                work.name = QFileInfo(kept).completeBaseName();
            } else {
                do
                    work.name = QStringLiteral("src%1").arg(++lastFile);
                while (QFileInfo::exists(m_project.path("src/" + work.name + ".tex")));
                work.file = QStringLiteral("src/%1.tex").arg(work.name);
                names[work.source] = work.file;
            }
            const int written = work.outline.value("lesson").toString().toInt();
            const QString writtenDate = work.outline.value("date").toString().trimmed();
            work.note = wanted.value(work.source).note;
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
            site(work, QStringLiteral("Blocchi"), QStringLiteral("in_corso"), {{"fatti", 0}, {"totale", int(work.outline.value("chunks").toArray().size())}});
        }
        saveRunning();

        // i blocchi di tutte le fonti, insieme (quante chiamate alla volta lo decide Model)
        std::atomic<int> done{0};
        progress(0.1, QStringLiteral("scrivo le lezioni"));
        std::vector<std::pair<Work *, std::future<QJsonObject>>> writing;
        for (Work &work : works) {
            if (work.lesson.failure.isEmpty() && !work.assembled)
                for (int k = 0; k < work.outline.value("chunks").toArray().size(); ++k)
                    writing.emplace_back(&work, std::async(std::launch::async, [&, k, w = &work] {
                        Site here(w->source);
                        const QJsonObject c = chunk(*w, k, model);
                        site(*w, QStringLiteral("Blocchi"), QStringLiteral("in_corso"), {{"piu", 1}});
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
            Site here(work.source);
            try {
                if (!work.assembled) {
                    site(work, QStringLiteral("Lezione"), QStringLiteral("in_corso"));
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
                    assembled << work.source;              // da qui una ripresa non la riscrive
                    saveRunning();
                    site(work, QStringLiteral("Lezione"), QStringLiteral("fatta"));
                    if (work.isTex) {                      // le figure dell'autore: tenute (e adattate alla colonna) o ridisegnate
                        progress(0.96, QStringLiteral("sistemo le figure dell'autore"));
                        const figures::Authored a = figures::authored(m_project, m_settings, m_style, model, work.file, QFileInfo(work.path).fileName());
                        if (a.kept + a.fitted + a.redrawn + a.failed > 0)
                            m_control->log(QStringLiteral("%1, figure dell'autore: %2 tenute, %3 adattate alla colonna, %4 ridisegnate, %5 che non compilano da sole")
                                               .arg(work.file).arg(a.kept).arg(a.fitted).arg(a.redrawn).arg(a.failed));
                    }
                } else {                                 // ripresa: le figure ancora da disegnare sono quelle del registro
                    work.lesson.file = work.file;
                    work.figures.clear();
                    for (const figures::Figure &figure : figures::all(m_project))
                        if (figure.state == "da_inserire" && figures::lessonOf(m_project, figure.id) == work.file)
                            work.figures << figure;
                }
                // le figure, insieme: ridisegnate o ritagliate dalla pagina; quelle che non riescono restano segnaposto
                progress(0.97, QStringLiteral("preparo le figure"));
                site(work, QStringLiteral("Figure"), work.figures.isEmpty() ? QStringLiteral("saltata") : QStringLiteral("in_corso"),
                     {{"fatti", 0}, {"totale", int(work.figures.size())}});
                std::vector<std::future<QString>> drawing;
                for (const figures::Figure &figure : std::as_const(work.figures)) {
                    figures::save(m_project, figure);
                    const QString page = work.pages.full.value(figure.page - 1);
                    drawing.push_back(std::async(std::launch::async, [this, figure, page, &model, &site, &work] {
                        Site here(work.source);
                        const QString done = m_settings.scanFigures == "ritaglia" ? figures::crop(m_project, figure, page)
                                                                                 : figures::draw(m_project, m_settings, m_style, model, figure, page);
                        site(work, QStringLiteral("Figure"), QStringLiteral("in_corso"), {{"piu", 1}});
                        return done;
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
                if (!work.figures.isEmpty())
                    site(work, QStringLiteral("Figure"), QStringLiteral("fatta"));
                // errori e righe fuori misura della lezione appena scritta
                progress(0.98, QStringLiteral("sistemo errori e righe"));
                site(work, QStringLiteral("Errori e righe"), QStringLiteral("in_corso"));
                const fix::Result fixed = fix::lesson(m_project, m_settings, m_style, model, work.file, m_control);
                work.lesson.errors = fixed.errorsAfter;
                work.lesson.rows = fixed.rowsAfter;
                site(work, QStringLiteral("Errori e righe"), QStringLiteral("fatta"), {{"errori", fixed.errorsAfter}, {"righe", fixed.rowsAfter}});
                // l'impaginazione, se il progetto la vuole da sola e la lezione compila
                if (m_settings.layout == "da_sola" && fixed.errorsAfter == 0 && fixed.failure.isEmpty()) {
                    progress(0.99, QStringLiteral("impagino"));
                    site(work, QStringLiteral("Impaginazione"), QStringLiteral("in_corso"));
                    const layout::Result paged = layout::lesson(m_project, m_style, work.file, m_control);
                    work.lesson.layoutDone = paged.done;
                    work.lesson.layoutOpen = paged.proposed;
                    site(work, QStringLiteral("Impaginazione"), QStringLiteral("fatta"), {{"mosse", paged.done}, {"proposte", paged.proposed}});
                } else {
                    site(work, QStringLiteral("Impaginazione"), QStringLiteral("saltata"));
                }
                const QString used = m_project.path(QStringLiteral("fonti/usate/lezione %1").arg(work.lesson.number));
                QDir().mkpath(used);
                if (!work.isTex)                       // un .tex può avere altre lezioni: lo si archivia alla fine, se sono tutte fatte
                    QFile::rename(m_project.path(work.path), files::freeName(used + '/' + work.lesson.source));
                site(work, QStringLiteral("Fine"), QStringLiteral("fatta"));
            } catch (const Stopped &) {
                throw;
            } catch (const LimitReached &) {
                throw;
            } catch (const std::exception &e) {
                work.lesson.failure = QString::fromUtf8(e.what());
            }
        }
        for (const Work &work : std::as_const(works))     // ciò che non è diventato una lezione, con il perché
            if (!work.lesson.failure.isEmpty())
                site(work, QStringLiteral("Fine"), QStringLiteral("fallita"), {{"motivo", work.lesson.failure}});
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
                left = left || texsource::parseSpec(s.file).path == it.key();
            if (!left) {
                QDir().mkpath(m_project.path("fonti/usate/più lezioni"));
                QFile::rename(m_project.path(it.key()), files::freeName(m_project.path("fonti/usate/più lezioni/" + QFileInfo(it.key()).fileName())));
            }
        }
        // in un progetto per argomento ogni lezione scritta va nel suo argomento (ciò che ha da fare la lezione è fatto)
        for (Work &work : works) {
            const Wanted chosen = wanted.value(work.source);
            if (work.lesson.file.isEmpty() || !chosen.byTopic())
                continue;
            if (const QString error = reorganize::place(m_project, work.lesson.file, chosen.topic, chosen.title, chosen.after); !error.isEmpty())
                m_control->log(QStringLiteral("%1 resta una lezione a sé: %2").arg(work.lesson.file, error));
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
