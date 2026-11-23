#include "Workspace.h"
#include <QJsonObject>

#include "core/Concepts.h"
#include "core/Figures.h"
#include "core/Files.h"
#include "core/Fix.h"
#include "core/Git.h"
#include "core/Layout.h"
#include "core/Model.h"
#include "core/NewLesson.h"
#include "core/Pages.h"
#include "core/Plans.h"
#include "core/AgentTools.h"
#include "core/Profiles.h"
#include "core/Provenance.h"
#include "core/Reorganize.h"
#include "core/Review.h"
#include "core/Run.h"
#include "core/Rules.h"
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
#include <QCryptographicHash>
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
const char *const UNRECOGNIZED = "build/lavori/smistamento/non-riconosciuti.json";   // in arrivo, non riconosciuti: il perché

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
    connect(this, &Workspace::busyChanged, this, &Workspace::nextSort, Qt::QueuedConnection);   // prima il materiale che aspetta
    m_runTimer.setSingleShot(true);
    m_runTimer.setInterval(250);
    connect(&m_runTimer, &QTimer::timeout, this, [this] {
        emit runChanged();
        saveRunView();
    });
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
    if (file.isEmpty())
        return QSettings();
    return QSettings(file, QSettings::IniFormat);   // niente ?: : MSVC vuole copiare QSettings, che non si copia
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

QString Workspace::newProject(const QString &path, const QString &profile, bool byTopic)
{
    if (const QString error = Project::create(path); !error.isEmpty())
        return error;
    QString problem;
    if (!profile.isEmpty())
        problem = profiles::setForProject(Project(path), profile);
    if (byTopic)
        reorganize::startByTopic(Project(path));
    open(path);
    if (!problem.isEmpty())
        say(problem);
    return {};
}

QStringList Workspace::profiles() const { return profiles::builtin() + profiles::library(); }
QStringList Workspace::builtinProfiles() const { return profiles::builtin(); }
QStringList Workspace::libraryProfiles() const { return profiles::library(); }

QString Workspace::useProfile(const QString &name)
{
    if (!hasProject())
        return QStringLiteral("Apri prima un progetto.");
    const QString error = profiles::setForProject(m_project, name);
    if (!error.isEmpty())
        return error;
    loadSettings();
    emit settingsChanged();
    refreshTree();
    say(QStringLiteral("Il progetto usa il profilo «%1».").arg(name));
    return {};
}

QString Workspace::importProfileToLibrary(const QString &path) { return profiles::importFile(path); }

QString Workspace::importProfile(const QString &path)
{
    const QString error = profiles::importFile(path);
    if (!error.isEmpty())
        return error;
    bool ok = false;
    const rules::Profile p = rules::Profile::parse(files::read(path), &ok);
    return useProfile(ok ? p.name : QFileInfo(path).fileName().section('.', 0, 0));
}

QString Workspace::exportProfile(const QString &path)
{
    const QString error = profiles::exportFile(m_project, path);
    if (error.isEmpty())
        say(QStringLiteral("Profilo esportato in %1.").arg(QFileInfo(path).fileName()));
    return error;
}

QVariantList Workspace::structuralSections() const
{
    QVariantList out;
    for (const rules::Section &s : rules::structural())
        out << QVariantMap{{"id", s.id}, {"title", s.title}, {"text", s.text}};
    return out;
}

bool Workspace::styleModified() const { return hasProject() && styleswitch::modified(m_project); }

QString Workspace::saveStyleToLibrary(const QString &name, bool replace)
{
    if (!hasProject())
        return QStringLiteral("Apri prima un progetto.");
    const QString error = styleswitch::saveToLibrary(m_project, name.trimmed(), replace);
    if (!error.isEmpty())
        return error;
    loadSettings();
    refreshTree();
    emit settingsChanged();
    say(QStringLiteral("Lo stile «%1» è nella libreria.").arg(name.trimmed()));
    return {};
}

QString Workspace::exportStyle(const QString &dir)
{
    const QString error = styleswitch::exportTo(m_project, dir);
    if (error.isEmpty())
        say(QStringLiteral("Lo stile è in %1.").arg(dir));
    return error;
}

QString Workspace::saveProfile(const QString &name, bool replace)
{
    const QString error = profiles::saveToLibrary(m_project, name, replace);
    if (error.isEmpty()) {
        loadSettings();
        say(QStringLiteral("Profilo «%1» salvato nella libreria: un progetto nuovo può partire da qui.").arg(name.trimmed()));
    }
    return error;
}

void Workspace::removeProfile(const QString &name)
{
    if (const QString error = profiles::remove(name); !error.isEmpty())
        say(error);
}

void Workspace::open(const QString &path)
{
    closeProject();
    m_project = Project(path);
    m_project.complete();                              // un progetto a cui manca qualcosa si completa da sé
    profiles::keepMine(m_project);                     // un progetto di prima: le sue regole anche nella libreria
    m_compiler.setProject(m_project);
    m_history = std::make_unique<History>(m_project.root());
    // com'era alla chiusura: se da allora i file sono cambiati, il primo passo lo dice
    const QString closed = files::read(m_project.path("build/stato-alla-chiusura")).trimmed();
    if (!closed.isEmpty() && closed != m_history->digest())
        m_history->renameFirst(QStringLiteral("Modifiche fuori da Texfix"));
    loadSettings();
    const QJsonObject unrecognized = QJsonDocument::fromJson(files::read(m_project.path(UNRECOGNIZED)).toUtf8()).object();
    for (auto it = unrecognized.begin(); it != unrecognized.end(); ++it)
        m_unrecognized.insert(it.key(), it.value().toString());

    const QJsonObject saved = QJsonDocument::fromJson(files::read(m_project.path(STATES)).toUtf8()).object();
    for (auto it = saved.begin(); it != saved.end(); ++it) {
        m_states[it.key()] = it.value().toObject().value("stato").toString();
        m_details[it.key()] = it.value().toObject().value("dettaglio").toString();
    }
    if (QFileInfo::exists(Compiler::pdfOf(m_project, {})))
        m_pdf = Compiler::pdfOf(m_project, {});
    loadRun();                                         // un lavoro di «Avvia» non archiviato: la sua finestra lo mostra
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
        m_closing = true;                              // un lavoro di «Avvia» resta interrotto, da riprendere
        m_jobControl->stop();
        m_jobThread->wait();
        QCoreApplication::sendPostedEvents(this);      // la fine del lavoro, prima di smontare il progetto
        m_closing = false;
    }
    m_compiler.stop();
    if (m_history)
        files::write(m_project.path("build/stato-alla-chiusura"), m_history->digest());
    m_history.reset();                                 // la cronologia vale finché il progetto è aperto
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    m_runId.clear();                                   // il lavoro di «Avvia» resta nei file del progetto
    m_runState.clear();
    m_runSites.clear();
    m_runCalls.clear();
    m_runSiteAt.clear();
    m_runCallAt.clear();
    m_runLessonSite.clear();
    m_runStreams.clear();
    m_runSteps.clear();
    m_runStepAt.clear();
    m_runOutcome.clear();
    emit runChanged();
    m_unrecognized.clear();
    m_sortWaiting.clear();
    m_converting.clear();
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
                      m_project.path("docs"), m_project.path("imgs"), m_project.path("salvataggi"), m_project.path(agent::PROPOSALS)};
    for (const QVariant &lesson : std::as_const(m_lessons))
        paths << m_project.path(lesson.toMap().value("file").toString());
    for (const QString &path : std::as_const(paths))
        if (QFileInfo::exists(path) && !m_watcher.files().contains(path) && !m_watcher.directories().contains(path))
            m_watcher.addPath(path);
}

void Workspace::readProposals()
{
    QVariantList out;
    const QString dir = m_project.path(agent::PROPOSALS);
    if (QFileInfo::exists(dir + "/profilo.md")) {
        const rules::Profile proposed = rules::Profile::parse(files::read(dir + "/profilo.md"));
        out << QVariantMap{{"kind", "profilo"}, {"name", proposed.name}, {"now", m_settings.profile.markdown()}, {"proposed", proposed.markdown()},
                           {"fresh", QJsonDocument::fromJson(files::read(dir + "/profilo.json").toUtf8()).object().value("nuovo").toBool()}};
    }
    if (const QJsonObject style = QJsonDocument::fromJson(files::read(dir + "/stile.json").toUtf8()).object(); !style.isEmpty()) {
        const QString name = style.value("nome").toString();
        out << QVariantMap{{"kind", "stile"}, {"name", name}, {"now", files::read(m_project.path(name + ".sty"))},
                           {"proposed", files::read(dir + '/' + name + ".sty")}, {"fresh", style.value("nuovo").toBool()}};
    }
    if (out != m_proposals) {
        m_proposals = out;
        emit proposalsChanged();
    }
}

void Workspace::askAgent(const QString &kind)
{
    static const QHash<QString, QString> titles{{"crea-profilo", "Il profilo del corso"}, {"migliora-profilo", "Migliora il profilo"},
                                                {"crea-stile", "Lo stile del corso"}, {"migliora-stile", "Migliora lo stile"}};
    if (hasProject() && titles.contains(kind))
        emit agentAsked(agent::procedureMessage(m_project, kind), titles.value(kind));
}

QString Workspace::acceptProposal(const QString &kind)
{
    const QString dir = m_project.path(agent::PROPOSALS);
    QString note;
    if (kind == "profilo") {
        rules::Profile p = rules::Profile::parse(files::read(dir + "/profilo.md"));
        const bool fresh = QJsonDocument::fromJson(files::read(dir + "/profilo.json").toUtf8()).object().value("nuovo").toBool();
        checkpoint(QStringLiteral("Modifiche a mano"));
        if (const QString error = profiles::saveToProject(m_project, p); !error.isEmpty())
            return error;
        if (fresh) {                                   // un profilo nuovo va anche nella libreria, con un nome libero
            QString name = p.name;
            for (int n = 2; profiles::exists(name); ++n)
                name = QStringLiteral("%1 (%2)").arg(p.name).arg(n);
            note = profiles::saveToLibrary(m_project, name);
        }
        QFile::remove(dir + "/profilo.md");
        QFile::remove(dir + "/profilo.json");
        checkpoint(QStringLiteral("Profilo «%1»").arg(profiles::ofProject(m_project).name));
        loadSettings();
        emit settingsChanged();
    } else if (kind == "stile") {
        const QJsonObject style = QJsonDocument::fromJson(files::read(dir + "/stile.json").toUtf8()).object();
        const QString name = style.value("nome").toString();
        const QString sty = dir + '/' + name + ".sty", description = dir + '/' + Style::descriptionFile(name);
        if (!QFileInfo::exists(sty))
            return QStringLiteral("La proposta non c'è più.");
        const bool same = name == (m_settings.style.isEmpty() ? QString::fromUtf8(Project::DEFAULT_STYLE) : m_settings.style);
        checkpoint(QStringLiteral("Modifiche a mano"));
        for (const QString &file : {sty, description}) {
            QFile::remove(m_project.path(QFileInfo(file).fileName()));
            QFile::copy(file, m_project.path(QFileInfo(file).fileName()));
        }
        if (style.value("nuovo").toBool())
            note = styleswitch::addToLibrary(m_project.path(name + ".sty"));
        for (const QString &file : {sty, description, dir + "/stile.json"})
            QFile::remove(file);
        if (same) {                                    // lo stesso stile, cambiato: la descrizione è quella proposta
            m_project.complete();
            checkpoint(QStringLiteral("Stile «%1» cambiato").arg(name));
            loadSettings();
            emit settingsChanged();
        } else {
            applyStyle(name);                          // uno stile nuovo: le lezioni passano a lui (con le domande, se servono)
        }
    }
    readProposals();
    refreshTree();
    if (!note.isEmpty())
        say(note);
    return {};
}

void Workspace::discardProposal(const QString &kind)
{
    const QString dir = m_project.path(agent::PROPOSALS);
    if (kind == "profilo") {
        QFile::remove(dir + "/profilo.md");
        QFile::remove(dir + "/profilo.json");
    } else {
        const QString name = QJsonDocument::fromJson(files::read(dir + "/stile.json").toUtf8()).object().value("nome").toString();
        QFile::remove(dir + '/' + name + ".sty");
        QFile::remove(dir + '/' + Style::descriptionFile(name));
        QFile::remove(dir + "/stile.json");
    }
    readProposals();
}

void Workspace::refreshTree()
{
    if (!hasProject())
        return;
    readProposals();
    QVariantList lessons, others;
    QStringList inMain;
    m_openDoubts = m_openDeclared = m_openFigures = m_openLayout = 0;
    m_openFigures = figures::unadopted(m_project, m_style);      // le figure scritte fuori, non ancora nel registro
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
        // lo stato a parole, per il suggerimento sulla lezione: la compilazione, poi ciò che resta da rivedere
        QString status;
        const QString compiled = m_states.value(lesson.file, "idle"), said = m_details.value(lesson.file);
        if (done)
            status = QStringLiteral("Fatta.");
        else if (compiled == "bad")
            status = QStringLiteral("Non compila") + (said.isEmpty() ? QString() : ": " + said) + '.';
        else if (compiled == "warn")
            status = QStringLiteral("Compila") + (said.isEmpty() ? QString() : ", con " + said) + '.';
        else if (compiled == "good")
            status = QStringLiteral("Compila senza problemi.");
        else
            status = QStringLiteral("Non ancora compilata.");
        if (!done) {
            QStringList left;
            if (open.doubts)
                left << plural(open.doubts, "dubbio", "dubbi");
            if (open.declared)
                left << plural(open.declared, "aggiunta o correzione", "aggiunte o correzioni");
            if (figuresLeft)
                left << plural(figuresLeft, "figura", "figure");
            if (movesLeft)
                left << plural(movesLeft, "mossa di impaginazione", "mosse di impaginazione");
            if (!left.isEmpty())
                status += QStringLiteral(" Da rivedere: ") + left.join(", ") + '.';
            else if (compiled == "good")
                status += QStringLiteral(" Niente da rivedere.");
        }
        row(QStringLiteral("Dubbi"), "rivedi:dubbi", allDoubts, done ? 0 : open.doubts);
        row(QStringLiteral("Correzioni"), "rivedi:correzioni", allDeclared, done ? 0 : open.declared);
        row(QStringLiteral("Impaginazione"), "rivedi:impaginazione", allMoves, done ? 0 : movesLeft);
        row(QStringLiteral("Figure"), "rivedi:figure", allFigures, done ? 0 : figuresLeft);
        lessons << QVariantMap{{"file", lesson.file},
                               {"rows", rows}, {"sources", sources}, {"status", status},
                               {"number", lesson.number},
                               {"date", lesson.date},
                               {"title", lesson.title.isEmpty() ? QFileInfo(lesson.file).completeBaseName() : lesson.title},
                               {"state", state},
                               {"done", m_settings.isDone(lesson.file)},
                               {"detail", detail}};
    }
    // il documento e, sotto, lo stile che usa; texfix.sty è del programma e non si mostra
    if (QFileInfo::exists(m_project.path(Project::MAIN)))
        others << QVariantMap{{"file", QString::fromUtf8(Project::MAIN)}, {"name", QString::fromUtf8(Project::MAIN)}};
    if (const QString style = m_settings.style.isEmpty() ? QString::fromUtf8(Project::DEFAULT_STYLE) : m_settings.style;
        QFileInfo::exists(m_project.path(style + ".sty")))
        others << QVariantMap{{"file", style + ".sty"}, {"name", style + ".sty"}, {"indent", 22}, {"detail", QStringLiteral("stile")}};
    if (!m_settings.profile.name.isEmpty())                // il profilo del corso: si apre nella sua finestra
        others << QVariantMap{{"file", QStringLiteral("profilo:")}, {"name", m_settings.profile.name}, {"indent", 22}, {"detail", QStringLiteral("profilo")}};
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
        jobs << QVariantMap{{"id", info.id}, {"title", info.title}, {"state", info.state}, {"kind", plans::load(m_project, info.id).value("tipo").toString()},
                            {"run", !run::load(m_project, info.id).isEmpty()}};   // avviato: si apre la finestra del lavoro
    QVariantList outcomes;                             // «AAAA-MM-GG HH.MM — titolo.md»: il titolo a sinistra, quando a destra
    for (const QString &name : QDir(m_project.path("salvataggi/esiti")).entryList({"*.md"}, QDir::Files, QDir::Name | QDir::Reversed)) {
        const QString stem = QFileInfo(name).completeBaseName();
        outcomes << QVariantMap{{"file", "salvataggi/esiti/" + name}, {"name", stem.section(" — ", 1)}, {"detail", stem.section(" — ", 0, 0)}};
    }
    QVariantList strays;                               // le fonti usate che nessuna lezione nomina
    for (const Used &u : used)
        if (!u.taken)
            strays << QVariantMap{{"file", u.file}, {"name", u.name}};
    const QVariantList documents = listing("docs");
    // in arrivo: ogni file ha uno stato (state: piano, lavoro, attesa, errore, smistare) e il suo perché
    QHash<QString, QString> inPlan;                    // file → il piano che lo contiene
    for (const plans::Info &info : plans::open(m_project)) {
        const QJsonObject plan = plans::load(m_project, info.id);
        if (plan.value("tipo").toString() == "materiale" && info.state == "piano")
            for (const QJsonValue &f : plan.value("file").toArray())
                inPlan.insert(f.toString(), info.id);
    }
    QSet<QString> waiting;
    for (const QStringList &files : std::as_const(m_sortWaiting))
        for (const QString &f : files)
            waiting.insert(f);
    QSet<QString> runFiles;                            // ciò che il lavoro di «Avvia» sta usando
    if (!m_runId.isEmpty() && m_runState != "finito") {
        const QJsonObject state = run::load(m_project, m_runId);
        for (const QJsonValue &v : state.value("fonti").toArray())
            runFiles.insert(texsource::parseSpec(v.toString()).path);
        for (const QJsonValue &t : state.value("compiti").toArray())
            for (const QJsonValue &v : t.toObject().value("fonti").toArray())
                runFiles.insert(v.toString());
    }
    QVariantList queue = listing("fonti/coda");
    for (QVariant &v : queue) {
        QVariantMap entry = v.toMap();
        const QString file = entry.value("file").toString();
        bool working = false;
        for (const QString &source : std::as_const(m_converting))
            working = working || texsource::parseSpec(source).path == file;
        working = working || runFiles.contains(file);
        if (working) {
            entry["state"] = "lavoro";
            entry["detail"] = QStringLiteral("in lavorazione");
        } else if (inPlan.contains(file)) {
            entry["state"] = "piano";
            entry["detail"] = QStringLiteral("nel piano");
        } else if (waiting.contains(file) || (m_jobTitle == QStringLiteral("Riconosco il materiale") && jobRunning())) {
            entry["state"] = "attesa";
            entry["detail"] = QStringLiteral("da riconoscere");
        } else if (m_unrecognized.contains(file)) {
            entry["state"] = "errore";
            entry["detail"] = QStringLiteral("non riconosciuto");
            entry["why"] = m_unrecognized.value(file);
        } else {
            entry["state"] = "smistare";
            entry["detail"] = QStringLiteral("da smistare");
        }
        v = entry;
    }
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
    // una lezione compilata da sola rifà solo i suoi avvisi: quelli delle altre restano (in Rivedi correzioni le
    // lezioni non spariscono e ricompaiono a ogni compilazione)
    QVariantList kept;
    if (!result.lesson.isEmpty())
        for (const QVariant &v : std::as_const(m_issues)) {
            const QString file = v.toMap().value("file").toString();
            if (!file.isEmpty() && file != result.lesson)
                kept << v;
        }
    m_issues = kept;
    m_errorCount = 0;
    for (const QVariant &v : std::as_const(kept))
        m_errorCount += v.toMap().value("kind").toString() == "error";
    int rows = 0, errors = 0;                          // di questa compilazione
    for (const Issue &issue : result.issues) {
        if (!result.lesson.isEmpty() && !issue.file.isEmpty() && issue.file != result.lesson)
            continue;                                  // di un altro file: c'è già, dall'ultima compilazione del documento
        const bool error = issue.kind == Issue::Error;
        (error ? counts[issue.file].errors : counts[issue.file].rows)++;
        (error ? errors : rows)++;
        m_errorCount += error;
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
    if (errors)
        say(QStringLiteral("%1 con %2.").arg(what, plural(errors, "errore", "errori")));
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
    if (!hasProject() || clean == m_settings.profile.extra)
        return;
    m_settings.profile.extra = clean;
    profiles::saveToProject(m_project, m_settings.profile);
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
    const rules::Profile origin = profiles::load(m_settings.profile.name);
    for (const rules::Heading &h : rules::profileSections()) {
        const QString id = QString::fromUtf8(h.id), text = m_settings.profile.sections.value(id);
        out << QVariantMap{{"id", id}, {"title", QString::fromUtf8(h.title)}, {"text", text}, {"custom", text != origin.sections.value(id)}};
    }
    return out;
}

void Workspace::setRuleText(const QString &id, const QString &text)
{
    if (!hasProject() || m_settings.profile.sections.value(id) == text.trimmed())
        return;
    m_settings.profile.sections.insert(id, text.trimmed());
    profiles::saveToProject(m_project, m_settings.profile);
    emit settingsChanged();
}

void Workspace::resetRule(const QString &id)
{
    bool ok = false;
    const rules::Profile origin = profiles::load(m_settings.profile.name, &ok);
    if (ok)
        setRuleText(id, origin.sections.value(id));
}

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

void Workspace::resume()
{
    if (!m_runId.isEmpty())                            // il lavoro di «Avvia»: riprende tutto, conversione e compiti dell'agente
        return resumeRun();
    QHash<QString, Convert::Wanted> wanted;                       // con numero, data e argomento scelti nel piano
    const QStringList sources = Convert::interrupted(m_project, &wanted);
    convert(sources, wanted);
}

void Workspace::convert(const QStringList &sources, const QHash<QString, Convert::Wanted> &wanted)
{
    if (!hasProject() || sources.isEmpty())
        return say(QStringLiteral("Non c'è niente da scrivere."));
    if (busy())
        return say(QStringLiteral("C'è già un lavoro in corso."));
    checkpoint(QStringLiteral("Modifiche a mano"));
    m_converting = sources;
    startJob(QStringLiteral("Scrivo le lezioni"), [this, project = m_project, settings = m_settings, style = m_style, sources, wanted](JobControl *control) {
        const Convert::Result result = Convert(project, settings, style, control).run(sources, wanted);
        return std::function<void()>([this, result] { convertFinished(result); });
    });
}

// Un lavoro: gira qui dentro, in un thread suo (nessun altro processo, nessun file che fa da tramite); ciò che
// restituisce gira alla fine nel thread della finestra.
void Workspace::startJob(const QString &title, std::function<std::function<void()>(JobControl *)> work, bool forRun)
{
    m_jobControl = std::make_unique<JobControl>();
    m_jobControl->onPause = [this](const QDateTime &until, bool connection) {   // il limite, o la rete che non torna
        QMetaObject::invokeMethod(this, [this, until, connection] {
            m_pausedUntil = until;
            m_pauseConnection = connection;
            if (connection)
                say(QStringLiteral("La connessione non è tornata: finisco ciò che è in corso e mi fermo. Si riprende quando c'è."));
            else if (until.isValid())
                say(QStringLiteral("Limite di utilizzo di Claude raggiunto: finisco ciò che è in corso e mi fermo. Si può riprendere %1.").arg(pausedUntil()));
            emit jobProgressChanged();
            emit runChanged();
        });
    };
    m_jobControl->onOffline = [this](bool on) {         // le chiamate aspettano la rete: lo si dice, e quando torna
        QMetaObject::invokeMethod(this, [this, on] {
            m_offline = on;
            say(on ? QStringLiteral("Manca la connessione: il lavoro aspetta che torni (al più %1 minuti), poi rifà le chiamate.").arg(Model::OFFLINE_WAIT / 60000)
                   : QStringLiteral("La connessione è tornata: il lavoro continua."));
            emit jobProgressChanged();
            emit runChanged();
        });
    };
    if (forRun)
        m_jobControl->onEvent = [this](const QJsonObject &e) { QMetaObject::invokeMethod(this, [this, e] { runEvent(e); }); };
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
        std::function<void()> finish;
        try {                                          // niente che esca dal lavoro può chiudere il programma
            finish = work(control);
        } catch (const LimitPause &) {
            finish = [this, connection = control->pausedForConnection()] {     // il controllo non c'è più, quando gira
                say(connection ? QStringLiteral("Lavoro fermato: manca la connessione. Ciò che è fatto resta: rifallo quando torna.")
                                                   : QStringLiteral("Lavoro fermato: limite di utilizzo di Claude raggiunto. Si può riprendere %1.").arg(pausedUntil()));
            };
        } catch (const Stopped &) {
            finish = [this] { say(QStringLiteral("Lavoro fermato.")); };
        } catch (const std::exception &e) {
            const QString why = QString::fromUtf8(e.what());
            finish = [this, why] { say(QStringLiteral("Il lavoro si è interrotto: %1").arg(why)); };
        }
        QMetaObject::invokeMethod(this, [this, finish] {
            m_jobThread->wait();
            m_jobThread->deleteLater();
            m_jobThread = nullptr;
            const bool stopped = m_jobControl && (m_jobControl->stopped() || m_jobControl->pausedUntil().isValid());
            m_jobControl.reset();
            m_recent.prepend(QVariantMap{{"title", m_jobTitle}, {"when", QTime::currentTime().toString("HH:mm")}, {"stopped", stopped}});
            while (m_recent.size() > 12)
                m_recent.removeLast();
            finish();
            m_pausedUntil = {};
            m_pauseConnection = m_offline = false;
            emit jobChanged();
        });
    });
    m_jobThread->start();
    emit jobChanged();
    emit jobProgressChanged();
}

void Workspace::convertFinished(const Convert::Result &result)
{
    m_converting.clear();
    checkpoint(QStringLiteral("Aggiungi materiale"));  // tutto il lavoro è un passo solo
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
    emit outcomeReady(QVariantMap{{"title", result.stopped ? QStringLiteral("Aggiungi materiale: fermato") : QStringLiteral("Aggiungi materiale: esito")},
                                  {"lines", lines},
                                  {"cost", QStringLiteral("%1 $").arg(italian.toString(result.cost, 'f', 2))},
                                  {"minutes", italian.toString(result.minutes, 'f', 1)}});
    say(QStringLiteral("Aggiungi materiale: %1.").arg(plural(result.written(), "lezione scritta", "lezioni scritte")));
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

namespace {
// La pagina della fonte da cui viene una figura, in immagine (la si rende se `make`; altrimenti vuoto se non c'è ancora).
QString pageImageOf(const Project &project, const figures::Figure &figure, bool make)
{
    if (figure.source.isEmpty() || figure.page <= 0)
        return {};
    const QString dir = project.path("build/lavori/figure/pagine/" + figure.source);
    if (!make) {
        const QString full = dir + "/full-" + QString::number(figure.page) + ".png";
        if (QFileInfo::exists(full))
            return full;
        for (const QString &name : QDir(dir).entryList({"full-*.png"}, QDir::Files, QDir::Name))   // pdftoppm: full-01, full-001
            if (name.section('-', -1).section('.', 0, 0).toInt() == figure.page)
                return dir + "/" + name;
        return {};
    }
    QString source;
    for (QDirIterator it(project.path("fonti"), {figure.source}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();)
        source = it.next();
    if (source.isEmpty())
        return {};
    return Pages::read(source, dir).full.value(figure.page - 1);
}
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
            // di che cosa si tratta: la didascalia, o ciò che ne ha detto la conversione
            const QString caption = figures::caption(m_project, m_style, f.id);
            const QString origin = figures::origin(m_project, f);
            QVariantList box, erase;
            for (const double v : f.box)
                box << v;
            for (const QList<double> &z : f.erase)
                erase << QVariant(QVariantList{z[0], z[1], z[2], z[3]});
            rows << QVariantMap{{"type", "figure"}, {"id", f.id}, {"lesson", lesson.file}, {"name", QStringLiteral("Figura %1").arg(++n)},
                                {"description", caption.isEmpty() ? f.description : caption}, {"state", f.state}, {"note", f.note},
                                {"source", f.source}, {"page", f.page}, {"decision", f.decision}, {"origin", origin},
                                {"line", int(text.left(at).count('\n')) + 1}, {"render", figures::rendered(m_project, f.id)},
                                {"original", figures::originalPath(m_project, f)}, {"pageImage", origin == "fonte" ? pageImageOf(m_project, f, false) : QString()},
                                {"hasDrawing", f.state == "disegnata" || !figures::drawing(m_project, f.id).isEmpty()},
                                {"box", box}, {"erase", erase}};
        }
        if (rows.isEmpty())
            continue;
        out << QVariantMap{{"type", "lesson"}, {"lesson", lesson.file},
                           {"title", lesson.number > 0 ? QStringLiteral("Lezione %1 · %2").arg(lesson.number).arg(lesson.title) : lesson.title}};
        out += rows;
    }
    return out;
}

void Workspace::redoFigure(const QString &id, const QString &how, const QString &instruction)
{
    if (!hasProject())
        return;
    if (busy())
        return say(QStringLiteral("C'è già un lavoro in corso."));
    checkpoint(QStringLiteral("Modifiche a mano"));
    const bool crop = how == "ritaglia";
    startJob(crop ? QStringLiteral("Ritaglio la figura") : QStringLiteral("Ridisegno la figura"),
             [this, id, crop, instruction, project = m_project, settings = m_settings, style = m_style](JobControl *control) {
                 QString error;
                 double cost = 0;
                 try {
                     const figures::Figure figure = figures::find(project, id);
                     // la pagina della fonte, dalle fonti usate; per una figura dell'autore, la resa del suo codice
                     QString page = pageImageOf(project, figure, true);
                     if (figures::origin(project, figure) == "autore" && figures::original(project, figure, {}))
                         page = figures::originalPath(project, figure);
                     Model model(settings.quality, project.root(), control);
                     error = crop ? figures::crop(project, figure, page) : figures::draw(project, settings, style, model, figure, page, instruction);
                     figures::render(project, id);           // com'è venuta, per Rivedi figure
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

int Workspace::unadoptedFigures() const { return hasProject() ? figures::unadopted(m_project, m_style) : 0; }

// Le figure scritte fuori da Converti entrano nel registro: il loro segno va nelle lezioni.
void Workspace::adoptFigures()
{
    if (!hasProject() || busy())
        return;
    checkpoint(QStringLiteral("Modifiche a mano"));
    const int n = figures::adopt(m_project, m_style);
    if (n == 0)
        return;
    checkpoint(QStringLiteral("Figure nel registro"));
    refreshTree();
    emit filesChanged();
}

// Rivedi figure mostra ogni figura com'è nella lezione e accanto il suo originale: si fanno qui, in un lavoro, quelle che
// mancano (una compilazione per figura). Una volta per codice: se una non compila non si ritenta finché non cambia.
void Workspace::renderFigures()
{
    if (!hasProject() || busy())
        return;
    QList<figures::Figure> todo;
    for (const figures::Figure &f : figures::all(m_project)) {
        if (figures::lessonOf(m_project, f.id).isEmpty())
            continue;
        const bool missing = figures::rendered(m_project, f.id).isEmpty()
                             || (!figures::origin(m_project, f).isEmpty() && figures::originalPath(m_project, f).isEmpty());
        const QString key = f.id + '\n' + figures::code(m_project, f.id);
        if (missing && !m_figuresTried.contains(key)) {
            m_figuresTried.insert(key);
            todo << f;
        }
    }
    if (todo.isEmpty())
        return;
    startJob(QStringLiteral("Preparo le figure"), [this, todo, project = m_project](JobControl *control) {
        try {
            for (int i = 0; i < todo.size(); ++i) {
                control->check();
                control->progress(double(i) / todo.size(), todo[i].id);
                figures::render(project, todo[i].id);
                if (!figures::origin(project, todo[i]).isEmpty()) {
                    QString page;
                    try {
                        page = pageImageOf(project, todo[i], true);
                    } catch (const std::exception &) {
                    }
                    figures::original(project, todo[i], page);
                }
            }
        } catch (const Stopped &) {
        }
        return std::function<void()>([this] { refreshTree(); });
    });
}

void Workspace::acceptFigure(const QString &id, bool accepted)
{
    if (!hasProject())
        return;
    figures::decide(m_project, id, accepted);
    refreshTree();
}

// Accetta (o riapre) tutte le figure di una lezione; con la lezione vuota, tutte quelle del progetto.
void Workspace::acceptAllFigures(const QString &lesson, bool accepted)
{
    if (!hasProject())
        return;
    for (const figures::Figure &f : figures::all(m_project)) {
        const QString in = figures::lessonOf(m_project, f.id);
        if (!in.isEmpty() && (lesson.isEmpty() || in == lesson) && (f.decision == "accettata") != accepted)
            figures::decide(m_project, f.id, accepted);
    }
    refreshTree();
}

// Disegno o ritaglio, per una figura che viene da una fonte: si torna all'ultimo disegno senza rifarlo.
void Workspace::useDrawing(const QString &id)
{
    if (!hasProject() || busy())
        return;
    checkpoint(QStringLiteral("Modifiche a mano"));
    const QString error = figures::useDrawing(m_project, id);
    if (!error.isEmpty())
        return say(error);
    checkpoint(QStringLiteral("Figura: di nuovo il disegno"));
    refreshTree();
    emit filesChanged();
}

// Il ritaglio scelto a mano: riquadro e cancellino (in frazioni della pagina), poi si ritaglia di nuovo.
void Workspace::cropFigure(const QString &id, const QVariantList &box, const QVariantList &erase)
{
    if (!hasProject() || busy())
        return;
    figures::Figure f = figures::find(m_project, id);
    if (f.id.isEmpty() || box.size() != 4)
        return;
    f.box.clear();
    for (const QVariant &v : box)
        f.box << v.toDouble();
    f.erase.clear();
    for (const QVariant &z : erase) {
        const QVariantList zone = z.toList();
        if (zone.size() == 4)
            f.erase << QList<double>{zone[0].toDouble(), zone[1].toDouble(), zone[2].toDouble(), zone[3].toDouble()};
    }
    f.boxChosen = true;
    figures::save(m_project, f);
    redoFigure(id, "ritaglia");
}

QString Workspace::figurePage(const QString &id) const
{
    return hasProject() ? pageImageOf(m_project, figures::find(m_project, id), false) : QString();
}

QVariantList Workspace::errorLessons() const
{
    QVariantList out;
    if (!hasProject())
        return out;
    for (const Lesson &lesson : m_project.lessons())
        if (m_states.value(lesson.file) == "bad" && !m_settings.isDone(lesson.file))
            out << QVariantMap{{"file", lesson.file}, {"number", lesson.number},
                               {"errors", m_details.value(lesson.file).section(' ', 0, 0).toInt()}, {"detail", m_details.value(lesson.file)}};
    return out;
}

void Workspace::fixAll(const QString &only, bool errorsOnly)
{
    if (!hasProject())
        return;
    if (busy())
        return say(QStringLiteral("C'è già un lavoro in corso."));
    // le lezioni con errori o righe fuori misura nell'ultima compilazione; quelle fatte si saltano
    QStringList lessons;
    for (const QString &file : m_project.inputs())
        if ((only.isEmpty() || file == only) && !m_settings.isDone(file) && (m_states.value(file) == "bad" || (!errorsOnly && m_states.value(file) == "warn")) && !m_details.value(file).isEmpty())
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
    paginateLessons(lessons, QStringLiteral("Impagino"));
}

// «Impagina di nuovo» su una lezione cambiata dopo l'ultima impaginazione: dal punto cambiato in giù (ciò che sta sopra
// resta deciso).
void Workspace::paginateLesson(const QString &file)
{
    if (!hasProject())
        return;
    if (busy())
        return say(QStringLiteral("C'è già un lavoro in corso."));
    if (!layout::needed(m_project, file))
        return say(QStringLiteral("Niente di nuovo da impaginare."));
    paginateLessons({file}, QStringLiteral("Impagino %1").arg(QFileInfo(file).fileName()));
}

// Le pagine in piccolo del «dopo» sono sempre quelle della lezione com'è: se è cambiata (a mano, con la cronologia) si
// rifanno, compilando la lezione una volta. La lezione non si tocca. Una volta per testo: se non compila non si ritenta
// finché non cambia ancora.
void Workspace::refreshShots()
{
    if (!hasProject() || busy())
        return;
    QList<QPair<QString, QStringList>> work;
    for (const QString &file : m_project.inputs()) {
        if (!QFileInfo::exists(m_project.path(file)))
            continue;
        const QStringList blocks = layout::oldShots(m_project, file);
        const QByteArray hash = QCryptographicHash::hash(files::read(m_project.path(file)).toUtf8(), QCryptographicHash::Sha1);
        if (blocks.isEmpty() || m_shotsTried.value(file) == hash)
            continue;
        m_shotsTried.insert(file, hash);
        work << qMakePair(file, blocks);
    }
    if (work.isEmpty())
        return;
    startJob(QStringLiteral("Rifaccio le pagine in piccolo"), [this, work, project = m_project, style = m_style](JobControl *control) {
        try {
            for (int i = 0; i < work.size(); ++i) {
                control->progress(double(i) / work.size(), QFileInfo(work[i].first).fileName());
                layout::retake(project, style, work[i].first, work[i].second, control);
            }
        } catch (const Stopped &) {
        }
        return std::function<void()>([this] { refreshTree(); });
    });
}

void Workspace::paginateLessons(const QStringList &lessons, const QString &title)
{
    checkpoint(QStringLiteral("Modifiche a mano"));
    startJob(title, [this, lessons, project = m_project, style = m_style](JobControl *control) {
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
        // l'ora del file: una pagina rifatta ha lo stesso nome, e la finestra deve accorgersi che è cambiata
        out << QVariantMap{{"path", shot.path}, {"rects", rects}, {"stamp", QFileInfo(shot.path).lastModified().toMSecsSinceEpoch()}};
    }
    return out;
}
}

namespace {
// Com'erano la lezione e le sue mosse prima di una modifica a mano applicata: «annulla la scelta» le rimette.
QString manualFile(const Project &project, const QString &lesson, const QString &block)
{
    return project.path(QStringLiteral("build/impaginazione/%1/%2-prima-della-modifica.json").arg(QFileInfo(lesson).completeBaseName(), block));
}
}

void Workspace::keepManual(const QString &lesson, const QString &block, const QString &text, const QString &registry)
{
    if (!hasProject())
        return;
    const QJsonValue entry = QJsonDocument::fromJson(registry.toUtf8()).object().value(lesson);
    files::write(manualFile(m_project, lesson, block), QString::fromUtf8(QJsonDocument(QJsonObject{{"testo", text}, {"mosse", entry}, {"applicata", layout::written(files::read(m_project.path(lesson)))}}).toJson()));
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
            out << QVariantMap{{"type", "todo"}, {"lesson", lesson.file}, {"fresh", !layout::known(m_project, lesson.file)}};
        // la lezione è cambiata da quando è stata misurata: le mosse dal punto cambiato in giù guardavano una pagina che non
        // c'è più. Restano visibili, «da rifare», senza pagine in piccolo, finché la rimpaginazione non le rifà.
        const QStringList stale = layout::changedBlocks(m_project, lesson.file);
        const QStringList oldShots = layout::oldShots(m_project, lesson.file);   // le pagine del dopo non sono più vere
        QList<layout::Move> ordered;                   // in ordine di documento: l'ordine conta
        QList<layout::Move> staleMoves;
        for (const layout::Move &m : moves)
            (stale.contains(m.block) ? staleMoves : ordered) << m;
        std::stable_sort(ordered.begin(), ordered.end(), [](const layout::Move &a, const layout::Move &b) { return a.line < b.line; });
        std::stable_sort(staleMoves.begin(), staleMoves.end(), [](const layout::Move &a, const layout::Move &b) { return a.line < b.line; });
        // l'ordine: la mossa attuale è la prima ancora da rivedere; quelle prima sono chiuse (non si toccano più, se non
        // annullando le scelte a ritroso), quelle dopo aspettano
        const QStringList openStates{"fatta", "proposta", "segnalata", "a_mano", "agente"};
        qsizetype now = ordered.size();
        for (qsizetype i = ordered.size() - 1; i >= 0; --i)
            if (openStates.contains(ordered[i].state))
                now = i;
        qsizetype index = -1;
        for (const layout::Move &m : ordered) {
            ++index;
            const QString role = index < now ? QStringLiteral("chiusa") : index == now ? QStringLiteral("attuale") : QStringLiteral("attesa");
            const bool locked = role != "attuale";
            // «annulla la scelta»: sull'ultima mossa decisa, quella subito prima dell'attuale (o l'ultima se sono tutte decise)
            const QString undo = index == now - 1 ? m.block : QString();
            out << QVariantMap{{"type", "move"}, {"lesson", m.lesson}, {"block", m.block}, {"what", m.what}, {"state", m.state}, {"locked", locked}, {"role", role}, {"undo", undo}, {"origin", m.origin}, {"same", m.same},
                               {"byHand", QFileInfo::exists(manualFile(m_project, m.lesson, m.block))},
                               {"hole", qRound(m.hole * 100)}, {"line", m.line},
                               {"before", shotList(m_project, m, "prima")}, {"after", oldShots.contains(m.block) ? QVariantList() : shotList(m_project, m, "dopo")},
                               {"oldShots", oldShots.contains(m.block)}};
        }
        for (const layout::Move &m : std::as_const(staleMoves))
            out << QVariantMap{{"type", "move"}, {"lesson", m.lesson}, {"block", m.block}, {"what", m.what}, {"state", m.state}, {"locked", true}, {"role", "rifare"}, {"undo", QString()}, {"origin", m.origin}, {"same", false},
                               {"byHand", false}, {"hole", qRound(m.hole * 100)}, {"line", m.line}, {"before", QVariantList()}, {"after", QVariantList()}};
    }
    return out;
}

// L'ordine conta: le mosse non ancora decise che vengono dopo un blocco si tolgono e si ricalcolano.
void Workspace::redoFollowing(const QString &lesson, const QString &block, bool retake)
{
    // il seguito si rimpagina se aveva mosse non decise, o se la lezione è cambiata da quando è stata misurata (una
    // modifica a mano applicata): ciò che la modifica ha spostato più sotto va guardato di nuovo
    const bool following = layout::dropAfter(m_project, lesson, block, retake) > 0 || layout::needed(m_project, lesson);
    if (!following && !retake)
        return;
    refreshTree();
    emit filesChanged();
    if (jobRunning())
        return say(QStringLiteral("Il seguito della lezione va rimpaginato: «Impagina ciò che è nuovo» quando il lavoro in corso finisce."));
    startJob(following ? QStringLiteral("Rimpagino il seguito") : QStringLiteral("Rifaccio le pagine in piccolo"),
             [this, lesson, block, following, retake, project = m_project, style = m_style](JobControl *control) {
        control->progress(0.1, QFileInfo(lesson).fileName());
        layout::Result r;
        try {
            if (retake)                                // sistemata a mano: com'è venuta si vede dalla lezione com'è adesso
                layout::retake(project, style, lesson, block, control);
            if (following)
                r = layout::lesson(project, style, lesson, control);
        } catch (const Stopped &) {
            r.failure = QStringLiteral("Lavoro fermato.");
        }
        return std::function<void()>([this, r, following] {
            if (following)
                checkpoint(QStringLiteral("Impaginazione del seguito"));
            refreshTree();
            emit filesChanged();
            if (following)
                say(r.failure.isEmpty() ? QStringLiteral("Seguito rimpaginato: %1 mosse fatte, %2 da rivedere.").arg(r.done).arg(r.proposed) : r.failure);
            m_compiler.compileDocument();
        });
    });
}

// Annulla la scelta su una mossa: torna com'era quando il programma l'ha lasciata, e ciò che segue si ricalcola.
void Workspace::reopenMove(const QString &lesson, const QString &block)
{
    if (m_agentRunning || jobRunning())
        return say(QStringLiteral("Aspetta la fine del lavoro in corso."));
    checkpoint(QStringLiteral("Modifiche a mano"));
    const QString textBefore = files::read(m_project.path(lesson));
    // una mossa sistemata a mano: la lezione e le sue mosse tornano com'erano prima della modifica
    const QString kept = manualFile(m_project, lesson, block);
    const QJsonObject saved = QJsonDocument::fromJson(files::read(kept).toUtf8()).object();
    QFile::remove(kept);
    // solo se, a parte le mosse, la lezione è ancora quella della modifica: ciò che è stato scritto dopo non si butta
    if (saved.contains("testo") && saved.value("applicata").toString() != layout::written(textBefore)) {
        say(QStringLiteral("La lezione è cambiata dopo la modifica a mano: annullo la scelta, il testo resta com'è."));
    } else if (saved.contains("testo")) {
        const QString registryPath = m_project.path("salvataggi/impaginazione.json");
        QJsonObject all = QJsonDocument::fromJson(files::read(registryPath).toUtf8()).object();
        all[lesson] = saved.value("mosse");
        files::write(m_project.path(lesson), saved.value("testo").toString());
        files::write(registryPath, QString::fromUtf8(QJsonDocument(all).toJson()));
        decided(lesson, QStringLiteral("Modifica a mano annullata"), {});
        say(QStringLiteral("Modifica a mano annullata: la lezione è tornata com'era."));
        if (!jobRunning())
            m_compiler.compileDocument();
        return;
    }
    decided(lesson, QStringLiteral("Scelta annullata"), layout::reopen(m_project, lesson, block));
    if (files::read(m_project.path(lesson)) != textBefore)
        redoFollowing(lesson, block);
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
    if (redo && (state == "risolta" || files::read(m_project.path(lesson)) != textBefore))
        redoFollowing(lesson, block, state == "risolta");
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

// Le modifiche al piano, una per una sul file: QML non tocca il piano annidato (gli elenchi che riceve sono copie).
void Workspace::setPlanEntry(const QString &id, int section, int entry, const QString &key, const QVariant &value)
{
    QJsonObject plan = plans::load(m_project, id);
    QJsonArray sections = plan.value("sezioni").toArray();
    QJsonObject s = sections.at(section).toObject();
    QJsonArray entries = s.value("voci").toArray();
    if (!hasProject() || (key != "fare" && key != "nota") || entry < 0 || entry >= entries.size())
        return;
    QJsonObject e = entries.at(entry).toObject();
    e[key] = QJsonValue::fromVariant(value);
    entries[entry] = e;
    s["voci"] = entries;
    sections[section] = s;
    plan["sezioni"] = sections;
    plans::save(m_project, id, plan);
    if (key == "fare")                                 // un'istruzione si scrive: rileggere il piano a ogni tasto toglierebbe il cursore
        emit planEdited(id);
}

void Workspace::setPlanChoice(const QString &id, int section, const QString &field, const QString &value)
{
    QJsonObject plan = plans::load(m_project, id);
    QJsonArray sections = plan.value("sezioni").toArray();
    QJsonObject s = sections.at(section).toObject(), choice = s.value(field).toObject();
    if (!hasProject() || (field != "scelta" && field != "intreccio") || !choice.value("valori").toArray().contains(value))
        return;
    choice["valore"] = value;
    s[field] = choice;
    sections[section] = s;
    plan["sezioni"] = sections;
    plans::save(m_project, id, plan);
    emit planEdited(id);
}

void Workspace::setPlanAnswer(const QString &id, int question, const QString &answer)
{
    QJsonObject plan = plans::load(m_project, id);
    QJsonArray questions = plan.value("domande").toArray();
    QJsonObject q = questions.at(question).toObject();
    if (!hasProject() || !q.value("opzioni").toArray().contains(answer))
        return;
    q["risposta"] = answer;
    questions[question] = q;
    plan["domande"] = questions;
    plans::save(m_project, id, plan);
    emit planEdited(id);
}

void Workspace::setPlanField(const QString &id, const QString &list, int index, const QString &key, const QVariant &value)
{
    static const QHash<QString, QStringList> values{{"comuni", {"one", "both", "skip"}}, {"versioni", {"porta", "nuovo", "salta"}}};
    QJsonObject plan = plans::load(m_project, id);
    QJsonArray items = plan.value(list).toArray();
    if (!hasProject() || index < 0 || index >= items.size() || !QStringList{"comuni", "destinazioni", "versioni"}.contains(list))
        return;
    const bool allowed = (key == "valore" && values.value(list).contains(value.toString())) || (list == "destinazioni" && (key == "fare" || key == "nota"));
    if (!allowed)
        return;
    QJsonObject item = items.at(index).toObject();
    item[key] = QJsonValue::fromVariant(value);
    items[index] = item;
    plan[list] = items;
    plans::save(m_project, id, plan);
    if (key != "nota")                                 // un'istruzione si scrive: rileggere a ogni tasto toglierebbe il cursore
        emit planEdited(id);
}

void Workspace::setPlanOrder(const QString &id, bool keep)
{
    QJsonObject plan = plans::load(m_project, id);
    if (!hasProject() || !plan.contains("ordine"))
        return;
    QJsonObject order = plan.value("ordine").toObject();
    order["valore"] = keep;
    plan["ordine"] = order;
    plans::save(m_project, id, plan);
    emit planEdited(id);
}

void Workspace::setPlanComment(const QString &id, const QString &text)
{
    QJsonObject plan = plans::load(m_project, id);
    if (!hasProject() || plan.isEmpty())
        return;
    plan["commento"] = text;
    plans::save(m_project, id, plan);
}

void Workspace::setOutlineNote(const QString &id, const QString &text)
{
    QJsonObject plan = plans::load(m_project, id);
    if (!hasProject() || plan.isEmpty())
        return;
    plan["nota_scaletta"] = text;
    plans::save(m_project, id, plan);
}

// Ci sono istruzioni per rifare il piano: quelle scritte, o (nel piano dei concetti) un programma del corso diverso da
// quello con cui è stata fatta la mappa.
bool Workspace::planHasInstructions(const QString &id) const
{
    const QJsonObject plan = plans::load(m_project, id);
    return plans::hasInstructions(plan) || (plan.value("formato").toString() == "concetti" && plan.value("stato").toString() == "piano" && plan.value("programma").toString() != m_settings.program);
}

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
}

void Workspace::archivePlan(const QString &id)
{
    const QString file = plans::archive(m_project, id);
    run::forget(m_project, id);                        // un lavoro di «Avvia» letto e archiviato: via i registri e la copia
    if (id == m_runId) {
        m_runId.clear();
        m_runState.clear();
        emit runChanged();
    }
    refreshTree();
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

void Workspace::addMaterial(const QStringList &paths, const QString &lesson)
{
    if (!hasProject() || paths.isEmpty())
        return;
    checkpoint(QStringLiteral("Modifiche a mano"));
    int skipped = 0;
    const QStringList added = newlesson::toQueue(m_project, paths, lesson, &skipped);
    // nella prima frase: la barra mostra solo quella
    const QString left = skipped == 0 ? QString()
                       : skipped == 1 ? QStringLiteral(" (uno lasciato fuori: Texfix non sa leggerlo)")
                       : QStringLiteral(" (%1 lasciati fuori: Texfix non sa leggerli)").arg(skipped);
    if (added.isEmpty())
        return say(skipped > 0 ? QStringLiteral("Niente da aggiungere%1.").arg(left) : QStringLiteral("Non riesco a copiare i file nel progetto."));
    checkpoint(QStringLiteral("Materiale aggiunto"));
    refreshTree();                                     // resta in arrivo: lo riconosce «Smista», quando l'utente vuole
    say(QStringLiteral("%1 in arrivo%2: «Smista» %3 riconosce e fa il piano.").arg(plural(added.size(), "file", "file"), left, added.size() == 1 ? "lo" : "li"));
}

void Workspace::newVersion(const QString &used, const QString &path)
{
    if (!hasProject() || !QFileInfo(path).isFile())
        return;
    // arriva con il nome della fonte usata: il piano la riconosce come sua versione nuova
    checkpoint(QStringLiteral("Modifiche a mano"));
    QDir().mkpath(m_project.path("fonti/coda"));
    const QString target = files::freeName(m_project.path("fonti/coda/" + QFileInfo(used).completeBaseName() + '.' + QFileInfo(path).suffix()));
    if (!QFile::copy(path, target))
        return say(QStringLiteral("Non riesco a copiare il file nel progetto."));
    checkpoint(QStringLiteral("Materiale aggiunto"));
    refreshTree();
    say(QStringLiteral("%1 è in arrivo: «Smista» la riconosce come versione nuova.").arg(QFileInfo(target).fileName()));
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

void Workspace::sortMaterial(const QStringList &sources) { sortJob(sources, {}, {}); }

void Workspace::reviseSortPlan(const QString &id)
{
    const QJsonObject plan = plans::load(m_project, id);
    QStringList sources;
    for (const QJsonValue &f : plan.value("file").toArray())
        if (QFileInfo::exists(m_project.path(f.toString())))
            sources << f.toString();
    sortJob(sources, plan, id);
}

void Workspace::sortJob(const QStringList &sources, const QJsonObject &previous, const QString &replaces)
{
    if (!hasProject() || sources.isEmpty())
        return;
    if (busy()) {                                      // si riconosce appena niente lavora: intanto la riga lo dice
        m_sortWaiting.append(sources);
        refreshTree();
        return say(QStringLiteral("Riconosco il materiale appena finisce il lavoro in corso."));
    }
    for (const QString &source : sources)
        m_unrecognized.remove(source);
    startJob(QStringLiteral("Riconosco il materiale"), [this, sources, previous, replaces, project = m_project, settings = m_settings](JobControl *control) {
        const sort::Outcome outcome = sort::plan(project, settings, sources, control, previous);
        return std::function<void()>([this, outcome, sources, replaces, project] {
            for (auto it = outcome.unread.begin(); it != outcome.unread.end(); ++it)
                m_unrecognized.insert(it.key(), it.value());
            if (outcome.plan.isEmpty()) {              // niente in silenzio: il perché resta accanto a ogni file
                const QString why = outcome.failure.isEmpty() ? QStringLiteral("non sono riuscito a fare il piano") : outcome.failure;
                for (const QString &source : sources)
                    if (!m_unrecognized.contains(source))
                        m_unrecognized.insert(source, why);
                saveUnrecognized();
                refreshTree();
                const QString said = why.left(1).toLower() + why.mid(1);
                return say(QStringLiteral("Materiale non riconosciuto: %1 Resta in arrivo; «Riprova» lo rifà.").arg(said.endsWith('.') ? said : said + '.'));
            }
            plans::discard(project, replaces);         // il piano rifatto prende il posto di quello di prima
            saveUnrecognized();
            refreshTree();
            say(QStringLiteral("Piano del materiale pronto. Costo: %1 $.").arg(QLocale(QLocale::Italian).toString(outcome.cost, 'f', 2)));
            emit planReady(outcome.plan);
        });
    });
    refreshTree();                                     // le righe in arrivo dicono che si sta riconoscendo
}

void Workspace::conceptSurvey(const QStringList &sources)
{
    if (!hasProject() || sources.isEmpty())
        return;
    if (busy())
        return say(QStringLiteral("C'è già un lavoro in corso."));
    startJob(QStringLiteral("Leggo i concetti"), [sources, project = m_project, quality = m_settings.quality, this](JobControl *control) {
        const concepts::Survey s = concepts::survey(project, quality, sources, control);
        return std::function<void()>([this, s] {
            say(s.failure.isEmpty() ? QStringLiteral("Mappa dei concetti pronta. Costo: %1 $.").arg(QLocale(QLocale::Italian).toString(s.cost, 'f', 2))
                                    : QStringLiteral("Mappa dei concetti: %1").arg(s.failure));
        });
    });
}

void Workspace::nextSort()
{
    if (m_sortWaiting.isEmpty() || busy() || !hasProject())
        return;
    const QStringList sources = m_sortWaiting.takeFirst();
    QStringList left;
    for (const QString &source : sources)
        if (QFileInfo::exists(m_project.path(source)))
            left << source;
    sortJob(left, {}, {});
}

void Workspace::saveUnrecognized()
{
    QJsonObject saved;
    for (auto it = m_unrecognized.begin(); it != m_unrecognized.end(); ++it)
        if (QFileInfo::exists(m_project.path(it.key())))
            saved.insert(it.key(), it.value());
    files::write(m_project.path(UNRECOGNIZED), QString::fromUtf8(QJsonDocument(saved).toJson()));
}

void Workspace::startSortPlan(const QString &id)
{
    if (busy())
        return say(QStringLiteral("C'è già un lavoro in corso."));
    checkpoint(QStringLiteral("Modifiche a mano"));
    // un lavoro nuovo: la finestra comincia vuota; quello di prima, se c'era e non era archiviato, va fra gli esiti
    if (!m_runId.isEmpty() && m_runId != id && m_runState == "finito")
        archivePlan(m_runId);
    m_runId = id;
    m_runSites.clear();
    m_runCalls.clear();
    m_runSiteAt.clear();
    m_runCallAt.clear();
    m_runLessonSite.clear();
    m_runStreams.clear();
    m_runSteps.clear();
    m_runStepAt.clear();
    m_runOutcome.clear();
    m_runWorked = 0;
    m_runEstimate = plans::load(m_project, id).value("stima").toDouble();
    startRun(id);
}

void Workspace::startRun(const QString &id)
{
    m_runStarted = QDateTime::currentMSecsSinceEpoch();   // da qui il tempo lavorato cresce
    m_runState = QStringLiteral("in_corso");
    m_runUntil = {};
    m_runConnection = false;
    m_runOutcome.clear();
    emit runChanged();
    emit runOpened(id);
    startJob(QStringLiteral("Aggiungi materiale"), [this, id, project = m_project, settings = m_settings, style = m_style, last = lastRunId()](JobControl *control) {
        control->callsAfter(last);                    // ripreso: le chiamate e i passi nuovi non prendono il posto dei vecchi
        const run::Result result = run::execute(project, settings, style, id, control);
        return std::function<void()>([this, result] { runFinished(result); });
    }, true);
    refreshTree();                                     // le righe in arrivo dicono che sono in lavorazione
}

void Workspace::runFinished(const run::Result &result)
{
    checkpoint(QStringLiteral("Aggiungi materiale"));  // tutto il lavoro è un passo solo
    loadSettings();                                    // i pacchetti chiesti dalle figure
    const QJsonObject state = run::load(m_project, m_runId);
    if (m_runStarted > 0)
        m_runWorked += QDateTime::currentMSecsSinceEpoch() - m_runStarted;
    m_runStarted = 0;
    if (state.isEmpty()) {                             // non è partito: il progetto è com'era
        m_runState.clear();
        m_runId.clear();
        say(result.failure);
    } else if (result.stopped) {
        m_runState = m_closing ? QStringLiteral("interrotto") : QStringLiteral("fermato");
        if (!m_closing && !result.failure.isEmpty())   // fermato da sé (una fonte che non si apre): il perché, in barra e in Attività
            say(result.failure);
        if (m_closing) {                               // chiuso il programma: alla riapertura si riprende con un clic
            QJsonObject s = state;
            s["stato"] = "interrotto";
            run::save(m_project, m_runId, s);
        }
    } else {
        m_runState = QStringLiteral("finito");
    }
    if (m_runState == "fermato" || m_runState == "interrotto")
        stillRun();                                    // come alla riapertura: la percentuale non cambia
    m_runUntil = QDateTime::fromString(state.value("pausa").toString(), Qt::ISODate);
    m_runConnection = state.value("pausa_per").toString() == "connessione";
    if (!m_runState.isEmpty()) {
        const QLocale italian(QLocale::Italian);
        QStringList lines;
        for (const QJsonValue &v : state.value("fatto").toArray())
            lines << v.toString().left(1).toUpper() + v.toString().mid(1) + '.';
        for (const Convert::Lesson &lesson : result.converted.lessons) {
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
        for (const QJsonValue &v : state.value("compiti").toArray()) {
            const QJsonObject t = v.toObject();
            if (t.value("stato").toString() == "fatta")
                lines << QStringLiteral("%1: fatte.").arg(t.value("titolo").toString());
        }
        lines += result.lines;
        if (result.stopped && !m_closing)
            lines << QStringLiteral("Lavoro fermato: ciò che è fatto resta, e si riprende da lì con «Riprendi».");
        if (!result.failure.isEmpty())
            lines << result.failure;
        m_runOutcome = QVariantMap{{"lines", lines}, {"cost", italian.toString(state.value("costo").toDouble(), 'f', 2)},
                                   {"minutes", italian.toString(result.minutes, 'f', 1)}};
        if (!m_closing)
            say(result.stopped ? QStringLiteral("Aggiungi materiale: fermato.")
                               : QStringLiteral("Aggiungi materiale: %1, %2.").arg(plural(result.converted.written(), "lezione scritta", "lezioni scritte"),
                                                                                    plural(result.tasksDone, "lezione completata dall'agente", "lezioni completate dall'agente")));
    }
    saveRunView();
    emit runChanged();
    refreshTree();
    emit filesChanged();
    if (!m_closing && (result.converted.written() > 0 || result.tasksDone > 0))
        m_compiler.compileDocument();                  // dà lo stato alle lezioni nuove e cambiate
}

void Workspace::resumeRun()
{
    if (m_runId.isEmpty() || (m_runState != "fermato" && m_runState != "interrotto"))
        return;
    if (busy())
        return say(QStringLiteral("C'è già un lavoro in corso."));
    checkpoint(QStringLiteral("Modifiche a mano"));
    startRun(m_runId);
}

void Workspace::undoRun()
{
    if (m_runId.isEmpty() || busy())
        return;
    checkpoint(QStringLiteral("Modifiche a mano"));
    QString error;
    const QString id = m_runId;
    const QStringList changed = run::undo(m_project, id, &error);
    if (!error.isEmpty())
        return say(error);
    checkpoint(QStringLiteral("Aggiungi materiale annullato"));
    m_runId.clear();
    m_runState.clear();
    emit runChanged();
    refreshTree();
    emit filesChanged();
    m_compiler.compileDocument();
    say(QStringLiteral("Il progetto è tornato com'era prima di «Avvia» (%1). Il piano è di nuovo da avviare.").arg(plural(changed.size(), "file", "file")));
    emit planReady(id);                                // il piano, di nuovo da leggere e avviare
}

void Workspace::closeRun()
{
    if (!m_runId.isEmpty() && m_runState == "finito")
        archivePlan(m_runId);
}

QString Workspace::pausedUntil() const { return m_pausedUntil.isValid() ? Model::when(m_pausedUntil) : QString(); }

bool Workspace::resumable() const
{
    if (!hasProject() || jobRunning())
        return false;
    if (!m_runId.isEmpty())
        return m_runState == "fermato" || m_runState == "interrotto";
    return !Convert::interrupted(m_project).isEmpty();
}

void Workspace::loadRun()
{
    const QStringList ids = run::started(m_project);
    if (ids.isEmpty())
        return;
    m_runId = ids.first();
    const QJsonObject state = run::load(m_project, m_runId);
    const QString saved = state.value("stato").toString();
    m_runState = saved == "finito" ? QStringLiteral("finito") : saved == "fermato" ? QStringLiteral("fermato") : QStringLiteral("interrotto");
    m_runUntil = QDateTime::fromString(state.value("pausa").toString(), Qt::ISODate);
    m_runConnection = state.value("pausa_per").toString() == "connessione";
    m_runEstimate = state.value("stima").toDouble();
    const QJsonObject view = QJsonDocument::fromJson(files::read(run::folder(m_project, m_runId) + "/vista.json").toUtf8()).object();
    m_runStarted = 0;
    m_runWorked = qint64(view.value("lavorato").toDouble());
    for (const QJsonValue &v : view.value("cantieri").toArray()) {
        QVariantMap site = v.toObject().toVariantMap();
        m_runSiteAt.insert(site.value("key").toString(), int(m_runSites.size()));
        if (!site.value("file").toString().isEmpty())
            m_runLessonSite.insert(QFileInfo(site.value("file").toString()).completeBaseName(), site.value("key").toString());
        m_runSites << site;
    }
    for (const QJsonValue &v : view.value("chiamate").toArray()) {
        QVariantMap call = v.toObject().toVariantMap();
        if (call.value("state") == "in_corso" || call.value("state") == "riprova")
            call["state"] = "interrotta";
        m_runCallAt.insert(call.value("id").toInt(), int(m_runCalls.size()));
        m_runCalls << call;
    }
    for (const QJsonValue &v : view.value("passi").toArray()) {
        m_runStepAt.insert(v.toObject().value("id").toInt(), int(m_runSteps.size()));
        m_runSteps << v.toObject().toVariantMap();
    }
    stillRun();
    m_runOutcome = view.value("esito").toObject().toVariantMap();
    if (m_runState == "interrotto" && m_runOutcome.isEmpty())
        m_runOutcome = QVariantMap{{"lines", QStringList{QStringLiteral("Il lavoro si è interrotto con la chiusura del programma: ciò che è fatto resta, "
                                                                         "e si riprende da lì con «Riprendi».")}}};
    emit runChanged();
}

void Workspace::stillRun()
{
    // il lavoro non gira: ciò che era a metà aspetta la ripresa (quanto è fatto resta, e conta)
    for (QVariantMap &site : m_runSites) {
        if (site.value("state") != "in_corso")
            continue;
        site["state"] = "da_fare";
        QVariantList phases = site.value("phases").toList();
        for (QVariant &p : phases) {
            QVariantMap m = p.toMap();
            if (m.value("state") == "in_corso")
                m["state"] = "da_fare";
            p = m;
        }
        site["phases"] = phases;
    }
    for (QVariantMap &call : m_runCalls)
        if (call.value("state") == "in_corso" || call.value("state") == "riprova")
            call["state"] = "interrotta";
    for (QVariantMap &step : m_runSteps)
        if (step.value("state") == "in_corso")
            step["state"] = "interrotto";
}

int Workspace::lastRunId() const
{
    int last = 0;
    for (const QVariantMap &call : m_runCalls)
        last = qMax(last, call.value("id").toInt());
    for (const QVariantMap &step : m_runSteps)
        last = qMax(last, step.value("id").toInt());
    return last;
}

void Workspace::saveRunView() const
{
    if (m_runId.isEmpty() || !hasProject() || run::load(m_project, m_runId).isEmpty())
        return;
    QJsonArray sites, calls, steps;
    for (const QVariantMap &site : m_runSites)
        sites.append(QJsonObject::fromVariantMap(site));
    for (const QVariantMap &call : m_runCalls)
        calls.append(QJsonObject::fromVariantMap(call));
    for (const QVariantMap &step : m_runSteps)
        steps.append(QJsonObject::fromVariantMap(step));
    QDir().mkpath(run::folder(m_project, m_runId));
    files::write(run::folder(m_project, m_runId) + "/vista.json",
                 QString::fromUtf8(QJsonDocument(QJsonObject{{"lavorato", double(m_runWorked + (m_runStarted > 0 ? QDateTime::currentMSecsSinceEpoch() - m_runStarted : 0))}, {"cantieri", sites},
                                                             {"chiamate", calls}, {"passi", steps}, {"esito", QJsonObject::fromVariantMap(m_runOutcome)}})
                                       .toJson(QJsonDocument::Compact)));
}

namespace {
// chi è una chiamata per chi guarda: il suo ruolo e su che cosa lavora
QString roleOf(const QString &stage, const QString &tag)
{
    if (stage == "outline")
        return tag.endsWith(" titolo") ? QStringLiteral("Titolo") : QStringLiteral("Scaletta");
    if (stage == "transcribe")
        return tag.endsWith(" correzione") ? QStringLiteral("Correzione dopo la verifica") : QStringLiteral("Trascrizione");
    if (stage == "verify")
        return QStringLiteral("Verifica");
    if (stage == "figure")
        return QStringLiteral("Figura");
    if (stage.startsWith("fix"))
        return QStringLiteral("Errori e righe");
    if (stage == "decide")
        return QStringLiteral("Dubbio");
    if (stage == "integrate")
        return QStringLiteral("Agente");
    return stage;
}
QString whatOf(const QString &stage, const QString &tag)
{
    static const QRegularExpression block(R"(#(\d+))"), figure(R"(-(\d+)$)");
    if (stage == "integrate")
        return {};
    if (const auto m = block.match(tag); m.hasMatch())
        return QStringLiteral("blocco %1").arg(m.captured(1));
    if (const auto m = figure.match(tag); m.hasMatch() && stage == "figure")
        return QStringLiteral("figura %1").arg(m.captured(1).toInt());
    return {};
}
const QStringList CONVERT_PHASES{"Scaletta", "Blocchi", "Lezione", "Figure", "Errori e righe", "Impaginazione"};
}

void Workspace::runEvent(const QJsonObject &e)
{
    const QString type = e.value("tipo").toString();
    if (type == "cantiere") {
        QString key = e.value("chiave").toString();
        if (!m_runSiteAt.contains(key) && m_runSiteAt.contains(texsource::parseSpec(key).path))
            key = texsource::parseSpec(key).path;      // una lezione di un .tex: il cantiere è il file
        if (!m_runSiteAt.contains(key)) {
            const QString kind = e.value("genere").toString("converti");
            QVariantList phases;
            for (const QString &name : kind == "converti" ? CONVERT_PHASES : QStringList{"Agente"})
                phases << QVariantMap{{"name", name}, {"state", "da_fare"}, {"done", 0}, {"total", 0}};
            m_runSiteAt.insert(key, int(m_runSites.size()));
            m_runSites << QVariantMap{{"key", key}, {"kind", kind}, {"title", e.value("titolo").toString()}, {"state", "da_fare"}, {"phases", phases}};
        }
        QVariantMap &site = m_runSites[m_runSiteAt.value(key)];
        if (e.contains("file") && !e.value("file").toString().isEmpty()) {
            site["file"] = e.value("file").toString();
            m_runLessonSite.insert(QFileInfo(e.value("file").toString()).completeBaseName(), key);
        }
        if (e.contains("numero"))
            site["number"] = e.value("numero").toInt();
        if (!e.value("titolo").toString().isEmpty())
            site["title"] = e.value("titolo").toString();
        const QString phase = e.value("fase").toString(), state = e.value("stato").toString();
        if (phase == "Fine" || phase.isEmpty()) {
            site["state"] = state;
            if (phase == "Fine" && state == "fatta") {  // finito: ciò che non è stato fatto da sé era da saltare
                QVariantList phases = site.value("phases").toList();
                for (QVariant &p : phases) {
                    QVariantMap m = p.toMap();
                    if (m.value("state") == "da_fare" || m.value("state") == "in_corso")
                        m["state"] = m.value("state") == "in_corso" ? "fatta" : "saltata";
                    p = m;
                }
                site["phases"] = phases;
            }
        } else {
            QVariantList phases = site.value("phases").toList();
            for (QVariant &p : phases) {
                QVariantMap m = p.toMap();
                if (m.value("name") != phase)
                    continue;
                m["state"] = state;
                if (e.contains("totale"))
                    m["total"] = e.value("totale").toInt();
                if (e.contains("fatti"))
                    m["done"] = e.value("fatti").toInt();
                if (e.contains("piu"))
                    m["done"] = m.value("done").toInt() + e.value("piu").toInt();
                if (e.contains("nota"))
                    m["note"] = e.value("nota").toString();
                if (m.value("total").toInt() > 0 && m.value("done").toInt() >= m.value("total").toInt() && m.value("state") == "in_corso")
                    m["state"] = "fatta";                   // tutti i blocchi scritti
                p = m;
            }
            site["phases"] = phases;
            if (site.value("state") == "da_fare" || site.value("state") == "fatta")
                site["state"] = "in_corso";
        }
        if (e.contains("motivo") && !e.value("motivo").toString().isEmpty())
            site["failure"] = e.value("motivo").toString();
        if (e.contains("esito"))
            site["outcome"] = e.value("esito").toString();
    } else if (type == "chiamata") {
        const int id = e.value("id").toInt();
        QString key = e.value("cantiere").toString();
        if (!key.isEmpty() && !m_runSiteAt.contains(key) && m_runSiteAt.contains(texsource::parseSpec(key).path))
            key = texsource::parseSpec(key).path;
        if (key.isEmpty()) {                           // una chiamata partita da un thread senza cantiere: dalla lezione nell'etichetta
            static const QRegularExpression lesson(R"(^((?:src|arg)\d+))");
            key = m_runLessonSite.value(lesson.match(e.value("etichetta").toString()).captured(1));
        }
        if (!m_runCallAt.contains(id)) {
            m_runCallAt.insert(id, int(m_runCalls.size()));
            m_runCalls << QVariantMap{{"id", id}};
        }
        QVariantMap &call = m_runCalls[m_runCallAt.value(id)];
        const QString stage = e.value("fase").toString(), tag = e.value("etichetta").toString();
        call["site"] = key;
        call["role"] = roleOf(stage, tag);
        call["what"] = whatOf(stage, tag);
        call["tag"] = tag;
        call["model"] = e.value("modello").toString().contains("opus") ? QStringLiteral("Opus") : QStringLiteral("Sonnet");
        const QString effort = e.value("impegno").toString();
        call["effort"] = effort == "low" ? QStringLiteral("basso") : effort == "medium" ? QStringLiteral("medio") : effort == "high" ? QStringLiteral("alto") : effort;
        call["state"] = e.value("stato").toString();
        call["started"] = e.value("inizio").toDouble();
        if (e.contains("secondi"))
            call["seconds"] = e.value("secondi").toDouble();
        if (e.contains("costo"))
            call["cost"] = e.value("costo").toDouble();
        call["error"] = e.value("motivo").toString();
        call["connection"] = e.value("connessione").toBool();
        if (e.value("stato").toString() != "in_corso" && e.value("stato").toString() != "riprova" && e.value("stato").toString() != "connessione")
            m_runStreams.remove(id);                   // finita: il flusso intero sta nel suo registro
    } else if (type == "passo") {
        const int id = e.value("id").toInt();
        QString key = e.value("cantiere").toString();
        if (!key.isEmpty() && !m_runSiteAt.contains(key) && m_runSiteAt.contains(texsource::parseSpec(key).path))
            key = texsource::parseSpec(key).path;
        if (!m_runStepAt.contains(id)) {
            const int STEPS_KEPT = 600;                // i più vecchi, finiti, se ne vanno: la vista resta leggera
            if (m_runSteps.size() >= STEPS_KEPT) {
                qsizetype i = 0;
                while (i < m_runSteps.size() && m_runSteps.size() >= STEPS_KEPT) {
                    if (m_runSteps[i].value("state") == "in_corso")
                        ++i;
                    else
                        m_runSteps.removeAt(i);
                }
                m_runStepAt.clear();
                for (qsizetype i = 0; i < m_runSteps.size(); ++i)
                    m_runStepAt.insert(m_runSteps[i].value("id").toInt(), int(i));
            }
            m_runStepAt.insert(id, int(m_runSteps.size()));
            m_runSteps << QVariantMap{{"id", id}};
        }
        QVariantMap &step = m_runSteps[m_runStepAt.value(id)];
        step["site"] = key;
        step["text"] = e.value("testo").toString();
        step["state"] = e.value("stato").toString();
        step["started"] = e.value("inizio").toDouble();
        step["seconds"] = e.value("secondi").toDouble();
        step["outcome"] = e.value("esito").toString();
    } else if (type == "flusso") {
        const int id = e.value("id").toInt();
        QVariantList &stream = m_runStreams[id];
        stream << e.value("riga").toObject().toVariantMap();
        if (stream.size() > 600)
            stream.removeFirst();
        if (m_runCallAt.contains(id))
            m_runCalls[m_runCallAt.value(id)]["last"] = e.value("riga").toObject().value("testo").toString().simplified().left(160);
    }
    if (!m_runTimer.isActive())
        m_runTimer.start();
}

QVariantMap Workspace::run() const
{
    if (m_runId.isEmpty())
        return {};
    QVariantList sites, calls;
    double progress = 0, cost = 0;
    int active = 0;
    // che cosa fa ora ogni cantiere: i passi e le chiamate in corso, nell'ordine in cui sono partiti
    QList<QVariantMap> running;
    for (const QVariantMap &step : m_runSteps)
        if (step.value("state") == "in_corso")
            running << QVariantMap{{"site", step.value("site")}, {"started", step.value("started")}, {"text", step.value("text")}};
    for (const QVariantMap &call : m_runCalls) {
        if (call.value("state") != "in_corso" && call.value("state") != "riprova" && call.value("state") != "connessione")
            continue;
        const QString what = call.value("role").toString() + (call.value("what").toString().isEmpty() ? QString() : " · " + call.value("what").toString());
        const QString text = call.value("state") == "connessione" ? what + QStringLiteral(": aspetta che torni la connessione")
                           : call.value("role") == "Agente" ? (call.value("last").toString().isEmpty() ? QStringLiteral("L'agente lavora") : "Agente: " + call.value("last").toString())
                                                          : QStringLiteral("%1, al modello (%2)").arg(what, call.value("model").toString());
        running << QVariantMap{{"site", call.value("site")}, {"started", call.value("started")}, {"text", text}};
    }
    std::stable_sort(running.begin(), running.end(), [](const QVariantMap &a, const QVariantMap &b) { return a.value("started").toDouble() < b.value("started").toDouble(); });
    QHash<QString, QStringList> doing;
    for (const QVariantMap &r : std::as_const(running))
        doing[r.value("site").toString()] << r.value("text").toString();
    for (const QVariantMap &site : m_runSites) {
        QVariantMap s = site;
        double part = 0;
        const QString state = site.value("state").toString();
        if (state == "fatta" || state == "fallita") {
            part = 1;
        } else if (site.value("kind") == "converti") {
            static const QHash<QString, double> weight{{"Scaletta", 0.1}, {"Blocchi", 0.55}, {"Lezione", 0.05}, {"Figure", 0.1}, {"Errori e righe", 0.12}, {"Impaginazione", 0.08}};
            for (const QVariant &p : site.value("phases").toList()) {
                const QVariantMap m = p.toMap();
                const double w = weight.value(m.value("name").toString());
                if (m.value("state") == "fatta" || m.value("state") == "saltata")
                    part += w;
                else if (m.value("total").toInt() > 0)            // a metà (anche fermato): quanto è fatto
                    part += w * qMin(1.0, m.value("done").toDouble() / m.value("total").toDouble());
            }
        } else if (state == "in_corso") {
            part = 0.4;
        }
        progress += part;
        QString title = site.value("title").toString();
        if (site.value("number").toInt() > 0 && site.value("kind") == "converti")
            title = QStringLiteral("Lezione %1 · %2").arg(site.value("number").toInt()).arg(title);
        s["title"] = title;
        s["now"] = doing.value(site.value("key").toString());
        sites << s;
    }
    QHash<QString, QString> titles;
    for (const QVariant &s : std::as_const(sites))
        titles.insert(s.toMap().value("key").toString(), s.toMap().value("title").toString());
    // le chiamate: quelle in corso in cima, poi le altre dalla più recente
    QList<QVariantMap> ordered = m_runCalls;
    std::stable_sort(ordered.begin(), ordered.end(), [](const QVariantMap &a, const QVariantMap &b) {
        const bool ra = a.value("state") == "in_corso" || a.value("state") == "riprova" || a.value("state") == "connessione",
                   rb = b.value("state") == "in_corso" || b.value("state") == "riprova" || b.value("state") == "connessione";
        if (ra != rb)
            return ra;
        return a.value("id").toInt() > b.value("id").toInt();
    });
    for (QVariantMap call : std::as_const(ordered)) {
        cost += call.value("cost").toDouble();
        active += call.value("state") == "in_corso" || call.value("state") == "riprova" || call.value("state") == "connessione";
        call["siteTitle"] = titles.value(call.value("site").toString());
        calls << call;
    }
    // l'attività: chiamate e passi insieme, quelli in corso in cima, poi dal più recente
    QList<QVariantMap> together;
    for (QVariant c : std::as_const(calls)) {
        QVariantMap m = c.toMap();
        m["kind"] = "chiamata";
        together << m;
    }
    for (QVariantMap step : m_runSteps) {
        step["kind"] = "passo";
        step["siteTitle"] = titles.value(step.value("site").toString());
        together << step;
    }
    std::stable_sort(together.begin(), together.end(), [](const QVariantMap &a, const QVariantMap &b) {
        const bool ra = a.value("state") == "in_corso" || a.value("state") == "riprova" || a.value("state") == "connessione",
                   rb = b.value("state") == "in_corso" || b.value("state") == "riprova" || b.value("state") == "connessione";
        if (ra != rb)
            return ra;
        return a.value("started").toDouble() > b.value("started").toDouble();
    });
    QVariantList activity;
    for (const QVariantMap &m : std::as_const(together))
        activity << m;
    // in alto: che cosa si fa adesso, cantiere per cantiere (i passi senza cantiere sono del lavoro)
    QVariantList now;
    for (const QVariant &s : std::as_const(sites))
        if (!s.toMap().value("now").toStringList().isEmpty())
            now << QVariantMap{{"title", s.toMap().value("title")}, {"key", s.toMap().value("key")}, {"text", s.toMap().value("now").toStringList().join(QStringLiteral(" — "))}};
    if (!doing.value(QString()).isEmpty())
        now.prepend(QVariantMap{{"title", QStringLiteral("Lavoro")}, {"key", QString()}, {"text", doing.value(QString()).join(QStringLiteral(" — "))}});
    QString state = m_runState;
    if (state == "in_corso" && m_pausedUntil.isValid())
        state = QStringLiteral("in_pausa");
    const QDateTime until = state == "in_pausa" ? m_pausedUntil : m_runUntil;
    return QVariantMap{{"id", m_runId}, {"title", QStringLiteral("Aggiungi materiale")}, {"state", state},
                       {"until", until.isValid() ? Model::when(until) : QString()},
                       {"untilPassed", until.isValid() && until <= QDateTime::currentDateTime()},
                       {"connection", state == "in_pausa" ? m_pauseConnection : m_runConnection}, {"offline", m_offline && state == "in_corso"},
                       {"sessionStart", double(m_runStarted)}, {"worked", double(m_runWorked)}, {"cost", cost}, {"estimate", m_runEstimate},
                       {"progress", m_runSites.isEmpty() ? 0.0 : progress / m_runSites.size()}, {"active", active}, {"sites", sites}, {"calls", calls}, {"activity", activity}, {"now", now}, {"steps", int(m_runSteps.size())},
                       {"outcome", m_runOutcome}, {"canUndo", QFileInfo::exists(run::folder(m_project, m_runId) + "/prima/stato.json")}};
}

QVariantMap Workspace::runCall(int id) const
{
    if (m_runId.isEmpty())
        return {};
    const QJsonObject record = QJsonDocument::fromJson(files::read(QStringLiteral("%1/%2.json").arg(run::recordsDir(m_project, m_runId)).arg(id)).toUtf8()).object();
    QVariantList parts;
    for (const QJsonValue &v : record.value("parti").toArray()) {
        const QJsonObject p = v.toObject();
        parts << (p.contains("immagine") ? QVariantMap{{"image", p.value("immagine").toString()}} : QVariantMap{{"text", p.value("testo").toString()}});
    }
    QVariantList stream = m_runStreams.value(id);
    if (stream.isEmpty())
        for (const QJsonValue &v : record.value("flusso").toArray())
            stream << v.toObject().toVariantMap();
    return QVariantMap{{"found", !record.isEmpty()}, {"system", record.value("istruzioni").toString()}, {"parts", parts},
                       {"response", record.value("risposta").toString()}, {"stream", stream}, {"error", record.value("motivo").toString()}};
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

void Workspace::revealFile(const QString &file) const
{
    const QString path = m_project.path(file);
#if defined(Q_OS_MACOS)
    QProcess::startDetached("open", {"-R", path});
#elif defined(Q_OS_WIN)
    QProcess::startDetached("explorer", {"/select," + QDir::toNativeSeparators(path)});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).path()));     // la cartella che lo contiene
#endif
}

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

void Workspace::setProgram(const QString &path)
{
    if (!hasProject())
        return;
    Settings settings = Settings::load(m_project);
    if (path.isEmpty()) {
        settings.program.clear();
    } else {
        const QFileInfo info(path);
        QString inside = m_project.relative(info.absoluteFilePath());
        if (!info.isFile())
            return say(QStringLiteral("Non trovo %1.").arg(path));
        if (inside.isEmpty() || inside.startsWith("..") || QFileInfo(inside).isAbsolute()) {   // da fuori: va fra i documenti
            const QString copy = files::freeName(m_project.path("docs/" + info.fileName()));
            QDir().mkpath(m_project.path("docs"));
            if (!QFile::copy(path, copy))
                return say(QStringLiteral("Non riesco a copiare %1 nel progetto.").arg(info.fileName()));
            inside = m_project.relative(copy);
        }
        settings.program = inside;
    }
    settings.save(m_project);
    loadSettings();
    refreshTree();
    say(path.isEmpty() ? QStringLiteral("Il corso non ha più un programma.")
                       : QStringLiteral("Il programma del corso è %1: gli argomenti seguiranno il suo ordine.").arg(QFileInfo(settings.program).fileName()));
}

void Workspace::setByTopic(bool on)
{
    if (!hasProject() || on == m_settings.byTopic())
        return;
    if (!m_project.inputs().isEmpty())                 // con delle lezioni si riorganizza: la scaletta, o il ritorno per data
        return on ? proposeTopics() : backToLessons();
    checkpoint(QStringLiteral("Modifiche a mano"));
    if (on) {
        reorganize::startByTopic(m_project);
    } else {
        Settings settings = Settings::load(m_project);
        settings.topics = {};
        settings.save(m_project);
    }
    checkpoint(on ? QStringLiteral("Progetto per argomento") : QStringLiteral("Progetto per data"));
    loadSettings();
    refreshTree();
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
    setProjectRules(m_settings.profile.extra + QStringList{rule.trimmed()});
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

QString Workspace::fileStamp(const QString &path) const
{
    const QFileInfo info(QFileInfo(path).isAbsolute() ? path : m_project.path(path));
    return info.exists() ? QString::number(info.lastModified().toMSecsSinceEpoch()) + '-' + QString::number(info.size()) : QString();
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
    const QStringList stale = layout::changedBlocks(m_project, lesson);      // da rifare: non si accettano
    for (const layout::Move &m : layout::moves(m_project, lesson))
        if (m.state == "fatta" && !stale.contains(m.block) && layout::decide(m_project, lesson, m.block, "accettata").isEmpty())
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
