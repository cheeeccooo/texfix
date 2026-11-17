// Le prove del motore: senza finestre e senza modello. Ogni prova lavora in una cartella temporanea sua.
#include "core/AgentTools.h"
#include "core/Blocks.h"
#include "core/Compiler.h"
#include "core/Convert.h"
#include "core/NewLesson.h"
#include "core/Plans.h"
#include "core/Profiles.h"
#include "core/Provenance.h"
#include "core/Prompts.h"
#include "core/Reorganize.h"
#include "core/Review.h"
#include "core/Figures.h"
#include "core/Files.h"
#include "core/Fix.h"
#include "core/Git.h"
#include "core/History.h"
#include "core/Layout.h"
#include "core/Project.h"
#include "core/Rules.h"
#include "core/Settings.h"
#include "core/Snippets.h"
#include "core/Sort.h"
#include "core/Style.h"
#include "core/StyleSwitch.h"
#include "core/SyncTex.h"
#include "core/TexSource.h"
#include "core/TexLog.h"
#include "core/Tools.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>

#include <future>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QImage>
#include <QTest>

using namespace tx;

class CoreTests : public QObject {
    Q_OBJECT

    static bool hasLatexmk() { return !QStandardPaths::findExecutable("latexmk").isEmpty(); }

    // compila e aspetta la fine
    static BuildResult build(Compiler &compiler, const QString &lesson)
    {
        QSignalSpy done(&compiler, &Compiler::finished);
        if (lesson.isEmpty())
            compiler.compileDocument();
        else
            compiler.compileLesson(lesson);
        if (done.isEmpty() && !done.wait(120000))
            return {.failure = "non finisce"};
        return done.first().first().value<BuildResult>();
    }

private slots:
    void initTestCase() { tools::adoptPath(); }

    void freeName()
    {
        QTemporaryDir dir;
        const QString a = dir.filePath("appunti.pdf");
        QCOMPARE(files::freeName(a), a);
        QVERIFY(files::write(a, "x"));
        QCOMPARE(files::freeName(a), dir.filePath("appunti (2).pdf"));
        QVERIFY(files::write(dir.filePath("appunti (2).pdf"), "x"));
        QCOMPARE(files::freeName(a), dir.filePath("appunti (3).pdf"));
        QDir().mkpath(dir.filePath("v1.0"));
        QCOMPARE(files::freeName(dir.filePath("v1.0")), dir.filePath("v1.0 (2)"));   // una cartella non ha estensione
    }

    void uncommentedLine()
    {
        QCOMPARE(uncommented(u"a % b").toString(), "a ");
        QCOMPARE(uncommented(u"50\\% di a % b").toString(), "50\\% di a ");
        QCOMPARE(uncommented(u"\\\\% b").toString(), "\\\\");
        QCOMPARE(uncommented(u"niente").toString(), "niente");
    }

    void settings()
    {
        QTemporaryDir dir;
        const Project p(dir.path());
        p.complete();
        Settings s = Settings::load(p);
        QCOMPARE(s.quality, "bilanciata");                                             // i predefiniti
        QCOMPARE(s.layout, "da_sola");
        QVERIFY(s.setValue("qualita", "massima"));
        QVERIFY(!s.setValue("qualita", "infinita"));
        QVERIFY(!s.setValue("non_esiste", "x"));
        s.rules = {"I vettori si scrivono con la freccia."};
        s.setDone("src/src1.tex", true);
        QVERIFY(s.save(p));
        const Settings back = Settings::load(p);
        QCOMPARE(back.quality, "massima");
        QCOMPARE(back.rules, s.rules);
        QVERIFY(back.isDone("src/src1.tex"));
        QVERIFY(!back.isDone("src/src2.tex"));
        QVERIFY(Project::isProject(dir.path()));
        QCOMPARE(Settings::choices("scrittura").size(), 2);
    }

    // Le regole editoriali: predefinite, cambiate nelle Preferenze, riportate al predefinito; il progetto in fondo.
    void editorialRules()
    {
        QTemporaryDir dir;
        rules::setHome(dir.path());
        QCOMPARE(rules::sections().size(), 8);
        for (const rules::Section &s : rules::sections()) {
            QVERIFY2(!s.text.isEmpty(), qPrintable(s.id));
            QVERIFY(!s.custom);
        }
        QVERIFY(rules::setText("sorgente", "Indentazione: 2 spazi."));
        QVERIFY(rules::section("sorgente").custom);
        QCOMPARE(rules::section("sorgente").text, "Indentazione: 2 spazi.");
        QVERIFY(!rules::setText("non_esiste", "x"));
        const QString prompt = rules::forPrompt({"sorgente", "scrittura"}, {"Mai il passivo."});
        QVERIFY(prompt.startsWith("## Stile del sorgente\n\nIndentazione: 2 spazi.\n\n## Come si scrive\n\n"));
        QVERIFY(prompt.endsWith("## Regole di questo progetto\n\nValgono più di tutte le altre.\n\n- Mai il passivo."));
        QVERIFY(rules::setText("sorgente", rules::defaultText("sorgente") + "\n"));   // uguale alla predefinita: non è più cambiata
        QVERIFY(!rules::section("sorgente").custom);
        rules::setText("sorgente", "x");
        rules::reset("sorgente");
        QCOMPARE(rules::section("sorgente").text, rules::defaultText("sorgente"));
        rules::setHome({});
    }

    void style()
    {
        const Style classic = Style::classic();
        QCOMPARE(classic.environmentFor("teorema"), "theorem");
        QCOMPARE(classic.environmentFor("attenzione"), QString());
        QVERIFY(classic.undecided().isEmpty());
        QVERIFY(classic.forPrompt().contains("- Definizione: `definition`, titolo opzionale"));
        QVERIFY(classic.forPrompt().contains("\\cref{etichetta}"));

        const QString sty = "\\definecolor{defgreen}{RGB}{164,225,174} % \\newtcolorbox{finto}\n"
                            "\\definecolor{warn}{HTML}{FF8800}\n"
                            "\\newtcolorbox{definitionbox}[1][]{colframe=defgreen, breakable}\n"
                            "\\newtcolorbox{achtungbox}{colframe=warn, unbreakable}\n"
                            "\\newenvironment{sidenote}{\\par}{\\par}\n"
                            "\\NewNoteBox{theorembox}{thm}{theorem}{Teorema}{x}\n"
                            "\\newcommand\\R{\\mathbb{R}} \\newcommand{\\norm}[1]{\\lVert #1\\rVert}\n"
                            "\\newcommand\\my@internal{x}\n\\NewDocumentCommand\\tagref{m}{#1}\n";
        Style s = Style::scan("mio", sty);
        QStringList names;
        for (const Style::Environment &e : s.environments)
            names << e.name + "=" + e.role;
        QCOMPARE(names, (QStringList{"definitionbox=definizione", "achtungbox=attenzione", "sidenote=", "theorembox=teorema"}));
        QCOMPARE(s.environment("definitionbox")->color, "#a4e1ae");
        QCOMPARE(s.environment("achtungbox")->color, "#ff8800");
        QVERIFY(!s.environment("achtungbox")->breaks);
        QCOMPARE(s.undecided(), QStringList{"sidenote"});
        QCOMPARE(s.macros.size(), 3);
        QCOMPARE(s.macros[1].name, "\\norm");
        QCOMPARE(s.references, "tag");

        QTemporaryDir dir;
        s.conventions = "Il fulcro va in evidenza.";
        s.macros[0].meaning = "i reali";
        QVERIFY(s.save(dir.filePath(Style::descriptionFile("mio"))));
        bool ok = false;
        QCOMPARE(Style::load(dir.filePath("mio.stile.json"), &ok), s);
        QVERIFY(ok);
        QVERIFY(s.forPrompt().contains("- `\\R`: i reali"));
        QVERIFY(s.forPrompt().endsWith("Il fulcro va in evidenza."));
        Style::load(dir.filePath("manca.json"), &ok);
        QVERIFY(!ok);
    }

    // Importare uno stile e cambiare stile: le lezioni si riscrivono secondo i ruoli, e si torna indietro.
    void switchStyle()
    {
        QTemporaryDir dir, outside;
        const QString root = dir.filePath("prova");
        QCOMPARE(Project::create(root), QString());
        const Project p(root);
        QString main = files::read(p.path("main.tex"));
        files::write(p.path("main.tex"), main.replace("\\end{document}", "\\input{src/a.tex}\n\\end{document}"));
        files::write(p.path("src/a.tex"), "\\begin{definition}[Norma]\nx, vedi \\cref{thm:a}\n\\end{definition}\n\\begin{remark}r\\end{remark}\n\\begin{align*}a\\end{align*}\n");
        files::write(outside.filePath("mio.sty"),
                     "\\ProvidesPackage{mio}\n\\RequirePackage{amsmath,amsthm,hyperref,cleveref}\n"
                     "\\newtheorem{definitionbox}{Definizione}\n\\newtheorem{theorembox}{Teorema}\n\\newcommand\\tagref[1]{\\cref{#1}}\n");

        QVERIFY(!styleswitch::import(p, outside.filePath("manca.sty")).error.isEmpty());
        const styleswitch::Imported in = styleswitch::import(p, outside.filePath("mio.sty"));
        QCOMPARE(in.error, QString());
        QVERIFY(!in.described);
        QVERIFY(QFileInfo::exists(p.path("mio.sty")));
        QVERIFY(QFileInfo::exists(p.path("mio.stile.json")));
        QCOMPARE(styleswitch::current(p).name, QString());                              // importare non cambia ancora lo stile

        styleswitch::Outcome o = styleswitch::apply(p, "mio");
        QCOMPARE(o.error, QString());
        QCOMPARE(o.lessons, 1);
        QCOMPARE(o.unmatched, QStringList{"remark"});                                  // «mio» non ha osservazioni
        QCOMPARE(files::read(p.path("src/a.tex")),
                 "\\begin{definitionbox}[Norma]\nx, vedi \\tagref{thm:a}\n\\end{definitionbox}\n\\begin{remark}r\\end{remark}\n\\begin{align*}a\\end{align*}\n");
        QCOMPARE(styleswitch::current(p).name, "mio");
        QVERIFY(files::read(p.path("texfix.sty")).contains("\\RequirePackage{mio}"));
        QVERIFY(!files::read(p.path("texfix.sty")).contains("newtheorem"));

        // la scelta dell'utente per ciò che non ha corrispondente; poi il ritorno a nessuno stile
        files::write(p.path("src/a.tex"), "\\begin{definitionbox}d\\end{definitionbox}\n\\begin{theorembox}t\\end{theorembox}\n");
        o = styleswitch::apply(p, {});
        QCOMPARE(o.unmatched, QStringList());
        QCOMPARE(files::read(p.path("src/a.tex")), "\\begin{definition}d\\end{definition}\n\\begin{theorem}t\\end{theorem}\n");
        QVERIFY(files::read(p.path("texfix.sty")).contains("\\newtheorem{theorem}"));
        files::write(p.path("src/a.tex"), "\\begin{remark}r\\end{remark}\n");
        o = styleswitch::apply(p, "mio", {{"remark", "theorembox"}});
        QCOMPARE(o.unmatched, QStringList());
        QCOMPARE(files::read(p.path("src/a.tex")), "\\begin{theorembox}r\\end{theorembox}\n");
        QVERIFY(!styleswitch::apply(p, "non_importato").error.isEmpty());

        if (hasLatexmk()) {                                                            // con lo stile importato il documento compila
            Compiler compiler;
            compiler.setProject(p);
            const BuildResult r = build(compiler, {});
            QVERIFY2(!r.hasErrors(), qPrintable(r.issues.isEmpty() ? r.failure : r.issues.first().text));
        }
    }

    // I marcatori dei blocchi: un riquadro, un tratto di testo, una figura con la didascalia; quelli che ci sono restano.
    void blockMarkers()
    {
        const blocks::Units units{{"theorem", "definition"}, {"center"}};
        const QString tex = "\\subsection{Uno}\nTesto prima,\nsu due righe.\n\n\\begin{definition}[Norma]\n    x\n    \\begin{itemize}\\item a\\end{itemize}\n\\end{definition}\n"
                            "% @L3.7 · vecchio.pdf p.9\nAncora testo.\n\\begin{center}\n    \\imgslot{f01}{0.6}{d}\n\\end{center}\n\\caption{c}\n\\begin{align*}\na\n\\end{align*}\n";
        const QList<blocks::Block> found = blocks::parse(tex, units);
        QStringList kinds;
        for (const blocks::Block &b : found)
            kinds << QStringLiteral("%1:%2-%3").arg(b.kind).arg(b.line).arg(b.endLine);
        QCOMPARE(kinds, (QStringList{"testo:2-3", "definition:5-8", "testo:10-10", "figura:11-14", "testo:15-17"}));
        QCOMPARE(found[1].title, "Norma");
        QCOMPARE(found[2].id, "L3.7");
        QCOMPARE(found[2].pages(), QList<int>{9});
        const QString marked = blocks::mark(tex, 3, units, [](const blocks::Block &b) { return b.kind == "figura" ? QString("a.pdf p.2") : QString(); });
        QVERIFY(marked.contains("% @L3.8\nTesto prima,"));
        QVERIFY(marked.contains("% @L3.9\n\\begin{definition}[Norma]"));
        QVERIFY(marked.contains("% @L3.7 · vecchio.pdf p.9\nAncora testo."));
        QVERIFY(marked.contains("% @L3.10 · a.pdf p.2\n\\begin{center}"));
        QCOMPARE(blocks::mark(marked, 3, units), marked);                              // già tutti marcati: non cambia
    }

    // Converti con un finto modello: la lezione scritta, il documento, la fonte fra quelle usate, i dubbi, il costo;
    // un lavoro rifatto riusa ciò che è già fatto e pagato; «Ferma» interrompe subito.
    void convert()
    {
        QTemporaryDir dir;
        const QString root = dir.filePath("prova");
        QCOMPARE(Project::create(root), QString());
        const Project p(root);
        files::write(p.path("fonti/coda/Numeri_complessi.md"), "Def. modulo |z|. Oss. vedi disegno.\n");
        files::write(p.path("fonti/coda/tabella.xlsx"), "x");

        // il finto modello: risponde secondo il prompt di sistema che riceve
        const QString answer = "```json\n{\"units\": [{\"env\": \"definition\", \"page\": 1}, {\"env\": \"text\", \"page\": 1}],"
                               " \"figures\": [{\"id\": \"F1\", \"page\": 1}],"
                               " \"doubts\": [{\"page\": 1, \"what\": \"È z o 2?\", \"latex\": \"|z|\", \"readings\": [{\"text\": \"z\", \"latex\": \"|z|\"}, {\"text\": \"2\", \"latex\": \"|2|\"}]}]}\n```\n"
                               "```latex\n\\subsection{Modulo}\n\\begin{definition}[Modulo]\n    Il modulo è $|z|$, con \\underline{z} complesso e $\\underline{w}$ no.\n\\end{definition}\n"
                               "\\begin{correction}\n    Negli appunti manca la radice.\n\\end{correction}\n"
                               "\\begin{center} % img F1\n    \\imgslot[0.6\\linewidth]{F1}{0.6}{il piano complesso}\n\\end{center}\n```\n";
        auto reply = [&](const char *name, const QString &text) {
            const QJsonObject line{{"type", "result"}, {"result", text}, {"total_cost_usd", 0.25}, {"usage", QJsonObject{{"input_tokens", 10}, {"output_tokens", 5}}}};
            files::writeBytes(dir.filePath(name), QJsonDocument(line).toJson(QJsonDocument::Compact) + '\n');
        };
        reply("trascrizione.json", answer);
        reply("verifica.json", "{\"missing\": [], \"wrong\": []}");
        reply("figura.json", "```latex\n\\begin{tikzpicture}\n    \\draw[->] (0,0) -- (30,0);\n\\end{tikzpicture}\n```");
        reply("bene.json", "OK");
        reply("titolo.json", "{\"title\": \"Il modulo di un numero complesso\"}");
        files::write(dir.filePath("claude"), QString("#!/bin/sh\nin=$(cat)\necho x >> '%1/chiamate'\n"
                                                     "case \"$in\" in *'how your code renders'*) cat '%1/bene.json'; exit 0;; esac\n"
                                                     "for a; do case \"$a\" in *'You check LaTeX'*) cat '%1/verifica.json'; exit 0;;\n"
                                                     "  *'You redraw one'*) cat '%1/figura.json'; exit 0;;\n"
                                                     "  *'beginning of the LaTeX notes'*) cat '%1/titolo.json'; exit 0;; esac; done\n"
                                                     "cat '%1/trascrizione.json'\n").arg(dir.path()));
        QFile(dir.filePath("claude")).setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qputenv("TEXFIX_CLAUDE", dir.filePath("claude").toUtf8());

        QCOMPARE(Convert::queue(p).size(), 2);
        QCOMPARE(Convert::queue(p).first().kind, "testo");
        QVERIFY(Convert::estimate("massima", Convert::queue(p)) > Convert::estimate("veloce", Convert::queue(p)));
        QStringList sources;
        for (const Convert::Source &s : Convert::queue(p))
            sources << s.file;

        JobControl control;
        Convert::Result r = Convert(p, Settings::load(p), Style::classic(), &control).run(sources);
        QCOMPARE(r.failure, QString());
        QCOMPARE(r.written(), 1);
        QCOMPARE(r.cost, 1.25);                                                        // trascrizione, verifica, titolo, disegno, revisione
        QCOMPARE(r.lessons[0].file, "src/src1.tex");
        QCOMPARE(r.lessons[0].doubts, 1);
        QCOMPARE(r.lessons[0].figures, 0);                                             // la figura è stata disegnata e inserita
        QCOMPARE(figures::all(p).size(), 1);
        QCOMPARE(figures::find(p, "src1-01").state, "disegnata");
        QCOMPARE(figures::find(p, "src1-01").description, "il piano complesso");
        QCOMPARE(figures::lessonOf(p, "src1-01"), "src/src1.tex");
        QCOMPARE(r.lessons[0].corrections, 1);
        QVERIFY(!r.lessons[1].failure.isEmpty());                                      // il foglio di calcolo non si converte
        QCOMPARE(files::read(p.path("src/src1.tex")),
                 "\\section{Il modulo di un numero complesso}\n\\lezione{1}{}\n\n\\subsection{Modulo}\n"
                 "% @L1.1 · Numeri_complessi.md\n\\begin{definition}[Modulo]\n    Il modulo è $|z|$, con \\textbf{z} complesso e $\\underline{w}$ no.\n\\end{definition}\n"
                 "% @L1.2 · Numeri_complessi.md\n\\begin{correction}\n    Negli appunti manca la radice.\n\\end{correction}\n"
                 "% @L1.3 · Numeri_complessi.md\n\\begin{center} % img src1-01\n    \\resizebox{\\linewidth}{!}{%\n    \\begin{tikzpicture}\n        \\draw[->] (0,0) -- (30,0);\n    \\end{tikzpicture}%\n    }\n\\end{center}\n");   // più larga della colonna: ristretta
        QCOMPARE(p.inputs(), QStringList{"src/src1.tex"});
        QVERIFY(QFileInfo::exists(p.path("fonti/usate/lezione 1/Numeri_complessi.md")));
        QVERIFY(!QFileInfo::exists(p.path("fonti/coda/Numeri_complessi.md")));
        QVERIFY(QFileInfo::exists(p.path("fonti/coda/tabella.xlsx")));
        const QJsonObject doubts = QJsonDocument::fromJson(files::read(p.path(Convert::doubtsFile("src/src1.tex"))).toUtf8()).object();
        QCOMPARE(doubts.value("dubbi").toArray().first().toObject().value("id").toString(), "src1-d1");
        QVERIFY(Convert::interrupted(p).isEmpty());

        if (hasLatexmk()) {                                                            // la lezione scritta compila
            Compiler compiler;
            compiler.setProject(p);
            const BuildResult b = build(compiler, {});
            QVERIFY2(!b.hasErrors(), qPrintable(b.issues.isEmpty() ? b.failure : b.issues.first().text));
        }

        // la stessa fonte di nuovo: il blocco è già fatto e pagato, il modello non si chiama
        const qsizetype callsBefore = files::read(dir.filePath("chiamate")).count('x');
        QCOMPARE(callsBefore, 5);
        files::write(p.path("fonti/coda/Numeri_complessi.md"), "Def. modulo |z|. Oss. vedi disegno.\n");
        QFile::remove(p.path("src/src1.tex"));
        r = Convert(p, Settings::load(p), Style::classic(), &control).run({"fonti/coda/Numeri_complessi.md"});
        QCOMPARE(r.written(), 1);
        QCOMPARE(r.cost, 0.0);
        QCOMPARE(files::read(dir.filePath("chiamate")).count('x'), callsBefore);

        // «Ferma» a metà di una chiamata: subito, e resta da riprendere
        files::write(dir.filePath("claude"), "#!/bin/sh\nsleep 30\n");
        files::write(p.path("fonti/coda/Altra.md"), "altro\n");
        JobControl stopping;
        QElapsedTimer timer;
        timer.start();
        auto future = std::async(std::launch::async, [&] { return Convert(p, Settings::load(p), Style::classic(), &stopping).run({"fonti/coda/Altra.md"}); });
        QTest::qWait(600);
        stopping.stop();
        r = future.get();
        QVERIFY(r.stopped);
        QVERIFY(timer.elapsed() < 4000);
        QCOMPARE(Convert::interrupted(p), QStringList{"fonti/coda/Altra.md"});
        QVERIFY(QFileInfo::exists(p.path("fonti/coda/Altra.md")));
        qunsetenv("TEXFIX_CLAUDE");
    }

    // I prompt hanno le tre parti, una volta sola ciascuna.
    void promptParts()
    {
        QTemporaryDir dir;
        rules::setHome(dir.path());
        Settings s;
        s.rules = {"Mai il passivo."};
        const QString studio = prompts::transcribe("src/src4.tex", s, Style::classic());
        QVERIFY(studio.contains("one fragment of `src/src4.tex`"));
        QVERIFY(studio.contains("\\label{<prefix>:src4-c<chunk>-<word>}"));
        QVERIFY(studio.contains("\\begin{center} % img F<k>"));
        QVERIFY(studio.contains("one of definition, theorem, proposition, lemma, corollary, proof, remark, example, exercise, or text"));
        QCOMPARE(studio.count("## Come si scrive"), 1);
        QCOMPARE(studio.count("- Mai il passivo."), 1);
        QCOMPARE(studio.count("## Lo stile del documento"), 1);
        QVERIFY(studio.contains("addedtext"));
        s.writing = "fedele";
        const QString faithful = prompts::transcribe("src/src4.tex", s, Style::classic());
        QVERIFY(faithful.contains("one-to-one transcription"));
        QVERIFY(!faithful.contains("## Come si scrive"));
        QVERIFY(!faithful.contains("\\begin{addedtext}"));
        QVERIFY(prompts::verify(s).contains("strict one-to-one"));
        rules::setHome({});
    }

    // Rivedi: una lettura scelta riscrive il passo, riaprire lo rimette; un'aggiunta si tiene, si toglie, si rimette.
    void reviewDecisions()
    {
        QTemporaryDir dir;
        const Project p(dir.path());
        const QString lesson = "src/src1.tex";
        const QString tex = "\\section{S}\nIl modulo è $|z|$.\n% @L1.2\n\\begin{addedtext}\n    Perché sì.\n\\end{addedtext}\nFine.\n\\begin{correction}\n    Era sbagliato.\n\\end{correction}\n";
        files::write(p.path(lesson), tex);
        files::write(p.path(Convert::doubtsFile(lesson)),
                     R"({"dubbi": [{"id": "src1-d1", "page": 2, "source": "a.pdf", "what": "z o 2?", "latex": "$|z|$",
                         "readings": [{"text": "z", "latex": "$|z|$"}, {"text": "2", "latex": "$|2|$"}]}]})");
        QCOMPARE(review::counts(p, lesson).doubts, 1);
        QCOMPARE(review::counts(p, lesson).declared, 2);
        QCOMPARE(review::doubts(p, lesson).first().line, 2);

        QCOMPARE(review::choose(p, lesson, "src1-d1", 1), QString());                   // un'altra lettura: il passo cambia
        QVERIFY(files::read(p.path(lesson)).contains("Il modulo è $|2|$."));
        review::Doubt d = review::doubts(p, lesson).first();
        QVERIFY(!d.open);
        QCOMPARE(d.chosen, 1);
        QCOMPARE(d.how, "scelta");
        QCOMPARE(review::choose(p, lesson, "src1-d1", 0), QString());                   // si cambia idea: torna la prima
        QVERIFY(files::read(p.path(lesson)).contains("$|z|$"));
        QCOMPARE(review::doubts(p, lesson).first().how, "confermata");
        review::choose(p, lesson, "src1-d1", 1);
        QCOMPARE(review::reopen(p, lesson, "src1-d1", true), QString());                // riapri e annulla la modifica
        QCOMPARE(files::read(p.path(lesson)), tex);
        QVERIFY(review::doubts(p, lesson).first().open);
        QVERIFY(!review::choose(p, lesson, "non-esiste", 0).isEmpty());

        const QList<review::Declared> all = review::declared(p, lesson);
        QCOMPARE(all.size(), 2);
        QCOMPARE(all[0].kind, "addedtext");
        QCOMPARE(all[0].text, "Perché sì.");
        QCOMPARE(all[0].line, 4);
        review::keep(p, lesson, all[1].hash);
        QCOMPARE(review::declared(p, lesson)[1].state, "tenuta");
        QCOMPARE(review::drop(p, lesson, all[0].hash), QString());                      // via l'ambiente con il suo marcatore
        QCOMPARE(files::read(p.path(lesson)), "\\section{S}\nIl modulo è $|z|$.\nFine.\n\\begin{correction}\n    Era sbagliato.\n\\end{correction}\n");
        QCOMPARE(review::declared(p, lesson).last().state, "tolta");
        QCOMPARE(review::counts(p, lesson).declared, 0);
        QCOMPARE(review::restore(p, lesson, all[0].hash), QString());
        QCOMPARE(files::read(p.path(lesson)), tex);
        review::keepAll(p, lesson);
        QCOMPARE(review::counts(p, lesson).declared, 0);
    }

    // Errori e righe fuori misura: il modello corregge l'errore e riscrive il paragrafo; resta solo ciò che, compilato, migliora.
    void fixLesson()
    {
        const QStringList lines{"\\begin{theorem}", "Uno.", "Due,", "tre.", "", "% @L1.2", "Quattro.", "\\[", "x", "\\]"};
        QCOMPARE(fix::paragraph(lines, 3, 3), (std::pair<int, int>{2, 4}));
        QCOMPARE(fix::paragraph(lines, 1, 2), (std::pair<int, int>{2, 4}));            // l'avviso parte da una riga di struttura
        QCOMPARE(fix::paragraph(lines, 7, 7), (std::pair<int, int>{7, 7}));
        QCOMPARE(fix::paragraph(lines, 8, 10), (std::pair<int, int>{0, 0}));           // una formula non è un paragrafo di testo
        if (!hasLatexmk())
            QSKIP("latexmk non c'è");

        QTemporaryDir dir;
        const QString root = dir.filePath("prova");
        QCOMPARE(Project::create(root), QString());
        const Project p(root);
        QString main = files::read(p.path("main.tex"));
        files::write(p.path("main.tex"), main.replace("\\end{document}", "\\input{src/src1.tex}\n\\end{document}"));
        const QString wide = "Unarigachenonsipuospezzareinnessunmodoperchenonhaspazienemmenounoequindiescedalmargine\\_destro\\_della\\_pagina\\_di\\_sicuro.";
        files::write(p.path("src/src1.tex"), "\\section{S}\n% @L1.1\nTesto \\nonesiste qui.\n\n% @L1.2\n" + wide + "\n");
        auto reply = [&](const char *name, const QJsonArray &answer) {
            const QJsonObject line{{"type", "result"}, {"result", QString::fromUtf8(QJsonDocument(answer).toJson())}, {"total_cost_usd", 0.1}};
            files::writeBytes(dir.filePath(name), QJsonDocument(line).toJson(QJsonDocument::Compact) + '\n');
        };
        reply("errori.json", {QJsonObject{{"old", "\\nonesiste "}, {"new", ""}}, QJsonObject{{"old", "% @L1.1"}, {"new", ""}}});
        reply("righe.json", {QJsonObject{{"card", 1}, {"old", wide}, {"new", QJsonArray{wide + " " + wide, "Una riga che ci sta."}}}});
        files::write(dir.filePath("claude"), QString("#!/bin/sh\ncat >/dev/null\nfor a; do case \"$a\" in *'You fix LaTeX compilation errors'*) cat '%1/errori.json'; exit 0;;\n"
                                                     "  *'You remove Overfull'*) cat '%1/righe.json'; exit 0;; esac; done\n").arg(dir.path()));
        QFile(dir.filePath("claude")).setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qputenv("TEXFIX_CLAUDE", dir.filePath("claude").toUtf8());

        JobControl control;
        Model model("veloce", root, &control);
        const fix::Result r = fix::lesson(p, Settings(), Style::classic(), model, "src/src1.tex", &control);
        qunsetenv("TEXFIX_CLAUDE");
        QCOMPARE(r.failure, QString());
        QCOMPARE(r.errorsBefore, 1);
        QCOMPARE(r.errorsAfter, 0);
        QVERIFY(r.rowsBefore >= 1);
        QCOMPARE(r.rowsAfter, 0);
        QCOMPARE(r.byModel, r.rowsBefore);
        // i marcatori restano, la variante che peggiorava non è rimasta
        QCOMPARE(files::read(p.path("src/src1.tex")), "\\section{S}\n% @L1.1\nTesto qui.\n\n% @L1.2\nUna riga che ci sta.\n");
        QCOMPARE(model.calls().size(), 2);
    }

    // La resa compilata dei pezzi per i pannelli Rivedi: la prosa con «ω0» e «V_CB», un tratto di formula, e un
    // pezzo che non compila resta senza resa (e non si riprova).
    void snippetRender()
    {
        QCOMPARE(snippets::prose("Vale ω0 o V_CB al 50%? Con $x_1$."), "Vale $\\omega_{0}$ o $V_{CB}$ al 50\\%? Con $x_1$.");
        QCOMPARE(snippets::prose("un $ solo"), "un  solo");
        if (!hasLatexmk())
            QSKIP("latexmk non c'è");
        QTemporaryDir dir;
        const QString root = dir.filePath("prova");
        QCOMPARE(Project::create(root), QString());
        const Project p(root);
        const QStringList texts{"f(x) = \\ell", "\\begin{theorem}Vero.\\end{theorem}", "\\nonesiste{"};
        const QStringList done = snippets::render(p, texts, snippets::Kind::Latex, "202020");
        QCOMPARE(done.size(), 2);
        QVERIFY(QFileInfo::exists(snippets::pathOf(p, texts[0], snippets::Kind::Latex, "202020")));
        QVERIFY(!QFileInfo::exists(snippets::pathOf(p, texts[2], snippets::Kind::Latex, "202020")));
        QImage formula(snippets::pathOf(p, texts[0], snippets::Kind::Latex, "202020"));
        QVERIFY(formula.width() > 20 && formula.width() < 400);                        // ritagliata a ciò che è scritto
        QVERIFY(formula.hasAlphaChannel());
        QCOMPARE(snippets::render(p, texts, snippets::Kind::Latex, "202020").size(), 0);   // niente da rifare
    }

    // Le versioni delle fonti: un file identico a una fonte usata non si aggiunge; uno diverso con lo stesso nome prende
    // il suo posto, la vecchia va in «precedenti» con la data, e una copia va fra le fonti da convertire.
    void sourceVersions()
    {
        QTemporaryDir dir;
        const QString root = dir.filePath("prova");
        QCOMPARE(Project::create(root), QString());
        const Project p(root);
        QDir().mkpath(p.path("fonti/usate/lezione 1"));
        files::write(p.path("fonti/usate/lezione 1/notes_LabSS_1.pdf"), "vecchia");
        files::write(dir.filePath("notes_LabSS (1).pdf"), "vecchia");
        QCOMPARE(files::bare("notes_LabSS (1).pdf"), files::bare("notes_LabSS_1.pdf"));
        newlesson::Version v = newlesson::versionOf(p, dir.filePath("notes_LabSS (1).pdf"));
        QCOMPARE(v.used, "fonti/usate/lezione 1/notes_LabSS_1.pdf");
        QVERIFY(v.identical);
        files::write(dir.filePath("notes_LabSS (1).pdf"), "nuova");
        v = newlesson::versionOf(p, dir.filePath("notes_LabSS (1).pdf"));
        QVERIFY(!v.identical);
        QVERIFY(newlesson::versionOf(p, dir.filePath("altro.pdf")).used.isEmpty());
        const QString copy = newlesson::replaceVersion(p, v.used, dir.filePath("notes_LabSS (1).pdf"));
        QCOMPARE(copy, "fonti/coda/notes_LabSS_1.pdf");
        QCOMPARE(files::read(p.path(copy)), "nuova");
        QCOMPARE(files::read(p.path(v.used)), "nuova");
        const QStringList old = QDir(p.path("fonti/usate/lezione 1/precedenti")).entryList(QDir::Files);
        QCOMPARE(old.size(), 1);
        QVERIFY(old[0].endsWith(" notes_LabSS_1.pdf"));
        QCOMPARE(files::read(p.path("fonti/usate/lezione 1/precedenti/" + old[0])), "vecchia");
        QVERIFY(newlesson::fuseMessage("src/src2.tex", 2, "", {"a.pdf", "b.pdf"}, {"b.pdf"}).contains("Fonti importanti: b.pdf"));
    }

    // Le figure di un .tex di un altro autore con «Tieni»: il codice resta; un disegno più largo della colonna si
    // restringe quanto basta, uno che ci sta non si tocca.
    void authoredFigures()
    {
        if (!hasLatexmk())
            QSKIP("latexmk non c'è");
        QTemporaryDir dir;
        const QString root = dir.filePath("prova");
        QCOMPARE(Project::create(root), QString());
        const Project p(root);
        const QString small = "\\begin{center}\n\\begin{tikzpicture}\n\\draw (0,0) -- (2,1);\n\\end{tikzpicture}\n\\end{center}\n";
        const QString noisy = "\\begin{center}\n\\begin{tikzpicture}\n% un commento dell'autore\n\\draw (0,0) -- (1,1);   % la diagonale\n\n\\end{tikzpicture}\n\\end{center}\n";
        const QString wide = "\\begin{center}\n\\begin{tikzpicture}\n\\draw (0,0) -- (30,1);\n\\end{tikzpicture}\n\\end{center}\n";
        files::write(p.path("src/src1.tex"), "\\section{S}\nTesto.\n" + small + "Altro.\n" + wide + "Ancora.\n" + noisy + "Fine.\n");
        JobControl control;
        Model model("veloce", root, &control);
        const figures::Authored a = figures::authored(p, Settings(), Style::classic(), model, "src/src1.tex", "autore.tex");
        QCOMPARE(a.kept, 2);
        QCOMPARE(a.fitted, 1);
        QCOMPARE(a.tidied, 1);
        QVERIFY(files::read(p.path("src/src1.tex")).contains("\\begin{tikzpicture}\n\\draw (0,0) -- (1,1);\n\\end{tikzpicture}"));
        QCOMPARE(a.failed, 0);
        const QString text = files::read(p.path("src/src1.tex"));
        QVERIFY(text.contains(small));
        QVERIFY(text.contains("\\begin{center}\n\\resizebox{\\linewidth}{!}{%\n\\begin{tikzpicture}\n\\draw (0,0) -- (30,1);\n\\end{tikzpicture}%\n}\n\\end{center}"));
        QCOMPARE(figures::authored(p, Settings(), Style::classic(), model, "src/src1.tex", "autore.tex").fitted, 0);   // di nuovo: niente da fare
    }

    // Il cambio di stile oltre gli ambienti: ciò che non ha un corrispondente è una domanda; una macro dello stile di
    // prima che il nuovo non ha cambia nome (stesso significato) o la sua definizione passa in texfix.sty; la
    // descrizione segue il .sty quando cambia; la libreria tiene gli stili dell'utente.
    void styleQuestionsAndMacros()
    {
        QTemporaryDir dir;
        const QString root = dir.filePath("prova");
        QCOMPARE(Project::create(root), QString());
        const Project p(root);
        QString main = files::read(p.path("main.tex"));
        files::write(p.path("main.tex"), main.replace("\\end{document}", "\\input{src/src1.tex}\n\\end{document}"));
        const QString sty = "\\ProvidesPackage{mio}\n\\RequirePackage{amsthm,amssymb}\n\\newtheorem{teorema}{Teorema}\n"
                            "\\newenvironment{nota}{\\par\\textit{Nota.}}{\\par}\n\\newcommand{\\R}{\\mathbb{R}}\n"
                            "\\newcommand{\\norma}[1]{\\left\\|\n  #1 \\right\\|}\n";
        files::write(dir.filePath("mio.sty"), sty);
        const styleswitch::Imported in = styleswitch::import(p, dir.filePath("mio.sty"));
        QCOMPARE(in.error, QString());
        Style mio = in.style;
        for (Style::Environment &env : mio.environments)
            env.role = env.name == "teorema" ? "teorema" : "attenzione";
        for (Style::Macro &macro : mio.macros)
            macro.meaning = macro.name == "\\R" ? "i numeri reali" : "la norma";
        QVERIFY(mio.save(p.path("mio.stile.json")));
        QCOMPARE(styleswitch::apply(p, "mio").error, QString());
        files::write(p.path("src/src1.tex"), "\\section{S}\n\\begin{teorema}\nIn $\\R$ vale $\\norma{x} \\ge 0$.\n\\end{teorema}\n\\begin{nota}\nOcchio.\n\\end{nota}\n");

        // tornare a nessuno stile: «nota» (avvertenza) non ha corrispondente nel classico
        const QList<styleswitch::Orphan> orphans = styleswitch::orphans(p, {});
        QCOMPARE(orphans.size(), 1);
        QCOMPARE(orphans[0].name, "nota");
        QVERIFY(orphans[0].options.contains("remark"));
        const styleswitch::Outcome o = styleswitch::apply(p, {}, {{"nota", "remark"}});
        QCOMPARE(o.error, QString());
        QCOMPARE(o.carried, (QStringList{"\\R", "\\norma"}));
        const QString text = files::read(p.path("src/src1.tex"));
        QVERIFY(text.contains("\\begin{theorem}") && text.contains("\\begin{remark}") && text.contains("\\norma{x}"));
        const QString package = files::read(p.path("texfix.sty"));
        QVERIFY(package.contains("\\ifdefined\\R\\else \\newcommand{\\R}{\\mathbb{R}}\\fi"));
        QVERIFY(package.contains("\\ifdefined\\norma\\else \\newcommand{\\norma}[1]{\\left\\|\n  #1 \\right\\|}\\fi"));
        if (hasLatexmk())
            QVERIFY(!Compiler::compileSync(p, {}).hasErrors());                         // il documento compila ancora

        // di nuovo allo stile: le macro tornano sue, niente resta portato
        QCOMPARE(styleswitch::apply(p, "mio").error, QString());
        QVERIFY(!files::read(p.path("texfix.sty")).contains("ifdefined\\R"));

        // lo stile cambia sul disco: un ambiente nuovo entra nella descrizione, ed è una domanda
        files::write(p.path("mio.sty"), sty + "\\newenvironment{esempietto}{\\par}{\\par}\n");
        QCOMPARE(styleswitch::refresh(p), QStringList{"esempietto"});
        const Style now = styleswitch::current(p);
        QVERIFY(now.environment("esempietto"));
        QCOMPARE(now.environment("teorema")->role, "teorema");                         // ciò che c'era resta com'era
        QCOMPARE(styleswitch::refresh(p), QStringList());

        // la libreria dell'utente
        styleswitch::setLibraryHome(dir.filePath("libreria"));
        QVERIFY(!styleswitch::library().contains("mio"));
        QCOMPARE(styleswitch::addToLibrary(p.path("mio.sty")), QString());
        QVERIFY(styleswitch::library().contains("mio") && styleswitch::library().contains("styles"));
        QVERIFY(!styleswitch::addToLibrary(dir.filePath("styles.sty")).isEmpty());      // non è un file, e il nome è di Texfix
        const QString other = dir.filePath("altro");
        QCOMPARE(Project::create(other), QString());
        const styleswitch::Imported lib = styleswitch::importFromLibrary(Project(other), "mio");
        QCOMPARE(lib.error, QString());
        QCOMPARE(lib.style.environment("teorema")->role, "teorema");                   // con la sua descrizione
        QVERIFY(styleswitch::removeFromLibrary("mio"));
        QVERIFY(!styleswitch::removeFromLibrary("styles"));
        styleswitch::setLibraryHome({});
    }

    // Una formula in display troppo larga si spezza senza modello: a ogni segno di relazione, o a un + fuori dalle parentesi.
    void splitWideDisplay()
    {
        QCOMPARE(fix::splitDisplay(" a = b + c = \\frac{d}{e} \\le f. "),
                 "\\begin{align*}\n    a &= b + c \\\\\n    &= \\frac{d}{e} \\\\\n    &\\le f.\n\\end{align*}");
        QCOMPARE(fix::splitDisplay("f(x) = a_1 x + (b - c) x^{2} + d"),
                 "\\begin{multline*}\n    f(x) = a_1 x \\\\\n    + (b - c) x^{2} + d\n\\end{multline*}");
        QCOMPARE(fix::splitDisplay("\\left( a = b \\right)"), QString());              // niente fuori dalle parentesi
        QCOMPARE(fix::splitDisplay("\\begin{aligned} a &= b \\\\ &= c \\end{aligned}"), QString());   // ha già i suoi a capo
        if (!hasLatexmk())
            QSKIP("latexmk non c'è");
        QTemporaryDir dir;
        const QString root = dir.filePath("prova");
        QCOMPARE(Project::create(root), QString());
        const Project p(root);
        QString main = files::read(p.path("main.tex"));
        files::write(p.path("main.tex"), main.replace("\\end{document}", "\\input{src/src1.tex}\n\\end{document}"));
        QString sum = "S";
        for (int i = 1; i <= 3; ++i)
            sum += QStringLiteral(" = a_{%1} + b_{%1} + c_{%1} + d_{%1} + e_{%1} + f_{%1} + g_{%1} + h_{%1}").arg(i);
        files::write(p.path("src/src1.tex"), "\\section{S}\nVale la catena\n\\[\n    " + sum + ".\n\\]\nFine.\n");
        JobControl control;
        qputenv("TEXFIX_CLAUDE", "/usr/bin/false");                                     // il modello non serve
        Model model("veloce", root, &control);
        const fix::Result r = fix::lesson(p, Settings(), Style::classic(), model, "src/src1.tex", &control);
        qunsetenv("TEXFIX_CLAUDE");
        QVERIFY(r.rowsBefore >= 1);
        QCOMPARE(r.rowsAfter, 0);
        QCOMPARE(r.byMoves, 1);
        QVERIFY(files::read(p.path("src/src1.tex")).contains("\\begin{align*}\n    S &= a_{1}"));
    }

    // L'impaginazione sistema anche una formula rimasta in fondo senza la frase che la commenta (passa oltre con il suo
    // paragrafo) e un elenco che lascia una voce sola dall'altra parte (la penultima la raggiunge): un a capo che, lasciato
    // com'era, se ne va senza lasciare traccia.
    void layoutFormulaAndList()
    {
        if (!hasLatexmk())
            QSKIP("latexmk non c'è");
        QTemporaryDir dir;
        const QString root = dir.filePath("prova");
        QCOMPARE(Project::create(root), QString());
        const Project p(root);
        QString main = files::read(p.path("main.tex"));
        files::write(p.path("main.tex"), main.replace("\\end{document}", "\\input{src/src1.tex}\n\\end{document}"));
        const Style style = Style::classic();
        const blocks::Units units{style.boxes(), {"center", "figure"}};
        auto found = [&](const QString &tail, const QString &what) {
            for (int filler = 30; filler <= 50; ++filler) {
                QString tex = "\\section{S}\n";
                for (int i = 0; i < filler; ++i)
                    tex += QStringLiteral("Riga %1 di testo.\n\n").arg(i + 1);
                const QString marked = blocks::mark(tex + tail, 1, units);
                files::write(p.path("src/src1.tex"), marked);
                QFile::remove(p.path("salvataggi/impaginazione.json"));
                layout::lesson(p, style, "src/src1.tex");
                for (const layout::Move &m : layout::moves(p, "src/src1.tex"))
                    if (m.state == "fatta" && m.what.startsWith(what)) {
                        const QString moved = files::read(p.path("src/src1.tex"));
                        if (moved.count("% impaginazione") != 1 || !layout::decide(p, "src/src1.tex", m.block, "lasciata").isEmpty())
                            return false;
                        if (files::read(p.path("src/src1.tex")) != marked)
                            return false;                          // lasciata com'era: la lezione torna uguale
                        if (!layout::decide(p, "src/src1.tex", m.block, "accettata").isEmpty())
                            return false;
                        return files::read(p.path("src/src1.tex")) == moved;   // e accettata di nuovo torna la mossa
                    }
            }
            return false;
        };
        QVERIFY(found("Vale la relazione\n\\[\n    a = b\n\\]\ndove $a$ e $b$ sono quelli di prima, e questo commento è lungo abbastanza da occupare più di una riga del "
                      "documento, anzi due righe intere, così che la pagina possa finire subito dopo la formula.\n",
                      "Una formula restava in fondo"));
        QVERIFY(found("\\begin{itemize}\n\\item Prima voce.\n\\item Seconda voce.\n\\item Terza voce.\n\\item Quarta voce.\n\\end{itemize}\nFine.\n",
                      "Un elenco lasciava"));
    }

    // Le formule in display degli appunti veri (i casi di riferimento della vecchia app): spezzarne una non ne cambia
    // mai il contenuto, e una buona parte si spezza.
    void splitDisplayCorpus()
    {
        const QJsonArray cases = QJsonDocument::fromJson(files::read(QStringLiteral(TEXFIX_SOURCE_DIR "/tests/dati/display.json")).toUtf8()).array();
        QVERIFY(cases.size() > 50);
        static const QRegularExpression wrapper(R"(\\(?:begin|end)\{(?:align|multline)\*\}|\\\\|&|\s)");
        int split = 0;
        for (const QJsonValue &v : cases) {
            const QString display = v.toString().trimmed();
            if (!display.startsWith("\\[") || !display.endsWith("\\]"))
                continue;
            const QString content = display.mid(2, display.size() - 4);
            const QString out = fix::splitDisplay(content);
            if (out.isEmpty())
                continue;
            ++split;
            QVERIFY2(QString(out).remove(wrapper) == QString(content).remove(wrapper), qPrintable(content + "\n→\n" + out));
            QVERIFY(out.startsWith("\\begin{align*}") || out.startsWith("\\begin{multline*}"));
        }
        QVERIFY2(split > cases.size() / 4, qPrintable(QString::number(split)));
    }

    // La misura di una figura: un disegno che non ci stava e lasciava un grande vuoto si rimpicciolisce quanto basta per
    // stare con ciò che lo precede; «lascia com'era» toglie la mossa, «accetta» la rimette.
    void layoutShrinkFigure()
    {
        if (!hasLatexmk())
            QSKIP("latexmk non c'è");
        QTemporaryDir dir;
        const QString root = dir.filePath("prova");
        QCOMPARE(Project::create(root), QString());
        const Project p(root);
        QString main = files::read(p.path("main.tex"));
        files::write(p.path("main.tex"), main.replace("\\end{document}", "\\input{src/src1.tex}\n\\end{document}"));
        const Style style = Style::classic();
        QString tex = "\\section{S}\n";
        for (int i = 0; i < 22; ++i)
            tex += QStringLiteral("Riga %1 di testo.\n\n").arg(i + 1);
        tex += "\\begin{center}\n\\begin{tikzpicture}\n\\draw (0,0) rectangle (4,13);\n\\end{tikzpicture}\n\\end{center}\nFine.\n";
        files::write(p.path("src/src1.tex"), blocks::mark(tex, 1, {style.boxes(), {"center", "figure"}}));
        const QString before = files::read(p.path("src/src1.tex"));
        const layout::Result r = layout::lesson(p, style, "src/src1.tex");
        QCOMPARE(r.failure, QString());
        QCOMPARE(r.done, 1);
        QList<layout::Move> moves = layout::moves(p, "src/src1.tex");
        QCOMPARE(moves.size(), 1);
        QCOMPARE(moves[0].state, "fatta");
        QVERIFY2(moves[0].what.contains("rimpicciolita al"), qPrintable(moves[0].what));
        const QString after = files::read(p.path("src/src1.tex"));
        QVERIFY(after.contains("{% impaginazione\n\\begin{tikzpicture}") && after.contains("\\end{tikzpicture}\n}% impaginazione\n\\end{center}"));
        QVERIFY(!Compiler::compileSync(p, "src/src1.tex").hasErrors());
        QCOMPARE(layout::lesson(p, style, "src/src1.tex").found, 0);                   // di nuovo: niente da fare
        QCOMPARE(layout::decide(p, "src/src1.tex", moves[0].block, "lasciata"), QString());
        QCOMPARE(files::read(p.path("src/src1.tex")), before);
        QCOMPARE(layout::decide(p, "src/src1.tex", moves[0].block, "accettata"), QString());
        QCOMPARE(files::read(p.path("src/src1.tex")), after);
    }

    // Lo spazio: un teorema che finisce con una riga sola sulla pagina dopo resta intero allungando la pagina di poco.
    void layoutEnlarge()
    {
        if (!hasLatexmk())
            QSKIP("latexmk non c'è");
        QTemporaryDir dir;
        const QString root = dir.filePath("prova");
        QCOMPARE(Project::create(root), QString());
        const Project p(root);
        QString main = files::read(p.path("main.tex"));
        files::write(p.path("main.tex"), main.replace("\\end{document}", "\\input{src/src1.tex}\n\\end{document}"));
        const Style style = Style::classic();
        bool seen = false;
        for (int pad = 0; pad <= 240 && !seen; pad += 6) {
            QString tex = QStringLiteral("\\section{S}\n\\vspace*{%1pt}\n\n").arg(pad);
            for (int i = 0; i < 30; ++i)
                tex += QStringLiteral("Riga %1 di testo.\n\n").arg(i + 1);
            tex += "\\begin{theorem}[Corto]\n";
            for (int i = 0; i < 4; ++i)
                tex += QStringLiteral("    Riga %1 del teorema.\n\n").arg(i + 1);
            tex += "\\end{theorem}\nFine.\n";
            files::write(p.path("src/src1.tex"), blocks::mark(tex, 1, {style.boxes(), {"center", "figure"}}));
            const QString before = files::read(p.path("src/src1.tex"));
            QFile::remove(p.path("salvataggi/impaginazione.json"));
            layout::lesson(p, style, "src/src1.tex");
            const QList<layout::Move> moves = layout::moves(p, "src/src1.tex");
            if (moves.size() != 1 || !moves[0].sign.contains("enlargethispage"))
                continue;
            seen = true;
            QCOMPARE(moves[0].state, "fatta");
            QVERIFY(files::read(p.path("src/src1.tex")).contains("\\enlargethispage{3\\baselineskip} % impaginazione\n% @L1."));
            for (const layout::Placed &placed : layout::measure(p, style, "src/src1.tex"))
                QVERIFY(placed.kind != "theorem" || !placed.split());
            QCOMPARE(layout::decide(p, "src/src1.tex", moves[0].block, "lasciata"), QString());
            QCOMPARE(files::read(p.path("src/src1.tex")), before);
            QCOMPARE(layout::decide(p, "src/src1.tex", moves[0].block, "accettata"), QString());
            QVERIFY(files::read(p.path("src/src1.tex")).contains("\\enlargethispage"));
        }
        QVERIFY2(seen, "nessun caso con poche righe dall'altra parte");
    }

    // L'impaginazione oltre i riquadri spezzati: una figura che non ci stava ed è passata oltre lascia un grande vuoto.
    // Qui non c'è un disegno da rimpicciolire: resta alla pagina dopo con un a capo, e il vuoto diventa voluto.
    void layoutHole()
    {
        if (!hasLatexmk())
            QSKIP("latexmk non c'è");
        QTemporaryDir dir;
        const QString root = dir.filePath("prova");
        QCOMPARE(Project::create(root), QString());
        const Project p(root);
        QString main = files::read(p.path("main.tex"));
        files::write(p.path("main.tex"), main.replace("\\end{document}", "\\input{src/src1.tex}\n\\end{document}"));
        const Style style = Style::classic();
        QString tex = "\\section{S}\n";
        for (int i = 0; i < 22; ++i)
            tex += QStringLiteral("Riga %1 di testo.\n\n").arg(i + 1);
        tex += "\\begin{center}\n\\rule{2cm}{13cm}\n\\end{center}\nFine.\n";
        files::write(p.path("src/src1.tex"), blocks::mark(tex, 1, {style.boxes(), {"center", "figure"}}));
        const QString text = files::read(p.path("src/src1.tex"));
        const layout::Result r = layout::lesson(p, style, "src/src1.tex");
        QCOMPARE(r.failure, QString());
        QCOMPARE(r.found, 1);
        QCOMPARE(r.done, 1);
        QVERIFY(text.count("\\newpage % impaginazione") == 0);
        QCOMPARE(files::read(p.path("src/src1.tex")).count("\\newpage % impaginazione\n% @L1."), 1);   // l'a capo, prima della figura
        const QList<layout::Move> moves = layout::moves(p, "src/src1.tex");
        QCOMPARE(moves.size(), 1);
        QCOMPARE(moves[0].state, "fatta");
        QVERIFY(moves[0].same);                                                        // la pagina è rimasta com'era
        QVERIFY2(moves[0].what.startsWith("La figura non ci stava"), qPrintable(moves[0].what));
        QVERIFY(moves[0].hole > layout::HOLE_OK);
        QCOMPARE(layout::toReview(p, "src/src1.tex"), 1);
        QCOMPARE(layout::decide(p, "src/src1.tex", moves[0].block, "a_mano"), QString());
        QCOMPARE(layout::toReview(p, "src/src1.tex"), 1);                              // la sistema l'utente: resta da rivedere
        QCOMPARE(layout::decide(p, "src/src1.tex", moves[0].block, "risolta"), QString());
        QCOMPARE(layout::toReview(p, "src/src1.tex"), 0);
        QCOMPARE(layout::lesson(p, style, "src/src1.tex").found, 0);                   // la decisione resta
        QCOMPARE(layout::moves(p, "src/src1.tex").size(), 1);
    }

    // L'impaginazione: un teorema a cavallo di due pagine va a pagina nuova se il vuoto è piccolo, altrimenti la mossa
    // resta proposta; le decisioni si cambiano e restano; ciò che non è cambiato non si rifà.
    void layoutLesson()
    {
        if (!hasLatexmk())
            QSKIP("latexmk non c'è");
        QTemporaryDir dir;
        const QString root = dir.filePath("prova");
        QCOMPARE(Project::create(root), QString());
        const Project p(root);
        QString main = files::read(p.path("main.tex"));
        files::write(p.path("main.tex"), main.replace("\\end{document}", "\\input{src/src1.tex}\n\\end{document}"));
        const Style style = Style::classic();
        const blocks::Units units{style.boxes(), {"center", "figure"}};
        // righe di testo fino quasi in fondo alla pagina, poi un teorema di otto righe: `filler` decide dove cade
        auto write = [&](int filler) {
            QString tex = "\\section{S}\n";
            for (int i = 0; i < filler; ++i)
                tex += QStringLiteral("Riga %1 di testo.\n\n").arg(i + 1);
            tex += "\\begin{theorem}[Lungo]\n";
            for (int i = 0; i < 8; ++i)
                tex += QStringLiteral("    Riga %1 del teorema.\n\n").arg(i + 1);
            tex += "\\end{theorem}\nFine.\n";
            files::write(p.path("src/src1.tex"), blocks::mark(tex, 1, units));
        };
        auto theorem = [&] {
            for (const layout::Placed &placed : layout::measure(p, style, "src/src1.tex"))
                if (placed.kind == "theorem")
                    return placed;
            return layout::Placed();
        };
        // si cerca dove il teorema cade a cavallo con poco vuoto sopra, e dove con molto
        int near = 0, far = 0;
        for (int filler = 20; filler <= 46 && !(near && far); ++filler) {
            write(filler);
            const layout::Placed t = theorem();
            if (t.split() && 1 - t.top <= 0.08 && !near)             // poco vuoto sopra, e più di qualche riga dall'altra parte
                near = filler;
        }
        QVERIFY2(near, "nessun caso a cavallo trovato");

        write(near);
        QVERIFY(layout::needed(p, "src/src1.tex"));
        layout::Result r = layout::lesson(p, style, "src/src1.tex");
        QCOMPARE(r.failure, QString());
        QCOMPARE(r.found, 1);
        QCOMPARE(r.done, 1);
        QVERIFY(!theorem().split());
        const QString text = files::read(p.path("src/src1.tex"));
        QVERIFY(text.contains("\\newpage % impaginazione\n% @L1.2\n\\begin{theorem}[Lungo]"));   // prima del marcatore
        QVERIFY(!layout::needed(p, "src/src1.tex"));
        QList<layout::Move> moves = layout::moves(p, "src/src1.tex");
        QCOMPARE(moves.size(), 1);
        QCOMPARE(moves[0].state, "fatta");
        QVERIFY(moves[0].what.startsWith("Teorema «Lungo» cadeva a cavallo"));
        QCOMPARE(layout::toReview(p, "src/src1.tex"), 1);

        r = layout::lesson(p, style, "src/src1.tex");                                  // di nuovo: niente da fare
        QCOMPARE(r.found, 0);
        QCOMPARE(files::read(p.path("src/src1.tex")), text);

        QCOMPARE(layout::decide(p, "src/src1.tex", "L1.2", "lasciata"), QString());     // lascia com'era: l'a capo se ne va
        QVERIFY(!files::read(p.path("src/src1.tex")).contains("impaginazione"));
        QVERIFY(theorem().split());
        QCOMPARE(layout::toReview(p, "src/src1.tex"), 0);
        r = layout::lesson(p, style, "src/src1.tex");                                  // la decisione resta: non ci riprova
        QCOMPARE(r.found, 0);
        QCOMPARE(layout::decide(p, "src/src1.tex", "L1.2", "accettata"), QString());    // si cambia idea
        QCOMPARE(files::read(p.path("src/src1.tex")), text);
        QVERIFY(!layout::needed(p, "src/src1.tex"));
        files::write(p.path("src/src1.tex"), text + "Altro.\n");                        // una modifica a mano: è da rifare
        QVERIFY(layout::needed(p, "src/src1.tex"));

        // molto vuoto: la mossa non si applica da sola, resta proposta
        write(near - 12);
        if (theorem().split()) {
            QSKIP("il caso con molto vuoto non è a cavallo con queste misure");
        }
        Q_UNUSED(far)
    }

    // Gli strumenti dell'agente: il protocollo a righe e le chiamate, eseguite dal programma sul progetto.
    void agentTools()
    {
        QTemporaryDir dir;
        const QString root = dir.filePath("prova");
        QCOMPARE(Project::create(root), QString());
        const Project p(root);
        QString main = files::read(p.path("main.tex"));
        files::write(p.path("main.tex"), main.replace("\\end{document}", "\\input{src/src1.tex}\n\\end{document}"));
        files::write(p.path("src/src1.tex"), "\\lezione{1}{01/10/2026}\n\\section{Limiti}\nTesto \\nonesiste.\n");
        Settings s = Settings::load(p);
        s.setDone("src/src1.tex", true);
        s.save(p);
        const Style style = Style::classic();
        auto ask = [&](const QJsonObject &request) { return QJsonDocument::fromJson(agent::handle(p, style, QJsonDocument(request).toJson(QJsonDocument::Compact))).object(); };

        QVERIFY(agent::handle(p, style, R"({"jsonrpc":"2.0","method":"notifications/initialized"})").isEmpty());
        QCOMPARE(ask({{"id", 1}, {"method", "initialize"}, {"params", QJsonObject{{"protocolVersion", "2025-06-18"}}}})
                     .value("result").toObject().value("protocolVersion").toString(), "2025-06-18");
        const QJsonArray list = ask({{"id", 2}, {"method", "tools/list"}}).value("result").toObject().value("tools").toArray();
        QCOMPARE(list.size(), 9);
        QCOMPARE(list[0].toObject().value("name").toString(), "stato_progetto");
        QVERIFY(ask({{"id", 3}, {"method", "boh"}}).contains("error"));

        auto callTool = [&](const QString &name, const QJsonObject &arguments) {
            return ask({{"id", "x"}, {"method", "tools/call"}, {"params", QJsonObject{{"name", name}, {"arguments", arguments}}}}).value("result").toObject();
        };
        const QString state = callTool("stato_progetto", {}).value("content").toArray().first().toObject().value("text").toString();
        QVERIFY2(state.contains("- src/src1.tex: lezione 1 del 01/10/2026, «Limiti» (FATTA"), qPrintable(state));
        QVERIFY(callTool("compila_lezione", {{"lezione", "src/altro.tex"}}).value("isError").toBool());
        QVERIFY(callTool("non_esiste", {}).value("isError").toBool());
        if (hasLatexmk()) {
            const QJsonObject built = callTool("compila_lezione", {{"lezione", "src/src1.tex"}});
            QVERIFY(!built.value("isError").toBool());
            QVERIFY(built.value("content").toArray().first().toObject().value("text").toString().startsWith("- errore, src/src1.tex riga 3: Undefined control sequence"));
        }
        QVERIFY(agent::systemPrompt(s, style).contains("## Come si scrive"));
    }

    // I piani: consegnato dall'agente, sistemato dall'utente, eseguito, archiviato con l'esito.
    void plansFlow()
    {
        QTemporaryDir dir;
        const Project p(dir.path());
        bool error = false;
        const QJsonObject delivered{{"titolo", "Fusione: lezione 3"}, {"riepilogo", "Unisco due fonti."},
                                    {"sezioni", QJsonArray{QJsonObject{{"titolo", "Lezione 3"}, {"voci", QJsonArray{"Tengo la struttura di A", "Aggiungo l'esempio di B"}}}}},
                                    {"domande", QJsonArray{QJsonObject{{"testo", "Quale notazione?"}, {"opzioni", QJsonArray{"di A", "di B"}}}}}};
        QVERIFY(agent::call(p, Style::classic(), "consegna_piano", {{"titolo", "Vuoto"}}, nullptr, &error).contains("almeno una sezione"));
        QVERIFY(error);
        const QString answer = agent::call(p, Style::classic(), "consegna_piano", delivered, nullptr, &error);
        QVERIFY(!error);
        QCOMPARE(plans::open(p).size(), 1);
        const QString id = plans::open(p).first().id;
        QVERIFY(answer.contains(id));
        QCOMPARE(plans::open(p).first().state, "piano");

        QJsonObject plan = plans::load(p, id);
        QVERIFY(!plans::hasInstructions(plan));
        QJsonArray sections = plan.value("sezioni").toArray();
        QJsonObject section = sections[0].toObject();
        QJsonArray entries = section.value("voci").toArray();
        QJsonObject second = entries[1].toObject();
        second["fare"] = false;
        second["nota"] = "mettilo dopo il teorema";
        entries[1] = second;
        section["voci"] = entries;
        sections[0] = section;
        plan["sezioni"] = sections;
        QVERIFY(plans::save(p, id, plan));
        QVERIFY(plans::hasInstructions(plans::load(p, id)));
        QVERIFY(plans::markdown(plan).contains("- [x] Tengo la struttura di A\n- [ ] Aggiungo l'esempio di B\n  ↳ mettilo dopo il teorema"));
        QVERIFY(plans::reviseMessage(id, plan).contains("lavoro: " + id));
        QVERIFY(plans::startMessage(id, plan).contains("consegna_esito"));

        agent::call(p, Style::classic(), "consegna_esito", {{"lavoro", id}, {"testo", "Fatto: src/src3.tex."}}, nullptr, &error);
        QVERIFY(!error);
        QCOMPARE(plans::open(p).first().state, "esito");
        const QString archived = plans::archive(p, id);
        QVERIFY(QFileInfo(archived).fileName().endsWith(" — Fusione lezione 3.md"));
        QVERIFY(files::read(archived).contains("Fatto: src/src3.tex.\n\n---\n\nIl piano seguito:"));
        QVERIFY(plans::open(p).isEmpty());
    }

    // Nuova lezione: numero e data dai nomi e dal testo delle fonti, la coda, la chiusura di una lezione dell'agente.
    void newLesson()
    {
        QCOMPARE(newlesson::readOne("Lezione_4 del 29-09-2026.pdf", {}).number, 4);
        QCOMPARE(newlesson::readOne("Lezione_4 del 29-09-2026.pdf", {}).date, "29/09/2026");
        QCOMPARE(newlesson::readOne("appunti 2026-10-01.pdf", {}).date, "01/10/2026");
        QCOMPARE(newlesson::readOne("lez12.pdf", {}).number, 12);
        QCOMPARE(newlesson::readOne("note.md", "# Lezione n. 7\ndel 3/10/26").number, 7);
        QCOMPARE(newlesson::readOne("note.md", "# Lezione n. 7\ndel 3/10/26").date, "03/10/2026");
        QCOMPARE(newlesson::readOne("capitolo 2024.pdf", {}).number, 0);
        QCOMPARE(newlesson::readOne("capitolo 2024.pdf", {}).date, QString());

        QTemporaryDir dir, outside;
        const QString root = dir.filePath("prova");
        QCOMPARE(Project::create(root), QString());
        const Project p(root);
        QCOMPARE(newlesson::nextNumber(p), 1);
        files::write(outside.filePath("Lezione_3.md"), "a");
        files::write(outside.filePath("lez_5.md"), "b");
        QCOMPARE(newlesson::read({outside.filePath("Lezione_3.md"), outside.filePath("lez_5.md")}).conflict, "Le fonti dicono numeri diversi: 3, 5.");
        files::write(p.path("fonti/coda/Lezione_3.md"), "già in coda");
        QCOMPARE(newlesson::toQueue(p, {outside.filePath("Lezione_3.md"), p.path("fonti/coda/Lezione_3.md")}),
                 (QStringList{"fonti/coda/Lezione_3 (2).md", "fonti/coda/Lezione_3.md"}));

        // l'agente ha scritto una lezione: chiuderla la mette nel documento, marca i blocchi, archivia le fonti
        files::write(p.path("src/src1.tex"), "\\section{S}\n\\lezione{3}{}\n\nTesto.\n\\begin{theorem}\nx\n\\end{theorem}\n");
        bool error = false;
        const QString did = agent::call(p, Style::classic(), "chiudi_lezione", {{"lezione", "src/src1.tex"}, {"fonti", QJsonArray{"fonti/coda/Lezione_3.md"}}}, nullptr, &error);
        QVERIFY2(did.contains("marcatori") && did.contains("aggiunta al documento") && did.contains("Lezione_3.md portata"), qPrintable(did));
        QCOMPARE(p.inputs(), QStringList{"src/src1.tex"});
        QVERIFY(files::read(p.path("src/src1.tex")).contains("% @L3.1\nTesto.\n% @L3.2\n\\begin{theorem}"));
        QVERIFY(QFileInfo::exists(p.path("fonti/usate/lezione 3/Lezione_3.md")));
        QCOMPARE(newlesson::nextNumber(p), 4);
        QVERIFY(agent::call(p, Style::classic(), "chiudi_lezione", {{"lezione", "src/src1.tex"}}, nullptr, &error).contains("già tutto a posto"));
        QVERIFY(newlesson::fuseMessage("src/src2.tex", 4, "02/10/2026", {"fonti/coda/a.pdf", "fonti/coda/b.md"}).contains("\\lezione{4}{02/10/2026}"));
    }

    // Aggiungi materiale: il piano che divide il materiale fra lezioni nuove e da completare; «Avvia» prepara le fonti.
    void sortMaterial()
    {
        QTemporaryDir dir;
        const QString root = dir.filePath("prova");
        QCOMPARE(Project::create(root), QString());
        const Project p(root);
        QString main = files::read(p.path("main.tex"));
        files::write(p.path("main.tex"), main.replace("\\end{document}", "\\input{src/src1.tex}\n\\end{document}"));
        files::write(p.path("src/src1.tex"), "\\section{Limiti}\n\\lezione{1}{01/10/2026}\n\\subsection{Definizione}\n");
        files::write(p.path("fonti/coda/A.md"), "Serie numeriche.\n");
        files::write(p.path("fonti/coda/B.md"), "Ancora limiti.\n");
        const QJsonObject answer{{"groups", QJsonArray{
            QJsonObject{{"dest", "new"}, {"date", "03/10/2026"}, {"title", "Serie"}, {"why", "argomento nuovo"}, {"pieces", QJsonArray{QJsonObject{{"file", "A.md"}, {"from", 1}, {"to", 1}}}}},
            QJsonObject{{"dest", "lesson"}, {"lesson", 1}, {"why", "stesso argomento"}, {"pieces", QJsonArray{QJsonObject{{"file", "B.md"}, {"from", 1}, {"to", 1}}}}}}}};
        const QJsonObject line{{"type", "result"}, {"result", QString::fromUtf8(QJsonDocument(answer).toJson())}, {"total_cost_usd", 0.02}};
        files::writeBytes(dir.filePath("risposta.json"), QJsonDocument(line).toJson(QJsonDocument::Compact) + '\n');
        files::write(dir.filePath("claude"), QString("#!/bin/sh\ncat > '%1/richiesta'\ncat '%1/risposta.json'\n").arg(dir.path()));
        QFile(dir.filePath("claude")).setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qputenv("TEXFIX_CLAUDE", dir.filePath("claude").toUtf8());

        JobControl control;
        sort::Outcome o = sort::plan(p, Settings(), {"fonti/coda/A.md", "fonti/coda/B.md"}, &control);
        QCOMPARE(o.failure, QString());
        QCOMPARE(o.cost, 0.02);
        QVERIFY(files::read(dir.filePath("richiesta")).contains("lecture 1, date 01/10/2026"));
        QJsonObject plan = plans::load(p, o.plan);
        QCOMPARE(plan.value("tipo").toString(), "materiale");
        const QJsonArray sections = plan.value("sezioni").toArray();
        QCOMPARE(sections.size(), 2);
        QCOMPARE(sections[0].toObject().value("titolo").toString(), "Lezione nuova · Serie · lezione del 03/10/2026 — argomento nuovo");
        QCOMPARE(sections[1].toObject().value("titolo").toString(), "Lezione 1 · Limiti: da completare — stesso argomento");
        QCOMPARE(sections[0].toObject().value("voci").toArray().first().toObject().value("testo").toString(), "A.md");

        // «Avvia»: la lezione nuova si converte con numero e data, quella da completare va all'agente
        sort::Prepared prepared = sort::prepare(p, plan);
        QCOMPARE(prepared.failure, QString());
        QCOMPARE(prepared.sources, QStringList{"fonti/coda/A.md"});
        QCOMPARE(prepared.wanted.value("fonti/coda/A.md").number, 2);
        QCOMPARE(prepared.wanted.value("fonti/coda/A.md").date, "03/10/2026");
        QCOMPARE(prepared.agent.size(), 1);
        QVERIFY(prepared.agent.first().startsWith("Integra in `src/src1.tex` queste fonti nuove: fonti/coda/B.md."));

        // un'istruzione sotto una voce: la lezione nuova passa dall'agente, e il piano di prima la porta al modello
        QJsonArray edited = sections;
        QJsonObject first = edited[0].toObject();
        QJsonArray entries = first.value("voci").toArray();
        QJsonObject entry = entries[0].toObject();
        entry["nota"] = "è la lezione del 4 ottobre";
        entries[0] = entry;
        first["voci"] = entries;
        edited[0] = first;
        plan["sezioni"] = edited;
        prepared = sort::prepare(p, plan);
        QVERIFY(prepared.sources.isEmpty());
        QCOMPARE(prepared.agent.size(), 2);
        QVERIFY(prepared.agent.first().contains("Istruzioni dell'utente: A.md: è la lezione del 4 ottobre"));
        QVERIFY(sort::previousPlan(plan).contains("- A.md pages 1-1 — USER: è la lezione del 4 ottobre"));
        o = sort::plan(p, Settings(), {"fonti/coda/A.md", "fonti/coda/B.md"}, &control, sort::previousPlan(plan));
        QVERIFY(files::read(dir.filePath("richiesta")).contains("YOUR PREVIOUS PLAN, WITH THE USER'S INSTRUCTIONS"));
        qunsetenv("TEXFIX_CLAUDE");
    }

    // Version control: modifiche, differenze, commit, cronologia.
    void versionControl()
    {
        QTemporaryDir dir;
        const QString root = dir.filePath("prova");
        QCOMPARE(Project::create(root), QString());
        const Project p(root);
        QVERIFY(git::hasRepository(p));
        tools::run("git", {"config", "user.name", "Prova"}, root);
        tools::run("git", {"config", "user.email", "prova@example.com"}, root);
        QVERIFY(!git::changes(p).isEmpty());                                           // un progetto nuovo: tutto da salvare
        QVERIFY(!git::commit(p, "  ").isEmpty());
        QCOMPARE(git::commit(p, "Progetto nuovo"), QString());
        QVERIFY(git::changes(p).isEmpty());
        files::write(p.path("src/lezione nuova.tex"), "ciao\n");
        files::write(p.path("main.tex"), files::read(p.path("main.tex")) + "% fine\n");
        const QList<git::Change> changes = git::changes(p);
        QCOMPARE(changes.size(), 2);
        QCOMPARE(changes[0].file, "main.tex");
        QCOMPARE(changes[0].state, "modificato");
        QCOMPARE(changes[1].file, "src/lezione nuova.tex");
        QCOMPARE(changes[1].state, "nuovo");
        QVERIFY(git::diff(p, "main.tex").contains("+% fine"));
        QVERIFY(git::diff(p, "src/lezione nuova.tex").contains("+ciao"));
        QCOMPARE(git::commit(p, "Una lezione"), QString());
        const QList<git::Entry> log = git::log(p);
        QCOMPARE(log.size(), 2);
        QCOMPARE(log[0].subject, "Una lezione");
        QVERIFY(git::show(p, log[0].hash).contains("+ciao"));
        // un commit con un file solo: l'altro resta da salvare
        files::write(p.path("src/lezione nuova.tex"), "ciao ciao\n");
        files::write(p.path("main.tex"), files::read(p.path("main.tex")) + "% ancora\n");
        QCOMPARE(git::commit(p, "Solo il documento", {"main.tex"}), QString());
        QCOMPARE(git::changes(p).size(), 1);
        QCOMPARE(git::changes(p)[0].file, "src/lezione nuova.tex");
    }

    // Rinomina, duplica: il documento segue la lezione rinominata.
    void fileOperations()
    {
        QTemporaryDir dir;
        const Project p(dir.path());
        files::write(p.path("main.tex"), "\\begin{document}\n\\input{src/a.tex}\n\\clearpage\n\\input{src/b}\n\\end{document}\n");
        files::write(p.path("src/a.tex"), "a");
        files::write(p.path("src/b.tex"), "b");
        QCOMPARE(fileops::rename(p, "src/b.tex", "serie"), QString());                  // l'estensione resta
        QCOMPARE(p.inputs(), (QStringList{"src/a.tex", "src/serie.tex"}));
        QVERIFY(!fileops::rename(p, "src/a.tex", "serie.tex").isEmpty());               // c'è già
        QVERIFY(!fileops::rename(p, "src/a.tex", "x/y").isEmpty());
        QCOMPARE(fileops::duplicate(p, "src/a.tex"), "src/a (2).tex");
        QCOMPARE(files::read(p.path("src/a (2).tex")), "a");
        QCOMPARE(p.inputs(), (QStringList{"src/a.tex", "src/serie.tex"}));              // la copia non entra nel documento
    }

    // Riorganizza: per argomenti e ritorno a un file per lezione; lo stesso contenuto, i file di prima tornano uguali.
    void reorganizeCourse()
    {
        QTemporaryDir dir;
        const QString root = dir.filePath("prova");
        QCOMPARE(Project::create(root), QString());
        const Project p(root);
        QString main = files::read(p.path("main.tex"));
        files::write(p.path("main.tex"), main.replace("\\end{document}", "\\input{src/src1.tex}\n\\clearpage\n\n\\input{src/src2.tex}\n\\clearpage\n\n\\end{document}"));
        const QString one = "\\section{Limiti}\n\\lezione{1}{01/10/2026}\n\n\\subsection{Definizione}\n% @L1.1 · a.pdf p.1\nTesto uno.\n\\paragraph{Nota.}\nResta dentro.\n\n\\subsection{Serie: primi esempi}\n% @L1.2 · a.pdf p.2\nTesto due.\n";
        const QString two = "\\section{Serie}\n\\lezione{2}{03/10/2026}\n\n\\subsection{Criteri}\n% @L2.1 · b.pdf p.1\nTesto tre.\n\n\\subsection{Limiti notevoli}\n% @L2.2 · b.pdf p.3\nTesto quattro.\n";
        files::write(p.path("src/src1.tex"), one);
        files::write(p.path("src/src2.tex"), two);
        files::write(p.path("salvataggi/dubbi/src2.json"), R"({"dubbi":[{"id":"src2-d1","what":"quattro o quarto?","latex":"Testo quattro.","readings":[{"text":"quattro","latex":"Testo quattro."},{"text":"quarto","latex":"Testo quarto."}]}]})");

        const QList<reorganize::Section> all = reorganize::sections(p);
        QCOMPARE(all.size(), 4);
        QCOMPARE(all[1].id, "src/src1.tex#2");
        QCOMPARE(all[1].title, "Serie: primi esempi");
        QCOMPARE(all[2].lesson, 2);
        QCOMPARE(all[0].lines.size(), 5);                                             // il \paragraph resta nella sua sezione

        QVERIFY(!reorganize::toTopics(p, {{"Limiti", {"src/src1.tex#1"}}}).isEmpty());  // sezioni rimaste fuori: niente si tocca
        QCOMPARE(files::read(p.path("src/src1.tex")), one);
        QCOMPARE(reorganize::toTopics(p, {{"Limiti", {"src/src1.tex#1", "src/src2.tex#2"}}, {"Serie", {"src/src1.tex#2", "src/src2.tex#1"}}}), QString());
        QCOMPARE(p.inputs(), (QStringList{"src/arg1.tex", "src/arg2.tex"}));
        QVERIFY(!QFileInfo::exists(p.path("src/src1.tex")));
        QCOMPARE(files::read(p.path("src/arg1.tex")),
                 "\\section{Limiti}\n\\lezione{1}{01/10/2026}\n\n\\subsection{Definizione}\n% @L1.1 · a.pdf p.1\nTesto uno.\n\\paragraph{Nota.}\nResta dentro.\n"
                 "\\lezione{2}{03/10/2026}\n\n\\subsection{Limiti notevoli}\n% @L2.2 · b.pdf p.3\nTesto quattro.\n");
        QVERIFY(Settings::load(p).byTopic());
        // i dubbi seguono il loro passo nel file per argomento, e lì si decidono
        QCOMPARE(review::doubts(p, "src/arg1.tex").size(), 1);
        QCOMPARE(review::doubts(p, "src/arg2.tex").size(), 0);
        QCOMPARE(review::choose(p, "src/arg1.tex", "src2-d1", 1), QString());
        QVERIFY(files::read(p.path("src/arg1.tex")).contains("Testo quarto."));
        QCOMPARE(review::reopen(p, "src/arg1.tex", "src2-d1", true), QString());
        QCOMPARE(review::counts(p, "src/arg1.tex").doubts, 1);
        QVERIFY(!reorganize::toTopics(p, {}).isEmpty());                                // è già per argomenti

        QCOMPARE(reorganize::toLessons(p), QString());
        QCOMPARE(p.inputs(), (QStringList{"src/src1.tex", "src/src2.tex"}));
        QCOMPARE(files::read(p.path("src/src1.tex")), one);
        QCOMPARE(files::read(p.path("src/src2.tex")), two);
        QVERIFY(!Settings::load(p).byTopic());
        QVERIFY(!QFileInfo::exists(p.path("src/arg1.tex")));
        QVERIFY(files::read(p.path("main.tex")).contains("\\input{src/src1.tex}\n\\clearpage\n\n\\input{src/src2.tex}\n\\clearpage\n\n\\end{document}"));

        // senza modello che risponda bene, la proposta è un argomento per lezione
        qputenv("TEXFIX_CLAUDE", "/usr/bin/false");
        JobControl control;
        const QList<reorganize::Topic> proposal = reorganize::propose(p, Settings(), &control);
        qunsetenv("TEXFIX_CLAUDE");
        QCOMPARE(proposal.size(), 2);
        QCOMPARE(proposal[0].title, "Limiti");
        QCOMPARE(proposal[1].sections, (QStringList{"src/src2.tex#1", "src/src2.tex#2"}));
    }

    // Le fonti .tex: un documento diviso in lezioni per data, una lezione nuova per ciascuna, con le macro dell'autore.
    void texSources()
    {
        const QString doc = "\\documentclass{article}\n\\newcommand{\\RR}{\\mathbb{R}}\n\\usepackage{amsmath}\n\\begin{document}\n"
                            "\\section{Lezione del 22 ottobre 2026}\nSeconda. $\\RR$\n\n\\section{Lezione del 20/10/2026}\nPrima.\nDue righe.\n\\end{document}\n";
        const QList<texsource::Lesson> lessons = texsource::lessons(doc);
        QCOMPARE(lessons.size(), 2);
        QCOMPARE(lessons[0].date, QDate(2026, 10, 20));                                // in ordine di calendario
        QCOMPARE(lessons[0].text, "Prima.\nDue righe.");
        QCOMPARE(lessons[1].heading, "Lezione del 22 ottobre 2026");
        QCOMPARE(texsource::macros(texsource::preamble(doc)), QStringList{"\\newcommand{\\RR}{\\mathbb{R}}"});
        QVERIFY(texsource::workerText(doc, QDate(2026, 10, 22)).endsWith("% \\newcommand{\\RR}{\\mathbb{R}}\n\nSeconda. $\\RR$"));
        QVERIFY(texsource::lessons("\\begin{document}\\section{Introduzione}x\\end{document}").isEmpty());   // non è diviso in lezioni
        QCOMPARE(texsource::parseSpec("fonti/coda/a.tex@2026-10-20"), (std::pair<QString, QDate>{"fonti/coda/a.tex", QDate(2026, 10, 20)}));
        QCOMPARE(texsource::parseSpec("fonti/coda/a@b.tex").first, "fonti/coda/a@b.tex");

        QTemporaryDir dir;
        const QString root = dir.filePath("prova");
        QCOMPARE(Project::create(root), QString());
        const Project p(root);
        files::write(p.path("fonti/coda/appunti.tex"), doc);
        const QList<Convert::Source> queue = Convert::queue(p);
        QCOMPARE(queue.size(), 2);
        QCOMPARE(queue[0].file, "fonti/coda/appunti.tex@2026-10-20");
        QCOMPARE(queue[0].kind, "tex");

        const QString answer = "```json\n{\"units\": [{\"env\": \"text\", \"page\": 1}], \"figures\": [], \"doubts\": []}\n```\n```latex\n\\subsection{Uno}\nTesto.\n```\n";
        const QJsonObject line{{"type", "result"}, {"result", answer}, {"total_cost_usd", 0.1}};
        files::writeBytes(dir.filePath("t.json"), QJsonDocument(line).toJson(QJsonDocument::Compact) + '\n');
        files::writeBytes(dir.filePath("v.json"), "{\"type\":\"result\",\"result\":\"{\\\"missing\\\": [], \\\"wrong\\\": []}\",\"total_cost_usd\":0.1}\n");
        files::write(dir.filePath("claude"), QString("#!/bin/sh\ncat >> '%1/richieste'\nfor a; do case \"$a\" in *'You check LaTeX'*) cat '%1/v.json'; exit 0;; esac; done\n"
                                                     "for a; do case \"$a\" in *'THE SOURCE IS LATEX'*) cat '%1/t.json'; exit 0;; esac; done\necho '{\"type\":\"result\",\"result\":\"{\\\"title\\\": \\\"Serie\\\"}\"}'\n").arg(dir.path()));
        QFile(dir.filePath("claude")).setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qputenv("TEXFIX_CLAUDE", dir.filePath("claude").toUtf8());
        JobControl control;
        const Convert::Result r = Convert(p, Settings::load(p), Style::classic(), &control).run({"fonti/coda/appunti.tex"});
        qunsetenv("TEXFIX_CLAUDE");
        QCOMPARE(r.failure, QString());
        QCOMPARE(r.written(), 2);
        QCOMPARE(r.lessons[0].source, "appunti.tex · lezione del 20/10/2026");
        const QString first = files::read(p.path("src/src1.tex"));
        QVERIFY2(first.startsWith("\\section{Serie}\n\\lezione{1}{20/10/2026}\n\n\\subsection{Uno}\n% @L1.1 · appunti.tex r.1-5\nTesto."), qPrintable(first));
        QVERIFY(files::read(p.path("src/src2.tex")).contains("\\lezione{2}{22/10/2026}"));
        QVERIFY(files::read(dir.filePath("richieste")).contains("SOURCE TEXT (lines 1-5 of the lecture)"));
        QVERIFY(QFileInfo::exists(p.path("fonti/usate/più lezioni/appunti.tex")));      // tutte le lezioni scritte: la fonte è usata
        QVERIFY(Convert::queue(p).isEmpty());
    }

    // La libreria: `styles` è già descritto, quindi si importa senza domande; le due colonne seguono lo stile.
    void bundledStyle()
    {
        QTemporaryDir dir, outside;
        const QString root = dir.filePath("prova");
        QCOMPARE(Project::create(root), QString());
        const Project p(root);
        files::write(outside.filePath("styles.sty"), "\\ProvidesPackage{styles}\n");
        const styleswitch::Imported in = styleswitch::import(p, outside.filePath("styles.sty"));
        QCOMPARE(in.error, QString());
        QVERIFY(in.described);
        QCOMPARE(in.style.environmentFor("attenzione"), "achtungbox");
        QCOMPARE(in.style.environment("theorembox")->color, "#f2c091");
        QCOMPARE(in.style.references, "tag");
        QCOMPARE(in.style.figureWrapper(), "tikzgraph");
        QVERIFY(in.style.twoColumns());
        QVERIFY(in.style.undecided().isEmpty());
        QVERIFY(!in.style.boxes().contains("eqbox"));                                   // il fulcro non è un'unità a sé
        QVERIFY(in.style.forPrompt().contains("**Il fulcro.**"));

        QCOMPARE(styleswitch::apply(p, "styles").error, QString());
        QVERIFY(files::read(p.path("main.tex")).startsWith("\\documentclass[11pt,a4paper,twocolumn]{article}"));

        // dalla libreria: lo styles vero, ripulito; con lui compila una lezione che usa ciò che ora è di Texfix
        QCOMPARE(styleswitch::library(), QStringList{"styles"});
        QCOMPARE(styleswitch::importFromLibrary(p, "styles").error, QString());
        const QString sty = files::read(p.path("styles.sty"));
        QVERIFY(sty.contains("v4.0") && !sty.contains("imgslot") && !sty.contains("addedtext") && !sty.contains("AllowBoxBreak"));
        if (hasLatexmk()) {
            QString main = files::read(p.path("main.tex"));
            files::write(p.path("main.tex"), main.replace("\\end{document}", "\\input{src/src1.tex}\n\\end{document}"));
            files::write(p.path("src/src1.tex"), "\\section{S}\n\\lezione{1}{01/10/2026}\n\\begin{definitionbox}[Norma]\n    Vale:\n    \\begin{eqbox}{defcolordarkest}\n        \\[ \\norm{\\vv{u}}_2 \\]\n    \\end{eqbox}\n\\end{definitionbox}\n"
                                                 "\\begin{addedtext}\n    Una nota.\n\\end{addedtext}\n\\begin{correction}\n    Un errore.\n\\end{correction}\n"
                                                 "\\begin{tikzgraph} % img src1-01\n    \\imgslot[0.6\\linewidth]{src1-01}{0.6}{un grafico}\n\\end{tikzgraph}\n\\begin{achtungbox}[!!]\n    Attenzione.\n\\end{achtungbox}\n");
            Compiler compiler;
            compiler.setProject(p);
            const BuildResult r = build(compiler, {});
            QVERIFY2(!r.hasErrors(), qPrintable(r.issues.isEmpty() ? r.failure : r.issues.first().text));
            files::write(p.path("main.tex"), main.replace("\\input{src/src1.tex}\n", ""));
            QFile::remove(p.path("src/src1.tex"));
        }
        QCOMPARE(styleswitch::apply(p, {}).error, QString());
        QVERIFY(files::read(p.path("main.tex")).startsWith("\\documentclass[11pt,a4paper]{article}"));
    }

    // I profili di partenza: le scelte di un progetto salvate, e un progetto nuovo che parte da lì.
    void startingProfiles()
    {
        QTemporaryDir dir, outside, home;
        profiles::setHome(home.path());
        const Project first(dir.filePath("primo"));
        QCOMPARE(Project::create(first.root()), QString());
        files::write(outside.filePath("styles.sty"), "\\ProvidesPackage{styles}\n");
        styleswitch::import(first, outside.filePath("styles.sty"));
        QCOMPARE(styleswitch::apply(first, "styles").error, QString());
        Settings s = Settings::load(first);
        s.setValue("qualita", "massima");
        s.rules = {"I vettori con la freccia."};
        s.setDone("src/src1.tex", true);
        s.save(first);

        QVERIFY(profiles::list().isEmpty());
        QVERIFY(!profiles::save(first, " ").isEmpty());
        QCOMPARE(profiles::save(first, "Come la vecchia Texfix"), QString());
        QCOMPARE(profiles::list(), QStringList{"Come la vecchia Texfix"});

        const Project second(dir.filePath("secondo"));
        QCOMPARE(Project::create(second.root()), QString());
        QCOMPARE(profiles::apply(second, "Come la vecchia Texfix"), QString());
        const Settings got = Settings::load(second);
        QCOMPARE(got.quality, "massima");
        QCOMPARE(got.rules, QStringList{"I vettori con la freccia."});
        QCOMPARE(got.style, "styles");
        QVERIFY(got.done.isEmpty());                                                   // le lezioni fatte non sono una scelta da riusare
        QVERIFY(QFileInfo::exists(second.path("styles.sty")));
        QVERIFY(QFileInfo::exists(second.path("styles.stile.json")));
        QVERIFY(files::read(second.path("texfix.sty")).contains("\\RequirePackage{styles}"));
        QVERIFY(files::read(second.path("main.tex")).contains("twocolumn"));
        QVERIFY(!profiles::apply(second, "non esiste").isEmpty());
        profiles::remove("Come la vecchia Texfix");
        QVERIFY(profiles::list().isEmpty());
        profiles::setHome({});
    }

    // «Correggo io» e «Decidi tu»: la modifica resta da confermare, con il prima e il dopo o con il perché.
    void correctAndDecide()
    {
        QTemporaryDir dir;
        const Project p(dir.path());
        const QString lesson = "src/src1.tex";
        const QString tex = "\\section{S}\nIl modulo è $|z|$.\nAltra riga.\n";
        files::write(p.path(lesson), tex);
        files::write(p.path("fonti/usate/lezione 1/a.md"), "modulo |2|\n");
        files::write(p.path(Convert::doubtsFile(lesson)),
                     R"({"dubbi": [{"id": "src1-d1", "page": 1, "source": "a.md", "what": "z o 2?", "latex": "$|z|$",
                         "readings": [{"text": "z", "latex": "$|z|$"}, {"text": "2", "latex": "$|2|$"}]}]})");

        QCOMPARE(review::startCorrection(p, lesson, "src1-d1"), QString());
        QVERIFY(review::change(p, lesson, "src1-d1").none());                          // ancora niente è cambiato
        files::write(p.path(lesson), "\\section{S}\nIl modulo vale $|w|$.\nAltra riga.\n");
        const review::Change c = review::change(p, lesson, "src1-d1");
        QCOMPARE(c.before, QStringList{"Il modulo è $|z|$."});
        QCOMPARE(c.after, QStringList{"Il modulo vale $|w|$."});
        review::Doubt d = review::doubts(p, lesson).first();
        QVERIFY(d.open && d.pending);
        QCOMPARE(review::counts(p, lesson).doubts, 1);                                 // da confermare: resta da decidere
        QCOMPARE(review::confirm(p, lesson, "src1-d1"), QString());
        d = review::doubts(p, lesson).first();
        QVERIFY(!d.open);
        QCOMPARE(d.how, "corretta");
        QVERIFY(!review::confirm(p, lesson, "src1-d1").isEmpty());

        // il modello: scrive la sua lettura, ma aspetta la conferma
        files::write(p.path(lesson), tex);
        review::reopen(p, lesson, "src1-d1", false);
        const QJsonObject line{{"type", "result"}, {"result", R"({"latex": "$|2|$", "why": "Nel testo è scritto 2."})"}, {"total_cost_usd", 0.01}};
        files::writeBytes(dir.filePath("r.json"), QJsonDocument(line).toJson(QJsonDocument::Compact) + '\n');
        files::write(dir.filePath("claude"), QString("#!/bin/sh\ncat > '%1/richiesta'\ncat '%1/r.json'\n").arg(dir.path()));
        QFile(dir.filePath("claude")).setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qputenv("TEXFIX_CLAUDE", dir.filePath("claude").toUtf8());
        JobControl control;
        Model model("veloce", dir.path(), &control);
        QCOMPARE(review::decide(p, Settings(), model, lesson, "src1-d1"), QString());
        qunsetenv("TEXFIX_CLAUDE");
        QVERIFY(files::read(dir.filePath("richiesta")).contains("SOURCE TEXT:\\nmodulo |2|"));
        QVERIFY(files::read(p.path(lesson)).contains("Il modulo è $|2|$."));
        d = review::doubts(p, lesson).first();
        QVERIFY(d.open && d.pending);
        QCOMPARE(d.how, "modello");
        QCOMPARE(d.why, "Nel testo è scritto 2.");
        QCOMPARE(review::reopen(p, lesson, "src1-d1", true), QString());                // annulla la modifica del modello
        QCOMPARE(files::read(p.path(lesson)), tex);
    }

    // I tag di provenienza: le fonti con la loro sigla, e dove cade nel PDF ogni blocco che ne ha una.
    void provenanceTags()
    {
        QTemporaryDir dir;
        const QString root = dir.filePath("prova");
        QCOMPARE(Project::create(root), QString());
        const Project p(root);
        QString main = files::read(p.path("main.tex"));
        files::write(p.path("main.tex"), main.replace("\\end{document}", "\\input{src/src1.tex}\n\\end{document}"));
        files::write(p.path("src/src1.tex"), "\\section{S}\n\\lezione{1}{}\n% @L1.1 · appunti.pdf p.2\nPrimo.\n\n% @L1.2 · dispensa.tex r.10-80\n\\begin{theorem}\nx\n\\end{theorem}\n"
                                             "% @L1.3\nSenza fonte.\n\n% @L1.4 · appunti.pdf pp.3-4\nUltimo.\n");
        files::write(p.path("fonti/usate/lezione 1/appunti.pdf"), "x");
        const Style style = Style::classic();
        const QList<provenance::Source> sources = provenance::sources(p, style);
        QCOMPARE(sources.size(), 2);
        QCOMPARE(sources[0].mark, "A");
        QCOMPARE(sources[0].path, "fonti/usate/lezione 1/appunti.pdf");
        QCOMPARE(sources[1].name, "dispensa.tex");
        QCOMPARE(sources[1].path, QString());                                          // la fonte non c'è più: resta la sigla
        if (!hasLatexmk())
            QSKIP("latexmk non c'è");
        Compiler compiler;
        compiler.setProject(p);
        const BuildResult r = build(compiler, {});
        QVERIFY(!r.hasErrors());
        const QList<provenance::Tag> tags = provenance::tags(p, style, r.pdf);
        QCOMPARE(tags.size(), 3);                                                      // il blocco senza fonte non ha tag
        QCOMPARE(tags[0].mark, "A");
        QCOMPARE(tags[0].sourcePage, 2);
        QCOMPARE(tags[1].mark, "B");
        QCOMPARE(tags[1].sourcePage, 0);
        QCOMPARE(tags[2].block, "L1.4");
        QCOMPARE(tags[2].sourcePage, 3);
        QVERIFY(tags[0].page == 2 && tags[1].y > tags[0].y && tags[2].y > tags[1].y);
    }

    // Con `styles` su due colonne: un riquadro più lungo di ciò che resta della colonna si spezza fra due paragrafi.
    void layoutBreakInside()
    {
        if (!hasLatexmk())
            QSKIP("latexmk non c'è");
        QTemporaryDir dir;
        const QString root = dir.filePath("prova");
        QCOMPARE(Project::create(root), QString());
        const Project p(root);
        QCOMPARE(styleswitch::importFromLibrary(p, "styles").error, QString());
        QCOMPARE(styleswitch::apply(p, "styles").error, QString());
        const Style style = styleswitch::current(p);
        QCOMPARE(style.breakCommand, "\\tcbbreak");
        QString main = files::read(p.path("main.tex"));
        files::write(p.path("main.tex"), main.replace("\\end{document}", "\\input{src/src1.tex}\n\\end{document}"));
        // mezza colonna di testo, poi un teorema di quaranta paragrafi: non entra né in ciò che resta né in una colonna intera
        QString tex = "\\section{S}\n\\lezione{1}{}\n\n";
        for (int i = 0; i < 12; ++i)
            tex += QStringLiteral("Riga %1 di testo della lezione, lunga quanto basta per andare a capo nella colonna stretta.\n\n").arg(i + 1);
        tex += "\\begin{theorembox}[Lungo]\n";
        for (int i = 0; i < 40; ++i)
            tex += QStringLiteral("    Paragrafo %1 del teorema, lungo quanto basta per andare a capo nella colonna stretta del documento.\n\n").arg(i + 1);
        tex += "\\end{theorembox}\nFine.\n";
        files::write(p.path("src/src1.tex"), blocks::mark(tex, 1, blocks::Units{style.boxes(), {"tikzgraph"}}));

        const layout::Result r = layout::lesson(p, style, "src/src1.tex");
        QCOMPARE(r.failure, QString());
        const QList<layout::Move> moves = layout::moves(p, "src/src1.tex");
        QVERIFY(!moves.isEmpty());
        if (moves.first().state != "fatta")
            QSKIP(qPrintable("con queste misure il riquadro non si è potuto spezzare su una cucitura: " + moves.first().what));
        QVERIFY2(moves.first().what.contains("fra due paragrafi"), qPrintable(moves.first().what));
        QCOMPARE(moves.first().same, moves.first().what.contains("la pagina resta uguale"));   // il segno «invariata» resta nel registro
        qInfo() << "spezzamento:" << moves.first().what;
        const QString text = files::read(p.path("src/src1.tex"));
        QVERIFY(text.contains("\n\n    \\tcbbreak % impaginazione\n\n    Paragrafo "));
        QVERIFY(!layout::needed(p, "src/src1.tex"));
        {   // la lezione cambia e si rimpagina: la mossa resta una, e la sua riga non si scrive due volte
            files::write(p.path("src/src1.tex"), QString(text).replace("Fine.", "Fine davvero."));
            QVERIFY(layout::needed(p, "src/src1.tex"));
            QCOMPARE(layout::lesson(p, style, "src/src1.tex").failure, QString());
            QCOMPARE(files::read(p.path("src/src1.tex")).count("% impaginazione"), 1);
            QCOMPARE(layout::moves(p, "src/src1.tex").size(), moves.size());
            QVERIFY(!layout::moves(p, "src/src1.tex").first().same);   // la lezione è cambiata: «invariata» non vale più
            files::write(p.path("src/src1.tex"), text);
            QCOMPARE(layout::lesson(p, style, "src/src1.tex").failure, QString());
            QCOMPARE(files::read(p.path("src/src1.tex")), text);
        }
        Compiler compiler;
        compiler.setProject(p);
        QVERIFY(!build(compiler, "src/src1.tex").hasErrors());

        QCOMPARE(layout::decide(p, "src/src1.tex", moves.first().block, "lasciata"), QString());     // via lo spezzamento
        QVERIFY(!files::read(p.path("src/src1.tex")).contains("impaginazione"));
        QCOMPARE(layout::decide(p, "src/src1.tex", moves.first().block, "accettata"), QString());    // e di nuovo dov'era
        QCOMPARE(files::read(p.path("src/src1.tex")), text);
    }

    // La cronologia: lavori e modifiche a mano si annullano e si ripetono; un passo nuovo toglie il «ripeti».
    void history()
    {
        QTemporaryDir dir;
        const QString root = dir.path();
        auto file = [&](const char *name) { return root + '/' + name; };
        files::write(file("main.tex"), "main");
        files::write(file("src/a.tex"), "a1");
        files::write(file("docs/nota.txt"), "non seguito");
        files::write(file("build/cronologia/vecchia"), "rimasta da una chiusura sbagliata");
        {
            History history(root);
            QVERIFY(!QFileInfo::exists(file("build/cronologia/vecchia")));
            QCOMPARE(history.steps().size(), 1);
            QCOMPARE(history.undoTitle(), QString());
            QVERIFY(!history.undo().ok);
            QVERIFY(!history.record("Niente"));                                        // niente è cambiato: nessun passo

            // un lavoro: scrive a, crea b, toglie main
            files::write(file("src/a.tex"), "a2");
            files::write(file("src/b.tex"), "b");
            QFile::remove(file("main.tex"));
            files::write(file("docs/nota.txt"), "cambiato");
            QVERIFY(history.record("Converti"));
            QCOMPARE(history.undoTitle(), "Converti");

            History::Outcome o = history.undo();
            QVERIFY(o.ok);
            QCOMPARE(o.changed, (QStringList{"main.tex", "src/a.tex", "src/b.tex"}));
            QCOMPARE(files::read(file("src/a.tex")), "a1");
            QCOMPARE(files::read(file("main.tex")), "main");
            QVERIFY(!QFileInfo::exists(file("src/b.tex")));
            QCOMPARE(files::read(file("docs/nota.txt")), "cambiato");                 // docs/ non si segue
            QCOMPARE(history.redoTitle(), "Converti");

            QVERIFY(history.redo().ok);
            QCOMPARE(files::read(file("src/a.tex")), "a2");
            QVERIFY(QFileInfo::exists(file("src/b.tex")));
            QVERIFY(!QFileInfo::exists(file("main.tex")));

            // modifiche a mano non ancora in un passo: ⌘Z annulla quelle
            files::write(file("src/a.tex"), "a3 a mano");
            QCOMPARE(history.undoTitle(), "Modifiche a mano");
            QVERIFY(history.undo().ok);
            QCOMPARE(files::read(file("src/a.tex")), "a2");
            QVERIFY(history.undo().ok);
            QCOMPARE(files::read(file("src/a.tex")), "a1");

            // una modifica dopo un annullamento toglie il «ripeti»
            files::write(file("src/a.tex"), "altra strada");
            QCOMPARE(history.redoTitle(), QString());
            QVERIFY(!history.redo().ok);
            QCOMPARE(history.steps().size(), 2);
            QCOMPARE(history.steps().last().title, "Modifiche a mano");

            QVERIFY(history.goTo(0).ok);                                               // vai a un passo
            QCOMPARE(files::read(file("src/a.tex")), "a1");
            QVERIFY(!history.goTo(0).ok);
            QVERIFY(history.goTo(1).ok);
            QCOMPARE(files::read(file("src/a.tex")), "altra strada");
        }
        QVERIFY(!QFileInfo::exists(file("build/cronologia")));                        // alla chiusura si butta
    }

    // La regola delle lezioni: un \input nel preambolo, uno commentato, uno che manca, senza .tex, l'ultima riga senza a capo.
    void inputs()
    {
        const QString main = "\\documentclass{article}\n"
                             "\\input{src/preambolo}\n"
                             "% \\begin{document}\n"
                             "\\begin{document}\n"
                             "\\input{src/src1.tex}\n"
                             "  \\input{src/src2}  % la seconda\n"
                             "% \\input{src/src3.tex}\n"
                             "\\input{altro/x.tex}\n"
                             "\\input{src/manca.tex}\\clearpage\n"
                             "\\input{src/ultima.tex}";
        QCOMPARE(Project::inputsOf(main), (QStringList{"src/src1.tex", "src/src2.tex", "src/manca.tex", "src/ultima.tex"}));
        QCOMPARE(Project::inputsOf("\\input{src/a.tex}"), QStringList());            // senza \begin{document} niente lezioni
        QCOMPARE(Project::inputsOf("\\begin{document}\\input{src/a.tex}"), QStringList{"src/a.tex"});
    }

    void lessonOf()
    {
        const Lesson l = Project::lessonOf("src/src3.tex", "% \\lezione{9}{no}\n\\lezione{3}{29/09/2026}\n"
                                                           "\\section[breve]{Serie di \\textbf{potenze} {e} raggio}\ntesto\n\\section{Altro}\n");
        QCOMPARE(l.number, 3);
        QCOMPARE(l.date, "29/09/2026");
        QCOMPARE(l.title, "Serie di \\textbf{potenze} {e} raggio");
        const Lesson none = Project::lessonOf("src/x.tex", "solo testo");
        QCOMPARE(none.number, 0);
        QVERIFY(none.title.isEmpty());
    }

    void createProject()
    {
        QTemporaryDir dir;
        const QString root = dir.filePath("Analisi 2");
        QCOMPARE(Project::create(root), QString());
        QVERIFY(Project::isProject(root));
        for (const char *f : {"main.tex", "texfix.sty", ".latexmkrc", ".gitignore", ".git", "src", "imgs", "fonti/coda",
                              "fonti/usate", "docs", "salvataggi/progetto.json"})
            QVERIFY2(QFileInfo::exists(root + '/' + f), f);
        QVERIFY(files::read(root + "/main.tex").contains("\\title{Analisi 2}"));
        QCOMPARE(Project(root).complete(), QStringList());                             // già intero: niente da aggiungere
        QVERIFY(!Project::create(root).isEmpty());                                     // non è più vuota

        // un main.tex dell'utente non si tocca; texfix.sty torna quello del programma
        files::write(root + "/main.tex", "mio");
        files::write(root + "/texfix.sty", "vecchio");
        QCOMPARE(Project(root).complete(), QStringList{"texfix.sty"});
        QCOMPARE(files::read(root + "/main.tex"), "mio");
    }

    void adoptFolder()
    {
        QTemporaryDir dir;
        const QString root = dir.path();
        files::write(root + "/appunti.pdf", "a");
        files::write(root + "/docs/appunti.pdf", "già qui");
        files::write(root + "/vecchi/uno.txt", "1");
        files::write(root + "/.nascosto", "n");
        const QStringList moved = Project::adopt(root);
        QCOMPARE(moved, (QStringList{"appunti.pdf → docs/appunti (2).pdf", "vecchi → docs/vecchi"}));
        QCOMPARE(files::read(root + "/docs/appunti.pdf"), "già qui");
        QCOMPARE(files::read(root + "/docs/appunti (2).pdf"), "a");
        QCOMPARE(files::read(root + "/docs/vecchi/uno.txt"), "1");
        QVERIFY(QFileInfo::exists(root + "/.nascosto"));
        QVERIFY(Project::isProject(root));
        QVERIFY(QFileInfo::exists(root + "/main.tex"));
    }

    void reducedMain()
    {
        QTemporaryDir dir;
        const Project p(dir.path());
        files::write(p.path("main.tex"), "\\documentclass{article}\n\\begin{document}\n\\tableofcontents\n"
                                         "\\input{src/a.tex}\n\\clearpage\n\n\\input{src/b}\n\\clearpage\n"
                                         "\\input{src/c.tex}\n\\end{document}");
        files::write(p.path("src/a.tex"), "\\section{Uno}\n% \\section{no}\n\\section[x]{Due}\n");
        files::write(p.path("src/b.tex"), "\\section{Tre}\n");
        QCOMPARE(Compiler::reducedMain(p, "src/b.tex"),
                 "\\documentclass{article}\n\\begin{document}\n\\tableofcontents\n\n"
                 "\\setcounter{section}{2}\n\\input{src/b}\n\\clearpage\n\\end{document}");
        QString error;
        QVERIFY(Compiler::reducedMain(p, "src/z.tex", &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void parseLog()
    {
        const QString log = "(./main.tex (./src/src1.tex\n"
                            "Overfull \\hbox (12.3pt too wide) in paragraph at lines 10--12\n"
                            "\\OT1/cmr/m/n/10.95 una ri-ga trop-po lar-ga $[]$ con (+20) te-sto\n"
                            "./src/src1.tex:40: Undefined control sequence.\n"
                            ") (./src/src2.tex\n"
                            "Underfull \\hbox (badness 10000) detected at line 7\n"
                            "[]\\OT1/cmr/m/n/10.95 vuota\n"
                            "! Emergency stop.\n"
                            ") )\nOutput written on build/main.pdf (12 pages, 3456 bytes).\n";
        const QList<Issue> issues = texlog::parse(log);
        QCOMPARE(issues.size(), 4);
        QCOMPARE(issues[0], (Issue{Issue::WideLine, "src/src1.tex", 10, 12, "una riga troppo larga con testo", "12.3pt too wide"}));
        QCOMPARE(issues[1], (Issue{Issue::Error, "src/src1.tex", 40, 40, "Undefined control sequence.", {}}));
        QCOMPARE(issues[2], (Issue{Issue::LooseLine, "src/src2.tex", 7, 7, "vuota", "badness 10000"}));
        QCOMPARE(issues[3], (Issue{Issue::Error, "src/src2.tex", 0, 0, "Emergency stop.", {}}));
        QCOMPARE(texlog::pages(log), 12);
        QVERIFY(!texlog::brokenAux(log));
        QVERIFY(texlog::brokenAux("(./build/main.aux)\nRunaway argument?\n{x\n! File ended.\nl.24 \\begin{document}\n"));
    }

    // Un progetto nuovo compila così com'è, e con una lezione; un errore porta alla sua riga.
    void compile()
    {
        if (!hasLatexmk())
            QSKIP("latexmk non c'è");
        QTemporaryDir dir;
        const QString root = dir.filePath("prova");
        QCOMPARE(Project::create(root), QString());
        const Project p(root);
        Compiler compiler;
        compiler.setProject(p);

        BuildResult r = build(compiler, {});
        QVERIFY2(r.failure.isEmpty(), qPrintable(r.failure));
        QVERIFY(!r.hasErrors());
        QCOMPARE(r.pdf, p.path("build/main.pdf"));
        QVERIFY(QFileInfo::exists(r.pdf));

        QString main = files::read(p.path("main.tex"));
        main.replace("\\end{document}", "\\input{src/src1.tex}\n\\clearpage\n\\input{src/src2.tex}\n\\end{document}");
        files::write(p.path("main.tex"), main);
        files::write(p.path("src/src1.tex"), "\\lezione{1}{01/10/2026}\n\\section{Prima}\n\\begin{theorem}Vale $a=b$.\\end{theorem}\n");
        files::write(p.path("src/src2.tex"), "\\lezione{2}{02/10/2026}\n\\section{Seconda}\nTesto \\nonesiste.\n");

        r = build(compiler, "src/src1.tex");
        QVERIFY2(r.failure.isEmpty(), qPrintable(r.failure));
        QVERIFY(!r.hasErrors());
        QCOMPARE(r.pdf, p.path("build/lezioni/src1.pdf"));
        QVERIFY(QFileInfo::exists(r.pdf));

        // un .aux troncato non ferma nemmeno la compilazione di un lavoro (quella dell'impaginazione): si butta e si rifà
        {
            const QString aux = p.path("build/lezioni/src1.aux");
            const QString whole = files::read(aux);
            QVERIFY(whole.contains("\\@writefile{toc}"));
            files::write(aux, whole.left(whole.indexOf("\\@writefile{toc}") + 40) + "\n");   // una riga lasciata a metà
            const BuildResult again = Compiler::compileSync(p, "src/src1.tex");
            QVERIFY2(!again.hasErrors(), "il .aux troncato ha fermato la compilazione");
            QVERIFY(files::read(aux).size() >= whole.size());
        }

        // il salto: dalla riga del teorema alla pagina della lezione, e da quel punto di nuovo alla lezione
        const synctex::Spot there = synctex::toPdf(root, r.pdf, "src/src1.tex", 3);
        QVERIFY(there.ok);
        QCOMPARE(there.page, 2);
        const synctex::Spot back = synctex::toSource(root, r.pdf, there.page, there.x + 5, there.y + there.height / 2);
        QVERIFY(back.ok);
        QCOMPARE(back.file, "src/src1.tex");
        QCOMPARE(back.line, 3);
        QVERIFY(!synctex::toPdf(root, r.pdf, "src/src2.tex", 1).ok);                  // quella lezione non è in questo PDF

        r = build(compiler, "src/src2.tex");
        QVERIFY(r.hasErrors());
        QCOMPARE(r.issues.first().file, "src/src2.tex");
        QCOMPARE(r.issues.first().line, 3);

        r = build(compiler, "src/manca.tex");
        QVERIFY(!r.failure.isEmpty());
        QCOMPARE(files::read(p.path("main.tex")), main);                               // il main non si tocca mai
    }

    // «Ferma» chiude subito latexmk e i suoi figli, e la compilazione dopo riparte pulita.
    void stopCompile()
    {
        if (!hasLatexmk())
            QSKIP("latexmk non c'è");
        QTemporaryDir dir;
        const QString root = dir.filePath("prova");
        QCOMPARE(Project::create(root), QString());
        const Project p(root);
        QString main = files::read(p.path("main.tex"));
        main.replace("\\end{document}", "\\newcount\\n \\loop\\ifnum\\n<200000 \\advance\\n1 x\\par\\repeat\n\\end{document}");
        files::write(p.path("main.tex"), main);
        Compiler compiler;
        compiler.setProject(p);
        QSignalSpy done(&compiler, &Compiler::finished);
        bool runningAtStart = false;                   // chi sente «started» lo vede già al lavoro
        connect(&compiler, &Compiler::started, &compiler, [&] { runningAtStart = compiler.isRunning(); });
        compiler.compileDocument();
        QTest::qWait(1500);
        QVERIFY(runningAtStart);
        QVERIFY(compiler.isRunning());
        QElapsedTimer timer;
        timer.start();
        compiler.stop();
        QVERIFY(timer.elapsed() < 3500);
        QVERIFY(!compiler.isRunning());
        QCOMPARE(done.size(), 1);
        QVERIFY(done.first().first().value<BuildResult>().stopped);
        QVERIFY(!QFileInfo::exists(p.path("build/main.aux")));

        files::write(p.path("main.tex"), main.replace("200000", "20"));
        const BuildResult r = build(compiler, {});
        QVERIFY(!r.hasErrors());
        QVERIFY(QFileInfo::exists(r.pdf));
    }
};

QTEST_GUILESS_MAIN(CoreTests)
#include "tst_core.moc"
