#include "core/Tools.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>

#include <cerrno>
#include <libproc.h>
#include <signal.h>
#include <sys/proc.h>
#include <sys/wait.h>
#include <unistd.h>

namespace tx::tools {

QProcessEnvironment environment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QStringList path = env.value("PATH").split(':', Qt::SkipEmptyParts);
    const QStringList extra{"/Library/TeX/texbin", "/opt/homebrew/bin", "/usr/local/bin", QDir::homePath() + "/.local/bin"};
    for (const QString &p : extra)
        if (!path.contains(p) && QFileInfo(p).isDir())
            path.prepend(p);
    env.insert("PATH", path.join(':'));
    return env;
}

void adoptPath() { qputenv("PATH", environment().value("PATH").toUtf8()); }

void ownGroup(QProcess &process)
{
    process.setChildProcessModifier([] { ::setpgid(0, 0); });
}

void stopGroup(qint64 pid)
{
    if (pid <= 1)
        return;
    ::kill(-pid_t(pid), SIGKILL);                      // il gruppo: lui e i figli
    ::kill(pid_t(pid), SIGKILL);                       // e lui, se il gruppo non ha fatto in tempo a nascere
}

bool gone(qint64 pid)
{
    if (pid <= 1)
        return true;
    struct proc_bsdinfo info = {};
    if (proc_pidinfo(pid_t(pid), PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != int(sizeof(info)))
        return ::kill(pid_t(pid), 0) != 0 && errno == ESRCH;
    return info.pbi_status == SZOMB;
}

void reap(qint64 pid)
{
    if (pid > 1)
        ::waitpid(pid_t(pid), nullptr, WNOHANG);
}

}

namespace tx::tools {
Output run(const QString &program, const QStringList &arguments, const QString &directory, int timeoutMs)
{
    QProcess process;
    ownGroup(process);
    process.setProcessEnvironment(environment());
    process.setWorkingDirectory(directory);
    process.start(program, arguments);
    if (!process.waitForStarted(5000))
        return {};
    if (!process.waitForFinished(timeoutMs)) {
        stopGroup(process.processId());
        process.waitForFinished(1000);
        return {};
    }
    return {process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0, QString::fromUtf8(process.readAllStandardOutput())};
}
}
