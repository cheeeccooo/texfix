#include "PdfBackend.h"

#include <QFileInfo>
#include <QPainter>
#include <QPdfLink>
#include <QUrl>

namespace {
int g_revision = 0;                                    // una per ricarica, in tutto il programma: l'immagine di prima non torna
}

PdfBackend::PdfBackend(QObject *parent) : QObject(parent) { m_links.setDocument(&m_document); }

QVariantMap PdfBackend::linkAt(int page, qreal x, qreal y)
{
    m_links.setPage(page);
    const QPdfLink link = m_links.linkAt(QPointF(x, y));
    if (!link.isValid())
        return {{"valid", false}};
    return {{"valid", true}, {"page", link.page()}, {"y", link.location().y()}, {"url", link.url().toString()}};
}

void PdfBackend::setSource(const QString &path)
{
    if (m_source == path)
        return;
    m_source = path;
    emit sourceChanged();
    reload();
}

void PdfBackend::reload()
{
    m_document.close();
    if (!m_source.isEmpty() && QFileInfo::exists(m_source))
        m_document.load(m_source);
    m_revision = ++g_revision;
    emit reloaded();
}

QSizeF PdfBackend::pageSize(int page) const { return m_document.pagePointSize(page); }

QString PdfBackend::pageUrl(int page, int widthPx) const
{
    return QStringLiteral("image://pdf/%1/%2/%3/%4").arg(m_revision).arg(page).arg(widthPx).arg(QString::fromLatin1(QUrl::toPercentEncoding(m_source)));
}

QImage PdfPages::requestImage(const QString &id, QSize *size, const QSize &)
{
    const QString revision = id.section('/', 0, 0);
    const int page = id.section('/', 1, 1).toInt(), width = id.section('/', 2, 2).toInt();
    const QString path = QUrl::fromPercentEncoding(id.section('/', 3).toLatin1());

    QMutexLocker lock(&m_mutex);
    if (const QString key = revision + '/' + path; key != m_key) {
        m_document = std::make_unique<QPdfDocument>();
        m_document->load(path);
        m_key = key;
    }
    const QSizeF points = m_document->pagePointSize(page);
    if (points.isEmpty() || width <= 0)
        return {};
    const QSize pixels(width, qRound(width * points.height() / points.width()));
    QImage rendered = m_document->render(page, pixels);
    if (rendered.isNull())
        return {};
    QImage paper(pixels, QImage::Format_RGB32);        // le pagine di un PDF sono trasparenti: sotto, la carta
    paper.fill(Qt::white);
    QPainter(&paper).drawImage(0, 0, rendered);
    if (size)
        *size = pixels;
    return paper;
}
