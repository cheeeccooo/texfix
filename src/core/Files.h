#pragma once
#include <QByteArray>
#include <QString>

// Letture e scritture dei file del progetto. Ogni scrittura è sicura: o il file nuovo intero, o quello di prima.
namespace tx::files {

QString read(const QString &path, bool *ok = nullptr);
bool write(const QString &path, const QString &text);
bool writeBytes(const QString &path, const QByteArray &data);

// Un nome libero accanto a `path`: «nome.pdf», poi «nome (2).pdf», «nome (3).pdf»…
QString freeName(const QString &path);

}
