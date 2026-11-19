#pragma once
#include "core/Job.h"
#include "core/Project.h"
#include "core/Settings.h"
#include "core/Style.h"

#include <QHash>
#include <QJsonObject>

namespace tx {

// La conversione: una fonte in arrivo diventa una lezione nuova (da «Avvia» di Aggiungi materiale). Per ogni fonte:
// le pagine, la scaletta, la trascrizione a blocchi con la verifica, l'assemblaggio con i marcatori dei blocchi, la
// lezione nel documento, la fonte fra quelle usate. Gira nel thread di chi la chiama (un thread di lavoro), dentro il
// programma.
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
    // I lavori interrotti: le fonti di una conversione che non è arrivata in fondo, con le scelte del piano.
    struct Wanted;
    static QStringList interrupted(const Project &project, QHash<QString, Wanted> *wanted = nullptr);

    struct Wanted {                                    // numero e data scelti nel piano di Aggiungi materiale
        int number = 0;
        QString date;
        // in un progetto per argomento: in fondo all'argomento `topic`, o un argomento nuovo `title` dopo `after`
        QString topic, title, after;
        QString note;                                  // ciò che il corso spiega già: si richiama, non si riscrive
        QStringList group;                             // gli argomenti nuovi del piano con lo stesso `after`, in ordine
        int rank = -1;                                 // il posto di questo fra loro (reorganize::anchor)
        bool byTopic() const { return !topic.isEmpty() || !title.isEmpty(); }
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
