#include "Workspace.h"

#include "core/Figures.h"
#include "core/Files.h"
#include "core/Fix.h"
#include "core/Git.h"
#include "core/Layout.h"
#include "core/NewLesson.h"
#include "core/Pages.h"
#include "core/Plans.h"
#include "core/Profiles.h"
#include "core/Provenance.h"
#include "core/Reorganize.h"
#include "core/Review.h"
#include "core/Rules.h"
#include "core/Snippets.h"
#include "core/Sort.h"
#include "core/TexSource.h"
#include "core/StyleSwitch.h"
#include "core/SyncTex.h"

#include <QCoreApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QProcess>
#include <QThreadPool>

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonDocument>
#include <QDate>
#include <QJsonArray>
#include <QRegularExpression>
#include <QTime>
#include <QUrl>

using namespace tx;

namespace {
const char *const STATES = "build/stato-lezioni.json"; // lo stato dell'ultima compilazione di ogni lezione: si rifà

QString plural(int n, const char *one, const char *many)
{
    return QStringLiteral("%1 %2").arg(n).arg(QString::fromUtf8(n == 1 ? one : many));
}
}

Workspace::Workspace()
{
    m_refresh.setSingleShot(true);
    m_refresh.setInterval(150);
    connect(&m_refresh, &QTimer::timeout, this, [this] {
        refreshTree();
        emit filesChanged();
    });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_refresh, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_refresh, qOverload<>(&QTimer::start));

    connect(this, &Workspace::compilingChanged, this, &Workspace::busyChanged);
    connect(this, &Workspace::jobChanged, this, &Workspace::busyChanged);
    connect(this, &Workspace::busyChanged, this, &Workspace::nextProcedure, Qt::QueuedConnection);
    connect(this, &Workspace::pdfChanged, this, &Workspace::computeTags);
    connect(this, &Workspace::pdfRebuilt, this, &Workspace::computeTags);
    m_showTags = settings().value("vista/tag", false).toBool();
    connect(&m_compiler, &Compiler::started, this, [this] {
        m_progress = 0;
        m_progressLabel.clear();
        emit progressChanged();
        emit compilingChanged();
    });
    connect(&m_compiler, &Compiler::progress, this, [this](double fraction, const QString &label) {
        m_progress = fraction;
        m_progressLabel = label;
        emit progressChanged();
    });
    connect(&m_compiler, &Compiler::finished, this, &Workspace::onBuildFinished);
    connect(qApp, &QCoreApplication::aboutToQuit, this, &Workspace::closeProject);   // ferma i lavori, butta la cronologia
}

Workspace *Workspace::instance()
{
    static Workspace *workspace = new Workspace;
    return workspace;
}

Workspace *Workspace::create(QQmlEngine *, QJSEngine *)
{
    QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
    return instance();
}

QSettings Workspace::settings()
{
    const QString file = qEnvironmentVariable("TEXFIX_PREFERENZE");
    return file.isEmpty() ? QSettings() : QSettings(file, QSettings::IniFormat);
}

QString Workspace::localPath(const QUrl &url) { return url.isLocalFile() ? url.toLocalFile() : url.toString(); }

QVariantList Workspace::recents() const
{
    QVariantList out;
    for (const QString &path : settings().value("recenti").toStringList())
        if (Project::isProject(path))
            out << QVariantMap{{"path", path}, {"name", QFileInfo(path).fileName()},
                               {"where", QDir::toNativeSeparators(QFileInfo(path).path()).replace(QDir::homePath(), "~")}};
    return out;
}

void Workspace::remember(const QString &path)
{
    QSettings s = settings();
    QStringList list = s.value("recenti").toStringList();
    list.removeAll(path);
    list.prepend(path);
    s.setValue("recenti", list.mid(0, 12));
    emit recentsChanged();
}

QString Workspace::openFolder(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isDir())
        return QStringLiteral("La cartella non c'è più.");
    if (!Project::isProject(info.absoluteFilePath()))
        return QStringLiteral("prepara");
    open(info.absoluteFilePath());
    return {};
}

void Workspace::prepareFolder(const QString &path)
{
    const QStringList moved = Project::adopt(path);
    open(path);
    if (!moved.isEmpty())
        say(QStringLiteral("Cartella preparata: ciò che c'era è in «Da consultare» (%1).").arg(plural(moved.size(), "elemento", "elementi")));
}

QString Workspace::newProject(const QString &path, const QString &profile)
{
    if (const QString error = Project::create(path); !error.isEmpty())
        return error;
    QString problem;
    if (!profile.isEmpty())
        problem = profiles::apply(Project(path), profile);
    open(path);
    if (!problem.isEmpty())
        say(problem);
    return {};
}

QStringList Workspace::profiles() const { return profiles::list(); }

QString Workspace::saveProfile(const QString &name)
{
    const QString error = profiles::save(m_project, name);
    if (error.isEmpty())
        say(QStringLiteral("Profilo «%1» salvato: un progetto nuovo può partire da qui.").arg(name.trimmed()));
    return error;
}

void Workspace::removeProfile(const QString &name) { profiles::remove(name); }

void Workspace::open(const QString &path)
{
    closeProject();
    m_project = Project(path);
    m_project.complete();                              // un progetto a cui manca qualcosa si completa da sé
    m_compiler.setProject(m_project);
    m_history = std::make_unique<History>(m_project.root());
    // com'era alla chiusura: se da allora i file sono cambiati, il primo passo lo dice
    const QString closed = files::read(m_project.path("build/stato-alla-chiusura")).trimmed();
    if (!closed.isEmpty() && closed != m_history->digest())
        m_history->renameFirst(QStringLiteral("Modifiche fuori da Texfix"));
    loadSettings();

    const QJsonObject saved = QJsonDocument::fromJson(files::read(m_project.path(STATES)).toUtf8()).object();
    for (auto it = saved.begin(); it != saved.end(); ++it) {
        m_states[it.key()] = it.value().toObject().value("stato").toString();
        m_details[it.key()] = it.value().toObject().value("dettaglio").toString();
    }
    if (QFileInfo::exists(Compiler::pdfOf(m_project, {})))
        m_pdf = Compiler::pdfOf(m_project, {});
    remember(m_project.root());
    refreshTree();
    emit projectChanged();
    emit pdfChanged();
}

void Workspace::closeProject()
{
    if (!hasProject())
        return;
    if (m_jobThread) {                                 // chiuso il progetto (o il programma) tutto si ferma
        m_jobControl->stop();
        m_jobThread->wait();
        QCoreApplication::sendPostedEvents(this);      // la fine del lavoro, prima di smontare il progetto
    }
    m_compiler.stop();
    if (m_history)
        files::write(m_project.path("build/stato-alla-chiusura"), m_history->digest());
    m_history.reset();                                 // la cronologia vale finché il progetto è aperto
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    m_project = Project();
    m_compiler.setProject(m_project);
    m_lessons.clear();
    m_documentFiles.clear();
    m_queue.clear();
    m_documents.clear();
    m_images.clear();
    m_issues.clear();
    m_states.clear();
    m_details.clear();
    m_errorCount = 0;
    m_pdf.clear();
    emit treeChanged();
    emit issuesChanged();
    emit pdfChanged();
    emit projectChanged();
}

void Workspace::watch()
{
    QStringList paths{m_project.root(), m_project.path(Project::MAIN), m_project.path("src"), m_project.path("fonti/coda"),
                      m_project.path("docs"), m_project.path("imgs")};
    for (const QVariant &lesson : std::as_const(m_lessons))
        paths << m_project.path(lesson.toMap().value("file").toString());
    for (const QString &path : std::as_const(paths))
        if (QFileInfo::exists(path) && !m_watcher.files().contains(path) && !m_watcher.directories().contains(path))
            m_watcher.addPath(path);
}

void Workspace::refreshTree()
{
    if (!hasProject())
        return;
    QVariantList lessons, others;
    QStringList inMain;
    m_openDoubts = m_openDeclared = m_openFigures = m_openLayout = 0;
    // le fonti usate, e di chi sono: della lezione che le nomina nei suoi marcatori, o della cartella «lezione N»
    struct Used { QString file, name, bare, folder; bool taken = false; };
    QList<Used> used;
    for (QDirIterator it(m_project.path("fonti/usate"), QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
        const QString path = it.next();
        if (it.fileName().startsWith('.') || path.contains("/precedenti/"))
            continue;
        used << Used{m_project.relative(path), it.fileName(), files::bare(it.fileName()), QFileInfo(path).dir().dirName()};
    }
    std::sort(used.begin(), used.end(), [](const Used &a, const Used &b) { return a.name.localeAwareCompare(b.name) < 0; });
    QStringList texts;
    static const QRegularExpression marker(R"(^% @L\d+\.\d+ · (.+?) (?:pp?\.|r\.)\s*[\d-]+\s*$)", QRegularExpression::MultilineOption);
    const bool byTopic = m_settings.byTopic();
    for (const Lesson &lesson : m_project.lessons()) {
        inMain << lesson.file;
        const QString text = files::read(m_project.path(lesson.file));
        texts << text;
        QSet<QString> named;
        for (auto it = marker.globalMatch(text); it.hasNext();)
            named << files::bare(it.next().captured(1));
        QVariantList sources;
        for (Used &u : used) {
            if (named.contains(u.bare) || (!byTopic && lesson.number > 0 && u.folder == QStringLiteral("lezione %1").arg(lesson.number))) {
                u.taken = true;
                sources << QVariantMap{{"file", u.file}, {"name", u.name}};
            }
        }
        // ciò che c'è da rivedere nella lezione: quanto resta, e se c'è qualcosa da mostrare
        int allDoubts = 0, allDeclared = 0, allFigures = 0;
        review::Counts open;
        for (const review::Doubt &d : review::doubts(m_project, lesson.file)) {
            ++allDoubts;
            open.doubts += d.open;
        }
        for (const review::Declared &d : review::declared(m_project, lesson.file)) {
            ++allDeclared;
            open.declared += d.state == "aperta";
        }
        allFigures = int(text.count("% img "));
        const int allMoves = int(layout::moves(m_project, lesson.file).size()), movesLeft = layout::toReview(m_project, lesson.file);
        QVariantList rows;
        auto row = [&rows](const QString &name, const QString &panel, int all, int left) {
            if (all > 0)
                rows << QVariantMap{{"name", name}, {"panel", panel}, {"detail", left > 0 ? QString::number(left) : QStringLiteral("✓")}};
        };
        const bool done = m_settings.isDone(lesson.file);
        if (!done) {
            m_openDoubts += open.doubts;
            m_openDeclared += open.declared;
        }
        const int figuresLeft = figures::pending(m_project, lesson.file);
        if (!done) {
            m_openFigures += figuresLeft;
            // da impaginare solo ciò che compila pulito; le mosse da rivedere contano sempre
            m_openLayout += layout::toReview(m_project, lesson.file)
                            + (m_states.value(lesson.file) == "good" && layout::needed(m_project, lesson.file) ? 1 : 0);
        }
        const int toReview = done ? 0 : open.doubts + open.declared + figuresLeft;
        QString state = m_states.value(lesson.file, "idle"), detail = m_details.value(lesson.file);
        if (toReview > 0 && state != "bad") {          // pronta solo quando non resta niente da decidere
            state = "warn";
            detail = QStringLiteral("%1 da rivedere").arg(toReview);
        }
        row(QStringLiteral("Dubbi"), "rivedi:dubbi", allDoubts, done ? 0 : open.doubts);
        row(QStringLiteral("Correzioni"), "rivedi:correzioni", allDeclared, done ? 0 : open.declared);
        row(QStringLiteral("Impaginazione"), "rivedi:impaginazione", allMoves, done ? 0 : movesLeft);
        row(QStringLiteral("Figure"), "rivedi:figure", allFigures, done ? 0 : figuresLeft);
        lessons << QVariantMap{{"file", lesson.file},
                               {"rows", rows}, {"sources", sources},
                               {"number", lesson.number},
                               {"date", lesson.date},
                               {"title", lesson.title.isEmpty() ? QFileInfo(lesson.file).completeBaseName() : lesson.title},
                               {"state", state},
                               {"done", m_settings.isDone(lesson.file)},
                               {"detail", detail}};
    }
    for (const char *file : {Project::MAIN, Project::PACKAGE})
        if (QFileInfo::exists(m_project.path(file)))
            others << QVariantMap{{"file", QString::fromUtf8(file)}, {"name", QString::fromUtf8(file)}};
    for (const QString &name : QDir(m_project.path("src")).entryList({"*.tex"}, QDir::Files, QDir::Name))
        if (!inMain.contains("src/" + name))
            others << QVariantMap{{"file", "src/" + name}, {"name", name}};
    // i file di una cartella, anche nelle sue sottocartelle, in ordine di nome
    auto listing = [this](const char *dir) {
        QVariantList out;
        const QString base = m_project.path(dir);
        for (QDirIterator it(base, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
            const QString path = it.next();
            if (!it.fileName().startsWith('.'))
                out << QVariantMap{{"file", m_project.relative(path)}, {"name", QDir(base).relativeFilePath(path)}};
        }
        std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
            return a.toMap().value("name").toString().localeAwareCompare(b.toMap().value("name").toString()) < 0;
        });
        return out;
    };
    const int changeCount = int(git::changes(m_project).size());
    const bool gitChanged = changeCount != m_changeCount;
    m_changeCount = changeCount;
    QVariantList jobs;
    for (const plans::Info &info : plans::open(m_project))
        jobs << QVariantMap{{"id", info.id}, {"title", info.title}, {"state", info.state}, {"kind", plans::load(m_project, info.id).value("tipo").toString()}};
    QVariantList outcomes;                             // «AAAA-MM-GG HH.MM — titolo.md»: il titolo a sinistra, quando a destra
    for (const QString &name : QDir(m_project.path("salvataggi/esiti")).entryList({"*.md"}, QDir::Files, QDir::Name | QDir::Reversed)) {
        const QString stem = QFileInfo(name).completeBaseName();
        outcomes << QVariantMap{{"file", "salvataggi/esiti/" + name}, {"name", stem.section(" — ", 1)}, {"detail", stem.section(" — ", 0, 0)}};
    }
    QVariantList strays;                               // le fonti usate che nessuna lezione nomina
    for (const Used &u : used)
        if (!u.taken)
            strays << QVariantMap{{"file", u.file}, {"name", u.name}};
    const QVariantList queue = listing("fonti/coda"), documents = listing("docs");
    QVariantList images = listing("imgs");             // di ogni immagine, la lezione che la usa
    const QList<Lesson> known = m_project.lessons();
    for (QVariant &v : images) {
        QVariantMap image = v.toMap();
        const QString stem = QFileInfo(image.value("file").toString()).completeBaseName();
        for (qsizetype i = 0; i < texts.size() && i < known.size(); ++i)
            if (texts[i].contains(stem)) {
                image["detail"] = QFileInfo(known[i].file).completeBaseName();
                break;
            }
        v = image;
    }
    if (gitChanged || strays != m_strays || outcomes != m_outcomes || jobs != m_jobs || lessons != m_lessons || others != m_documentFiles || queue != m_queue || documents != m_documents || images != m_images) {
        m_jobs = jobs;
        m_outcomes = outcomes;
        m_strays = strays;
        m_lessons = lessons;
        m_documentFiles = others;
        m_queue = queue;
        m_documents = documents;
        m_images = images;
        emit treeChanged();
    }
    watch();
}

QString Workspace::read(const QString &file) const { return files::read(m_project.path(file)); }

bool Workspace::save(const QString &file, const QString &text)
{
    if (files::write(m_project.path(file), text)) {
        if (!m_settings.style.isEmpty() && file == m_settings.style + ".sty")
            QTimer::singleShot(0, this, &Workspace::styleFileChanged);      // lo stile cambiato a mano: la descrizione lo segue
        return true;
    }
    say(QStringLiteral("Non riesco a salvare «%1».").arg(file));
    return false;
}

bool Workspace::isLesson(const QString &file) const { return m_project.inputs().contains(file); }

void Workspace::compileFile(const QString &file)
{
    if (!hasProject() || compiling())
        return;
    if (isLesson(file))
        m_compiler.compileLesson(file);
    else
        m_compiler.compileDocument();
}

void Workspace::compileDocument()
{
    if (hasProject() && !compiling())
        m_compiler.compileDocument();
}

void Workspace::stop()
{
    if (m_jobControl)
        m_jobControl->stop();                          // il lavoro si chiude da sé, subito: convertFinished
    m_compiler.stop();
}

void Workspace::say(const QString &text)
{
    if (text.isEmpty())
        return;
    m_console << QTime::currentTime().toString("HH:mm:ss  ") + text;
    emit consoleChanged();
    emit message(text);
}

void Workspace::onBuildFinished(const BuildResult &result)
{
    emit compilingChanged();
    if (result.stopped) {
        say(QStringLiteral("Compilazione fermata."));
        emit buildFinished();
        return;
    }
    if (!result.failure.isEmpty()) {
        m_issues = {QVariantMap{{"kind", "error"}, {"file", QString()}, {"line", 0}, {"text", result.failure}, {"detail", QString()}}};
        m_errorCount = 1;
        emit issuesChanged();
        say(result.failure);
        emit buildFinished();
        return;
    }

    // gli avvisi, e per ogni lezione quanti errori e quante righe fuori misura
    struct Count { int errors = 0, rows = 0; };
    QHash<QString, Count> counts;
    m_issues.clear();
    m_errorCount = 0;
    int rows = 0;
    for (const Issue &issue : result.issues) {
        const bool error = issue.kind == Issue::Error;
        (error ? counts[issue.file].errors : counts[issue.file].rows)++;
        (error ? m_errorCount : rows)++;
        m_issues << QVariantMap{{"kind", error ? "error" : issue.kind == Issue::WideLine ? "wide" : "loose"},
                                {"file", issue.file},
                                {"line", issue.line},
                                {"text", issue.text},
                                {"detail", issue.amount}};
    }
    emit issuesChanged();

    // il documento intero aggiorna tutte le lezioni, una lezione da sola aggiorna la sua
    const QStringList touched = result.lesson.isEmpty() ? m_project.inputs() : QStringList{result.lesson};
    for (const QString &file : touched) {
        const Count c = counts.value(file);
        m_states[file] = c.errors ? "bad" : c.rows ? "warn" : "good";
        m_details[file] = c.errors ? plural(c.errors, "errore", "errori") : c.rows ? plural(c.rows, "riga fuori misura", "righe fuori misura") : QString();
    }
    QJsonObject saved;
    for (auto it = m_states.cbegin(); it != m_states.cend(); ++it)
        saved[it.key()] = QJsonObject{{"stato", it.value()}, {"dettaglio", m_details.value(it.key())}};
    files::write(m_project.path(STATES), QString::fromUtf8(QJsonDocument(saved).toJson()));
    refreshTree();

    if (QFileInfo::exists(result.pdf)) {
        if (m_pdf != result.pdf) {
            m_pdf = result.pdf;
            emit pdfChanged();
        } else {
            emit pdfRebuilt();
        }
    }
    const QString what = result.lesson.isEmpty() ? QStringLiteral("Documento compilato") : QStringLiteral("Lezione compilata");
    const QString seconds = QLocale(QLocale::Italian).toString(result.ms / 1000.0, 'f', 1);
    if (m_errorCount)
        say(QStringLiteral("%1 con %2.").arg(what, plural(m_errorCount, "errore", "errori")));
    else if (rows)
        say(QStringLiteral("%1 in %2 s, %3.").arg(what, seconds, plural(rows, "riga fuori misura", "righe fuori misura")));
    else
        say(QStringLiteral("%1 in %2 s.").arg(what, seconds));
    emit buildFinished();
}

void Workspace::watchFile(const QString &file)
{
    const QString path = m_project.path(file);
    if (hasProject() && QFileInfo::exists(path) && !m_watcher.files().contains(path))
        m_watcher.addPath(path);
}

void Workspace::openExternally(const QString &file) const
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_project.path(file)));
}

QVariantMap Workspace::toPdf(const QString &file, int line) const
{
    // un PDF più vecchio del file porterebbe alla riga sbagliata
    if (m_pdf.isEmpty() || QFileInfo(m_pdf).lastModified() < QFileInfo(m_project.path(file)).lastModified())
        return {{"ok", false}, {"stale", true}};
    synctex::Spot spot;                                // una riga che non lascia segno (un commento, un comando): una vicina
    for (const int delta : {0, 1, 2, -1, 3, -2, 4, 5, 6}) {
        if (line + delta < 1)
            continue;
        spot = synctex::toPdf(m_project.root(), m_pdf, file, line + delta);
        if (spot.ok)
            break;
    }
    return {{"ok", spot.ok}, {"page", spot.page}, {"x", spot.x}, {"y", spot.y}, {"height", spot.height}};
}

QVariantMap Workspace::toSource(int page, qreal x, qreal y) const
{
    const synctex::Spot spot = synctex::toSource(m_project.root(), m_pdf, page, x, y);
    return {{"ok", spot.ok}, {"file", spot.file}, {"line", spot.line}};
}

void Workspace::moved(const History::Outcome &outcome)
{
    say(outcome.message);
    if (!outcome.ok)
        return;
    loadSettings();                                    // anche le impostazioni possono essere tornate indietro
    refreshTree();
    emit filesChanged();                               // gli editor rileggono ciò che è cambiato
}

void Workspace::undo()
{
    if (jobRunning() || m_agentRunning)
        return say(QStringLiteral("Aspetta la fine del lavoro."));
    if (m_history)
        moved(m_history->undo());
}

void Workspace::redo()
{
    if (jobRunning() || m_agentRunning)
        return say(QStringLiteral("Aspetta la fine del lavoro."));
    if (m_history)
        moved(m_history->redo());
}

void Workspace::goToStep(int index)
{
    if (m_history)
        moved(m_history->goTo(index));
}

QString Workspace::undoTitle() const { return m_history ? m_history->undoTitle() : QString(); }
QString Workspace::redoTitle() const { return m_history ? m_history->redoTitle() : QString(); }

QVariantList Workspace::historySteps() const
{
    QVariantList out;
    if (!m_history)
        return out;
    const QList<History::Step> steps = m_history->steps();
    for (int i = 0; i < steps.size(); ++i)
        out << QVariantMap{{"title", steps[i].title}, {"when", steps[i].when}, {"current", i == m_history->cursor()}};
    return out;
}

void Workspace::checkpoint(const QString &title)
{
    if (m_history)
        m_history->record(title);
}

void Workspace::loadSettings()
{
    m_settings = Settings::load(m_project);
    Style style = Style::classic();
    if (!m_settings.style.isEmpty()) {
        bool ok = false;
        const Style described = Style::load(m_project.path(Style::descriptionFile(m_settings.style)), &ok);
        if (ok)
            style = described;
    }
    emit settingsChanged();
    if (style != m_style) {
        m_style = style;
        emit styleChanged();
    }
}

QVariantList Workspace::settingChoices(const QString &key) const
{
    QVariantList out;
    for (const Settings::Choice &choice : Settings::choices(key))
        out << QVariantMap{{"value", choice.value}, {"label", choice.label}};
    return out;
}

QString Workspace::setting(const QString &key) const { return m_settings.value(key); }

void Workspace::setSetting(const QString &key, const QString &value)
{
    if (!hasProject() || m_settings.value(key) == value || !m_settings.setValue(key, value))
        return;
    m_settings.save(m_project);
    emit settingsChanged();
}

void Workspace::setProjectRules(const QStringList &rules)
{
    QStringList clean;
    for (const QString &rule : rules)
        if (!rule.trimmed().isEmpty())
            clean << rule.trimmed();
    if (!hasProject() || clean == m_settings.rules)
        return;
    m_settings.rules = clean;
    m_settings.save(m_project);
    emit settingsChanged();
}

void Workspace::setDone(const QString &lesson, bool done)
{
    if (!hasProject() || m_settings.isDone(lesson) == done)
        return;
    checkpoint(QStringLiteral("Modifiche a mano"));
    m_settings.setDone(lesson, done);
    m_settings.save(m_project);
    checkpoint(done ? QStringLiteral("Lezione fatta") : QStringLiteral("Lezione non più fatta"));
    refreshTree();
    emit settingsChanged();
}

QVariantList Workspace::ruleSections() const
{
    QVariantList out;
    for (const rules::Section &s : rules::sections())
        out << QVariantMap{{"id", s.id}, {"title", s.title}, {"text", s.text}, {"custom", s.custom}};
    return out;
}

void Workspace::setRuleText(const QString &id, const QString &text) { rules::setText(id, text); }
void Workspace::resetRule(const QString &id) { rules::reset(id); }

QVariantMap Workspace::importStyle(const QString &styPath)
{
    if (!hasProject())
        return {{"error", "Apri prima un progetto."}};
    const styleswitch::Imported in = styleswitch::import(m_project, styPath);
    QVariantList environments;
    for (const Style::Environment &env : in.style.environments)
        environments << QVariantMap{{"name", env.name}, {"role", env.role}, {"color", env.color}};
    return {{"error", in.error}, {"name", in.style.name}, {"described", in.described}, {"environments", environments}};
}

QVariantList Workspace::styleRoles() const
{
    QVariantList out;
    for (const Style::Role &role : Style::roles())
        out << QVariantMap{{"value", role.value}, {"label", role.label}};
    return out;
}

void Workspace::setStyleRoles(const QString &name, const QVariantMap &roles)
{
    const QString path = m_project.path(Style::descriptionFile(name));
    bool ok = false;
    Style style = Style::load(path, &ok);
    if (!ok)
        return;
    for (Style::Environment &env : style.environments)
        if (roles.contains(env.name))
            env.role = roles.value(env.name).toString();
    style.save(path);
}

void Workspace::applyStyle(const QString &name) { applyStyleWith(name, {}); }

void Workspace::applyStyleWith(const QString &name, const QVariantMap &choices)
{
    if (!hasProject() || busy())
        return say(QStringLiteral("Aspetta la fine del lavoro in corso."));
    // ciò che nel nuovo stile non ha un corrispondente è una domanda, prima di toccare le lezioni
    if (choices.isEmpty()) {
        QVariantList questions;
        for (const styleswitch::Orphan &o : styleswitch::orphans(m_project, name)) {
            QString role = o.role;
            for (const Style::Role &r : Style::roles())
                if (r.value == o.role)
                    role = r.label;
            questions << QVariantMap{{"name", o.name}, {"role", role}, {"options", o.options}};
        }
        if (!questions.isEmpty()) {
            emit styleQuestions(name, questions);
            return;
        }
    }
    QHash<QString, QString> chosen;
    for (auto it = choices.constBegin(); it != choices.constEnd(); ++it)
        chosen.insert(it.key(), it.value().toString());
    checkpoint(QStringLiteral("Modifiche a mano"));
    const styleswitch::Outcome outcome = styleswitch::apply(m_project, name, chosen);
    checkpoint(name.isEmpty() ? QStringLiteral("Ritorno a nessuno stile") : QStringLiteral("Stile «%1»").arg(name));
    loadSettings();
    refreshTree();
    emit filesChanged();
    if (!outcome.error.isEmpty())
        return say(outcome.error);
    QString text = name.isEmpty() ? QStringLiteral("Il progetto è senza stile") : QStringLiteral("Il progetto usa lo stile «%1»").arg(name);
    text += QStringLiteral(": %1.").arg(plural(outcome.lessons, "lezione riscritta", "lezioni riscritte"));
    if (!outcome.unmatched.isEmpty())
        text += QStringLiteral(" Senza corrispondente nel nuovo stile, rimasti com'erano: %1.").arg(outcome.unmatched.join(", "));
    if (!outcome.renamed.isEmpty())
        text += QStringLiteral(" Macro che hanno cambiato nome: %1.").arg(outcome.renamed.join(", "));
    if (!outcome.carried.isEmpty())
        text += QStringLiteral(" Macro dello stile di prima tenute nel documento: %1.").arg(outcome.carried.join(", "));
    say(text);
    m_compiler.compileDocument();                      // si rivede subito com'è venuto
}

namespace {
QVariantList environmentsOf(const Style &style, const QStringList &only = {})
{
    QVariantList out;
    for (const Style::Environment &env : style.environments)
        if (only.isEmpty() || only.contains(env.name))
            out << QVariantMap{{"name", env.name}, {"role", env.role}, {"color", env.color}};
    return out;
}
}

void Workspace::proposeStyle(const QString &name)
{
    if (!hasProject())
        return;
    if (busy())
        return say(QStringLiteral("Aspetta la fine del lavoro in corso."));
    startJob(QStringLiteral("Leggo lo stile"), [this, name, project = m_project, quality = m_settings.quality](JobControl *control) {
        QString note;
        try {
            control->progress(0.3, QStringLiteral("a che cosa serve ogni ambiente"));
            note = styleswitch::propose(project, quality, name, control);
        } catch (const Stopped &) {
            note = QStringLiteral("Lettura fermata: i ruoli vengono dai nomi.");
        }
        return std::function<void()>([this, name, note] {
            if (!note.isEmpty())
                say(note);
            bool ok = false;
            const Style style = Style::load(m_project.path(Style::descriptionFile(name)), &ok);
            emit styleProposed(name, environmentsOf(style));
        });
    });
}

void Workspace::styleFileChanged()
{
    if (!hasProject() || m_settings.style.isEmpty())
        return;
    const QStringList fresh = styleswitch::refresh(m_project);
    loadSettings();
    if (!fresh.isEmpty())                              // un ambiente nuovo nello stile: a che cosa serve?
        emit styleProposed(m_style.name, environmentsOf(m_style, fresh));
}

QVariantList Workspace::libraryStyles() const
{
    QVariantList out;
    for (const QString &name : styleswitch::library())
        out << QVariantMap{{"name", name}, {"bundled", styleswitch::isBundled(name)}};
    return out;
}

QString Workspace::addStyleToLibrary(const QString &styPath) { return styleswitch::addToLibrary(styPath); }

void Workspace::removeLibraryStyle(const QString &name) { styleswitch::removeFromLibrary(name); }

QVariantList Workspace::convertSources() const
{
    QVariantList out;
    if (!hasProject())
        return out;
    for (const Convert::Source &s : Convert::queue(m_project)) {
        const bool can = s.kind != "altro";
        const auto [path, date] = texsource::parseSpec(s.file);
        const QString detail = s.kind == "pagine" ? plural(s.size, "pagina", "pagine")
                               : s.kind == "testo" ? plural(s.size, "riga", "righe")
                               : s.kind == "tex" ? plural(s.size, "riga", "righe") : QStringLiteral("non si converte");
        out << QVariantMap{{"file", s.file}, {"name", QFileInfo(path).fileName() + (date.isValid() ? QStringLiteral(" · lezione del %1").arg(date.toString("dd/MM/yyyy")) : QString())}, {"detail", detail}, {"convertible", can}};
    }
    return out;
}

double Workspace::convertEstimate() const
{
    return hasProject() ? Convert::estimate(m_settings.quality, Convert::queue(m_project)) : 0;
}

void Workspace::resume() { convert(Convert::interrupted(m_project)); }

void Workspace::convert(const QStringList &sources) { convert(sources, {}); }

void Workspace::convert(const QStringList &sources, const QHash<QString, Convert::Wanted> &wanted)
{
    if (!hasProject() || sources.isEmpty())
        return say(QStringLiteral("Non c'è niente da convertire."));
    if (busy())
        return say(QStringLiteral("C'è già un lavoro in corso."));
    checkpoint(QStringLiteral("Modifiche a mano"));
    startJob(QStringLiteral("Converti"), [this, project = m_project, settings = m_settings, style = m_style, sources, wanted](JobControl *control) {
        const Convert::Result result = Convert(project, settings, style, control).run(sources, wanted);
        return std::function<void()>([this, result] { convertFinished(result); });
    });
}

// Un lavoro: gira qui dentro, in un thread suo (nessun altro processo, nessun file che fa da tramite); ciò che
// restituisce gira alla fine nel thread della finestra.
void Workspace::startJob(const QString &title, std::function<std::function<void()>(JobControl *)> work)
{
    m_jobControl = std::make_unique<JobControl>();
    m_jobControl->onProgress = [this](double fraction, const QString &label) {
        QMetaObject::invokeMethod(this, [this, fraction, label] {
            m_jobProgress = fraction;
            m_jobLabel = label;
            emit jobProgressChanged();
        });
    };
    m_jobControl->onLog = [this](const QString &line) {
        QMetaObject::invokeMethod(this, [this, line] {
            m_jobLog << QTime::currentTime().toString("HH:mm:ss ") + line;
            emit jobLogChanged();
            m_console << QTime::currentTime().toString("HH:mm:ss  ") + line;
            emit consoleChanged();
        });
    };
    m_jobTitle = title;
    m_jobProgress = 0;
    m_jobLabel.clear();
    m_jobLog.clear();
    emit jobLogChanged();
    m_jobThread = QThread::create([this, work = std::move(work), control = m_jobControl.get()] {
        const std::function<void()> finish = work(control);
        QMetaObject::invokeMethod(this, [this, finish] {
            m_jobThread->wait();
            m_jobThread->deleteLater();
            m_jobThread = nullptr;
            const bool stopped = m_jobControl && m_jobControl->stopped();
            m_jobControl.reset();
            m_recent.prepend(QVariantMap{{"title", m_jobTitle}, {"when", QTime::currentTime().toString("HH:mm")}, {"stopped", stopped}});
            while (m_recent.size() > 12)
                m_recent.removeLast();
            finish();
            emit jobChanged();
        });
    });
    m_jobThread->start();
    emit jobChanged();
    emit jobProgressChanged();
}

void Workspace::convertFinished(const Convert::Result &result)
{
    checkpoint(QStringLiteral("Converti"));            // tutto il lavoro è un passo solo
    loadSettings();                                    // i pacchetti chiesti dalle figure
    refreshTree();
    emit filesChanged();

    const QLocale italian(QLocale::Italian);
    QStringList lines;
    for (const Convert::Lesson &lesson : result.lessons) {
        if (lesson.file.isEmpty()) {
            if (!lesson.failure.isEmpty())
                lines << QStringLiteral("%1: non convertita (%2).").arg(lesson.source, lesson.failure);
            continue;
        }
        QStringList open;
        if (lesson.doubts)
            open << plural(lesson.doubts, "dubbio", "dubbi");
        if (lesson.corrections)
            open << plural(lesson.corrections, "aggiunta o correzione", "fra aggiunte e correzioni");
        if (lesson.figures)
            open << plural(lesson.figures, "figura da inserire", "figure da inserire");
        if (lesson.layoutOpen)
            open << plural(lesson.layoutOpen, "mossa di impaginazione da rivedere", "mosse di impaginazione da rivedere");
        if (lesson.errors)
            open << plural(lesson.errors, "errore", "errori");
        if (lesson.rows)
            open << plural(lesson.rows, "riga fuori misura", "righe fuori misura");
        lines << QStringLiteral("Lezione %1 · %2: %3.").arg(lesson.number).arg(lesson.title, open.isEmpty() ? QStringLiteral("niente da rivedere") : open.join(", "));
    }
    if (result.stopped)
        lines << QStringLiteral("Lavoro fermato: ciò che è fatto resta, e si riprende da lì con «Riprendi».");
    if (!result.failure.isEmpty())
        lines << result.failure;
    if (!m_outcomeHead.isEmpty())                      // una riga in testa: che cosa si è fatto delle fonti
        lines.prepend(m_outcomeHead);
    m_outcomeHead.clear();
    emit outcomeReady(QVariantMap{{"title", result.stopped ? QStringLiteral("Converti: fermato") : QStringLiteral("Converti: esito")},
                                  {"lines", lines},
                                  {"cost", QStringLiteral("%1 $").arg(italian.toString(result.cost, 'f', 2))},
                                  {"minutes", italian.toString(result.minutes, 'f', 1)}});
    say(QStringLiteral("Converti: %1.").arg(plural(result.written(), "lezione scritta", "lezioni scritte")));
    if (result.written() > 0)
        m_compiler.compileDocument();                  // dà lo stato alle lezioni nuove
}

QVariantList Workspace::reviewItems(const QString &section, bool summary) const
{
    QVariantList out;
    if (!hasProject())
        return out;
    // dove sta una fonte usata, per mostrarne la pagina
    auto sourcePath = [this](const QString &name) {
        for (QDirIterator it(m_project.path("fonti/usate"), {name}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();)
            return it.next();
        return QString();
    };
    for (const Lesson &lesson : m_project.lessons()) {
        const bool done = m_settings.isDone(lesson.file);
        if (done && !summary)
            continue;
        QVariantList rows;
        int open = 0;
        if (section == "dubbi") {
            for (const review::Doubt &d : review::doubts(m_project, lesson.file)) {
                open += d.open;
                if (!d.open && !summary)
                    continue;
                QVariantList readings;
                for (const review::Reading &r : d.readings)
                    readings << QVariantMap{{"text", r.text}, {"latex", r.latex}};
                rows << QVariantMap{{"type", "doubt"}, {"lesson", d.lesson}, {"id", d.id}, {"what", d.what}, {"latex", d.latex},
                                    {"readings", readings}, {"open", d.open}, {"chosen", d.chosen}, {"how", d.how}, {"line", d.line},
                                    {"pending", d.pending}, {"why", d.why},
                                    {"before", d.pending && d.how == "corretta" ? review::change(m_project, d.lesson, d.id).before : QStringList()},
                                    {"after", d.pending && d.how == "corretta" ? review::change(m_project, d.lesson, d.id).after : QStringList()},
                                    {"page", d.page}, {"source", d.source},
                                    {"sourcePath", d.source.endsWith(".pdf", Qt::CaseInsensitive) ? sourcePath(d.source) : QString()}};
            }
        } else {
            for (const review::Declared &d : review::declared(m_project, lesson.file)) {
                open += d.state == "aperta";
                if (d.state != "aperta" && !summary)
                    continue;
                rows << QVariantMap{{"type", "declared"}, {"lesson", d.lesson}, {"kind", d.kind}, {"text", d.text}, {"hash", d.hash},
                                    {"state", d.state}, {"line", d.line}};
            }
            // anche gli errori e le righe fuori misura dell'ultima compilazione: portano a «Sistema»
            if (!done)
                for (const QVariant &v : m_issues) {
                    const QVariantMap issue = v.toMap();
                    if (issue.value("file").toString() != lesson.file)
                        continue;
                    ++open;
                    rows << QVariantMap{{"type", "issue"}, {"lesson", lesson.file}, {"kind", issue.value("kind")}, {"text", issue.value("text")},
                                        {"detail", issue.value("detail")}, {"line", issue.value("line")}};
                }
        }
        if (rows.isEmpty())
            continue;
        out << QVariantMap{{"type", "lesson"}, {"lesson", lesson.file}, {"open", open}, {"done", done},
                           {"title", lesson.number > 0 ? QStringLiteral("Lezione %1 · %2").arg(lesson.number).arg(lesson.title) : lesson.title}};
        out += rows;
    }
    return out;
}

void Workspace::decided(const QString &lesson, const QString &title, const QString &error)
{
    if (!error.isEmpty())
        return say(error);
    if (m_settings.isDone(lesson)) {                   // rimettere mano a una lezione fatta le toglie «fatta»
        m_settings.setDone(lesson, false);
        m_settings.save(m_project);
        emit settingsChanged();
    }
    checkpoint(title);
    refreshTree();
    emit filesChanged();
}

void Workspace::chooseReading(const QString &lesson, const QString &id, int reading)
{
    checkpoint(QStringLiteral("Modifiche a mano"));
    decided(lesson, QStringLiteral("Dubbio deciso"), review::choose(m_project, lesson, id, reading));
}

void Workspace::reopenDoubt(const QString &lesson, const QString &id, bool revert)
{
    checkpoint(QStringLiteral("Modifiche a mano"));
    decided(lesson, QStringLiteral("Dubbio riaperto"), review::reopen(m_project, lesson, id, revert));
}

void Workspace::keepDeclared(const QString &lesson, const QString &hash, bool keep)
{
    checkpoint(QStringLiteral("Modifiche a mano"));
    review::keep(m_project, lesson, hash, keep);
    decided(lesson, keep ? QStringLiteral("Aggiunta tenuta") : QStringLiteral("Aggiunta riaperta"), {});
}

void Workspace::keepAllDeclared(const QString &lesson)
{
    checkpoint(QStringLiteral("Modifiche a mano"));
    review::keepAll(m_project, lesson);
    decided(lesson, QStringLiteral("Tenute tutte"), {});
}

void Workspace::dropDeclared(const QString &lesson, const QString &hash)
{
    checkpoint(QStringLiteral("Modifiche a mano"));
    decided(lesson, QStringLiteral("Aggiunta tolta"), review::drop(m_project, lesson, hash));
}

void Workspace::restoreDeclared(const QString &lesson, const QString &hash)
{
    checkpoint(QStringLiteral("Modifiche a mano"));
    decided(lesson, QStringLiteral("Aggiunta rimessa"), review::restore(m_project, lesson, hash));
}

QVariantList Workspace::figureItems() const
{
    QVariantList out;
    if (!hasProject())
        return out;
    const QList<figures::Figure> all = figures::all(m_project);
    for (const Lesson &lesson : m_project.lessons()) {
        const QString text = files::read(m_project.path(lesson.file));
        QVariantList rows;
        int n = 0;
        for (const figures::Figure &f : all) {
            const qsizetype at = text.indexOf("% img " + f.id);
            if (at < 0)
                continue;
            rows << QVariantMap{{"type", "figure"}, {"id", f.id}, {"lesson", lesson.file}, {"name", QStringLiteral("Figura %1").arg(++n)},
                                {"description", f.description}, {"state", f.state}, {"note", f.note}, {"source", f.source}, {"page", f.page},
                                {"line", int(text.left(at).count('\n')) + 1}, {"preview", figures::preview(m_project, f)}};
        }
        if (rows.isEmpty())
            continue;
        out << QVariantMap{{"type", "lesson"}, {"lesson", lesson.file},
                           {"title", lesson.number > 0 ? QStringLiteral("Lezione %1 · %2").arg(lesson.number).arg(lesson.title) : lesson.title}};
        out += rows;
    }
    return out;
}

void Workspace::redoFigure(const QString &id, const QString &how)
{
    if (!hasProject())
        return;
    if (busy())
        return say(QStringLiteral("C'è già un lavoro in corso."));
    checkpoint(QStringLiteral("Modifiche a mano"));
    const bool crop = how == "ritaglia";
    startJob(crop ? QStringLiteral("Ritaglio la figura") : QStringLiteral("Ridisegno la figura"),
             [this, id, crop, project = m_project, settings = m_settings, style = m_style](JobControl *control) {
                 QString error;
                 double cost = 0;
                 try {
                     const figures::Figure figure = figures::find(project, id);
                     // la pagina della fonte, dalle fonti usate
                     QString source;
                     for (QDirIterator it(project.path("fonti"), {figure.source}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();)
                         source = it.next();
                     QString page;
                     if (!source.isEmpty())
                         page = Pages::read(source, project.path("build/lavori/figure/pagine/" + figure.source)).full.value(figure.page - 1);
                     Model model(settings.quality, project.root(), control);
                     error = crop ? figures::crop(project, figure, page) : figures::draw(project, settings, style, model, figure, page);
                     cost = model.cost();
                 } catch (const Stopped &) {
                     error = QStringLiteral("Lavoro fermato.");
                 } catch (const std::exception &e) {
                     error = QString::fromUtf8(e.what());
                 }
                 return std::function<void()>([this, error, crop, cost] {
                     checkpoint(crop ? QStringLiteral("Figura ritagliata") : QStringLiteral("Figura ridisegnata"));
                     loadSettings();
                     refreshTree();
                     emit filesChanged();
                     const QString paid = cost > 0 ? QStringLiteral(" Costo: %1 $.").arg(QLocale(QLocale::Italian).toString(cost, 'f', 2)) : QString();
                     say(error.isEmpty() ? QStringLiteral("Figura inserita.") + paid : QStringLiteral("La figura non è stata inserita: %1").arg(error) + paid);
                 });
             });
}

void Workspace::fixAll(const QString &only)
{
    if (!hasProject())
        return;
    if (busy())
        return say(QStringLiteral("C'è già un lavoro in corso."));
    // le lezioni con errori o righe fuori misura nell'ultima compilazione; quelle fatte si saltano
    QStringList lessons;
    for (const QString &file : m_project.inputs())
        if ((only.isEmpty() || file == only) && !m_settings.isDone(file) && (m_states.value(file) == "bad" || m_states.value(file) == "warn") && !m_details.value(file).isEmpty())
            lessons << file;
    if (lessons.isEmpty())
        return say(QStringLiteral("Non c'è niente da sistemare."));
    checkpoint(QStringLiteral("Modifiche a mano"));
    startJob(QStringLiteral("Sistemo errori e righe"), [this, lessons, project = m_project, settings = m_settings, style = m_style](JobControl *control) {
        Model model(settings.quality, project.root(), control);
        int errors = 0, rows = 0, errorsLeft = 0, rowsLeft = 0;
        QString failure;
        try {
            for (int i = 0; i < lessons.size(); ++i) {
                control->progress(double(i) / lessons.size(), QFileInfo(lessons[i]).fileName());
                const fix::Result r = fix::lesson(project, settings, style, model, lessons[i], control);
                errors += r.errorsBefore - r.errorsAfter;
                rows += r.rowsBefore - r.rowsAfter;
                errorsLeft += r.errorsAfter;
                rowsLeft += r.rowsAfter;
            }
        } catch (const Stopped &) {
            failure = QStringLiteral("Lavoro fermato.");
        } catch (const std::exception &e) {
            failure = QString::fromUtf8(e.what());
        }
        const double cost = model.cost();
        return std::function<void()>([=, this] {
            checkpoint(QStringLiteral("Errori e righe sistemati"));
            refreshTree();
            emit filesChanged();
            QString text = QStringLiteral("Sistemati %1 e %2").arg(plural(errors, "errore", "errori"), plural(rows, "riga fuori misura", "righe fuori misura"));
            if (errorsLeft + rowsLeft > 0)
                text += QStringLiteral("; restano %1 e %2").arg(plural(errorsLeft, "errore", "errori"), plural(rowsLeft, "riga", "righe"));
            text += QStringLiteral(". Costo: %1 $.").arg(QLocale(QLocale::Italian).toString(cost, 'f', 2));
            say(failure.isEmpty() ? text : failure + ' ' + text);
            m_compiler.compileDocument();
        });
    });
}

void Workspace::paginate()
{
    if (!hasProject())
        return;
    if (busy())
        return say(QStringLiteral("C'è già un lavoro in corso."));
    QStringList lessons;
    for (const QString &file : m_project.inputs())
        if (!m_settings.isDone(file) && QFileInfo::exists(m_project.path(file)) && layout::needed(m_project, file))
            lessons << file;
    if (lessons.isEmpty())
        return say(QStringLiteral("Niente di nuovo da impaginare."));
    checkpoint(QStringLiteral("Modifiche a mano"));
    startJob(QStringLiteral("Impagino"), [this, lessons, project = m_project, style = m_style](JobControl *control) {
        int done = 0, proposed = 0;
        QStringList failed;
        QString stopped;
        try {
            for (int i = 0; i < lessons.size(); ++i) {
                control->progress(double(i) / lessons.size(), QFileInfo(lessons[i]).fileName());
                const layout::Result r = layout::lesson(project, style, lessons[i], control);
                done += r.done;
                proposed += r.proposed;
                if (!r.failure.isEmpty())
                    failed << QFileInfo(lessons[i]).fileName();
            }
        } catch (const Stopped &) {
            stopped = QStringLiteral("Lavoro fermato. ");
        } catch (const std::exception &e) {
            stopped = QString::fromUtf8(e.what()) + ' ';
        }
        return std::function<void()>([=, this] {
            checkpoint(QStringLiteral("Impaginazione"));
            refreshTree();
            emit filesChanged();
            QString text = stopped + QStringLiteral("Impaginazione: %1, %2 da rivedere.").arg(plural(done, "mossa fatta", "mosse fatte")).arg(proposed);
            if (!failed.isEmpty())
                text += QStringLiteral(" Non impaginate perché hanno errori: %1.").arg(failed.join(", "));
            say(text);
            if (done > 0)
                m_compiler.compileDocument();
        });
    });
}

namespace {
QVariantList shotList(const Project &project, const layout::Move &m, const QString &which)
{
    QVariantList out;
    for (const layout::Shot &shot : layout::shots(project, m.lesson, m.block, which)) {
        QVariantList rects;
        for (const QRectF &r : shot.rects)
            rects << QVariantMap{{"x", r.x()}, {"y", r.y()}, {"w", r.width()}, {"h", r.height()}};
        out << QVariantMap{{"path", shot.path}, {"rects", rects}};
    }
    return out;
}
}

QVariantList Workspace::layoutItems() const
{
    QVariantList out;
    if (!hasProject())
        return out;
    for (const Lesson &lesson : m_project.lessons()) {
        const QList<layout::Move> moves = layout::moves(m_project, lesson.file);
        // una lezione che compila pulita e non è ancora impaginata (o è cambiata dopo) è da impaginare: lo si dice
        const bool todo = !m_settings.isDone(lesson.file) && m_states.value(lesson.file) == "good" && layout::needed(m_project, lesson.file);
        if (moves.isEmpty() && !todo)
            continue;
        out << QVariantMap{{"type", "lesson"}, {"lesson", lesson.file},
                           {"title", lesson.number > 0 ? QStringLiteral("Lezione %1 · %2").arg(lesson.number).arg(lesson.title) : lesson.title}};
        if (todo)
            out << QVariantMap{{"type", "todo"}, {"lesson", lesson.file}, {"fresh", moves.isEmpty()}};
        QList<layout::Move> ordered = moves;           // in ordine di documento: l'ordine conta
        std::stable_sort(ordered.begin(), ordered.end(), [](const layout::Move &a, const layout::Move &b) { return a.line < b.line; });
        bool openAbove = false;
        for (const layout::Move &m : ordered) {
            const bool open = QStringList{"fatta", "proposta", "segnalata", "a_mano", "agente"}.contains(m.state);
            const bool locked = open && openAbove;      // prima si decidono quelle sopra
            openAbove = openAbove || open;
            out << QVariantMap{{"type", "move"}, {"lesson", m.lesson}, {"block", m.block}, {"what", m.what}, {"state", m.state}, {"locked", locked},
                               {"hole", qRound(m.hole * 100)}, {"line", m.line},
                               {"before", shotList(m_project, m, "prima")}, {"after", shotList(m_project, m, "dopo")}};
        }
    }
    return out;
}

void Workspace::decideMove(const QString &lesson, const QString &block, const QString &state, bool redo)
{
    // si decide anche mentre un lavoro gira, purché non stia lavorando proprio su questa lezione
    if (m_agentRunning || (jobRunning() && (m_jobLabel.isEmpty() || m_jobLabel.contains(QFileInfo(lesson).fileName()))))
        return say(QStringLiteral("Aspetta: il lavoro in corso sta toccando questa lezione."));
    checkpoint(QStringLiteral("Modifiche a mano"));
    QString what;
    for (const layout::Move &m : layout::moves(m_project, lesson))
        if (m.block == block)
            what = m.what;
    const QString textBefore = files::read(m_project.path(lesson));
    decided(lesson, state == "accettata" ? QStringLiteral("Mossa accettata") : state == "lasciata" ? QStringLiteral("Mossa lasciata com'era")
                    : state == "risolta" ? QStringLiteral("Impaginazione risolta") : QStringLiteral("Impaginazione: scelta cambiata"),
            layout::decide(m_project, lesson, block, state));
    // l'ordine conta: se la lezione è cambiata, le mosse non ancora decise che vengono dopo si ricalcolano
    if (redo && (state == "risolta" || files::read(m_project.path(lesson)) != textBefore) && layout::dropAfter(m_project, lesson, block) > 0) {
        refreshTree();
        emit filesChanged();
        if (jobRunning()) {
            say(QStringLiteral("Il seguito della lezione va rimpaginato: «Impagina ciò che è nuovo» quando il lavoro in corso finisce."));
        } else {
            startJob(QStringLiteral("Rimpagino il seguito"), [this, lesson, project = m_project, style = m_style](JobControl *control) {
                control->progress(0.1, QFileInfo(lesson).fileName());
                layout::Result r;
                try {
                    r = layout::lesson(project, style, lesson, control);
                } catch (const Stopped &) {
                    r.failure = QStringLiteral("Lavoro fermato.");
                }
                return std::function<void()>([this, r] {
                    checkpoint(QStringLiteral("Impaginazione del seguito"));
                    refreshTree();
                    emit filesChanged();
                    say(r.failure.isEmpty() ? QStringLiteral("Seguito rimpaginato: %1 mosse fatte, %2 da decidere.").arg(r.done).arg(r.proposed) : r.failure);
                    m_compiler.compileDocument();
                });
            });
        }
    }
    if (state == "agente")                             // se ne occupa l'agente, nella chat
        emit agentAsked(QStringLiteral("Nella lezione %1, al blocco %2, c'è un problema di impaginazione: %3\nSistemalo toccando il meno possibile "
                                       "(accorciare, spostare, cambiare la misura di una figura, spezzare in un punto migliore), poi compila la lezione "
                                       "e controlla com'è venuta. Non cambiare il contenuto.").arg(lesson, block, what), QStringLiteral("Impaginazione di %1").arg(QFileInfo(lesson).completeBaseName()));
}

void Workspace::setAgentRunning(bool running)
{
    if (m_agentRunning == running)
        return;
    m_agentRunning = running;
    emit busyChanged();
}

void Workspace::agentTurnDone()
{
    if (!hasProject())
        return;
    // un piano o un esito consegnato in questo turno si apre da sé
    QHash<QString, QString> before;
    for (const QVariant &job : std::as_const(m_jobs))
        before.insert(job.toMap().value("id").toString(), job.toMap().value("state").toString());
    checkpoint(QStringLiteral("Agente"));
    styleFileChanged();                                // se l'agente ha toccato lo stile, la descrizione lo segue
    loadSettings();
    refreshTree();
    emit filesChanged();
    for (const QVariant &job : std::as_const(m_jobs)) {
        const QVariantMap now = job.toMap();
        if (before.value(now.value("id").toString()) != now.value("state").toString() && now.value("state") != "in_corso")
            emit planReady(now.value("id").toString());
    }
}

QVariantMap Workspace::plan(const QString &id) const { return plans::load(m_project, id).toVariantMap(); }

void Workspace::savePlan(const QString &id, const QVariantMap &plan)
{
    if (hasProject() && !id.isEmpty())
        plans::save(m_project, id, QJsonObject::fromVariantMap(plan));
}

bool Workspace::planHasInstructions(const QString &id) const { return plans::hasInstructions(plans::load(m_project, id)); }

QString Workspace::planMessage(const QString &id, const QString &how)
{
    QJsonObject plan = plans::load(m_project, id);
    if (how != "avvia")
        return plans::reviseMessage(id, plan);
    plan["stato"] = "in_corso";
    plans::save(m_project, id, plan);
    refreshTree();
    return plans::startMessage(id, plan);
}

void Workspace::discardPlan(const QString &id)
{
    plans::discard(m_project, id);
    refreshTree();
    nextProcedure();
}

void Workspace::archivePlan(const QString &id)
{
    const QString file = plans::archive(m_project, id);
    refreshTree();
    nextProcedure();
    if (!file.isEmpty())
        say(QStringLiteral("Archiviato fra gli esiti: %1.").arg(QFileInfo(file).completeBaseName()));
}

QStringList Workspace::localPaths(const QList<QUrl> &urls)
{
    QStringList out;
    for (const QUrl &url : urls)
        out << localPath(url);
    return out;
}

QStringList Workspace::addSources(const QStringList &paths)
{
    if (!hasProject() || paths.isEmpty())
        return {};
    // un file con il nome di una fonte già usata: identico non si aggiunge; diverso, si chiede se è la versione nuova
    QStringList fresh;
    for (const QString &path : paths) {
        const newlesson::Version version = newlesson::versionOf(m_project, path);
        if (version.used.isEmpty())
            fresh << path;
        else if (version.identical)
            say(QStringLiteral("«%1» è già fra le fonti usate, identica: non la aggiungo.").arg(QFileInfo(path).fileName()));
        else
            emit versionAsked(path, version.used);
    }
    if (fresh.isEmpty())
        return {};
    checkpoint(QStringLiteral("Modifiche a mano"));
    const QStringList added = newlesson::toQueue(m_project, fresh);
    checkpoint(QStringLiteral("Fonti aggiunte"));
    refreshTree();
    return added;
}

void Workspace::addDocuments(const QStringList &paths)
{
    if (!hasProject() || paths.isEmpty())
        return;
    int n = 0;
    for (const QString &path : paths)
        n += QFileInfo(path).isFile() && QFile::copy(path, files::freeName(m_project.path("docs/" + QFileInfo(path).fileName())));
    refreshTree();
    say(n == 1 ? QStringLiteral("Un file aggiunto fra quelli da consultare.") : QStringLiteral("%1 file aggiunti fra quelli da consultare.").arg(n));
}

QVariantMap Workspace::lessonMarks(const QStringList &paths) const
{
    const newlesson::Marks marks = newlesson::read(paths);
    return {{"number", marks.number}, {"date", marks.date}, {"conflict", marks.conflict}, {"next", newlesson::nextNumber(m_project)},
            {"today", QDate::currentDate().toString("dd/MM/yyyy")}};
}

void Workspace::acceptVersion(const QString &path, const QString &used, bool isNew)
{
    if (!hasProject())
        return;
    if (busy())
        return say(QStringLiteral("Aspetta la fine del lavoro in corso."));
    checkpoint(QStringLiteral("Modifiche a mano"));
    if (!isNew) {                                      // un'altra fonte con lo stesso nome: va fra quelle da convertire
        const QStringList added = newlesson::toQueue(m_project, {path});
        checkpoint(QStringLiteral("Fonti aggiunte"));
        refreshTree();
        if (!added.isEmpty())
            sortMaterial(added);
        return;
    }
    QString lesson;                                    // la lezione che ha quella fonte
    for (const QVariant &v : m_lessons)
        for (const QVariant &s : v.toMap().value("sources").toList())
            if (s.toMap().value("file").toString() == used)
                lesson = v.toMap().value("file").toString();
    const QString copy = newlesson::replaceVersion(m_project, used, path);
    if (copy.isEmpty())
        return say(QStringLiteral("Non riesco a mettere la versione nuova al posto della vecchia."));
    checkpoint(QStringLiteral("Versione nuova di una fonte"));
    refreshTree();
    const QString previous = QFileInfo(used).path() + "/precedenti/";
    if (lesson.isEmpty())
        sortMaterial({copy});
    else
        emit agentAsked(newlesson::updateMessage(lesson, copy, previous), QStringLiteral("Versione nuova di %1").arg(QFileInfo(used).fileName()));
}

QString Workspace::createLesson(int number, const QString &date, const QStringList &paths, const QStringList &important)
{
    if (!hasProject())
        return QStringLiteral("Apri prima un progetto.");
    if (busy())
        return QStringLiteral("C'è già un lavoro in corso.");
    if (number <= 0)
        return QStringLiteral("Serve il numero della lezione.");
    for (const Lesson &lesson : m_project.lessons())
        if (lesson.number == number)
            return QStringLiteral("La lezione %1 c'è già: «%2».").arg(number).arg(lesson.title);
    if (paths.isEmpty())
        return QStringLiteral("Serve almeno una fonte.");
    const QStringList sources = addSources(paths);
    if (sources.isEmpty())
        return QStringLiteral("Non riesco a leggere le fonti.");
    if (sources.size() == 1 && Pages::kindOf(sources.first()) != "tex" && Pages::kindOf(sources.first()) != "altro") {
        convert(sources, {{sources.first(), Convert::Wanted{number, date}}});
        return {};
    }
    // più fonti: le fonde l'agente, sempre con un piano da approvare
    static const QRegularExpression numbered(R"(^src(\d+)$)");
    int last = 0;
    for (const QString &name : QDir(m_project.path("src")).entryList({"*.tex"}, QDir::Files))
        last = qMax(last, numbered.match(QFileInfo(name).completeBaseName()).captured(1).toInt());
    QStringList weighty;                               // le fonti importanti, con il nome che hanno nel progetto
    for (const QString &path : important)
        for (const QString &source : sources)
            if (files::bare(QFileInfo(path).fileName()) == files::bare(QFileInfo(source).fileName()))
                weighty << source;
    const QString fuse = newlesson::fuseMessage(QStringLiteral("src/src%1.tex").arg(last + 1), number, date, sources, weighty);
    const QString title = QStringLiteral("Lezione %1: fusione delle fonti").arg(number);
    bool pages = sources.size() > 1;
    for (const QString &source : sources)
        pages = pages && Pages::kindOf(source) == "pagine";
    if (!pages) {                                      // tipi diversi: le fonde l'agente
        emit agentAsked(fuse, title);
        return {};
    }
    // più scansioni o PDF: una chiamata leggera dice se si sovrappongono. Se no, si uniscono e si convertono.
    startJob(QStringLiteral("Guardo le fonti"), [=, this, project = m_project, quality = m_settings.quality](JobControl *control) {
        newlesson::Overlap seen;
        QString united;
        try {
            control->progress(0.2, QStringLiteral("che cosa hanno in comune"));
            seen = newlesson::overlap(project, quality, sources, control);
            if (seen.known && !seen.overlap)
                united = newlesson::unite(project, sources, seen.order, number);
        } catch (const Stopped &) {
            return std::function<void()>([this] { say(QStringLiteral("Lavoro fermato: le fonti restano fra quelle da convertire.")); refreshTree(); });
        }
        return std::function<void()>([=, this] {
            refreshTree();
            if (united.isEmpty()) {                    // si sovrappongono (o non si sa): le fonde l'agente, con un piano
                QTimer::singleShot(0, this, [=, this] { emit agentAsked(fuse, title); });
                return;
            }
            QStringList names;
            for (int i : seen.order)
                names << QFileInfo(sources.value(i)).fileName();
            m_outcomeHead = QStringLiteral("Fonti unite in quest'ordine: %1. %2").arg(names.join(", "), seen.why);
            QTimer::singleShot(0, this, [=, this] { convert({united}, {{united, Convert::Wanted{number, date}}}); });
        });
    });
    return {};
}

void Workspace::integrateSources(const QString &lesson, const QStringList &paths)
{
    if (busy())
        return say(QStringLiteral("C'è già un lavoro in corso."));
    const QStringList sources = addSources(paths);
    if (!sources.isEmpty())
        emit agentAsked(newlesson::integrateMessage(lesson, sources), QStringLiteral("Fonti aggiunte a %1").arg(QFileInfo(lesson).completeBaseName()));
}

void Workspace::sortMaterial(const QStringList &sources) { sortJob(sources, {}, {}); }

void Workspace::reviseSortPlan(const QString &id)
{
    const QJsonObject plan = plans::load(m_project, id);
    QStringList sources;
    for (const QJsonValue &f : plan.value("file").toArray())
        sources << f.toString();
    sortJob(sources, sort::previousPlan(plan), id);
}

void Workspace::sortJob(const QStringList &sources, const QString &previous, const QString &replaces)
{
    if (!hasProject() || sources.isEmpty())
        return;
    if (busy())
        return say(QStringLiteral("C'è già un lavoro in corso."));
    startJob(QStringLiteral("Smisto il materiale"), [this, sources, previous, replaces, project = m_project, settings = m_settings](JobControl *control) {
        const sort::Outcome outcome = sort::plan(project, settings, sources, control, previous);
        return std::function<void()>([this, outcome, replaces, project] {
            if (outcome.plan.isEmpty())
                return say(outcome.failure.isEmpty() ? QStringLiteral("Non sono riuscito a fare il piano.") : outcome.failure);
            plans::discard(project, replaces);         // il piano rifatto prende il posto di quello di prima
            refreshTree();
            say(QStringLiteral("Piano del materiale pronto. Costo: %1 $.").arg(QLocale(QLocale::Italian).toString(outcome.cost, 'f', 2)));
            emit planReady(outcome.plan);
        });
    });
}

void Workspace::startSortPlan(const QString &id)
{
    if (busy())
        return say(QStringLiteral("C'è già un lavoro in corso."));
    checkpoint(QStringLiteral("Modifiche a mano"));
    const sort::Prepared prepared = sort::prepare(m_project, plans::load(m_project, id));
    if (!prepared.failure.isEmpty())
        return say(prepared.failure);
    plans::discard(m_project, id);                     // il piano è eseguito da Texfix: l'esito è quello di Converti
    m_agentQueue += prepared.agent;
    refreshTree();
    if (!prepared.sources.isEmpty())
        convert(prepared.sources, prepared.wanted);    // le lezioni semplici, tutte insieme; poi le procedure dell'agente
    else
        nextProcedure();
    if (prepared.sources.isEmpty() && prepared.agent.isEmpty())
        say(QStringLiteral("Nel piano non c'è niente da fare."));
}

void Workspace::nextProcedure()
{
    if (m_agentQueue.isEmpty() || busy() || !hasProject())
        return;
    for (const plans::Info &info : plans::open(m_project))
        if (info.state != "esito")
            return;                                    // una procedura aspetta che il piano aperto sia deciso ed eseguito
    emit agentAsked(m_agentQueue.takeFirst(), QStringLiteral("Materiale da sistemare"));
}

bool Workspace::hasRepository() const { return hasProject() && git::hasRepository(m_project); }

QVariantList Workspace::gitChanges() const
{
    QVariantList out;
    for (const git::Change &c : git::changes(m_project))
        out << QVariantMap{{"file", c.file}, {"state", c.state}};
    return out;
}

QVariantList Workspace::gitLog() const
{
    QVariantList out;
    for (const git::Entry &e : git::log(m_project))
        out << QVariantMap{{"hash", e.hash}, {"when", e.when}, {"subject", e.subject}};
    return out;
}

QString Workspace::gitDiff(const QString &what) const
{
    return what.startsWith("commit:") ? git::show(m_project, what.mid(7)) : git::diff(m_project, what.mid(5));
}

QString Workspace::gitCommit(const QString &message, const QStringList &files)
{
    const QString error = git::commit(m_project, message, files);
    refreshTree();
    emit treeChanged();
    if (error.isEmpty())
        say(QStringLiteral("Commit fatto."));
    return error;
}

void Workspace::gitInit()
{
    git::init(m_project);
    refreshTree();
    emit treeChanged();
}

QString Workspace::renameFile(const QString &file, const QString &newName)
{
    if (busy())
        return QStringLiteral("Aspetta la fine del lavoro in corso.");
    checkpoint(QStringLiteral("Modifiche a mano"));
    const QString error = fileops::rename(m_project, file, newName);
    if (!error.isEmpty())
        return error;
    checkpoint(QStringLiteral("File rinominato"));
    refreshTree();
    QString name = newName.trimmed();
    if (QFileInfo(name).suffix().isEmpty() && !QFileInfo(file).suffix().isEmpty())
        name += '.' + QFileInfo(file).suffix();
    emit fileRenamed(file, QFileInfo(file).path() + '/' + name);
    return {};
}

void Workspace::duplicateFile(const QString &file)
{
    checkpoint(QStringLiteral("Modifiche a mano"));
    if (fileops::duplicate(m_project, file).isEmpty())
        return say(QStringLiteral("Non riesco a duplicare il file."));
    checkpoint(QStringLiteral("File duplicato"));
    refreshTree();
}

void Workspace::trashFile(const QString &file)
{
    if (busy())
        return say(QStringLiteral("Aspetta la fine del lavoro in corso."));
    checkpoint(QStringLiteral("Modifiche a mano"));
    const QString error = fileops::trash(m_project, file);
    if (!error.isEmpty())
        return say(error);
    checkpoint(QStringLiteral("File nel Cestino"));     // ⌘Z lo riporta
    refreshTree();
    emit fileRenamed(file, QString());
    say(QStringLiteral("«%1» è nel Cestino.").arg(QFileInfo(file).fileName()));
}

void Workspace::revealFile(const QString &file) const { QProcess::startDetached("open", {"-R", m_project.path(file)}); }

void Workspace::copyText(const QString &text) const { QGuiApplication::clipboard()->setText(text); }

void Workspace::proposeTopics()
{
    if (!hasProject())
        return;
    if (busy())
        return say(QStringLiteral("C'è già un lavoro in corso."));
    if (byTopic())
        return say(QStringLiteral("Il progetto è già per argomenti."));
    startJob(QStringLiteral("Propongo gli argomenti"), [this, project = m_project, settings = m_settings](JobControl *control) {
        double cost = 0;
        QList<reorganize::Topic> topics;
        QString failure;
        try {
            topics = reorganize::propose(project, settings, control, &cost);
        } catch (const Stopped &) {
            failure = QStringLiteral("Lavoro fermato.");
        }
        QHash<QString, reorganize::Section> byId;
        for (const reorganize::Section &s : reorganize::sections(project))
            byId.insert(s.id, s);
        QVariantList outline;
        for (const reorganize::Topic &topic : std::as_const(topics)) {
            QVariantList sections;
            for (const QString &id : topic.sections)
                sections << QVariantMap{{"id", id}, {"title", byId.value(id).title}, {"lesson", byId.value(id).lesson}};
            outline << QVariantMap{{"title", topic.title}, {"sections", sections}};
        }
        return std::function<void()>([this, outline, failure, cost] {
            if (!failure.isEmpty())
                return say(failure);
            say(QStringLiteral("Scaletta degli argomenti pronta. Costo: %1 $.").arg(QLocale(QLocale::Italian).toString(cost, 'f', 2)));
            emit outlineReady(outline);
        });
    });
}

QString Workspace::applyTopics(const QVariantList &topics)
{
    if (busy())
        return QStringLiteral("C'è già un lavoro in corso.");
    QList<reorganize::Topic> list;
    for (const QVariant &t : topics) {
        reorganize::Topic topic{t.toMap().value("title").toString(), {}};
        for (const QVariant &s : t.toMap().value("sections").toList())
            topic.sections << s.toMap().value("id").toString();
        list << topic;
    }
    checkpoint(QStringLiteral("Modifiche a mano"));
    const QString error = reorganize::toTopics(m_project, list);
    if (!error.isEmpty())
        return error;
    checkpoint(QStringLiteral("Riorganizza per argomenti"));
    m_states.clear();                                  // gli stati valevano per i file di prima
    m_details.clear();
    loadSettings();
    refreshTree();
    emit filesChanged();
    m_compiler.compileDocument();
    return {};
}

void Workspace::backToLessons()
{
    if (busy())
        return say(QStringLiteral("C'è già un lavoro in corso."));
    checkpoint(QStringLiteral("Modifiche a mano"));
    const QString error = reorganize::toLessons(m_project);
    if (!error.isEmpty())
        return say(error);
    checkpoint(QStringLiteral("Riorganizza per data di lezione"));
    m_states.clear();
    m_details.clear();
    loadSettings();
    refreshTree();
    emit filesChanged();
    m_compiler.compileDocument();
}

void Workspace::correctDoubt(const QString &lesson, const QString &id)
{
    checkpoint(QStringLiteral("Modifiche a mano"));
    decided(lesson, QStringLiteral("Dubbio da correggere a mano"), review::startCorrection(m_project, lesson, id));
}

void Workspace::confirmDoubt(const QString &lesson, const QString &id)
{
    checkpoint(QStringLiteral("Modifiche a mano"));
    decided(lesson, QStringLiteral("Dubbio confermato"), review::confirm(m_project, lesson, id));
}

void Workspace::addProjectRule(const QString &rule)
{
    if (rule.trimmed().isEmpty())
        return;
    setProjectRules(m_settings.rules + QStringList{rule.trimmed()});
    say(QStringLiteral("Regola del progetto aggiunta."));
}

void Workspace::decideDoubts(const QString &lesson, const QStringList &ids)
{
    if (busy())
        return say(QStringLiteral("C'è già un lavoro in corso."));
    QStringList todo = ids;
    if (todo.isEmpty())
        for (const review::Doubt &d : review::doubts(m_project, lesson))
            if (d.open && !d.pending)
                todo << d.id;
    if (todo.isEmpty())
        return say(QStringLiteral("Non ci sono dubbi da far decidere."));
    checkpoint(QStringLiteral("Modifiche a mano"));
    startJob(QStringLiteral("Il modello decide i dubbi"), [this, lesson, todo, project = m_project, settings = m_settings](JobControl *control) {
        Model model(settings.quality, project.root(), control);
        int done = 0;
        QStringList failed;
        QString stopped;
        try {
            for (int i = 0; i < todo.size(); ++i) {
                control->progress(double(i) / todo.size(), QStringLiteral("dubbio %1 di %2").arg(i + 1).arg(todo.size()));
                try {
                    const QString error = review::decide(project, settings, model, lesson, todo[i]);   // una chiamata per dubbio
                    error.isEmpty() ? void(++done) : void(failed << error);
                } catch (const Stopped &) {
                    throw;
                } catch (const LimitReached &) {
                    throw;
                } catch (const std::exception &e) {
                    failed << QString::fromUtf8(e.what());
                }
            }
        } catch (const Stopped &) {
            stopped = QStringLiteral("Lavoro fermato. ");
        } catch (const std::exception &e) {
            stopped = QString::fromUtf8(e.what()) + ' ';
        }
        const double cost = model.cost();
        return std::function<void()>([=, this] {
            checkpoint(QStringLiteral("Dubbi decisi dal modello"));
            refreshTree();
            emit filesChanged();
            say(stopped + QStringLiteral("Il modello ha deciso %1: aspettano la tua conferma. Costo: %2 $.%3")
                              .arg(plural(done, "dubbio", "dubbi"), QLocale(QLocale::Italian).toString(cost, 'f', 2),
                                   failed.isEmpty() ? QString() : QStringLiteral(" Non decisi: %1.").arg(failed.size())));
        });
    });
}

QStringList Workspace::styleLibrary() const { return styleswitch::library(); }

void Workspace::useLibraryStyle(const QString &name)
{
    if (!hasProject())
        return;
    const styleswitch::Imported in = styleswitch::importFromLibrary(m_project, name);
    if (!in.error.isEmpty())
        return say(in.error);
    applyStyle(name);
}

void Workspace::setShowTags(bool show)
{
    if (show == m_showTags)
        return;
    m_showTags = show;
    settings().setValue("vista/tag", show);
    emit tagsChanged();
    computeTags();
}

void Workspace::setMarkHidden(const QString &mark, bool hidden)
{
    m_hiddenMarks.removeAll(mark);
    if (hidden)
        m_hiddenMarks << mark;
    emit tagsChanged();
}

QVariantList Workspace::tagSources() const
{
    QVariantList out;
    if (!hasProject())
        return out;
    for (const provenance::Source &s : provenance::sources(m_project, m_style))
        out << QVariantMap{{"mark", s.mark}, {"name", s.name}, {"path", s.path}, {"hidden", m_hiddenMarks.contains(s.mark)}};
    return out;
}

// Dove cade ogni blocco lo dice la compilazione: si calcola in un thread, e resta solo l'ultimo calcolo chiesto.
void Workspace::computeTags()
{
    const int run = ++m_tagsRun;
    if (!m_showTags || m_pdf.isEmpty() || !hasProject()) {
        if (!m_tags.isEmpty()) {
            m_tags.clear();
            emit tagsChanged();
        }
        return;
    }
    QThreadPool::globalInstance()->start([this, run, project = m_project, style = m_style, pdf = m_pdf] {
        QHash<QString, QString> paths;
        for (const provenance::Source &s : provenance::sources(project, style))
            paths.insert(s.name, s.path);
        QVariantList tags;
        for (const provenance::Tag &t : provenance::tags(project, style, pdf))
            tags << QVariantMap{{"page", t.page}, {"y", t.y}, {"mark", t.mark}, {"source", t.source}, {"sourcePage", t.sourcePage},
                                {"path", paths.value(t.source)}, {"block", t.block}};
        QMetaObject::invokeMethod(this, [this, run, tags] {
            if (run != m_tagsRun)
                return;
            m_tags = tags;
            emit tagsChanged();
        });
    });
}

QString Workspace::snippet(const QString &text, const QString &kind, const QString &color)
{
    if (!hasProject() || text.trimmed().isEmpty())
        return {};
    const snippets::Kind k = kind == "latex" ? snippets::Kind::Latex : snippets::Kind::Prose;
    const QString path = snippets::pathOf(m_project, text, k, color);
    if (QFileInfo::exists(path))
        return path;
    if (QFileInfo::exists(path.chopped(4) + ".no"))
        return {};                                     // non compila: resta il testo
    const QStringList wanted{text, kind, color};
    if (!m_snippetQueue.contains(wanted))
        m_snippetQueue << wanted;
    QTimer::singleShot(40, this, &Workspace::renderSnippets);   // quelle chieste insieme si fanno insieme
    return {};
}

void Workspace::renderSnippets()
{
    if (m_snippetsRunning || m_snippetQueue.isEmpty() || !hasProject())
        return;
    m_snippetsRunning = true;
    const QList<QStringList> queue = m_snippetQueue;
    m_snippetQueue.clear();
    QThreadPool::globalInstance()->start([this, queue, project = m_project] {
        // un giro per ogni tipo e colore
        QHash<QString, QStringList> groups;
        for (const QStringList &q : queue)
            groups[q[1] + '\n' + q[2]] << q[0];
        for (auto it = groups.constBegin(); it != groups.constEnd(); ++it)
            snippets::render(project, it.value(), it.key().startsWith("latex") ? snippets::Kind::Latex : snippets::Kind::Prose, it.key().section('\n', 1));
        QMetaObject::invokeMethod(this, [this] {
            m_snippetsRunning = false;
            emit snippetsReady();
            renderSnippets();
        });
    });
}

void Workspace::recheckFigures()
{
    if (!hasProject())
        return;
    if (busy())
        return say(QStringLiteral("Aspetta la fine del lavoro in corso."));
    emit agentAsked(QStringLiteral("Ricontrolla le figure del progetto con le fonti.\n"
                                   "- Rileggi tutte le fonti usate (`fonti/usate/`) e, lezione per lezione, confrontale con le figure che ci sono "
                                   "(il segno `% img <nome>` nelle lezioni; il registro è `salvataggi/figure.json`).\n"
                                   "- Cerca: figure delle fonti che mancano nelle lezioni, figure che non corrispondono al disegno della fonte, "
                                   "figure doppie o nel posto sbagliato, didascalie sbagliate.\n"
                                   "- Consegna un piano con consegna_piano: una sezione per lezione, una voce per figura da aggiungere, rifare o "
                                   "spostare, con la fonte e la pagina. Se è tutto a posto dillo e non consegnare niente.\n"
                                   "- Quando il piano è approvato fai le modifiche, compila le lezioni toccate finché sono pulite e consegna l'esito."),
                    QStringLiteral("Figure: ricontrollo con le fonti"));
}

QVariantList Workspace::texAlreadyThere() const
{
    QVariantList out;
    if (!hasProject())
        return out;
    QStringList have;
    for (const Lesson &lesson : m_project.lessons())
        have << lesson.date;
    const QDir dir(m_project.path("fonti/coda"));
    for (const QString &name : dir.entryList({"*.tex"}, QDir::Files, QDir::Name)) {
        QStringList dates;
        for (const texsource::Lesson &lesson : texsource::lessons(files::read(dir.filePath(name))))
            if (have.contains(lesson.date.toString("dd/MM/yyyy")))
                dates << lesson.date.toString("dd/MM/yyyy");
        if (!dates.isEmpty())
            out << QVariantMap{{"file", "fonti/coda/" + name}, {"name", name}, {"count", int(dates.size())}, {"dates", dates}};
    }
    return out;
}

void Workspace::integrateTex(const QString &file)
{
    if (busy())
        return say(QStringLiteral("Aspetta la fine del lavoro in corso."));
    QStringList dates;
    for (const QVariant &v : texAlreadyThere())
        if (v.toMap().value("file").toString() == file)
            dates = v.toMap().value("dates").toStringList();
    if (dates.isEmpty())
        return;
    emit agentAsked(QStringLiteral("Il file `%1` ha lezioni che il corso ha già, per data: %2.\n"
                                   "- Per ognuna leggi la parte del file (`\\section{Lezione del …}`) e la lezione del corso con quella data. Porta "
                                   "nella lezione solo ciò che manca o le correzioni, vicino al blocco a cui appartiene; ciò che c'è già non si riscrive.\n"
                                   "- Prima consegna un piano con consegna_piano: una sezione per lezione, una voce per ogni aggiunta.\n"
                                   "- Quando il piano è approvato fai le aggiunte, compila finché è pulito e consegna l'esito. Il file resta dov'è.")
                        .arg(file, dates.join(", ")),
                    QStringLiteral("Materiale da %1").arg(QFileInfo(file).fileName()));
}

void Workspace::reopenLesson(const QString &lesson, const QString &section)
{
    if (!hasProject())
        return;
    if (busy())
        return say(QStringLiteral("Aspetta la fine del lavoro."));
    checkpoint(QStringLiteral("Modifiche a mano"));
    QString error;
    if (section == "dubbi") {
        for (const review::Doubt &d : review::doubts(m_project, lesson))
            if (!d.open && error.isEmpty())
                error = review::reopen(m_project, lesson, d.id, false);
    } else {
        for (const review::Declared &d : review::declared(m_project, lesson)) {
            if (d.state == "tenuta")
                review::keep(m_project, lesson, d.hash, false);
            else if (d.state == "tolta" && error.isEmpty())
                error = review::restore(m_project, lesson, d.hash);
        }
    }
    decided(lesson, QStringLiteral("Lezione riaperta"), error);      // riaprire toglie «fatta» alla lezione
}

bool Workspace::showsLesson(const QString &file) const { return hasProject() && m_pdf == Compiler::pdfOf(m_project, file); }

QVariantMap Workspace::moveSpot(const QString &lesson, const QString &block) const
{
    int line = 0;
    QString sign;
    for (const layout::Move &m : layout::moves(m_project, lesson))
        if (m.block == block) {
            line = m.line;
            sign = m.sign.trimmed();
        }
    // la riga della mossa nella lezione, se c'è: attorno al blocco
    const QStringList lines = files::read(m_project.path(lesson)).split('\n');
    static const QRegularExpression marker(R"(^\s*%\s*@L\d+\.\d+)");
    int end = line;
    while (end < lines.size() && !marker.match(lines[end]).hasMatch())
        ++end;
    for (int i = qMax(0, line - 11); i < end && i < lines.size(); ++i)
        if (!sign.isEmpty() && lines[i].trimmed() == sign)
            return {{"line", i + 1}, {"present", true}, {"movable", true}};
    return {{"line", qMax(1, line)}, {"present", false}, {"movable", !sign.isEmpty()}};
}

void Workspace::resetLayout()
{
    if (!hasProject())
        return;
    if (busy())
        return say(QStringLiteral("Aspetta la fine del lavoro in corso."));
    checkpoint(QStringLiteral("Modifiche a mano"));
    int n = 0, lessons = 0;
    for (const QString &file : m_project.inputs()) {
        if (m_settings.isDone(file))
            continue;                                  // le lezioni fatte non si toccano
        n += layout::reset(m_project, file);
        ++lessons;
    }
    checkpoint(QStringLiteral("Impaginazioni eliminate"));
    refreshTree();
    emit filesChanged();
    say(QStringLiteral("Impaginazioni eliminate (%1): %2 da rifare con «Impagina ciò che è nuovo».").arg(n).arg(plural(lessons, "lezione", "lezioni")));
    m_compiler.compileDocument();
}

void Workspace::acceptRest(const QString &lesson)
{
    if (!hasProject() || m_agentRunning)
        return;
    checkpoint(QStringLiteral("Modifiche a mano"));
    int n = 0;
    for (const layout::Move &m : layout::moves(m_project, lesson))
        if (m.state == "fatta" && layout::decide(m_project, lesson, m.block, "accettata").isEmpty())
            ++n;                                       // la riga c'è già: accettare non cambia la lezione
    checkpoint(QStringLiteral("Mosse accettate"));
    refreshTree();
    emit filesChanged();
    say(n == 1 ? QStringLiteral("Una mossa accettata.") : QStringLiteral("%1 mosse accettate.").arg(n));
}

void Workspace::restoreManual(const QString &lesson, const QString &text, const QString &registry)
{
    if (!hasProject())
        return;
    files::write(m_project.path(lesson), text);
    files::write(m_project.path("salvataggi/impaginazione.json"), registry);
    refreshTree();
    emit filesChanged();
}
