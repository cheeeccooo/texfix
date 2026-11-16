#include "core/SyncTex.h"

#include "core/Tools.h"

#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QMap>

namespace tx::synctex {

namespace {
// L'uscita di synctex: righe «Chiave:valore»; ogni risultato comincia con la chiave `first`.
QList<QMap<QString, QString>> records(const QString &out, const QString &first)
{
    QList<QMap<QString, QString>> all;
    for (const QString &line : out.split('\n')) {
        const qsizetype colon = line.indexOf(':');
        if (colon <= 0)
            continue;
        const QString key = line.left(colon);
        if (key == first)
            all.append(QMap<QString, QString>{});
        if (!all.isEmpty())
            all.last()[key] = line.mid(colon + 1);
    }
    return all;
}
}

bool available(const QString &pdf)
{
    const QFileInfo info(pdf);
    return info.exists() && QFileInfo::exists(info.absolutePath() + '/' + info.completeBaseName() + ".synctex.gz");
}

QList<Spot> allToPdf(const QString &root, const QString &pdf, const QString &file, int line)
{
    QList<Spot> out;
    if (!available(pdf))
        return out;
    // il nome com'è scritto nell'\input (relativo alla radice), poi quello assoluto
    for (const QString &name : {file, QDir(root).absoluteFilePath(file)}) {
        const tools::Output run = tools::run("synctex", {"view", "-i", QStringLiteral("%1:0:%2").arg(line).arg(name), "-o", pdf}, root);
        for (const auto &record : records(run.text, "Output")) {
            if (!record.contains("Page"))
                continue;
            Spot spot;
            spot.ok = true;
            spot.page = record.value("Page").toInt();
            spot.x = record.value("h").toDouble();
            spot.height = record.value("H").toDouble();
            spot.y = record.value("v").toDouble() - spot.height;
            spot.file = file;
            spot.line = line;
            out << spot;
        }
        if (!out.isEmpty())
            break;
    }
    return out;
}

Spot toPdf(const QString &root, const QString &pdf, const QString &file, int line)
{
    const QList<Spot> all = allToPdf(root, pdf, file, line);
    return all.isEmpty() ? Spot() : all.first();
}

Spot toSource(const QString &root, const QString &pdf, int page, double x, double y)
{
    Spot spot;
    if (!available(pdf))
        return spot;
    const tools::Output run = tools::run(
        "synctex", {"edit", "-o", QStringLiteral("%1:%2:%3:%4").arg(page).arg(x, 0, 'f', 2).arg(y, 0, 'f', 2).arg(pdf)}, root);
    const auto all = records(run.text, "Output");
    const QMap<QString, QString> *best = nullptr;
    for (const auto &record : all) {
        if (!record.contains("Input") || record.value("Line").toInt() <= 0)
            continue;
        if (!best)
            best = &record;
        if (!record.value("Input").endsWith(".main.tex")) {   // meglio una lezione che il main ridotto
            best = &record;
            break;
        }
    }
    if (!best)
        return spot;
    spot.ok = true;
    spot.page = page;
    // synctex dà il percorso vero del file: anche la radice va presa senza collegamenti, o il relativo non torna
    const QString input = QDir::cleanPath(QDir(root).absoluteFilePath(best->value("Input")));
    const QString real = QFileInfo(input).canonicalFilePath(), realRoot = QFileInfo(root).canonicalFilePath();
    spot.file = QDir(realRoot.isEmpty() ? root : realRoot).relativeFilePath(real.isEmpty() ? input : real);
    spot.line = best->value("Line").toInt();
    return spot;
}

}
