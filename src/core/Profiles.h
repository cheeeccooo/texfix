#pragma once
#include "core/Project.h"

#include <QStringList>

// I profili di partenza: le scelte di un progetto (lo stile con la sua descrizione, le impostazioni, le regole del
// progetto) salvate con un nome, da cui un progetto nuovo può partire. Stanno fra i dati del programma, una cartella
// per profilo. Le regole editoriali delle Preferenze valgono già per tutti i progetti e non si copiano.
namespace tx::profiles {

QStringList list();
// Salva le scelte del progetto come profilo (uno con lo stesso nome viene sostituito). Vuoto se è andata.
QString save(const Project &project, const QString &name);
// Porta in un progetto le scelte del profilo: lo stile (il .sty e la descrizione), le impostazioni, le regole.
QString apply(const Project &project, const QString &name);
void remove(const QString &name);
void setHome(const QString &dir);          // per le prove: una cartella a parte

}
