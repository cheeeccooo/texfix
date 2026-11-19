#pragma once
#include <QString>
#include <QStringList>

namespace tx {

// Una fonte come la vede il modello: le pagine in immagini (scansioni, PDF, foto) oppure il testo (documenti).
struct Pages {
    QStringList full, thumb;               // percorsi assoluti dei .png: per la trascrizione, per la scaletta
    bool isText = false;
    QString text;
    QStringList figures;                   // le immagini di un documento (relative al progetto), al loro posto nel testo
    QString note;                          // ciò che di un documento non si legge (senza pandoc, una figura emf)

    int count() const { return isText ? 1 : int(full.size()); }
    // Che cos'è una fonte dal suo nome: pagine | testo | tex | altro (non si converte).
    static QString kindOf(const QString &file);
    // Rende la fonte nella cartella `dir` (ciò che c'è già e non è più vecchio della fonte si riusa). Lancia
    // std::runtime_error se non riesce. Con `root` (la cartella del progetto) un documento si legge con pandoc, con
    // tabelle, formule e figure (in build/lavori/documenti/<impronta>/, i riferimenti relativi al progetto); senza, o
    // senza pandoc, solo il testo.
    static Pages read(const QString &source, const QString &dir, const QString &root = {});
    // Quante pagine ha, senza renderla (per la stima del costo); per il testo le righe.
    static int size(const QString &source);
};

}
