#include "core/Compiler.h"

#include "core/Files.h"
#include "core/Job.h"
#include "core/Tools.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

using RE = QRegularExpression;

namespace tx {

namespace {
const char *const OUT = "build";
const char *const LESSONS = "build/lezioni";

QString stemOf(const QString &lesson) { return QFileInfo(lesson).completeBaseName(); }

int sectionsIn(const QString &text)
{
    static const RE section(R"(\\section\s*[\[{])");                // quelle con l'asterisco non hanno numero
    int n = 0;
    for (QStringView line : QStringView(text).split('\n'))
        for (auto it = section.globalMatchView(uncommented(line)); it.hasNext(); it.next())
            ++n;
    return n;
}
}

bool BuildResult::hasErrors() const
{
    if (!failure.isEmpty())
        return true;
    for (const Issue &issue : issues)
        if (issue.kind == Issue::Error)
            return true;
    return false;
}

Compiler::Compiler(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<tx::BuildResult>();
    makeProcess();
    // A volte Qt non si accorge che latexmk è finito: resterebbe «in corso» per sempre. Se il processo non c'è più da
    // un paio di secondi, la compilazione si chiude da qui con ciò che dice il log.
    m_watch.setInterval(1000);
    connect(&m_watch, &QTimer::timeout, this, [this] {
        if (!isRunning())
            return m_watch.stop();
        if (!tools::gone(m_process->processId())) {
            m_goneTicks = 0;
        } else if (++m_goneTicks >= 2) {
            abandon();
            finish();
        }
    });
}

Compiler::~Compiler()
{
    blockSignals(true);                                // chi ascoltava si sta già smontando
    m_process->disconnect(this);
    if (isRunning()) {                                 // alla chiusura: niente attese senza limite
        tools::stopGroup(m_process->processId());
        if (!m_process->waitForFinished(1500))
            abandon();
    }
}

bool Compiler::isRunning() const { return m_process->state() != QProcess::NotRunning; }

void Compiler::makeProcess()
{
    m_process = new QProcess(this);
    tools::ownGroup(*m_process);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &Compiler::readOutput);
    connect(m_process, &QProcess::finished, this, [this] {
        m_watch.stop();
        readOutput();
        m_process->readAllStandardError();
        finish();
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_watch.stop();
        m_result.failure = QStringLiteral("Non trovo latexmk: serve una distribuzione TeX (MacTeX).");
        emit finished(m_result);
    });
}

// Un processo di cui Qt non vede la fine si lascia andare (distruggerlo vorrebbe dire aspettarlo) e se ne prepara un altro.
void Compiler::abandon()
{
    m_watch.stop();
    QProcess *old = m_process;
    old->disconnect(this);
    old->setParent(nullptr);
    tools::reap(old->processId());
    makeProcess();
}

QString Compiler::pdfOf(const Project &project, const QString &lesson)
{
    return lesson.isEmpty() ? project.path(QStringLiteral("%1/main.pdf").arg(OUT))
                            : project.path(QStringLiteral("%1/%2.pdf").arg(LESSONS, stemOf(lesson)));
}

QString Compiler::base() const
{
    return Compiler::pdfOf(m_project, m_result.lesson).chopped(4);
}

QString Compiler::reducedMain(const Project &project, const QString &lesson, QString *error)
{
    const QString main = files::read(project.path(Project::MAIN));
    const QStringList inputs = Project::inputsOf(main);
    const qsizetype at = inputs.indexOf(lesson);
    if (at < 0) {
        if (error)
            *error = QStringLiteral("«%1» non è nel documento: manca il suo \\input in main.tex.").arg(lesson);
        return {};
    }
    // le sezioni delle lezioni prima: la lezione da sola ha gli stessi numeri che ha nel documento
    int sections = 0;
    for (qsizetype i = 0; i < at; ++i)
        sections += sectionsIn(files::read(project.path(inputs[i])));

    static const RE input(R"(\\input\{\s*(src/[^}\s]+)\s*\})");
    static const RE clearpage(R"(^\s*\\clearpage\s*$)");
    QStringList out;
    bool body = false, dropped = false;                // dropped: la riga prima era un \input tolto
    for (const QString &line : main.split('\n')) {
        const QStringView code = uncommented(line);
        body = body || code.contains(QLatin1String("\\begin{document}"));
        const auto m = body ? input.matchView(code) : QRegularExpressionMatch();
        if (m.hasMatch()) {
            QString name = m.captured(1);
            if (!name.endsWith(".tex"))
                name += ".tex";
            dropped = name != lesson;
            if (!dropped)
                out << QStringLiteral("\\setcounter{section}{%1}").arg(sections) << line;
            continue;
        }
        if (dropped && clearpage.matchView(code).hasMatch())
            continue;                                   // il \clearpage che seguiva un \input tolto
        dropped = dropped && code.trimmed().isEmpty();
        out << line;
    }
    return out.join('\n');
}

void Compiler::compileDocument() { start({}); }
void Compiler::compileLesson(const QString &file) { start(file); }

void Compiler::start(const QString &lesson)
{
    if (isRunning() || !m_project.isValid())
        return;
    m_result = BuildResult();
    m_result.lesson = lesson;
    m_result.pdf = pdfOf(m_project, lesson);
    m_retried = false;
    m_timer.start();
    if (!lesson.isEmpty()) {
        QString error;
        const QString main = reducedMain(m_project, lesson, &error);
        if (!error.isEmpty()) {
            m_result.failure = error;
            emit finished(m_result);
            return;
        }
        files::write(base() + ".main.tex", main);
    }
    m_expectedPages = texlog::pages(files::read(base() + ".log"));   // quante pagine aveva l'ultima volta
    emit started(lesson);
    emit progress(0, QString());
    launch();
}

void Compiler::launch()
{
    m_process->setProcessEnvironment(environment());
    m_process->setWorkingDirectory(m_project.root());
    m_output.clear();
    m_scanned = 0;
    m_pass = m_pages = m_lastPage = 0;
    m_goneTicks = 0;
    m_process->start("latexmk", arguments(m_result.lesson));
    m_watch.start();
}

void Compiler::readOutput()
{
    m_output += m_process->readAllStandardOutput();
    // latexmk dice «Run number N of rule 'pdflatex'» a ogni passata; pdflatex scrive [n] a ogni pagina uscita
    static const RE run(R"(Run number (\d+) of rule '(?:pdf)?latex)"), page(R"(\[(\d+)(?=[\]{<\s]))");
    const qsizetype end = m_output.size() - 8;         // non la coda: un segno può essere a metà
    if (end <= m_scanned)
        return;
    const QString chunk = QString::fromLatin1(m_output.mid(m_scanned, end - m_scanned));
    m_scanned = end;
    const int pagesBefore = m_pages, passBefore = m_pass;
    qsizetype from = 0;
    for (auto it = run.globalMatch(chunk); it.hasNext();) {   // una passata nuova: le pagine si contano da capo
        const auto m = it.next();
        m_pass = m.captured(1).toInt();
        m_pages = m_lastPage = 0;
        from = m.capturedEnd();
    }
    for (auto it = page.globalMatch(chunk.mid(from)); it.hasNext();) {
        const int n = it.next().captured(1).toInt();
        if (n == m_lastPage + 1 || (n == 1 && m_lastPage > 0)) {   // i numeri ripartono se cambia la numerazione
            m_lastPage = n;
            ++m_pages;
        }
    }
    if (m_pages == pagesBefore && m_pass == passBefore)
        return;
    const int expectedPasses = m_result.lesson.isEmpty() ? 2 : 1;
    const int passes = qMax(expectedPasses, m_pass), pass = qMax(1, m_pass);
    const double within = m_expectedPages > 0 ? qMin(0.98, m_pages / double(m_expectedPages)) : 0;
    emit progress(qMin(0.99, (pass - 1 + within) / passes),
                  passes > 1 ? QStringLiteral("passata %1, pagina %2").arg(pass).arg(m_pages)
                             : QStringLiteral("pagina %1").arg(m_pages));
}

void Compiler::stop()
{
    if (!isRunning())
        return;
    m_process->disconnect(this);                       // la fine la diciamo noi, una volta sola
    tools::stopGroup(m_process->processId());          // latexmk e i suoi figli, o pdflatex resta a scrivere in build/
    if (m_process->waitForFinished(3000)) {
        m_process->deleteLater();
        makeProcess();
    } else {
        abandon();
    }
    m_watch.stop();
    dropAux();                                         // interrotta a metà: il suo .aux può essere troncato
    m_result.stopped = true;
    m_result.ms = m_timer.elapsed();
    emit finished(m_result);
}

void Compiler::dropAux() const
{
    for (const char *ext : {".aux", ".fdb_latexmk"})   // senza .fdb_latexmk latexmk rifà tutte le passate
        QFile::remove(base() + QLatin1String(ext));
}

void Compiler::finish()
{
    // gli avvisi dal log dell'ultima passata (sullo schermo latexmk le mette tutte in fila)
    const QString log = files::read(base() + ".log");
    if (!m_retried && texlog::brokenAux(log)) {        // un .aux troncato: lo si butta e si ricompila, una volta
        m_retried = true;
        dropAux();
        launch();
        return;
    }
    m_result.issues = texlog::parse(log.isEmpty() ? QString::fromUtf8(m_output) : log);
    if (log.isEmpty() && !QFileInfo::exists(m_result.pdf))
        m_result.failure = QStringLiteral("La compilazione non è partita.");
    m_result.ms = m_timer.elapsed();
    emit finished(m_result);
}

}

namespace tx {

QStringList Compiler::arguments(const QString &lesson)
{
    // latexmk legge il .latexmkrc del progetto (le regole dell'utente); queste opzioni fissano ciò che serve per
    // leggere il log e trovare il risultato. -cd-: si compila dalla radice, qualunque cosa dica il .latexmkrc.
    QStringList args{"-pdf", "-cd-", "-interaction=nonstopmode", "-file-line-error", "-synctex=1"};
    if (lesson.isEmpty())
        return args << "-jobname=main" << QStringLiteral("-outdir=%1").arg(OUT) << Project::MAIN;
    // una lezione da sola: una passata (i rimandi alle altre lezioni restano ??)
    const QString stem = stemOf(lesson);
    return args << "-jobname=" + stem << QStringLiteral("-outdir=%1").arg(LESSONS) << "-e" << "$max_repeat=1"
                << QStringLiteral("%1/%2.main.tex").arg(LESSONS, stem);
}

QProcessEnvironment Compiler::environment()
{
    QProcessEnvironment env = tools::environment();
    env.insert("max_print_line", "100000");
    env.insert("error_line", "254");
    env.insert("half_error_line", "238");
    return env;
}

BuildResult Compiler::compileSync(const Project &project, const QString &lesson, JobControl *control)
{
    BuildResult result;
    result.lesson = lesson;
    result.pdf = pdfOf(project, lesson);
    const QString base = result.pdf.chopped(4);
    if (!lesson.isEmpty()) {
        QString error;
        const QString main = reducedMain(project, lesson, &error);
        if (!error.isEmpty()) {
            result.failure = error;
            return result;
        }
        files::write(base + ".main.tex", main);
    }
    QElapsedTimer timer;
    timer.start();
    QProcess process;
    tools::ownGroup(process);
    process.setProcessEnvironment(environment());
    process.setWorkingDirectory(project.root());
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    process.start("latexmk", arguments(lesson));
    if (!process.waitForStarted(10000)) {
        result.failure = QStringLiteral("Non trovo latexmk: serve una distribuzione TeX (MacTeX).");
        return result;
    }
    if (control)
        control->track(process.processId());
    while (!process.waitForFinished(200)) {
        if ((control && control->stopped()) || timer.elapsed() > 10 * 60 * 1000) {
            tools::stopGroup(process.processId());
            process.waitForFinished(2000);
            for (const char *ext : {".aux", ".fdb_latexmk"})   // interrotta a metà: il suo .aux può essere troncato
                QFile::remove(base + QLatin1String(ext));
            break;
        }
    }
    if (control) {
        control->untrack(process.processId());
        control->check();
    }
    result.issues = texlog::parse(files::read(base + ".log"));
    result.ms = timer.elapsed();
    return result;
}

}
