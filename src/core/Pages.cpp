#include "core/Pages.h"

#include "core/Files.h"
#include "core/Tools.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <stdexcept>

namespace tx {

namespace {
const QStringList IMAGES{"png", "jpg", "jpeg", "heic", "tif", "tiff"};
const QStringList DOCUMENTS{"docx", "doc", "rtf", "odt"};
const QStringList TEXTS{"md", "txt", "markdown"};

QStringList rendered(const QString &dir, const QString &prefix)
{
    QStringList out;
    for (const QString &name : QDir(dir).entryList({prefix + "*.png"}, QDir::Files, QDir::Name))
        out << dir + '/' + name;
    return out;
}
}

QString Pages::kindOf(const QString &file)
{
    const QString ext = QFileInfo(file).suffix().toLower();
    if (ext == "pdf" || IMAGES.contains(ext))
        return "pagine";
    if (DOCUMENTS.contains(ext) || TEXTS.contains(ext))
        return "testo";
    return ext == "tex" ? "tex" : "altro";
}

Pages Pages::read(const QString &source, const QString &dir, const QString &root)
{
    Pages pages;
    const QFileInfo info(source);
    const QString ext = info.suffix().toLower();
    QDir().mkpath(dir);
    if (ext == "pdf") {
        // 100 punti per pollice per leggere, 40 per la scaletta
        const QFileInfo stamp(dir + "/fatto");
        if (!stamp.exists() || stamp.lastModified() < info.lastModified()) {
            for (const QString &old : QDir(dir).entryList({"*.png"}, QDir::Files))
                QFile::remove(dir + '/' + old);
            for (const auto &[prefix, dpi] : {std::pair<const char *, const char *>{"full", "100"}, {"thumb", "40"}})
                if (!tools::run("pdftoppm", {"-png", "-r", dpi, source, dir + '/' + prefix}, dir, 300000).ok)
                    throw std::runtime_error(("Non riesco a leggere le pagine di " + info.fileName() + " (serve poppler).").toStdString());
            files::write(stamp.filePath(), "");
        }
        pages.full = rendered(dir, "full");
        pages.thumb = rendered(dir, "thumb");
        if (pages.full.isEmpty())
            throw std::runtime_error((info.fileName() + " non ha pagine.").toStdString());
        return pages;
    }
    if (IMAGES.contains(ext)) {
        const QString png = dir + "/full-1.png";
        if (!QFileInfo::exists(png) || QFileInfo(png).lastModified() < info.lastModified())
            if (!tools::imageToPng(source, png, 1300))
                throw std::runtime_error(("Non riesco a leggere l'immagine " + info.fileName() + ".").toStdString());
        pages.full << png;
        pages.thumb << png;
        return pages;
    }
    pages.isText = true;
    if (DOCUMENTS.contains(ext) && !root.isEmpty()) {
        QFile f(source);
        const QString media = f.open(QIODevice::ReadOnly)
            ? QStringLiteral("build/lavori/documenti/") + QString::fromLatin1(QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha1).toHex().left(12))
            : QString();
        pages.text = media.isEmpty() ? QString() : tools::documentMarkdown(source, root, media);
        if (!pages.text.trimmed().isEmpty()) {
            // le immagini: quelle che LaTeX e il modello leggono restano; le altre diventano PNG; emf e wmf no
            static const QStringList kept{"png", "jpg", "jpeg", "pdf"}, convertible{"gif", "bmp", "tif", "tiff", "webp", "heic"};
            static const QRegularExpression image(R"(!\[[^\]]*\]\(([^)\s]+)[^)]*\))");
            QStringList unread;
            for (auto it = image.globalMatch(pages.text); it.hasNext();) {
                const QString path = it.next().captured(1), kind = QFileInfo(path).suffix().toLower();
                if (kept.contains(kind)) {
                    pages.figures << path;
                } else if (convertible.contains(kind)) {
                    const QString png = path.left(path.size() - kind.size()) + "png";
                    if (tools::imageToPng(root + '/' + path, root + '/' + png, 2000)) {
                        pages.text.replace(path, png);
                        pages.figures << png;
                    } else {
                        unread << QFileInfo(path).fileName();
                    }
                } else {
                    unread << QFileInfo(path).fileName();
                }
            }
            if (!unread.isEmpty())
                pages.note = QStringLiteral("di %1 non si leggono %2 figure (%3)").arg(info.fileName()).arg(unread.size()).arg(unread.join(", "));
        } else {
            pages.text = tools::documentText(source);
            pages.note = QStringLiteral("di %1 si legge solo il testo: figure, tabelle e formule chiedono pandoc").arg(info.fileName());
        }
    } else {
        pages.text = DOCUMENTS.contains(ext) ? tools::documentText(source) : files::read(source);
    }
    if (pages.text.trimmed().isEmpty())
        throw std::runtime_error((info.fileName() + " è vuoto.").toStdString());
    return pages;
}

int Pages::size(const QString &source)
{
    const QString ext = QFileInfo(source).suffix().toLower();
    if (ext == "pdf") {
        static const QRegularExpression pages(R"(Pages:\s+(\d+))");
        return qMax(1, pages.match(tools::run("pdfinfo", {source}, QFileInfo(source).absolutePath(), 20000).text).captured(1).toInt());
    }
    if (IMAGES.contains(ext))
        return 1;
    return int(files::read(source).count('\n')) + 1;
}

}
