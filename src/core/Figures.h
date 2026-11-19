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
    QString state = "da_inserire";         // da_inserire | disegnata | ritagliata | scritta (com'era nella lezione)
    QString note;                          // perché non è stata inserita
    double cost = 0;
    QString decision;                      // vuoto: da rivedere; «accettata»
    QList<QList<double>> erase;            // il cancellino: zone della pagina da coprire di bianco nel ritaglio
    bool boxChosen = false;                // il riquadro l'ha scelto l'utente: si ritaglia esatto, senza margine
};

QList<Figure> all(const Project &project);
Figure find(const Project &project, const QString &id);
void save(const Project &project, const Figure &figure);          // aggiunge o aggiorna
// La lezione che ha il segno della figura (vuoto se nessuna) e quante figure di una lezione restano da inserire o da
// rivedere (non ancora accettate).
QString lessonOf(const Project &project, const QString &id);
int pending(const Project &project, const QString &lesson);

// Mette il contenuto al posto della figura nella sua lezione (fra il \begin con il segno e il suo \end).
QString place(const Project &project, const QString &id, const QString &content);

// Ridisegna la figura in TikZ dalla pagina della fonte: disegno, prova di compilazione, revisione sulla resa. La
// inserisce nella lezione se compila. Vuoto se è andata; altrimenti il perché (e resta il segnaposto).
// «instruction»: che cosa cambiare, detto dall'utente con «Rigenera» (il disegnatore vede anche il disegno com'è).
QString draw(const Project &project, const Settings &settings, const Style &style, Model &model, Figure figure, const QString &pageImage,
             const QString &instruction = {});
// Ritaglia la figura dalla pagina della fonte in imgs/<nome>.png, coprendo di bianco le zone del cancellino, e la
// inserisce. Il disegno che c'era resta in salvataggi/figure/<nome>.tex.
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

// Le immagini che una lezione include da fuori di imgs/ (quelle di un documento, in build/lavori/documenti/) si copiano
// in imgs/<lezione>-dNN.<ext> e la lezione le include da lì: build/ si rifà, imgs/ resta. Quante ne ha portate.
int bringImages(const Project &project, const QString &lesson);

// Rivedi figure. Tutte le figure delle lezioni sono nel registro: quelle scritte fuori da Converti (a mano, dall'agente,
// portate da fuori) ci entrano con `adopt`, che dà loro il segno sulla riga del \begin (quante).
int adopt(const Project &project, const Style &style);
// Le figure delle lezioni ancora senza segno.
int unadopted(const Project &project, const Style &style);
// La didascalia della figura nella lezione (il comando dello stile, o \caption), com'è scritta; vuota se non ce l'ha.
QString caption(const Project &project, const Style &style, const QString &id);
// Il codice della figura nella lezione (fra il \begin con il segno e il suo \end).
QString code(const Project &project, const QString &id);
// Da dove viene: «fonte» (una pagina di una fonte, con il riquadro), «autore» (il codice di un .tex di un altro autore,
// ridisegnato), vuoto (scritta così).
QString origin(const Project &project, const Figure &figure);
// La resa della figura com'è nella lezione, compilata da sola: il PNG se c'è già per questo codice (vuoto se no).
// `render` la fa (falso se non compila).
QString rendered(const Project &project, const QString &id);
bool render(const Project &project, const QString &id);
// L'originale da mettere accanto: il pezzo della pagina della fonte nel riquadro, o la resa del codice dell'autore.
// `original` lo fa (dalla pagina della fonte, se la figura viene da lì); `originalPath` dice dov'è (vuoto se non c'è).
QString originalPath(const Project &project, const Figure &figure);
bool original(const Project &project, const Figure &figure, const QString &pageImage);
// L'ultimo disegno della figura (salvataggi/figure/<nome>.tex): resta anche quando si passa al ritaglio.
QString drawing(const Project &project, const QString &id);
// Rimette nella lezione l'ultimo disegno (dopo un ritaglio). Vuoto se è andata.
QString useDrawing(const Project &project, const QString &id);
// Accetta la figura com'è (o la riapre).
void decide(const Project &project, const QString &id, bool accepted);
// Il prompt del disegnatore, in tre parti.
QString prompt(const Settings &settings, const Style &style);

}
