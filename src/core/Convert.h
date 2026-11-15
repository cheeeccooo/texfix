#pragma once
#include "core/Job.h"
#include "core/Project.h"
#include "core/Settings.h"
#include "core/Style.h"

#include <QHash>
#include <QJsonObject>

namespace tx {

// Converti: ogni fonte di «Da convertire» diventa una lezione nuova. Per ogni fonte: le pagine, la scaletta, la
// trascrizione a blocchi con la verifica, l'assemblaggio con i marcatori dei blocchi, la lezione nel documento, la
// fonte fra quelle usate. Gira nel thread di chi la chiama (un thread di lavoro), dentro il programma.
//
// Ciò che è fatto e pagato resta in build/lavori/converti/: un lavoro interrotto, rilanciato, lo riusa.
class Convert {
public:
    struct Lesson {
        QString source, file, title, date;             // la fonte (nome), «src/src4.tex»
        int number = 0, doubts = 0, figures = 0, corrections = 0;
        int layoutDone = 0, layoutOpen = 0;            // mosse dell'impaginazione fatte, e proposte da rivedere
        int errors = 0, rows = 0;                      // ciò che resta dopo la sistemazione
        QString failure;                               // perché questa fonte non è diventata una lezione
    };
    struct Result {
        QList<Lesson> lessons;
        double cost = 0, minutes = 0;
        bool stopped = false;
        QString failure;                               // il lavoro si è interrotto (limite di utilizzo, un errore)
        int written() const;
    };
    struct Source {
        QString file, kind;                            // «fonti/coda/x.pdf»; pagine | testo | tex | altro
        int size = 0;                                  // pagine, o righe di testo
    };

    Convert(const Project &project, const Settings &settings, const Style &style, JobControl *control);

    static QList<Source> queue(const Project &project);
    // Quanto costa all'incirca convertire queste fonti, in dollari.
    static double estimate(const QString &quality, const QList<Source> &sources);
    // I lavori interrotti: le fonti di un Converti che non è arrivato in fondo (vuoto se non ce n'è).
    static QStringList interrupted(const Project &project);

    struct Wanted {                                    // numero e data scelti dall'utente per una fonte (Nuova lezione)
        int number = 0;
        QString date;
    };
    Result run(const QStringList &sources, const QHash<QString, Wanted> &wanted = {});

    // I dubbi di una lezione stanno in salvataggi/dubbi/<nome>.json.
    static QString doubtsFile(const QString &lessonFile);

private:
    struct Work;
    void plan(Work &work);
    QJsonObject chunk(const Work &work, int k, class Model &model) const;
    void assemble(Work &work) const;
    QString workDir(const QString &rel = {}) const;

    Project m_project;
    Settings m_settings;
    Style m_style;
    JobControl *m_control;
};

}
