#pragma once
#include "core/Project.h"

#include <QList>

// Il version control del progetto: lo usa solo l'utente, dalla scheda accanto a Progetto. Il programma chiama `git`.
namespace tx::git {

struct Change {
    QString file, state;                   // modificato | nuovo | tolto | rinominato
};
struct Entry {
    QString hash, when, subject;
};
bool hasRepository(const Project &project);
bool init(const Project &project);
QList<Change> changes(const Project &project);
QString diff(const Project &project, const QString &file);         // le differenze di un file rispetto all'ultimo commit
QString show(const Project &project, const QString &hash);         // un commit: che cosa ha cambiato
// Un commit con tutte le modifiche, o solo con i file dati. Vuoto se è andata; altrimenti il perché.
QString commit(const Project &project, const QString &message, const QStringList &files = {});
QList<Entry> log(const Project &project, int limit = 100);

}
