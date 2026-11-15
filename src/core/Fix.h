#pragma once
#include "core/Model.h"
#include "core/Project.h"
#include "core/Settings.h"
#include "core/Style.h"

// Errori di compilazione e righe fuori misura di una lezione. Gli errori li sistema il modello; per le righe prima le
// mosse che non costano (spezzare il paragrafo a una fine di frase), poi il modello, che riscrive il paragrafo in più
// varianti. Ogni mossa si misura compilando: resta solo se toglie avvisi senza aggiungerne.
namespace tx::fix {

struct Result {
    int errorsBefore = 0, errorsAfter = 0;
    int rowsBefore = 0, rowsAfter = 0;
    int byMoves = 0, byModel = 0;          // righe sistemate senza modello, e dal modello
    QString failure;                       // la lezione non si è potuta compilare
    bool changed() const { return errorsAfter != errorsBefore || rowsAfter != rowsBefore; }
};

Result lesson(const Project &project, const Settings &settings, const Style &style, Model &model, const QString &file, JobControl *control);

// Il paragrafo di testo che contiene le righe first..last (da 1): la prima e l'ultima riga; {0, 0} se non è testo.
std::pair<int, int> paragraph(const QStringList &lines, int first, int last);

}
