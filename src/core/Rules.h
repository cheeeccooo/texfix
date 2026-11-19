#pragma once
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

// Le regole che arrivano a chi scrive, in due parti.
// - Le strutturali (regole/strutturali/, dentro il programma): ciò che serve a Texfix per funzionare (sorgente,
//   rimandi, larghezza, figure, aggiunte e correzioni, impaginazione). Uguali per ogni corso, non si cambiano.
// - Il profilo: le regole editoriali di un corso (come si scrive, riquadri, controlli, notazione, figure della materia)
//   e le regole in più. Un progetto ne ha una copia (core/Profiles); qui c'è il suo formato.
namespace tx::rules {

struct Section {
    QString id, title, text;
};

// Le strutturali, in ordine di lettura; vuota se l'id non è di una strutturale.
QList<Section> structural();
Section structuralSection(const QString &id);

// Le sezioni di un profilo: id e titolo, in ordine.
struct Heading {
    const char *id, *title;
};
const QList<Heading> &profileSections();
// Tutti gli argomenti, per chi le legge tutte (l'agente): ognuno porta la sua strutturale e la sua sezione del profilo.
QStringList allIds();

// Un profilo, in un file Markdown: «# Nome», poi una sezione «## Titolo» per ognuna di profileSections(), e «## Regole
// in più» con una regola per voce («- …»). Una sezione che manca è vuota.
struct Profile {
    QString name;
    QMap<QString, QString> sections;       // id → testo
    QStringList extra;                     // le regole in più: valgono più di tutte le altre
    bool operator==(const Profile &) const = default;
    static Profile parse(const QString &markdown, bool *ok = nullptr);
    QString markdown() const;
};

// Il blocco delle regole per un prompt: per ogni id la strutturale e la sezione del profilo, poi le regole in più.
QString forPrompt(const QStringList &ids, const Profile &profile);

}
