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
// Il limite di utilizzo con un lavoro: il lavoro finisce ciò che sta facendo e si ferma (è anche Stopped), da riprendere
// con un clic quando il limite è passato (JobControl::pausedUntil dice quando).
struct LimitPause : Stopped {
    const char *what() const noexcept override { return "limite di utilizzo di Claude raggiunto, o connessione assente"; }
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

    // Il limite di utilizzo non è un errore: chi lo incontra lo dice (pause, con l'ora in cui si può riprendere). Da lì
    // nessuna chiamata nuova parte (checkLimit lancia LimitPause), quelle in corso finiscono, e il lavoro si ferma da sé
    // come se fosse stato fermato: si riprende con un clic. onPause: l'ora in cui si può riprendere.
    // La connessione che manca è come il limite (connection): ci si ferma allo stesso modo, ma si può riprendere subito.
    void pause(const QDateTime &until, const QString &message, bool connection = false);
    void checkLimit() const;                           // lancia LimitPause se il limite è stato raggiunto
    QDateTime pausedUntil() const;
    bool pausedForConnection() const { return m_pauseConnection.load(); }
    std::function<void(const QDateTime &, bool connection)> onPause;

    // Le chiamate che aspettano che la connessione torni (Model::awaitOnline): onOffline dice quando comincia la prima
    // e quando finisce l'ultima.
    void waitingOnline(bool on);
    bool offline() const { return m_waitingOnline.load() > 0; }
    std::function<void(bool)> onOffline;

    // Gli eventi per chi guarda il lavoro: una chiamata al modello che parte, scrive, finisce; un cantiere (una lezione
    // che si scrive, una lezione del corso che l'agente completa) che passa di fase. records: dove ogni chiamata lascia
    // il suo registro (istruzioni, parti, risposta); vuoto: da nessuna parte.
    void event(const QJsonObject &e) const
    {
        if (onEvent)
            onEvent(e);
    }
    std::function<void(const QJsonObject &)> onEvent;
    // Il numero della chiamata (o del passo) che parte; un lavoro ripreso continua dopo quelli che ha già (callsAfter),
    // perché i registri della volta prima restino.
    int nextCall() { return ++m_calls; }
    void callsAfter(int last) { m_calls.store(qMax(m_calls.load(), last)); }
    QString records;

private:
    std::atomic<bool> m_stopped{false};
    std::atomic<qint64> m_pausedUntil{0};              // millisecondi dall'epoca; 0: nessuna pausa
    std::atomic<bool> m_pauseConnection{false};
    std::atomic<int> m_waitingOnline{0};
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

// Un passo di un lavoro che non è una chiamata al modello (una compilazione, una prova, la preparazione dei pezzi): per
// chi guarda, con il cantiere del thread. Parte quando nasce e finisce con done(); se muore prima (un'eccezione, «Ferma»)
// è «fermato». Un passo che copre le compilazioni (covers) le tiene per sé: quelle fatte dentro non sono passi a parte.
// Senza un JobControl non fa niente.
class Step {
public:
    Step(JobControl *control, const QString &text, bool covers = false);
    ~Step();
    Step(const Step &) = delete;
    Step &operator=(const Step &) = delete;
    void done(const QString &outcome = {}, bool ok = true);
    static bool covered();                             // questo thread è dentro un passo che copre le compilazioni

private:
    void send(const QString &state, const QString &outcome) const;
    JobControl *m_control;
    int m_id = 0;
    QString m_text, m_site;
    qint64 m_start = 0;
    bool m_open = false, m_covers, m_previous = false;
};

}
