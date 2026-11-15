#pragma once
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
// Il modello ha raggiunto il limite di utilizzo: il lavoro si interrompe e si riprende più tardi.
struct LimitReached : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Ciò che un lavoro in corso divide con chi lo guarda: «Ferma», l'avanzamento, il registro. Il lavoro gira in un
// thread suo; le funzioni si possono chiamare da qualunque thread.
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

private:
    std::atomic<bool> m_stopped{false};
    QMutex m_mutex;
    QList<qint64> m_pids;
};

}
