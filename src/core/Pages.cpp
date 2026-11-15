#include "core/Pages.h"

#include "core/Files.h"
#include "core/Tools.h"

#include <QDir>
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

Pages Pages::read(const QString &source, const QString &dir)
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
            if (!tools::run("sips", {"-s", "format", "png", "-Z", "1300", source, "--out", png}, dir, 60000).ok)
                throw std::runtime_error(("Non riesco a leggere l'immagine " + info.fileName() + ".").toStdString());
        pages.full << png;
        pages.thumb << png;
        return pages;
    }
    pages.isText = true;
    pages.text = DOCUMENTS.contains(ext) ? tools::run("textutil", {"-convert", "txt", "-stdout", source}, dir, 60000).text : files::read(source);
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
