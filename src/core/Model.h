#pragma once
#include "core/Job.h"

#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

namespace tx {

// Le chiamate al modello: ognuna è un `claude -p` senza strumenti e senza impostazioni, con il solo contesto che le
// si dà (Claude Code locale, con l'abbonamento dell'utente). Al più PARALLEL insieme, in tutto il programma.
class Model {
public:
    static constexpr int PARALLEL = 6;

    struct Call {                                      // una chiamata fatta: che cosa è costata
        QString stage, tag, model, effort;
        double seconds = 0, cost = 0;
        int input = 0, output = 0;
        bool ok = false;
    };

    // quality: veloce | bilanciata | massima (Settings::quality). directory: dove gira il processo.
    Model(const QString &quality, const QString &directory, JobControl *control);

    // Una chiamata. stage dice quale modello e quanto impegno (outline, transcribe, verify, figure, fix, decide…);
    // parts sono testi e percorsi di .png, nell'ordine in cui il modello li vede. Lancia Stopped, LimitReached o
    // std::runtime_error.
    QString ask(const QString &stage, const QString &system, const QStringList &parts, const QString &tag = {});

    QList<Call> calls() const;
    double cost() const;

    static QString modelFor(const QString &quality, const QString &stage);
    static QString effortFor(const QString &quality, const QString &stage);
    // Il blocco ```lang di una risposta (vuoto se manca) e il suo JSON (lancia std::runtime_error se non si legge).
    static QString block(const QString &text, const QString &lang);
    static QJsonValue json(const QString &text);

private:
    QString m_quality, m_directory;
    JobControl *m_control;
    mutable QMutex m_mutex;
    QList<Call> m_calls;
};

}
