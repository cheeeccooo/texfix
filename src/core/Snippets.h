#pragma once
#include "core/Project.h"

#include <QString>
#include <QStringList>

// La resa compilata di piccoli pezzi (una domanda, una lettura, un'aggiunta) per i pannelli Rivedi: come nel PDF, con
// il preambolo del progetto, su fondo trasparente e nel colore del testo del tema. Una resa fatta resta in
// build/resa/; un pezzo che non compila non ha resa, e chi lo mostra tiene il testo.
namespace tx::snippets {

// Come va letto un pezzo: prosa (con la matematica fra $…$; «ω0» e «V_CB» diventano matematica) o LaTeX com'è scritto
// in una lezione (se da solo non compila, lo si prova come formula).
enum class Kind { Prose, Latex };

// Dove sta (o starà) la resa di un pezzo: lo stesso pezzo, con lo stesso colore e lo stesso preambolo, ha lo stesso file.
QString pathOf(const Project &project, const QString &text, Kind kind, const QString &color);
// Fa le rese che mancano, tutte insieme (una compilazione; se qualcosa non compila, una per pezzo). Restituisce i
// file fatti; quelli che non compilano lasciano un file «.no», così non si riprovano.
QStringList render(const Project &project, const QStringList &texts, Kind kind, const QString &color);
// La prosa pronta per LaTeX (pubblica per le prove).
QString prose(const QString &text);

}
