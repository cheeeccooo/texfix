#include "core/AgentTools.h"

#include "core/Compiler.h"
#include "core/Figures.h"
#include "core/Layout.h"
#include "core/NewLesson.h"
#include "core/Plans.h"
#include "core/Review.h"
#include "core/Rules.h"

#include <QFileInfo>
#include <QJsonDocument>

namespace tx::agent {

namespace {
QJsonObject tool(const char *name, const char *description, const QJsonObject &properties = {}, const QJsonArray &required = {})
{
    return {{"name", QString::fromUtf8(name)}, {"description", QString::fromUtf8(description)},
            {"inputSchema", QJsonObject{{"type", "object"}, {"properties", properties}, {"required", required}}}};
}
const QJsonObject LESSON{{"lezione", QJsonObject{{"type", "string"}, {"description", "Il file della lezione, per esempio src/src3.tex"}}}};

QString issuesText(const BuildResult &built, const QString &only)
{
    if (!built.failure.isEmpty())
        return built.failure;
    QStringList lines;
    for (const Issue &issue : built.issues) {
        if (!only.isEmpty() && issue.kind != Issue::Error && issue.file != only)
            continue;
        const QString kind = issue.kind == Issue::Error ? QStringLiteral("errore") : issue.kind == Issue::WideLine ? QStringLiteral("riga troppo larga")
                                                                                                                 : QStringLiteral("riga troppo vuota");
        lines << QStringLiteral("- %1, %2 riga %3: %4").arg(kind, issue.file).arg(issue.line).arg(issue.text);
    }
    return lines.isEmpty() ? QStringLiteral("Compila pulito: nessun errore, nessuna riga fuori misura.") : lines.join('\n');
}
}

QJsonArray tools()
{
    return {tool("stato_progetto", "Le lezioni del progetto in ordine, con numero, data, titolo, file, se sono fatte e che cosa resta da decidere "
                                   "(dubbi, aggiunte e correzioni, figure da inserire, mosse di impaginazione). Da chiamare prima di lavorare."),
            tool("compila_lezione", "Compila una lezione da sola e dice errori e righe fuori misura, con file e riga. Da chiamare dopo ogni "
                                    "modifica a una lezione: una lezione è finita solo quando compila pulita.", LESSON, {"lezione"}),
            tool("compila_documento", "Compila tutto il documento e dice errori e righe fuori misura."),
            tool("dubbi", "I dubbi di una lezione: la domanda, il passo com'è scritto, le letture possibili, se è già deciso.", LESSON, {"lezione"}),
            tool("impagina", "Impagina una lezione che compila pulita: sistema ciò che cade a cavallo di due pagine e dice le mosse fatte e "
                             "quelle solo proposte.", LESSON, {"lezione"}),
            tool("chiudi_lezione", "Da chiamare quando una lezione scritta o completata da te compila pulita: la mette nel documento se manca, "
                                   "aggiunge i marcatori dei blocchi che mancano e porta le fonti usate fuori da quelle da convertire.",
                 QJsonObject{{"lezione", QJsonObject{{"type", "string"}}},
                             {"fonti", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}, {"description", "I file di fonti/coda usati"}}}},
                 {"lezione"}),
            tool("consegna_piano", "Consegna all'utente il piano di un lavoro grande (fondere fonti, riorganizzare, toccare più lezioni) PRIMA di "
                                   "eseguirlo. Dopo averlo consegnato fermati: l'utente lo sistema e lo approva. Per rifare un piano già "
                                   "consegnato indica il suo lavoro.",
                 QJsonObject{{"titolo", QJsonObject{{"type", "string"}}},
                             {"riepilogo", QJsonObject{{"type", "string"}, {"description", "Due o tre frasi: che cosa si fa e perché"}}},
                             {"sezioni", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
                                 {"titolo", QJsonObject{{"type", "string"}}},
                                 {"voci", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}}}}}}}}},
                             {"domande", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
                                 {"testo", QJsonObject{{"type", "string"}}},
                                 {"opzioni", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}}}}}}}}},
                             {"lavoro", QJsonObject{{"type", "string"}, {"description", "Solo per rifare un piano già consegnato"}}}},
                 {"titolo", "sezioni"}),
            tool("consegna_esito", "Consegna l'esito di un lavoro eseguito: che cosa è stato fatto, file per file, e che cosa resta da decidere.",
                 QJsonObject{{"lavoro", QJsonObject{{"type", "string"}}}, {"testo", QJsonObject{{"type", "string"}, {"description", "In markdown, breve"}}}},
                 {"lavoro", "testo"}),
            // la conferma dell'utente prima di un'azione: la chiama il CLI, non l'agente; risponde la finestra (app/Chat)
            tool("conferma", "Chiede all'utente se permettere un'azione. Lo usa il programma, non l'agente.",
                 QJsonObject{{"tool_name", QJsonObject{{"type", "string"}}}, {"input", QJsonObject{{"type", "object"}}}}, {"tool_name", "input"})};
}

QString call(const Project &project, const Style &style, const QString &name, const QJsonObject &arguments, JobControl *control, bool *error)
{
    if (error)
        *error = false;
    auto fail = [&](const QString &why) {
        if (error)
            *error = true;
        return why;
    };
    const QString lesson = arguments.value("lezione").toString();
    if ((name == "compila_lezione" || name == "dubbi" || name == "impagina") && !project.inputs().contains(lesson))
        return fail(QStringLiteral("«%1» non è una lezione del documento. Le lezioni si vedono con stato_progetto.").arg(lesson));

    if (name == "stato_progetto") {
        const Settings settings = Settings::load(project);
        QStringList lines{QStringLiteral("Progetto «%1». Stile: %2.").arg(project.name(), style.name.isEmpty() ? QStringLiteral("nessuno (article con amsthm)") : style.name)};
        for (const Lesson &l : project.lessons()) {
            const review::Counts open = review::counts(project, l.file);
            QStringList notes;
            if (settings.isDone(l.file))
                notes << QStringLiteral("FATTA: non toccarla senza che l'utente lo confermi");
            if (open.doubts)
                notes << QStringLiteral("%1 dubbi aperti").arg(open.doubts);
            if (open.declared)
                notes << QStringLiteral("%1 aggiunte o correzioni da decidere").arg(open.declared);
            if (const int n = figures::pending(project, l.file))
                notes << QStringLiteral("%1 figure da inserire").arg(n);
            if (const int n = layout::toReview(project, l.file))
                notes << QStringLiteral("%1 mosse di impaginazione da rivedere").arg(n);
            lines << QStringLiteral("- %1: lezione %2%3, «%4»%5").arg(l.file).arg(l.number).arg(l.date.isEmpty() ? QString() : " del " + l.date, l.title,
                                                                    notes.isEmpty() ? QString() : " (" + notes.join("; ") + ")");
        }
        if (lines.size() == 1)
            lines << QStringLiteral("Ancora nessuna lezione.");
        return lines.join('\n');
    }
    if (name == "compila_lezione")
        return issuesText(Compiler::compileSync(project, lesson, control), lesson);
    if (name == "compila_documento")
        return issuesText(Compiler::compileSync(project, {}, control), {});
    if (name == "dubbi") {
        QStringList lines;
        for (const review::Doubt &d : review::doubts(project, lesson)) {
            QStringList readings;
            for (const review::Reading &r : d.readings)
                readings << r.text;
            lines << QStringLiteral("- %1 (%2): %3 Scritto: `%4`. Letture: %5.").arg(d.id, d.open ? "aperto" : "deciso", d.what, d.latex, readings.join(" | "));
        }
        return lines.isEmpty() ? QStringLiteral("Questa lezione non ha dubbi.") : lines.join('\n');
    }
    if (name == "impagina") {
        const layout::Result r = layout::lesson(project, style, lesson, control);
        if (!r.failure.isEmpty())
            return fail(r.failure);
        QStringList lines{QStringLiteral("Mosse fatte: %1; proposte da far decidere all'utente: %2.").arg(r.done).arg(r.proposed)};
        for (const layout::Move &m : layout::moves(project, lesson))
            lines << QStringLiteral("- %1: %2 [%3]").arg(m.block, m.what, m.state);
        return lines.join('\n');
    }
    if (name == "chiudi_lezione") {
        QStringList sources;
        for (const QJsonValue &v : arguments.value("fonti").toArray())
            sources << v.toString();
        return newlesson::settle(project, style.boxes(), style.figureWrapper(), arguments.value("lezione").toString(), sources).join("; ") + '.';
    }
    if (name == "consegna_piano") {
        QString why;
        const QString id = plans::create(project, arguments, &why);
        if (id.isEmpty())
            return fail(why);
        plans::discard(project, arguments.value("lavoro").toString());   // il piano rifatto prende il posto di quello di prima
        return QStringLiteral("Piano consegnato (lavoro: %1). Ora fermati: l'utente lo legge, lo sistema e lo approva.").arg(id);
    }
    if (name == "consegna_esito") {
        const QString why = plans::setOutcome(project, arguments.value("lavoro").toString(), arguments.value("testo").toString());
        return why.isEmpty() ? QStringLiteral("Esito consegnato.") : fail(why);
    }
    return fail(QStringLiteral("Lo strumento «%1» non esiste.").arg(name));
}

QByteArray handle(const Project &project, const Style &style, const QByteArray &request, JobControl *control)
{
    const QJsonObject message = QJsonDocument::fromJson(request).object();
    if (!message.contains("id"))
        return {};                                     // una notifica: niente risposta
    const QString method = message.value("method").toString();
    QJsonObject reply{{"jsonrpc", "2.0"}, {"id", message.value("id")}};
    if (method == "initialize") {
        reply["result"] = QJsonObject{{"protocolVersion", message.value("params").toObject().value("protocolVersion").toString("2024-11-05")},
                                      {"capabilities", QJsonObject{{"tools", QJsonObject()}}},
                                      {"serverInfo", QJsonObject{{"name", "texfix"}, {"version", "1"}}}};
    } else if (method == "ping") {
        reply["result"] = QJsonObject();
    } else if (method == "tools/list") {
        reply["result"] = QJsonObject{{"tools", tools()}};
    } else if (method == "tools/call") {
        const QJsonObject params = message.value("params").toObject();
        bool error = false;
        QString text;
        try {
            text = call(project, style, params.value("name").toString(), params.value("arguments").toObject(), control, &error);
        } catch (const std::exception &e) {
            error = true;
            text = QString::fromUtf8(e.what());
        }
        reply["result"] = QJsonObject{{"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", text}}}}, {"isError", error}};
    } else {
        reply["error"] = QJsonObject{{"code", -32601}, {"message", "metodo sconosciuto"}};
    }
    return QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\n';
}

QString systemPrompt(const Settings &settings, const Style &style)
{
    QStringList all;
    for (const rules::Section &s : rules::sections())
        all << s.id;
    return QStringLiteral(
               R"(Sei l'agente di Texfix, un programma che trasforma appunti di ogni tipo in appunti LaTeX da studiare. Lavori dentro il progetto aperto dall'utente e parli con lui in italiano, con parole semplici e risposte brevi.

Che cosa hai:
- i file del progetto: li leggi e li modifichi con gli strumenti per i file. `main.tex` è il documento, le lezioni sono in `src/`, le immagini in `imgs/`, le fonti in `fonti/`, i documenti da consultare in `docs/` (quelli non si cambiano mai);
- gli strumenti di Texfix (stato_progetto, compila_lezione, compila_documento, dubbi, impagina, chiudi_lezione, consegna_piano, consegna_esito): sono l'unico modo di compilare e di sapere lo stato delle lezioni. Non hai una shell: niente comandi, niente Python, niente git.

Regole che valgono sempre:
- prima di cambiare qualcosa chiama stato_progetto; una lezione FATTA si tocca solo dopo che l'utente ha detto di sì;
- dopo ogni modifica a una lezione chiama compila_lezione su quella lezione, e non dire che hai finito finché non compila pulita;
- `texfix.sty` lo scrive il programma: non modificarlo. In `salvataggi/` non scrivere;
- lo stile del progetto (il suo `.sty` nella cartella principale, se c'è) è un file del progetto: se l'utente lo chiede puoi aggiungere o cambiare un ambiente, un colore, una macro. Non togliere ciò che le lezioni usano, poi compila il documento. La descrizione dello stile (`<nome>.stile.json`) la aggiorna il programma, che chiede all'utente a che cosa serve un ambiente nuovo: non scriverla tu;
- le righe `% @L3.7 · fonte p.2` sono i marcatori dei blocchi e `% img src3-01` i segni delle figure: restano con il loro blocco, non si tolgono e non si inventano;
- le aggiunte tue vanno in `addedtext`, le correzioni alla fonte in `correction`;
- un lavoro grande (fondere più fonti in una lezione, integrare fonti nuove, riorganizzare, toccare più lezioni) comincia sempre da un piano: consegnalo con consegna_piano e fermati; lo esegui solo quando l'utente lo approva, e alla fine consegni l'esito con consegna_esito;
- di' sempre che cosa hai cambiato, in quali file, e che cosa resta da decidere all'utente.
)") + "\nREGOLE EDITORIALI\n\n" + rules::forPrompt(all, settings.rules) + "\n\nSTILE DEL DOCUMENTO\n\n" + style.forPrompt();
}

}
