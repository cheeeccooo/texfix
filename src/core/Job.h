#pragma once
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QMutex>
#include <QString>

#include <atomic>
#include <functional>
#include <stdexcept>

namespace tx {

// Un lavoro fermato dall'utente: chi lavora lo lancia appena se ne accorge, e niente resta a metà.
struct Stopped : std::exception {
    const char *what() const noexcept override { return "fermato"; }
};
// Il modello ha raggiunto il limite di utilizzo e nessun lavoro può aspettarlo (una chiamata senza JobControl): until
// è l'ora in cui riprende, se il messaggio la dice.
struct LimitReached : std::runtime_error {
    explicit LimitReached(const std::string &what, const QDateTime &until = {}) : std::runtime_error(what), until(until) {}
    QDateTime until;
};

// Ciò che un lavoro in corso divide con chi lo guarda: «Ferma», l'avanzamento, il registro, gli eventi delle chiamate e
// dei cantieri, la pausa per il limite di utilizzo. Il lavoro gira in un thread suo (e nei thread che apre); le
// funzioni si possono chiamare da qualunque thread.
class JobControl {
public:
    void stop();                                       // ferma subito: chiude anche i processi in corso
    bool stopped() const { return m_stopped.load(); }
    void check() const                                 // lancia Stopped se il lavoro è stato fermato
    {
        if (stopped())
            throw Stopped();
    }
    void track(qint64 pid);                            // un processo del lavoro, da chiudere con «Ferma»
    void untrack(qint64 pid);

    void progress(double fraction, const QString &label) const
    {
        if (onProgress)
            onProgress(fraction, label);
    }
    void log(const QString &line) const
    {
        if (onLog)
            onLog(line);
    }
    std::function<void(double, const QString &)> onProgress;
    std::function<void(const QString &)> onLog;

    // Il limite di utilizzo non è un errore: chi lo incontra lo dice (pause, con l'ora di ripresa), e il lavoro intero
    // si ferma lì. Chi sta per chiamare il modello aspetta (waitLimit) finché l'ora non arriva, o finché qualcuno non
    // dice di riprendere subito (wake); «Ferma» interrompe anche l'attesa. onPause: l'ora di ripresa, o vuota quando
    // riprende.
    void pause(const QDateTime &until, const QString &message);
    void waitLimit();                                  // lancia Stopped
    QDateTime pausedUntil() const;
    void wake();
    std::function<void(const QDateTime &)> onPause;

    // Gli eventi per chi guarda il lavoro: una chiamata al modello che parte, scrive, finisce; un cantiere (una lezione
    // che si scrive, una lezione del corso che l'agente completa) che passa di fase. records: dove ogni chiamata lascia
    // il suo registro (istruzioni, parti, risposta); vuoto: da nessuna parte.
    void event(const QJsonObject &e) const
    {
        if (onEvent)
            onEvent(e);
    }
    std::function<void(const QJsonObject &)> onEvent;
    int nextCall() { return ++m_calls; }
    QString records;

private:
    std::atomic<bool> m_stopped{false};
    std::atomic<qint64> m_pausedUntil{0};              // millisecondi dall'epoca; 0: nessuna pausa
    std::atomic<int> m_calls{0};
    QMutex m_mutex;
    QList<qint64> m_pids;
};

// Il cantiere su cui lavora questo thread: le chiamate al modello che partono da qui sono sue. Vale finché l'oggetto
// vive; un thread nuovo non ne ha uno (chi lo apre ne mette uno).
class Site {
public:
    explicit Site(const QString &key);
    ~Site();
    Site(const Site &) = delete;
    Site &operator=(const Site &) = delete;
    static QString current();

private:
    QString m_previous;
};

}
