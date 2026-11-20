#pragma once
#include "core/Project.h"

#include <QList>
#include <QString>
#include <QStringList>

// La resa compilata di piccoli pezzi (una domanda, una lettura, un'aggiunta) per i pannelli Rivedi: come nel PDF, con
// il preambolo del progetto, su fondo trasparente e nel colore del testo del tema. Una resa fatta resta in
// build/resa/; un pezzo che non compila non ha resa, e chi lo mostra tiene il testo (`preview`, già formattato).
namespace tx::snippets {

// Come va letto un pezzo: prosa (con la matematica fra $…$; «ω0» e «V_CB» diventano matematica) o LaTeX com'è scritto
// in una lezione (se da solo non compila, lo si prova come formula).
enum class Kind { Prose, Latex };

struct Piece {
    QString text;
    Kind kind = Kind::Prose;
    QString color;                                     // esadecimale, «202020»
};

// Dove sta (o starà) la resa di un pezzo: lo stesso pezzo, con lo stesso colore e lo stesso preambolo, ha lo stesso file.
QString pathOf(const Project &project, const QString &text, Kind kind, const QString &color);
// Fa le rese che mancano, tutte insieme: una compilazione sola, con il preambolo del progetto precompilato
// (`build/resa/preambolo-….fmt`, rifatto quando cambia uno stile); se qualcosa non compila, si divide a metà finché
// non si trova (un pezzo che non compila non ferma gli altri). Restituisce i file fatti; quelli che non compilano
// lasciano un file «.no», così non si riprovano.
QStringList render(const Project &project, const QList<Piece> &pieces);
QStringList render(const Project &project, const QStringList &texts, Kind kind, const QString &color);
// La prosa pronta per LaTeX (pubblica per le prove).
QString prose(const QString &text);
// Il pezzo da leggere subito, mentre la resa si fa (o se non compila): testo ricco di Qt, con la matematica in
// simboli, pedici ed esponenti, il grassetto e il corsivo, gli elenchi, i riquadri con il loro nome. Non è il PDF,
// ma non è il sorgente.
QString preview(const QString &text, Kind kind);

}
