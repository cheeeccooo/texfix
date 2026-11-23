#pragma once
#include <QJsonObject>
#include <QJsonValue>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QObject>
#include <QProcess>
#include <QQmlEngine>
#include <QVariantList>

// La chat con l'agente: Claude Code locale (il CLI `claude`), chiuso nel progetto. Gli strumenti di Texfix glieli dà
// il programma: il CLI lancia `Texfix --strumenti <nome>`, un passacarte che porta le sue richieste a questo processo
// su un canale locale (nessuna porta di rete); le esegue il motore, sul progetto aperto.
class Chat : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // I messaggi: role (user | assistant | tool | note), text.
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY modelChanged)   // sonnet | opus
    // Quanto l'agente fa da solo, come in Claude Code: auto (decide lui che cosa è sicuro, chiede per il resto),
    // chiedi (chiede conferma prima di ogni modifica), autonomo (non chiede mai). Vale per tutti i progetti.
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(bool waiting READ waiting NOTIFY waitingChanged)
    Q_PROPERTY(QString title READ title NOTIFY messagesChanged)          // il nome della chat           // aspetta una conferma dell'utente

public:
    static Chat *instance();
    static Chat *create(QQmlEngine *, QJSEngine *);
    // Il passacarte: `Texfix --strumenti <nome>` porta righe da stdin al programma aperto e le risposte a stdout.
    static int relay(const QString &serverName);

    QVariantList messages() const { return m_messages; }
    bool running() const { return m_process.state() != QProcess::NotRunning; }
    QString model() const { return m_model; }
    void setModel(const QString &model);
    QString mode() const { return m_mode; }
    void setMode(const QString &mode);
    bool waiting() const { return m_asking != nullptr; }
    Q_INVOKABLE void answer(bool allow);               // la risposta a una richiesta di conferma

    // Un messaggio, con i file allegati (percorsi sul disco): si copiano nel progetto, dove l'agente li legge.
    Q_INVOKABLE void send(const QString &text, const QStringList &attachments = {});
    Q_INVOKABLE void stop();
    Q_INVOKABLE void newChat();
    // Una procedura apre una chat sua, con il suo nome.
    Q_INVOKABLE void startProcedure(const QString &title, const QString &message);
    // Lo storico del progetto: id, title, when, current. openChat riprende una chat; removeChat la butta.
    Q_INVOKABLE QVariantList chats() const;
    Q_INVOKABLE void openChat(const QString &id);
    Q_INVOKABLE void removeChat(const QString &id);
    QString title() const { return m_title; }
    // Dove salvare una foto da allegare (un file nuovo fra quelli che si rifanno).
    Q_INVOKABLE QString shotPath() const;

signals:
    void messagesChanged();
    void runningChanged();
    void modelChanged();
    void modeChanged();
    void waitingChanged();

private:
    Chat();
    void readOutput();
    void finished();
    void add(const QString &role, const QString &text, const QString &detail = {});
    void launch(const QString &text);
    // il messaggio che il limite ha fermato, di nuovo, nella stessa chat (id): senza ripeterlo fra i messaggi
    void resend(const QString &id, const QString &text);
    void appendToAssistant(const QString &text);
    void save() const;
    void load();

    QProcess m_process;
    QLocalServer m_tools;
    QByteArray m_buffer;
    QVariantList m_messages;
    QString m_model = "sonnet", m_session, m_id, m_title;
    QString m_lastText;                                // l'ultimo messaggio mandato: se la chat non si riprende, riparte con la conversazione
    bool m_retried = false, m_answered = false;
    QString m_mode;
    QPointer<QLocalSocket> m_asking;                   // chi aspetta la conferma, la sua richiesta e che cosa voleva fare
    QJsonValue m_askingId;
    QJsonObject m_askingInput;
    bool m_streamed = false;                           // il testo di questo messaggio è già arrivato a pezzi
};
