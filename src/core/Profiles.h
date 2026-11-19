#pragma once
#include "core/Project.h"
#include "core/Rules.h"

#include <QStringList>

// I profili: le regole editoriali di un corso (rules::Profile), un file Markdown ciascuno.
// - I predefiniti (Neutro, Scientifico, Umanistico) stanno nel programma (profili/) e non si cambiano.
// - La libreria dell'utente sta fra i dati del programma (profili/<nome>.profilo.md).
// - Un progetto ne ha una copia (Project::PROFILE): cambiata, è il profilo di quel progetto; la libreria e gli altri
//   progetti non ne sanno niente finché non la si salva nella libreria. Nella libreria, predefiniti compresi, due
//   profili non hanno mai lo stesso nome.
namespace tx::profiles {

QStringList builtin();                     // i nomi dei predefiniti, in ordine
QStringList library();                     // i nomi di quelli dell'utente
bool exists(const QString &name);          // fra i predefiniti o nella libreria
rules::Profile load(const QString &name, bool *ok = nullptr);

rules::Profile ofProject(const Project &project);
// Il profilo `name` (predefinito o della libreria) diventa la copia del progetto. Vuoto se è andata.
QString setForProject(const Project &project, const QString &name);
// La copia del progetto, cambiata (dalla finestra del profilo, dalle regole ricordate in Rivedi, dall'agente con il sì
// dell'utente). Vuoto se è andata.
QString saveToProject(const Project &project, const rules::Profile &profile);
// La copia del progetto va nella libreria con il nome `name`, che deve essere libero (o, con replace, di un profilo
// dell'utente); la copia prende quel nome. Vuoto se è andata.
QString saveToLibrary(const Project &project, const QString &name, bool replace = false);
QString remove(const QString &name);       // solo quelli dell'utente
// Un file .profilo.md nella libreria, con il nome che ha dentro (o del file); il nome deve essere libero.
QString importFile(const QString &path);
QString exportFile(const Project &project, const QString &path);
// Il profilo di un progetto di prima (myprofile) va anche nella libreria, se non c'è.
void keepMine(const Project &project);

void setHome(const QString &dir);          // per le prove: una cartella a parte

}
