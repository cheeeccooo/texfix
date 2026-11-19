#include "core/Tools.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
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
    QStringList out{home + "/.local/bin", local + "/Programs/MiKTeX/miktex/bin/x64", programs + "/MiKTeX/miktex/bin/x64",
                    programs + "/Git/cmd", qEnvironmentVariable("APPDATA") + "/npm"};
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

void start(QProcess &process, const QString &name, const QStringList &arguments)
{
    QString resolved = program(name);
#if defined(Q_OS_WIN)
    // un programma senza estensione è uno script (i finti modelli delle prove): lo esegue sh, quello di Git
    if (QFileInfo(resolved).isFile() && QFileInfo(resolved).suffix().isEmpty()) {
        process.start(program("sh"), QStringList{resolved} + arguments);
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

void ownGroup(QProcess &process)
{
#if defined(Q_OS_WIN)
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW;
    });
#else
    process.setChildProcessModifier([] { ::setpgid(0, 0); });
#endif
}

void stopGroup(qint64 pid)
{
    if (pid <= 1)
        return;
#if defined(Q_OS_WIN)
    // Windows non ha gruppi come Unix: taskkill /T ferma lui e l'albero dei suoi figli
    QProcess::execute("taskkill", {"/F", "/T", "/PID", QString::number(pid)});
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
    return {process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0, QString::fromUtf8(process.readAllStandardOutput())};
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
