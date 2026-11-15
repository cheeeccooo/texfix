#pragma once
#include <QString>
#include <QStringList>

namespace tx {

// Una fonte come la vede il modello: le pagine in immagini (scansioni, PDF, foto) oppure il testo (documenti).
struct Pages {
    QStringList full, thumb;               // percorsi assoluti dei .png: per la trascrizione, per la scaletta
    bool isText = false;
    QString text;

    int count() const { return isText ? 1 : int(full.size()); }
    // Che cos'è una fonte dal suo nome: pagine | testo | tex | altro (non si converte).
    static QString kindOf(const QString &file);
    // Rende la fonte nella cartella `dir` (ciò che c'è già e non è più vecchio della fonte si riusa). Lancia
    // std::runtime_error se non riesce.
    static Pages read(const QString &source, const QString &dir);
    // Quante pagine ha, senza renderla (per la stima del costo); per il testo le righe.
    static int size(const QString &source);
};

}
