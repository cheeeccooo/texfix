#include "core/Tools.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QAtomicInt>
#include <QImage>
#include <QImageReader>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QProcess>
#include <QStandardPaths>

#if defined(Q_OS_WIN)
#include <windows.h>
#else
#include <cerrno>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#if defined(Q_OS_LINUX)
#include <sys/prctl.h>
#endif
#if defined(Q_OS_MACOS)
#include <libproc.h>
#include <sys/proc.h>
#endif

namespace tx::tools {

namespace {
// Dove stanno TeX, poppler, git e claude quando il programma non è lanciato da un terminale.
QStringList extraPaths()
{
    const QString home = QDir::homePath();
#if defined(Q_OS_MACOS)
    return {"/Library/TeX/texbin", "/opt/homebrew/bin", "/usr/local/bin", home + "/.local/bin"};
#elif defined(Q_OS_WIN)
    const QString local = qEnvironmentVariable("LOCALAPPDATA"), programs = qEnvironmentVariable("ProgramFiles");
    QStringList out{home + "/.local/bin", local + "/Microsoft/WinGet/Links", local + "/Programs/MiKTeX/miktex/bin/x64",
                    programs + "/MiKTeX/miktex/bin/x64", programs + "/Git/cmd", programs + "/Git/bin", programs + "/Pandoc",
                    qEnvironmentVariable("APPDATA") + "/npm"};
    // Poppler di winget: la sua cartella ha la versione nel nome
    for (const QString &dir : QDir(local + "/Microsoft/WinGet/Packages").entryList({"oschwartz10612.Poppler_*"}, QDir::Dirs))
        for (const QString &version : QDir(local + "/Microsoft/WinGet/Packages/" + dir).entryList({"poppler-*"}, QDir::Dirs, QDir::Name | QDir::Reversed))
            out << local + "/Microsoft/WinGet/Packages/" + dir + "/" + version + "/Library/bin";
    for (const QString &year : QDir("C:/texlive").entryList({"20*"}, QDir::Dirs, QDir::Name | QDir::Reversed))
        out << "C:/texlive/" + year + "/bin/windows" << "C:/texlive/" + year + "/bin/win64";
    for (QString &p : out)
        p = QDir::toNativeSeparators(p);
    return out;
#else
    QStringList out{"/usr/local/bin", home + "/.local/bin"};
    for (const QString &year : QDir("/usr/local/texlive").entryList({"20*"}, QDir::Dirs, QDir::Name | QDir::Reversed))
        out << "/usr/local/texlive/" + year + "/bin/x86_64-linux" << "/usr/local/texlive/" + year + "/bin/aarch64-linux";
    return out;
#endif
}
}

QProcessEnvironment environment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QStringList path = env.value("PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts);
    const QStringList extra = extraPaths();
    for (auto it = extra.rbegin(); it != extra.rend(); ++it)   // il primo della lista resta il primo
        if (!path.contains(*it) && QFileInfo(*it).isDir())
            path.prepend(*it);
    env.insert("PATH", path.join(QDir::listSeparator()));
    return env;
}

void adoptPath() { qputenv("PATH", environment().value("PATH").toUtf8()); }

QString program(const QString &name)
{
    if (name.contains('/') || name.contains('\\'))
        return name;
    const QString found = QStandardPaths::findExecutable(name, environment().value("PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts));
    return found.isEmpty() ? name : found;
}

#if defined(Q_OS_WIN)
namespace {
// Su Windows la famiglia di un processo è un Job Object: ogni figlio nasce dentro, TerminateJobObject li ferma tutti.
// ownGroup fa partire il processo sospeso e annota qui la sua PROCESS_INFORMATION (il modificatore gira dentro start,
// nello stesso thread); start lo mette nel contenitore e lo fa ripartire.
thread_local PROCESS_INFORMATION *t_starting = nullptr;
QMutex g_jobsMutex;
QHash<qint64, HANDLE> g_jobs;

void adopt(QProcess &process)
{
    PROCESS_INFORMATION *info = t_starting;
    t_starting = nullptr;
    if (!info || process.state() == QProcess::NotRunning)
        return;
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {                                         // se Texfix muore il contenitore si chiude e la famiglia con lui
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    }
    if (job && !AssignProcessToJobObject(job, info->hProcess)) {
        CloseHandle(job);
        job = nullptr;
    }
    ResumeThread(info->hThread);
    QMutexLocker lock(&g_jobsMutex);
    for (auto it = g_jobs.begin(); it != g_jobs.end();) {     // i contenitori vuoti non servono più
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting = {};
        if (QueryInformationJobObject(it.value(), JobObjectBasicAccountingInformation, &accounting, sizeof(accounting), nullptr)
            && accounting.ActiveProcesses == 0) {
            CloseHandle(it.value());
            it = g_jobs.erase(it);
        } else {
            ++it;
        }
    }
    if (job)
        g_jobs.insert(qint64(info->dwProcessId), job);
}
}
#endif

namespace {
void launch(QProcess &process, const QString &name, const QStringList &arguments)
{
    QString resolved = program(name);
#if defined(Q_OS_WIN)
    // un programma senza estensione è uno script (i finti modelli delle prove): lo esegue sh, quello di Git
    if (QFileInfo(resolved).isFile() && QFileInfo(resolved).suffix().isEmpty()) {
        // sh di Git legge la riga di comando con regole sue: i prompt lunghi, con virgolette e barre rovesciate, gli
        // arriverebbero spezzati. Gli argomenti passano da un file («set -- '…' '…'»), che sh legge prima dello script.
        static QAtomicInt counter;
        const QString file = QDir::tempPath() + QStringLiteral("/texfix-argomenti-%1-%2.sh").arg(QCoreApplication::applicationPid()).arg(counter.fetchAndAddRelaxed(1));
        QString line = QStringLiteral("set --");
        for (QString a : arguments)
            line += QStringLiteral(" '") + a.replace(QLatin1String("'"), QLatin1String("'\\''")) + QLatin1Char('\'');
        QFile out(file);
        if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            out.write(line.toUtf8() + '\n');
        out.close();
        QObject::connect(&process, &QProcess::finished, &process, [file] { QFile::remove(file); });
        QProcessEnvironment env = process.processEnvironment().isEmpty() ? environment() : process.processEnvironment();
        env.insert("MSYS2_ARG_CONV_EXCL", "*");
        env.insert("MSYS_NO_PATHCONV", "1");
        process.setProcessEnvironment(env);
        process.start(program("sh"), {"-c", "s=$1; . \"$0\"; . \"$s\"", QDir::fromNativeSeparators(file), QDir::fromNativeSeparators(resolved)});
        return;
    }
    // claude.cmd (npm) e gli altri .cmd/.bat: CreateProcess li apre solo attraverso cmd
    const QString ext = QFileInfo(resolved).suffix().toLower();
    if (ext == "cmd" || ext == "bat") {
        process.start(qEnvironmentVariable("COMSPEC", "cmd.exe"), QStringList{"/d", "/s", "/c", resolved} + arguments);
        return;
    }
#endif
    process.start(resolved, arguments);
}
}

void start(QProcess &process, const QString &name, const QStringList &arguments)
{
    launch(process, name, arguments);
#if defined(Q_OS_WIN)
    adopt(process);
#endif
}

void ownGroup(QProcess &process)
{
#if defined(Q_OS_WIN)
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_SUSPENDED;                // riparte da start, dentro il suo contenitore
        t_starting = args->processInformation;
    });
#else
    process.setChildProcessModifier([] {
        ::setpgid(0, 0);
#if defined(Q_OS_LINUX)
        ::prctl(PR_SET_PDEATHSIG, SIGKILL);            // muore con Texfix, anche se Texfix è ucciso
#endif
    });
#endif
}

QString commandOf(qint64 pid)
{
    if (pid <= 1)
        return {};
#if defined(Q_OS_WIN)
    return {};                                         // su Windows la famiglia muore con Texfix (il contenitore)
#else
    if (::kill(pid_t(pid), 0) != 0)
        return {};
    QProcess ps;
    ps.start("ps", {"-o", "command=", "-p", QString::number(pid)});
    ps.waitForFinished(5000);
    return QString::fromUtf8(ps.readAllStandardOutput()).trimmed();
#endif
}

void stopGroup(qint64 pid)
{
    if (pid <= 1)
        return;
#if defined(Q_OS_WIN)
    {
        QMutexLocker lock(&g_jobsMutex);
        if (HANDLE job = g_jobs.take(pid)) {
            TerminateJobObject(job, 1);
            CloseHandle(job);
            return;
        }
    }
    // senza contenitore: taskkill /T ferma lui e l'albero dei suoi figli
    QProcess kill;
    kill.setStandardOutputFile(QProcess::nullDevice());
    kill.setStandardErrorFile(QProcess::nullDevice());
    kill.start("taskkill", {"/F", "/T", "/PID", QString::number(pid)});
    kill.waitForFinished(5000);
    if (HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, DWORD(pid))) {
        TerminateProcess(h, 1);
        CloseHandle(h);
    }
#else
    ::kill(-pid_t(pid), SIGKILL);                      // il gruppo: lui e i figli
    ::kill(pid_t(pid), SIGKILL);                       // e lui, se il gruppo non ha fatto in tempo a nascere
#endif
}

bool gone(qint64 pid)
{
    if (pid <= 1)
        return true;
#if defined(Q_OS_WIN)
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if (!h)
        return true;
    DWORD code = 0;
    const bool running = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return !running;
#elif defined(Q_OS_MACOS)
    struct proc_bsdinfo info = {};
    if (proc_pidinfo(pid_t(pid), PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != int(sizeof(info)))
        return ::kill(pid_t(pid), 0) != 0 && errno == ESRCH;
    return info.pbi_status == SZOMB;
#else
    QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!stat.open(QIODevice::ReadOnly))
        return ::kill(pid_t(pid), 0) != 0 && errno == ESRCH;
    const QByteArray line = stat.readAll();            // «pid (nome) S …»: lo stato dopo l'ultima parentesi
    const int close = line.lastIndexOf(')');
    return close >= 0 && line.size() > close + 2 && line.at(close + 2) == 'Z';
#endif
}

void reap(qint64 pid)
{
#if defined(Q_OS_WIN)
    Q_UNUSED(pid);                                     // su Windows non resta niente da raccogliere
#else
    if (pid > 1)
        ::waitpid(pid_t(pid), nullptr, WNOHANG);
#endif
}

}

namespace tx::tools {
Output run(const QString &program, const QStringList &arguments, const QString &directory, int timeoutMs)
{
    QProcess process;
    ownGroup(process);
    process.setProcessEnvironment(environment());
    process.setWorkingDirectory(directory);
    start(process, program, arguments);
    if (!process.waitForStarted(5000))
        return {};
    if (!process.waitForFinished(timeoutMs)) {
        stopGroup(process.processId());
        process.waitForFinished(1000);
        return {};
    }
    // su Windows synctex e altri chiudono le righe con \r\n: chi legge vuole solo \n
    QString text = QString::fromUtf8(process.readAllStandardOutput());
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    return {process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0, text};
}

bool imageToPng(const QString &source, const QString &target, int maxSide)
{
#if defined(Q_OS_MACOS)
    // sips legge anche HEIC
    return run("sips", {"-s", "format", "png", "-Z", QString::number(maxSide), source, "--out", target}, QFileInfo(target).path(), 60000).ok;
#else
    QImageReader reader(source);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull())
        return false;
    if (qMax(image.width(), image.height()) > maxSide)
        image = image.scaled(maxSide, maxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return image.save(target, "PNG");
#endif
}

bool imageToPdf(const QString &source, const QString &target)
{
#if defined(Q_OS_MACOS)
    return run("sips", {"-s", "format", "pdf", source, "--out", target}, QFileInfo(target).path(), 60000).ok;
#else
    QImageReader reader(source);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull())
        return false;
    // una pagina grande quanto l'immagine, a 72 punti per pollice come sips
    QPdfWriter pdf(target);
    pdf.setResolution(72);
    pdf.setPageSize(QPageSize(QSizeF(image.width(), image.height()), QPageSize::Point));
    pdf.setPageMargins(QMarginsF(0, 0, 0, 0));
    QPainter painter(&pdf);
    if (!painter.isActive())
        return false;
    painter.drawImage(QRect(0, 0, image.width(), image.height()), image);
    return painter.end();
#endif
}

QString documentMarkdown(const QString &source, const QString &root, const QString &media)
{
    // senza gli attributi di pandoc ({width=…}, i div e gli span): al modello serve il testo
    const Output out = run("pandoc", {"-t", "markdown-raw_html-native_divs-native_spans-fenced_divs-bracketed_spans-header_attributes"
                                            "-link_attributes-raw_attribute-smart",
                                      "--wrap=none", "--extract-media=" + media, QFileInfo(source).absoluteFilePath()},
                           root, 120000);
    return out.ok ? out.text : QString();
}

QString documentText(const QString &source)
{
#if defined(Q_OS_MACOS)
    return run("textutil", {"-convert", "txt", "-stdout", source}, QFileInfo(source).path(), 60000).text;
#else
    // pandoc legge .docx, .odt e .rtf; LibreOffice anche .doc
    if (const Output out = run("pandoc", {"-t", "plain", "--wrap=none", source}, QFileInfo(source).path(), 60000); out.ok)
        return out.text;
    const QString dir = QDir::tempPath() + "/texfix-testo-" + QString::number(QCoreApplication::applicationPid());
    QDir().mkpath(dir);
    QString text;
    for (const char *office : {"soffice", "libreoffice"})
        if (run(office, {"--headless", "--convert-to", "txt:Text", "--outdir", dir, source}, dir, 120000).ok) {
            QFile file(dir + '/' + QFileInfo(source).completeBaseName() + ".txt");
            if (file.open(QIODevice::ReadOnly))
                text = QString::fromUtf8(file.readAll());
            break;
        }
    QDir(dir).removeRecursively();
    return text;
#endif
}

QString nullDevice() { return QProcess::nullDevice(); }
}
