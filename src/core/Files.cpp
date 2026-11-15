#include "core/Files.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace tx::files {

QString read(const QString &path, bool *ok)
{
    QFile f(path);
    const bool opened = f.open(QIODevice::ReadOnly);
    if (ok)
        *ok = opened;
    return opened ? QString::fromUtf8(f.readAll()) : QString();
}

bool write(const QString &path, const QString &text) { return writeBytes(path, text.toUtf8()); }

bool writeBytes(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(data);
    return f.commit();
}

QString freeName(const QString &path)
{
    if (!QFileInfo::exists(path))
        return path;
    const QFileInfo fi(path);
    const bool dir = fi.isDir();
    const QString stem = fi.path() + '/' + (dir ? fi.fileName() : fi.completeBaseName());
    const QString ext = dir || fi.suffix().isEmpty() ? QString() : '.' + fi.suffix();
    for (int n = 2;; ++n) {
        const QString candidate = QStringLiteral("%1 (%2)%3").arg(stem).arg(n).arg(ext);
        if (!QFileInfo::exists(candidate))
            return candidate;
    }
}

}
