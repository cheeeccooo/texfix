#pragma once
#include "core/Model.h"
#include "core/Project.h"
#include "core/Settings.h"
#include "core/Style.h"

#include <QJsonObject>
#include <QList>

// Le figure del progetto: un registro solo, salvataggi/figure.json. Il nome di una figura resta per sempre
// (<lezione>-NN) ed è scritto nella lezione accanto al suo posto: «\begin{…} % img src4-02». La lezione di una figura
// è il .tex che ha quel segno, non la fonte.
namespace tx::figures {

struct Figure {
    QString id;                            // «src4-02»
    QString source;                        // il nome della fonte
    int page = 0;                          // la pagina della fonte (da 1)
    QList<double> box;                     // dove sta nella pagina: x0, y0, x1, y1 fra 0 e 1
    QString description, width;            // «0.7\linewidth»
    QString state = "da_inserire";         // da_inserire | disegnata | ritagliata
    QString note;                          // perché non è stata inserita
    double cost = 0;
};

QList<Figure> all(const Project &project);
Figure find(const Project &project, const QString &id);
void save(const Project &project, const Figure &figure);          // aggiunge o aggiorna
// La lezione che ha il segno della figura (vuoto se nessuna) e quante figure restano da inserire in una lezione.
QString lessonOf(const Project &project, const QString &id);
int pending(const Project &project, const QString &lesson);

// Mette il contenuto al posto della figura nella sua lezione (fra il \begin con il segno e il suo \end).
QString place(const Project &project, const QString &id, const QString &content);

// Ridisegna la figura in TikZ dalla pagina della fonte: disegno, prova di compilazione, revisione sulla resa. La
// inserisce nella lezione se compila. Vuoto se è andata; altrimenti il perché (e resta il segnaposto).
QString draw(const Project &project, const Settings &settings, const Style &style, Model &model, Figure figure, const QString &pageImage);
// Ritaglia la figura dalla pagina della fonte in imgs/<nome>.png e la inserisce.
QString crop(const Project &project, Figure figure, const QString &pageImage);

// Le figure di un .tex di un altro autore, dopo che la lezione è scritta. «Tieni» (Settings::texFigures): il codice
// dell'autore resta, asciugato (via commenti e righe vuote) solo se la resa è identica, e un disegno più largo della
// colonna si restringe quanto basta. «Ridisegna»: il disegnatore la rifà
// nello stile del progetto guardando la resa del codice; entra nel registro come le altre.
struct Authored {
    int kept = 0, fitted = 0, redrawn = 0, failed = 0;
    int tidied = 0;                        // quelle asciugate (contate anche fra le tenute o le adattate)
};
Authored authored(const Project &project, const Settings &settings, const Style &style, Model &model, const QString &lesson, const QString &source);

// Dov'è la resa di una figura per mostrarla: il ritaglio, o l'ultima prova di disegno (vuoto se non c'è).
QString preview(const Project &project, const Figure &figure);
// Il prompt del disegnatore, in tre parti.
QString prompt(const Settings &settings, const Style &style);

}
