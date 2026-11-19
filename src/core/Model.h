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
    // parts sono testi e percorsi di .png, nell'ordine in cui il modello li vede. Con un JobControl il limite di
    // utilizzo ferma il lavoro (JobControl::pause, LimitPause) e la chiamata si rifà alla ripresa; senza, LimitReached. Lancia anche Stopped
    // o std::runtime_error. Con un JobControl manda gli eventi della chiamata e, se c'è dove, ne lascia il registro.
    QString ask(const QString &stage, const QString &system, const QStringList &parts, const QString &tag = {});

    QList<Call> calls() const;
    double cost() const;

    static QString modelFor(const QString &quality, const QString &stage);
    static QString effortFor(const QString &quality, const QString &stage);
    // Il blocco ```lang di una risposta (vuoto se manca) e il suo JSON (lancia std::runtime_error se non si legge).
    static QString block(const QString &text, const QString &lang);
    static QJsonValue json(const QString &text);
    // Un messaggio di limite di utilizzo raggiunto (della chiamata o dell'agente) e l'ora in cui riprende.
    static bool limit(const QString &text, QDateTime *until = nullptr);
    // Quando, per chi legge: «alle 14:30», «domani alle 02:21», «il 21/11 alle 09:00».
    static QString when(const QDateTime &at);
    // La connessione. offline: un messaggio di rete che manca («Connection error», «ENOTFOUND»…); online: api.anthropic.com
    // risponde (curl, al più 8 s; TEXFIX_RETE=assente|presente lo decide nelle prove). awaitOnline: aspetta che torni,
    // guardando ogni 15 s, al più maxMs; false se non torna, se il lavoro si ferma per un altro motivo o senza un lavoro.
    // Lancia Stopped. Una chiamata senza rete aspetta lì e si rifà; dopo OFFLINE_WAIT il lavoro si ferma come per il limite.
    static constexpr qint64 OFFLINE_WAIT = 10 * 60 * 1000;
    static bool offline(const QString &text);
    static bool online();
    static bool awaitOnline(JobControl *control, qint64 maxMs = OFFLINE_WAIT);

private:
    QString m_quality, m_directory;
    JobControl *m_control;
    mutable QMutex m_mutex;
    QList<Call> m_calls;
};

}
