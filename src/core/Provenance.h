#pragma once
#include "core/Job.h"
#include "core/Project.h"
#include "core/Style.h"

#include <QList>

// I tag di provenienza (D5): da quale fonte e pagina viene ogni blocco. Nascono dai marcatori dei blocchi, che lo sanno
// già, non da comandi scritti nelle lezioni: Texfix li mostra sul PDF, accanto al blocco, e un clic porta alla pagina
// della fonte. Mostrarli o nasconderli non sposta una riga, perché nel documento non c'è niente.
namespace tx::provenance {

struct Tag {
    int page = 0;                          // la pagina del PDF (da 1)
    double y = 0;                          // l'altezza nella pagina, in punti dall'alto
    QString mark;                          // la sigla della fonte: A, B, C…
    QString source;                        // il nome della fonte
    int sourcePage = 0;                    // la pagina della fonte; 0 se il marcatore non la dice
    QString block;                         // «L3.7»
};
struct Source {
    QString mark, name, path;              // la sigla, il nome, dov'è (relativo alla radice; vuoto se non si trova)
};

// Le fonti del progetto con la loro sigla, nell'ordine in cui compaiono nelle lezioni.
QList<Source> sources(const Project &project, const Style &style);
// I tag dei blocchi che stanno nel PDF dato (il documento, o una lezione compilata da sola).
QList<Tag> tags(const Project &project, const Style &style, const QString &pdf, JobControl *control = nullptr);

}
