// Le misure: quanto costa ciò che l'interfaccia rifà spesso, su un progetto finto ma grande (30 lezioni di 400 righe,
// con dubbi, aggiunte, figure e mosse). I tempi non devono peggiorare: `texfix_bench` li confronta con quelli di
// riferimento in tests/misure.txt ed esce con 1 se uno supera il doppio del riferimento più 5 ms.
//   build/texfix_bench            misura e confronta
//   build/texfix_bench --scrivi   misura e riscrive i tempi di riferimento
#include "core/Blocks.h"
#include "core/Figures.h"
#include "core/Files.h"
#include "core/Fix.h"
#include "core/History.h"
#include "core/Layout.h"
#include "core/Project.h"
#include "core/Review.h"
#include "core/Snippets.h"
#include "core/Style.h"
#include "core/TexLog.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

#include <functional>

using namespace tx;

static double median(const std::function<void()> &work, int runs = 7)
{
    QList<double> times;
    for (int i = 0; i < runs; ++i) {
        QElapsedTimer timer;
        timer.start();
        work();
        times << timer.nsecsElapsed() / 1e6;
    }
    std::sort(times.begin(), times.end());
    return times[times.size() / 2];
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTemporaryDir dir;
    const QString root = dir.filePath("prova");
    if (!Project::create(root).isEmpty())
        return 2;
    const Project p(root);
    const Style style = Style::classic();
    const blocks::Units units{style.boxes(), {"center", "figure"}};

    // il progetto finto
    QString inputs, log;
    QJsonArray figures;
    for (int n = 1; n <= 30; ++n) {
        QString tex = QStringLiteral("\\section{Lezione %1}\n\\lezione{%1}{01/10/2026}\n\n").arg(n);
        QJsonArray doubts;
        for (int k = 0; k < 40; ++k) {
            tex += QStringLiteral("\\subsection{Parte %1}\nSia $f_{%1}$ una funzione della lezione %2, con $x_{%1} \\in \\mathbb{R}$ e un po' di testo che riempie la riga.\n\n").arg(k).arg(n);
            tex += QStringLiteral("\\begin{theorem}[Enunciato %1]\n    Vale $a_{%1}^{%2} = b_{%1}$ per ogni indice.\n\n    Inoltre la somma converge.\n\\end{theorem}\n\n").arg(k).arg(n);
            if (k % 8 == 0)
                tex += QStringLiteral("\\begin{addedtext}\nUn'aggiunta numero %1 della lezione %2.\n\\end{addedtext}\n\n").arg(k).arg(n);
            if (k % 10 == 0) {
                tex += QStringLiteral("\\begin{center} %% img src%1-%2\n\\imgslot{src%1-%2}{0.6}{un grafico}\n\\end{center}\n\n").arg(n).arg(k / 10 + 1, 2, 10, QChar('0'));
                figures.append(QJsonObject{{"nome", QStringLiteral("src%1-%2").arg(n).arg(k / 10 + 1, 2, 10, QChar('0'))}, {"stato", k ? "disegnata" : "da_inserire"}});
            }
            if (k % 6 == 0)
                doubts.append(QJsonObject{{"id", QStringLiteral("src%1-d%2").arg(n).arg(k)}, {"what", "Quale indice?"},
                                          {"latex", QStringLiteral("a_{%1}^{%2} = b_{%1}").arg(k).arg(n)},
                                          {"readings", QJsonArray{QJsonObject{{"text", "b"}, {"latex", QStringLiteral("a_{%1}^{%2} = b_{%1}").arg(k).arg(n)}},
                                                                  QJsonObject{{"text", "c"}, {"latex", QStringLiteral("a_{%1}^{%2} = c_{%1}").arg(k).arg(n)}}}}});
            log += QStringLiteral("Overfull \\hbox (%1.5pt too wide) in paragraph at lines %2--%3\n[]\\OT1/cmr/m/n/10 testo della riga\n\n").arg(k).arg(k * 9 + 4).arg(k * 9 + 5);
        }
        files::write(p.path(QStringLiteral("src/src%1.tex").arg(n)), blocks::mark(tex, n, units));
        files::write(p.path(QStringLiteral("salvataggi/dubbi/src%1.json").arg(n)), QString::fromUtf8(QJsonDocument(QJsonObject{{"dubbi", doubts}}).toJson()));
        inputs += QStringLiteral("\\input{src/src%1.tex}\n").arg(n);
    }
    files::write(p.path("salvataggi/figure.json"), QString::fromUtf8(QJsonDocument(QJsonObject{{"figure", figures}}).toJson()));
    QString main = files::read(p.path("main.tex"));
    files::write(p.path("main.tex"), main.replace("\\end{document}", inputs + "\\end{document}"));
    const QString one = files::read(p.path("src/src7.tex"));
    const QStringList oneLines = one.split('\n');

    // ciò che si misura
    QList<std::pair<QString, double>> measures;
    auto measure = [&](const QString &name, const std::function<void()> &work) { measures << std::pair{name, median(work)}; };
    measure("lezioni del documento (numero, data, titolo)", [&] { p.lessons(); });
    measure("blocchi di una lezione", [&] { blocks::parse(one, units); });
    measure("dubbi e aggiunte di tutte le lezioni", [&] {
        for (const Lesson &l : p.lessons()) {
            review::doubts(p, l.file);
            review::declared(p, l.file);
        }
    });
    measure("figure da inserire di tutte le lezioni", [&] {
        for (const Lesson &l : p.lessons())
            figures::pending(p, l.file);
    });
    measure("mosse di impaginazione di tutte le lezioni", [&] {
        for (const Lesson &l : p.lessons())
            layout::toReview(p, l.file);
    });
    measure("una decisione su un dubbio", [&] {
        review::choose(p, "src/src7.tex", "src7-d6", 1);
        review::reopen(p, "src/src7.tex", "src7-d6", true);
    });
    measure("avvisi di un log di 1200 righe fuori misura", [&] { texlog::parse(log); });
    measure("paragrafo di una riga", [&] { fix::paragraph(oneLines, 200, 200); });
    measure("prosa di una domanda per la resa", [&] { snippets::prose("Vale ω0 o V_CB al 50%? Con $x_1$ e tanto altro testo dopo."); });
    {
        History history(root);
        measure("un passo della cronologia dopo una modifica", [&] {
            files::write(p.path("src/src3.tex"), files::read(p.path("src/src3.tex")) + "x\n");
            history.record("modifica");
        });
    }

    // il confronto con i tempi di riferimento
    const QString reference = QStringLiteral(TEXFIX_SOURCE_DIR "/tests/misure.txt");
    if (app.arguments().contains("--scrivi")) {
        QString text;
        for (const auto &[name, ms] : measures)
            text += QStringLiteral("%1\t%2\n").arg(ms, 0, 'f', 2).arg(name);
        files::write(reference, text);
        out << "tempi di riferimento scritti in " << reference << "\n";
    }
    QHash<QString, double> known;
    for (const QString &line : files::read(reference).split('\n', Qt::SkipEmptyParts))
        known.insert(line.section('\t', 1), line.section('\t', 0, 0).toDouble());
    int worse = 0;
    for (const auto &[name, ms] : measures) {
        const bool has = known.contains(name), bad = has && ms > 2 * known.value(name) + 5;
        worse += bad;
        out << QStringLiteral("%1 ms").arg(ms, 9, 'f', 2) << "   " << (has ? QStringLiteral("(riferimento %1)").arg(known.value(name), 0, 'f', 2) : QStringLiteral("(nuova)"))
            << "   " << name << (bad ? "   ← PEGGIORATA" : "") << "\n";
    }
    out << (worse ? QStringLiteral("%1 misure peggiorate\n").arg(worse) : QStringLiteral("nessuna misura peggiorata\n"));
    return worse ? 1 : 0;
}
