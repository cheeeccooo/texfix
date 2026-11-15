#pragma once
#include <QList>
#include <QString>
#include <QStringList>

// Le regole editoriali: come l'utente vuole i suoi appunti. Una gerarchia sola: le predefinite del programma
// (regole/editoriali/, dentro il programma), le Preferenze (una sezione alla volta, per tutti i progetti), le regole
// del progetto (Settings::rules). Le regole strutturali, senza cui il programma non funziona, stanno nel codice.
namespace tx::rules {

struct Section {
    QString id, title, text;
    bool custom = false;                   // cambiata nelle Preferenze
};

QList<Section> sections();                 // in ordine di lettura, con le Preferenze applicate
Section section(const QString &id);
QString defaultText(const QString &id);
bool setText(const QString &id, const QString &text);   // uguale alla predefinita: torna predefinita
void reset(const QString &id);

// Il blocco delle regole editoriali per un prompt: le sezioni chieste, poi le regole del progetto, che valgono di più.
QString forPrompt(const QStringList &ids, const QStringList &projectRules);

// Dove stanno le sezioni cambiate nelle Preferenze (per le prove: una cartella a parte).
void setHome(const QString &dir);

}
