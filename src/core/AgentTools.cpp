#include "core/AgentTools.h"
#include <QRegularExpression>
#include "core/Files.h"
#include "core/Tools.h"

#include "core/Compiler.h"
#include "core/Figures.h"
#include "core/Layout.h"
#include "core/NewLesson.h"
#include "core/Plans.h"
#include "core/Review.h"
#include "core/Rules.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>

#include <iostream>
#include <string>

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
                                   "aggiunge i marcatori dei blocchi che mancano e porta le fonti usate fuori da quelle in arrivo.",
                 QJsonObject{{"lezione", QJsonObject{{"type", "string"}}},
                             {"fonti", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}, {"description", "I file di fonti/coda usati"}}}},
                 {"lezione"}),
            tool("aggiungi_materiale", "Mette del materiale nuovo (file del progetto, o allegati della chat) fra quelli in arrivo, da smistare. "
                                       "È l'unico modo di portare materiale nuovo nel corso: Texfix lo riconosce per concetti e propone il "
                                       "piano quando l'utente preme «Smista». Non scrivere tu lezioni da materiale nuovo e non farti un piano tuo.",
                 QJsonObject{{"file", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}},
                                                  {"description", "I file, relativi alla cartella del progetto (anche salvataggi/chat/allegati/…)"}}},
                             {"lezione", QJsonObject{{"type", "string"}, {"description", "Solo se l'utente vuole il materiale in una lezione precisa: il suo file (src/…)"}}}},
                 {"file"}),
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
            tool("proponi_profilo", "Propone all'utente il profilo del corso, intero, in Markdown: «# Nome», poi «## Come si scrive», "
                                    "«## Riquadri», «## Controlli», «## Notazione», «## Figure», «## Regole in più» (una regola per voce). "
                                    "L'utente lo vede accanto a quello che c'è e decide: il profilo non si scrive mai a mano.",
                 QJsonObject{{"testo", QJsonObject{{"type", "string"}}},
                             {"nuovo", QJsonObject{{"type", "boolean"}, {"description", "Vero se è un profilo nuovo, da mettere anche nella libreria"}}}},
                 {"testo"}),
            tool("proponi_stile", "Propone all'utente uno stile LaTeX: il .sty e la sua descrizione. Prima lo compila su una pagina di prova "
                                  "con tutti i suoi ambienti: se non compila dice gli errori e non lo consegna. L'utente lo vede e decide: un "
                                  ".sty non si scrive mai a mano.",
                 QJsonObject{{"nome", QJsonObject{{"type", "string"}, {"description", "Il nome del pacchetto: una parola, lettere, cifre, - e _"}}},
                             {"sty", QJsonObject{{"type", "string"}, {"description", "Il file .sty intero"}}},
                             {"descrizione", QJsonObject{{"type", "object"}, {"description",
                                 "Come Texfix lo usa: {\"ambienti\": [{\"nome\", \"ruolo\" (a che cosa serve, una o due parole), \"prefisso\" (delle "
                                 "etichette, o vuoto), \"titolo\" (\"opzionale\" o \"nessuno\"), \"nota\"}], \"macro\": [{\"nome\", \"significato\"}], "
                                 "\"rimandi\": \"cref\", \"convenzioni\": \"…\"}"}}},
                             {"nuovo", QJsonObject{{"type", "boolean"}, {"description", "Vero se è uno stile nuovo, da mettere anche nella libreria"}}}},
                 {"nome", "sty", "descrizione"}),
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
    const bool writing = name == "compila_lezione" && !Compiler::reducedMain(project, lesson).isEmpty();   // anche una nuova, in src/
    if ((name == "compila_lezione" || name == "dubbi" || name == "impagina") && !project.inputs().contains(lesson) && !writing)
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
    if (name == "aggiungi_materiale") {
        const QString chosen = arguments.value("lezione").toString();
        if (!chosen.isEmpty() && !project.inputs().contains(chosen))
            return fail(QStringLiteral("«%1» non è una lezione del documento. Le lezioni si vedono con stato_progetto.").arg(chosen));
        QStringList paths, missing;
        for (const QJsonValue &v : arguments.value("file").toArray()) {
            const QString given = v.toString();
            const QString path = QFileInfo(given).isAbsolute() ? QFileInfo(given).absoluteFilePath() : project.path(given);
            if (outside(project.root(), QJsonObject{{"file_path", path}}))   // solo ciò che è nel progetto
                return fail(QStringLiteral("«%1» è fuori dal progetto: chiedi all'utente di aggiungerlo con «Aggiungi materiale».").arg(given));
            if (QFileInfo::exists(path))
                paths << path;
            else
                missing << given;
        }
        if (!missing.isEmpty())
            return fail(QStringLiteral("Non trovo %1.").arg(missing.join(", ")));
        int skipped = 0;
        const QStringList added = newlesson::toQueue(project, paths, chosen, &skipped);
        if (added.isEmpty())
            return fail(skipped > 0 ? QStringLiteral("Niente che Texfix sappia leggere.") : QStringLiteral("Non riesco a copiare i file fra quelli in arrivo."));
        QStringList names;
        for (const QString &a : added)
            names << QFileInfo(a).fileName();
        return QStringLiteral("In arrivo, da smistare: %1%2. Ora fermati e di' all'utente di premere «Smista», accanto ad Aggiungi materiale: "
                              "Texfix lo riconosce per concetti e gli propone il piano.")
            .arg(names.join(", "), skipped > 0 ? QStringLiteral(" (%1 file lasciati fuori: Texfix non sa leggerli)").arg(skipped) : QString());
    }
    if (name == "consegna_piano") {
        QString why;
        const QString id = plans::create(project, arguments, &why);
        if (id.isEmpty())
            return fail(why);
        plans::discard(project, arguments.value("lavoro").toString());   // il piano rifatto prende il posto di quello di prima
        return QStringLiteral("Piano consegnato (lavoro: %1). Ora fermati: l'utente lo legge, lo sistema e lo approva.").arg(id);
    }
    if (name == "proponi_profilo") {
        bool ok = false;
        const rules::Profile p = rules::Profile::parse(arguments.value("testo").toString(), &ok);
        if (!ok)
            return fail(QStringLiteral("Il profilo comincia con «# Nome»: manca il nome."));
        if (p.sections.isEmpty())
            return fail(QStringLiteral("Il profilo non ha sezioni: servono «## Come si scrive», «## Riquadri», «## Controlli», «## Notazione», «## Figure»."));
        QDir().mkpath(project.path(PROPOSALS));
        files::write(project.path(QStringLiteral("%1/profilo.md").arg(PROPOSALS)), p.markdown());
        files::write(project.path(QStringLiteral("%1/profilo.json").arg(PROPOSALS)),
                     QString::fromUtf8(QJsonDocument(QJsonObject{{"nuovo", arguments.value("nuovo").toBool()}}).toJson()));
        return QStringLiteral("Proposta consegnata: l'utente la vede accanto al profilo che c'è e decide. Ora fermati.");
    }
    if (name == "proponi_stile") {
        static const QRegularExpression shape(R"(^[A-Za-z][A-Za-z0-9_-]*$)");
        const QString styleName = arguments.value("nome").toString().trimmed(), sty = arguments.value("sty").toString();
        if (!shape.match(styleName).hasMatch() || styleName == "texfix")
            return fail(QStringLiteral("Il nome dello stile è una parola sola: lettere, cifre, - e _ (e non «texfix»)."));
        QJsonObject description = arguments.value("descrizione").toObject();
        description["formato"] = 1;
        description["nome"] = styleName;
        // la pagina di prova: ogni ambiente con un titolo e una riga, nella classe che lo stile chiede
        const QString dir = project.path("build/prova-stile");
        QDir(dir).removeRecursively();
        QDir().mkpath(dir);
        files::write(dir + '/' + styleName + ".sty", sty);
        files::write(dir + '/' + Style::descriptionFile(styleName), QString::fromUtf8(QJsonDocument(description).toJson()));
        bool ok = false;
        const Style proposed = Style::load(dir + '/' + Style::descriptionFile(styleName), &ok);
        QString sample = QStringLiteral("\\documentclass[11pt,a4paper%1]{article}\n\\usepackage[T1]{fontenc}\n\\usepackage[italian]{babel}\n"
                                        "\\usepackage{%2}\n\\begin{document}\n\\section{Prova}\nUn testo.\n")
                             .arg(proposed.classOptions.isEmpty() ? QString() : "," + proposed.classOptions, styleName);
        for (const Style::Environment &e : proposed.environments) {
            if (e.role == "altro")
                continue;
            sample += e.title == "nessuno" ? QStringLiteral("\\begin{%1}\nUna riga.\n\\end{%1}\n").arg(e.name)
                                           : QStringLiteral("\\begin{%1}[Titolo]\nUna riga.\n\\end{%1}\n").arg(e.name);
        }
        files::write(dir + "/prova.tex", sample + "\\end{document}\n");
        tools::run("pdflatex", {"-interaction=nonstopmode", "-halt-on-error", "prova.tex"}, dir, 120000);
        const QString log = files::read(dir + "/prova.log");
        QStringList errors;
        for (const QString &line : log.split('\n'))
            if (line.startsWith('!') && errors.size() < 5)
                errors << line;
        if (!QFileInfo::exists(dir + "/prova.pdf") || !errors.isEmpty())
            return fail(QStringLiteral("La pagina di prova non compila:\n%1").arg(errors.isEmpty() ? QStringLiteral("(nessun PDF)") : errors.join('\n')));
        QDir().mkpath(project.path(PROPOSALS));
        files::write(project.path(QStringLiteral("%1/%2.sty").arg(PROPOSALS, styleName)), sty);
        files::write(project.path(QStringLiteral("%1/%2").arg(PROPOSALS, Style::descriptionFile(styleName))), QString::fromUtf8(QJsonDocument(description).toJson()));
        files::write(project.path(QStringLiteral("%1/stile.json").arg(PROPOSALS)),
                     QString::fromUtf8(QJsonDocument(QJsonObject{{"nome", styleName}, {"nuovo", arguments.value("nuovo").toBool()}}).toJson()));
        return QStringLiteral("La pagina di prova compila. Proposta consegnata: l'utente la vede e decide. Ora fermati.");
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
    return QStringLiteral(
               R"(Sei l'agente di Texfix, un programma che trasforma appunti di ogni tipo in appunti LaTeX da studiare. Lavori dentro il progetto aperto dall'utente e parli con lui in italiano, con parole semplici e risposte brevi.

Che cosa hai:
- i file del progetto: li leggi e li modifichi con gli strumenti per i file. `main.tex` è il documento, le lezioni sono in `src/`, le immagini in `imgs/`, le fonti in `fonti/`, i documenti da consultare in `docs/` (quelli non si cambiano mai);
- gli strumenti di Texfix (stato_progetto, compila_lezione, compila_documento, dubbi, impagina, chiudi_lezione, aggiungi_materiale, consegna_piano, consegna_esito): sono l'unico modo di compilare e di sapere lo stato delle lezioni. Non hai una shell: niente comandi, niente Python, niente git.

Regole che valgono sempre:
- prima di cambiare qualcosa chiama stato_progetto; una lezione FATTA si tocca solo dopo che l'utente ha detto di sì;
- dopo ogni modifica a una lezione chiama compila_lezione su quella lezione, e non dire che hai finito finché non compila pulita;
- `texfix.sty` lo scrive il programma: non modificarlo. In `salvataggi/` non scrivere;
- lo stile del progetto (il suo `.sty` nella cartella principale, se c'è) è un file del progetto: se l'utente lo chiede puoi aggiungere o cambiare un ambiente, un colore, una macro. Non togliere ciò che le lezioni usano, poi compila il documento. La descrizione dello stile (`<nome>.stile.json`) la aggiorna il programma, che chiede all'utente a che cosa serve un ambiente nuovo: non scriverla tu;
- le righe `% @L3.7 · fonte p.2` sono i marcatori dei blocchi e `% img src3-01` i segni delle figure: restano con il loro blocco, non si tolgono e non si inventano;
- le aggiunte tue vanno in `addedtext`, le correzioni alla fonte in `correction`;
- il materiale nuovo (un file che l'utente ti dà o allega, appunti, dispense, una versione nuova di una fonte) passa sempre da aggiungi_materiale: Texfix lo riconosce per concetti e propone lui il piano quando l'utente preme «Smista». Non leggerlo per scriverne tu una lezione o un'aggiunta, e non farne un piano tuo;
- un altro lavoro grande (riorganizzare, rifare o fondere lezioni che ci sono già, toccare più lezioni) comincia sempre da un piano: consegnalo con consegna_piano e fermati; lo esegui solo quando l'utente lo approva, e alla fine consegni l'esito con consegna_esito;
- di' sempre che cosa hai cambiato, in quali file, e che cosa resta da decidere all'utente.
)") + "\nREGOLE EDITORIALI\n\n" + rules::forPrompt(rules::allIds(), settings.profile) + "\n\nSTILE DEL DOCUMENTO\n\n" + style.forPrompt();
}

}

namespace tx::agent {

QString workerPrompt(const Settings &settings, const Style &style)
{
    return QStringLiteral(
               R"(Sei uno degli agenti di Texfix, un programma che trasforma appunti di ogni tipo in appunti LaTeX da studiare. Esegui una parte di un piano che l'utente ha già letto e approvato: nessuno ti risponde mentre lavori, quindi non consegni piani e non fai domande. Fai il compito che ricevi fino in fondo.

Che cosa hai:
- i file del progetto: li leggi e li modifichi con gli strumenti per i file. Le lezioni sono in `src/`, le fonti in `fonti/`;
- gli strumenti di Texfix (stato_progetto, compila_lezione, dubbi, chiudi_lezione): sono l'unico modo di compilare. Non hai una shell.

Regole che valgono sempre:
- tocca solo la lezione del tuo compito (quella da completare, o quella nuova da scrivere); le fonti si leggono e basta. `main.tex`, `texfix.sty`, i `.sty` e `salvataggi/` non si toccano: chiudi_lezione mette nel documento una lezione nuova;
- altri agenti lavorano nello stesso momento su altre lezioni: non aprire né compilare le loro;
- dopo ogni modifica chiama compila_lezione sulla tua lezione, finché compila pulita;
- le righe `% @L3.7 · fonte p.2` sono i marcatori dei blocchi e `% img src3-01` i segni delle figure: restano con il loro blocco, non si tolgono e non si inventano;
- le aggiunte tue vanno in `addedtext`, le correzioni alla fonte in `correction`;
- dove le fonti non sono chiare o non concordano scegli la lettura più fedele e dillo nell'esito;
- ogni fonte del compito va letta: se una non si apre o non si legge (protetta da password, rovinata, vuota) e non ce n'è una copia leggibile, non ripiegare sulle altre e non scrivere niente. Il tuo ultimo messaggio comincia allora con «FONTE ILLEGGIBILE:» e dice quale fonte e perché; Texfix ferma il lavoro;
- alla fine chiama chiudi_lezione con la lezione e le fonti che hai usato;
- il tuo ultimo messaggio è l'esito per l'utente: in italiano, breve, a punti. Che cosa hai aggiunto o cambiato e dove (vicino a quale blocco), e che cosa resta da decidere a lui.
)") + "\nREGOLE EDITORIALI\n\n" + rules::forPrompt(rules::allIds(), settings.profile) + "\n\nSTILE DEL DOCUMENTO\n\n" + style.forPrompt();
}

bool guarded(const QString &root, const QJsonObject &input)
{
    const QString base = QFileInfo(root).canonicalFilePath();
    for (const char *key : {"file_path", "path", "notebook_path"}) {
        const QString given = input.value(QLatin1String(key)).toString();
        if (given.isEmpty())
            continue;
        // relativo alla radice com'è scritta o com'è davvero (sul Mac /var è /private/var)
        QString relative;
        for (const QString &top : {QFileInfo(root).absoluteFilePath(), base}) {
            const QString candidate = QDir(top).relativeFilePath(QDir::cleanPath(QDir(top).absoluteFilePath(given)));
            if (!candidate.startsWith(".."))
                relative = candidate;
        }
        if (relative.isEmpty())
            continue;
        if (relative == QLatin1String(Project::PROFILE) || relative.startsWith(QLatin1String(PROPOSALS)) || relative == QLatin1String(Project::PACKAGE)
            || (!relative.contains('/') && (relative.endsWith(".sty") || relative.endsWith(".stile.json"))))
            return true;
    }
    return false;
}

QString guardedRules()
{
    QStringList out;
    for (const char *tool : {"Edit", "Write", "MultiEdit", "NotebookEdit"})
        for (const char *pattern : {"salvataggi/profilo.md", "salvataggi/proposte/**", "*.sty", "*.stile.json"})
            out << QStringLiteral("%1(%2)").arg(QLatin1String(tool), QLatin1String(pattern));
    return out.join(',');
}

QString procedureMessage(const Project &project, const QString &kind)
{
    const Settings settings = Settings::load(project);
    const QString style = (settings.style.isEmpty() ? QString::fromUtf8(Project::DEFAULT_STYLE) : settings.style);
    const QString program = settings.program.isEmpty() ? QString() : QStringLiteral(", il programma del corso (`%1`)").arg(settings.program);
    const QString look = QStringLiteral("Leggi lo stile del progetto (`%1.sty` e la sua descrizione `%1.stile.json`: gli ambienti con il loro ruolo), "
                                        "il profilo che c'è (`%2`), le lezioni in `src/` e le fonti in `fonti/` se ce ne sono%3.")
                             .arg(style, QString::fromUtf8(Project::PROFILE), program);
    const QString profileShape = QStringLiteral(
        "Il profilo dice come si scrive per questo corso: «# Nome», poi «## Come si scrive», «## Riquadri», «## Controlli», «## Notazione», "
        "«## Figure», «## Regole in più» (una regola per voce). Regole brevi, a elenco, che parlano dei ruoli dello stile (una tesi, un "
        "esempio), non dei nomi degli ambienti; niente di ciò che Texfix fa sempre (sorgente, etichette e rimandi, larghezza delle righe, "
        "posto delle figure, aggiunte e correzioni dichiarate, impaginazione): è già nelle sue regole.");
    if (kind == "crea-profilo")
        return QStringLiteral("Crea il profilo di questo corso.\n- %1\n- Fai all'utente poche domande, al più cinque e tutte insieme: la materia, "
                              "a che cosa gli servono gli appunti, come vuole formule, citazioni, figure, esempi. Aspetta le risposte.\n- %2\n"
                              "- Consegnalo con proponi_profilo (nuovo: vero), con un nome suo, e fermati.").arg(look, profileShape);
    if (kind == "migliora-profilo")
        return QStringLiteral("Migliora il profilo di questo corso.\n- %1\n- Chiedi all'utente che cosa non va o che cosa vuole diverso, se non lo ha "
                              "già detto. Aspetta la risposta.\n- %2\n- Consegna il profilo intero, cambiato, con proponi_profilo (nuovo: falso), e fermati.")
            .arg(look, profileShape);
    const QString styleShape = QStringLiteral(
        "Uno stile è un pacchetto LaTeX per `article` (amsthm, o tcolorbox se l'utente vuole riquadri colorati) con un ambiente per ogni "
        "ruolo che serve al corso, numerati per sezione, i nomi dei rimandi per `\\cref` in italiano, e le macro che il corso usa. La "
        "descrizione dà a ogni ambiente il suo ruolo in una o due parole («tesi», «caso clinico»), il prefisso delle etichette, se ha un "
        "titolo, una nota d'uso breve. Non carica pacchetti esotici e non tocca la pagina oltre il necessario.");
    if (kind == "crea-stile")
        return QStringLiteral("Crea lo stile di questo corso.\n- %1\n- Fai all'utente poche domande, al più cinque e tutte insieme: che riquadri "
                              "gli servono, se li vuole colorati o sobri, una colonna o due. Aspetta le risposte.\n- %2\n- Consegnalo con "
                              "proponi_stile (nuovo: vero), con un nome nuovo: lo compila su una pagina di prova; se non compila, correggi e "
                              "riprova. Poi fermati.").arg(look, styleShape);
    return QStringLiteral("Migliora lo stile di questo corso (`%1.sty`).\n- %2\n- Chiedi all'utente che cosa vuole diverso, se non lo ha già detto. "
                          "Aspetta la risposta.\n- %3\n- Consegna lo stile intero, cambiato, con proponi_stile (stesso nome, nuovo: falso): lo "
                          "compila su una pagina di prova; se non compila, correggi e riprova. Poi fermati.").arg(style, look, styleShape);
}

bool outside(const QString &root, const QJsonObject &input)
{
    const QString base = QFileInfo(root).canonicalFilePath();
    for (const char *key : {"file_path", "path", "notebook_path"}) {
        const QString given = input.value(QLatin1String(key)).toString();
        if (given.isEmpty())
            continue;
        QFileInfo info(QDir(base).absoluteFilePath(given));
        while (!info.exists() && info.absolutePath() != info.absoluteFilePath())    // un file nuovo: conta la cartella che c'è
            info = QFileInfo(info.absolutePath());
        const QString real = info.canonicalFilePath();
        if (!(real == base || real.startsWith(base + '/')))
            return true;
    }
    return false;
}

int serve(const QString &root)
{
    const Project project(root);
    const Settings settings = Settings::load(project);
    Style style = Style::classic();
    if (!settings.style.isEmpty()) {
        bool ok = false;
        const Style described = Style::load(project.path(Style::descriptionFile(settings.style)), &ok);
        if (ok)
            style = described;
    }
    std::string line;
    while (std::getline(std::cin, line)) {
        const QByteArray request = QByteArray::fromStdString(line);
        const QJsonObject message = QJsonDocument::fromJson(request).object();
        const QString method = message.value("method").toString();
        const QJsonObject params = message.value("params").toObject();
        QByteArray reply;
        if (method == "tools/list") {                  // solo gli strumenti di chi lavora dentro un piano, più la conferma
            QJsonArray shown;
            for (const QJsonValue &t : tools())
                if (WORKER_TOOLS.contains(t.toObject().value("name").toString()) || t.toObject().value("name").toString() == "conferma")
                    shown.append(t);
            reply = QJsonDocument(QJsonObject{{"jsonrpc", "2.0"}, {"id", message.value("id")}, {"result", QJsonObject{{"tools", shown}}}})
                        .toJson(QJsonDocument::Compact) + '\n';
        } else if (method == "tools/call" && params.value("name").toString() == "conferma") {
            // i file del progetto sì, fuori no; uno strumento di Texfix solo se è fra i suoi
            const QJsonObject asked = params.value("arguments").toObject(), input = asked.value("input").toObject();
            const QString tool = asked.value("tool_name").toString();
            const bool writes = QStringList{"Edit", "Write", "MultiEdit"}.contains(tool);
            const bool allowed = QStringList{"Read", "Edit", "Write", "MultiEdit", "Grep", "Glob"}.contains(tool)
                                     ? !outside(root, input) && !(writes && guarded(root, input))
                                     : tool.startsWith("mcp__texfix__") && WORKER_TOOLS.contains(tool.mid(13));
            const QJsonObject decision = allowed ? QJsonObject{{"behavior", "allow"}, {"updatedInput", input}}
                                                 : QJsonObject{{"behavior", "deny"}, {"message", "Non è permesso: lavora solo sui file del progetto e con gli strumenti che hai."}};
            const QJsonObject result{{"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", QString::fromUtf8(QJsonDocument(decision).toJson(QJsonDocument::Compact))}}}}};
            reply = QJsonDocument(QJsonObject{{"jsonrpc", "2.0"}, {"id", message.value("id")}, {"result", result}}).toJson(QJsonDocument::Compact) + '\n';
        } else if (method == "tools/call" && !WORKER_TOOLS.contains(params.value("name").toString())) {
            const QJsonObject result{{"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", "Questo strumento non c'è per chi lavora dentro un piano."}}}}, {"isError", true}};
            reply = QJsonDocument(QJsonObject{{"jsonrpc", "2.0"}, {"id", message.value("id")}, {"result", result}}).toJson(QJsonDocument::Compact) + '\n';
        } else {
            reply = handle(project, style, request);
        }
        if (!reply.isEmpty())
            std::cout << reply.toStdString() << std::flush;
    }
    return 0;
}

}
