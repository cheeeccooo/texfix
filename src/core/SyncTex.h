#pragma once
#include <QList>
#include <QString>

// Il salto fra sorgente e PDF: dalla riga di un file al punto della pagina e ritorno. Serve il .synctex.gz che la
// compilazione lascia accanto al PDF.
namespace tx::synctex {

struct Spot {
    bool ok = false;
    int page = 0;                          // da 1
    double x = 0, y = 0;                   // punti dall'angolo in alto a sinistra della pagina; y: il bordo alto della riga
    double height = 0;                     // l'altezza della riga
    QString file;                          // relativo alla radice
    int line = 0;                          // da 1
};

bool available(const QString &pdf);
Spot toPdf(const QString &root, const QString &pdf, const QString &file, int line);
// Tutti i segni che una riga lascia nel PDF (una riga può lasciarne più d'uno, anche in pagine diverse).
QList<Spot> allToPdf(const QString &root, const QString &pdf, const QString &file, int line);
Spot toSource(const QString &root, const QString &pdf, int page, double x, double y);

}
