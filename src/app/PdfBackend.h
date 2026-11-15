#pragma once
#include <QMutex>
#include <QObject>
#include <QPdfDocument>
#include <QPdfLinkModel>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QSizeF>
#include <QVariantMap>

#include <memory>

// Un PDF per la vista QML: quante pagine ha e quanto sono grandi. Le pagine le disegna PdfPages, come immagini
// («image://pdf/<revisione>/<pagina>/<larghezza in pixel>/<percorso>»): la revisione cambia a ogni ricarica.
class PdfBackend : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY reloaded)
    Q_PROPERTY(int revision READ revision NOTIFY reloaded)

public:
    explicit PdfBackend(QObject *parent = nullptr);

    QString source() const { return m_source; }
    void setSource(const QString &path);
    int pageCount() const { return m_document.pageCount(); }
    int revision() const { return m_revision; }

    Q_INVOKABLE void reload();                         // il file è cambiato su disco
    Q_INVOKABLE QSizeF pageSize(int page) const;       // in punti
    Q_INVOKABLE QString pageUrl(int page, int widthPx) const;
    // Il collegamento in un punto della pagina (in punti): valid, e poi page e y (dentro il documento) oppure url.
    Q_INVOKABLE QVariantMap linkAt(int page, qreal x, qreal y);

signals:
    void sourceChanged();
    void reloaded();

private:
    QString m_source;
    QPdfDocument m_document;
    QPdfLinkModel m_links;
    int m_revision = 0;
};

class PdfPages : public QQuickImageProvider {
public:
    PdfPages() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    QMutex m_mutex;
    QString m_key;                                     // revisione e percorso del documento aperto
    std::unique_ptr<QPdfDocument> m_document;
};
