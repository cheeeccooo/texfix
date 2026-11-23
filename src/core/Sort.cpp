#include "core/Sort.h"

#include "core/Concepts.h"
#include "core/Files.h"
#include "core/NewLesson.h"
#include "core/Pages.h"
#include "core/Plans.h"
#include "core/TexSource.h"
#include "core/Tools.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>
#include <QSet>

namespace tx::sort {

namespace {

// La lezione che ha una fonte usata: quella della sua cartella «lezione N».
QString ownerOf(const Project &project, const QString &used)
{
    static const QRegularExpression folder(R"(fonti/usate/lezione (\d+)/)");
    const int number = folder.match(used).captured(1).toInt();
    for (const Lesson &lesson : project.lessons())
        if (number > 0 && lesson.number == number)
            return lesson.file;
    return {};
}

QString settingsLine(const Settings &settings)
{
    const QList<std::pair<QString, QString>> keys{{"scrittura", "Scrittura"}, {"qualita", "Qualità"}, {"figure_scansioni", "Figure delle scansioni"},
                                                  {"figure_tex", "Figure dei .tex"}, {"impaginazione", "Impaginazione"}};
    QStringList parts;
    for (const auto &[key, name] : keys)
        for (const Settings::Choice &c : Settings::choices(key))
            if (c.value == settings.value(key))
                parts << QStringLiteral("%1: %2").arg(name, c.label);
    return parts.join(" · ");
}

QString holderOf(const QString &id) { return id.section('.', 0, 0); }

// Un nome di file da un titolo: niente barre né caratteri che danno noia.
QString fileSafe(const QString &title)
{
    QString out = title;
    static const QRegularExpression bad(R"([/\\:*?"<>|]+)");
    out.replace(bad, " ");
    return out.simplified().left(60);
}
}

Outcome plan(const Project &project, const Settings &settings, const QStringList &files, JobControl *control, const QJsonObject &previous)
{
    Outcome out;
    // senza modello: doppioni, versioni nuove, file che non si leggono
    QStringList survey, known;
    QJsonArray versions;
    QSet<QString> asNew;                               // versioni che l'utente vuole smistare come materiale nuovo
    for (const QJsonValue &v : previous.value("versioni").toArray())
        if (v.toObject().value("valore").toString() == "nuovo")
            asNew.insert(v.toObject().value("fonte").toString());
    for (const QString &rel : files) {
        const QString name = QFileInfo(rel).fileName(), path = project.path(rel), kind = Pages::kindOf(name);
        if (!QFileInfo::exists(path)) {
            out.unread.insert(rel, QStringLiteral("il file non c'è più"));
            continue;
        }
        const newlesson::Version version = newlesson::versionOf(project, path);
        if (!version.used.isEmpty() && version.identical) {
            known << QStringLiteral("%1: è già fra le fonti usate, uguale").arg(name);
            continue;
        }
        if (!version.used.isEmpty() && !asNew.contains(rel)) {
            versions.append(QJsonObject{{"fonte", rel}, {"usata", version.used}, {"file", ownerOf(project, version.used)}, {"valore", "porta"},
                                        {"titolo", QStringLiteral("%1 è una versione nuova di %2").arg(name, QFileInfo(version.used).fileName())}});
            continue;
        }
        if (const Lesson owner = newlesson::namedBy(project, rel); !owner.file.isEmpty()) {   // rimasto in arrivo da un lavoro di prima
            known << QStringLiteral("%1: è già la lezione %2, i suoi blocchi vengono da lì").arg(name).arg(owner.number);
            continue;
        }
        if (kind == "altro") {
            out.unread.insert(rel, QStringLiteral("Texfix non sa leggere questo tipo di file"));
            continue;
        }
        survey << rel;
    }

    // il resto lo legge il modello: i concetti, e la mappa
    concepts::Survey s;
    if (!survey.isEmpty()) {
        QString instructions = previous.isEmpty() ? QString() : previousPlan(previous);
        const QHash<QString, QString> chosen = newlesson::chosenLessons(project);   // «Aggiungi materiale a questa lezione…»
        for (const QString &rel : std::as_const(survey))
            if (const QString target = chosen.value(rel); !target.isEmpty())
                for (const Lesson &lesson : project.lessons())
                    if (lesson.file == target)
                        instructions += QStringLiteral("\nTHE USER ADDED THE MATERIAL «%1» TO THE UNIT «%2»: its concepts go there, unless one clearly belongs elsewhere.")
                                            .arg(QFileInfo(rel).completeBaseName(), lesson.title);
        s = concepts::survey(project, settings.quality, survey, control, instructions);
        out.cost = s.cost;
        known += s.known;
        for (auto it = s.unread.begin(); it != s.unread.end(); ++it)   // restano in arrivo, non riconosciuti, con il perché
            out.unread.insert(it.key(), it.value());
        if (!s.failure.isEmpty() && (s.map.isEmpty() || s.fresh.isEmpty())) {
            out.failure = s.failure.trimmed();
            for (const QString &rel : std::as_const(survey))
                if (!out.unread.contains(rel))
                    out.unread.insert(rel, out.failure);
            if (versions.isEmpty() && known.isEmpty())
                return out;
        }
    }
    if (s.fresh.isEmpty() && versions.isEmpty() && known.isEmpty()) {
        QStringList why;
        for (auto it = out.unread.begin(); it != out.unread.end(); ++it)
            why << QStringLiteral("%1 (%2)").arg(QFileInfo(it.key()).fileName(), it.value());
        out.failure = why.isEmpty() ? QStringLiteral("Non c'è materiale da aggiungere.") : QStringLiteral("Non so leggere: %1.").arg(why.join("; "));
        return out;
    }

    // il piano: le fonti e le lezioni, i concetti che servono, le decisioni
    QJsonArray sources, course;
    QJsonObject concepts;
    QSet<QString> needed;                              // i concetti del corso che il piano nomina
    for (const QJsonValue &v : s.map.value("shared").toArray())
        for (const QJsonValue &id : v.toObject().value("items").toArray())
            needed.insert(id.toString());
    for (const concepts::Holder &h : std::as_const(s.course)) {
        course.append(QJsonObject{{"key", h.key}, {"name", h.name}, {"source", h.source}});
        for (const concepts::Concept &c : h.concepts)
            if (needed.contains(c.id))
                concepts.insert(c.id, QJsonObject{{"title", c.title}, {"what", c.what}});
    }
    for (const concepts::Holder &h : std::as_const(s.fresh)) {
        const texsource::Ref ref = texsource::parseSpec(h.source);
        const bool pages = Pages::kindOf(ref.path) == "pagine";
        sources.append(QJsonObject{{"key", h.key}, {"name", h.name}, {"source", h.source}, {"file", QFileInfo(ref.path).completeBaseName()}, {"pagine", pages}});
        for (const concepts::Concept &c : h.concepts)     // da dove a dove: anche per la stima
            concepts.insert(c.id, QJsonObject{{"title", c.title}, {"what", c.what}, {"from", c.from}, {"to", c.to}});
    }
    // le sigle dei concetti sono del modello: in ciò che legge l'utente diventano il nome della lezione o della fonte
    QHash<QString, QString> spoken;
    static const QRegularExpression lessonName(R"(^(Lezione|Argomento) (\d+) · )");
    for (const concepts::Holder &h : std::as_const(s.course)) {
        const auto m = lessonName.match(h.name);
        spoken.insert(h.key, m.hasMatch() ? (m.captured(1) == "Lezione" ? QStringLiteral("la lezione %1") : QStringLiteral("l'argomento %1")).arg(m.captured(2)) : h.name);
    }
    for (const concepts::Holder &h : std::as_const(s.fresh))
        spoken.insert(h.key, h.name);
    auto plain = [&](const QJsonValue &value) {
        static const QRegularExpression id(R"(\b(L\d+|[A-Z]{1,2})\.\d+\b)");
        QString text = value.toString();
        for (auto m = id.match(text); m.hasMatch(); m = id.match(text, m.capturedStart() + 1))
            if (spoken.contains(m.captured(1)))
                text.replace(m.capturedStart(), m.capturedLength(), spoken.value(m.captured(1)));
        return text;
    };
    // le decisioni prese prima restano, se il gruppo è lo stesso
    QHash<QString, QString> decided;
    for (const QJsonValue &v : previous.value("comuni").toArray()) {
        QStringList items;
        for (const QJsonValue &id : v.toObject().value("items").toArray())
            items << id.toString();
        items.sort();
        decided.insert(items.join(','), v.toObject().value("valore").toString());
    }
    QJsonArray shared;
    for (const QJsonValue &v : s.map.value("shared").toArray()) {
        const QJsonObject g = v.toObject();
        QStringList items;
        for (const QJsonValue &id : g.value("items").toArray())
            items << id.toString();
        QString host = g.value("keep").toString();
        if (!items.contains(host)) {                   // l'ospite di «una versione sola»: quella del corso, o la prima
            host = items.value(0);
            for (const QString &id : std::as_const(items))
                if (id.startsWith('L')) {
                    host = id;
                    break;
                }
        }
        QStringList sorted = items;
        sorted.sort();
        const QString proposal = QStringList{"one", "both", "skip"}.contains(g.value("proposal").toString()) ? g.value("proposal").toString() : QStringLiteral("one");
        shared.append(QJsonObject{{"name", plain(g.value("name"))}, {"items", QJsonArray::fromStringList(items)}, {"note", plain(g.value("note"))},
                                  {"proposta", proposal}, {"valore", decided.value(sorted.join(','), proposal)}, {"ospite", host}});
    }
    QJsonArray destinations;
    for (const QJsonValue &v : s.map.value("place").toArray()) {
        const QJsonObject p = v.toObject();
        destinations.append(QJsonObject{{"into", p.value("into")}, {"title", p.value("title")}, {"date", p.value("date")}, {"after", p.value("after")}, {"concepts", p.value("concepts")},
                                        {"why", plain(p.value("why"))}, {"fare", true}, {"nota", ""}});
    }
    // l'ordine nuovo degli argomenti, se il modello ne propone uno: per nome, con il posto che aveva ognuno
    QJsonArray order;
    if (settings.byTopic()) {
        QHash<QString, QString> unitName;
        for (const QJsonValue &v : course)
            unitName.insert(v.toObject().value("key").toString(), v.toObject().value("name").toString());
        for (const QJsonValue &v : s.map.value("order").toArray()) {
            const QString key = v.toString().trimmed();
            if (key.startsWith("new:"))
                order.append(QJsonObject{{"key", key}, {"title", key.mid(4).trimmed()}});
            else if (unitName.contains(key))
                order.append(QJsonObject{{"key", key}, {"title", unitName.value(key).section(" · ", 1).trimmed()}});
        }
        QStringList now, proposed;
        for (const QJsonValue &v : course)
            now << v.toObject().value("key").toString();
        for (const QJsonValue &v : order)
            if (!v.toObject().value("key").toString().startsWith("new:"))
                proposed << v.toObject().value("key").toString();
        if (proposed == now.mid(0, proposed.size()) && proposed.size() == now.size())
            order = {};                                // gli argomenti di prima restano come sono: niente da proporre
    }
    QStringList unread, inPlan;                        // un file che non si legge non è nel piano: resta in arrivo, con «Riprova»
    for (auto it = out.unread.begin(); it != out.unread.end(); ++it)
        unread << QStringLiteral("%1 (%2)").arg(QFileInfo(it.key()).fileName(), it.value());
    for (const QString &rel : files)
        if (!out.unread.contains(rel))
            inPlan << rel;

    QStringList summary{QStringLiteral("Il materiale nuovo, per concetti: prima ciò che compare in più punti, poi dove va ciò che resta di quei file, "
                                       "poi ciò che è tutto nuovo.")};
    if (!s.failure.isEmpty())
        summary << QStringLiteral("Attenzione: %1").arg(s.failure.trimmed());
    QJsonObject document{{"titolo", "Aggiungi materiale"}, {"riepilogo", summary.join("\n\n")}, {"stato", "piano"}, {"tipo", "materiale"},
                         {"formato", "concetti"}, {"file", QJsonArray::fromStringList(inPlan)},
                         {"fonti", sources}, {"corso", course}, {"concetti", concepts}, {"comuni", shared}, {"destinazioni", destinations},
                         {"salta", [&] {
                             QJsonArray out;
                             for (const QJsonValue &v : s.map.value("skip").toArray())
                                 out.append(QJsonObject{{"concepts", v.toObject().value("concepts")}, {"why", plain(v.toObject().value("why"))}});
                             return out;
                         }()}, {"gia", QJsonArray::fromStringList(known)}, {"versioni", versions},
                         {"illeggibili", QJsonArray::fromStringList(unread)}, {"commento", ""}, {"nota_scaletta", ""}, {"programma", settings.program}, {"impostazioni", settingsLine(settings)},
                         {"ordine", QJsonObject{{"proposta", order}, {"valore", !order.isEmpty()}}},
                         {"costo", out.cost}, {"sezioni", QJsonArray()},
                         {"domande", QJsonArray()}};
    const Estimate guess = estimate(project, document, settings.quality);   // ciò che il piano farà, con le medie imparate
    document["stima"] = guess.total();
    document["stima_parti"] = QJsonObject{{"lezioni", guess.lessons}, {"agente", guess.agent}};
    out.plan = plans::create(project, QJsonObject{{"titolo", "Aggiungi materiale"}, {"sezioni", QJsonArray{QJsonObject{{"titolo", "-"}, {"voci", QJsonArray{"-"}}}}}}, &out.failure);
    if (!out.plan.isEmpty())
        plans::save(project, out.plan, document);
    return out;
}

QString previousPlan(const QJsonObject &document)
{
    QStringList lines;
    for (const QJsonValue &v : document.value("comuni").toArray()) {
        const QJsonObject g = v.toObject();
        if (g.value("valore") == g.value("proposta"))
            continue;
        QStringList items;
        for (const QJsonValue &id : g.value("items").toArray())
            items << id.toString();
        const QString value = g.value("valore").toString();
        lines << QStringLiteral("- shared «%1» (%2): USER wants %3").arg(g.value("name").toString(), items.join(", "),
                                                                          value == "one" ? QStringLiteral("one version, in %1").arg(g.value("ospite").toString())
                                                                          : value == "both" ? QStringLiteral("to keep all of them") : QStringLiteral("to skip the new ones"));
    }
    for (const QJsonValue &v : document.value("destinazioni").toArray()) {
        const QJsonObject d = v.toObject();
        QStringList ids;
        for (const QJsonValue &id : d.value("concepts").toArray())
            ids << id.toString();
        const QString where = d.value("into").toString() == "new" ? QStringLiteral("new unit «%1»").arg(d.value("title").toString()) : d.value("into").toString();
        if (!d.value("fare").toBool())
            lines << QStringLiteral("- %1 (%2): USER: not now, leave these concepts out").arg(where, ids.join(", "));
        if (!d.value("nota").toString().trimmed().isEmpty())
            lines << QStringLiteral("- %1 (%2): USER: %3").arg(where, ids.join(", "), d.value("nota").toString().trimmed());
    }
    const QJsonObject concepts = document.value("concetti").toObject();
    for (const QJsonValue &v : document.value("salta").toArray())
        for (const QJsonValue &id : v.toObject().value("tieni").toArray())
            lines << QStringLiteral("- skipped «%1» (%2): USER: do not skip it, place it like the other new concepts")
                         .arg(concepts.value(id.toString()).toObject().value("title").toString(), id.toString());
    if (!document.value("nota_scaletta").toString().trimmed().isEmpty())
        lines << QStringLiteral("USER, about the outline of the course (which units, how they are split, their order): %1").arg(document.value("nota_scaletta").toString().trimmed());
    if (!document.value("commento").toString().trimmed().isEmpty())
        lines << QStringLiteral("USER, about the whole plan: %1").arg(document.value("commento").toString().trimmed());
    return lines.join('\n');
}

Prepared prepare(const Project &project, const QJsonObject &document)
{
    Prepared out;
    const bool byTopic = Settings::load(project).byTopic();
    int number = newlesson::nextNumber(project);
    const int lastFile = newlesson::lastFile(project);

    QHash<QString, QJsonObject> sources;               // la chiave di una fonte → la fonte
    for (const QJsonValue &v : document.value("fonti").toArray())
        sources.insert(v.toObject().value("key").toString(), v.toObject());
    QHash<QString, QString> lessonOf;                  // la chiave di una lezione del corso → il suo file
    for (const QJsonValue &v : document.value("corso").toArray())
        lessonOf.insert(v.toObject().value("key").toString(), v.toObject().value("source").toString());
    const QJsonObject concepts = document.value("concetti").toObject();
    auto lessonName = [&](const QString &file) {
        for (const Lesson &l : project.lessons())
            if (l.file == file)
                return QStringLiteral("lezione %1").arg(l.number);
        return QFileInfo(file).completeBaseName();
    };
    struct Integration {                               // ciò che va in una lezione del corso: un compito solo
        QStringList sources, named, notes;
    };
    QMap<QString, Integration> integrations;
    auto titleOf = [&](const QString &id) { return concepts.value(id).toObject().value("title").toString(); };

    // i doppioni di una fonte usata si tolgono per primi: dopo, una versione nuova al posto della vecchia li farebbe diversi
    for (const QJsonValue &v : document.value("file").toArray()) {
        const QString file = v.toString();
        if (!QFileInfo::exists(project.path(file)))
            continue;
        const newlesson::Version version = newlesson::versionOf(project, project.path(file));
        if (!version.used.isEmpty() && version.identical) {
            QFile::remove(project.path(file));
            out.done << QStringLiteral("tolto %1, già fra le fonti usate").arg(QFileInfo(file).fileName());
        }
    }
    // le versioni nuove: la vecchia in «precedenti», la nuova al suo posto, l'agente porta le differenze
    for (const QJsonValue &v : document.value("versioni").toArray()) {
        const QJsonObject version = v.toObject();
        if (version.value("valore").toString() != "porta")
            continue;
        const QString source = version.value("fonte").toString(), owner = version.value("file").toString(), used = version.value("usata").toString();
        if (owner.isEmpty()) {
            out.done << QStringLiteral("%1 resta in arrivo: non so quale lezione usava %2").arg(QFileInfo(source).fileName(), QFileInfo(used).fileName());
            continue;
        }
        const QString copy = newlesson::replaceVersion(project, used, project.path(source));
        if (copy.isEmpty()) {
            out.done << QStringLiteral("non riesco a mettere %1 al posto della versione vecchia").arg(QFileInfo(source).fileName());
            continue;
        }
        out.tasks << Task{QStringLiteral("versione"), owner, QStringLiteral("Versione nuova di %1, nella %2").arg(QFileInfo(used).fileName(), lessonName(owner)), {copy},
                          newlesson::updateMessage(owner, newlesson::forAgent(project, {copy}).first(), QFileInfo(used).path() + "/precedenti/")};
    }

    // i concetti in comune: chi resta fuori dal suo posto, chi va in una lezione del corso
    QSet<QString> excluded;
    QHash<QString, QStringList> intoCourse;            // lezione del corso → concetti nuovi da portarci (una versione sola)
    QHash<QString, QStringList> courseNotes;           // lezione del corso → che cosa aggiungono
    QHash<QString, QStringList> joinHost;              // concetto nuovo ospite → altri concetti nuovi che si fondono con lui
    QHash<QString, QString> sharedNotes;               // concetto nuovo ospite → che cosa dire all'agente che li fonde
    for (const QJsonValue &v : document.value("comuni").toArray()) {
        const QJsonObject g = v.toObject();
        const QString value = g.value("valore").toString(), host = g.value("ospite").toString();
        QStringList fresh;
        for (const QJsonValue &id : g.value("items").toArray())
            if (sources.contains(holderOf(id.toString())))
                fresh << id.toString();
        if (value == "skip") {
            for (const QString &id : std::as_const(fresh))
                excluded.insert(id);
        } else if (value == "one") {
            for (const QString &id : std::as_const(fresh)) {
                if (id == host)
                    continue;
                excluded.insert(id);
                if (lessonOf.contains(holderOf(host))) {
                    intoCourse[holderOf(host)] << id;
                    courseNotes[holderOf(host)] << QStringLiteral("«%1» c'è già (%2): porta solo ciò che aggiunge, %3")
                                                       .arg(g.value("name").toString(), titleOf(host), g.value("note").toString());
                } else {
                    joinHost[host] << id;
                    sharedNotes[host] = QStringLiteral("«%1» è in più fonti: una versione sola, con ciò che ognuna aggiunge (%2)")
                                            .arg(g.value("name").toString(), g.value("note").toString());
                }
            }
        }
    }

    // un file per ciò che va in un posto: le righe (o le pagine) dei suoi concetti, fonte per fonte; i pezzi di testo e di
    // .tex in un .tex solo, con le macro di tutti; le pagine ritagliate in un PDF per fonte
    int pieceCount = 0;
    auto piecesOf = [&](const QStringList &ids, const QString &title, QString *failure) -> QStringList {
        QMap<QString, QList<std::pair<int, int>>> ranges;
        QStringList order;
        for (const QString &id : ids) {
            const QString key = holderOf(id);
            if (!sources.contains(key))
                continue;
            if (!ranges.contains(key))
                order << key;
            ranges[key] << std::pair{concepts.value(id).toObject().value("from").toInt(), concepts.value(id).toObject().value("to").toInt()};
        }
        QStringList made, texts;                       // texts: le fonti che finiscono nel .tex (non le pagine)
        QString texBody, texHead;
        for (const QString &key : std::as_const(order)) {
            const QJsonObject src = sources.value(key);
            const QString source = src.value("source").toString(), path = texsource::parseSpec(source).path;
            if (src.value("pagine").toBool()) {
                const QString work = project.path(QStringLiteral("build/lavori/smistamento/pezzo%1").arg(++pieceCount));
                QDir(work).removeRecursively();
                QDir().mkpath(work);
                QStringList parts;
                QSet<int> taken;
                for (auto [from, to] : ranges.value(key))
                    for (int p = qMax(1, from); p <= qMax(from, to); ++p) {
                        if (taken.contains(p))
                            continue;
                        taken.insert(p);
                        const QString one = work + QStringLiteral("/p%1.pdf").arg(p, 4, 10, QChar('0'));
                        if (path.endsWith(".pdf", Qt::CaseInsensitive)
                                ? tools::run("pdfseparate", {"-f", QString::number(p), "-l", QString::number(p), project.path(path), one}, work, 120000).ok
                                : tools::imageToPdf(project.path(path), one))
                            parts << one;
                    }
                std::sort(parts.begin(), parts.end());
                const QString target = files::freeName(project.path(QStringLiteral("fonti/coda/%1 · %2.pdf").arg(src.value("file").toString(), fileSafe(title))));
                if (!Pages::unite(parts, target, work)) {
                    *failure = parts.isEmpty() ? QStringLiteral("Non riesco a ritagliare le pagine di %1 (pdfseparate di poppler non le separa).").arg(src.value("name").toString())
                                               : QStringLiteral("Non riesco a rimettere insieme le pagine di %1.").arg(src.value("name").toString());
                    return {};
                }
                made << project.relative(target);
                continue;
            }
            if (Pages::kindOf(path) == "tex")
                for (const QString &macro : texsource::macros(texsource::preamble(files::read(project.path(path)))))
                    if (!texHead.contains(macro))
                        texHead += macro + '\n';
            QString text;
            try {
                text = concepts::textOf(project, source);
            } catch (const std::exception &e) {         // una fonte sparita o illeggibile: si dice, non si cade
                *failure = QStringLiteral("Non riesco a leggere %1: %2").arg(src.value("name").toString(), QString::fromUtf8(e.what()));
                return {};
            }
            texBody += QStringLiteral("% da %1\n%2\n\n").arg(src.value("name").toString(), concepts::extract(text, ranges.value(key)));
            texts << key;
        }
        if (!texBody.isEmpty()) {
            const QString base = texts.size() == 1 ? sources.value(texts.first()).value("file").toString() : QStringLiteral("insieme");
            const QString target = files::freeName(project.path(QStringLiteral("fonti/coda/%1 · %2.tex").arg(base, fileSafe(title))));
            files::write(target, texHead + "\\begin{document}\n" + texBody + "\\end{document}\n");
            made.prepend(project.relative(target));
        }
        return made;
    };

    // le destinazioni: in una lezione che c'è (l'agente) o una lezione nuova (la conversione, o l'agente se mescola pagine e testi)
    struct Fuse {
        QStringList named, notes, sources, concepts;   // concepts: che cosa prendere dalle fonti, per l'agente
        QString title;
        int number;
        QString date;
        Convert::Wanted where;
    };
    QList<Fuse> fuses;
    QStringList destinations;                          // i titoli dei posti del piano: ciò che non è di uno va negli altri
    QHash<QString, QStringList> groups;                // per argomento: i titoli dei nuovi che vanno nello stesso posto, in ordine
    QHash<QString, QString> afterOf;                   // titolo di un nuovo → il suo «dopo» (anche «new:<titolo>»: dopo un altro nuovo)
    for (const QJsonValue &v : document.value("destinazioni").toArray())
        if (const QJsonObject d = v.toObject(); d.value("fare").toBool() && d.value("into").toString() == "new")
            afterOf.insert(d.value("title").toString().trimmed(), d.value("after").toString());
    // il posto vero di un nuovo che segue un altro nuovo: quello in cima alla catena
    auto rootAfter = [&](QString after) {
        for (int guard = 0; after.startsWith("new:") && guard < 50; ++guard)
            after = afterOf.value(after.mid(4).trimmed());
        return after;
    };
    for (const QJsonValue &v : document.value("destinazioni").toArray())
        if (const QJsonObject d = v.toObject(); d.value("fare").toBool()) {
            const bool fresh = d.value("into").toString() == "new";
            destinations << (fresh ? d.value("title").toString().trimmed() : lessonName(lessonOf.value(d.value("into").toString())));
            if (fresh && !d.value("title").toString().trimmed().isEmpty())
                groups[rootAfter(d.value("after").toString())] << d.value("title").toString().trimmed();
        }
    // le pagine di una fonte e le righe vicine portano anche concetti di altri posti: l'agente prende solo i suoi
    auto onlyThese = [&](const QStringList &ids, const QString &here) {
        QStringList what, others;
        for (const QString &id : ids)
            if (!what.contains("«" + titleOf(id) + "»"))
                what << "«" + titleOf(id) + "»";
        for (const QString &d : std::as_const(destinations))
            if (d != here && !d.isEmpty())
                others << "«" + d + "»";
        QStringList out{QStringLiteral("Sono questi concetti, in quest'ordine: %1.").arg(what.join("; "))};
        if (!others.isEmpty())
            out << QStringLiteral("Le fonti possono contenere anche altro (le pagine sono intere): ciò che non è fra questi concetti va in "
                                  "%1, e qui non si scrive.").arg(others.join(", "));
        for (const QString &id : ids)
            if (sharedNotes.contains(id))
                out << sharedNotes.value(id) + '.';
        return out;
    };
    for (const QJsonValue &v : document.value("destinazioni").toArray()) {
        const QJsonObject d = v.toObject();
        QStringList ids;
        for (const QJsonValue &id : d.value("concepts").toArray())
            if (!excluded.contains(id.toString())) {
                ids << id.toString();
                ids += joinHost.value(id.toString());  // le altre versioni nuove dello stesso concetto vengono con lui
            }
        if (ids.isEmpty())
            continue;
        const bool fresh = d.value("into").toString() == "new";
        const QString title = fresh ? d.value("title").toString().trimmed() : titleOf(ids.first());
        QString failure;
        if (!d.value("fare").toBool()) {               // non ora: i pezzi restano in arrivo, da smistare un'altra volta
            piecesOf(ids, title.isEmpty() ? QStringLiteral("da smistare") : title, &failure);
            if (!failure.isEmpty())
                return Prepared{{}, {}, {}, {}, failure, {}};
            continue;
        }
        const QStringList made = piecesOf(ids, title.isEmpty() ? QStringLiteral("lezione nuova") : title, &failure);
        if (!failure.isEmpty())
            return Prepared{{}, {}, {}, {}, failure, {}};
        const QStringList named = newlesson::forAgent(project, made);
        QStringList notes;
        if (!d.value("nota").toString().trimmed().isEmpty())
            notes << d.value("nota").toString().trimmed();
        if (!fresh) {
            Integration &into = integrations[lessonOf.value(d.value("into").toString())];
            into.sources += made;
            into.named += named;
            into.notes += onlyThese(ids, lessonName(lessonOf.value(d.value("into").toString())));
            if (!notes.isEmpty())
                into.notes << "Istruzioni dell'utente: " + notes.join("; ");
            continue;
        }
        const int n = number++;
        Convert::Wanted where{n, d.value("date").toString()};
        if (!byTopic && d.value("date").toString().trimmed().isEmpty()) {   // per data, senza data: dove dice la logica
            const QString after = d.value("after").toString();
            where.after = after == "first" ? QStringLiteral("^") : lessonOf.value(after);
        }
        if (byTopic) {                                 // un argomento nuovo: dopo quale (o in testa, o in fondo)
            where.title = title.isEmpty() ? QStringLiteral("Argomento %1").arg(n) : title;
            const QString after = rootAfter(d.value("after").toString());
            where.after = after == "first" ? QStringLiteral("^") : lessonOf.value(after);
            where.group = groups.value(after);         // con gli altri nuovi dello stesso posto, nell'ordine del piano
            where.rank = int(where.group.indexOf(where.title));
        }
        if (made.size() == 1 && notes.isEmpty()) {
            if (made.first().endsWith(".pdf", Qt::CaseInsensitive))   // pagine intere: possono portare concetti di altri posti
                where.only = onlyThese(ids, title).join(' ');
            out.sources << made.first();
            out.wanted.insert(made.first(), where);
        } else {
            fuses << Fuse{named, notes, made, onlyThese(ids, title), title, n, d.value("date").toString(), where};
        }
    }
    // le versioni sole nelle lezioni del corso: l'agente porta ciò che aggiungono
    for (auto it = intoCourse.begin(); it != intoCourse.end(); ++it) {
        QString failure;
        const QStringList made = piecesOf(it.value(), QStringLiteral("per %1").arg(it.key()), &failure);
        if (!failure.isEmpty())
            return Prepared{{}, {}, {}, {}, failure, {}};
        Integration &into = integrations[lessonOf.value(it.key())];
        into.sources += made;
        into.named += newlesson::forAgent(project, made);
        into.notes << QStringLiteral("Sono concetti che la lezione ha già: %1.").arg(courseNotes.value(it.key()).join("; "));
    }
    for (auto it = integrations.begin(); it != integrations.end(); ++it)
        out.tasks << Task{QStringLiteral("integra"), it.key(), QStringLiteral("Aggiunte alla %1").arg(lessonName(it.key())), it->sources,
                          newlesson::integrateMessage(it.key(), it->named) + '\n' + it->notes.join('\n')};
    int next = lastFile + int(out.sources.size());
    for (const Fuse &f : std::as_const(fuses)) {
        const QString file = QStringLiteral("src/src%1.tex").arg(++next);
        out.tasks << Task{QStringLiteral("fondi"), file, QStringLiteral("Lezione nuova %1: %2").arg(f.number).arg(f.where.title.isEmpty() ? f.title : f.where.title), f.sources,
                          newlesson::fuseMessage(file, f.number, f.date, f.named, {}, f.where.byTopic() ? f.where.title : QString())
                              + "\n- " + f.concepts.join("\n- ")
                              + (f.notes.isEmpty() ? QString() : "\nIstruzioni dell'utente: " + f.notes.join("; "))};
        if (f.where.byTopic())
            out.placements.insert(file, QJsonObject{{"argomento", f.where.topic}, {"titolo", f.where.title}, {"dopo", f.where.after},
                                                    {"gruppo", QJsonArray::fromStringList(f.where.group)}, {"posto", f.where.rank}});
        else if (!f.where.after.isEmpty())             // per data, una lezione senza data: dopo quella che dice il piano
            out.placements.insert(file, QJsonObject{{"dopo", f.where.after}});
    }
    // l'ordine nuovo degli argomenti, se l'utente l'ha tenuto: si applica alla fine del lavoro, quando ci sono tutti
    if (const QJsonObject order = document.value("ordine").toObject(); byTopic && order.value("valore").toBool())
        for (const QJsonValue &v : order.value("proposta").toArray())
            out.order << v.toObject().value("title").toString();
    // le fonti smistate (tutto ciò che contenevano è in un pezzo, nel corso o saltato) vanno fra le usate; i doppioni si tolgono
    QSet<QString> read;
    for (const QJsonObject &src : std::as_const(sources))
        read.insert(texsource::parseSpec(src.value("source").toString()).path);
    for (const QJsonValue &v : document.value("file").toArray()) {
        const QString file = v.toString();
        if (!QFileInfo::exists(project.path(file)))
            continue;
        bool sorted = read.contains(file);
        if (!sorted && Pages::kindOf(file) == "tex") {         // un .tex con solo lezioni già nel corso
            const QList<newlesson::TexLecture> lectures = newlesson::texLectures(project, file);
            sorted = !lectures.isEmpty() && std::all_of(lectures.begin(), lectures.end(), [](const newlesson::TexLecture &t) { return !t.in.isEmpty(); });
        }
        if (!sorted) {                                 // già diventato una lezione: va con le sue fonti
            if (const Lesson owner = newlesson::namedBy(project, file); !owner.file.isEmpty() && owner.number > 0) {
                const QString used = project.path(QStringLiteral("fonti/usate/lezione %1").arg(owner.number));
                QDir().mkpath(used);
                QFile::rename(project.path(file), files::freeName(used + '/' + QFileInfo(file).fileName()));
                out.done << QStringLiteral("%1 è già la lezione %2: va fra le sue fonti usate").arg(QFileInfo(file).fileName()).arg(owner.number);
            }
            continue;
        }
        QDir().mkpath(project.path("fonti/usate/smistate"));
        QFile::rename(project.path(file), files::freeName(project.path("fonti/usate/smistate/" + QFileInfo(file).fileName())));
        out.done << QStringLiteral("%1 smistato per concetti: l'originale va fra le fonti usate").arg(QFileInfo(file).fileName());
    }
    return out;
}


namespace {
// le medie (in dollari, a qualità normale) delle conversioni della vecchia app: una pagina, cento righe di LaTeX, cento
// di testo; un compito dell'agente costa quanto la conversione di ciò che legge, più una base (legge la lezione, compila).
// Una lezione nuova ha in più un costo fisso, misurato con il modello vero il 23/11/2026 (una fonte di 15 righe: 0,09 $
// contro 0,01 stimati): il prompt della trascrizione con regole e stile, la verifica, la scaletta e il titolo, un giro sulle righe
constexpr double PAGE = 0.12, TEX_LINES = 0.23, TEXT_LINES = 0.08, AGENT_BASE = 0.35, LESSON_BASE = 0.08;

QString estimatesFile() { return files::dataDir() + "/stime.json"; }

// le correzioni imparate dai lavori di prima, per qualità: {"normale": {"lessons": 1.3, "agent": 0.8}}
QJsonObject learned() { return QJsonDocument::fromJson(files::read(estimatesFile()).toUtf8()).object(); }
}

Estimate estimate(const Project &project, const QJsonObject &plan, const QString &quality)
{
    const double factor = quality == "veloce" ? 0.45 : quality == "massima" ? 2.5 : 1.0;
    QHash<QString, QJsonObject> sources;
    for (const QJsonValue &v : plan.value("fonti").toArray())
        sources.insert(v.toObject().value("key").toString(), v.toObject());
    QSet<QString> course;
    for (const QJsonValue &v : plan.value("corso").toArray())
        course.insert(v.toObject().value("key").toString());
    const QJsonObject concepts = plan.value("concetti").toObject();
    // quanto costa scrivere un concetto del materiale nuovo: dalle sue pagine, o dalle sue righe
    auto cost = [&](const QString &id) {
        const QJsonObject c = concepts.value(id).toObject();
        const QJsonObject source = sources.value(holderOf(id));
        if (source.isEmpty())                          // un concetto del corso: non si riscrive
            return 0.0;
        const int size = qMax(1, c.value("to").toInt() - c.value("from").toInt() + 1);
        if (source.value("pagine").toBool())
            return PAGE * size;
        return (Pages::kindOf(source.value("source").toString()) == "tex" ? TEX_LINES : TEXT_LINES) * size / 100.0;
    };
    auto sum = [&](const QJsonArray &ids) {
        double out = 0;
        for (const QJsonValue &id : ids)
            out += cost(id.toString());
        return out;
    };
    Estimate e;
    QSet<QString> touched;                             // le lezioni del corso che l'agente tocca: un compito ciascuna
    for (const QJsonValue &v : plan.value("destinazioni").toArray()) {
        const QJsonObject d = v.toObject();
        if (!d.value("fare").toBool(true))
            continue;
        if (d.value("into").toString() == "new") {
            e.lessons += LESSON_BASE + sum(d.value("concepts").toArray());
        } else {
            e.agent += sum(d.value("concepts").toArray());
            touched.insert(d.value("into").toString());
        }
    }
    for (const QJsonValue &v : plan.value("comuni").toArray()) {        // una versione sola, in una lezione del corso
        const QJsonObject g = v.toObject();
        const QString host = holderOf(g.value("ospite").toString());
        if (g.value("valore").toString() == "one" && course.contains(host)) {
            e.agent += sum(g.value("items").toArray());
            touched.insert(host);
        }
    }
    for (const QJsonValue &v : plan.value("versioni").toArray()) {      // le differenze di una versione nuova
        const QJsonObject version = v.toObject();
        if (version.value("valore").toString() != "porta")
            continue;
        const QString file = version.value("fonte").toString();
        const int size = Pages::size(project.path(file));
        e.agent += Pages::kindOf(file) == "pagine" ? PAGE * size : (Pages::kindOf(file) == "tex" ? TEX_LINES : TEXT_LINES) * size / 100.0;
        touched.insert(version.value("file").toString());
    }
    e.agent += AGENT_BASE * touched.size();
    if (plan.value("formato").toString() != "concetti")   // un piano dell'agente: un compito per sezione con voci da fare
        for (const QJsonValue &s : plan.value("sezioni").toArray()) {
            int entries = 0;
            for (const QJsonValue &v : s.toObject().value("voci").toArray())
                entries += v.toObject().value("fare").toBool() ? 1 : 0;
            if (entries > 0)
                e.agent += AGENT_BASE + PAGE * entries;
        }
    const QJsonObject correction = learned().value(quality.isEmpty() ? QStringLiteral("normale") : quality).toObject();
    e.lessons *= factor * correction.value("lessons").toDouble(1.0);
    e.agent *= factor * correction.value("agent").toDouble(1.0);
    return e;
}

void learn(const QString &quality, const Estimate &estimated, const Estimate &spent)
{
    QJsonObject all = learned();
    const QString key = quality.isEmpty() ? QStringLiteral("normale") : quality;
    QJsonObject mine = all.value(key).toObject();
    // a metà strada fra la correzione di prima e quella che questo lavoro chiede: un lavoro strano non sposta tutto
    auto adjust = [&](const char *part, double guessed, double real) {
        if (guessed < 0.02 || real <= 0)
            return;
        const double before = mine.value(QLatin1String(part)).toDouble(1.0);
        const double ratio = qBound(0.2, real / guessed, 5.0);
        mine[QLatin1String(part)] = qBound(0.1, before * (0.5 + 0.5 * ratio), 10.0);
    };
    adjust("lessons", estimated.lessons, spent.lessons);
    adjust("agent", estimated.agent, spent.agent);
    all[key] = mine;
    files::write(estimatesFile(), QString::fromUtf8(QJsonDocument(all).toJson()));
}

}
