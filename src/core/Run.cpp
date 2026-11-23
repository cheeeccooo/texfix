#include "core/Run.h"

#include "core/Files.h"
#include "core/History.h"
#include "core/Reorganize.h"
#include "core/NewLesson.h"
#include "core/Plans.h"
#include "core/Sort.h"
#include "core/TexSource.h"
#include "core/Worker.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QSet>
#include <QThread>

#include <future>

namespace tx::run {

namespace {
QString stateFile(const Project &project, const QString &id) { return project.path("salvataggi/lavori/" + id + "/esecuzione.json"); }

// il nome di un pezzo per chi guarda: «insieme · Amplificatori operazionali ideali.tex» → «Amplificatori operazionali ideali»
QString pieceTitle(const QString &source)
{
    const QString name = QFileInfo(texsource::parseSpec(source).path).completeBaseName();
    return name.contains(" · ") ? name.section(" · ", 1) : name;
}

// quanto è costato davvero, dai registri delle chiamate (restano da una ripresa all'altra): i compiti dell'agente
// (fase «integrate») e il resto, che è la scrittura delle lezioni nuove
sort::Estimate spentOf(const QString &records)
{
    sort::Estimate out;
    for (const QString &name : QDir(records).entryList({"*.json"}, QDir::Files)) {
        const QJsonObject call = QJsonDocument::fromJson(files::read(records + '/' + name).toUtf8()).object();
        (call.value("fase").toString() == "integrate" ? out.agent : out.lessons) += call.value("costo").toDouble();
    }
    return out;
}
}

QString folder(const Project &project, const QString &id) { return project.path("build/lavori/esecuzione/" + id); }
QString recordsDir(const Project &project, const QString &id) { return folder(project, id) + "/chiamate"; }

QJsonObject load(const Project &project, const QString &id)
{
    return QJsonDocument::fromJson(files::read(stateFile(project, id)).toUtf8()).object();
}

bool save(const Project &project, const QString &id, const QJsonObject &state)
{
    return files::write(stateFile(project, id), QString::fromUtf8(QJsonDocument(state).toJson()));
}

QStringList started(const Project &project)
{
    QStringList out;
    for (const plans::Info &info : plans::open(project))
        if (QFileInfo::exists(stateFile(project, info.id)))
            out.prepend(info.id);                      // i nomi sono date: l'ultimo è il più recente
    return out;
}

Result execute(const Project &project, const Settings &settings, const Style &style, const QString &id, JobControl *control)
{
    Result out;
    QElapsedTimer timer;
    timer.start();
    QJsonObject plan = plans::load(project, id);
    if (plan.isEmpty()) {
        out.failure = QStringLiteral("Il piano non c'è più.");
        return out;
    }
    QJsonObject state = load(project, id);
    const QString copy = folder(project, id) + "/prima";
    if (!state.value("preparato").toBool()) {
        // prima di toccare qualcosa, com'era il progetto: «Annulla tutto il lavoro» torna lì
        QDir(folder(project, id)).removeRecursively();
        Step copying(control, QStringLiteral("Copio il progetto com'è, per «Annulla tutto il lavoro»"));
        if (!History::saveCopy(project.root(), copy)) {
            out.failure = QStringLiteral("Non riesco a fare una copia del progetto prima di cominciare: non ho toccato niente.");
            copying.done(out.failure, false);
            return out;
        }
        copying.done();
        const bool agentPlan = plan.value("formato").toString() != "concetti";
        Step preparing(control, agentPlan ? QStringLiteral("Preparo i compiti del piano, uno per parte") : QStringLiteral("Preparo i pezzi delle fonti per ogni lezione"));
        sort::Prepared prepared;
        try {
            if (agentPlan) {                           // il piano dell'agente: un compito per sezione, uno dopo l'altro
                const QJsonArray sections = plan.value("sezioni").toArray();
                for (int i = 0; i < sections.size(); ++i) {
                    const QJsonArray entries = sections[i].toObject().value("voci").toArray();
                    if (std::any_of(entries.begin(), entries.end(), [](const QJsonValue &v) { return v.toObject().value("fare").toBool(); }))
                        prepared.tasks << sort::Task{QStringLiteral("piano"), QString(), sections[i].toObject().value("titolo").toString(), {}, plans::taskMessage(plan, i)};
                }
                if (prepared.tasks.isEmpty())
                    prepared.failure = QStringLiteral("nessuna voce del piano ha la spunta.");
            } else {
                prepared = sort::prepare(project, plan);
            }
        } catch (const std::exception &e) {            // niente in silenzio, e niente che chiuda il programma
            prepared.failure = QString::fromUtf8(e.what());
        }
        if (!prepared.failure.isEmpty()) {             // il progetto torna com'era: nessun lavoro a metà
            preparing.done(prepared.failure, false);
            History::restoreCopy(project.root(), copy);
            QDir(folder(project, id)).removeRecursively();
            out.failure = QStringLiteral("Non riesco ad avviare il piano: %1").arg(prepared.failure);
            return out;
        }
        preparing.done(agentPlan ? (prepared.tasks.size() == 1 ? QStringLiteral("1 compito per l'agente") : QStringLiteral("%1 compiti per l'agente, uno dopo l'altro").arg(prepared.tasks.size()))
                                 : (prepared.wanted.size() == 1 ? QStringLiteral("1 lezione nuova da scrivere") : QStringLiteral("%1 lezioni nuove da scrivere").arg(prepared.wanted.size()))
                                       + (prepared.tasks.size() == 1 ? QStringLiteral(", 1 compito per l'agente") : QStringLiteral(", %1 compiti per l'agente").arg(prepared.tasks.size())));
        newlesson::addPlacements(project, prepared.placements);
        QJsonObject chosen;
        for (auto it = prepared.wanted.begin(); it != prepared.wanted.end(); ++it)
            chosen.insert(it.key(), QJsonObject{{"numero", it->number}, {"data", it->date}, {"argomento", it->topic}, {"titolo", it->title},
                                                {"dopo", it->after}, {"nota", it->note}, {"solo", it->only}, {"gruppo", QJsonArray::fromStringList(it->group)},
                                                {"posto", it->rank}});
        QJsonArray tasks;
        for (const sort::Task &t : std::as_const(prepared.tasks))
            tasks.append(QJsonObject{{"id", QString::number(tasks.size() + 1)}, {"tipo", t.kind}, {"lezione", t.lesson}, {"titolo", t.title},
                                     {"fonti", QJsonArray::fromStringList(t.sources)}, {"messaggio", t.message}, {"stato", "da_fare"}});
        state = QJsonObject{{"preparato", true}, {"inizio", QDateTime::currentDateTime().toString(Qt::ISODate)},
                            {"stima_parti", plan.value("stima_parti")},
                            {"fatto", QJsonArray::fromStringList(prepared.done)}, {"fonti", QJsonArray::fromStringList(prepared.sources)},
                            {"scelte", chosen}, {"compiti", tasks}, {"stima", plan.value("stima").toDouble()},
                            {"ordine", QJsonArray::fromStringList(prepared.order)}};
        plan["stato"] = "in_corso";
        plans::save(project, id, plan);
    }
    QDir().mkpath(recordsDir(project, id));
    if (control) {
        control->records = recordsDir(project, id);
        int last = 0;                                  // ripreso: le chiamate nuove dopo quelle che ci sono, i registri restano
        for (const QString &name : QDir(control->records).entryList({"*.json"}, QDir::Files))
            last = qMax(last, QFileInfo(name).completeBaseName().toInt());
        control->callsAfter(last);
    }
    state["stato"] = "in_corso";
    state.remove("fine");
    state.remove("pausa");
    state.remove("pausa_per");
    QMutex mutex;                                      // lo stato su disco, da più thread
    save(project, id, state);
    auto change = [&](const std::function<void(QJsonObject &)> &edit) {
        QMutexLocker lock(&mutex);
        edit(state);
        save(project, id, state);
    };
    // la pausa per il limite resta scritta: alla riapertura si sa perché il lavoro era fermo
    std::function<void(const QDateTime &, bool)> previousPause;
    if (control) {
        previousPause = control->onPause;
        control->onPause = [&, previousPause](const QDateTime &until, bool connection) {
            change([&](QJsonObject &s) {
                if (until.isValid())
                    s["pausa"] = until.toString(Qt::ISODate);
                else
                    s.remove("pausa");
                if (connection)
                    s["pausa_per"] = "connessione";
                else
                    s.remove("pausa_per");
            });
            if (previousPause)
                previousPause(until, connection);
        };
    }
    auto site = [&](const QJsonObject &e) {
        if (control)
        {
            QJsonObject full = e;
            full["tipo"] = "cantiere";
            control->event(full);
        }
    };

    // chi guarda vede subito tutti i cantieri: le lezioni da scrivere e i compiti dell'agente
    QStringList pending;
    for (const QJsonValue &v : state.value("fonti").toArray()) {
        const QString source = v.toString();
        const bool waiting = QFileInfo::exists(project.path(texsource::parseSpec(source).path));
        if (waiting)
            pending << source;
        site({{"chiave", source}, {"genere", "converti"}, {"titolo", pieceTitle(source)}, {"fase", waiting ? "" : "Fine"},
              {"stato", waiting ? "da_fare" : "fatta"}});
    }
    QJsonArray tasks = state.value("compiti").toArray();
    for (const QJsonValue &v : std::as_const(tasks)) {
        const QJsonObject t = v.toObject();
        site({{"chiave", "compito:" + t.value("id").toString()}, {"genere", t.value("tipo").toString()}, {"titolo", t.value("titolo").toString()},
              {"file", t.value("lezione").toString()}, {"fase", t.value("stato").toString() == "fatta" ? "Fine" : ""},
              {"stato", t.value("stato").toString() == "fatta" ? "fatta" : "da_fare"}, {"esito", t.value("esito").toString()}});
    }

    QHash<QString, Convert::Wanted> wanted;
    const QJsonObject chosen = state.value("scelte").toObject();
    for (auto it = chosen.begin(); it != chosen.end(); ++it) {
        const QJsonObject w = it.value().toObject();
        QStringList group;
        for (const QJsonValue &title : w.value("gruppo").toArray())
            group << title.toString();
        wanted.insert(it.key(), Convert::Wanted{w.value("numero").toInt(), w.value("data").toString(), w.value("argomento").toString(),
                                                w.value("titolo").toString(), w.value("dopo").toString(), w.value("nota").toString(),
                                                w.value("solo").toString(), group, w.value("posto").toInt(-1)});
    }
    // la conversione, in un thread suo
    std::future<Convert::Result> converting = std::async(std::launch::async, [&] {
        return pending.isEmpty() ? Convert::Result{} : Convert(project, settings, style, control).run(pending, wanted);
    });

    // un compito dell'agente
    std::atomic<bool> stopped{false};
    QString halted;                                    // perché il lavoro si è fermato da sé (una fonte che non si apre)
    QMutex haltMutex;
    auto runTask = [&](int index) {
        QJsonObject t;
        {
            QMutexLocker lock(&mutex);
            t = state.value("compiti").toArray().at(index).toObject();
        }
        const QString key = "compito:" + t.value("id").toString();
        Site here(key);
        QString lesson = t.value("lezione").toString(), message = t.value("messaggio").toString();
        QStringList sources;
        for (const QJsonValue &s : t.value("fonti").toArray())
            sources << s.toString();
        const bool anyLeft = std::any_of(sources.begin(), sources.end(), [&](const QString &s) { return QFileInfo::exists(project.path(s)); });
        if (t.value("stato").toString() == "in_corso") {
            if (!anyLeft && !sources.isEmpty()) {      // interrotto dopo chiudi_lezione: era finito
                change([&](QJsonObject &s) {
                    QJsonArray all = s.value("compiti").toArray();
                    QJsonObject done = all[index].toObject();
                    done["stato"] = "fatta";
                    done["esito"] = QStringLiteral("Finito prima dell'interruzione.");
                    all[index] = done;
                    s["compiti"] = all;
                });
                site({{"chiave", key}, {"fase", "Fine"}, {"stato", "fatta"}});
                return;
            }
            if (t.value("tipo").toString() == "piano") {
                // una parte di un piano dell'agente, interrotta senza conversazione: non si sa che lezioni avesse toccato, si
                // rifà guardando com'è
                message += QStringLiteral("\n\nUn tentativo di prima di questa parte si è interrotto a metà: guarda com'è ogni lezione "
                                          "e fai solo ciò che manca.");
            } else {
            // interrotto a metà: la lezione torna com'era prima del lavoro, e il compito si rifà da capo
            Step restoring(control, QStringLiteral("Rimetto %1 com'era: il compito interrotto si rifà da capo").arg(lesson));
            History::restoreCopy(project.root(), copy, {lesson});
            restoring.done();
            }
        }
        if (t.value("tipo").toString() == "fondi" && QFileInfo::exists(project.path(lesson)) && !project.inputs().contains(lesson)
            && t.value("stato").toString() != "in_corso") {
            // il nome previsto l'ha preso la conversione: un nome libero
            int n = newlesson::lastFile(project);
            QString fresh;
            do
                fresh = QStringLiteral("src/src%1.tex").arg(++n);
            while (QFileInfo::exists(project.path(fresh)));
            message.replace(lesson, fresh);
            lesson = fresh;
        }
        if (control)
            control->checkLimit();                     // il limite è già stato raggiunto: il compito aspetta la ripresa
        // fermato a metà dal limite o messo in pausa: l'agente continua la sua conversazione, la lezione resta com'è
        const QString session = t.value("stato").toString() == "limite" || t.value("stato").toString() == "pausa" ? t.value("sessione").toString() : QString();
        change([&](QJsonObject &s) {
            QJsonArray all = s.value("compiti").toArray();
            QJsonObject now = all[index].toObject();
            now["stato"] = "in_corso";
            now["lezione"] = lesson;
            now["messaggio"] = message;
            all[index] = now;
            s["compiti"] = all;
        });
        site({{"chiave", key}, {"file", lesson}, {"fase", "Agente"}, {"stato", "in_corso"}});
        const worker::Result r = worker::run(project, settings, style, message, t.value("titolo").toString(), control, session);
        if (r.limited) {
            change([&](QJsonObject &s) {
                QJsonArray all = s.value("compiti").toArray();
                QJsonObject now = all[index].toObject();
                now["stato"] = "limite";
                now["sessione"] = r.session;
                now["costo"] = now.value("costo").toDouble() + r.cost;
                all[index] = now;
                s["compiti"] = all;
            });
            site({{"chiave", key}, {"fase", "Agente"}, {"stato", "da_fare"}});
            throw LimitPause();
        }
        if (r.paused) {                                // «Ferma»: in pausa, con la conversazione; niente si rimette com'era
            change([&](QJsonObject &s) {
                QJsonArray all = s.value("compiti").toArray();
                QJsonObject now = all[index].toObject();
                now["stato"] = "pausa";
                now["sessione"] = r.session;
                now["costo"] = now.value("costo").toDouble() + r.cost;
                all[index] = now;
                s["compiti"] = all;
            });
            site({{"chiave", key}, {"fase", "Agente"}, {"stato", "fermata"}});
            throw Stopped();
        }
        // se l'agente non ha chiuso la lezione, la chiude Texfix: marcatori, fonti fra le usate, il posto nel documento
        if (r.ok && QFileInfo::exists(project.path(lesson))
            && std::any_of(sources.begin(), sources.end(), [&](const QString &s) { return QFileInfo::exists(project.path(s)); }))
        {
            Step closing(control, QStringLiteral("Chiudo %1: marcatori dei blocchi, fonti fra le usate").arg(lesson));
            newlesson::settle(project, style.boxes(), style.figureWrapper(), lesson, sources);
            closing.done();
        }
        change([&](QJsonObject &s) {
            QJsonArray all = s.value("compiti").toArray();
            QJsonObject now = all[index].toObject();
            now["stato"] = r.ok ? "fatta" : "fallita";
            now["esito"] = r.text;
            now["motivo"] = r.failure;
            now["costo"] = now.value("costo").toDouble() + r.cost;
            all[index] = now;
            s["compiti"] = all;
        });
        site({{"chiave", key}, {"fase", "Fine"}, {"stato", r.ok ? "fatta" : "fallita"}, {"esito", r.text}, {"motivo", r.failure}});
        if (!r.ok && r.text.startsWith(QLatin1String(worker::UNREADABLE), Qt::CaseInsensitive)) {
            // una fonte che non si apre: niente lezioni scritte a metà delle fonti, il lavoro si ferma e lo dice
            {
                QMutexLocker lock(&haltMutex);
                if (halted.isEmpty())
                    halted = QStringLiteral("%1: %2 Il lavoro è fermo: sistema la fonte (o caricane una versione nuova) e premi «Riprendi».")
                                 .arg(t.value("titolo").toString(), r.failure.endsWith('.') ? r.failure : r.failure + '.');
            }
            stopped = true;
            if (control)
                control->stop();
        }
    };
    // i compiti, due alla volta e mai due sulla stessa lezione
    auto runTasks = [&](const QStringList &kinds) {
        QList<int> todo;
        for (int i = 0; i < tasks.size(); ++i)
            if (kinds.contains(tasks[i].toObject().value("tipo").toString()) && tasks[i].toObject().value("stato").toString() != "fatta")
                todo << i;
        QSet<QString> busy;
        QMutex queue;
        auto worker = [&] {
            while (!stopped && !(control && (control->stopped() || control->pausedUntil().isValid()))) {
                int index = -1;
                {
                    QMutexLocker lock(&queue);
                    if (todo.isEmpty())
                        return;
                    for (int i : std::as_const(todo))
                        if (!busy.contains(tasks[i].toObject().value("lezione").toString())) {
                            index = i;
                            break;
                        }
                    if (index >= 0) {
                        todo.removeOne(index);
                        busy.insert(tasks[index].toObject().value("lezione").toString());
                    }
                }
                if (index < 0) {
                    QThread::msleep(300);
                    continue;
                }
                try {
                    runTask(index);
                } catch (const Stopped &) {
                    stopped = true;
                } catch (const std::exception &e) {
                    site({{"chiave", "compito:" + tasks[index].toObject().value("id").toString()}, {"fase", "Fine"}, {"stato", "fallita"},
                          {"motivo", QString::fromUtf8(e.what())}});
                }
                QMutexLocker lock(&queue);
                busy.remove(tasks[index].toObject().value("lezione").toString());
            }
        };
        auto other = std::async(std::launch::async, worker);
        worker();
        other.get();
    };
    runTasks({"integra", "versione", "piano"});      // le parti di un piano dell'agente hanno la stessa «lezione» (nessuna): una alla volta
    out.converted = converting.get();
    if (!stopped && !out.converted.stopped && !(control && (control->stopped() || control->pausedUntil().isValid()))) {
        tasks = state.value("compiti").toArray();
        runTasks({"fondi"});                           // le lezioni nuove dell'agente: dopo, con i nomi che restano liberi
    }
    if (control)
        control->onPause = previousPause;
    // l'ordine nuovo degli argomenti, quando tutti sono al loro posto
    if (!stopped && !out.converted.stopped && !(control && (control->stopped() || control->pausedUntil().isValid()))) {
        QStringList order;
        for (const QJsonValue &title : state.value("ordine").toArray())
            order << title.toString();
        if (!order.isEmpty() && reorganize::reorder(project, order))
            change([&](QJsonObject &s) {
                QJsonArray done = s.value("fatto").toArray();
                done.append(QStringLiteral("argomenti riordinati come nel piano"));
                s["fatto"] = done;
                s.remove("ordine");
            });
    }

    // l'esito
    out.stopped = stopped || out.converted.stopped || (control && (control->stopped() || control->pausedUntil().isValid()));
    out.failure = halted.isEmpty() ? out.converted.failure : halted;
    // il costo vero dai registri: sommare qui contava due volte le sessioni di prima di una ripresa
    const sort::Estimate spent = spentOf(recordsDir(project, id));
    out.cost = spent.total();
    QStringList outcome;
    for (const QJsonValue &v : state.value("fatto").toArray())
        outcome << v.toString();
    for (const QJsonValue &v : state.value("compiti").toArray()) {
        const QJsonObject t = v.toObject();
        if (t.value("stato").toString() == "fatta")
            ++out.tasksDone;
        else if (t.value("stato").toString() == "fallita") {
            ++out.tasksFailed;
            out.lines << QStringLiteral("%1: non riuscito (%2).").arg(t.value("titolo").toString(), t.value("motivo").toString());
        }
    }
    out.minutes = timer.elapsed() / 60000.0;
    change([&](QJsonObject &s) {
        s["stato"] = out.stopped ? "fermato" : "finito";
        if (!out.stopped)
            s["fine"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        s["costo"] = out.cost;
    });
    if (!out.stopped) {                                // le stime dopo imparano da questa
        const QJsonObject guessed = state.value("stima_parti").toObject();
        sort::learn(settings.quality, {guessed.value("lezioni").toDouble(), guessed.value("agente").toDouble()}, spent);
    }
    if (!out.stopped) {                                // l'esito resta nel piano: si legge, poi si archivia
        QStringList text;
        for (const QString &line : std::as_const(outcome))
            text << "- " + line;
        for (const Convert::Lesson &l : std::as_const(out.converted.lessons))
            text << (l.file.isEmpty() ? QStringLiteral("- %1: non convertita (%2)").arg(l.source, l.failure)
                                      : QStringLiteral("- Lezione %1 · %2 (`%3`): scritta").arg(l.number).arg(l.title, l.file));
        for (const QJsonValue &v : state.value("compiti").toArray()) {
            const QJsonObject t = v.toObject();
            text << QStringLiteral("- %1: %2").arg(t.value("titolo").toString(), t.value("stato").toString() == "fatta" ? "fatto" : "non riuscito");
            if (!t.value("esito").toString().trimmed().isEmpty())
                text << "\n" + t.value("esito").toString().trimmed() + "\n";
        }
        plan = plans::load(project, id);
        plan["stato"] = "esito";
        plan["esito"] = text.join('\n');
        plans::save(project, id, plan);
    }
    return out;
}

QStringList undo(const Project &project, const QString &id, QString *error)
{
    const QString copy = folder(project, id) + "/prima";
    if (!QFileInfo::exists(copy + "/stato.json")) {
        if (error)
            *error = QStringLiteral("La copia di com'era il progetto prima del lavoro non c'è più.");
        return {};
    }
    const QStringList changed = History::restoreCopy(project.root(), copy);
    QFile::remove(project.path("build/lavori/converti/in-corso.json"));     // niente da riprendere
    QJsonObject plan = plans::load(project, id);
    if (!plan.isEmpty()) {                             // il piano torna da avviare, com'era
        plan["stato"] = "piano";
        plan.remove("esito");
        plans::save(project, id, plan);
    }
    QFile::remove(stateFile(project, id));
    forget(project, id);
    if (error)
        error->clear();
    return changed;
}

void forget(const Project &project, const QString &id) { QDir(folder(project, id)).removeRecursively(); }

}
